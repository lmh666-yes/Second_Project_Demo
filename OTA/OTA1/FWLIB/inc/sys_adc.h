#ifndef __FWLIB_SYS_ADC_H
#define __FWLIB_SYS_ADC_H

#include "stm32f4xx.h"
#include "sys_tim.h"    /* 定时器触发采样要用 SysTimId_t（见 DmaTimerTrigInit） */

/* ================================================================
 *  sys_adc.h — ADC 模数转换模块 头文件
 *  用法: SYS_ADC_Read 单次 / SYS_ADC_ContInit 连续 / SYS_ADC_DmaInit、DmaScanInit DMA 采集
 *  本板: 光敏分压 → PF7 = ADC3_IN5;DHT11 为单总线器件,不经 ADC
 *  12 位 0~4095;mV = 读数 × SYS_ADC_VREF_MV / 4095;引脚须配模拟输入(AIN)
 *  换算、采样时间与接线细节见 检测数据端设计.md
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 参考电压(mV):默认 VDDA = 3300;换板或实测 VREF 后改此处,SYS_ADC_ToMilliVolt 按它换算 */
#define SYS_ADC_VREF_MV         3300UL

/* 采样时间:越长越抗干扰、转换越慢。3Cycles 用于低阻抗源(运放输出),
 * 84Cycles 通用,480Cycles 默认用于光敏等高阻源 */
#define SYS_ADC_SAMPLE_TIME     ADC_SampleTime_480Cycles

/* SYS_ADC_ReadAvg 不传次数时的默认平均次数 */
#define SYS_ADC_AVG_TIMES       8

/* ---- 忙等上限(纯计数循环次数;按 168MHz 下 1 次循环约 3~6 个时钟估算) ----
 * ADC 时钟异常或未上电时无上限会永久挂死且不报错,故封顶:
 *   100000  ≈ 2~4ms(单次转换,正常仅几微秒)
 *   1000000 ≈ 20~40ms(校准,正常仅几十微秒) */
#define SYS_ADC_EOC_TIMEOUT     100000UL    /* EOC 上限 */
#define SYS_ADC_CAL_TIMEOUT     1000000UL   /* CAL 上限 */

/* SYS_ADC_Read 超时返回值;12 位读数上限 4095,0xFFFF 不与真实值冲突,由 SYS_ADC_ReadOK() 判定 */
#define SYS_ADC_RAW_INVALID     0xFFFFU

/* -------------------- 板载光敏（示例） -------------------- */
#define SYS_ADC_LIGHT_ADC       ADC3
#define SYS_ADC_LIGHT_CH        ADC_Channel_5      /* PF7 = ADC3_IN5 */
#define SYS_ADC_LIGHT_PORT      GPIOF
#define SYS_ADC_LIGHT_PIN       GPIO_Pin_7


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化(ADC_ContinuousConvMode = DISABLE):时钟/引脚(模拟输入)/参数/校准
 * 调用链: RCC_APB2PeriphClockCmd(APB2) → GPIO_Init(AIN) → ADC_CommonInit(ADCCLK ≤ 21MHz)
 *         → ADC_StructInit+ADC_Init(12 位/软件触发) → ADC_RegularChannelConfig → ADC_Cmd + 校准(寄存器直写 CR2.CAL)
 * 参数: adc — ADC1/ADC2/ADC3;channel — ADC_Channel_0 ~ ADC_Channel_18(与引脚对应见数据手册,PF7=ADC_Channel_5)
 *       port/pin — 对应引脚,须与 channel 匹配
 * 说明: 同一 ADC 可初始化多个通道,由 SYS_ADC_Read 选通道读取 */
void SYS_ADC_Init(ADC_TypeDef *adc, uint8_t channel,
                  GPIO_TypeDef *port, uint16_t pin);

/* 单次转换:启动 → 等 EOC → 返回 12 位读数(0~4095);阻塞约几十 µs,EOC 由硬件自动清
 * 库调用: ADC_RegularChannelConfig + ADC_SoftwareStartConv + ADC_GetFlagStatus(EOC) + ADC_GetConversionValue
 * 返回: 0~4095;等 EOC 超时(上限 SYS_ADC_EOC_TIMEOUT)返回 SYS_ADC_RAW_INVALID(0xFFFF)
 * 约束: 与 DMA 采集互斥 — 会重写规则序列(SQR)并读 DR,该 ADC 处于 DMA/扫描/连续采集态时
 *       直接返回 SYS_ADC_RAW_INVALID 并计入 SYS_ADC_ConflictCount;
 *       ReadOK() = 0 的超时与冲突两种原因由 SYS_ADC_DmaBusy() 区分;单次读前先 SYS_ADC_DmaStop() */
uint16_t SYS_ADC_Read(ADC_TypeDef *adc, uint8_t channel);

/* 判定 SYS_ADC_Read 返回值是否有效:1 = 有效,0 = 无效(超时或与 DMA 采集冲突) */
uint8_t SYS_ADC_ReadOK(uint16_t raw);

