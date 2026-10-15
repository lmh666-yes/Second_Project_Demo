#ifndef __FWLIB_SYS_FAULT_H
#define __FWLIB_SYS_FAULT_H

#include "stm32f4xx.h"

/* sys_fault.h — CPU 故障捕获诊断模块（头文件）
 * 接管 NMI/HardFault/MemManage/BusFault/UsageFault，把现场（pc、通用寄存器、
 * CFSR/HFSR/MMFAR/BFAR）存入 SYS_FAULT_Record，供调试器观察或 Report() 打印；
 * SysTick/SVC/PendSV 归 sys_tick、FreeRTOS。 */


/* 区块 1：定义与宏定义区 */
/* 1 = 使能故障细分：MemManage/BusFault/UsageFault 分别进各自处理函数；
 * 0 = 全部并入 HardFault */
#define SYS_FAULT_ENABLE_SPLIT   1

/* 1 = 使能除零陷阱（SCB->CCR 位 4 DIV_0_TRP），除以 0 进 UsageFault；
 * 0 = 芯片默认（不报错，结果为 0） */
#define SYS_FAULT_DIV0_TRAP      0

/* 1 = 报警后软复位（NVIC_SystemReset 写 AIRCR 的 SYSRESETREQ），
 *     用于无人值守设备；0 = 停在原地等待调试器（现场只有一份） */
#define SYS_FAULT_AUTO_RESET     0

/* 故障类型：SYS_FAULT_Record.type 取值 */
#define SYS_FAULT_TYPE_NONE      0U
#define SYS_FAULT_TYPE_NMI       1U
#define SYS_FAULT_TYPE_HARD      2U
#define SYS_FAULT_TYPE_MEM       3U
#define SYS_FAULT_TYPE_BUS       4U
#define SYS_FAULT_TYPE_USAGE     5U

/* 故障现场记录：异常入口保存，调试器观察 */
typedef struct {
    uint32_t type;        /* 故障类型，取值 SYS_FAULT_TYPE_xxx */
    uint32_t from_psp;    /* 1 = 出错时使用进程栈（RTOS 任务常见） */
    uint32_t pc;          /* 出错指令地址，反查源码用 */
    uint32_t lr;          /* 出错时的返回地址 */
    uint32_t psr;         /* 程序状态寄存器 */
    uint32_t r0, r1, r2, r3, r12;   /* 出错时的通用寄存器 */
    uint32_t cfsr;        /* 可配置故障状态（含 MMFSR/BFSR/UFSR 位） */
    uint32_t hfsr;        /* 硬件故障状态（FORCED 等） */
    uint32_t mmfar;       /* 内存管理故障地址（MMARVALID 有效时） */
    uint32_t bfar;        /* 总线故障地址（BFARVALID 有效时） */
    uint32_t count;       /* 累计进入故障处理的次数 */
} SYS_FAULT_Record_t;

/* 全局记录：Keil Watch 窗口添加 SYS_FAULT_Record 查看 */
extern volatile SYS_FAULT_Record_t SYS_FAULT_Record;


/* 区块 2：基础功能 */
/* 初始化：清零记录，按宏使能故障细分（SCB->SHCSR 的 MEMFAULTENA/
 * BUSFAULTENA/USGFAULTENA）与除零陷阱（SCB->CCR 位 4 DIV_0_TRP）。
 * 参数无，返回值无。main 开头调用一次；不调用也能捕获，仅细分不生效。
 * 依据：Cortex-M4 SCB 寄存器；复位后故障细分默认关闭 */
void SYS_FAULT_Init(void);

/* 注册故障报警回调：停在死循环前调用一次，用于轮询串口打印
 * SYS_FAULT_Report()、LED 报警或置标志。
 * 参数 callback：传 0 取消回调；无回调时直接停在死循环等待调试器。
 * 回调在异常上下文执行，不能用延时、中断与 RTOS API */
void SYS_FAULT_SetCallback(void (*callback)(void));

/* 是否发生过故障：1 = 发生过（SYS_FAULT_Record.type 非 NONE） */
uint8_t SYS_FAULT_Happened(void);

/* 清空记录（count 一并归零） */
void SYS_FAULT_Clear(void);


/* 区块 3：扩展功能 */
/* 生成一行现场报告（纯 ASCII）写入 buf，格式：
 * "UsageFault DIVBYZERO PC=0x08000C4A CFSR=0x00020000"
 * 参数 buf：输出缓冲区；size：缓冲区字节数。
 * 返回：实际写入字符数（不含结尾 '\0'，缓冲区不足时截断）。
 * 依据：报告内容取自 SYS_FAULT_Record 的 type/pc/cfsr 字段。
 * 在报警回调中调用 */
uint32_t SYS_FAULT_Report(char *buf, uint32_t size);


/* 附:本模块涉及的内核寄存器（SCB 定义见 core_cm4.h）
 *   ICSR     中断控制与状态:当前异常号/挂起异常
 *   AIRCR    应用中断与复位控制:PRIGROUP 优先级分组位
 *            （NVIC_PriorityGroupConfig / SYS_NVIC_Init 写）
 *   CCR      配置控制:位 4 DIV_0_TRP 除零陷阱（SYS_FAULT_Init 写）
 *   SHCSR    系统处理控制:MEMFAULTENA/BUSFAULTENA/USGFAULTENA
 *            故障细分使能（SYS_FAULT_Init 写）
 *   CFSR     可配置故障状态:MMFSR/BFSR/UFSR（SYS_FAULT_Record 快照记录）
 *   HFSR     硬件故障状态:FORCED 表示子故障升级为 HardFault
 *   MMFAR    内存管理故障地址:出错访问地址（MMARVALID 有效时）
 *   BFAR     总线故障地址:出错访问地址（BFARVALID 有效时）
 *   SHP[12]  系统异常优先级（SYS_NVIC_SetPriority 对负 IRQn 写）
 *   IP[240]  外设中断优先级:240 个 IRQ 的优先级字节
 *            （NVIC_SetPriority 对正 IRQn 写）
 *   VTOR     向量表偏移:重定位中断向量表用,本模块未用
 *   AFSR     辅助故障状态:少见,本模块未用
 *   DFSR     调试故障状态:调试器相关,本模块未用
 *   STIR     软件触发中断:本模块未用 */

#endif /* __FWLIB_SYS_FAULT_H */
