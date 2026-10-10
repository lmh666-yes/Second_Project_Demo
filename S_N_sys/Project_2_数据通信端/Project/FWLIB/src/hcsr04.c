#include "hcsr04.h"
#include "delay.h"      /* 延时（delay_us 等）独立文件 */

/* hcsr04.c : HC-SR04 超声波测距实现
 * ---------------------------------------------------------------
 *   功能        资源                  板上位置          改这里
 * ---------------------------------------------------------------
 *   TRIG 脚     GPIO（运行时指定）    自己接排针        HCSR04_Init 参数
 *   ECHO 脚     GPIO（运行时指定）    自己接排针        HCSR04_Init 参数
 *   计时机        DWT CYCCNT           内核自带          gpio_core.h
 *   微秒延时     delay_us()                              delay.h
 * ---------------------------------------------------------------
 * ECHO 是 5V 电平，STM32F407 的 FT 脚可直接接，要保险就分压
 *
 * TRIG 高电平 >= 10us 触发测距；ECHO 高电平宽度 t 为回波往返时间；
 * 距离 = 声速 × t ÷ 2；20℃ 时距离(cm) ≈ t(us) ÷ 58
 *
 * 一次测量最长阻塞 HCSR04_TIMEOUT_US(32ms)，常用调用周期 60ms，本模块用忙等实现，不占定时器 */

/* 编译期检查：配置越界时编译报错 */
typedef char hcsr04_timeout_check[(HCSR04_TIMEOUT_US >= 6000U) ? 1 : -1];   /* 至少要够 1m */
typedef char hcsr04_range_check[((HCSR04_MAX_CM) > (HCSR04_MIN_CM)) ? 1 : -1];
typedef char hcsr04_pulse_check[((HCSR04_TRIG_PULSE_US) >= 10U) ? 1 : -1];
typedef char hcsr04_median_check[(HCSR04_MEDIAN_MAX >= 3U) ? 1 : -1];


/* 模块内部状态 */
static GPIO_TypeDef *s_trig_port = 0;
static uint16_t      s_trig_pin  = 0U;
static GPIO_TypeDef *s_echo_port = 0;
static uint16_t      s_echo_pin  = 0U;
static uint8_t       s_ready     = 0U;
static uint32_t      s_speed     = HCSR04_DEFAULT_SPEED;


/* 基础功能 */

uint8_t HCSR04_Init(GPIO_TypeDef *trig_port, uint16_t trig_pin,
                    GPIO_TypeDef *echo_port, uint16_t echo_pin)
{
    if (trig_port == 0 || echo_port == 0) return HCSR04_ERR_PARAM;
    if (trig_pin == 0U || echo_pin == 0U) return HCSR04_ERR_PARAM;

    s_ready = 0U;

    /* TRIG 推挽输出，初始化后拉低 */
    GPIO_OutInit(trig_port, trig_pin);
    GPIO_OutReset(trig_port, trig_pin);

    /* ECHO 上拉输入，未接传感器时恒为高，返回 HCSR04_ERR_NO_ECHO 而不是随机值 */
    GPIO_InInit(echo_port, echo_pin, GPIO_PuPd_UP);

    s_trig_port = trig_port;
    s_trig_pin  = trig_pin;
    s_echo_port = echo_port;
    s_echo_pin  = echo_pin;
    s_speed     = HCSR04_DEFAULT_SPEED;
    s_ready     = 1U;

    return HCSR04_OK;
}

uint8_t HCSR04_ReadUs(uint32_t *us)
{
    uint32_t t0;
    uint32_t t_rise;
    uint32_t t_fall;

    if (us == 0) return HCSR04_ERR_PARAM;
    if (s_ready == 0U) return HCSR04_ERR_NOT_INIT;

    /* 1) 触发：TRIG 输出 >=10us 高脉冲，模块随即发出 8 个 40kHz 脉冲 */
    GPIO_OutSet(s_trig_port, s_trig_pin);
    delay_us(HCSR04_TRIG_PULSE_US);
    GPIO_OutReset(s_trig_port, s_trig_pin);

    /* 2) 等 ECHO 变高，说明超声已发出 */
    t0 = DWT_GetUs();
    while (GPIO_InRead(s_echo_port, s_echo_pin) == 0U) {
        if (DWT_ElapsedUs(t0) > HCSR04_TIMEOUT_US) return HCSR04_ERR_NO_ECHO;
    }
    t_rise = DWT_GetUs();

    /* 3) 等 ECHO 变低，说明回波已到，高电平宽度即往返时间 */
    while (GPIO_InRead(s_echo_port, s_echo_pin) != 0U) {
        if (DWT_ElapsedUs(t_rise) > HCSR04_TIMEOUT_US) return HCSR04_ERR_NO_ECHO;
    }
    t_fall = DWT_GetUs();

    /* 两个时间戳都单调递增，直接相减，回绕时结果仍正确 */
    *us = (uint32_t)(t_fall - t_rise);
    return HCSR04_OK;
}

