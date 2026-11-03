#ifndef __FWLIB_SYS_RTOS_H
#define __FWLIB_SYS_RTOS_H

#include "stm32f4xx.h"

/* sys_rtos.h: 工程是否运行在 FreeRTOS 下的统一判定
 * SYS_RTOS_PRESENT 为 1 表示 RTOS 工程，0 表示裸机。sys_tick 有 RTOS 时
 * SysTick 归内核调度，本模块不能自己累加时基，否则 SYS_TICK_GetTick 恒 0；
 * sys_clock 运行中切主频后要按新频率重装 SysTick；w25qxx_log 写侧与读侧
 * 分属不同任务，需任务级互斥，擦除最坏 50 ms，用 vTaskSuspendAll 而不是
 * 关中断；sys_adc/sys_dma 能否在 ISR 里调 FromISR 取决于抢占优先级。
 *
 * 判定方式，命中任意一个即认为是 RTOS 工程：
 *     1) 工程里显式定义过 SYS_RTOS_PRESENT（在工程宏里加）；
 *     2) 已经包含了 FreeRTOS.h（INC_FREERTOS_H）；
 *     3) 已经包含了 FreeRTOSConfig.h（configUSE_PREEMPTION / configTICK_RATE_HZ）。
 *
 * 强制指定：在包含本头文件之前 #define SYS_RTOS_PRESENT 为 0（按裸机处理）
 * 或 1（按 RTOS 处理）。 */

#ifndef SYS_RTOS_PRESENT
#  if defined(INC_FREERTOS_H) || defined(configUSE_PREEMPTION) || defined(configTICK_RATE_HZ)
#    define SYS_RTOS_PRESENT   1
#  else
#    define SYS_RTOS_PRESENT   0
#  endif
#endif


/* 给 sys_clock 用的：切主频后按新频率重装 SysTick
 * SysTick 重装值由 port.c 按 configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ - 1
 * 编译期算好，SystemCoreClockUpdate() 只改内存里的 SystemCoreClock 变量，
 * 改不了已写入 SysTick->LOAD 的值；运行中切主频（例如 168MHz 切到 HSI 16MHz）
 * 必须重装一次，否则节拍被拉长约 10.5 倍。
 *
 * 实现：调用 FreeRTOS 端口层自带的 vPortSetupTimerInterrupt()，它是 port.c
 * 里的 __weak 函数，按当前 CPU 频率把 LOAD/VAL/CTRL 一次配好。
 *
 * 调用时机：调度器未启动，或已 vTaskSuspendAll()，否则改 LOAD/VAL 会打断
 * 正在进行的节拍。SYS_CLK_Switch() 要求不在中断内调用、由调用方保证独占。 */
#if (SYS_RTOS_PRESENT != 0)
/* 原型来自 port.c，非 static，可直接调用；
 * 不同 FreeRTOS 版本的 CMSIS 端口都提供这个函数名 */
void vPortSetupTimerInterrupt(void);

#define SYS_RTOS_SYSTICK_RELOAD()   do { vPortSetupTimerInterrupt(); } while (0)
#else
/* 裸机：SysTick 归 sys_tick 模块管，重装即重新 Init 一次
 * 使用处自行 #include "sys_tick.h" 取原型；本文件不包含它，避免互相包含改变 SYS_RTOS_PRESENT 判定 */
#define SYS_RTOS_SYSTICK_RELOAD()   do { SYS_TICK_Init(); } while (0)
#endif

#endif /* __FWLIB_SYS_RTOS_H */
