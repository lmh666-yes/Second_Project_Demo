#include "ntc_pt100.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "sys_adc.h"
#include "gpio_core.h"
#include "sys_usart.h"      /* SENSOR_DumpInfo 用 printf 宏 */

#include <math.h>

/* ================================================================
 *  ntc_pt100.c —— 【板载】NTC / PT100 温度检测模块  实现文件
 * ================================================================
 *  这条链路的数学（看懂它，出问题自己就能定位）：
 *
 *      raw  --(x 3300mV / 4095)-->  V_out          （ADC 读到的电压）
 *      V_out --> V_node = V_out / 11               （扣掉运放 11 倍增益）
 *      V_node --> Rs = 100K x V_node/(3300 - V_node)（分压器反算电阻）
 *      Rs --> T                                    （查传感器特性）
 *
 *  ⇒ 所以"读数不对"永远只有三种原因，按顺序查：
 *      ① J8 没把 TAD1 接到 STM_ADC  → 读到的永远是电位器的值或 0
 *      ② CN9 没短接对（NPT_IN↔NTC 或 NPT_IN↔PT100）
 *      ③ 传感器阻值超出量程（Rs > 10.7K 就饱和，见 .h 文件头的坑①）
 *
 *  ⚠ 为什么要用 float：B 参数公式里有 ln()/exp()，纯整数写只能靠查表。
 *    F407 带**硬件 FPU**（工程里已开 RvdsVP=2），logf/expf 跑起来很快，
 *    这里直接用 float 反而更准更好懂。
 * ================================================================ */


/* ================================================================
 *                      内部状态
 * ================================================================ */
static SensorType_t sensor_type  = SENSOR_TYPE_NTC10K;
static uint8_t      sensor_ready = 0U;

/* 两点校准：把 ADC 原始值线性映射到温度
 *   cal_on = 0 时用理论换算；=1 时用 (adc_lo,t_lo)/(adc_hi,t_hi) 做线性插值 */
static uint8_t  cal_on  = 0U;
static uint16_t cal_alo = 0U;
static uint16_t cal_ahi = 1U;
static float    cal_tlo = 0.0f;
static float    cal_thi = 1.0f;

/* 最近一次读取的原始值（给 IsSaturated / SelfTest 用） */
static uint16_t sensor_last_raw = 0U;


/* ================================================================
 *                      内部小工具
 * ================================================================ */

/* raw → TAD1 电压（mV）。整数算，避免浮点误差进反算 */
static uint32_t raw_to_mv(uint16_t raw)
{
    if (raw > 4095U) raw = 4095U;

    return ((uint32_t)raw * SENSOR_VREF_MV + 2047UL) / 4095UL;
}

/* TAD1 电压（mV）→ 传感器电阻（Ω）
 * 返回 0 表示"超过量程/算不出"（V_node 已经贴到 3.3V 附近） */
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

    /* 先乘后除；Rs 最大约几 MΩ，用 64 位会浪费，这里给个上界保护 */
    if (den == 0UL) return 0UL;
    if (v_node > 3200UL) return 0UL;

    return (SENSOR_PULLUP_OHM / 1000UL) * v_node * 1000UL / den;
}

/* NTC：电阻 → 开氏温度（B 参数公式） */
static float ntc_res_to_kelvin(uint32_t r_ohm, float r25)
{
    float ratio;

    if (r_ohm == 0UL) return 0.0f;

    ratio = (float)r_ohm / r25;

    /* 1/T = 1/T25 + ln(R/R25)/B  →  T = 1 / (1/T25 + ln(R/R25)/B) */
    return 1.0f / ((1.0f / NTC_T25_KELVIN) + (logf(ratio) / NTC_B_VALUE));
}

/* 取 NTC 的 R25（按当前类型） */
static float ntc_r25_ohm(void)
{
    return (sensor_type == SENSOR_TYPE_NTC1K) ? 1000.0f : 10000.0f;
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
uint8_t SENSOR_Init(SensorType_t type)
{
#if (SENSOR_ENABLE == 0)
    (void)type;
    return 1U;
#else
    if (type > SENSOR_TYPE_RAW) return 1U;

    sensor_type = type;

    /* 底层 ADC 由 sys_adc 负责：开时钟 + 引脚设模拟 + 单次转换模式 */
    SYS_ADC_Init(SENSOR_ADC, SENSOR_ADC_CH, SENSOR_ADC_PORT, SENSOR_ADC_PIN);

    sensor_ready    = 1U;
    sensor_last_raw = 0U;
    cal_on          = 0U;       /* 校准不跨 Init 保留，要校准请重新调 */

    /* 先读一次：让 ADC 采样保持电容充好，第一枪往往偏低 */
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
    /* 注意：这里用"上一次 ReadRaw 的结果"，所以要先调用读函数 */
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
        /* R = R0(1 + alpha T)  →  T = (R/R0 - 1)/alpha */
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
        /* 已校准：直接把 raw 线性映射成温度（旁路掉理论换算） */
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


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
void SENSOR_Calibrate(uint16_t adc_lo, float t_lo, uint16_t adc_hi, float t_hi)
{
    if (adc_lo == 0U) {          /* 传 0 = 关闭校准 */
        cal_on = 0U;
        return;
    }
    if (adc_hi <= adc_lo) {      /* 高低点顺序反了：自动交换 */
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

    if (raw >= SENSOR_SAT_RAW) return 1U;       /* 顶到轨：Rs 太大 / 传感器没插 */
    if (raw <= 3U)             return 2U;       /* 地板：短路 / 没插但 NPT_IN 接地 */

    return 0U;
}

void SENSOR_DumpInfo(void)
{
    uint16_t raw = SENSOR_ReadRaw();
    uint32_t mv  = raw_to_mv(raw);
    uint32_t rs  = mv_to_resistance(mv);
    int32_t  t10 = SENSOR_ReadTempX10();

    /* 注意：AC5 下不要用 %f（浮点 printf 要额外库支持），
     * 温度用"0.1度整数"打印最省事 */
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

/* 文件结束 */
