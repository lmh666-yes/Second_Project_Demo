#ifndef __FWLIB_SYS_TICK_H
#define __FWLIB_SYS_TICK_H

#include "stm32f4xx.h"

/* 模式选择：SysTick 是内核独占资源，全芯片只能有一个使用者。
 * FreeRTOS 端口层 port.c 靠 SysTick 产生调度节拍，其 xPortSysTickHandler 经
 * FreeRTOSConfig.h 的 #define xPortSysTickHandler SysTick_Handler 映射到该函数。
 * 裸机模式（默认）：本模块定义弱 SysTick_Handler 累加毫秒计数器，SYS_TICK_Init()
 * 配置 LOAD/CTRL 并开中断。
 * RTOS 模式：本模块不定义 SysTick_Handler，避免与 port.c 抢符号，时基读内核节拍
 * xTaskGetTickCount()；SYS_TICK_Init() 为空操作，调用会重写 LOAD/CTRL 与 RTOS 抢
 * SysTick，微秒级延时用 delay.h 的 DWT 版。
 * 显式定义 SYS_TICK_USE_RTOS=0 可强制走裸机分支。 */
#ifndef SYS_TICK_USE_RTOS
#  if defined(SYS_RTOS_PRESENT)
     /* 若已包含过 sys_rtos.h，直接沿用它的统一判定结果 */
#    define SYS_TICK_USE_RTOS   SYS_RTOS_PRESENT
#  elif (defined(INC_FREERTOS_H) || defined(configUSE_PREEMPTION) || \
         defined(configTICK_RATE_HZ))
     /* 否则就地判定：工程里包含过 FreeRTOS.h / FreeRTOSConfig.h 即算 RTOS */
#    define SYS_TICK_USE_RTOS   1
#  else
#    define SYS_TICK_USE_RTOS   0
#  endif
#endif

/* sys_tick.h: SysTick 硬件定时器模块头文件
 * 毫秒时基 + 微秒延时，基于 Cortex-M4 内核 24 位递减计数器，不占用外设定时器。
 * 直接操作内核寄存器 SysTick->LOAD/VAL/CTRL（CMSIS 头 core_cm4.h 定义），
 * 标准外设库不提供 SysTick API。
 * delay.h 的 delay_ms 是 DWT 忙等，占用 CPU；本模块由中断计数，不占 CPU，
 * 改主频后重新 Init 仍准确。
 *
 * 1) 仅裸机模式下每 1ms 触发 SysTick_Handler 给毫秒计数器加 1。
 * 2) SysTick 为内核独占，其它代码或库（含 RTOS）也配置会相互冲突；RTOS 下本模块
 *    自动让位（见文件头模式选择），全部 API 照常可用。
 * 3) 换主频后 SysTick 的 LOAD 仍是旧主频的值，重装由 SYS_CLK_Switch() 内部完成
 *    （sys_rtos.h 的 SYS_RTOS_SYSTICK_RELOAD()），调用方不需要手动 Init；
 *    切换后必须重做的模块清单见 sys_clock.h 顶部。
 * 4) SysTick_Handler 只在裸机模式的 .c 内定义（弱定义），手写同名函数会顶替库版，
 *    顶替后本模块计时与延时全部停用。
 * 5) 本模块不设置 SysTick 中断优先级，保持内核复位默认值；统一规划优先级用
 *    sys_nvic 模块，上 RTOS 后由 RTOS 接管。 */


/* SysTick 中断周期（ms）：1 = 每 1ms 中断一次，ms 时基（Delay_ms/GetTick/Elapsed）
 * 按该周期工作；改大周期会降低 ms 计时精度 */
#define SYS_TICK_PERIOD_MS   1


/* 初始化：按 SystemCoreClock 计算 1ms 重装值并启动中断计时，仅裸机模式有效。
 * 直接写寄存器 SysTick->LOAD/VAL/CTRL（CMSIS 定义）。
 * FreeRTOS 模式下为空操作：SysTick 归内核调度使用，重写 LOAD/CTRL 会破坏
 * 调度节拍；切主频后的重装由 SYS_CLK_Switch() 内部按模式完成。 */
void     SYS_TICK_Init    (void);

/* 阻塞延时（ms）：按毫秒计数器差值判断，与主频无关，延时期间 CPU 空转。
 * 不能在中断/回调里使用：它依赖 SysTick 中断继续累加时基，中断优先级配置不当
 * 时会在 ISR 内死等；中断内延时用 delay.h 的 delay_ms()，纯忙等、不依赖中断。 */
void     SYS_TICK_Delay_ms(uint32_t ms);

/* 取当前毫秒时刻（时间戳）：上电从 0 开始累加，约 49.7 天回绕 */
uint32_t SYS_TICK_GetTick (void);

/* 计算 start_tick 至今过去的毫秒数：基于无符号减法，计数器回绕瞬间结果仍正确 */
uint32_t SYS_TICK_Elapsed (uint32_t start_tick);


/* 微秒级阻塞延时（1 ~ 1000us，超出按 1000 处理）：轮询 SysTick->VAL，需先调用
 * SYS_TICK_Init() 使计数器运行；RTOS 模式下为空操作。已过时，新代码用 delay_us(us)
 * （delay.c 的 DWT 周期计数器，实测误差 < 1us，用前调 delay_dwt_init()），毫秒级
 * 用 delay_ms_dwt(ms) / delay_ms(ms)，RTOS 任务内等 1ms 以上用 vTaskDelay(ms)。 */
void     SYS_TICK_Delay_us(uint32_t us);

/* 秒级阻塞延时：内部按 1 秒步进循环，支持任意 uint32 秒数 */
void     SYS_TICK_Delay_s (uint32_t s);

/* 获取微秒级时间戳（毫秒部分 + 当前毫秒周期内的微秒数）：轮询计数器拼合，典型误差
 * 几个微秒，适合测脉冲宽度与短过程耗时，约 49.7 天回绕；RTOS 模式下只返回毫秒 ×
 * 1000，无亚毫秒部分 */
uint32_t SYS_TICK_GetUs   (void);

/* 超时判断辅助（非阻塞）：传入起始时间戳与超时时间，返回 1 = 已超时，0 = 未到。
 * 内部用 SYS_TICK_Elapsed 差值判断，计数器回绕安全。 */
uint8_t  SYS_TICK_Timeout (uint32_t start_tick, uint32_t timeout_ms);

/* 周期节拍辅助（非阻塞）：距上次执行已满 period_ms 时返回 1，未到返回 0；
 * 到点时内部已把 *last_tick 更新为当前时刻。last_tick 保存上次执行时刻，
 * 首次传 0；period_ms 单位 ms，0 视为 1。每处任务各自持有变量，多路节拍互不干扰。 */
uint8_t  SYS_TICK_Every (uint32_t *last_tick, uint32_t period_ms);

#endif /* __FWLIB_SYS_TICK_H */

