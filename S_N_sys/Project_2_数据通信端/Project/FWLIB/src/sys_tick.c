#include "sys_tick.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include <stddef.h>     /* NULL（周期节拍辅助的指针判断） */

/* ================================================================
 *  sys_tick.c —— 【系统】SysTick 硬件定时器模块  实现文件
 * ================================================================
 *  时基原理 : SysTick 每计满 1ms 触发中断 → systick_ms++，
 *  延时/计时全部基于这个毫秒计数器做"差值判断"，
 *  因此不受主频变化影响（重新 Init 后依然精确）。
 * ================================================================ */

/* 毫秒计数器（中断里累加，主循环里读取）
 *  必须 volatile：跨"中断/主循环"共享，防止编译器把变量缓存到
 *  寄存器导致读到旧值；无符号 32 位，约 49.7 天自然回绕，
 *  所有"差值读取"在回绕瞬间依然正确。 */
static volatile uint32_t systick_ms = 0;


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：按 SYS_TICK_PERIOD_MS 配置周期中断（默认 1ms）
 *   重装值 = SystemCoreClock / 1000 × 周期ms - 1
 *     （168MHz、1ms → 167999，24 位计数器上限 0xFFFFFF，满足要求）
 *   CTRL 位：HCLK 时钟源 + 中断使能 + 计数器使能
 *   最后清零毫秒计数器：从 0 重新计时 */
void SYS_TICK_Init(void)
{
    uint32_t reload = (SystemCoreClock / 1000U) * SYS_TICK_PERIOD_MS - 1U;

    SysTick->LOAD = reload;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |   /* HCLK 时钟源 */
                    SysTick_CTRL_TICKINT_Msk   |   /* 使能中断 */
                    SysTick_CTRL_ENABLE_Msk;

    systick_ms = 0;
}

/* 中断服务函数：函数名由启动文件固定，不可改名；保持最轻量——
 * 中断内只做累加，耗时越短对系统影响越小
 *
 * __weak（弱定义）说明 : 
 *   ① 单独使用本库时：它就是 SysTick 中断入口，行为与普通函数无异；
 *   ② 与 FreeRTOS 混用时：FreeRTOS 端口层提供"强定义"的
 *      SysTick_Handler，链接器自动用强符号覆盖本函数 → SysTick
 *      交给 RTOS 做调度节拍；此时 systick_ms 不再累加，
 *      SYS_TICK_Delay_ms / GetTick 等失效，延时请改用 vTaskDelay。 */
__weak void SysTick_Handler(void)
{
    systick_ms += SYS_TICK_PERIOD_MS;    /* 每个中断周期累加对应毫秒数 */
}

/* 阻塞延时：记下起始时刻，等待计数器差值达到 ms
 * 差值判断天然处理回绕：即使期间计数器归零也不影响结果 */
void SYS_TICK_Delay_ms(uint32_t ms)
{
    uint32_t start = systick_ms;
    while ((systick_ms - start) < ms);
}

/* 取当前时间戳 */
uint32_t SYS_TICK_GetTick(void)
{
    return systick_ms;
}

/* 已过去的时间：无符号减法，回绕安全 */
uint32_t SYS_TICK_Elapsed(uint32_t start_tick)
{
    return systick_ms - start_tick;
}


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 微秒级延时（轮询计数器实现，不依赖中断计数）
 *   目标 tick 数 = 1us 的 tick 数 × us；
 *   等待计数器递减穿过目标位置——约 ticks 个时钟 = us 微秒。
 * 限制与注意 : 
 *   ① 参数已限幅 ≤1000us，且含边界保护（1000us 不会瞬间返回）；
 *   ② 需先调用 SYS_TICK_Init()（计数器要处于运行状态）；
 *   ③ 依赖 SystemCoreClock 正确（切换时钟后需重新 Init）。 */
void SYS_TICK_Delay_us(uint32_t us)
{
    uint32_t ticks;
    uint32_t target;

    if (us == 0) return;
    if (us > 1000) us = 1000;

    ticks = us * (SystemCoreClock / 1000000U);

    /* 边界保护：ticks 比 LOAD 大时直接做减法会下溢，
     * 退化为"等待一个完整计数周期"（误差可忽略） */
    target = (ticks > SysTick->LOAD) ? 0U : (SysTick->LOAD - ticks);

    SysTick->VAL = 0;

    /* 先确认计数器已重装（避免第一拍读到 0 就提前退出），
     * 再等它递减穿过目标位置 */
    while (SysTick->VAL < (SysTick->LOAD / 2U));
    while (SysTick->VAL > target);
}


/* 秒级延时：按 1 秒步进循环（避免 s×1000 直接相乘的溢出问题） */
void SYS_TICK_Delay_s(uint32_t s)
{
    while (s--) {
        SYS_TICK_Delay_ms(1000U);
    }
}

/* 微秒时间戳 = 毫秒计数 × 1000 + 当前毫秒周期内已过的微秒数
 *   周期内已过 tick 数 = LOAD - VAL；
 *   换算微秒：ticks × 1000 / (LOAD + 1)
 *   do-while 重读：保证 ms 与 VAL 取自同一毫秒周期（防跨越中断点） */
uint32_t SYS_TICK_GetUs(void)
{
    uint32_t ms;
    uint32_t val;

    do {
        ms  = systick_ms;
        val = SysTick->VAL;
    } while (ms != systick_ms);

    return (ms * 1000U) + ((SysTick->LOAD - val) * (SYS_TICK_PERIOD_MS * 1000U) / (SysTick->LOAD + 1U));
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

