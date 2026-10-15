#ifndef __FWLIB_NTC_PT100_H
#define __FWLIB_NTC_PT100_H

#include "stm32f4xx.h"

/* ntc_pt100.h：板载 NTC / PT100 温度检测，读电阻与读温度
 * 依赖 sys_adc.h（ADC 单次采样）与 gpio_core.h
 *
 * 前端电路（普中-天马 F407开发板原理图）:
 *      VCC3.3 ──[R72 100K/1%]──┬──[R73 10K/1%]──┬── U13B pin3 (+)
 *                              │                 │
 *                          [R74 0R]         [C51 104]
 *                              │                 │
 *                          NPT_IN             GND
 *                              │
 *                      NTC1(2P) / P12(2P)：NTC 或 PT100 传感器插座
 *
 *      U13B 同相放大（pin2/pin1）:
 *          pin2 (−) ──[R70 1K/1%]── GND，pin2 (−) ──[R71 10K/1%]── 输出 pin1，
 *          增益 = 1 + 10K/1K = 11 倍；电源 pin8 = VCC5（5V）, pin4 = GND
 *          pin1 ──[R75 1K/1%]── TAD1 ──[C52 104]── GND，输出 RC 低通 fc≈1.6kHz
 *
 *      CN9（3 脚）: 1 = NTC   2 = NPT_IN   3 = PT100
 *          二选一，用 NTC 时 NPT_IN 与 NTC 短接，用 PT100 时与 PT100 短接
 *      J8（5 脚共享模拟排）: R_ADC / STM_ADC / P_TOUCH / STM_DAC / TAD1
 *          TAD1 与 STM_ADC 必须短接，否则信号到不了 MCU 的 PA5（ADC12_IN5）
 *
 * 计算链路:
 *      1) V_node = 3300mV x Rs / (100000 + Rs)
 *      2) V_out  = 11 x V_node，即 TAD1 电压
 *      3) ADC（12 位，参考 3.3V）读回 raw 后依次反推 V_out、Rs，再查温度
 *
 * 量程限制（由硬件决定）:
 *      1) LM358 用 5V 供电，输出上限约 3.5V，故 V_node 须小于 318mV，
 *         Rs 须小于约 10.7K。10K NTC 在低于约 24°C 时饱和，读数顶在满量程；
 *         1K NTC 与 PT100 全量程在范围内
 *      2) PT100 分辨率：100°C 时 TAD1 只有 50mV，约 62 个 ADC 码，1°C 约 0.2 个码，
 *         需靠本模块默认的 32 次平均，或用 SENSOR_Calibrate 扣掉系统误差
 *      3) TAD1 最高约 3.5V，超过 PA5 的 3.3V 量程，由 R75(1K) 限流，
 *         灌入 ESD 二极管的电流约 0.5mA */


/* 区块 1：定义与宏定义区（换板子只改这里） */
/* 0 = 不编译本模块 */
#ifndef SENSOR_ENABLE
#define SENSOR_ENABLE       1
#endif

/* -------------------- 前端增益电阻（板上实测值） -------------------- */
/* 增益 = 1 + R71/R70 = 1 + 10K/1K = 11
 * 下面用分数表示，避免浮点初始化：gain = _NUM / _DEN */
#define SENSOR_AMP_R_FB_OHM     10000UL     /* R71：输出 → 反相输入 */
#define SENSOR_AMP_R_GND_OHM    1000UL      /* R70：反相输入 → GND */

/* 分压上拉电阻（R72，100K/1%，1% 精度为测量前提） */
#define SENSOR_PULLUP_OHM       100000UL

/* ADC 参考电压（mV）: 3.3V */
#define SENSOR_VREF_MV          3300UL

/* -------------------- ADC 通道（PA5 = ADC12_IN5） -------------------- */
/* 用之前必须把 J8 的 TAD1 与 STM_ADC 短接 */
#define SENSOR_ADC              ADC1
#define SENSOR_ADC_CH           ADC_Channel_5   /* PA5 = ADC12_IN5 */
#define SENSOR_ADC_PORT         GPIOA
#define SENSOR_ADC_PIN          GPIO_Pin_5

/* 每次读取的平均次数（12 位 ADC 过采样，抑制噪声）
 * 1 = 不平均；次数越大噪声越小，耗时越长。默认 32 次约几十微秒 */
#define SENSOR_ADC_AVG_TIMES    32

/* -------------------- 饱和判定门限 -------------------- */
/* raw 超过此值判为前端顶到轨（满量程 4095）
 * 取 4050 留出噪声余量 */
#define SENSOR_SAT_RAW          4050U

/* -------------------- 传感器类型 -------------------- */
typedef enum {
    SENSOR_TYPE_NTC10K = 0,     /* 10K NTC（B=3950），低温段会饱和 */
    SENSOR_TYPE_NTC1K  = 1,     /* 1K NTC（B=3950），量程较宽 */
    SENSOR_TYPE_PT100  = 2,     /* PT100 铂电阻 */
    SENSOR_TYPE_RAW    = 3      /* 不换算，只读原始 ADC / 电阻 */
} SensorType_t;

