#include "xpt2046.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"      /* 引脚 / 电平 */
#include "delay.h"          /* delay_ns / DWT 计时（延时与测时） */
#include "sys_exti.h"       /* T_PEN 中断（可关，见 XPT2046_USE_EXTI） */
#include "at24c02.h"        /* 校准参数掉电存储（可关，见 XPT2046_USE_EEPROM） */

/* ================================================================
 *  xpt2046.c —— XPT2046 电阻触摸屏驱动  实现文件
 * ================================================================
 *  XPT2046 的时序（和普通 SPI 从机**不一样**，别按 SPI 想）：
 *
 *      一次完整转换 = 24 个 DCLK：
 *        ┌ 8 个时钟：主机发 1 字节"控制字"（S / A2A1A0 / MODE / SER-DFR / PD）
 *        ├ 1 个时钟 ：ADC 正在转换，DOUT 输出的是**占位位**（必须丢掉）
 *        └ 12 个时钟：真正的 12 位结果，MSB 先出
 *
 *      ⚠ 少丢那个"占位位"是新手最常见的错 —— 现象是"读数整体偏大 1 倍左右"。
 *      ⚠ 数据在 DCLK **上升沿**有效（MODE=0 时），所以读的时候要
 *        "拉低 → 等 → 拉高 → 等 → 采样"。
 *
 *  另一个反直觉的点：**X/Y 的原始值和屏幕像素完全不是线性映射到同一区间**，
 *  而且每块屏的电阻膜工艺有差异（同一型号两块屏的 x_min 都能差 200），
 *  所以校准不是"可选项"，是必须做的。
 * ================================================================ */


/* ================================================================
 *                     引脚操作宏（只在本文件用）
 * ================================================================ */
#define XPT_CS_LOW()     GPIO_OutReset(XPT2046_CS_PORT,   XPT2046_CS_PIN)
#define XPT_CS_HIGH()    GPIO_OutSet  (XPT2046_CS_PORT,   XPT2046_CS_PIN)
#define XPT_CLK_LOW()    GPIO_OutReset(XPT2046_CLK_PORT,  XPT2046_CLK_PIN)
#define XPT_CLK_HIGH()   GPIO_OutSet  (XPT2046_CLK_PORT,  XPT2046_CLK_PIN)
#define XPT_DIN_LOW()    GPIO_OutReset(XPT2046_DIN_PORT,  XPT2046_DIN_PIN)
#define XPT_DIN_HIGH()   GPIO_OutSet  (XPT2046_DIN_PORT,  XPT2046_DIN_PIN)
#define XPT_DOUT_READ()  GPIO_InRead  (XPT2046_DOUT_PORT, XPT2046_DOUT_PIN)

#if (XPT2046_PEN_ACTIVE_LOW)
#define XPT_PEN_DOWN()   (GPIO_InRead(XPT2046_PEN_PORT, XPT2046_PEN_PIN) == 0U)
#else
#define XPT_PEN_DOWN()   (GPIO_InRead(XPT2046_PEN_PORT, XPT2046_PEN_PIN) != 0U)
#endif

/* 滤波缓冲上限（比宏大一点，允许运行时传更大的 n） */
#define XPT_MAX_SAMPLE      16U

/* XPT2046 控制字表：索引就是 XPT2046_CH_xxx
 * 位定义 : S(1) | A2 A1 A0 | MODE(0=12bit) | SER/DFR(0=差分) | PD1 PD0(00=转换后掉电) */
static const uint8_t xpt_cmd[8] = {
    0xD0U,      /* X     : A=101 */
    0x90U,      /* Y     : A=001 */
    0xB0U,      /* Z1    : A=011 */
    0xC0U,      /* Z2    : A=100 */
    0xA0U,      /* VBAT  : A=010 */
    0xE0U,      /* AUX   : A=110 */
    0x80U,      /* TEMP0 : A=000 */
    0xF0U       /* TEMP1 : A=111 */
};

