#include "rgb5x5.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"

/* ================================================================
 *  rgb5x5.c —— 【板载】5x5 全彩 LED 阵列（WS2812B x25）  实现文件
 * ================================================================
 *  为什么这套驱动"不能被打断"？
 *
 *      WS2812B 不看时钟线，它**只靠高电平的持续时间**判断这个 bit 是 0 还是 1：
 *          高 0.35us = 0            高 0.70us = 1
 *      中间如果被中断打断 5us，灯就会把它当成"一个超长的高电平" → 数据错位，
 *      现象就是**颜色乱跳 / 只有前面几颗亮 / 整屏闪烁**。
 *      所以 Show() 里会 __disable_irq()，整帧发完再开回来（约 1ms）。
 *
 *  为什么可以用 DWT 计时而不需要定时器？
 *      Cortex-M4 内核自带一个 32 位自由运行的周期计数器 CYCCNT
 *      （gpio_core 的延时函数已经在用它）。读一次它只要一个指令周期，
 *      精度就是 CPU 主频，对 0.35us 这种量级完全够，还省下一个定时器 + DMA。
 *      代价是"要关中断"，这也是它和 PWM+DMA 方案的本质取舍。
 *
 *  RGB5X5_TIMING_NOTE（现场微调指引）
 *      下面 4 个 CYC_xxx 值是从 .h 传进来的，理论值 @168MHz：
 *          T0H = 0.35us = 59 周期   T0L = 0.90us = 151 周期
 *          T1H = 0.70us = 118 周期  T1L = 0.55us = 92 周期
 *      但"把引脚拉高"这条语句本身要花几个周期，所以实际等待要减掉
 *      RGB5X5_CYC_OVERHEAD。若现象是：
 *          · 全屏乱色 / 只亮第一颗   → overhead 估小了，把它加大到 16~24
 *          · 亮度普遍偏暗 / 偏亮     → 四个值整体按比例缩放
 *          · 完全没反应              → 先查 CN5 的 2↔3 有没有短接（灯板没供电）
 * ================================================================ */


/* ================================================================
 *                      内部状态
 * ================================================================ */
/* 帧缓冲：每颗灯 3 字节 = G/R/B（按 WS2812B 的发送顺序存，发的时候直接拷） */
static uint8_t rgb_buf[RGB5X5_LED_COUNT * 3U];

static uint8_t  rgb_bright = RGB5X5_DEFAULT_BRIGHT;
static uint32_t rgb_last_us = 0U;

/* 编译期护栏：帧缓冲大小必须与 "灯数 x 3" 一致；
 * 同时校验 4 个延时宏都大于开销值，避免无符号下溢变成天文数字延时 */
typedef char rgb5x5_buf_check[(sizeof(rgb_buf) == (RGB5X5_LED_COUNT * 3U)) ? 1 : -1];
typedef char rgb5x5_t0h_check[((RGB5X5_CYC_T0H > RGB5X5_CYC_OVERHEAD)) ? 1 : -1];
typedef char rgb5x5_t1h_check[((RGB5X5_CYC_T1H > RGB5X5_CYC_OVERHEAD)) ? 1 : -1];


/* ================================================================
 *                      内部小工具
 * ================================================================ */

/* 坐标 → 串联序号
 * ⚠ 不同批次的 5x5 模块内部走线可能是"蛇形(来回绕)"，那时要把本函数改成：
 *      return ((y & 1U) ? (y * W + (W - 1U - x)) : (y * W + x));
 *   判断方法：只点 (0,0) 和 (0,1)（即第 1、2 颗），看亮的是不是左上和第二行 */
static uint8_t rgb_xy_to_index(uint8_t x, uint8_t y)
{
    return (uint8_t)(y * RGB5X5_WIDTH + x);
}

/* 亮度缩放：0~100 百分比 → 0~255 的乘数
 * 用 (v * pct) / 100 而不是位移，是因为要支持任意百分比（0~100 连续可调） */
static uint8_t rgb_scale(uint8_t v, uint8_t pct)
{
    if (pct == 0U)   return 0U;
    if (pct >= 100U) return v;

    return (uint8_t)(((uint16_t)v * (uint16_t)pct) / 100U);
}

/* 精确等待 n 个 CPU 周期（用内核 CYCCNT）
 * 说明 : DWT->CYCCNT 是自由运行计数器，读一次约 1 个周期；
 *        用无符号相减，32 位回绕也正确（约 25.6s @168MHz 绕一圈） */
static void rgb_dwt_delay(uint32_t cycles)
{
    uint32_t t0 = DWT->CYCCNT;

    while ((DWT->CYCCNT - t0) < cycles) { }
}

