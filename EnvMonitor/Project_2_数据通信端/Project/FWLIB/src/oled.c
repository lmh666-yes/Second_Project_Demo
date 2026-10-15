#include "oled.h"
#include "gpio_core.h"
#include "lcd_font.h"     /* 复用 LCD 的 8x16 ASCII 点阵字库 */

/* SSD1306 OLED（I2C）显存布局，与硬件页结构一致，可整块搬运:
 *   buf[page][col]，page = 0~7（每页 8 行），col = 0~127
 *   像素(x, y) 对应 buf[y/8][x] 的 bit (y%8)，bit0 = 页内第 0 行
 *   绘图只改显存，只有 OLED_Refresh / OLED_RefreshDirty 才发 I2C */


/* 显存 128 x 64 / 8 = 1024 字节 */
static uint8_t oled_buf[OLED_PAGE_CNT][OLED_WIDTH];

/* 脏页位图:bit0 = 第 0 页 … bit7 = 第 7 页，1 = 该页显存已改动、尚未上屏
 * 上电初值 0xFF = 全脏（屏上内容未知，首次局部刷新应整屏写一遍）；某页写进屏后才清位 */
static uint8_t s_oled_dirty = 0xFFU;

/* 上屏错误统计：一次 OLED_Refresh 发 8 页 × (1 条定位命令 + 4 块数据)
 * = 40 次 I2C 事务，任何一次失败即该页未写上 */
static volatile uint16_t s_oled_err_cnt   = 0U;   /* 累计失败次数 */
static volatile uint8_t  s_oled_last_err  = 0U;   /* 最近一次的 SYS_I2C 错误码 */

/* 临界区（帧锁）:oled_buf 与 I2C1 总线是共享资源，所有改 oled_buf 的公共函数与
 * OLED_Refresh 共用同一把总线锁 SYS_I2C_Lock(OLED_I2C_ID)（递归锁，同任务可重入）。
 * 依据:Refresh 逐页写共 40 次独立 I2C 事务，24C02 / MPU6050 的事务插入其间会写坏整帧。
 * 刷新一屏 20ms 以上，锁只让出 CPU 忙等，不关中断，也不在中断中调用本模块。
 * 上电初始化阶段调度器未运行，SYS_I2C_Lock 为空操作。 */
#define OLED_FRAME_LOCK()    SYS_I2C_Lock(OLED_I2C_ID)
#define OLED_FRAME_UNLOCK()  SYS_I2C_Unlock(OLED_I2C_ID)


/* SSD1306 I2C 帧格式（0x00/0x40 作为 sys_i2c 寄存器写接口的寄存器号）:
 *   [从机地址+W] [0x00] [命令字节...]   0x00 = 后续为命令
 *   [从机地址+W] [0x40] [数据字节...]   0x40 = 后续为显存数据 */
