#ifndef __FWLIB_NTC_PT100_H
#define __FWLIB_NTC_PT100_H

#include "stm32f4xx.h"

/* ================================================================
 *  ntc_pt100.h —— 【板载】NTC / PT100 温度检测模块  头文件
 * ================================================================
 *  设计定位 : 把板上那套"NTC&PT100 检测"模拟前端变成两个函数：
 *             读电阻 → 读温度。底层复用 sys_adc（不重复造 ADC 轮子）
 *  依赖     : sys_adc.h（ADC 单次采样）+ gpio_core.h
 *  标准库关键词 : 经 sys_adc 间接使用 ADC_Init / ADC_SoftwareStartConv /
 *                 ADC_GetConversionValue / ADC_RegularChannelConfig
 *
 *  【板上这套前端长什么样（原理图逐脚核对，别自己猜）】
 *
 *      VCC3.3 ──[R72 100K/1%]──┬──[R73 10K/1%]──┬── U13B pin3 (+)
 *                              │                 │
 *                          [R74 0R]         [C51 104]
 *                              │                 │
 *                          NPT_IN             GND
 *                              │
 *                      ┌───────┴───────┐
 *                   NTC1(2P)        P12(2P)      ← 传感器插这里
 *                   NTC / GND      PT100 / GND
 *
 *      U13B 同相放大（pin2/pin1）:
 *          pin2 (−) ──[R70 1K/1%]── GND
 *          pin2 (−) ──[R71 10K/1%]── 输出 pin1     → 增益 = 1 + 10K/1K = 11 倍
 *          电源 pin8 = VCC5（5V）, pin4 = GND
 *          pin1 ──[R75 1K/1%]── TAD1 ──[C52 104]── GND   ← 输出 RC 低通 fc≈1.6kHz
 *
 *      CN9（3 脚）: 1 = NTC   2 = NPT_IN   3 = PT100
 *          ★ **用 NTC 时把 NPT_IN↔NTC 短接；用 PT100 时把 NPT_IN↔PT100 短接**
 *            （二选一，不能同时测）
 *      J8（5 脚共享模拟排）: R_ADC / STM_ADC / P_TOUCH / STM_DAC / TAD1
 *          ★★ **必须把 J8 的 `TAD1` 与 `STM_ADC` 短接**，
 *             否则检测信号根本到不了 MCU 的 PA5（ADC12_IN5）
 *
 *  【这条链路怎么算温度（看懂这个就会调了）】
 *      ① 传感器电阻 Rs 与 R72(100K) 从 3.3V 分压:
 *             V_node = 3300mV x Rs / (100000 + Rs)
 *      ② 运放放大 11 倍:
 *             V_out  = 11 x V_node = TAD1 电压
 *      ③ ADC（12 位，参考 3.3V）读回 → raw → V_out → 反推 Rs → 查温度
 *
 *  ⚠⚠ **三个必须知道的坑**
 *    ① **有效量程很窄（这是硬件决定的，不是代码问题）**：
 *       LM358 用 5V 供电，输出最高只能到约 3.5V，所以 V_node 必须 < 318mV
 *       → **Rs 必须 < 约 10.7K**。换句话说：
 *         · 用 **10K NTC** 时，**低于约 24°C 就会饱和（读数顶死）**，
 *           它本来就是为了"高温段更准"设计的，测常温附近勉强能用；
 *         · 用 **1K NTC** 或者 **PT100** 则整段都在范围内，好用得多。
 *       读数卡在满量程不走 → 先查是不是传感器阻值太大（用 SENSOR_IsSaturated()）。
 *    ② **PT100 分辨率很粗**：100°C 时 TAD1 只有 50mV（约 62 个 ADC 码），
 *       1°C 才 0.2 个码。想用好必须：
 *         · 在 `sys_adc` 基础上**过采样**（本模块默认平均 32 次）；
 *         · 或者先做 **零点校准**（SENSOR_Calibrate），把系统误差扣掉。
 *    ③ **TAD1 最高可能到 3.5V，超过 PA5 的 3.3V 量程**：
 *       好在前端有 R75(1K) 限流，灌进 ESD 二极管的电流只有 0.5mA 上下，
 *       不会立刻坏，但**长期如此不推荐** —— 别故意把传感器拔掉后猛转量程。
 *
 *  使用方式 :
 *      SENSOR_Init(SENSOR_TYPE_NTC10K);        // ① 初始化（含 ADC）
 *      float t = SENSOR_ReadTempC();           // ② 摄氏温度
 *      int32_t t10 = SENSOR_ReadTempX10();     //   或 0.1°C 单位整数（好显示）
 *      if (SENSOR_IsSaturated()) { ... }       // ③ 饱和自检
 *
 *  移植指引 : 换电阻/运放改"区块 1"的几个阻值宏；换传感器改 SENSOR_TYPE_* ；
 *             换 ADC 引脚改 SENSOR_ADC_* 四个宏。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 0 = 不编译本模块 */
