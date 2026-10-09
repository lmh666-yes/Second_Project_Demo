#include "delay.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */

/* ================================================================
 *  delay.c —— 【通用】延时函数  实现文件
 * ================================================================
 *  来源 : 由 gpio_core.c 拆分独立（2026-10-08）。
 *  分两套 :
 *    粗延时 —— volatile 空循环（防优化删除;未标定）;
 *    DWT    —— 忙等周期差值（换算全 32 位整数,避免 64 位库函数开销;
 *              差值法对 CYCCNT 回绕天然安全）。
 * ================================================================ */


/* ================================================================
 *                    区块 2：基础功能（粗延时）
 * ================================================================
 * 未做硬件标定，精度说明见 delay.h。
 * volatile 修饰不可省略：防止编译器判定"空循环无作用"而将其删除。 */

/* 空转 n 次：最细粒度的延时单元，供上层组合出更长延时 */
void delay_loop(volatile uint32_t n)
{
    for (volatile uint32_t i = 0; i < n; i++);
}

/* 毫秒级粗延时：外层 ms 次 × 内层约 50000 次空转 ≈ 1ms @168MHz */
void delay_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        for (volatile uint32_t j = 0; j < 50000; j++);
    }
}


/* ================================================================
 *        精准短延时（DWT 周期计数器）—— 实现
 * ================================================================
 * 为什么这么实现 :
 *   ① 换算全用 32 位整数 —— 64 位除法会调 __aeabi 库函数（数百
 *      周期开销），会直接毁掉"纳秒级"的意义；
 *   ② 三个函数各自内联"读数-比较"忙等循环 —— 时延路径上少一层
 *      函数调用，只有 DWT 使能是共用的；
 *   ③ 差值比较法：CYCCNT 32 位回绕（约 25.6s @168MHz）也正确。 */

/* DWT 使能（幂等）：TRCENA 跟踪总开关 → CYCCNTENA 周期计数启动
 * 说明 : 不依赖调试器，程序自己置位即可（首次调用自动完成） */
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

/* 毫秒级：ms × (主频/1000) 个周期（差值法,支持 CYCCNT 回绕,单次 ≤ 约 25.5s@168MHz）
 * 与 us/ns 同机制:不占 SysTick / 不占中断——FreeRTOS 下照样可用;
 * 属于忙等:任务里超长延时优先 vTaskDelay,驱动级毫秒时序用它最稳 */
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