uint8_t HCSR04_ReadMm(uint32_t *mm)
{
    uint32_t us = 0U;
    uint32_t d;
    uint8_t  r;

    if (mm == 0) return HCSR04_ERR_PARAM;

    r = HCSR04_ReadUs(&us);
    if (r != HCSR04_OK) return r;

    /* 距离(mm) = 声速(mm/s) × 时间(us) ÷ 1000000 ÷ 2
     * 直接把时间和声速相乘会溢出：32000 × 346400 = 1.1e10，32 位装不下；
     * 声速先除以 100 降一个量级，最高 3600，32000 × 3600 = 1.15e8，不溢出 */
    d = (uint32_t)((us * (s_speed / 100UL)) / 20000UL);

    *mm = d;
    return HCSR04_OK;
}

uint8_t HCSR04_ReadCm(uint16_t *cm)
{
    uint32_t mm = 0U;
    uint32_t c;
    uint8_t  r;

    if (cm == 0) return HCSR04_ERR_PARAM;

    r = HCSR04_ReadMm(&mm);
    if (r != HCSR04_OK) return r;

    c = (mm + 5UL) / 10UL;              /* 毫米转厘米，四舍五入 */

    if (c < (uint32_t)HCSR04_MIN_CM) return HCSR04_ERR_RANGE;
    if (c > (uint32_t)HCSR04_MAX_CM) return HCSR04_ERR_RANGE;

    *cm = (uint16_t)c;
    return HCSR04_OK;
}


/* 扩展功能 */

void HCSR04_SetSpeed(uint32_t mm_per_s)
{
    /* 范围外的值直接忽略：声速不小于 250m/s，不大于 400m/s */
    if (mm_per_s < 250000UL || mm_per_s > 400000UL) return;
    s_speed = mm_per_s;
}

void HCSR04_SetTempC10(int16_t t_c10)
{
    /* v(mm/s) = 331400 + 60 × t(℃)，t 为摄氏度 × 10 */
    int32_t v = 331400 + (60L * (int32_t)t_c10);

    if (v < 250000L) v = 250000L;
    if (v > 400000L) v = 400000L;

    s_speed = (uint32_t)v;
}

uint8_t HCSR04_ReadCmMedian(uint16_t *cm, uint8_t times)
{
    uint16_t buf[HCSR04_MEDIAN_MAX];
    uint8_t  n = 0U;
    uint8_t  i;
    uint8_t  j;

    if (cm == 0) return HCSR04_ERR_PARAM;
    if (times == 0U) return HCSR04_ERR_PARAM;
    if (times > (uint8_t)HCSR04_MEDIAN_MAX) times = (uint8_t)HCSR04_MEDIAN_MAX;

    /* 1) 连续测 times 次，失败的结果丢弃 */
    for (i = 0U; i < times; i++) {
        uint16_t v = 0U;
        if (HCSR04_ReadCm(&v) == HCSR04_OK) buf[n++] = v;
    }
    if (n == 0U) return HCSR04_ERR_NO_ECHO;

    /* 2) 冒泡排序，n 最大为 HCSR04_MEDIAN_MAX */
    for (i = 0U; i + 1U < n; i++) {
        for (j = 0U; j + 1U < (uint8_t)(n - i); j++) {
            if (buf[j] > buf[j + 1U]) {
                uint16_t t = buf[j];
                buf[j] = buf[j + 1U];
                buf[j + 1U] = t;
            }
        }
    }

    /* 3) 取中值 */
    *cm = buf[n / 2U];
    return HCSR04_OK;
}

const char *HCSR04_ErrStr(uint8_t err)
{
    switch (err) {
    case HCSR04_OK:           return "OK";
    case HCSR04_ERR_PARAM:    return "ERR: null param";
    case HCSR04_ERR_NO_ECHO:  return "ERR: no echo (out of range or not wired)";
    case HCSR04_ERR_RANGE:    return "ERR: out of 2-400cm";
    case HCSR04_ERR_NOT_INIT: return "ERR: call HCSR04_Init first";
    default:                  return "ERR: unknown";
    }
}