/* 当前校准参数 */
static XptCalib_t xpt_cal;
static uint8_t    xpt_cal_valid = 0U;


/* ================================================================
 *                     内部：位敲时序
 * ================================================================ */

/* 产生 1 个时钟并返回该时钟后 DOUT 的电平 */
static uint8_t xpt_clk_one_bit(void)
{
    uint8_t b;

    XPT_CLK_LOW();
    delay_ns(XPT2046_CLK_DELAY_NS);
    XPT_CLK_HIGH();
    delay_ns(XPT2046_CLK_DELAY_NS);

    b = (uint8_t)XPT_DOUT_READ();
    return b;
}

/* 一次完整转换：发命令 → 丢占位位 → 收 12 位 */
static uint16_t xpt_transfer(uint8_t cmd)
{
    uint8_t  i;
    uint16_t val = 0U;

    XPT_CS_LOW();

    /* ① 8 个时钟发控制字（MSB 先出） */
    for (i = 0; i < 8U; i++) {
        XPT_CLK_LOW();
        if ((cmd & 0x80U) != 0U) {
            XPT_DIN_HIGH();
        } else {
            XPT_DIN_LOW();
        }
        cmd = (uint8_t)(cmd << 1);
        delay_ns(XPT2046_CLK_DELAY_NS);
        XPT_CLK_HIGH();
        delay_ns(XPT2046_CLK_DELAY_NS);
    }

    /* ② 占位位：此刻 ADC 还在转换，这一位没有意义 —— 必须丢掉 */
    (void)xpt_clk_one_bit();

    /* ③ 12 位结果 */
    for (i = 0; i < 12U; i++) {
        val = (uint16_t)((val << 1) | (uint16_t)xpt_clk_one_bit());
    }

    XPT_CS_HIGH();
    delay_ns(XPT2046_CLK_DELAY_NS);

    return (uint16_t)(val & 0x0FFFU);
}

/* 简单升序排序（采样数只有几个，选择排序最快） */
static void xpt_sort(uint16_t *a, uint8_t n)
{
    uint8_t i;
    uint8_t j;
    uint16_t t;

    for (i = 0U; i < n; i++) {
        uint8_t min_i = i;
        for (j = (uint8_t)(i + 1U); j < n; j++) {
            if (a[j] < a[min_i]) min_i = j;
        }
        if (min_i != i) {
            t = a[i];
            a[i] = a[min_i];
            a[min_i] = t;
        }
    }
}

/* 线性映射 + 限幅 */
static int32_t xpt_map(int32_t raw, int32_t rmin, int32_t rmax, int32_t px_max)
{
    int32_t v;

    if (rmax <= rmin) return 0;

    v = ((raw - rmin) * px_max) / (rmax - rmin);

    if (v < 0) v = 0;
    if (v > px_max) v = px_max;

    return v;
}

