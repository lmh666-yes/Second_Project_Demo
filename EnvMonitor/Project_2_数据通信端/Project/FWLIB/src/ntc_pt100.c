#include "ntc_pt100.h"
#include "sys_adc.h"
#include "gpio_core.h"
#include "sys_usart.h"      /* SENSOR_DumpInfo 用 printf 宏 */

#include <math.h>

/* NTC / PT100 温度检测模块实现
 * 换算链路：raw -> V_out(mV) -> V_node(扣运放 11 倍增益) -> Rs(分压器反算) -> 温度
 * 读数不对时按顺序查：J8 是否把 TAD1 接到 STM_ADC、CN9 是否短接正确（NPT_IN 到 NTC 或 PT100）、
 * 传感器阻值是否超量程（Rs > 10.7K 饱和）；用 float 是因为 B 参数公式含 ln()/exp()，F407 带硬件 FPU（RvdsVP=2） */


/* 内部状态 */
static SensorType_t sensor_type  = SENSOR_TYPE_NTC10K;
static uint8_t      sensor_ready = 0U;

/* 两点校准：把 ADC 原始值线性映射到温度
 * cal_on = 0 用理论换算；cal_on = 1 用 (adc_lo,t_lo)/(adc_hi,t_hi) 线性插值 */
static uint8_t  cal_on  = 0U;
static uint16_t cal_alo = 0U;
static uint16_t cal_ahi = 1U;
static float    cal_tlo = 0.0f;
static float    cal_thi = 1.0f;

/* 最近一次读取的原始值，供 IsSaturated / SelfTest 使用 */
static uint16_t sensor_last_raw = 0U;


/* 内部小工具 */

/* raw -> TAD1 电压（mV）；raw 上限 4095，对 4095 取整时加 2047 做四舍五入
 * 用整数运算，避免浮点误差进入后续反算 */
static uint32_t raw_to_mv(uint16_t raw)
{
    if (raw > 4095U) raw = 4095U;

    return ((uint32_t)raw * SENSOR_VREF_MV + 2047UL) / 4095UL;
}

/* TAD1 电压（mV）-> 传感器电阻（Ω）；返回 0 表示超量程或算不出 */
static uint32_t mv_to_resistance(uint32_t v_out_mv)
{
    uint32_t gain_num = (SENSOR_AMP_R_FB_OHM + SENSOR_AMP_R_GND_OHM);
    uint32_t gain_den = SENSOR_AMP_R_GND_OHM;
    uint32_t v_node;
    uint32_t den;

    /* 步骤 1：扣掉运放增益  V_node = V_out x Rgnd/(Rfb+Rgnd) */
    v_node = (v_out_mv * gain_den) / gain_num;

    /* 步骤 2：分压器反算  Rs = Rpullup x V_node / (Vref - V_node) */
    if (v_node >= (SENSOR_VREF_MV - 2UL)) return 0UL;   /* 除数会趋 0，判无效 */

    den = SENSOR_VREF_MV - v_node;

    /* 先乘后除，避免整除丢精度；v_node 上界 3200mV 由量程决定，再加 den 判 0 兜底 */
    if (den == 0UL) return 0UL;
    if (v_node > 3200UL) return 0UL;

    return (SENSOR_PULLUP_OHM / 1000UL) * v_node * 1000UL / den;
}

/* NTC：电阻 -> 开氏温度（B 参数公式），参数 r25 为 25℃ 标称阻值（Ω），返回 0 表示输入无效 */
static float ntc_res_to_kelvin(uint32_t r_ohm, float r25)
{
    float ratio;

    if (r_ohm == 0UL) return 0.0f;

    ratio = (float)r_ohm / r25;

    /* 1/T = 1/T25 + ln(R/R25)/B，即 T = 1 / (1/T25 + ln(R/R25)/B) */
    return 1.0f / ((1.0f / NTC_T25_KELVIN) + (logf(ratio) / NTC_B_VALUE));
}

/* 取当前 NTC 类型的 R25：NTC1K 为 1000Ω，其余为 10000Ω */
static float ntc_r25_ohm(void)
{
    return (sensor_type == SENSOR_TYPE_NTC1K) ? 1000.0f : 10000.0f;
}


/* 基础功能 */
uint8_t SENSOR_Init(SensorType_t type)
{
#if (SENSOR_ENABLE == 0)
    (void)type;
    return 1U;
#else
    if (type > SENSOR_TYPE_RAW) return 1U;

    sensor_type = type;

    /* 底层 ADC 由 sys_adc 负责：开时钟、引脚设模拟输入、单次转换模式 */
    SYS_ADC_Init(SENSOR_ADC, SENSOR_ADC_CH, SENSOR_ADC_PORT, SENSOR_ADC_PIN);

    sensor_ready    = 1U;
    sensor_last_raw = 0U;
    cal_on          = 0U;       /* 校准不跨 Init 保留，需要校准要重新调用 */

    /* 先读一次：让 ADC 采样保持电容充好，第一次转换结果往往偏低 */
    (void)SENSOR_SampleAveraged(4U, 0);

    return 0U;
#endif
}

void SENSOR_SetType(SensorType_t type)
{
    if (type > SENSOR_TYPE_RAW) return;
    sensor_type = type;
}

SensorType_t SENSOR_GetType(void)
{
    return sensor_type;
}

uint16_t SENSOR_SampleAveraged(uint16_t times, uint16_t *avg_raw)
{
    uint32_t sum = 0UL;
    uint16_t i;
    uint16_t n = (times == 0U) ? 1U : times;
    uint16_t avg;

    for (i = 0U; i < n; i++) {
        sum += SYS_ADC_Read(SENSOR_ADC, SENSOR_ADC_CH);
    }
    avg = (uint16_t)(sum / (uint32_t)n);

    if (avg_raw != 0) *avg_raw = avg;

    return avg;
}