/* 连续采样 times 次求平均(降低随机噪声)
 * 参数: times — 次数,0 = 使用默认 SYS_ADC_AVG_TIMES 次
 * 返回: 任一次失败(超时或与 DMA 采集冲突)整组作废,返回 SYS_ADC_RAW_INVALID,不以 0xFFFF 参与平均 */
uint16_t SYS_ADC_ReadAvg(ADC_TypeDef *adc, uint8_t channel, uint16_t times);

/* 读数 → 毫伏电压,mV = raw × SYS_ADC_VREF_MV / 4095(SYS_ADC_VREF_MV 见区块 1) */
uint32_t SYS_ADC_ToMilliVolt(uint16_t raw);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* ---- 连续转换（后台自由跑，随时读最新值） ---- */

/* 连续转换(ADC_ContinuousConvMode = ENABLE)初始化并启动:转换完一个立即自动开始下一个
 * 参数: 同 SYS_ADC_Init */
void SYS_ADC_ContInit(ADC_TypeDef *adc, uint8_t channel,
                      GPIO_TypeDef *port, uint16_t pin);

/* 读连续转换的最新结果(读 DR,不停转换)
 * 约束: 与 DMA 采集互斥 — 读 DR 会清 EOC 并取走一个样本,DMA 采集期间返回 SYS_ADC_RAW_INVALID;
 *       需要连续值请直接读 DMA 缓冲 */
uint16_t SYS_ADC_ContValue(ADC_TypeDef *adc);

/* 停止连续转换(关 ADC 使能位) */
void SYS_ADC_ContStop(ADC_TypeDef *adc);

/* ---- DMA 采集（数据自动搬运进内存，CPU 完全不参与） ---- */

/* 通道描述（多通道扫描用）：通道号 + 对应引脚 */
typedef struct {
    uint8_t       channel;      /* ADC_Channel_x */
    GPIO_TypeDef *port;         /* 引脚端口 */
    uint16_t      pin;          /* 引脚掩码 */
} SysAdcCh_t;

/* 单通道 DMA 采集:转换结果自动写入 buf(循环模式 DMA_Mode_Circular,持续刷新)
 * 参数: buf — 数据缓冲;len — 缓冲长度(uint16 个数)
 * 库调用: ADC_DMACmd(ADC→DMA 请求) + 经 sys_dma 的 DMA_Init 系列;ADC 参数部分同 SYS_ADC_Init
 * 用法: 读 buf[任意下标] 即最新采样 */
void SYS_ADC_DmaInit(ADC_TypeDef *adc, uint8_t channel,
                     GPIO_TypeDef *port, uint16_t pin,
                     volatile uint16_t *buf, uint16_t len);

/* 多通道扫描 + DMA 采集(扫描序列)
 * 参数: chs — 通道描述数组(含 channel/port/pin);count — 通道个数(≤16,按数组顺序轮流转换)
 *       buf — 数据缓冲;len — 缓冲长度
 * 说明: DMA_Mode_Circular 下 buf[0..count-1] 依次为通道 0..count-1 的结果,按 count 个一组滚动刷新 */
void SYS_ADC_DmaScanInit(ADC_TypeDef *adc, const SysAdcCh_t *chs,
                         uint8_t count, volatile uint16_t *buf, uint16_t len);

/* 停止 DMA 采集(单通道/多通道通用);同时注销运行态占用,SYS_ADC_Read/ContValue 随即恢复可用 */
void SYS_ADC_DmaStop(ADC_TypeDef *adc);

/* ---- 定时器触发采样（精确采样率 / 波形采集 / FFT 前级） ---- */
/* 定时器 TRGO 硬件触发 ADC:采样间隔由硬件决定,不受主循环与中断影响
 * 约束: 软件循环触发受中断与任务切换影响,采样率不稳会使波形时间轴失真,FFT 不成立
 * 数据流: 定时器更新事件 → TRGO → ADC 启动一轮扫描(count 个通道) → 每通道转换完由 DMA 搬入 buf → 循环覆盖
 * 参数: adc — ADC1/ADC2/ADC3
 *       chs/count — 通道描述数组(同 SYS_ADC_DmaScanInit 的 SysAdcCh_t)与通道个数
 *       buf/len — DMA 目标缓冲(uint16_t 数组;建议 len 取 count 的整数倍)
 *       tim — 触发定时器,仅 SYS_TIM_2 / SYS_TIM_3 / SYS_TIM_8
 *             (F4 的 ADC 外部触发源只有 T2_TRGO/T3_TRGO/T8_TRGO,TIM1/4/5 的 TRGO 不在列表)
 *             选中的定时器由本模块完全占用,不可再作 PWM 等用途
 *       sample_hz — 每个通道的采样率(Hz)
 * 返回: 0 = 成功;1 = 参数非法;2 = 该定时器不支持作 ADC 触发 */
uint8_t SYS_ADC_DmaTimerTrigInit(ADC_TypeDef *adc, const SysAdcCh_t *chs,
                                 uint8_t count, volatile uint16_t *buf, uint16_t len,
                                 SysTimId_t tim, uint32_t sample_hz);

