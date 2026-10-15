#ifndef __FWLIB_DELAY_H
#define __FWLIB_DELAY_H

#include "stm32f4xx.h"

/* ===================
 *  delay.h — 延时函数（粗延时 + DWT 精准延时）  头文件
 * ===================
 *  命名 : 函数小写开头（delay_xxx）;老工程调用处 Delay_ 改 delay_,
 *         #include "gpio_core.h" 已转含本文件。
 *  边界 : SYS_TICK_Delay_ms/us/s 属 sys_tick 模块。
 *
 *  选型 :
 *    delay_ms — 毫秒级,DWT 计时,用于时序敏感场合（DHT11 起始信号、
 *        传感器上电等待）;
 *    delay_loop — 软件空循环,未标定,仅用于 LED 闪烁 / 蜂鸣器节拍;
 *    delay_cycles / delay_us / delay_ns / delay_ms_dwt — DWT 硬件
 *        周期计数器,忙等,免初始化、不占中断,RTOS 下可用;
 *    SYS_TICK_Delay_ms / us / s — 中断计时、不占 CPU,须先 SYS_TICK_Init。
 *
 *  依据 : 内核 DWT（core_cm4.h）与空循环
 * =================== */


/* ===================
 *                    区块 1：定义与宏定义区
 * =================== */
/* 无硬件映射、无可调宏（换板子 / 换型号零改动） */


/* ===================
 *                    区块 2：基础功能
 * =================== */
/* ----------------------------------------------------------------
 *  毫秒延时（内部 DWT 计时）
 * ----------------------------------------------------------------
 * 实现 : 循环调 delay_us(1000),时长跟随 SystemCoreClock,
 *        168MHz 下误差千分之几以内。
 * 参数 : ms 单位毫秒。
 * 示例 : delay_ms(500);      // 500ms
 *        delay_loop(1000);   空转 1000 次 */
void delay_ms  (uint32_t ms);                    /* 毫秒级延时（DWT 精确实现） */
void delay_loop(volatile uint32_t n);            /* 空转 n 次（未标定） */


/* ===================
 *        精准延时（DWT 周期计数器,纳秒 ~ 毫秒）
 * ===================
 * 原理 : DWT->CYCCNT 每 CPU 周期 +1;延时 = 忙等到两次读数之差达到
 *   目标周期数,换算用 SystemCoreClock。
 * 约束 : 首次调用自动使能 DWT,不占定时器与中断,含 RTOS 可用;忙等,
 *   被中断打断则总时长顺延（ISR 耗时计入）;精度 1 周期 ≈ 6ns @168MHz,
 *   误差 ±几十 ns;32 位换算须 us/ns/ms×主频 < 2^32,@168MHz 上限
 *   delay_us 约 25.5s、delay_ns 约 25.5ms、delay_ms_dwt 单次约 25.5s
 *   （超出按上限执行）。
 * 适用 : 单总线时序（WS2812 / DS18B20）、传感器建立-保持时间、
 *        脉冲宽度、移位寄存器时钟。
 * =================== */
void delay_cycles(uint32_t cycles);   /* 原语：忙等 cycles 个 CPU 周期; 例:delay_cycles(168) ≈ 1µs@168MHz */
void delay_us    (uint32_t us);       /* 微秒级精准延时; 例:delay_us(10) = 10µs */
void delay_ns    (uint32_t ns);       /* 纳秒级精准延时; 例:delay_ns(500) = 0.5µs */
void delay_ms_dwt(uint32_t ms);       /* 毫秒级精准延时(不用 SysTick—RTOS 下可用); 例:delay_ms_dwt(100) = 100ms */


/* ===================
 *  附:DWT_Type 结构体速查（core_cm4.h）
 * ===================
 *    CTRL      位 0 = CYCCNT 周期计数使能,由 delay_dwt_enable 置位
 *    CYCCNT    每 CPU 周期 +1,32 位 25.6s @168MHz 回绕,差值法安全
 *    CPICNT / EXCCNT / SLEEPCNT / LSUCNT / FOLDCNT  性能计数,库未用
 *    PCSR 程序计数器采样、COMP/MASK/FUNCTION × 4 数据观察点,库未用
 *    DWT 使能还须置 CoreDebug->DEMCR 的 TRCENA 位（跟踪总开关）
 * =================== */

#endif /* __FWLIB_DELAY_H */
