#include "sys_tick.h"
#include <stddef.h>     /* NULL（周期节拍辅助的指针判断） */
#include "sys_rtos.h"   /* SYS_RTOS_PRESENT：统一的"有没有上 FreeRTOS"判定 */

/* RTOS 模式需 FreeRTOS.h + task.h 提供 xTaskGetTickCount()；不依赖端口层私有符号 */
#if defined(SYS_TICK_USE_RTOS) && (SYS_TICK_USE_RTOS != 0)
#include "FreeRTOS.h"
#include "task.h"
#endif

/* ================================================================
 *  sys_tick.c — 【系统】SysTick 硬件定时器模块  实现文件
 * ================================================================
 *  裸机：SysTick 每 1ms 中断累加 systick_ms，延时/计时按差值判断。
 *  RTOS：FreeRTOSConfig.h 的 xPortSysTickHandler → SysTick_Handler 为强定义，
 *  顶掉此处的 __weak 版；时基改读 xTaskGetTickCount()。
 * ================================================================ */

/* 毫秒计数器（中断累加，主循环读取）；仅裸机模式使用 */
#if !defined(SYS_TICK_USE_RTOS) || (SYS_TICK_USE_RTOS == 0)
/* 必须 volatile：中断/主循环共享，防编译器缓存到寄存器；uint32 约 49.7 天回绕 */
static volatile uint32_t systick_ms = 0;
#endif


/* ---------------------- 区块 2：基础功能 ---------------------- */
/* 初始化：按 SYS_TICK_PERIOD_MS 配置周期中断（默认 1ms）。
 *  重装值 = SystemCoreClock / 1000 × 周期ms - 1（168MHz → 167999，24 位满 0xFFFFFF）。
 *  CTRL：HCLK 时钟源 + 中断使能 + 计数器使能；最后清零 systick_ms。
 *  RTOS 模式为空操作：写 LOAD/CTRL 会与内核调度抢 SysTick。 */
void SYS_TICK_Init(void)
{
#if !defined(SYS_TICK_USE_RTOS) || (SYS_TICK_USE_RTOS == 0)
    uint32_t reload = (SystemCoreClock / 1000U) * SYS_TICK_PERIOD_MS - 1U;

    SysTick->LOAD = reload;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |   /* HCLK 时钟源 */
                    SysTick_CTRL_TICKINT_Msk   |   /* 使能中断 */
                    SysTick_CTRL_ENABLE_Msk;

    systick_ms = 0;
#endif
}

#if !defined(SYS_TICK_USE_RTOS) || (SYS_TICK_USE_RTOS == 0)
/* 中断服务函数（仅裸机模式定义）：函数名由启动文件固定，不可改名。
 * 弱定义：单独用时即 SysTick 中断入口；RTOS 模式下被上面的 #if 排除编译，
 * 不与 port.c 强定义抢符号。中断内只累加。 */
__weak void SysTick_Handler(void)
{
    systick_ms += SYS_TICK_PERIOD_MS;    /* 每个中断周期累加对应毫秒数 */
}
#endif

/* 阻塞延时：等待计数器差值达到 ms；差值判断在回绕时依然正确 */
void SYS_TICK_Delay_ms(uint32_t ms)
{
    uint32_t start = SYS_TICK_GetTick();
    while ((SYS_TICK_GetTick() - start) < ms);
}

/* 取当前时间戳（毫秒）
 *   RTOS 模式  : 直接换算内核节拍（本工程 1 tick = 1ms）
 *   裸机模式  : 读中断累加的 systick_ms */
uint32_t SYS_TICK_GetTick(void)
{
#if defined(SYS_TICK_USE_RTOS) && (SYS_TICK_USE_RTOS != 0)
    return (uint32_t)xTaskGetTickCount();
#else
    return systick_ms;
#endif
}

/* 已过去的时间：无符号减法，回绕安全 */
uint32_t SYS_TICK_Elapsed(uint32_t start_tick)
{
    return SYS_TICK_GetTick() - start_tick;
}


/* ---------------------- 区块 3：扩展功能 ---------------------- */
/* 微秒级延时（轮询计数器，不依赖中断）：ticks = us × (SystemCoreClock/1MHz)。
 * 约束：us 限幅 1~1000；需先调用 SYS_TICK_Init() 使计数器运行；
 * SystemCoreClock 须与实际主频一致（切换时钟后重新 Init）。
 * RTOS 模式为空操作：SysTick->VAL 归内核维护，清零会打乱调度节拍。 */
void SYS_TICK_Delay_us(uint32_t us)
{
#if !defined(SYS_TICK_USE_RTOS) || (SYS_TICK_USE_RTOS == 0)
    uint32_t ticks;
    uint32_t target;

    if (us == 0) return;
    if (us > 1000) us = 1000;

    ticks = us * (SystemCoreClock / 1000000U);

    /* 边界保护：ticks > LOAD 时减法会下溢，退化为等待一个完整计数周期 */
    target = (ticks > SysTick->LOAD) ? 0U : (SysTick->LOAD - ticks);

    SysTick->VAL = 0;

    /* 先等计数器重装（避免第一拍读到 0 就退出），再等它递减穿过目标位置 */
    while (SysTick->VAL < (SysTick->LOAD / 2U));
    while (SysTick->VAL > target);
#else
    (void)us;
#endif
}


/* 秒级延时：按 1 秒步进循环，避免 s×1000 溢出 */
void SYS_TICK_Delay_s(uint32_t s)
{
    while (s--) {
        SYS_TICK_Delay_ms(1000U);
    }
}

/* 微秒时间戳 = ms × 1000 + 当前毫秒周期内已过微秒数。
 * 周期内已过 tick = LOAD - VAL；换算 ticks × 1000 / (LOAD + 1)；
 * do-while 重读保证 ms 与 VAL 同属一个毫秒周期，防跨越中断点。
 * RTOS 模式只返回 ms × 1000（SysTick->VAL 归内核维护，无亚毫秒偏移）。 */
uint32_t SYS_TICK_GetUs(void)
{
#if defined(SYS_TICK_USE_RTOS) && (SYS_TICK_USE_RTOS != 0)
    return SYS_TICK_GetTick() * 1000U;
#else
    uint32_t ms;
    uint32_t val;

    do {
        ms  = systick_ms;
        val = SysTick->VAL;
    } while (ms != systick_ms);

    return (ms * 1000U) + ((SysTick->LOAD - val) * (SYS_TICK_PERIOD_MS * 1000U) / (SysTick->LOAD + 1U));
#endif
}

/* 超时判断：复用 Elapsed 的差值逻辑，回绕安全 */
uint8_t SYS_TICK_Timeout(uint32_t start_tick, uint32_t timeout_ms)
{
    return (SYS_TICK_Elapsed(start_tick) >= timeout_ms) ? 1 : 0;
}

/* 周期节拍（非阻塞）：到点返回 1 并推进时间戳；未到返回 0
 * 首拍行为：*last_tick 为 0（首次调用）时，若距上电已超过一个
 * 周期会立即放行一次（不会吞掉第一个周期） */
uint8_t SYS_TICK_Every(uint32_t *last_tick, uint32_t period_ms)
{
    if (last_tick == NULL) return 0;
    if (period_ms == 0U) period_ms = 1U;

    if (SYS_TICK_Elapsed(*last_tick) >= period_ms) {
        *last_tick = SYS_TICK_GetTick();
        return 1;
    }
    return 0;
}

