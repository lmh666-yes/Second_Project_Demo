#ifndef __FWLIB_SYS_RTOS_H
#define __FWLIB_SYS_RTOS_H

#include "stm32f4xx.h"

/* sys_rtos.h — 工程是否运行 FreeRTOS 的判定开关（SYS_RTOS_PRESENT）
 * 判定：INC_FREERTOS_H 或 configUSE_PREEMPTION / configTICK_RATE_HZ 已定义则为 1；
 *   也可在包含本头文件前自己 #define SYS_RTOS_PRESENT 0/1 强制指定。
 * 影响：SYS_RTOS_SYSTICK_RELOAD() 走 vPortSetupTimerInterrupt() 还是 SYS_TICK_Init()。
 * 注意：自动判定只认"包含本头文件之前"已定义的那些符号，故 FreeRTOS.h /
 *   FreeRTOSConfig.h 需先于本头文件包含；需要确定结果时用宏强制指定。 */

#ifndef SYS_RTOS_PRESENT
#  if defined(INC_FREERTOS_H) || defined(configUSE_PREEMPTION) || defined(configTICK_RATE_HZ)
#    define SYS_RTOS_PRESENT   1
#  else
#    define SYS_RTOS_PRESENT   0
#  endif
#endif


/* ================================================================
 *  给 sys_clock 用的：切主频后按新频率重装 SysTick
 * ================================================================
 *  SysTick 重装值是编译期常量（port.c 的
 *  configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ - 1），
 *  SystemCoreClockUpdate() 只改 SystemCoreClock 变量，
 *  不改已写入 SysTick->LOAD 的值，故切主频（如 168MHz → HSI 16MHz）
 *  后必须重装一次，否则节拍被拉长约 10.5 倍。
 *  调用时机：调度器未启动或已 vTaskSuspendAll()；改 LOAD/VAL 会打断
 *  正在进行的节拍。 */
#if (SYS_RTOS_PRESENT != 0)
/* 原型来自 port.c（非 static）；各版本 CMSIS 端口均提供此函数名 */
void vPortSetupTimerInterrupt(void);

#define SYS_RTOS_SYSTICK_RELOAD()   do { vPortSetupTimerInterrupt(); } while (0)
#else
/* 裸机：SysTick 归 sys_tick 模块，重装即重新 Init。
 * 只放前向声明，不 #include "sys_tick.h"：否则两文件互相包含，
 * 包含顺序会决定 SYS_RTOS_PRESENT 是否已定义，判定结果不稳定。 */
void SYS_TICK_Init(void);

#define SYS_RTOS_SYSTICK_RELOAD()   do { SYS_TICK_Init(); } while (0)
#endif

#endif /* __FWLIB_SYS_RTOS_H */
