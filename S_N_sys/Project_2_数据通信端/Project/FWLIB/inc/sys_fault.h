#ifndef __FWLIB_SYS_FAULT_H
#define __FWLIB_SYS_FAULT_H

#include "stm32f4xx.h"

/* sys_fault.h：CPU 故障捕获诊断模块 头文件
 *
 * 接管 Cortex-M4 硬件故障异常，把出错瞬间的现场（出错地址、
 * 寄存器、故障状态）保存到全局变量，供调试器查看或串口打印。
 *
 * 捕获异常：NMI / HardFault / MemManage / BusFault / UsageFault。
 * SysTick、SVC、PendSV 不动，归 sys_tick / FreeRTOS 管理；
 * 任务中发生故障同样会被捕获。
 *
 * 报警回调运行在异常上下文，只能做轮询串口发送、翻转 LED 这类
 * 短操作；不得调用延时、中断、RTOS API。本模块不依赖板级外设与引脚。
 *
 * 无标准库依赖：基于 Cortex-M4 内核寄存器（SCB->CFSR/HFSR/
 * SHCSR/CCR 等，CMSIS 定义）与 CMSIS 的 NVIC_SystemReset；
 * 异常入口 NMI/HardFault/…_Handler 为 __asm 汇编
 */


/* 区块 1：定义与宏定义区 */
/* 1 = 使能故障细分：MemManage / BusFault / UsageFault 分别进入各自
 *     的处理函数；0 = 全部并入 HardFault */
#define SYS_FAULT_ENABLE_SPLIT   1

/* 1 = 使能除零陷阱：除以 0 进入 UsageFault；
 * 0 = 保持芯片默认，除零不报错，结果按 0 处理 */
#define SYS_FAULT_DIV0_TRAP      0

/* 1 = 报警完成后自动软复位，用于无人值守设备；
 * 0 = 停在原地等待调试器 */
#define SYS_FAULT_AUTO_RESET     0

/* 故障类型（SYS_FAULT_Record.type 的取值） */
#define SYS_FAULT_TYPE_NONE      0U
#define SYS_FAULT_TYPE_NMI       1U
#define SYS_FAULT_TYPE_HARD      2U
#define SYS_FAULT_TYPE_MEM       3U
#define SYS_FAULT_TYPE_BUS       4U
#define SYS_FAULT_TYPE_USAGE     5U

/* 故障现场记录（调试器直接观察） */
typedef struct {
    uint32_t type;        /* 故障类型：SYS_FAULT_TYPE_xxx            */
    uint32_t from_psp;    /* 1 = 出错时使用进程栈（常见于 RTOS 任务） */
    uint32_t pc;          /* 出错指令地址：反查源码的关键             */
    uint32_t lr;          /* 出错时的返回地址                        */
    uint32_t psr;         /* 程序状态寄存器                          */
    uint32_t r0, r1, r2, r3, r12;   /* 出错时的通用寄存器           */
    uint32_t cfsr;        /* 可配置故障状态（含 MMFSR/BFSR/UFSR 位） */
    uint32_t hfsr;        /* 硬件故障状态（FORCED 等）               */
    uint32_t mmfar;       /* 内存管理故障地址（有效时）              */
    uint32_t bfar;        /* 总线故障地址（有效时）                  */
    uint32_t count;       /* 累计进入故障处理的次数                  */
} SYS_FAULT_Record_t;

/* 全局故障记录，调试器直接观察 */
extern volatile SYS_FAULT_Record_t SYS_FAULT_Record;


/* 区块 2：基础功能 */
/* 初始化：清零记录，按宏开启故障细分与除零捕获
 * 在 main 开头调用一次；不调用也能捕获故障，只是细分不生效
 * 写 SCB->SHCSR（故障细分使能）与 SCB->CCR（除零捕获） */
void SYS_FAULT_Init(void);

/* 注册故障报警回调：发生故障时、停在死循环前调用一次
 * 参数传 0 取消回调；无回调时直接停在死循环等待调试器
 * 回调运行在异常上下文，必须短小无延时；需要打印时用轮询串口 */
void SYS_FAULT_SetCallback(void (*callback)(void));

/* 是否发生过故障：1 = 发生过（SYS_FAULT_Record.type 非 NONE） */
uint8_t SYS_FAULT_Happened(void);

/* 清空记录（处理完一次故障后手动清零，count 一并归零） */
void SYS_FAULT_Clear(void);


/* 区块 3：扩展功能 */
/* 生成一行现场报告（纯 ASCII）写入 buf，供串口打印
 * 输出形如 "UsageFault DIVBYZERO PC=0x08000C4A CFSR=0x00020000"
 * 返回实际写入的字符数，不含结尾 '\0'；缓冲区不足时自动截断
 * 在报警回调里调用 */
uint32_t SYS_FAULT_Report(char *buf, uint32_t size);


/* SCB_Type 相关成员（定义见 core_cm4.h，系统控制块）
 *   ICSR     中断控制与状态：当前异常号 / 挂起异常
 *   VTOR     向量表偏移：重定位中断向量表用，本库未用
 *   AIRCR    应用中断 / 复位控制：PRIGROUP 优先级分组位在此
 *            （NVIC_PriorityGroupConfig / SYS_NVIC_Init 写）
 *   CCR      配置控制：位 4 = DIV_0_TRP 除零陷阱
 *            （SYS_FAULT_Init 写）
 *   SHCSR    系统处理控制：MEMFAULTENA / BUSFAULTENA / USGFAULTENA
 *            为故障细分开关，使能后分别进入各自的故障处理函数
 *            （SYS_FAULT_Init 写）
 *   CFSR     可配置故障状态：细分错误全在此，含 MMFSR / BFSR / UFSR
 *            （SYS_FAULT_Record 记录）
 *   HFSR     硬件故障状态：FORCED 位表示由子故障升级而来
 *   MMFAR    内存管理故障地址：出错访问的地址（MMARVALID 有效时）
 *   BFAR     总线故障地址：出错访问的地址（BFARVALID 有效时）
 *   AFSR     辅助故障状态，本库未用
 *   DFSR     调试故障状态，调试器相关，本库未用
 *   SHP[12]  系统异常优先级（SYS_NVIC_SetPriority 对负 IRQn 写此处）
 *   IP[240]  外设中断优先级：240 个 IRQ 的优先级字节
 *            （NVIC_SetPriority 对正 IRQn 写此处）
 *   STIR     软件触发中断，本库未用
 */

#endif /* __FWLIB_SYS_FAULT_H */
