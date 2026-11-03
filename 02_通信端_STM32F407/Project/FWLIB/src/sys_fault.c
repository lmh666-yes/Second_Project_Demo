#include "sys_fault.h"
/* CPU 故障捕获与诊断，实现文件 */
#include <stddef.h>     /* NULL：回调缺省判断 */

/* 全局故障现场，调试器 Watch 窗口观察 */
volatile SYS_FAULT_Record_t SYS_FAULT_Record;

/* 故障报警回调（SYS_FAULT_SetCallback 注册，缺省为空） */
static void (*sys_fault_callback)(void) = NULL;


/* 故障类型转名称，ASCII，供 Report 使用 */
static const char *fault_type_name(uint32_t type)
{
    switch (type) {
        case SYS_FAULT_TYPE_NMI:   return "NMI";
        case SYS_FAULT_TYPE_HARD:  return "HardFault";
        case SYS_FAULT_TYPE_MEM:   return "MemManage";
        case SYS_FAULT_TYPE_BUS:   return "BusFault";
        case SYS_FAULT_TYPE_USAGE: return "UsageFault";
        default:                   return "None";
    }
}

/* 故障原因位表：reg 0 = CFSR（含 MMFSR/BFSR/UFSR），1 = HFSR
 * 名称全部 ASCII（AC5 对中文字符串支持差，保持 ASCII 才安全） */
typedef struct {
    uint8_t     reg;
    uint8_t     bit;
    const char *name;
} FaultCause_t;

static const FaultCause_t fault_cause_tbl[] = {
    { 0U,  0U, "IACCVIOL"   },   /* 取指访问违规（MPU）        */
    { 0U,  1U, "DACCVIOL"   },   /* 数据访问违规（MPU）        */
    { 0U,  3U, "MUNSTKERR"  },   /* 异常返回时出栈失败（内存）  */
    { 0U,  4U, "MSTKERR"    },   /* 进入异常时入栈失败（内存）  */
    { 0U,  5U, "MLSPERR"    },   /* 惰性保存浮点现场失败        */
    { 0U,  8U, "IBUSERR"    },   /* 取指总线错误（不精确）      */
    { 0U,  9U, "PRECISERR"  },   /* 数据访问总线错误（精确）      */
    { 0U, 10U, "IMPRECISERR" },  /* 数据访问总线错误（写缓冲）  */
    { 0U, 11U, "UNSTKERR"   },   /* 返回时出栈总线错误          */
    { 0U, 12U, "STKERR"     },   /* 入栈总线错误（栈溢出常见）    */
    { 0U, 13U, "LSPERR"     },   /* 浮点惰性保存总线错误        */
    { 0U, 16U, "UNDEFINSTR" },   /* 未定义指令                  */
    { 0U, 17U, "INVSTATE"   },   /* 非法状态（如跳转丢失 Thumb 位）*/
    { 0U, 18U, "INVPC"      },   /* 异常返回状态非法            */
    { 0U, 19U, "NOCP"       },   /* 协处理器不存在（无 FPU 却用浮点）*/
    { 0U, 24U, "UNALIGNED"  },   /* 非对齐访问（需开捕捉）      */
    { 0U, 25U, "DIVBYZERO"  },   /* 除零（需开捕捉）            */
    { 1U,  1U, "VECTTBL"    },   /* 取向量表失败（向量表被破坏）*/
    { 1U, 30U, "FORCED"     },   /* 被细分故障升级上来的 HardFault */
    { 1U, 31U, "DEBUGEVT"   },   /* 调试事件                    */
};

#define FAULT_CAUSE_COUNT  (sizeof(fault_cause_tbl) / sizeof(fault_cause_tbl[0]))

/* 编译期断言：表项数固定 20，增删条目时此处报错 */
typedef char fault_cause_count_check[(FAULT_CAUSE_COUNT == 20U) ? 1 : -1];

/* 向缓冲区追加字符串：max 为可用字符数（不含结尾 '\0'） */
static uint32_t fault_put_str(char *buf, uint32_t max, uint32_t pos, const char *s)
{
    while ((*s != '\0') && (pos < max)) {
        buf[pos++] = *s++;
    }
    return pos;
}

