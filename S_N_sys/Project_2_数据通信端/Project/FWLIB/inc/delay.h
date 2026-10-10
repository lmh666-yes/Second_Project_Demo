#ifndef __FWLIB_DELAY_H
#define __FWLIB_DELAY_H

#include "stm32f4xx.h"

/* delay.h: 延时函数，粗延时 + DWT 精准延时
 * delay_ms / delay_loop 为粗延时，未标定的软件空循环，时长随主频、编译优化等级变化，只用于 LED 闪烁、蜂鸣器节拍等场合
 * delay_cycles / delay_us / delay_ns / delay_ms_dwt 用 DWT 硬件周期计数器计时，免初始化、不占中断，RTOS 下可用（忙等，纳秒到毫秒级）
 * 毫秒以上且不想占 CPU 的延时用 sys_tick 模块的 SYS_TICK_Delay_ms / us / s，需先 SYS_TICK_Init */


/* 无硬件映射、无可调宏 */


/* 粗延时：软件空循环，未做硬件标定，实际时长随主频、编译优化等级、Flash 等待周期变化
 * 用于 LED 闪烁、按键消抖、蜂鸣器节拍等对精度不敏感处；高精度毫秒级延时用 sys_tick，纳秒到微秒级用下方 DWT 延时 */
void delay_ms  (uint32_t ms);                    /* 毫秒级粗延时 */
void delay_loop(volatile uint32_t n);            /* 空转 n 次 */


/* DWT 精准延时：Cortex-M4 内核的 DWT->CYCCNT 每个 CPU 周期自动 +1，延时即忙等到两次读数之差达到目标周期数，周期数与时间换算用 SystemCoreClock
 * 不用定时器与中断，首次调用自动使能 DWT，RTOS 下可用（不占 SysTick）；忙等期间被中断打断则总时长顺延
 * 精度为周期级（1 周期 ≈ 6ns @168MHz），换算与循环粒度合计误差约几十 ns；us / ns / ms 与主频的乘积须小于 2^32
 * @168MHz 单次上限：delay_us 约 25.5s、delay_ns 约 25.5ms、delay_ms_dwt 约 25.5s，超出按上限执行；更长延时用 SYS_TICK_Delay_ms */
void delay_cycles(uint32_t cycles);   /* 忙等 cycles 个 CPU 周期；@168MHz 时 168 周期 ≈ 1µs */
void delay_us    (uint32_t us);       /* 微秒级精准延时 */
void delay_ns    (uint32_t ns);       /* 纳秒级精准延时 */
void delay_ms_dwt(uint32_t ms);       /* 毫秒级精准延时，不用 SysTick，RTOS 下可用 */

/* 同一个 DWT 计数器的读数接口，用于测时间，不是延时
 * 用于单总线或自定义协议的位宽测量、代码耗时统计、信号脉宽测量
 * 首次调用自动使能 DWT；CYCCNT 自由运行，约 25.6s 回绕一次，比较时间须用 DWT_ElapsedUs 的差值法 */
uint32_t DWT_GetCycles (void);              /* 原始周期计数（1 周期 ≈ 6ns @168MHz） */
uint32_t DWT_GetUs     (void);              /* 微秒时间戳（可直接相减） */
uint32_t DWT_ElapsedUs (uint32_t start_us); /* 距时间戳已过多少微秒 */


/* DWT_Type 定义在 core_cm4.h，精准短延时用到的位：
 * CTRL 位 0 = CYCCNT 周期计数使能（delay_dwt_enable 置位后计数器开始计数）
 * CYCCNT 每个 CPU 周期 +1，延时即忙等到目标差值；32 位约 25.6s @168MHz 回绕，差值法安全
 * CPICNT / EXCCNT / SLEEPCNT / LSUCNT / FOLDCNT / PCSR / COMP / MASK / FUNCTION 为性能计数与数据观察点，库未用
 * CoreDebug->DEMCR 的 TRCENA 位是跟踪总开关，使能 DWT 时一并打开，见 delay.c 的 delay_dwt_enable */

#endif /* __FWLIB_DELAY_H */
