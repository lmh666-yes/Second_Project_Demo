#include "oled.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "lcd_font.h"     /* 复用 LCD 的 8x16 ASCII 点阵字库 */

/* ================================================================
 *  oled.c —— SSD1306 OLED 显示屏（I2C）  实现文件
 * ================================================================
 *  显存布局（与 SSD1306 硬件一致，所以推屏时能整块搬运）:
 *      buf[page][col]，page = 0~7（每页 8 行），col = 0~127
 *      buf[p][c] 的 bit0 = 该页第 0 行像素，bit7 = 第 7 行像素
 *      即：像素(x, y) → buf[y/8][x] 的 bit (y%8)
 *
 *  三个实现要点 :
 *    ① 所有绘图只改显存，只有 OLED_Refresh 才碰 I2C —— 不闪、还快；
 *    ② 推屏时"逐页写"：每页先发页地址+列地址，再连续写 128 字节显存；
 *    ③ 字库复用 lcd_font.h（8×16），因此 OLED 与 TFT 的字符代码完全一致。
 * ================================================================ */


/* ================================================================
 *                     显存（128 x 64 / 8 = 1024 字节）
 * ================================================================ */
static uint8_t oled_buf[OLED_PAGE_CNT][OLED_WIDTH];


/* ================================================================
 *                      低层：命令 / 数据
 * ================================================================
 * SSD1306 的 I2C 帧格式：
 *     [从机地址+W] [0x00] [命令字节...]      ← 0x00 = 后面是命令
 *     [从机地址+W] [0x40] [数据字节...]      ← 0x40 = 后面是显存数据
 * 本文件用 sys_i2c 的"寄存器写"接口把 0x00/0x40 当成"寄存器号"用，
 * 刚好等价——这也是大多数 OLED 驱动的通用写法。
 * ================================================================ */
static uint8_t oled_wr(uint8_t ctrl, const uint8_t *buf, uint16_t len)
{
    uint8_t k;

    for (k = 0; k < OLED_RETRY; k++) {
        if (SYS_I2C_WriteBytes(OLED_I2C_ID, OLED_ADDR, ctrl, buf, len) == SYS_I2C_OK) {
            return 0U;
        }
    }
    return 1U;
}

static void oled_cmd(uint8_t cmd)
{
    (void)oled_wr(0x00U, &cmd, 1U);
}

static void oled_cmd2(uint8_t cmd, uint8_t arg)
{
    uint8_t b[2];
    b[0] = cmd;
    b[1] = arg;
    (void)oled_wr(0x00U, b, 2U);
}