static uint8_t oled_wr(uint8_t ctrl, const uint8_t *buf, uint16_t len)
{
    uint8_t k;
    uint8_t err = 1U;

    for (k = 0; k < OLED_RETRY; k++) {
        err = SYS_I2C_WriteBytes(OLED_I2C_ID, OLED_ADDR, ctrl, buf, len);
        if (err == SYS_I2C_OK) {
            return 0U;
        }
    }
    /* 重试 OLED_RETRY 次仍失败:累计错误计数并记录最近错误码
     * 依据:屏未接、地址错、总线被拉死时上屏动作全部失败，仅靠 OLED_IsOnline
     * 在"可寻址但写不进"的场合仍返回 1，需要可观测的计数与错误码 */
    s_oled_err_cnt++;
    s_oled_last_err = err;
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

/* 把全部页标脏（清缓冲 / 反色 / 上电等整屏变化的场合）；调用方须已持帧锁 */
static void oled_dirty_all(void)
{
    s_oled_dirty = (uint8_t)((1U << OLED_PAGE_CNT) - 1U);   /* 8 页全置位 = 0xFF */
}


/* SSD1306 标准上电初始化序列 */
void OLED_Init(void)
{
    OLED_FRAME_LOCK();            /* 配置期间独占总线（上电时调度器未运行，加锁为空操作） */
    oled_cmd(0xAEU);              /* 关显示（配置期间先关，避免花屏） */

    oled_cmd2(0xD5U, 0x80U);      /* 显示时钟分频/振荡频率 */
    oled_cmd2(0xA8U, 0x3FU);      /* 复用率 = 64 行（0x3F = 63） */
    oled_cmd2(0xD3U, 0x00U);      /* 显示偏移 = 0 */
    oled_cmd(0x40U);              /* 显示起始行 = 0 */

    oled_cmd2(0x8DU, 0x14U);      /* 电荷泵：0x14 = 内部升压使能 */
    oled_cmd2(0x20U, 0x00U);      /* 寻址模式 = 水平寻址（0x20 参数 00b） */

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
    (void)OLED_Refresh();             /* 首次清屏失败不作为 Init 失败，屏可能后插上 */

    oled_cmd(0xAFU);              /* 开显示 */
    OLED_FRAME_UNLOCK();
}

uint8_t OLED_IsOnline(void)
{
    return (SYS_I2C_IsDeviceReady(OLED_I2C_ID, OLED_ADDR) == SYS_I2C_OK) ? 1U : 0U;
}

/* 上屏失败次数（累计），0 = 自上次清零以来每次写都成功
 * 计数在涨说明写不进去（接线、上拉、地址、总线被占）；计数为 0 仍不亮则问题在屏或供电 */
uint16_t OLED_ErrCount(void)
{
    return s_oled_err_cnt;
}

/* 最近一次失败的 SYS_I2C 错误码（0 = 没失败过，见 sys_i2c.h 的 SYS_I2C_ERR_*） */
uint8_t OLED_LastErr(void)
{
    return s_oled_last_err;
}

/* 清零错误计数与最近错误码 */
void OLED_ErrClear(void)
{
    s_oled_err_cnt  = 0U;
    s_oled_last_err = 0U;
}

void OLED_ClearBuffer(void)
{
    uint16_t p;
    uint16_t c;

    /* 只动 RAM，但需与 OLED_Refresh 互斥，否则清到一半的显存会被推上屏（递归锁，同任务可重入） */
    OLED_FRAME_LOCK();
    for (p = 0; p < OLED_PAGE_CNT; p++) {
        for (c = 0; c < OLED_WIDTH; c++) {
            oled_buf[p][c] = 0x00U;
        }
    }
    oled_dirty_all();          /* 8 页全变，整屏标脏 */
    OLED_FRAME_UNLOCK();
}

uint8_t OLED_Refresh(void)
{
    uint8_t  p;
    uint16_t off;
    uint8_t  fail = 0U;

    /* 阻塞时长:8 页 × (1 条定位命令 + 4 块数据) = 40 次 I2C 事务，
     * 整屏 1024 字节 @400kHz 约 23~25ms（不含重试）；失败时每次 oled_wr 重试
     * OLED_RETRY 次并等 I2C 超时，最坏可放大到数百毫秒，不可在 1ms 定时器中断中调用。
     * 返回:0 = 全部页写成功；1~8 = 写失败的页数 */
    OLED_FRAME_LOCK();      /* 40 次事务在同一临界区内，避免其他器件插入本帧 */
    for (p = 0; p < OLED_PAGE_CNT; p++) {
        uint8_t page_ok = 1U;
        uint8_t pos[3];

        pos[0] = (uint8_t)(0xB0U | p);      /* 页地址（0xB0 ~ 0xB7） */
        pos[1] = 0x00U;                     /* 列地址低 4 位 = 0 */
        pos[2] = 0x10U;                     /* 列地址高 4 位 = 0 */
        if (oled_wr(0x00U, pos, 3U) != 0U) {
            page_ok = 0U;                   /* 3 条定位命令合并为一次事务 */
        }

        /* 一页 128 字节，分块写以适配 I2C 缓冲 */
        for (off = 0; off < OLED_WIDTH; off += OLED_CHUNK) {
            uint16_t n = OLED_CHUNK;
            if ((uint16_t)(off + n) > OLED_WIDTH) n = (uint16_t)(OLED_WIDTH - off);
            if (oled_wr(0x40U, &oled_buf[p][off], n) != 0U) {
                page_ok = 0U;
            }
        }

        if (page_ok == 0U) {
            fail++;
        } else {
            s_oled_dirty &= (uint8_t)~(uint8_t)(1U << p);   /* 整页写成功，清除该页脏位 */
        }
    }

    OLED_FRAME_UNLOCK();
    return fail;
}

/* 局部刷新:只把脏页推上屏，无脏页时不碰总线。
 * 需显式定位的原因:整屏刷新依赖上电时设的水平寻址模式按页序铺满，单写一页
 * 需先用 0x21/0x22 圈出窗口（页地址 0xB0 只在页寻址模式下有效）；写完把窗口
 * 还原为整屏、页 0 起，OLED_Refresh 自身不定位，依赖该状态。
 * 返回:实际写成功的页数 0~8；未写成功的页保留脏位，下次再试 */
uint8_t OLED_RefreshDirty(void)
{
    uint8_t  p;
    uint8_t  cnt = 0U;
    uint8_t  cmd[6];

    OLED_FRAME_LOCK();

    if (s_oled_dirty == 0x00U) {            /* 全干净:不碰总线，返回 0 */
        OLED_FRAME_UNLOCK();
        return 0U;
    }

    /* 寻址模式 = 水平，与 OLED_Init 的 0x20/0x00 一致:
     * 水平模式下 0x21/0x22 的窗口与当前页指针位置无关 */
    oled_cmd2(0x20U, 0x00U);

    for (p = 0; p < OLED_PAGE_CNT; p++) {
        uint8_t  bit = (uint8_t)(1U << p);
        uint8_t  page_ok = 1U;
        uint16_t off;

        if ((s_oled_dirty & bit) == 0x00U) continue;    /* 该页无改动，跳过 */

        /* 圈出本页:列 0~127、页 p~p（两段命令合并为一次事务） */
        cmd[0] = 0x21U; cmd[1] = 0x00U; cmd[2] = (uint8_t)(OLED_WIDTH - 1U);
        cmd[3] = 0x22U; cmd[4] = p;     cmd[5] = p;
        if (oled_wr(0x00U, cmd, 6U) != 0U) {
            page_ok = 0U;
        }

        /* 本页 128 字节，分块写以适配 I2C 缓冲 */
        for (off = 0; off < OLED_WIDTH; off += OLED_CHUNK) {
            uint16_t n = OLED_CHUNK;
            if ((uint16_t)(off + n) > OLED_WIDTH) n = (uint16_t)(OLED_WIDTH - off);
            if (oled_wr(0x40U, &oled_buf[p][off], n) != 0U) {
                page_ok = 0U;
            }
        }

        if (page_ok != 0U) {                /* 写成功才清该页脏位 */
            s_oled_dirty &= (uint8_t)~bit;
            cnt++;
        }
    }

    /* 窗口还原为整屏、页 0 起，OLED_Refresh 依赖此状态 */
    cmd[0] = 0x21U; cmd[1] = 0x00U; cmd[2] = (uint8_t)(OLED_WIDTH - 1U);
    cmd[3] = 0x22U; cmd[4] = 0x00U; cmd[5] = (uint8_t)(OLED_PAGE_CNT - 1U);
    (void)oled_wr(0x00U, cmd, 6U);

    OLED_FRAME_UNLOCK();
    return cnt;
}

/* 手动把 8 页全标脏，下次局部刷新整屏重写
 * 用于外部绕过本模块直接改过屏内容，或强制整屏重刷 */
void OLED_MarkAllDirty(void)
{
    OLED_FRAME_LOCK();
    oled_dirty_all();
    OLED_FRAME_UNLOCK();
}

uint8_t OLED_Clear(void)
{
    uint8_t fail;

    /* 清显存与刷屏放在同一临界区（内部两次取锁均为重入），中间无其他任务改显存 */
    OLED_FRAME_LOCK();
    OLED_ClearBuffer();
    fail = OLED_Refresh();
    OLED_FRAME_UNLOCK();
    return fail;
}

void OLED_DisplayOn(void)  { oled_cmd(0xAFU); }
void OLED_DisplayOff(void) { oled_cmd(0xAEU); }

void OLED_SetContrast(uint8_t contrast)
{
    oled_cmd2(0x81U, contrast);
}


/* SSD1306 基本图元绘制，均只改显存 */
void OLED_DrawPoint(uint16_t x, uint16_t y, uint8_t on)
{
    OLED_FRAME_LOCK();
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) { OLED_FRAME_UNLOCK(); return; }

    if (on) oled_buf[y >> 3][x] |= (uint8_t)(1U << (y & 0x07U));
    else    oled_buf[y >> 3][x] &= (uint8_t)~(1U << (y & 0x07U));
    s_oled_dirty |= (uint8_t)(1U << (y >> 3));    /* 点所在的那一页标脏 */
    OLED_FRAME_UNLOCK();
}