uint16_t SENSOR_ReadRaw(void)
{
    uint16_t raw;

    if (sensor_ready == 0U) return 0U;

    raw = SENSOR_SampleAveraged(SENSOR_ADC_AVG_TIMES, 0);
    sensor_last_raw = raw;

    return raw;
}

uint32_t SENSOR_ReadMilliVolt(void)
{
    return raw_to_mv(SENSOR_ReadRaw());
}

uint32_t SENSOR_ReadResistance(void)
{
    return mv_to_resistance(SENSOR_ReadMilliVolt());
}

uint8_t SENSOR_IsSaturated(void)
{
    /* 判据取自最近一次 ReadRaw 的结果，调用本函数前须先调用读函数 */
    return (sensor_last_raw >= SENSOR_SAT_RAW) ? 1U : 0U;
}

float SENSOR_ResToTempC(uint32_t r_ohm)
{
    float tk;

    if (r_ohm == 0UL) return -999.0f;

    if ((sensor_type == SENSOR_TYPE_NTC10K) || (sensor_type == SENSOR_TYPE_NTC1K)) {
        tk = ntc_res_to_kelvin(r_ohm, ntc_r25_ohm());
        if (tk <= 0.0f) return -999.0f;
        return tk - 273.15f;
    }

    if (sensor_type == SENSOR_TYPE_PT100) {
        /* R = R0(1 + alpha T)，即 T = (R/R0 - 1)/alpha */
        return (((float)r_ohm / PT100_R0_OHM) - 1.0f) / PT100_ALPHA;
    }

    return -999.0f;
}

uint32_t SENSOR_TempCToRes(float temp_c)
{
    if (sensor_type == SENSOR_TYPE_PT100) {
        float r = PT100_R0_OHM * (1.0f + PT100_ALPHA * temp_c);
        return (r < 0.0f) ? 0UL : (uint32_t)r;
    }

    if ((sensor_type == SENSOR_TYPE_NTC10K) || (sensor_type == SENSOR_TYPE_NTC1K)) {
        float tk = temp_c + 273.15f;
        float r;
        if (tk <= 0.0f) return 0UL;
        r = ntc_r25_ohm() * expf(NTC_B_VALUE * ((1.0f / tk) - (1.0f / NTC_T25_KELVIN)));
        return (r < 0.0f) ? 0UL : (uint32_t)r;
    }

    return 0UL;
}

float SENSOR_ReadTempC(void)
{
    uint16_t raw;
    float    t;

    if (sensor_ready == 0U) return -999.0f;

    raw = SENSOR_ReadRaw();

    if (sensor_type == SENSOR_TYPE_RAW) return -999.0f;

    if (cal_on != 0U) {
        /* 已校准：直接按两点把 raw 线性映射成温度，旁路理论换算 */
        float span = (float)(cal_ahi - cal_alo);
        if (span == 0.0f) return -999.0f;
        t = cal_tlo + ((float)((int32_t)raw - (int32_t)cal_alo) *
                       (cal_thi - cal_tlo) / span);
        return t;
    }

    return SENSOR_ResToTempC(mv_to_resistance(raw_to_mv(raw)));
}

int32_t SENSOR_ReadTempX10(void)
{
    float t = SENSOR_ReadTempC();

    if (t <= -900.0f) return -9990;

    return (int32_t)((t < 0.0f) ? (t * 10.0f - 0.5f) : (t * 10.0f + 0.5f));
}


/* 扩展功能 */
void SENSOR_Calibrate(uint16_t adc_lo, float t_lo, uint16_t adc_hi, float t_hi)
{
    if (adc_lo == 0U) {          /* 传 0 表示关闭校准 */
        cal_on = 0U;
        return;
    }
    if (adc_hi <= adc_lo) {      /* 高低点顺序反了就交换 */
        uint16_t tmp_adc = adc_lo;
        float    tmp_t   = t_lo;
        adc_lo = adc_hi;  t_lo = t_hi;
        adc_hi = tmp_adc;  t_hi = tmp_t;
    }

    cal_alo = adc_lo;
    cal_ahi = adc_hi;
    cal_tlo = t_lo;
    cal_thi = t_hi;
    cal_on  = 1U;
}

void SENSOR_CalibrateOff(void)
{
    cal_on = 0U;
}

uint8_t SENSOR_SelfTest(void)
{
    uint16_t raw;

    if (sensor_ready == 0U) return 3U;

    raw = SENSOR_ReadRaw();

    if (raw >= SENSOR_SAT_RAW) return 1U;       /* 顶到轨：Rs 过大或传感器未接 */
    if (raw <= 3U)             return 2U;       /* 触底：短路，或未接且 NPT_IN 接地 */

    return 0U;
}

void SENSOR_DumpInfo(void)
{
    uint16_t raw = SENSOR_ReadRaw();
    uint32_t mv  = raw_to_mv(raw);
    uint32_t rs  = mv_to_resistance(mv);
    int32_t  t10 = SENSOR_ReadTempX10();

    /* AC5 下不能用 %f（浮点 printf 需额外库支持），温度用 0.1 度整数打印 */
    SYS_USART_Printf(SYS_USART_PRINTF_ID,
                     "sensor: type=%d raw=%u vout=%lumV rs=%luohm t=%d.%d cal=%d\r\n",
                     (int)sensor_type,
                     (unsigned)raw,
                     (unsigned long)mv,
                     (unsigned long)rs,
                     (int)(t10 / 10),
                     (int)((t10 < 0) ? (-t10 % 10) : (t10 % 10)),
                     (int)cal_on);
}
