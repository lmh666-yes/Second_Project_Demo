#ifndef __FWLIB_SYS_FAULT_H
#define __FWLIB_SYS_FAULT_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_fault.h —— 【系统】CPU 故障捕获诊断模块  头文件
 * ================================================================
 *  设计定位 : 程序"跑飞"时的黑匣子——
 *             自动接管 Cortex-M4 的硬件故障异常，把出错瞬间的
 *             现场（出错地址、寄存器、故障状态）保存到全局变量，
 *             方便在 Keil 调试器里定位，或经串口打印出来。
 *
 *  为什么需要它 :
 *     默认情况下任何硬件故障（野指针、数组越界、总线访问错误、
 *     栈溢出等）都会跳进启动文件里的死循环——板子表现为"突然
 *     卡死、零提示"。本模块顶替这些死循环，给调试留下线索。
 *
 *  捕获哪些异常 :
 *     NMI / HardFault / MemManage / BusFault / UsageFault
 *     （SysTick、SVC、PendSV 不动——它们归 sys_tick / FreeRTOS 管）
 *
 *  使用方式 :
 *      SYS_FAULT_Init();                     // main 开头调用一次
 *      SYS_FAULT_SetCallback(报警函数);       // 可选：串口打印 / LED 报警
 *      ... 正常运行 ...
 *      出错后：① Keil 调试时在 Watch 窗口添加 SYS_FAULT_Record，
 *                看 .pc（出错指令地址）——反查源码即可定位；
 *              ② 或在报警函数里用 SYS_FAULT_Report() 生成文字发串口
 *
 *  注意事项 :
 *   ① 报警回调运行在"异常上下文"，只做最简单的事（轮询串口发
 *      几十个字符、翻转 LED）；不要调用延时、中断、RTOS API；
 *   ② 与 FreeRTOS 共存：SVC/PendSV/SysTick 由 RTOS 占用，本模块
 *      不碰；任务里发生故障同样会被捕获；
 *   ③ 本模块是通用 Cortex-M 代码，不依赖任何板级外设与引脚。
 *
 *  标准库关键词 : 无 —— 全部基于 Cortex-M4 内核寄存器（SCB->CFSR/HFSR/
 *                 SHCSR/CCR 等,CMSIS 定义）与 CMSIS 的 NVIC_SystemReset；
 *                 异常入口 NMI/HardFault/…_Handler 为 __asm 汇编
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* 1 = 初始化时开启"故障细分"：内存保护错/总线错/用法错分别进
 *     各自的处理函数（报错更准确）；0 = 全部并入 HardFault */
#define SYS_FAULT_ENABLE_SPLIT   1

/* 1 = 开启"除零捕获"：除以 0 会跳 UsageFault（方便抓这类 bug）；
 * 0 = 保持芯片默认（除零不报错，结果为 0，推荐先关着） */
#define SYS_FAULT_DIV0_TRAP      0

/* 1 = 报警完成后自动软复位（适合无人值守设备）；
 * 0 = 停在原地等调试器（推荐调试阶段：现场只有一份，别冲掉） */
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
    uint32_t pc;          /* ★出错指令地址：反查源码的关键           */
    uint32_t lr;          /* 出错时的返回地址                        */
    uint32_t psr;         /* 程序状态寄存器                          */
    uint32_t r0, r1, r2, r3, r12;   /* 出错时的通用寄存器           */
    uint32_t cfsr;        /* 可配置故障状态（含 MMFSR/BFSR/UFSR 位） */
    uint32_t hfsr;        /* 硬件故障状态（FORCED 等）               */
    uint32_t mmfar;       /* 内存管理故障地址（有效时）              */
    uint32_t bfar;        /* 总线故障地址（有效时）                  */
    uint32_t count;       /* 累计进入故障处理的次数                  */
} SYS_FAULT_Record_t;