uint8_t OLED_GetPoint(uint16_t x, uint16_t y)
{
    uint8_t bit;

    OLED_FRAME_LOCK();
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) { OLED_FRAME_UNLOCK(); return 0; }

    bit = (oled_buf[y >> 3][x] & (uint8_t)(1U << (y & 0x07U))) ? 1U : 0U;
    OLED_FRAME_UNLOCK();
    return bit;
}

void OLED_InvertScreen(void)
{
    uint8_t  p;
    uint16_t c;

    OLED_FRAME_LOCK();
    for (p = 0; p < OLED_PAGE_CNT; p++) {
        for (c = 0; c < OLED_WIDTH; c++) {
            oled_buf[p][c] = (uint8_t)~oled_buf[p][c];
        }
    }
    oled_dirty_all();          /* 整屏都变，全标脏 */
    OLED_FRAME_UNLOCK();
}

void OLED_DrawHLine(uint16_t x0, uint16_t x1, uint16_t y, uint8_t on)
{
    uint16_t t;
    uint16_t x;

    OLED_FRAME_LOCK();
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    for (x = x0; x <= x1; x++) OLED_DrawPoint(x, y, on);
    OLED_FRAME_UNLOCK();
}

void OLED_DrawVLine(uint16_t x, uint16_t y0, uint16_t y1, uint8_t on)
{
    uint16_t t;
    uint16_t y;

    OLED_FRAME_LOCK();
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    for (y = y0; y <= y1; y++) OLED_DrawPoint(x, y, on);
    OLED_FRAME_UNLOCK();
}

