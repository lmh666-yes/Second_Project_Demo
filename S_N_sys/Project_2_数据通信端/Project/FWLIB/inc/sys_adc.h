#ifndef __FWLIB_SYS_ADC_H
#define __FWLIB_SYS_ADC_H

#include "stm32f4xx.h"
#include "sys_tim.h"    /* 定时器触发采样要用 SysTimId_t（见文件末尾的 DmaTimerTrigInit） */

/* ================================================================
 *  sys_adc.h —— 【系统】ADC 模数转换模块  头文件
 * ================================================================
 *  设计定位 : 标准外设库 ADC 的"薄封装"—— 覆盖三种典型用法:
 *      ① 单次转换   SYS_ADC_Read()        —— 用一次测一次（测电压/光敏）
 *      ② 连续转换   SYS_ADC_ContInit()    —— 后台不停转换，随时取最新值
 *      ③ DMA 采集   SYS_ADC_DmaInit()     —— 数据自动进内存缓冲
 *                      （单通道或多通道扫描，见区块 3）
 *  标准库关键词 : ADC_CommonInit / ADC_Init / ADC_RegularChannelConfig / ADC_Cmd /
 *                 ADC_SoftwareStartConv / ADC_GetFlagStatus / ADC_GetConversionValue / ADC_DMACmd
 *
 *  本板资源（普中-天马 F407开发板原理图）:
 *      光敏电阻分压 → PF7 = ADC3_IN5（区块 1 有现成宏 SYS_ADC_LIGHT_*）
 *      ⚠ 同一引脚还被 ext_io 当作数字量使用（EXT_LIGHT_*）——
 *        两者会互相改引脚配置，同一工程里只用其中一种
 *      ⚠ 板载 DS18B20 温湿度/温度是"单总线"器件，不归 ADC/I2C 管
 *      ⚠ 模拟输入排针 STM_ADC = PA5（J8）、R_ADC = PA4（外部电位器/NTC）
 *
 *  基础概念（切换引脚时看这里）:
 *      · 分辨率 12 位：读数 0 ~ 4095，对应电压 0 ~ VREF
 *       · 换算：电压(mV) = 读数 × 3300 / 4095（SYS_ADC_ToMilliVolt）
 *       · 采样时间越长越抗干扰（高阻抗源如光敏/热敏电阻用长采样）
 *       · F407 单次转换耗时 ≈ (采样周期 + 12) / 21MHz，
 *         用 480 周期采样时约 23µs —— 单次读也很快
 *       · 引脚必须配置为"模拟输入(AIN)"，此时施密特触发器关闭
 *
 *  使用方式 :
 *      SYS_ADC_Init(SYS_ADC_LIGHT_ADC, SYS_ADC_LIGHT_CH,
 *                   SYS_ADC_LIGHT_PORT, SYS_ADC_LIGHT_PIN);
 *      uint16_t raw = SYS_ADC_Read(SYS_ADC_LIGHT_ADC, SYS_ADC_LIGHT_CH);
 *      uint32_t mv  = SYS_ADC_ToMilliVolt(raw);
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 参考电压（mV）：默认 VDDA = 3300；板上有 VREF 跳线、或换板后
 * 实测过参考电压的话改这里，SYS_ADC_ToMilliVolt 会跟着变准 */
#define SYS_ADC_VREF_MV         3300UL

/* 采样时间：越大越稳、越慢
 *   ADC_SampleTime_3Cycles   ~ 最快，适合低阻抗源（运放输出）
 *   ADC_SampleTime_84Cycles  ~ 通用
 *   ADC_SampleTime_480Cycles ~ 最慢最稳（默认，适合光敏等高阻源） */
#define SYS_ADC_SAMPLE_TIME     ADC_SampleTime_480Cycles

/* SYS_ADC_ReadAvg 不传次数时的默认平均次数 */
#define SYS_ADC_AVG_TIMES       8

/* -------------------- 板载光敏（示例） -------------------- */
#define SYS_ADC_LIGHT_ADC       ADC3
#define SYS_ADC_LIGHT_CH        ADC_Channel_5      /* PF7 = ADC3_IN5 */
#define SYS_ADC_LIGHT_PORT      GPIOF
#define SYS_ADC_LIGHT_PIN       GPIO_Pin_7

