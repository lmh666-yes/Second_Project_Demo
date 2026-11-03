#include "sys_fault.h"
#include <stddef.h>

/* sys_fault.c: CPU 故障捕获诊断模块实现文件。汇编包装 NMI/HardFault/MemManage/BusFault/UsageFault 入口, 按 EXC_RETURN(LR).bit2
 * 判定现场在 MSP(0) 或 PSP(1); 现场/类型/EXC_RETURN 传 sys_fault_c_handler; 包装与 C 函数名不可改(向量表按符号名链接), 汇编段为 AC5 __asm。 */


/* 全局故障现场（调试器 Watch 窗口观察） */
volatile SYS_FAULT_Record_t SYS_FAULT_Record;

/* 故障报警回调（SYS_FAULT_SetCallback 注册，缺省为空） */
static void (*sys_fault_callback)(void) = NULL;


/* 故障类型 → 名称 */
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

/* 故障原因位表: reg 0=CFSR(含 MMFSR/BFSR/UFSR), 1=HFSR; bit 为位号 0~31; name 为打印名(AC5 须 ASCII) */
typedef struct {
    uint8_t     reg;
    uint8_t     bit;
    const char *name;
} FaultCause_t;

static const FaultCause_t fault_cause_tbl[] = {
    { 0U,  0U, "IACCVIOL"   },
    { 0U,  1U, "DACCVIOL"   },
    { 0U,  3U, "MUNSTKERR"  },
    { 0U,  4U, "MSTKERR"    },
    { 0U,  5U, "MLSPERR"    },
    { 0U,  8U, "IBUSERR"    },
    { 0U,  9U, "PRECISERR"  },
    { 0U, 10U, "IMPRECISERR" },
    { 0U, 11U, "UNSTKERR"   },
    { 0U, 12U, "STKERR"     },
    { 0U, 13U, "LSPERR"     },
    { 0U, 16U, "UNDEFINSTR" },
    { 0U, 17U, "INVSTATE"   },
    { 0U, 18U, "INVPC"      },
    { 0U, 19U, "NOCP"       },
    { 0U, 24U, "UNALIGNED"  },
    { 0U, 25U, "DIVBYZERO"  },
    { 1U,  1U, "VECTTBL"    },
    { 1U, 30U, "FORCED"     },
    { 1U, 31U, "DEBUGEVT"   },
};

#define FAULT_CAUSE_COUNT  (sizeof(fault_cause_tbl) / sizeof(fault_cause_tbl[0]))

/* 编译期护栏: 表项数固定 20 */
typedef char fault_cause_count_check[(FAULT_CAUSE_COUNT == 20U) ? 1 : -1];

/* 追加字符串: max 不含结尾 '\0' */
static uint32_t fault_put_str(char *buf, uint32_t max, uint32_t pos, const char *s)
{
    while ((*s != '\0') && (pos < max)) {
        buf[pos++] = *s++;
    }
    return pos;
}

/* 追加 32 位十六进制(0x???????? 大写) */
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


/* 异常入口(汇编包装): LR 入异常时为 EXC_RETURN, bit2=0 现场在 MSP(裸机/中断), bit2=1 现场在 PSP(RTOS 任务)。
 * 传参 R0=现场指针, R1=故障类型, R2=EXC_RETURN, 尾调用 C 处理函数。
 * 各入口 __weak, 手写同名 Handler 会顶替本实现(弱定义机制见 sys_tim.c)。 */
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


/* 由上面汇编包装调用(勿改名, 勿加 static); frame 布局 R0 R1 R2 R3 R12 LR PC xPSR(硬件压栈顺序) */
void sys_fault_c_handler(uint32_t *frame, uint32_t type, uint32_t exc_return)
{
    uint32_t cfsr;

    /* 记录出错现场(重复进入覆盖为最新) */
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

    /* 记录故障状态寄存器；地址类寄存器仅在有效时取 */
    cfsr = SCB->CFSR;
    SYS_FAULT_Record.cfsr  = cfsr;
    SYS_FAULT_Record.hfsr  = SCB->HFSR;
    SYS_FAULT_Record.mmfar = (cfsr & (1UL << 7))  ? SCB->MMFAR : 0U;  /* MMARVALID */
    SYS_FAULT_Record.bfar  = (cfsr & (1UL << 15)) ? SCB->BFAR  : 0U;  /* BFARVALID */
    SYS_FAULT_Record.count++;

    if (sys_fault_callback != NULL) {
        sys_fault_callback();
    }

#if SYS_FAULT_AUTO_RESET
    NVIC_SystemReset();
#endif

    /* 停留原地, 供调试器观察 */
    for (;;) {
    }
}


/* 初始化: 清零记录, 按宏开启细分与除零捕捉 */
void SYS_FAULT_Init(void)
{
    SYS_FAULT_Clear();

#if SYS_FAULT_ENABLE_SPLIT
    /* 细分使能 MemManage / BusFault / UsageFault */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk
               |  SCB_SHCSR_BUSFAULTENA_Msk
               |  SCB_SHCSR_USGFAULTENA_Msk;
#endif

#if SYS_FAULT_DIV0_TRAP
    /* 开启除零捕捉(默认关闭, 见头文件); 非对齐访问捕捉需另置 CCR.UNALIGN_TRP */
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
#endif
}

/* 注册故障报警回调(传 0 取消) */
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


/* 生成一行现场报告(ASCII) */
uint32_t SYS_FAULT_Report(char *buf, uint32_t size)
{
    uint32_t pos;
    uint32_t max;
    uint32_t i;

    if ((buf == NULL) || (size < 2U)) return 0U;

    max = size - 1U;    /* 预留 '\0' 位置 */

    pos = fault_put_str(buf, max, 0U, fault_type_name(SYS_FAULT_Record.type));

    /* 附加命中的故障原因(CFSR / HFSR) */
    for (i = 0U; i < (uint32_t)FAULT_CAUSE_COUNT; i++) {
        uint32_t regval = (fault_cause_tbl[i].reg == 0U)
                        ? SYS_FAULT_Record.cfsr
                        : SYS_FAULT_Record.hfsr;
        if ((regval & (1UL << fault_cause_tbl[i].bit)) != 0U) {
            pos = fault_put_str(buf, max, pos, " ");
            pos = fault_put_str(buf, max, pos, fault_cause_tbl[i].name);
        }
    }

    pos = fault_put_str(buf, max, pos, " PC=");
    pos = fault_put_hex(buf, max, pos, SYS_FAULT_Record.pc);
    pos = fault_put_str(buf, max, pos, " CFSR=");
    pos = fault_put_hex(buf, max, pos, SYS_FAULT_Record.cfsr);

    buf[pos] = '\0';
    return pos;
}