/* 直线:Bresenham 算法，与 lcd.c 同一套，绘制目标为显存 */
void OLED_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on)
{
    int16_t x = (int16_t)x0, y = (int16_t)y0;
    int16_t ex = (int16_t)x1, ey = (int16_t)y1;
    int16_t dx = (ex > x) ? (int16_t)(ex - x) : (int16_t)(x - ex);
    int16_t dy = (ey > y) ? (int16_t)(y - ey) : (int16_t)(ey - y);
    int16_t sx = (x < ex) ? 1 : -1;
    int16_t sy = (y < ey) ? 1 : -1;
    int16_t err = (int16_t)(dx + dy);

    OLED_FRAME_LOCK();
    for (;;) {
        OLED_DrawPoint((uint16_t)x, (uint16_t)y, on);
        if (x == ex && y == ey) break;
        {
            int16_t e2 = (int16_t)(2 * err);
            if (e2 >= dy) { err = (int16_t)(err + dy); x = (int16_t)(x + sx); }
            if (e2 <= dx) { err = (int16_t)(err + dx); y = (int16_t)(y + sy); }
        }
    }
    OLED_FRAME_UNLOCK();
}

void OLED_DrawRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on)
{
    OLED_FRAME_LOCK();
    OLED_DrawHLine(x0, x1, y0, on);
    OLED_DrawHLine(x0, x1, y1, on);
    OLED_DrawVLine(x0, y0, y1, on);
    OLED_DrawVLine(x1, y0, y1, on);
    OLED_FRAME_UNLOCK();
}