/* 向缓冲区追加 32 位十六进制，形如 0x????????，大写 */
static uint32_t fault_put_hex(char *buf, uint32_t max, uint32_t pos, uint32_t val)
{
    int32_t i;

    pos = fault_put_str(buf, max, pos, "0x");
    for (i = 28; i >= 0; i -= 4) {
        uint32_t nib = (val >> (uint32_t)i) & 0xFU;
        char     c   = (nib < 10U) ? (char)('0' + nib)
                                   : (char)('A' + nib - 10U);
        if (pos < max) buf[pos++] = c;
    }
    return pos;
}


/* 异常入口（汇编包装）
 * 进入异常时 LR 保存的是 EXC_RETURN：bit2 = 1 现场压在 PSP（RTOS 任务），
 * bit2 = 0 现场压在 MSP（裸机或中断上下文）。现场指针放 R0、故障类型放 R1、
 * EXC_RETURN 放 R2，尾调用 C 处理函数。
 * 本组入口均为 __weak：自定义同名 NMI_Handler / HardFault_Handler 等会顶替库版
 * （顶替后本模块的记录随之停用）。函数名由异常向量表按符号名链接，不可改。
 * 汇编部分为 ARM Compiler 5（__asm）写法，AC6 需改写。 */
void sys_fault_c_handler(uint32_t *frame, uint32_t type, uint32_t exc_return);

__weak __asm void NMI_Handler(void)
{
    IMPORT  sys_fault_c_handler
    MOV     R2, LR              ; save EXC_RETURN
    TST     R2, #4
    ITE     EQ
    MRSEQ   R0, MSP             ; frame on MSP
    MRSNE   R0, PSP             ; frame on PSP
    MOV     R1, #1              ; SYS_FAULT_TYPE_NMI
    B       sys_fault_c_handler
}

__weak __asm void HardFault_Handler(void)
{
    IMPORT  sys_fault_c_handler
    MOV     R2, LR
    TST     R2, #4
    ITE     EQ
    MRSEQ   R0, MSP
    MRSNE   R0, PSP
    MOV     R1, #2              ; SYS_FAULT_TYPE_HARD
    B       sys_fault_c_handler
}

__weak __asm void MemManage_Handler(void)
{
    IMPORT  sys_fault_c_handler
    MOV     R2, LR
    TST     R2, #4
    ITE     EQ
    MRSEQ   R0, MSP
    MRSNE   R0, PSP
    MOV     R1, #3              ; SYS_FAULT_TYPE_MEM
    B       sys_fault_c_handler
}

__weak __asm void BusFault_Handler(void)
{
    IMPORT  sys_fault_c_handler
    MOV     R2, LR
    TST     R2, #4
    ITE     EQ
    MRSEQ   R0, MSP
    MRSNE   R0, PSP
    MOV     R1, #4              ; SYS_FAULT_TYPE_BUS
    B       sys_fault_c_handler
}

__weak __asm void UsageFault_Handler(void)
{
    IMPORT  sys_fault_c_handler
    MOV     R2, LR
    TST     R2, #4
    ITE     EQ
    MRSEQ   R0, MSP
    MRSNE   R0, PSP
    MOV     R1, #5              ; SYS_FAULT_TYPE_USAGE
    B       sys_fault_c_handler
}


/* 故障现场处理（C 部分） */
/* 由上面的汇编包装调用，不可改名、不可加 static
 * frame 布局：R0 R1 R2 R3 R12 LR PC xPSR，硬件自动压栈顺序 */
void sys_fault_c_handler(uint32_t *frame, uint32_t type, uint32_t exc_return)
{
    uint32_t cfsr;

    /* 1) 记录出错现场，重复进入时覆盖为最新现场 */
    SYS_FAULT_Record.type     = type;
    SYS_FAULT_Record.from_psp = (exc_return & 0x4U) ? 1U : 0U;
    SYS_FAULT_Record.r0       = frame[0];
    SYS_FAULT_Record.r1       = frame[1];
    SYS_FAULT_Record.r2       = frame[2];
    SYS_FAULT_Record.r3       = frame[3];
    SYS_FAULT_Record.r12      = frame[4];
    SYS_FAULT_Record.lr       = frame[5];
    SYS_FAULT_Record.pc       = frame[6];
    SYS_FAULT_Record.psr      = frame[7];

    /* 2) 记录故障状态寄存器，地址类寄存器仅在有效时取值 */
    cfsr = SCB->CFSR;
    SYS_FAULT_Record.cfsr  = cfsr;
    SYS_FAULT_Record.hfsr  = SCB->HFSR;
    SYS_FAULT_Record.mmfar = (cfsr & (1UL << 7))  ? SCB->MMFAR : 0U;  /* MMARVALID */
    SYS_FAULT_Record.bfar  = (cfsr & (1UL << 15)) ? SCB->BFAR  : 0U;  /* BFARVALID */
    SYS_FAULT_Record.count++;

    /* 3) 调用用户报警回调（可选，回调保持短小） */
    if (sys_fault_callback != NULL) {
        sys_fault_callback();
    }

#if SYS_FAULT_AUTO_RESET
    /* 4) 自动复位模式：记录已留档，复位重启 */
    NVIC_SystemReset();
#endif

    /* 5) 停在此处，等待调试器读取 SYS_FAULT_Record */
    for (;;) {
    }
}