/* -------------------- J8 共享模拟输入（PA5 = 网络名 STM_ADC） --------------------
 * J8 是一个 5 脚跳线排（R_ADC / STM_ADC / P_TOUCH / STM_DAC / TAD1），
 * 用跳线帽把想要的信号接到 PA5 上 —— 一次只能接一个：
 *     R_ADC   ↔ STM_ADC ：板上电位器 AD1(10K) 分压 → 做 ADC 实验
 *     P_TOUCH ↔ STM_ADC ：电容触摸板 → 做触摸实验（要用充电时间法量）
 *     TAD1    ↔ STM_ADC ：NTC/PT100 模块输出 → 做温度检测
 * ⚠ PA5 与 ext_io 的"电容触摸"是同一个脚，两者不能同时用。 */
#define SYS_ADC_JMP_ADC         ADC1
#define SYS_ADC_JMP_CH          ADC_Channel_5      /* PA5 = ADC12_IN5 */
#define SYS_ADC_JMP_PORT        GPIOA
#define SYS_ADC_JMP_PIN         GPIO_Pin_5

/* -------------------- J8 上的 DAC 输出（PA4 = 网络名 STM_DAC） --------------------
 * PA4 = DAC_OUT1（也是 ADC12_IN4）。本库未封 DAC 模块，要用需自己
 * 开 DAC（RCC_APB1Periph_DAC + DAC_Init），或再让我加个 sys_dac。 */
#define SYS_DAC_OUT_PORT        GPIOA
#define SYS_DAC_OUT_PIN         GPIO_Pin_4


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化（单次转换模式 ADC_ContinuousConvMode = DISABLE）：时钟 / 引脚(模拟输入) / 参数 / 校准
 *
 * 标准库调用链（库内部依次调用，可对照学习）:
 *   ① RCC_APB2PeriphClockCmd     开 ADC1/2/3 时钟（均挂 APB2）
 *   ② GPIO_Init                  引脚配为模拟输入(AIN)
 *   ③ ADC_CommonInit             公共分频（ADCCLK 不超 21MHz）
 *   ④ ADC_StructInit + ADC_Init  分辨率 12 位（ADC_Resolution_12b）/ 软件触发（ADC_ExternalTrigConvEdge_None）
 *   ⑤ ADC_RegularChannelConfig   规则通道 + 采样时间
 *   ⑥ ADC_Cmd + 校准             使能;校准为寄存器直写（CR2 的 ADC_CR2_CAL 位）
 *
 * 参数 : adc —— ADC 编号，三选一: ADC1 / ADC2 / ADC3
 *        channel —— 通道号，取值 ADC_Channel_0 ~ ADC_Channel_18
 *                    （标准库宏，与引脚绑定见数据手册;如 PF7=ADC_Channel_5）
 *        port/pin —— 对应引脚（换引脚时此处与 channel 必须匹配！）
 * 说明 : 可对同一 ADC 初始化多个通道，之后用 SYS_ADC_Read 选通道读取
 * 示例 : SYS_ADC_Init(SYS_ADC_LIGHT_ADC, SYS_ADC_LIGHT_CH,      // 板载光敏
 *                     SYS_ADC_LIGHT_PORT, SYS_ADC_LIGHT_PIN);   // PF7=ADC3_IN5
 *        SYS_ADC_Init(ADC1, ADC_Channel_0, GPIOA, GPIO_Pin_0);  // 自接电位器 */
void SYS_ADC_Init(ADC_TypeDef *adc, uint8_t channel,
                  GPIO_TypeDef *port, uint16_t pin);

/* 单次转换：启动 → 等转换完成 → 返回 12 位读数（0~4095）
 * 说明 : 阻塞约几十 µs；完成后 ADC_FLAG_EOC 标志由硬件自动清
 * 标准库 : ADC_RegularChannelConfig + ADC_SoftwareStartConv +
 *          ADC_GetFlagStatus(ADC_FLAG_EOC) + ADC_GetConversionValue
 * 示例 : uint16_t raw = SYS_ADC_Read(ADC3, ADC_Channel_5);
 *        uint32_t mv  = SYS_ADC_ToMilliVolt(raw);      // 读数 → 毫伏 */
uint16_t SYS_ADC_Read(ADC_TypeDef *adc, uint8_t channel);

/* 连续采样 times 次求平均（降低随机噪声）
 * 参数 : times —— 次数（0 = 使用默认 SYS_ADC_AVG_TIMES 次）
 * 示例 : uint16_t v = SYS_ADC_ReadAvg(ADC3, ADC_Channel_5, 16);   // 平均 16 次 */