void OLED_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on)
{
    uint16_t y;

    OLED_FRAME_LOCK();
    for (y = y0; y <= y1; y++) OLED_DrawHLine(x0, x1, y, on);
    OLED_FRAME_UNLOCK();
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

    OLED_FRAME_LOCK();
    if (r < 0) { OLED_FRAME_UNLOCK(); return; }

    while (x <= y) {
        oled_plot8(x0, y0, x, y, on);
        if (d < 0) d = (int16_t)(d + 2 * x + 3);
        else { d = (int16_t)(d + 2 * (x - y) + 5); y--; }
        x++;
    }
    OLED_FRAME_UNLOCK();
}

void OLED_ShowProgress(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t permille)
{
    uint16_t fill;
    uint16_t i;

    OLED_FRAME_LOCK();
    if (w < 2U || h < 2U) { OLED_FRAME_UNLOCK(); return; }
    if (permille > 1000U) permille = 1000U;

    OLED_DrawRect(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U), 1);

    /* 内部可用宽度（去掉 1 像素边框）按比例填充 */
    fill = (uint16_t)(((uint32_t)(w - 2U) * permille) / 1000U);

    for (i = 0; i < fill; i++) {
        OLED_DrawVLine((uint16_t)(x + 1U + i),
                       (uint16_t)(y + 1U), (uint16_t)(y + h - 2U), 1);
    }
    OLED_FRAME_UNLOCK();
}


/* 文字显示（8×16 字库，复用 lcd_font.h） */
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

    OLED_FRAME_LOCK();
    if (x + LCD_FONT8X16_W > OLED_WIDTH ||
        y + LCD_FONT8X16_H > OLED_HEIGHT) {
        OLED_FRAME_UNLOCK();
        return;
    }

    for (i = 0; i < LCD_FONT8X16_H; i++) {
        row = lcd_font8x16[idx][i];
        for (j = 0; j < LCD_FONT8X16_W; j++) {
            bit = (row & (uint8_t)(0x80U >> j)) ? 1U : 0U;
            if (inv) bit = (uint8_t)(bit ^ 1U);      /* 反显：黑白互换 */
            OLED_DrawPoint((uint16_t)(x + j), (uint16_t)(y + i), bit);
        }
    }
    OLED_FRAME_UNLOCK();
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
    /* 整串放同一临界区（内部 ShowChar 再取锁，同任务可重入），避免新旧串混杂 */
    OLED_FRAME_LOCK();
    oled_puts(x, y, str, inv);
    OLED_FRAME_UNLOCK();
}

void OLED_ShowStringCenter(uint16_t y, const char *str, uint8_t inv)
{
    uint16_t n = 0;
    const char *p = str;
    uint16_t x;

    if (str == 0) return;

    while (*p != '\0') { n++; p++; }

    OLED_FRAME_LOCK();
    if ((uint32_t)n * LCD_FONT8X16_W > OLED_WIDTH) {   /* 太长就顶格显示 */
        oled_puts(0, y, str, inv);
        OLED_FRAME_UNLOCK();
        return;
    }
    x = (uint16_t)((OLED_WIDTH - (uint16_t)n * LCD_FONT8X16_W) / 2U);
    oled_puts(x, y, str, inv);
    OLED_FRAME_UNLOCK();
}

void OLED_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t inv)
{
    uint8_t  i;
    uint8_t  n;
    uint32_t div = 1U;

    if (len == 0U || len > 10U) len = 10U;

    for (n = 1U; n < len; n++) div *= 10U;

    OLED_FRAME_LOCK();
    for (i = 0U; i < len; i++) {
        uint8_t d = (uint8_t)((num / div) % 10U);
        char c = (div > num && i != (uint8_t)(len - 1U))
                 ? ' ' : (char)('0' + d);
        OLED_ShowChar((uint16_t)(x + (uint16_t)i * LCD_FONT8X16_W), y, c, inv);
        div /= 10U;
    }
    OLED_FRAME_UNLOCK();
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

    OLED_FRAME_LOCK();
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
    OLED_FRAME_UNLOCK();
}