/* 初始化：清零记录，按宏开启故障细分与除零捕获 */
void SYS_FAULT_Init(void)
{
    SYS_FAULT_Clear();

#if SYS_FAULT_ENABLE_SPLIT
    /* 把 MemManage / BusFault / UsageFault 从 HardFault 细分出来 */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk
               |  SCB_SHCSR_BUSFAULTENA_Msk
               |  SCB_SHCSR_USGFAULTENA_Msk;
#endif

#if SYS_FAULT_DIV0_TRAP
    /* 开启除零捕捉，默认关闭，见头文件说明 */
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
#endif
}

/* 注册故障报警回调（传 0 取消） */
void SYS_FAULT_SetCallback(void (*callback)(void))
{
    sys_fault_callback = callback;
}

/* 是否发生过故障 */
uint8_t SYS_FAULT_Happened(void)
{
    return (SYS_FAULT_Record.type != SYS_FAULT_TYPE_NONE) ? 1U : 0U;
}

/* 清空记录 */
void SYS_FAULT_Clear(void)
{
    SYS_FAULT_Record.type     = SYS_FAULT_TYPE_NONE;
    SYS_FAULT_Record.from_psp = 0U;
    SYS_FAULT_Record.pc       = 0U;
    SYS_FAULT_Record.lr       = 0U;
    SYS_FAULT_Record.psr      = 0U;
    SYS_FAULT_Record.r0       = 0U;
    SYS_FAULT_Record.r1       = 0U;
    SYS_FAULT_Record.r2       = 0U;
    SYS_FAULT_Record.r3       = 0U;
    SYS_FAULT_Record.r12      = 0U;
    SYS_FAULT_Record.cfsr     = 0U;
    SYS_FAULT_Record.hfsr     = 0U;
    SYS_FAULT_Record.mmfar    = 0U;
    SYS_FAULT_Record.bfar     = 0U;
    SYS_FAULT_Record.count    = 0U;
}


/* 生成一行现场报告，ASCII，供串口或显示屏输出 */
uint32_t SYS_FAULT_Report(char *buf, uint32_t size)
{
    uint32_t pos;
    uint32_t max;
    uint32_t i;

    if ((buf == NULL) || (size < 2U)) return 0U;

    max = size - 1U;    /* 预留结尾 '\0' 位置 */

    /* 1) 故障类型名 */
    pos = fault_put_str(buf, max, 0U, fault_type_name(SYS_FAULT_Record.type));

    /* 2) 逐条附加命中的故障原因，依据 CFSR / HFSR 位表 */
    for (i = 0U; i < (uint32_t)FAULT_CAUSE_COUNT; i++) {
        uint32_t regval = (fault_cause_tbl[i].reg == 0U)
                        ? SYS_FAULT_Record.cfsr
                        : SYS_FAULT_Record.hfsr;
        if ((regval & (1UL << fault_cause_tbl[i].bit)) != 0U) {
            pos = fault_put_str(buf, max, pos, " ");
            pos = fault_put_str(buf, max, pos, fault_cause_tbl[i].name);
        }
    }

    /* 3) 出错地址和 CFSR 原始值，方便对照手册 */
    pos = fault_put_str(buf, max, pos, " PC=");
    pos = fault_put_hex(buf, max, pos, SYS_FAULT_Record.pc);
    pos = fault_put_str(buf, max, pos, " CFSR=");
    pos = fault_put_hex(buf, max, pos, SYS_FAULT_Record.cfsr);

    buf[pos] = '\0';
    return pos;
}
