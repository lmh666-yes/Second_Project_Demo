#include "rgb5x5.h"
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_us 等）独立文件 */

/* rgb5x5.c:板载 5x5 全彩 LED 阵列（WS2812B x25）实现
 *
 * WS2812B 无时钟线，靠高电平持续时间判定 bit：高 0.35us 为 0，高 0.70us 为 1。
 * 发送期间被中断打断 5us 会使该 bit 判错、后续数据错位，因此 Show() 内关中断，
 * 整帧发完再开（25 x 24 bit x 1.25us，加复位约 1ms）。
 *
 * 计时用内核 32 位自由运行周期计数器 DWT->CYCCNT（gpio_core 的延时函数已启用），
 * 读一次约 1 个指令周期，精度为 CPU 主频，不占用定时器与 DMA，代价是发送期间关中断。
 *
 * 每 bit 总长 1.25us（800kHz）。下面 4 个 CYC_xxx 由 .h 传入，理论值 @168MHz：
 *     T0H = 0.35us = 59 周期    T0L = 0.90us = 151 周期
 *     T1H = 0.70us = 118 周期   T1L = 0.55us = 92 周期
 * 把引脚拉高的语句本身占若干周期，实际等待要减去 RGB5X5_CYC_OVERHEAD。
 * 全屏乱色或只亮第一颗：overhead 估小，加大到 16~24；
 * 亮度整体偏暗或偏亮：四个值按比例缩放；
 * 完全无反应：先查 CN5 的 2 与 3 是否短接（灯板未供电）。
 * 接线：数据脚由 RGB5X5_DATA_PORT/PIN 指定，来自 P9 扩展排针的飞线
 */


/* 帧缓冲：每颗灯 3 字节，按 WS2812B 发送顺序存 G/R/B */
static uint8_t rgb_buf[RGB5X5_LED_COUNT * 3U];

static uint8_t  rgb_bright = RGB5X5_DEFAULT_BRIGHT;
static uint32_t rgb_last_us = 0U;

/* 编译期校验：帧缓冲大小须等于灯数 x 3；延时宏须大于开销值，避免无符号下溢 */
typedef char rgb5x5_buf_check[(sizeof(rgb_buf) == (RGB5X5_LED_COUNT * 3U)) ? 1 : -1];
typedef char rgb5x5_t0h_check[((RGB5X5_CYC_T0H > RGB5X5_CYC_OVERHEAD)) ? 1 : -1];
typedef char rgb5x5_t1h_check[((RGB5X5_CYC_T1H > RGB5X5_CYC_OVERHEAD)) ? 1 : -1];


/* 坐标转串联序号（逐行左到右、从上到下）
 * 部分批次 5x5 模块内部为蛇形走线，奇偶行 x 方向相反，此时需改本函数 */
static uint8_t rgb_xy_to_index(uint8_t x, uint8_t y)
{
    return (uint8_t)(y * RGB5X5_WIDTH + x);
}

/* 亮度缩放：pct 为 0~100 百分比，返回缩放后的值
 * 用 (v * pct) / 100 而不移位，是为了支持 0~100 任意百分比 */
static uint8_t rgb_scale(uint8_t v, uint8_t pct)
{
    if (pct == 0U)   return 0U;
    if (pct >= 100U) return v;

    return (uint8_t)(((uint16_t)v * (uint16_t)pct) / 100U);
}

/* 忙等 n 个 CPU 周期（用内核 CYCCNT）
 * 说明 : DWT->CYCCNT 自由运行，读一次约 1 个周期；用无符号相减，
 *        32 位回绕也正确（约 25.6s @168MHz 绕一圈） */
static void rgb_dwt_delay(uint32_t cycles)
{
    uint32_t t0 = DWT->CYCCNT;

    while ((DWT->CYCCNT - t0) < cycles) { }
}

/* 发送一个字节，MSB 先出 */
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


void RGB5X5_Init(void)
{
#if (RGB5X5_ENABLE == 0)
    return;
#else
    uint16_t i;

    GPIO_ClockEnable(RGB5X5_DATA_PORT);
    GPIO_OutInit(RGB5X5_DATA_PORT, RGB5X5_DATA_PIN);
    GPIO_OutReset(RGB5X5_DATA_PORT, RGB5X5_DATA_PIN);   /* 先拉低，避免上电毛刺 */

    /* 确保 DWT 计数器已启动，gpio_core 的延时函数会启用它 */
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
    /* 亮度只影响之后写入的像素；已写入缓冲的像素需重新 Fill / SetPixel 才生效 */
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

    /* 整帧发送期间不能被打断；保存并恢复 PRIMASK，
     * 避免调用者原本关中断时被 __enable_irq 打开 */
    primask = __get_PRIMASK();
    __disable_irq();

    for (i = 0U; i < (uint16_t)(RGB5X5_LED_COUNT * 3U); i++) {
        rgb_send_byte(rgb_buf[i]);
    }

    if (primask == 0U) __enable_irq();

    /* 复位：拉低保持 RGB5X5_RESET_US 微秒，灯板才认为一帧结束 */
    GPIO_OutReset(RGB5X5_DATA_PORT, RGB5X5_DATA_PIN);
    delay_us(RGB5X5_RESET_US);

    rgb_last_us = DWT_ElapsedUs(t0);
#endif
}

uint8_t RGB5X5_GetPixelR(uint8_t idx)
{
    if (idx >= RGB5X5_LED_COUNT) return 0U;

#if (RGB5X5_COLOR_ORDER_GRB)
    /* 缓冲为 GRB 顺序，R 在 +1；返回的是乘过亮度的缓冲值 */
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
            /* 色相 = phase + 位置 x 10，phase 递增产生流动 */
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
