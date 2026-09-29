#ifndef __FWLIB_SYS_SOFTIMER_H
#define __FWLIB_SYS_SOFTIMER_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_softimer.h —— 【系统】软定时器（模块联动引擎）  头文件
 * ================================================================
 *  设计定位 : "一个硬件节拍,驱动一堆软定时任务"——把 TIM 中断 /
 *             SysTick 的 1ms 心跳变成 N 条并行"软定时器",每条到点
 *             执行一个回调——模块联动的公共底座:
 *               · 每秒:刷 OLED / 打印时间（联动 sys_rtc / sys_usart）
 *               · 每 1ms:LED_BlinkUpdate / BEEP_Update（联动 led/beep）
 *               · 每 10ms:KEY_Scan 轮询（联动 key）
 *               · 每 100ms:ADC 采样 / 传感器读取（联动 sys_adc…）
 *  标准库关键词 : 无——纯软件表驱动;时基借 sys_tick 的 1ms 计数
 *                 （SYS_TICK_GetTick）
 *
 *  使用方式 :
 *      SYS_TICK_Init();                                  // ① 1ms 时基
 *      SYS_SOFTIMER_Add(On1ms,   1);                     // ② 注册周期任务
 *      SYS_SOFTIMER_Add(On100ms, 100);
 *      while (1) { SYS_SOFTIMER_Poll(); 主循环自己的活(); }  // ③ 轮询
 *
 *  说明 :
 *   - 回调在"调用 Poll 的上下文"执行——主循环轮询则回调在主循环;
 *     放进 TIM 中断也行,但回调必须短小（中断里别干重活）;
 *   - 回调里可以再调用 led/beep/key/adc/rtc 等任何库函数;
 *   - 周期精度 = 调用 Poll 的节拍精度（主循环阻塞久会推迟,属软定时器特性;
 *     硬实时需求请用真正的硬件定时器 SYS_TIM_InitIT）。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* 最大软定时器条数（1 ~ 16）——改大自动生效（表按此宏开） */
#define SYS_SOFTIMER_MAX   8

/* 编译期自检 */
#if (SYS_SOFTIMER_MAX < 1) || (SYS_SOFTIMER_MAX > 16)
#error "SYS_SOFTIMER_MAX must be 1..16"
#endif


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 清零所有软定时器（上电/复位状态本来就是空的,可不调用） */
void SYS_SOFTIMER_Init(void);

/* 轮询执行：到点的回调逐个执行
 * 返回 : 本次执行的回调个数（0 = 没有到点的,正常情况）
 * 示例 : while (1) { SYS_SOFTIMER_Poll(); ... } */
uint8_t SYS_SOFTIMER_Poll(void);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 注册一个周期任务
 * 参数 : callback —— 到点执行的回调（无参无返回,保持短小）
 *        period_ms —— 周期（1 ~ 24 天;0 会被拒绝）
 * 返回 : 0 ~ SYS_SOFTIMER_MAX-1 = 任务编号;0xFF = 表满/参数错
 * 标准库 : 无——纯软件表
 * 示例 : SYS_SOFTIMER_Add(OnKeyTick, 10);   // 每 10ms 跑一次按键扫描 */
uint8_t SYS_SOFTIMER_Add(void (*callback)(void), uint32_t period_ms);

/* 注销任务（任务内自己注销也行——本函数只清一项） */
void SYS_SOFTIMER_Remove(uint8_t id);

/* 当前已注册任务数（调试/展示用） */
uint8_t SYS_SOFTIMER_Count(void);

#endif /* __FWLIB_SYS_SOFTIMER_H */
