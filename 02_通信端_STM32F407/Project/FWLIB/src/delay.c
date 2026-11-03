#include "delay.h"

/* 延时函数：粗延时为 volatile 空循环，未做标定；DWT 系列为忙等周期差值 */


/* 空转 n 次；volatile 防止编译器删除空循环 */
void delay_loop(volatile uint32_t n)
{
    for (volatile uint32_t i = 0; i < n; i++);
}

/* 毫秒级延时：循环调用 delay_us(1000)，精度随 SystemCoreClock
 * 空循环系数未标定误差大；无 DWT 时用 delay_loop() */
void delay_ms(uint32_t ms)
{
    while (ms > 0U) {
        delay_us(1000U);
        ms--;
    }
}


/* 精准短延时（DWT 计数器）
 * 换算用 32 位整数：64 位除法调 __aeabi 库函数，数百周期开销破坏短延时精度
 * 差值法比较：CYCCNT 回绕（约 25.6s @168MHz）时仍正确 */

/* DWT 使能（幂等）：TRCENA 跟踪总开关，CYCCNTENA 启动周期计数，不依赖调试器 */
static void delay_dwt_enable(void)
{
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0UL) return;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
}

/* 忙等 cycles 个 CPU 周期 */
void delay_cycles(uint32_t cycles)
{
    uint32_t start;

    if (cycles == 0U) return;
    delay_dwt_enable();

    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles) { }
}

/* 微秒级：1 微秒 = 主频 MHz 个 CPU 周期
 * 32 位换算溢出上限见 delay.h */
void delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t cycles;

    if (us == 0U) return;
    delay_dwt_enable();

    cycles = us * (SystemCoreClock / 1000000U);

    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles) { }
}

/* 纳秒级：ns × MHz ÷ 1000，+500 四舍五入，先乘后除保证精度 */
void delay_ns(uint32_t ns)
{
    uint32_t start;
    uint32_t cycles;

    if (ns == 0U) return;
    delay_dwt_enable();

    cycles = (ns * (SystemCoreClock / 1000000U) + 500U) / 1000U;

    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles) { }
}

/* 毫秒级：ms × (主频/1000) 个周期，单次上限约 25.5s@168MHz
 * 忙等，不占用 SysTick 与中断，FreeRTOS 下可用 */
void delay_ms_dwt(uint32_t ms)
{
    uint32_t start;
    uint32_t cycles;
    uint32_t cycles_per_ms;

    if (ms == 0U) return;
    delay_dwt_enable();

    cycles_per_ms = SystemCoreClock / 1000U;
    if (ms > (0xFFFFFFFFUL / cycles_per_ms)) ms = 0xFFFFFFFFUL / cycles_per_ms;

    cycles = ms * cycles_per_ms;

    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles) { }
}

/* DWT 计时读数（与延时共用 CYCCNT 计数器，首次调用自动使能）
 * 用于测脉宽与短过程耗时；比较时间必须用差值法，DWT_ElapsedUs 内部相减 */
uint32_t DWT_GetCycles(void)
{
    delay_dwt_enable();
    return DWT->CYCCNT;
}

/* 微秒时间戳（自由运行，约 71 分钟回绕），比较须用 DWT_ElapsedUs */
uint32_t DWT_GetUs(void)
{
    delay_dwt_enable();
    return DWT->CYCCNT / (SystemCoreClock / 1000000U);
}

/* 距时间戳 start_us 已过的微秒数（无符号相减，回绕安全） */
uint32_t DWT_ElapsedUs(uint32_t start_us)
{
    return DWT_GetUs() - start_us;
}
