#ifndef __FWLIB_SYS_SOFTIMER_H
#define __FWLIB_SYS_SOFTIMER_H

#include "stm32f4xx.h"

/* sys_softimer.h — 软定时器:1ms 时基驱动多条周期回调
 * 时基取自 sys_tick 的 1ms 计数 SYS_TICK_GetTick
 * 回调在 SYS_SOFTIMER_Poll 的调用上下文执行,须短小;周期精度受 Poll 调用节拍制约
 * 硬实时需求请用硬件定时器 SYS_TIM_InitIT;详见 检测数据端设计.md */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* 软定时器最大条数,有效范围 1 ~ 16 */
#define SYS_SOFTIMER_MAX   8

/* 编译期自检 */
#if (SYS_SOFTIMER_MAX < 1) || (SYS_SOFTIMER_MAX > 16)
#error "SYS_SOFTIMER_MAX must be 1..16"
#endif


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 清零所有软定时器;上电/复位后表已为空,可不调用 */
void SYS_SOFTIMER_Init(void);

/* 轮询:到点回调逐个执行,回调内可安全注销自身
 * 返回:本次执行的回调个数,0 表示无到点
 * 依据:按 (int32_t)(now - due) >= 0 判断到点,时基回绕安全 */
uint8_t SYS_SOFTIMER_Poll(void);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 注册周期任务
 * callback : 到点回调,无参无返回,须短小
 * period_ms: 周期,单位 ms,有效范围 1 ~ 2073600000(24 天)
 *            上限依据:到点按 (int32_t) 差值比较,周期须 < 2^31 ms
 * 返回:0 ~ SYS_SOFTIMER_MAX-1 = 任务编号;0xFF = 表满,或 callback/period 为 0 */
uint8_t SYS_SOFTIMER_Add(void (*callback)(void), uint32_t period_ms);

/* 注销任务 id;id >= SYS_SOFTIMER_MAX 忽略;任务内可注销自身 */
void SYS_SOFTIMER_Remove(uint8_t id);

/* 当前已注册任务数 */
uint8_t SYS_SOFTIMER_Count(void);

#endif /* __FWLIB_SYS_SOFTIMER_H */