static void oled_data(const uint8_t *buf, uint16_t len)
{
    (void)oled_wr(0x40U, buf, len);
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化序列（SSD1306 标准上电流程，逐条注释方便对照手册） */
void OLED_Init(void)
{
    oled_cmd(0xAEU);              /* 关显示（配置期间先关，避免花屏） */

    oled_cmd2(0xD5U, 0x80U);      /* 显示时钟分频/振荡频率 */
    oled_cmd2(0xA8U, 0x3FU);      /* 复用率 = 64 行（0x3F = 63） */
    oled_cmd2(0xD3U, 0x00U);      /* 显示偏移 = 0 */
    oled_cmd(0x40U);              /* 显示起始行 = 0 */

    oled_cmd2(0x8DU, 0x14U);      /* 电荷泵：0x14 = 内部升压使能（必须！） */
    oled_cmd2(0x20U, 0x00U);      /* 寻址模式 = 页模式（配合本驱动的逐页写） */

    oled_cmd(0xA1U);              /* 段重映射：列 127 → SEG0（左右方向） */
    oled_cmd(0xC8U);              /* 行扫描方向：从下往上（上下方向） */

    oled_cmd2(0xDAU, 0x12U);      /* COM 引脚配置：交替 + 不左右反转（64 行屏） */
    oled_cmd2(0x81U, 0xCFU);      /* 亮度（0x00~0xFF） */
    oled_cmd2(0xD9U, 0xF1U);      /* 预充电周期 */
    oled_cmd2(0xDBU, 0x40U);      /* VCOMH 电压 */

    oled_cmd(0xA4U);              /* 显示跟随显存内容（不是全亮） */
    oled_cmd(0xA6U);              /* 正常显示（非反色） */
    oled_cmd(0x2EU);              /* 关闭滚动 */

    OLED_ClearBuffer();
    OLED_Refresh();

    oled_cmd(0xAFU);              /* 开显示 */
}

uint8_t OLED_IsOnline(void)
{
    return (SYS_I2C_IsDeviceReady(OLED_I2C_ID, OLED_ADDR) == SYS_I2C_OK) ? 1U : 0U;
}

void OLED_ClearBuffer(void)
{
    uint16_t p;
    uint16_t c;

    for (p = 0; p < OLED_PAGE_CNT; p++) {
        for (c = 0; c < OLED_WIDTH; c++) {
            oled_buf[p][c] = 0x00U;
        }
    }
}

void OLED_Refresh(void)
{
    uint8_t  p;
    uint16_t off;

    for (p = 0; p < OLED_PAGE_CNT; p++) {
        oled_cmd((uint8_t)(0xB0U | p));    /* 设置页地址（0xB0 ~ 0xB7） */
        oled_cmd(0x00U);                   /* 列地址低 4 位 = 0 */
        oled_cmd(0x10U);                   /* 列地址高 4 位 = 0 */

        /* 一页 128 字节，分块写以适配 I2C 缓冲 */
        for (off = 0; off < OLED_WIDTH; off += OLED_CHUNK) {
            uint16_t n = OLED_CHUNK;
            if ((uint16_t)(off + n) > OLED_WIDTH) n = (uint16_t)(OLED_WIDTH - off);
            oled_data(&oled_buf[p][off], n);
        }
    }
}

void OLED_Clear(void)
{
    OLED_ClearBuffer();
    OLED_Refresh();
}

void OLED_DisplayOn(void)  { oled_cmd(0xAFU); }
void OLED_DisplayOff(void) { oled_cmd(0xAEU); }

void OLED_SetContrast(uint8_t contrast)
{
    oled_cmd2(0x81U, contrast);
}


/* ================================================================
 *                    区块 3：像素 / 图元
 * ================================================================ */
void OLED_DrawPoint(uint16_t x, uint16_t y, uint8_t on)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;

    if (on) oled_buf[y >> 3][x] |= (uint8_t)(1U << (y & 0x07U));
    else    oled_buf[y >> 3][x] &= (uint8_t)~(1U << (y & 0x07U));
}

uint8_t OLED_GetPoint(uint16_t x, uint16_t y)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return 0;

    return (oled_buf[y >> 3][x] & (uint8_t)(1U << (y & 0x07U))) ? 1U : 0U;
}

void OLED_InvertScreen(void)
{
    uint8_t  p;
    uint16_t c;

    for (p = 0; p < OLED_PAGE_CNT; p++) {
        for (c = 0; c < OLED_WIDTH; c++) {
            oled_buf[p][c] = (uint8_t)~oled_buf[p][c];
        }
    }
}

void OLED_DrawHLine(uint16_t x0, uint16_t x1, uint16_t y, uint8_t on)
{
    uint16_t t;
    uint16_t x;

    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    for (x = x0; x <= x1; x++) OLED_DrawPoint(x, y, on);
}

void OLED_DrawVLine(uint16_t x, uint16_t y0, uint16_t y1, uint8_t on)
{
    uint16_t t;
    uint16_t y;

    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    for (y = y0; y <= y1; y++) OLED_DrawPoint(x, y, on);
}