/* 停止"定时器触发"采集（停 ADC + DMA + 定时器） */
void SYS_ADC_DmaTimerTrigStop(ADC_TypeDef *adc, SysTimId_t tim);

/* 定时器触发 + 扫描 + DMA:固定采样率的自动数据流
 * 原理: 触发源定时器(TIM2/3/8 的 TRGO,先用 SYS_TIM_TrgoInit 配置)每周期产生一个事件 →
 *       ADC 自动启动一轮扫描 → DMA 按序把结果搬进缓冲,采样时刻由硬件对齐
 * 参数: ext_trig — 外部触发源常量(标准库宏),与 SYS_TIM_TrgoInit 的定时器配对选:
 *         TIM2 → ADC_ExternalTrigConv_T2_TRGO;TIM3 → ADC_ExternalTrigConv_T3_TRGO;
 *         TIM8 → ADC_ExternalTrigConv_T8_TRGO。其他触发点(T1/T2/T3/T5/T8 的 CCx 等)见
 *         stm32f4xx_adc.h 的 ADC_ExternalTrigConv_ 宏;选错源则不触发,现象为缓冲不刷新
 *       chs/count/buf/len — 同 SYS_ADC_DmaScanInit(通道表/个数/缓冲/长度)
 * 说明: 调用后无需软件启动转换,触发事件到达即采样;停止用 SYS_TIM_Stop(触发源) 或 SYS_ADC_DmaStop */
void SYS_ADC_ExtTrigScanInit(ADC_TypeDef *adc, uint32_t ext_trig,
                             const SysAdcCh_t *chs, uint8_t count,
                             volatile uint16_t *buf, uint16_t len);

/* 读电压(mV),等价于 SYS_ADC_ToMilliVolt(SYS_ADC_Read(adc, channel))
 * 前提: 对应通道已 SYS_ADC_Init
 * 返回: 底层 SYS_ADC_Read 失败(超时或与 DMA 冲突)时由 0xFFFF 换算得 52817mV;判据: 结果 > SYS_ADC_VREF_MV 即无效;
 *       需要原始值请用 SYS_ADC_Read + SYS_ADC_ReadOK */
uint32_t SYS_ADC_ReadMilliVolt(ADC_TypeDef *adc, uint8_t channel);


/* ================================================================
 *        区块 4：运行态查询（DMA 采集互斥机制）
 *  依据: §3 第 30 条 — 波形偶发跳变与单次读返回 0xFFFF 的真超时/DMA 抢占两种原因
 *  在返回值上相同,需以下接口区分
 * ================================================================ */

/* 该 ADC 当前是否处于 DMA 采集态:1 = 是(单次读会被拒),0 = 否 */
uint8_t  SYS_ADC_DmaBusy(ADC_TypeDef *adc);

/* 该 ADC 最近一次校准是否成功:1 = 成功,0 = 超时或尚未执行 Init
 * 用途: 读数整体偏移/线性差时先查此项 */
uint8_t  SYS_ADC_CalOK(ADC_TypeDef *adc);

/* 单次读被 DMA 采集拒绝的累计次数(含 ADC1/ADC3 争用同一条 DMA 流的拒绝)
 * 用途: 非 0 说明代码中存在 SQR/DR 争用 */
uint32_t SYS_ADC_ConflictCount(void);

/* 冲突计数清零 */
void     SYS_ADC_ConflictClear(void);


/* ================================================================
 *  附:标准库结构体速查
 *  ADC_TypeDef(stm32f4xx.h;每个 ADC 一套):
 *    SR      状态:EOC 位(ADC_FLAG_EOC) = 转换完成(SYS_ADC_Read 轮询)
 *    CR1     控制 1:分辨率/扫描模式开关(ADC_Init 写)
 *    CR2     控制 2:ADON(ADC_CR2_ADON)/SWSTART(ADC_CR2_SWSTART)/CONTINUOUS(ADC_CR2_CONT)/
 *            CAL 校准位(本 DFP 未定义 ADC_CR2_CAL,库内 .c 已 #ifndef 补定义)/DMA 使能(ADC_CR2_DMA)
 *    SMPR1/2 采样时间:每通道 3 位(RegularChannelConfig 写入)
 *    JOFR1~4 注入通道偏移;HTR/LTR 模拟看门狗上下限;JSQR 注入序列;JDR1~4 注入数据(本模块未用)
 *    SQR1~3  规则序列:通道转换次序
 *    DR      规则数据:转换结果的 12 位(GetConversionValue 读)
 *  ADC_Common_TypeDef(三个 ADC 共用一份):
 *    CSR     公共状态:多 ADC 模式标志(单 ADC 少用)
 *    CCR     公共控制:ADC 时钟分频(ADC_Prescaler_Div4 → 84MHz÷4 = 21MHz,硬件上限 36MHz;ADC_CommonInit 写)
 *    CDR     公共数据:多 ADC 同步采时合成(库未用)
 * ================================================================ */

#endif /* __FWLIB_SYS_ADC_H */
