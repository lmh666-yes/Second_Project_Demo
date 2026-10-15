#include "delay.h"
/* 配套指引见同名 .h;本文件为实现层 */

/* delay.c — 通用延时函数实现
 * 毫秒 : delay_ms / delay_ms_dwt,复用 DWT,自动跟随 SystemCoreClock
 * 周期 : delay_cycles / delay_us / delay_ns,忙等 CYCCNT 差值
 * 空转 : delay_loop() 最细粒度原语,未标定,仅供组合 */


/* 区块 2：基础功能
 * volatile 不可省略:防止编译器判定空循环无作用而删除 */

/* 空转 n 次：最细粒度的延时单元，供上层组合出更长延时 */
void delay_loop(volatile uint32_t n)
{
    for (volatile uint32_t i = 0; i < n; i++);
}

/* 毫秒级延时：基于 delay_us,跟随 SystemCoreClock;禁止 DWT 时改用 delay_loop()
 * 供 DHT11 起始信号(≥18ms)、按键去抖、LED/BEEP 等时序使用 */
void delay_ms(uint32_t ms)
{
    while (ms > 0U) {
        delay_us(1000U);
        ms--;
    }
}


/* 精准短延时（DWT 周期计数器）
 * 换算全用 32 位整数,避免 64 位除法调用库函数
 * 各函数内联读数-比较忙等循环;差值比较法正确处理 CYCCNT 回绕（约 25.6s @168MHz） */

/* DWT 使能（幂等）：CoreDebug->DEMCR 置 CoreDebug_DEMCR_TRCENA_Msk 跟踪总开关 → DWT->CTRL 置 DWT_CTRL_CYCCNTENA_Msk
 * 不依赖调试器,首次调用自动完成 */
static void delay_dwt_enable(void)
{
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0UL) return;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
}

/* 原语：忙等 cycles 个 CPU 周期 */
void delay_cycles(uint32_t cycles)
{
    uint32_t start;

    if (cycles == 0U) return;
    delay_dwt_enable();

    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles) { }
}

/* 微秒级：1 微秒 = 主频 MHz 数个 CPU 周期（168MHz → 168 个）
 * 安全范围（32 位换算防溢出）见 delay.h 注释 */
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

/* 纳秒级：ns × MHz ÷ 1000，+500 四舍五入（先乘后除，保证精度） */
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

/* 毫秒级：ms × (主频/1000) 周期,差值法支持 CYCCNT 回绕,单次 ≤ 约 25.5s@168MHz
 * 与 us/ns 同机制,不占 SysTick / 不占中断;忙等,任务级长延时优先 vTaskDelay */
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