/* 直线：Bresenham（与 lcd.c 同一套算法，这里改成"画到显存"） */
void OLED_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on)
{
    int16_t x = (int16_t)x0, y = (int16_t)y0;
    int16_t ex = (int16_t)x1, ey = (int16_t)y1;
    int16_t dx = (ex > x) ? (int16_t)(ex - x) : (int16_t)(x - ex);
    int16_t dy = (ey > y) ? (int16_t)(y - ey) : (int16_t)(ey - y);
    int16_t sx = (x < ex) ? 1 : -1;
    int16_t sy = (y < ey) ? 1 : -1;
    int16_t err = (int16_t)(dx + dy);

    for (;;) {
        OLED_DrawPoint((uint16_t)x, (uint16_t)y, on);
        if (x == ex && y == ey) break;
        {
            int16_t e2 = (int16_t)(2 * err);
            if (e2 >= dy) { err = (int16_t)(err + dy); x = (int16_t)(x + sx); }
            if (e2 <= dx) { err = (int16_t)(err + dx); y = (int16_t)(y + sy); }
        }
    }
}

void OLED_DrawRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on)
{
    OLED_DrawHLine(x0, x1, y0, on);
    OLED_DrawHLine(x0, x1, y1, on);
    OLED_DrawVLine(x0, y0, y1, on);
    OLED_DrawVLine(x1, y0, y1, on);
}

void OLED_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on)
{
    uint16_t y;

    for (y = y0; y <= y1; y++) OLED_DrawHLine(x0, x1, y, on);
}

/* 空心圆：Bresenham 八分对称（越界由 DrawPoint 忽略） */
static void oled_plot8(int16_t xc, int16_t yc, int16_t x, int16_t y, uint8_t on)
{
    OLED_DrawPoint((uint16_t)(xc + x), (uint16_t)(yc + y), on);
    OLED_DrawPoint((uint16_t)(xc - x), (uint16_t)(yc + y), on);
    OLED_DrawPoint((uint16_t)(xc + x), (uint16_t)(yc - y), on);
    OLED_DrawPoint((uint16_t)(xc - x), (uint16_t)(yc - y), on);
    OLED_DrawPoint((uint16_t)(xc + y), (uint16_t)(yc + x), on);
    OLED_DrawPoint((uint16_t)(xc - y), (uint16_t)(yc + x), on);
    OLED_DrawPoint((uint16_t)(xc + y), (uint16_t)(yc - x), on);
    OLED_DrawPoint((uint16_t)(xc - y), (uint16_t)(yc - x), on);
}

void OLED_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint8_t on)
{
    int16_t x = 0;
    int16_t y = r;
    int16_t d = (int16_t)(1 - r);

    if (r < 0) return;

    while (x <= y) {
        oled_plot8(x0, y0, x, y, on);
        if (d < 0) d = (int16_t)(d + 2 * x + 3);
        else { d = (int16_t)(d + 2 * (x - y) + 5); y--; }
        x++;
    }
}

void OLED_ShowProgress(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t permille)
{
    uint16_t fill;
    uint16_t i;

    if (w < 2U || h < 2U) return;
    if (permille > 1000U) permille = 1000U;

    OLED_DrawRect(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U), 1);

    /* 内部可用宽度（去掉 1 像素边框）按比例填充 */
    fill = (uint16_t)(((uint32_t)(w - 2U) * permille) / 1000U);

    for (i = 0; i < fill; i++) {
        OLED_DrawVLine((uint16_t)(x + 1U + i),
                       (uint16_t)(y + 1U), (uint16_t)(y + h - 2U), 1);
    }
}


/* ================================================================
 *                    区块 3：文字（8×16 字库）
 * ================================================================ */
void OLED_ShowChar(uint16_t x, uint16_t y, char ch, uint8_t inv)
{
    uint8_t idx;
    uint8_t i;
    uint8_t j;
    uint8_t row;
    uint8_t bit;

    idx = (uint8_t)ch;
    if (idx < 0x20U || idx > 0x7EU) idx = 0x20U;   /* 范围外按空格 */
    idx = (uint8_t)(idx - 0x20U);

    if (x + LCD_FONT8X16_W > OLED_WIDTH) return;
    if (y + LCD_FONT8X16_H > OLED_HEIGHT) return;

    for (i = 0; i < LCD_FONT8X16_H; i++) {
        row = lcd_font8x16[idx][i];
        for (j = 0; j < LCD_FONT8X16_W; j++) {
            bit = (row & (uint8_t)(0x80U >> j)) ? 1U : 0U;
            if (inv) bit = (uint8_t)(bit ^ 1U);      /* 反显：黑白互换 */
            OLED_DrawPoint((uint16_t)(x + j), (uint16_t)(y + i), bit);
        }
    }
}

