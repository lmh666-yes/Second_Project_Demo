#ifndef __FWLIB_SYS_SOFTIMER_H
#define __FWLIB_SYS_SOFTIMER_H

#include "stm32f4xx.h"

/* softimer: 软件定时器，用 sys_tick 的 1ms 时基驱动 N 条周期任务
 * 时基取 SYS_TICK_GetTick 的 1ms 计数；纯软件表驱动，不占用硬件定时器
 * 应用注册的典型任务：1ms 跑 LED_BlinkUpdate / BEEP_Update，10ms 跑 KEY_Scan，
 * 100ms 跑 ADC 与传感器读取，1s 刷 OLED 与打印时间
 * 回调在调用 Poll 的上下文执行，必须短小，回调内可再调用 led/beep/key/adc/rtc
 * 等库函数；周期精度取决于调用 Poll 的节拍精度，主循环阻塞会推迟回调；
 * 硬实时需求用硬件定时器 SYS_TIM_InitIT */

/* 定义与宏定义区 */
/* 最大软定时器条数，取值 1 ~ 16；表按本宏大小开辟，改大只增加 RAM 占用 */
#define SYS_SOFTIMER_MAX   8

#if (SYS_SOFTIMER_MAX < 1) || (SYS_SOFTIMER_MAX > 16)
#error "SYS_SOFTIMER_MAX must be 1..16"
#endif


/* 基础功能 */
/* 清零所有软定时器；上电与复位后本就是零值，可不调用 */
void SYS_SOFTIMER_Init(void);

/* 轮询执行本轮到点的回调，到点的逐个执行
 * 返回 : 本次执行的回调个数，0 表示没有到点的，属正常情况 */
uint8_t SYS_SOFTIMER_Poll(void);


/* 扩展功能 */
/* 注册一个周期任务
 * 参数 : callback 为到点执行的回调，无参无返回，应保持短小
 *        period_ms 为周期，1 ~ 24 天（uint32 毫秒），0 会被拒绝
 *        上限 24 天：判到点用 (int32_t)(now - due) 有符号差值，周期须小于 2^31 ms
 * 返回 : 0 ~ SYS_SOFTIMER_MAX-1 为任务编号，0xFF 表示表满或参数错 */
uint8_t SYS_SOFTIMER_Add(void (*callback)(void), uint32_t period_ms);

/* 注销任务，只清除指定的一项；任务回调内也可调用 */
void SYS_SOFTIMER_Remove(uint8_t id);

/* 当前已注册任务数，用于调试与展示 */
uint8_t SYS_SOFTIMER_Count(void);

#endif /* __FWLIB_SYS_SOFTIMER_H */