#ifndef SENSOR_ENABLE
#define SENSOR_ENABLE       1
#endif

/* -------------------- 前端增益电阻（板上实测值，别乱改） -------------------- */
/*  增益 = 1 + R71/R70 = 1 + 10K/1K = 11
 *  下面用"分数"表示，避免浮点初始化：gain = _NUM / _DEN */
#define SENSOR_AMP_R_FB_OHM     10000UL     /* R71：输出 → 反相输入 */
#define SENSOR_AMP_R_GND_OHM    1000UL      /* R70：反相输入 → GND */

/* 分压上拉电阻（R72，100K/1%，1% 精度是"能当测量用"的前提） */
#define SENSOR_PULLUP_OHM       100000UL

/* ADC 参考电压（mV）—— 3.3V */
#define SENSOR_VREF_MV          3300UL

/* -------------------- ADC 通道（PA5 = ADC12_IN5） -------------------- */
/* ⚠ 用之前必须把 J8 的 TAD1 ↔ STM_ADC 短接 */
#define SENSOR_ADC              ADC1
#define SENSOR_ADC_CH           ADC_Channel_5   /* PA5 = ADC12_IN5 */
#define SENSOR_ADC_PORT         GPIOA
#define SENSOR_ADC_PIN          GPIO_Pin_5

/* 每次读取的平均次数（12 位 ADC 过采样，抑制噪声）
 * 1 = 不平均；越大越稳但越慢。默认 32 次 ≈ 几十微秒 */
#define SENSOR_ADC_AVG_TIMES    32

/* -------------------- 饱和判定门限 -------------------- */
/* raw 超过这个值就认为"前端顶到轨了"（4095 满量程）
 * 取 4050 是给噪声留点余量 */
#define SENSOR_SAT_RAW          4050U

/* -------------------- 传感器类型 -------------------- */
typedef enum {
    SENSOR_TYPE_NTC10K = 0,     /* 10K NTC（B=3950）—— 注意低温段会饱和 */
    SENSOR_TYPE_NTC1K  = 1,     /* 1K NTC（B=3950） —— 量程宽得多，推荐 */
    SENSOR_TYPE_PT100  = 2,     /* PT100 铂电阻（工业标准） */
    SENSOR_TYPE_RAW    = 3      /* 不换算，只读原始 ADC / 电阻 */
} SensorType_t;

/* NTC 的 B 参数公式参数（R25 = 25°C 时的阻值） */
#define NTC_B_VALUE             3950.0f     /* B25/50 典型值，查你买的规格书 */
#define NTC_T25_KELVIN          298.15f     /* 25°C 的开氏温度 */

/* PT100：R = R0 x (1 + alpha x T) */
#define PT100_R0_OHM            100.0f
#define PT100_ALPHA             0.00385f    /* 3850 ppm/°C（IEC 60751） */


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：配 ADC 引脚 + 打开 ADC（并记住传感器类型）
 * 参数 : type —— SENSOR_TYPE_NTC10K / NTC1K / PT100 / RAW
 * 返回 : 0 = 成功；1 = 失败
 * 标准库 : 经 sys_adc → RCC_APB2PeriphClockCmd(ADC1) + GPIO 模拟输入 +
 *          ADC_CommonInit + ADC_Init + ADC_Cmd
 * 示例 : SENSOR_Init(SENSOR_TYPE_NTC10K); */
uint8_t SENSOR_Init(SensorType_t type);

/* 运行时切换传感器类型（**注意：换之前先改 CN9 上的短接**）
 * 示例 : SENSOR_SetType(SENSOR_TYPE_PT100);   // 并把 CN9 改为 NPT_IN↔PT100 */
void SENSOR_SetType(SensorType_t type);
SensorType_t SENSOR_GetType(void);

/* 读一次原始 ADC 值（0~4095，已做 SENSOR_ADC_AVG_TIMES 次平均）
 * 示例 : uint16_t raw = SENSOR_ReadRaw(); */
uint16_t SENSOR_ReadRaw(void);