/* 内部：把字符串按字符推进（含自动换行），供 ShowString / Center 复用 */
static void oled_puts(uint16_t x, uint16_t y, const char *str, uint8_t inv)
{
    uint16_t xs = x;

    if (str == 0) return;

    while (*str != '\0') {
        if (*str == '\n') {
            x  = xs;
            y  = (uint16_t)(y + LCD_FONT8X16_H);
            str++;
            continue;
        }
        if (x + LCD_FONT8X16_W > OLED_WIDTH) {      /* 到右边界：换行 */
            x  = xs;
            y  = (uint16_t)(y + LCD_FONT8X16_H);
        }
        if (y + LCD_FONT8X16_H > OLED_HEIGHT) break; /* 到底：停 */

        OLED_ShowChar(x, y, *str, inv);
        x = (uint16_t)(x + LCD_FONT8X16_W);
        str++;
    }
}

void OLED_ShowString(uint16_t x, uint16_t y, const char *str, uint8_t inv)
{
    oled_puts(x, y, str, inv);
}

void OLED_ShowStringCenter(uint16_t y, const char *str, uint8_t inv)
{
    uint16_t n = 0;
    const char *p = str;
    uint16_t x;

    if (str == 0) return;

    while (*p != '\0') { n++; p++; }
    if ((uint32_t)n * LCD_FONT8X16_W > OLED_WIDTH) {   /* 太长就顶格显示 */
        oled_puts(0, y, str, inv);
        return;
    }
    x = (uint16_t)((OLED_WIDTH - (uint16_t)n * LCD_FONT8X16_W) / 2U);
    oled_puts(x, y, str, inv);
}

void OLED_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t inv)
{
    uint8_t  i;
    uint8_t  n;
    uint32_t div = 1U;

    if (len == 0U || len > 10U) len = 10U;

    for (n = 1U; n < len; n++) div *= 10U;

    for (i = 0U; i < len; i++) {
        uint8_t d = (uint8_t)((num / div) % 10U);
        char c = (div > num && i != (uint8_t)(len - 1U))
                 ? ' ' : (char)('0' + d);
        OLED_ShowChar((uint16_t)(x + (uint16_t)i * LCD_FONT8X16_W), y, c, inv);
        div /= 10U;
    }
}

void OLED_ShowFixed(uint16_t x, uint16_t y, int32_t val, uint8_t frac,
                    uint8_t int_len, uint8_t inv)
{
    uint32_t mag;
    uint32_t div;
    uint32_t fp;
    uint16_t px = x;
    uint8_t  i;

    if (frac > 6U) frac = 6U;
    if (int_len == 0U) int_len = 1U;

    if (val < 0) {
        OLED_ShowChar(px, y, '-', inv);
        px = (uint16_t)(px + LCD_FONT8X16_W);
        mag = (uint32_t)(-val);
    } else {
        mag = (uint32_t)val;
    }

    div = 1U;
    for (i = 0U; i < frac; i++) div *= 10U;

    OLED_ShowNum(px, y, mag / div, int_len, inv);
    px = (uint16_t)(px + (uint16_t)int_len * LCD_FONT8X16_W);

    if (frac != 0U) {
        uint32_t p = div / 10U;

        OLED_ShowChar(px, y, '.', inv);
        px = (uint16_t)(px + LCD_FONT8X16_W);

        fp = mag % div;
        for (i = 0U; i < frac; i++) {
            OLED_ShowChar((uint16_t)(px + (uint16_t)i * LCD_FONT8X16_W), y,
                          (char)('0' + (uint8_t)((fp / p) % 10U)), inv);
            p /= 10U;
        }
    }
}