/* 原始值 → 屏幕坐标（含 swap / invert） */
static void xpt_apply(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y)
{
    int32_t rx = (int32_t)raw_x;
    int32_t ry = (int32_t)raw_y;
    int32_t t;
    int32_t sx;
    int32_t sy;

    if (xpt_cal.swap_xy != 0U) {
        t = rx; rx = ry; ry = t;
    }

    sx = xpt_map(rx, (int32_t)xpt_cal.x_min, (int32_t)xpt_cal.x_max,
                 (int32_t)XPT2046_SCREEN_W - 1);
    sy = xpt_map(ry, (int32_t)xpt_cal.y_min, (int32_t)xpt_cal.y_max,
                 (int32_t)XPT2046_SCREEN_H - 1);

    if (xpt_cal.invert_x != 0U) sx = (int32_t)(XPT2046_SCREEN_W - 1) - sx;
    if (xpt_cal.invert_y != 0U) sy = (int32_t)(XPT2046_SCREEN_H - 1) - sy;

    if (x != 0) *x = (uint16_t)sx;
    if (y != 0) *y = (uint16_t)sy;
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
uint8_t XPT2046_Init(void)
{
    /* CS：空闲必须是高（否则总线一直被选中） */
    GPIO_ClockEnable(XPT2046_CS_PORT);
    GPIO_OutInit(XPT2046_CS_PORT, XPT2046_CS_PIN);
    XPT_CS_HIGH();

    /* CLK / DIN：输出，先给低 */
    GPIO_ClockEnable(XPT2046_CLK_PORT);
    GPIO_OutInit(XPT2046_CLK_PORT, XPT2046_CLK_PIN);
    XPT_CLK_LOW();

    GPIO_ClockEnable(XPT2046_DIN_PORT);
    GPIO_OutInit(XPT2046_DIN_PORT, XPT2046_DIN_PIN);
    XPT_DIN_LOW();

    /* DOUT：输入（由 XPT2046 驱动，不加内部上下拉） */
    GPIO_ClockEnable(XPT2046_DOUT_PORT);
    GPIO_InInit(XPT2046_DOUT_PORT, XPT2046_DOUT_PIN, GPIO_PuPd_NOPULL);

    /* PEN(/PENIRQ)：芯片开漏输出 → **必须上拉（GPIO_PuPd_UP）**，否则脚悬空乱跳 */
    GPIO_ClockEnable(XPT2046_PEN_PORT);
    GPIO_InInit(XPT2046_PEN_PORT, XPT2046_PEN_PIN, GPIO_PuPd_UP);

    /* 没校准过就先给一组典型值顶着（否则读出来的坐标全是 0） */
    if (xpt_cal_valid == 0U) XPT2046_CalibDefault();

    delay_us(10);       /* 等 XPT2046 上电稳定 */

    return 0U;
}

uint8_t XPT2046_IsPenDown(void)
{
    return XPT_PEN_DOWN() ? 1U : 0U;
}

uint16_t XPT2046_ReadChannel(uint8_t ch)
{
    if (ch >= 8U) return 0U;

    return xpt_transfer(xpt_cmd[ch]);
}

uint8_t XPT2046_ReadRaw(uint16_t *raw_x, uint16_t *raw_y)
{
    if (raw_x == 0 || raw_y == 0) return 1U;
    if (!XPT_PEN_DOWN()) return 1U;         /* 没按下时读到的都是垃圾 */

    *raw_x = xpt_transfer(xpt_cmd[XPT2046_CH_X]);
    *raw_y = xpt_transfer(xpt_cmd[XPT2046_CH_Y]);

    return 0U;
}

uint8_t XPT2046_ReadRawFiltered(uint16_t *raw_x, uint16_t *raw_y, uint8_t n, uint8_t trim)
{
    uint16_t bx[XPT_MAX_SAMPLE];
    uint16_t by[XPT_MAX_SAMPLE];
    uint32_t sx = 0U;
    uint32_t sy = 0U;
    uint8_t  i;
    uint8_t  keep;

    if (raw_x == 0 || raw_y == 0) return 1U;
    if (!XPT_PEN_DOWN()) return 1U;

    if (n == 0U) n = XPT2046_SAMPLE_N;
    if (n > XPT_MAX_SAMPLE) n = XPT_MAX_SAMPLE;
    if (trim == 0U) trim = XPT2046_SAMPLE_TRIM;

    /* 去极值后至少要剩 1 个数，否则取消去极值 */
    if (((uint16_t)trim * 2U) >= (uint16_t)n) trim = 0U;

    for (i = 0U; i < n; i++) {
        bx[i] = xpt_transfer(xpt_cmd[XPT2046_CH_X]);
        by[i] = xpt_transfer(xpt_cmd[XPT2046_CH_Y]);
    }

    xpt_sort(bx, n);
    xpt_sort(by, n);

    keep = (uint8_t)(n - (uint8_t)(2U * trim));
    for (i = trim; i < (uint8_t)(n - trim); i++) {
        sx += bx[i];
        sy += by[i];
    }

    *raw_x = (uint16_t)(sx / keep);
    *raw_y = (uint16_t)(sy / keep);

    return 0U;
}

uint8_t XPT2046_Read(uint16_t *x, uint16_t *y)
{
    uint16_t rx;
    uint16_t ry;

    if (x == 0 || y == 0) return 0U;
    if (!XPT_PEN_DOWN()) return 0U;

    if (XPT2046_ReadRawFiltered(&rx, &ry, 0U, 0U) != 0U) return 0U;

    /* 采完再确认一次：采样期间松手 → 本帧丢弃（否则会画出一个"飞点"） */
    if (!XPT_PEN_DOWN()) return 0U;

    xpt_apply(rx, ry, x, y);

    return 1U;
}

uint8_t XPT2046_GetEvent(uint16_t *x, uint16_t *y)
{
    static uint8_t  down = 0U;
    static uint16_t lx   = 0U;
    static uint16_t ly   = 0U;

    uint16_t nx;
    uint16_t ny;

    if (XPT2046_Read(&nx, &ny) != 0U) {
        lx = nx;
        ly = ny;
        down = 1U;

        if (x != 0) *x = nx;
        if (y != 0) *y = ny;

        return XPT2046_EVENT_DOWN;
    }

    if (down != 0U) {
        down = 0U;
        if (x != 0) *x = lx;        /* 抬起时给出最后有效坐标 */
        if (y != 0) *y = ly;
        return XPT2046_EVENT_UP;
    }

    return XPT2046_EVENT_NONE;
}

uint8_t XPT2046_PenWaitDown(uint32_t timeout_ms)
{
    uint32_t t0;

    if (timeout_ms == 0U) {
        while (!XPT_PEN_DOWN()) { }
        return 1U;
    }

    t0 = DWT_GetUs();
    while (DWT_ElapsedUs(t0) < (timeout_ms * 1000UL)) {
        if (XPT_PEN_DOWN()) return 1U;
    }

    return 0U;
}

uint8_t XPT2046_PenWaitUp(uint32_t timeout_ms)
{
    uint32_t t0;

    if (timeout_ms == 0U) {
        while (XPT_PEN_DOWN()) { }
        return 1U;
    }

    t0 = DWT_GetUs();
    while (DWT_ElapsedUs(t0) < (timeout_ms * 1000UL)) {
        if (!XPT_PEN_DOWN()) return 1U;
    }

    return 0U;
}


/* ================================================================
 *                    区块 3：校准与扩展
 * ================================================================ */
void XPT2046_Calibrate(uint16_t x_min, uint16_t x_max,
                       uint16_t y_min, uint16_t y_max,
                       uint8_t swap_xy, uint8_t invert_x, uint8_t invert_y)
{
    xpt_cal.x_min = x_min;
    xpt_cal.x_max = x_max;
    xpt_cal.y_min = y_min;
    xpt_cal.y_max = y_max;

    xpt_cal.swap_xy  = (swap_xy  != 0U) ? 1U : 0U;
    xpt_cal.invert_x = (invert_x != 0U) ? 1U : 0U;
    xpt_cal.invert_y = (invert_y != 0U) ? 1U : 0U;

    xpt_cal_valid = 1U;
}

void XPT2046_SetCalib(const XptCalib_t *c)
{
    if (c == 0) return;

    xpt_cal = *c;
    xpt_cal_valid = 1U;
}

void XPT2046_GetCalib(XptCalib_t *c)
{
    if (c == 0) return;

    *c = xpt_cal;
}

void XPT2046_CalibDefault(void)
{
    /* 本板 3.2 寸 ILI9341 + XPT2046 的**典型**范围。
     * ⚠ 只是"能看出方向对不对"的兜底值，正式用请按头文件里的步骤实测。 */
    XPT2046_Calibrate(300U, 3800U, 300U, 3800U, 0U, 0U, 0U);
}

uint8_t XPT2046_IsCalibrated(void)
{
    return xpt_cal_valid;
}

uint8_t XPT2046_CalibSave(void)
{
#if (XPT2046_USE_EEPROM)
    uint8_t buf[10];
    uint8_t flags;

    buf[0] = XPT2046_CALIB_MAGIC;

    buf[1] = (uint8_t)(xpt_cal.x_min & 0xFFU);
    buf[2] = (uint8_t)((xpt_cal.x_min >> 8) & 0xFFU);
    buf[3] = (uint8_t)(xpt_cal.x_max & 0xFFU);
    buf[4] = (uint8_t)((xpt_cal.x_max >> 8) & 0xFFU);
    buf[5] = (uint8_t)(xpt_cal.y_min & 0xFFU);
    buf[6] = (uint8_t)((xpt_cal.y_min >> 8) & 0xFFU);
    buf[7] = (uint8_t)(xpt_cal.y_max & 0xFFU);
    buf[8] = (uint8_t)((xpt_cal.y_max >> 8) & 0xFFU);

    flags  = (uint8_t)((xpt_cal.swap_xy  != 0U) ? 0x01U : 0U);
    flags |= (uint8_t)((xpt_cal.invert_x != 0U) ? 0x02U : 0U);
    flags |= (uint8_t)((xpt_cal.invert_y != 0U) ? 0x04U : 0U);
    buf[9] = flags;

    if (AT24C02_WriteBytes(AT24C02_ADDR_CALIB, buf, 10U) != 0U) return 1U;

    return 0U;
#else
    return 1U;
#endif
}

uint8_t XPT2046_CalibLoad(void)
{
#if (XPT2046_USE_EEPROM)
    uint8_t buf[10];
    uint8_t flags;

    if (AT24C02_ReadBytes(AT24C02_ADDR_CALIB, buf, 10U) != 0U) return 1U;
    if (buf[0] != XPT2046_CALIB_MAGIC) return 1U;       /* 没存过 */

    xpt_cal.x_min = (uint16_t)((uint16_t)buf[1] | ((uint16_t)buf[2] << 8));
    xpt_cal.x_max = (uint16_t)((uint16_t)buf[3] | ((uint16_t)buf[4] << 8));
    xpt_cal.y_min = (uint16_t)((uint16_t)buf[5] | ((uint16_t)buf[6] << 8));
    xpt_cal.y_max = (uint16_t)((uint16_t)buf[7] | ((uint16_t)buf[8] << 8));

    flags = buf[9];
    xpt_cal.swap_xy  = ((flags & 0x01U) != 0U) ? 1U : 0U;
    xpt_cal.invert_x = ((flags & 0x02U) != 0U) ? 1U : 0U;
    xpt_cal.invert_y = ((flags & 0x04U) != 0U) ? 1U : 0U;

    /* 范围明显不合理（比如全 0 / 全 0xFFFF）也算没存过 */
    if (xpt_cal.x_max <= xpt_cal.x_min) return 1U;
    if (xpt_cal.y_max <= xpt_cal.y_min) return 1U;

    xpt_cal_valid = 1U;

    return 0U;
#else
    return 1U;
#endif
}

uint8_t XPT2046_ExtiInit(void (*callback)(void))
{
#if (XPT2046_USE_EXTI)
    /* T_PEN = PB1 → EXTI 线 1（按键占了线 0/2/3/4，本线空闲） */
    return (SYS_EXTI_InitLine(1U, XPT2046_PEN_PORT, XPT2046_PEN_PIN,
                              SYS_EXTI_FALLING, callback) != 0U) ? 0U : 1U;
#else
    (void)callback;
    return 1U;
#endif
}

void XPT2046_ExtiDisable(void)
{
#if (XPT2046_USE_EXTI)
    SYS_EXTI_Disable(1U);
#endif
}