/* 读 TAD1 电压（mV）—— 即运放输出，不是传感器上的电压
 * 示例 : uint32_t mv = SENSOR_ReadMilliVolt(); */
uint32_t SENSOR_ReadMilliVolt(void);

/* 反推传感器电阻（Ω）—— 把"分压 + 11 倍放大 + ADC"整条链路反算回去
 * 说明 : 公式 raw → V_out → V_node = V_out/11 → Rs = 100K x V_node/(3300mV − V_node)
 * 返回 : 电阻（欧姆）；0 = 明显超出量程算不出（已饱和或断线）
 * 示例 : uint32_t rs = SENSOR_ReadResistance(); */
uint32_t SENSOR_ReadResistance(void);

/* 是否已饱和（前端顶到轨 / 传感器阻值超出量程）
 * 说明 : 返回 1 时读数不可信 —— 大概率是"传感器阻值太大"（见文件头坑①）
 * 示例 : if (SENSOR_IsSaturated()) printf("out of range\r\n"); */
uint8_t SENSOR_IsSaturated(void);

/* 读温度（摄氏度，浮点）
 * 说明 : 按 SENSOR_GetType() 选换算方式；RAW 类型时返回 -999
 * 示例 : float t = SENSOR_ReadTempC();   printf("t=%.1f\r\n", t); */
float SENSOR_ReadTempC(void);

/* 读温度（0.1°C 为单位的整数，方便直接显示，且不需要浮点 printf）
 * 返回 : 如 253 = 25.3°C；-9990 = 出错/未支持
 * 示例 : int32_t t = SENSOR_ReadTempX10();   printf("t=%d.%d\r\n", t/10, t%10); */
int32_t SENSOR_ReadTempX10(void);

/* 阻塞式连续采样（自己控制节奏时用）
 * 参数 : times —— 采样次数；结果取平均后写入 *avg_raw（传 0 忽略）
 * 返回 : 平均原始值
 * 示例 : uint16_t r = SENSOR_SampleAveraged(64, 0); */
uint16_t SENSOR_SampleAveraged(uint16_t times, uint16_t *avg_raw);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* ---- 纯换算（不碰硬件）---- */

/* 电阻 → 温度（°C）。按当前类型选择公式
 * 说明 : NTC 用 B 参数公式  1/T = 1/T25 + ln(R/R25)/B
 *        PT100 用线性近似   T = (R/R0 − 1) / alpha（0~100°C 内误差 <0.5°C）
 * 参数 : r_ohm —— 传感器电阻（Ω）
 * 返回 : 摄氏度；无效输入返回 -999 */
float SENSOR_ResToTempC(uint32_t r_ohm);

/* 温度（°C）→ 电阻（反向换算，做仿真的/生成查表数据时用） */
uint32_t SENSOR_TempCToRes(float temp_c);

/* ---- 校准（抵消器件误差，PT100 强烈建议做）---- */
/* 两点校准：给两个"已知温度点"的实测 ADC 原始值，之后读温度会自动修正
 * 参数 : adc_lo / t_lo  —— 低温点（例：冰水 0°C），传 adc=0 关闭校准
 *        adc_hi / t_hi  —— 高温点（例：沸水/体温计对比）
 * 说明 : 内部只做"线性修正"（偏移 + 增益），够用且不会算飞；
 *        两个点的温差别太小（建议 ≥40°C），否则增益修正会放大噪声
 * 示例 : SENSOR_Calibrate(120, 0.0f, 980, 100.0f);   // 冰水 + 沸水 */
void SENSOR_Calibrate(uint16_t adc_lo, float t_lo, uint16_t adc_hi, float t_hi);

/* 关闭校准，回到理论换算 */
void SENSOR_CalibrateOff(void);

/* ---- 自检 ---- */
/* 快速自检：读一次并返回可能的原因码
 * 返回 : 0 = 正常
 *        1 = 饱和（Rs 太大 / 传感器没插 / CN9 短接错）
 *        2 = 接近 0（短路 / CN9 短接错 / 传感器没插但 NPT_IN 接地）
 *        3 = ADC 完全没反应（J8 没短接 TAD1↔STM_ADC）
 * 示例 : uint8_t e = SENSOR_SelfTest(); */
uint8_t SENSOR_SelfTest(void);

/* 打印诊断信息（需要 printf 已重定向，见 sys_usart）
 * 示例 : SENSOR_DumpInfo(); */
void SENSOR_DumpInfo(void);

#endif /* __FWLIB_NTC_PT100_H */
