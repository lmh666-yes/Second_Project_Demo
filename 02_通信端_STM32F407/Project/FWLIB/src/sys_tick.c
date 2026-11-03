#include "sys_tick.h"
/* 接口说明见 sys_tick.h */
#include <stddef.h>     /* NULL */

/* FreeRTOS 头文件必须在 sys_tick.h 之后引入：
 * sys_tick.h 靠 configUSE_PREEMPTION 等宏判定 SYS_TICK_USE_RTOS */
#if defined(SYS_TICK_USE_RTOS) && (SYS_TICK_USE_RTOS != 0)
#include "FreeRTOS.h"
#include "task.h"
#endif

/* sys_tick.c：SysTick 硬件定时器模块 实现文件
 *
 * 裸机模式时基：SysTick 每计满 1ms 触发中断，systick_ms 累加，
 * 延时与计时基于该毫秒计数器做差值判断。
 *
 * RTOS 模式：FreeRTOSConfig.h 中
 * #define xPortSysTickHandler SysTick_Handler，port.c 里该函数为强定义，
 * 与本地同名定义冲突。因此本文件在 RTOS 模式下不编译
 * SysTick_Handler，时基直接读内核节拍 xTaskGetTickCount()。
 */

/* 毫秒计数器：中断里累加，主循环里读取，仅裸机模式使用 */
#if !defined(SYS_TICK_USE_RTOS) || (SYS_TICK_USE_RTOS == 0)
/*  volatile：中断与主循环共享，防止编译器把变量缓存到寄存器读到旧值
 *  无符号 32 位，约 49.7 天回绕，差值读取在回绕瞬间仍正确 */
static volatile uint32_t systick_ms = 0;
#endif


/* 区块 2：基础功能 */
/* 初始化：按 SYS_TICK_PERIOD_MS 配置周期中断，默认 1ms
 *   重装值 = SystemCoreClock / 1000 × 周期ms - 1
 *   168MHz、1ms 时为 167999，未超过 24 位计数器上限 0xFFFFFF
 *   CTRL：HCLK 时钟源 + 中断使能 + 计数器使能；最后清零毫秒计数器
 *
 * FreeRTOS 模式：本函数为空操作。SysTick 归内核调度使用，
 *   写 LOAD/CTRL 会破坏调度节拍。 */
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
/* 中断服务函数，仅裸机模式定义。函数名由启动文件固定，不可改名；
 * 中断内只做累加，耗时越短对系统影响越小
 *
 * __weak：单独使用本库时它就是 SysTick 中断入口；与 FreeRTOS 混用时
 * 被上面的 #if 排除，不参与编译，避免与 port.c 的强定义抢符号。 */
__weak void SysTick_Handler(void)
{
    systick_ms += SYS_TICK_PERIOD_MS;    /* 每个中断周期累加对应毫秒数 */
}
#endif

/* 阻塞延时：记下起始时刻，等待计数器差值达到 ms
 * 差值判断可处理回绕：期间计数器归零也不影响结果 */
void SYS_TICK_Delay_ms(uint32_t ms)
{
    uint32_t start = SYS_TICK_GetTick();
    while ((SYS_TICK_GetTick() - start) < ms);
}

/* 取当前时间戳，单位毫秒
 * RTOS 模式：换算内核节拍，本工程 1 tick = 1ms
 * 裸机模式：读中断累加的 systick_ms */
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


/* 区块 3：扩展功能 */
/* 微秒级延时，轮询计数器实现，不依赖中断计数
 *   目标 tick 数 = 1us 的 tick 数 × us，等待计数器递减穿过目标位置
 *   参数限幅 ≤1000us，含边界保护；需先调用 SYS_TICK_Init() 使计数器运行；
 *   依赖 SystemCoreClock 正确，切换时钟后需重新 Init
 *
 * FreeRTOS 模式：本函数为空操作。它写 SysTick->VAL = 0，而该寄存器
 *   此刻由内核维护，清零会打乱调度节拍。微秒延时改用 delay.h 的
 *   DWT 版（delay_us / delay_ms_dwt），不占用 SysTick。 */
void SYS_TICK_Delay_us(uint32_t us)
{
#if !defined(SYS_TICK_USE_RTOS) || (SYS_TICK_USE_RTOS == 0)
    uint32_t ticks;
    uint32_t target;

    if (us == 0) return;
    if (us > 1000) us = 1000;

    ticks = us * (SystemCoreClock / 1000000U);

    /* 边界保护：ticks 大于 LOAD 时直接做减法会下溢，
     * 退化为等待一个完整计数周期，误差可忽略 */
    target = (ticks > SysTick->LOAD) ? 0U : (SysTick->LOAD - ticks);

    SysTick->VAL = 0;

    /* 先确认计数器已重装，避免第一拍读到 0 就提前退出，
     * 再等它递减穿过目标位置 */
    while (SysTick->VAL < (SysTick->LOAD / 2U));
    while (SysTick->VAL > target);
#else
    (void)us;
#endif
}


/* 秒级延时：按 1 秒步进循环（避免 s×1000 直接相乘的溢出问题） */
void SYS_TICK_Delay_s(uint32_t s)
{
    while (s--) {
        SYS_TICK_Delay_ms(1000U);
    }
}

/* 微秒时间戳 = 毫秒计数 × 1000 + 当前毫秒周期内已过的微秒数
 *   周期内已过 tick 数 = LOAD - VAL，换算微秒 ticks × 1000 / (LOAD + 1)
 *   do-while 重读保证 ms 与 VAL 取自同一毫秒周期，避免跨越中断点
 *
 * FreeRTOS 模式只返回 毫秒 × 1000，不含亚毫秒部分：此时
 *   SysTick->VAL 由内核维护，读它得不到稳定的亚毫秒偏移。
 *   需要微秒时间戳时用 delay.h 的 DWT 版。 */
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

/* 周期节拍，非阻塞：到点返回 1 并推进时间戳，未到返回 0
 * *last_tick 为 0（首次调用）时，若距上电已超过一个周期会立即
 * 放行一次，不吞掉第一个周期 */
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

