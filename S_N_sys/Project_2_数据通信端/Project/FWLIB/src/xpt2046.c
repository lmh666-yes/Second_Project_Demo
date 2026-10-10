#include "xpt2046.h"
#include "gpio_core.h"      /* 引脚 / 电平 */
#include "delay.h"          /* delay_ns / DWT 计时（延时与测时） */
#include "sys_exti.h"       /* T_PEN 中断（可关，见 XPT2046_USE_EXTI） */
#include "at24c02.h"        /* 校准参数掉电存储（可关，见 XPT2046_USE_EEPROM） */

/* XPT2046 时序（非标准 SPI）：一次转换 24 个 DCLK，8 位控制字（S/A2A1A0/MODE/SER-DFR/PD）
 * + 1 个占位位 + 12 位结果，MSB 先出；占位位对应 ADC 转换期间，须丢弃，漏丢时读数约偏大 1 倍
 * MODE=0 时数据在 DCLK 上升沿有效，读取顺序：拉低、等待、拉高、等待、采样
 * 各屏电阻膜工艺有差异（同型号 x_min 可差 200），校准参数必须实测 */


/* 引脚操作宏：只在本文件使用 */
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

/* 滤波缓冲上限：运行时 n 大于本值时按本值截断 */
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


/* 内部：位敲时序 */

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

/* 一次完整转换：发命令，丢占位位，收 12 位 */
static uint16_t xpt_transfer(uint8_t cmd)
{
    uint8_t  i;
    uint16_t val = 0U;

    XPT_CS_LOW();

    /* 1) 8 个时钟发控制字（MSB 先出） */
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

    /* 2) 占位位：ADC 转换期间输出，无意义，丢弃 */
    (void)xpt_clk_one_bit();

    /* 3) 12 位结果 */
    for (i = 0; i < 12U; i++) {
        val = (uint16_t)((val << 1) | (uint16_t)xpt_clk_one_bit());
    }

    XPT_CS_HIGH();
    delay_ns(XPT2046_CLK_DELAY_NS);

    return (uint16_t)(val & 0x0FFFU);
}

/* 升序排序：采样数少，用选择排序 */
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

/* 原始值转屏幕坐标：含 swap / invert */
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


/* 基础功能 */
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

    /* PEN(/PENIRQ)：芯片开漏输出，须上拉 GPIO_PuPd_UP，否则引脚悬空 */
    GPIO_ClockEnable(XPT2046_PEN_PORT);
    GPIO_InInit(XPT2046_PEN_PORT, XPT2046_PEN_PIN, GPIO_PuPd_UP);

    /* 未校准时载入默认典型值，否则映射结果全为 0 */
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
    if (!XPT_PEN_DOWN()) return 1U;         /* 未按下时读数无效 */

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

    /* 采样后再次确认按下：采样期间松手则丢弃本帧 */
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


/* 校准与扩展 */
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
    /* 本板 3.2 寸 ILI9341 + XPT2046 的典型范围；仅作兜底值，正式使用按头文件步骤实测校准 */
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

    /* 范围为全 0 / 全 0xFFFF 等不合理值时按未存储处理 */
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
    /* T_PEN = PB1 对应 EXTI 线 1；按键占用线 0/2/3/4，本线空闲 */
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