/* 全局记录：Keil 调试时在 Watch 窗口添加 SYS_FAULT_Record 查看 */
extern volatile SYS_FAULT_Record_t SYS_FAULT_Record;


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：清零记录 + 按宏开启故障细分 / 除零捕获
 * 说明 : 在 main 开头调用一次即可；不调用也能捕获（细分不生效）
 * 标准库 : 无——写 SCB->SHCSR（故障细分使能）与 SCB->CCR（除零捕获）
 * 示例 : SYS_FAULT_Init();
 *        SYS_FAULT_SetCallback(OnFault);   // 可选:故障时先报警再停下 */
void SYS_FAULT_Init(void);

/* 注册"故障报警回调"：发生故障时、停在死循环前调用一次
 * 用途 : 轮询串口打印 SYS_FAULT_Report() / LED 报警 / 置标志
 * 参数 : 传 0 取消回调（无回调时直接停在死循环等待调试器）
 * 示例 : void OnFault(void) { BEEP_On(); }      // 报警内容任意组合
 *        SYS_FAULT_SetCallback(OnFault);
 * 扩展提示 : 回调保持"短小无延时"——需要打印时用轮询串口（见 Report 注释） */
void SYS_FAULT_SetCallback(void (*callback)(void));

/* 是否发生过故障：1 = 发生过（SYS_FAULT_Record.type 非 NONE） */
uint8_t SYS_FAULT_Happened(void);

/* 清空记录（处理完一次故障后手动清零，count 一并归零） */
void SYS_FAULT_Clear(void);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 生成一行现场报告（纯 ASCII，供串口打印），写入 buf
 * 格式示例："UsageFault DIVBYZERO PC=0x08000C4A CFSR=0x00020000"
 * 返回 : 实际写入的字符数（不含结尾 '\0'；缓冲区不足时自动截断）
 * 用法 : 在报警回调里调用（回调必须短小）：
 *          char line[80];
 *          SYS_FAULT_Report(line, sizeof(line));
 *          SYS_USART_SendLine(SYS_USART_1, line);  // 联动 sys_usart */
uint32_t SYS_FAULT_Report(char *buf, uint32_t size);


/* ================================================================
 *  附:标准库结构体速查 —— SCB_Type（定义在 core_cm4.h;系统控制块）
 * ================================================================
 *  库用到/相关的成员（完整定义见 core_cm4.h）:
 *    ICSR     中断控制与状态:当前异常号/挂起异常
 *    VTOR     向量表偏移:重定位中断向量表用,库未用
 *    AIRCR    应用中断/复位控制:PRIGROUP 优先级分组位在这
 *             （NVIC_PriorityGroupConfig / SYS_NVIC_Init 写它）
 *    CCR      配置控制:位 4 = DIV_0_TRP 除零陷阱
 *             （SYS_FAULT_Init 写它）
 *    SHCSR    系统处理控制:MEMFAULTENA/BUSFAULTENA/USGFAULTENA
 *             故障细分开关（SYS_FAULT_Init 写它——开了才分别进
 *              各自的故障处理函数）
 *    CFSR     可配置故障状态:细分错误全在这（SYS_FAULT_Record 快照
 *             记录它——MMFSR/BFSR/UFSR 三层含义）
 *    HFSR     硬件故障状态:FORCED 位说明"是子故障升级上来的"
 *    MMFAR    内存管理故障地址:出错访问的地址（MMARVALID 有效时）
 *    BFAR     总线故障地址:出错访问的地址（BFARVALID 有效时）
 *    AFSR     辅助故障状态:少见,库未用
 *    DFSR     调试故障状态:调试器相关,库未用
 *    SHP[12]  系统异常优先级（SYS_NVIC_SetPriority 对负 IRQn 写这里）
 *    IP[240]  外设中断优先级:240 个 IRQ 的优先级字节
 *             （NVIC_SetPriority 对正 IRQn 写这里）
 *    STIR     软件触发中断:库未用
 * ================================================================ */

#endif /* __FWLIB_SYS_FAULT_H */