uint16_t SYS_ADC_ReadAvg(ADC_TypeDef *adc, uint8_t channel, uint16_t times);

/* 读数 → 毫伏电压（按区块 1 的 SYS_ADC_VREF_MV 换算）
 * 示例 : uint32_t mv = SYS_ADC_ToMilliVolt(raw);   // raw=2048 → 约 1650mV */
uint32_t SYS_ADC_ToMilliVolt(uint16_t raw);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* ---- 连续转换（后台自由跑，随时读最新值） ---- */

/* 连续转换初始化并启动（ADC_ContinuousConvMode = ENABLE）：转换完一个立刻自动开始下一个
 * 用途 : 求平均值、做信号观察（比每次软件触发快得多）
 * 示例 : SYS_ADC_ContInit(ADC3, ADC_Channel_5, GPIOF, GPIO_Pin_7);   // 参数同 Init */
void SYS_ADC_ContInit(ADC_TypeDef *adc, uint8_t channel,
                      GPIO_TypeDef *port, uint16_t pin);

/* 读连续转换的"最新结果"（读走数据寄存器，不停转换）
 * 示例 : uint16_t now = SYS_ADC_ContValue(ADC3);   // 随时取最新采样 */
uint16_t SYS_ADC_ContValue(ADC_TypeDef *adc);

/* 停止连续转换（关 ADC 使能位）
 * 示例 : SYS_ADC_ContStop(ADC3); */
void SYS_ADC_ContStop(ADC_TypeDef *adc);

/* ---- DMA 采集（数据自动搬运进内存，CPU 完全不参与） ---- */

/* 通道描述（多通道扫描用）：通道号 + 对应引脚 */
typedef struct {
    uint8_t       channel;      /* ADC_Channel_x */
    GPIO_TypeDef *port;         /* 引脚端口 */
    uint16_t      pin;          /* 引脚掩码 */
} SysAdcCh_t;

/* 单通道 DMA 采集：转换结果自动写入 buf（循环模式 DMA_Mode_Circular，一直刷新）
 * 参数 : buf —— 数据缓冲；len —— 缓冲长度（uint16 个数）
 * 用法 : 调用后 buf[] 会被持续刷新，读 buf[任意下标] 即最新采样
 * 标准库 : ADC_DMACmd（打开 ADC→DMA 请求）+ 经 sys_dma → DMA_Init 系列
 *          （ADC 参数部分与 SYS_ADC_Init 同一套调用链）*/
void SYS_ADC_DmaInit(ADC_TypeDef *adc, uint8_t channel,
                     GPIO_TypeDef *port, uint16_t pin,
                     uint16_t *buf, uint16_t len);

/* 多通道扫描 + DMA 采集（教学重点：扫描序列）
 * 参数 : chs   —— 通道描述数组（每项含 channel/port/pin）
 *        count —— 通道个数（≤16，按数组顺序轮流转换）
 *        buf   —— 数据缓冲；len —— 缓冲长度
 * 说明 : 循环模式（DMA_Mode_Circular）下 buf[0..count-1] 依次为
 *        通道0..通道count-1 的结果，按 count 个一组滚动刷新
 * 示例 : static const SysAdcCh_t chs[2] = {
 *            { ADC_Channel_5, GPIOF, GPIO_Pin_7 },    // 光敏
 *            { ADC_Channel_4, GPIOF, GPIO_Pin_6 } };  // 按实际接线填
 *        uint16_t adcbuf[2];
 *        SYS_ADC_DmaScanInit(ADC3, chs, 2, adcbuf, 2); */
void SYS_ADC_DmaScanInit(ADC_TypeDef *adc, const SysAdcCh_t *chs,
                         uint8_t count, uint16_t *buf, uint16_t len);

/* 停止 DMA 采集（单通道/多通道通用）
 * 示例 : SYS_ADC_DmaStop(ADC3); */
void SYS_ADC_DmaStop(ADC_TypeDef *adc);