/* NTC 的 B 参数公式参数（R25 = 25°C 时的阻值） */
#define NTC_B_VALUE             3950.0f     /* B25/50 典型值，以所购型号规格书为准 */
#define NTC_T25_KELVIN          298.15f     /* 25°C 的开氏温度 */

/* PT100：R = R0 x (1 + alpha x T) */
#define PT100_R0_OHM            100.0f
#define PT100_ALPHA             0.00385f    /* 3850 ppm/°C（IEC 60751） */


/* 区块 2：基础功能 */
/* 初始化：配 ADC 引脚 + 打开 ADC（并保存传感器类型）
 * 参数 : type, SENSOR_TYPE_NTC10K / NTC1K / PT100 / RAW
 * 返回 : 0 = 成功；1 = 失败 */
uint8_t SENSOR_Init(SensorType_t type);

/* 运行时切换传感器类型（换之前先改 CN9 上的短接） */
void SENSOR_SetType(SensorType_t type);
SensorType_t SENSOR_GetType(void);

/* 读一次原始 ADC 值（0~4095，已做 SENSOR_ADC_AVG_TIMES 次平均） */
uint16_t SENSOR_ReadRaw(void);

/* 读 TAD1 电压（mV），即运放输出，不是传感器上的电压 */
uint32_t SENSOR_ReadMilliVolt(void);

/* 反推传感器电阻（Ω）：分压 + 11 倍放大 + ADC 整条链路反算
 * 公式 : raw → V_out → V_node = V_out/11 → Rs = 100K x V_node/(3300mV − V_node)
 * 返回 : 电阻（欧姆）；0 = 超出量程算不出（已饱和或断线） */
uint32_t SENSOR_ReadResistance(void);

/* 是否已饱和（前端顶到轨 / 传感器阻值超出量程）
 * 返回 1 时读数不可信，多为传感器阻值太大（量程上限见文件头） */
uint8_t SENSOR_IsSaturated(void);

/* 读温度（摄氏度，浮点）
 * 按 SENSOR_GetType() 选换算方式；RAW 类型时返回 -999 */
float SENSOR_ReadTempC(void);

/* 读温度（0.1°C 为单位的整数，可直接显示，不需要浮点 printf）
 * 返回 : 如 253 = 25.3°C；-9990 = 出错/未支持 */
int32_t SENSOR_ReadTempX10(void);

/* 阻塞式连续采样（自己控制节奏时用）
 * 参数 : times, 采样次数；结果取平均后写入 *avg_raw（传 0 忽略）
 * 返回 : 平均原始值 */
uint16_t SENSOR_SampleAveraged(uint16_t times, uint16_t *avg_raw);


/* 区块 3：扩展功能 */
/* ---- 纯换算（不碰硬件）---- */

/* 电阻 → 温度（°C）。按当前类型选择公式
 * 说明 : NTC 用 B 参数公式  1/T = 1/T25 + ln(R/R25)/B
 *        PT100 用线性近似   T = (R/R0 − 1) / alpha（0~100°C 内误差 <0.5°C）
 * 参数 : r_ohm, 传感器电阻（Ω）
 * 返回 : 摄氏度；无效输入返回 -999 */
float SENSOR_ResToTempC(uint32_t r_ohm);

/* 温度（°C）→ 电阻（反向换算，仿真或生成查表数据用） */
uint32_t SENSOR_TempCToRes(float temp_c);

/* ---- 校准（抵消器件误差，PT100 建议做）---- */
/* 两点校准：给出两个已知温度点的实测 ADC 原始值，之后读温度自动修正
 * 参数 : adc_lo / t_lo, 低温点（例：冰水 0°C），adc=0 关闭校准
 *        adc_hi / t_hi, 高温点（例：沸水 100°C）
 * 说明 : 只做线性修正（偏移 + 增益）；两点温差不小于 40°C，否则增益修正放大噪声 */
void SENSOR_Calibrate(uint16_t adc_lo, float t_lo, uint16_t adc_hi, float t_hi);

/* 关闭校准，回到理论换算 */
void SENSOR_CalibrateOff(void);

/* ---- 自检 ---- */
/* 快速自检：读一次并返回可能的原因码
 * 返回 : 0 = 正常
 *        1 = 饱和（Rs 太大 / 传感器没插 / CN9 短接错）
 *        2 = 接近 0（短路 / CN9 短接错 / 传感器没插但 NPT_IN 接地）
 *        3 = ADC 完全没反应（J8 没短接 TAD1 与 STM_ADC） */
uint8_t SENSOR_SelfTest(void);

/* 打印诊断信息（需要 printf 已重定向，见 sys_usart） */
void SENSOR_DumpInfo(void);

#endif /* __FWLIB_NTC_PT100_H */