/* 发送一个字节（MSB 先出）—— 时序核心就这 12 行 */
static void rgb_send_byte(uint8_t byte)
{
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
        if ((byte & 0x80U) != 0U) {
            /* 发 1：高 T1H，低 T1L */
            GPIO_OutSet(RGB5X5_DATA_PORT, RGB5X5_DATA_PIN);
            rgb_dwt_delay(RGB5X5_CYC_T1H - RGB5X5_CYC_OVERHEAD);
            GPIO_OutReset(RGB5X5_DATA_PORT, RGB5X5_DATA_PIN);
            rgb_dwt_delay(RGB5X5_CYC_T1L);
        } else {
            /* 发 0：高 T0H，低 T0L */
            GPIO_OutSet(RGB5X5_DATA_PORT, RGB5X5_DATA_PIN);
            rgb_dwt_delay(RGB5X5_CYC_T0H - RGB5X5_CYC_OVERHEAD);
            GPIO_OutReset(RGB5X5_DATA_PORT, RGB5X5_DATA_PIN);
            rgb_dwt_delay(RGB5X5_CYC_T0L);
        }
        byte = (uint8_t)(byte << 1);
    }
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
void RGB5X5_Init(void)
{
#if (RGB5X5_ENABLE == 0)
    return;
#else
    uint16_t i;

    GPIO_ClockEnable(RGB5X5_DATA_PORT);
    GPIO_OutInit(RGB5X5_DATA_PORT, RGB5X5_DATA_PIN);
    GPIO_OutReset(RGB5X5_DATA_PORT, RGB5X5_DATA_PIN);   /* 先拉低，避免上电毛刺 */

    /* 确保 DWT 计数器已启动（gpio_core 的延时函数会顺手打开它） */
    (void)DWT_GetUs();

    for (i = 0U; i < (uint16_t)(RGB5X5_LED_COUNT * 3U); i++) {
        rgb_buf[i] = 0U;
    }
    rgb_bright = RGB5X5_DEFAULT_BRIGHT;

    RGB5X5_Show();      /* 上电先灭一次，清掉灯板里的残留数据 */
#endif
}

void RGB5X5_SetPixel(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t base;

    if (idx >= RGB5X5_LED_COUNT) return;

    base = (uint16_t)idx * 3U;

    /* 注意顺序：WS2812B 先收的是 G，再 R，最后 B */
#if (RGB5X5_COLOR_ORDER_GRB)
    rgb_buf[base + 0U] = rgb_scale(g, rgb_bright);
    rgb_buf[base + 1U] = rgb_scale(r, rgb_bright);
    rgb_buf[base + 2U] = rgb_scale(b, rgb_bright);
#else
    rgb_buf[base + 0U] = rgb_scale(r, rgb_bright);
    rgb_buf[base + 1U] = rgb_scale(g, rgb_bright);
    rgb_buf[base + 2U] = rgb_scale(b, rgb_bright);
#endif
}

void RGB5X5_SetPixelXY(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b)
{
    if ((x >= RGB5X5_WIDTH) || (y >= RGB5X5_HEIGHT)) return;

    RGB5X5_SetPixel(rgb_xy_to_index(x, y), r, g, b);
}

void RGB5X5_Fill(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t i;

    for (i = 0U; i < RGB5X5_LED_COUNT; i++) {
        RGB5X5_SetPixel(i, r, g, b);
    }
}

void RGB5X5_Clear(void)
{
    uint16_t i;

    for (i = 0U; i < (uint16_t)(RGB5X5_LED_COUNT * 3U); i++) {
        rgb_buf[i] = 0U;
    }
}

void RGB5X5_SetBrightness(uint8_t percent)
{
    if (percent > 100U) percent = 100U;

    rgb_bright = percent;
    /* 说明：亮度只影响"之后写入"的像素。想让已经在缓冲里的像素也变暗，
     *      最简单的做法是改完亮度后重新 Fill/Set 一遍。 */
}

uint8_t RGB5X5_GetBrightness(void)
{
    return rgb_bright;
}

void RGB5X5_Show(void)
{
#if (RGB5X5_ENABLE == 0)
    return;
#else
    uint16_t i;
    uint32_t t0;
    uint32_t primask;

    t0 = DWT_GetUs();

    /* ★ 关键：整帧发送期间必须不被打断（见文件头说明）。
     * ⚠ 不要直接 __enable_irq() 收尾：万一调用者本来就把中断关着，
     *   那样会"帮你"把它打开 —— 所以先存 PRIMASK，再原样恢复。 */
    primask = __get_PRIMASK();
    __disable_irq();

    for (i = 0U; i < (uint16_t)(RGB5X5_LED_COUNT * 3U); i++) {
        rgb_send_byte(rgb_buf[i]);
    }

    if (primask == 0U) __enable_irq();

    /* 复位：拉低保持 RGB5X5_RESET_US 微秒，灯板才认为一帧结束 */
    GPIO_OutReset(RGB5X5_DATA_PORT, RGB5X5_DATA_PIN);
    Delay_us(RGB5X5_RESET_US);

    rgb_last_us = DWT_ElapsedUs(t0);
#endif
}