/* ---- 定时器触发采样（精确采样率 / 波形采集 / FFT 前级） ---- */
/* 用定时器 TRGO 硬件触发 ADC：采样间隔由硬件决定，**不受主循环与中断影响**
 *
 * 为什么必须用硬件触发：软件循环触发的间隔会被中断、任务切换打乱，
 *   采样率不稳 → 波形横轴（时间轴）就是歪的，做 FFT 更是不成立。
 *
 * 数据流：定时器更新事件 → TRGO → ADC 启动一轮扫描(count 个通道)
 *         → 每转换完一个通道自动 DMA 搬进 buf → 循环覆盖
 *
 * 参数 : adc       —— ADC1 / ADC2 / ADC3
 *        chs/count —— 通道描述数组（同 DmaScanInit 的 SysAdcCh_t）
 *        buf/len   —— DMA 目标缓冲（uint16_t 数组；建议 len 取 count 的整数倍）
 *        tim       —— 触发定时器，**只支持 SYS_TIM_2 / SYS_TIM_3 / SYS_TIM_8**
 *                     （F4 的 ADC 外部触发源里只有 T2_TRGO/T3_TRGO/T8_TRGO，
 *                      TIM1/4/5 的 TRGO 不在 ADC 触发源列表里）
 *                     ⚠ 选中的定时器会被本模块完全占用，不能再拿去计 PWM 等
 *        sample_hz —— 采样率 Hz（**每个通道**的速率）
 * 返回 : 0 = 成功；1 = 参数非法；2 = 该定时器不支持作 ADC 触发
 *
 * 示例（双通道、每通道 10kHz、共 2000 个采样点）:
 *     static const SysAdcCh_t chs[] = {
 *         { ADC_Channel_5,  GPIOF, GPIO_Pin_7 },   // 光敏 PF7
 *         { ADC_Channel_13, GPIOC, GPIO_Pin_3 },   // 外部信号 PC3
 *     };
 *     static uint16_t wave[2000];
 *     SYS_ADC_DmaTimerTrigInit(ADC1, chs, 2, wave, 2000, SYS_TIM_2, 10000);
 *     // 之后 wave[] 被硬件持续刷新，直接拿去做显示/滤波/FFT */
uint8_t SYS_ADC_DmaTimerTrigInit(ADC_TypeDef *adc, const SysAdcCh_t *chs,
                                 uint8_t count, uint16_t *buf, uint16_t len,
                                 SysTimId_t tim, uint32_t sample_hz);

/* 停止"定时器触发"采集（停 ADC + DMA + 定时器） */
void SYS_ADC_DmaTimerTrigStop(ADC_TypeDef *adc, SysTimId_t tim);

/* 一站式读电压（mV）：单次转换 + 按 VREF 换算，一步到位
 * 等价：SYS_ADC_ToMilliVolt(SYS_ADC_Read(adc, channel))
 * 前提 : 对应通道已 SYS_ADC_Init（同 SYS_ADC_Read）
 * 示例 : uint32_t mv = SYS_ADC_ReadMilliVolt(ADC3, ADC_Channel_5); */
uint32_t SYS_ADC_ReadMilliVolt(ADC_TypeDef *adc, uint8_t channel);


/* ================================================================
 *  附:标准库结构体速查 —— ADC_TypeDef（stm32f4xx.h;每个 ADC 一套）
 * ================================================================
 *  成员一览（含库中用法）:
 *    SR      状态:ADC_FLAG_EOC = 转换完成（SYS_ADC_Read 轮询它）
 *    CR1     控制 1:分辨率（ADC_Resolution_xb）/ 扫描模式（ADC_ScanConvMode;ADC_Init 写它）
 *    CR2     控制 2:ADC_CR2_ADON 使能 / _SWSTART 软件启动 / _CONT 连续 /
 *            _CAL 校准 / _DMA 使能（ADC_Cmd、SoftwareStartConv、
 *            寄存器直写的校准都动它）
 *    SMPR1/2 采样时间:每通道 3 位（RegularChannelConfig 的采样时间）
 *    JOFR1~4 注入通道偏移:未用
 *    HTR/LTR 模拟看门狗上下限:未用
 *    SQR1~3  规则序列:哪个通道在第几位转换（选通道写这里）
 *    JSQR    注入序列:未用
 *    JDR1~4  注入数据:未用
 *    DR      规则数据:转换结果的 12 位（GetConversionValue 读它）
 *
 *  附:标准库结构体速查 —— ADC_Common_TypeDef（三个 ADC 共用一份）
 *    CSR    公共状态:多 ADC 模式标志,单 ADC 少用
 *    CCR    公共控制:ADC 时钟分频 ADC_Prescaler_Div4（84MHz÷4 = 21MHz,
 *           硬件上限 36MHz;ADC_CommonInit 写它）
 *    CDR    公共数据:多 ADC 同步采时合成,库未用
 * ================================================================ */

#endif /* __FWLIB_SYS_ADC_H */
