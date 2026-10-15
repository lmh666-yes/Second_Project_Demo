#ifndef __FWLIB_SYS_TICK_H
#define __FWLIB_SYS_TICK_H

#include "stm32f4xx.h"

/* ================================================================
 *           模式选择（上 FreeRTOS 时本模块自动让位）
 * ================================================================
 *  FreeRTOS port.c 用 SysTick 产生调度节拍，xPortSysTickHandler 由
 *  FreeRTOSConfig.h 映射为 SysTick_Handler。裸机（SYS_TICK_USE_RTOS=0）：
 *  本模块定义弱 SysTick_Handler 累加毫秒计数，SYS_TICK_Init() 配置
 *  LOAD/CTRL 并开中断，全部 API 可用。RTOS（检测到 FreeRTOSConfig.h /
 *  configUSE_PREEMPTION）：本模块不再定义 SysTick_Handler（避免与 port.c
 *  抢符号），时基读 xTaskGetTickCount()（task.h 原型）；SYS_TICK_Init() 为
 *  空操作，不得调用（会重写 LOAD/CTRL 并抢 SysTick）；微秒延时用 delay.h DWT 版。
 *  强制走裸机分支可显式定义 SYS_TICK_USE_RTOS=0。
 * ================================================================ */
#ifndef SYS_TICK_USE_RTOS
#  if defined(SYS_RTOS_PRESENT)
     /* 已含 sys_rtos.h 时沿用其判定结果 */
#    define SYS_TICK_USE_RTOS   SYS_RTOS_PRESENT
#  elif (defined(INC_FREERTOS_H) || defined(configUSE_PREEMPTION) || \
         defined(configTICK_RATE_HZ))
     /* 包含过 FreeRTOS.h / FreeRTOSConfig.h 即判为 RTOS */
#    define SYS_TICK_USE_RTOS   1
#  else
#    define SYS_TICK_USE_RTOS   0
#  endif
#endif

#if (SYS_TICK_USE_RTOS != 0)
/* RTOS 时基来自内核节拍，原型由 task.h 提供（.c 请自行 #include
 * "FreeRTOS.h" 和 "task.h"）；configTICK_RATE_HZ = 1000，
 * 即 1 tick = 1ms，可直接当毫秒用 */
#endif

/* ================================================================
 *  sys_tick.h — 【系统】SysTick 硬件定时器模块  头文件
 * ================================================================
 *  功能：毫秒时基 + 微秒延时，用 Cortex-M4 内核 24 位递减计数器，不占外设定时器。
 *  SysTick->LOAD/VAL/CTRL 见 CMSIS core_cm4.h；delay.h 的 delay_ms 为软件空循环，
 *  主频/优化一变即失准，本模块为硬件计数。
 *  SysTick 为内核独占。用中断方式，每 SYS_TICK_PERIOD_MS 触发 SysTick_Handler
 *  使毫秒计数器 +1。该函数在本模块 .c 内弱定义，手写同名函数会顶替之；中断优先级
 *  用内核复位默认值，统一规划用 sys_nvic。
 *  约束：SYS_CLK_Switch() 换主频后重装值仍按旧主频，须重调 SYS_TICK_Init()：
 *      if (SYS_CLK_Switch(target) == SYS_CLK_OK) { SYS_TICK_Init(); }
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* SysTick 中断周期（ms）：1 = 每 1ms 中断一次（默认）
 * ms 时基（Delay_ms / GetTick / Elapsed）按该周期工作 */
#define SYS_TICK_PERIOD_MS   1


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：按 SystemCoreClock 算 1ms 重装值并启动中断计时
 * 调用时机：main 开头；SYS_CLK_Switch() 换主频后重调
 * 标准库 : 无—寄存器直写 SysTick->LOAD / VAL / CTRL */
void     SYS_TICK_Init    (void);

/* 精确阻塞延时（ms）：靠毫秒计数器差值判断，与主频无关，延时期间 CPU 空转
 * 不得在中断/回调内使用：依赖 SysTick 中断累加时基，配置不当会在 ISR 内死等；
 *   中断内延时用 delay.h 的 delay_ms()。示例 SYS_TICK_Delay_ms(500); */
void     SYS_TICK_Delay_ms(uint32_t ms);

/* 取当前毫秒时刻（时间戳）：上电从 0 累加，约 49.7 天回绕 */
uint32_t SYS_TICK_GetTick (void);

/* 计算 start_tick 至今过去的 ms
 * 溢出安全：基于无符号减法，回绕瞬间结果仍正确 */
uint32_t SYS_TICK_Elapsed (uint32_t start_tick);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 微秒级阻塞延时（1 ~ 1000us，超出按 1000 处理）  已过时，新代码用 delay_us()
 * 轮询 SysTick->VAL 实现（1ms 中断粒度不够微秒用），阻塞忙等；
 * 替代 delay_us(us)（delay.c DWT 周期计数器，误差 < 1us，用前 delay_dwt_init()）；
 * FreeRTOS 任务内 > 1ms 等待用 vTaskDelay(ms)。不加 __attribute__((deprecated))：
 * 会给调用方刷 Warning（验收要求两板 0 Error 0 Warning）；裸机工程与老代码仍依赖它 */
void     SYS_TICK_Delay_us(uint32_t us);

/* 秒级阻塞延时（内部按 1 秒步进循环，支持任意 uint32 秒数）
 * 示例 : SYS_TICK_Delay_s(2); */
void     SYS_TICK_Delay_s (uint32_t s);

/* 微秒级时间戳（毫秒部分 + 当前毫秒周期内微秒数）：计数器轮询拼合，典型误差几个微秒；约 49.7 天回绕 */
uint32_t SYS_TICK_GetUs   (void);

/* 超时判断辅助（非阻塞）：起始时间戳 + 超时时间，返回 1=已超时 / 0=未到
 * 用法 : uint32_t t = SYS_TICK_GetTick();
 *        while (!SYS_TICK_Timeout(t, 500)) { }   // 最多等 500ms
 * 溢出安全：内部用 SYS_TICK_Elapsed 差值判断 */
uint8_t  SYS_TICK_Timeout (uint32_t start_tick, uint32_t timeout_ms);

/* 周期节拍辅助（非阻塞）：判断"距上次执行已满 period_ms"
 * 参数 : last_tick — 上次执行时刻变量（首次传 0）；period_ms — 周期 ms，0 视为 1
 * 返回 : 1 = 到点（内部已把 *last_tick 更新为当前时刻）；0 = 未到
 * 用法 : static uint32_t t = 0; if (SYS_TICK_Every(&t, 200)) { ... } */
uint8_t  SYS_TICK_Every (uint32_t *last_tick, uint32_t period_ms);

#endif /* __FWLIB_SYS_TICK_H */