uint8_t RGB5X5_GetPixelR(uint8_t idx)
{
    if (idx >= RGB5X5_LED_COUNT) return 0U;

#if (RGB5X5_COLOR_ORDER_GRB)
    /* 缓冲里是 G/R/B，所以 R 在 +1 —— 但那是"缩放后"的值，
     * 想拿原始值请自己除亮度，这里返回缓冲真值（更诚实） */
    return rgb_buf[(uint16_t)idx * 3U + 1U];
#else
    return rgb_buf[(uint16_t)idx * 3U + 0U];
#endif
}

uint8_t RGB5X5_GetPixelG(uint8_t idx)
{
    if (idx >= RGB5X5_LED_COUNT) return 0U;

#if (RGB5X5_COLOR_ORDER_GRB)
    return rgb_buf[(uint16_t)idx * 3U + 0U];
#else
    return rgb_buf[(uint16_t)idx * 3U + 1U];
#endif
}

uint8_t RGB5X5_GetPixelB(uint8_t idx)
{
    if (idx >= RGB5X5_LED_COUNT) return 0U;

    return rgb_buf[(uint16_t)idx * 3U + 2U];
}


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
void RGB5X5_HsvToRgb(uint8_t h, uint8_t s, uint8_t v,
                     uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region;
    uint8_t remainder;
    uint8_t p, q, t;

    if ((r == 0) || (g == 0) || (b == 0)) return;

    if (s == 0U) {                  /* 灰阶：直接返回明度 */
        *r = v; *g = v; *b = v;
        return;
    }

    /* 把 0~255 的色相切成 6 段（红→黄→绿→青→蓝→品红） */
    region    = (uint8_t)(h / 43U);             /* 256/6 ≈ 43 */
    remainder = (uint8_t)((h - (uint8_t)(region * 43U)) * 6U);

    p = (uint8_t)(((uint16_t)v * (255U - s)) / 255U);
    q = (uint8_t)(((uint16_t)v * (255U - ((uint16_t)s * remainder) / 255U)) / 255U);
    t = (uint8_t)(((uint16_t)v * (255U - ((uint16_t)s * (255U - remainder)) / 255U)) / 255U);

    switch (region) {
        case 0U:  *r = v; *g = t; *b = p; break;
        case 1U:  *r = q; *g = v; *b = p; break;
        case 2U:  *r = p; *g = v; *b = t; break;
        case 3U:  *r = p; *g = q; *b = v; break;
        case 4U:  *r = t; *g = p; *b = v; break;
        default:  *r = v; *g = p; *b = q; break;
    }
}

void RGB5X5_SetPixelHSV(uint8_t x, uint8_t y, uint8_t h, uint8_t s, uint8_t v)
{
    uint8_t r, g, b;

    RGB5X5_HsvToRgb(h, s, v, &r, &g, &b);
    RGB5X5_SetPixelXY(x, y, r, g, b);
}

void RGB5X5_Rainbow(uint8_t phase)
{
    uint8_t x, y;
    uint8_t r, g, b;

    for (y = 0U; y < RGB5X5_HEIGHT; y++) {
        for (x = 0U; x < RGB5X5_WIDTH; x++) {
            /* 色相按"位置 + phase"铺开：phase 一变整条彩虹就流动起来 */
            uint8_t h = (uint8_t)(phase + (uint8_t)((uint16_t)(y * RGB5X5_WIDTH + x) * 10U));

            RGB5X5_HsvToRgb(h, 255U, 255U, &r, &g, &b);
            RGB5X5_SetPixelXY(x, y, r, g, b);
        }
    }
}

void RGB5X5_Dot(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b)
{
    RGB5X5_Clear();
    RGB5X5_SetPixelXY(x, y, r, g, b);
}

void RGB5X5_Off(void)
{
    RGB5X5_Clear();
    RGB5X5_Show();
}

uint32_t RGB5X5_LastShowUs(void)
{
    return rgb_last_us;
}


/* ================================================================
 *  扩展提示：什么时候该换成"PWM + DMA"方案
 * ================================================================
 *  本模块用的是"GPIO 位翻转 + 关中断"，优点是**零外设占用、易懂**；
 *  代价是 Show() 期间约 1ms 不能响应任何中断。以下情况建议改方案：
 *
 *    · 在跑 FreeRTOS，且串口/定时中断丢 1ms 会造成丢数据；
 *    · 要驱动几十上百颗灯（帧时间线性增长，几十颗就 10ms 级了）；
 *    · 需要极高刷新率。
 *
 *  换法（标准库也能做，思路记下来就行）：
 *    ① 取一个定时器（如 TIM3），PSC 使计数频率 = 800kHz/每bit的**子周期**，
 *       例如把 1 bit 拆成 10 个子周期，ARR = 10-1；
 *    ② 用 PWM 通道输出：占空比 3/10 表示 0，7/10 表示 1；
 *    ③ 开该通道的 DMA 请求，PSC/ARR 固定，**只改 CCR** —— 把整帧
 *       （600 个 CCR 值 + 末尾一串 0 表示复位）放进数组，DMA 循环发完；
 *    ④ DMA 传输完成中断里再拉低，或直接把复位段也放进数组。
 *  这样 CPU 完全不参与，也不怕被打断。
 * ================================================================ */

/* 文件结束 */
