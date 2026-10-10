#ifndef __FWLIB_SYS_ADC_H
#define __FWLIB_SYS_ADC_H

#include "stm32f4xx.h"
#include "sys_tim.h"    /* 定时器触发采样要用 SysTimId_t（见文件末尾的 DmaTimerTrigInit） */

/* ADC 模块：单次转换 / 连续转换 / DMA 采集（含多通道扫描） */

/* 板载资源（普中-天马 F407开发板原理图）：
 *   光敏电阻分压 PF7 = ADC3_IN5，宏 SYS_ADC_LIGHT_*
 *   PA5 = STM_ADC、PA4 = R_ADC（J8 排针，外部电位器/NTC）
 *   PF7 与 ext_io 的 EXT_LIGHT_* 是同一引脚，两者会互相改引脚配置，同一工程只用一种
 *   PA5 与 ext_io 的电容触摸是同一引脚，不能同时用
 *   板载 DS18B20 是单总线器件，不归 ADC 管 */

/* 分辨率 12 位：读数 0 ~ 4095，对应电压 0 ~ VREF
 * 换算：电压(mV) = 读数 × 3300 / 4095（SYS_ADC_ToMilliVolt）
 * F407 单次转换耗时 ≈ (采样周期 + 12) / 21MHz，480 周期采样约 23µs
 * 引脚必须配置为模拟输入(AIN)，此时施密特触发器关闭 */

/* 区块 1：定义与宏定义区（换板子只改这里） */
/* 参考电压（mV）：默认 VDDA = 3300；板上有 VREF 跳线或实测过参考电压时改这里 */
#define SYS_ADC_VREF_MV         3300UL

/* 采样时间：越长越抗干扰、转换越慢
 *   ADC_SampleTime_3Cycles   最快，低阻抗源（运放输出）
 *   ADC_SampleTime_84Cycles  通用
 *   ADC_SampleTime_480Cycles 默认，光敏等高阻源 */
#define SYS_ADC_SAMPLE_TIME     ADC_SampleTime_480Cycles

/* SYS_ADC_ReadAvg 不传次数时的默认平均次数 */
#define SYS_ADC_AVG_TIMES       8

/* 防死等上限（纯计数循环次数，按主频估算）
 * 不加上限时 ADC 时钟异常或未上电会让调用任务永久挂死，且不报错
 * 168MHz 下 1 次循环约 3~6 个时钟：
 *   100000  ≈ 2~4ms   单次转换，正常只需几微秒
 *   1000000 ≈ 20~40ms 校准，正常只需几十微秒 */
#define SYS_ADC_EOC_TIMEOUT     100000UL    /* 等"转换结束(EOC)"的上限 */
#define SYS_ADC_CAL_TIMEOUT     1000000UL   /* 等"校准完成(CAL)"的上限 */

/* SYS_ADC_Read 超时时的返回值：12 位 ADC 正常原始值只到 4095，0xFFFF 不会与真实读数相同 */
#define SYS_ADC_RAW_INVALID     0xFFFFU

/* -------------------- 板载光敏 -------------------- */
#define SYS_ADC_LIGHT_ADC       ADC3
#define SYS_ADC_LIGHT_CH        ADC_Channel_5      /* PF7 = ADC3_IN5 */
#define SYS_ADC_LIGHT_PORT      GPIOF
#define SYS_ADC_LIGHT_PIN       GPIO_Pin_7

/* J8 共享模拟输入（PA5 = 网络名 STM_ADC）
 * J8 是 5 脚跳线排（R_ADC / STM_ADC / P_TOUCH / STM_DAC / TAD1），跳线帽一次只能接一个：
 *   R_ADC   ↔ STM_ADC：板上电位器 AD1(10K) 分压
 *   P_TOUCH ↔ STM_ADC：电容触摸板，用充电时间法测量
 *   TAD1    ↔ STM_ADC：NTC/PT100 模块输出 */
#define SYS_ADC_JMP_ADC         ADC1
#define SYS_ADC_JMP_CH          ADC_Channel_5      /* PA5 = ADC12_IN5 */
#define SYS_ADC_JMP_PORT        GPIOA
#define SYS_ADC_JMP_PIN         GPIO_Pin_5

/* J8 上的 DAC 输出（PA4 = 网络名 STM_DAC）
 * PA4 = DAC_OUT1，也是 ADC12_IN4；本库未封 DAC 模块，要用需自己开 RCC_APB1Periph_DAC + DAC_Init */
#define SYS_DAC_OUT_PORT        GPIOA
#define SYS_DAC_OUT_PIN         GPIO_Pin_4


/* 区块 2：基础功能 */
/* 初始化：单次转换模式（ADC_ContinuousConvMode = DISABLE），时钟 / 引脚(模拟输入) / 参数 / 校准
 *
 * 参数 : adc: ADC 编号，三选一: ADC1 / ADC2 / ADC3
 *        channel: 通道号，取值 ADC_Channel_0 ~ ADC_Channel_18
 *                    （标准库宏，与引脚绑定见数据手册;如 PF7=ADC_Channel_5）
 *        port/pin: 对应引脚（换引脚时此处与 channel 必须匹配）
 * 说明 : 可对同一 ADC 初始化多个通道，之后用 SYS_ADC_Read 选通道读取
 *        时间基准 ADCCLK 不超 21MHz（ADC_CommonInit 公共分频） */
void SYS_ADC_Init(ADC_TypeDef *adc, uint8_t channel,
                  GPIO_TypeDef *port, uint16_t pin);

/* 单次转换：启动 → 等转换完成 → 返回 12 位读数（0~4095）
 * 说明 : 阻塞约几十 µs；EOC 标志由硬件自动清
 * 返回 : 12 位原始值 0~4095；等待 EOC 超时返回 SYS_ADC_RAW_INVALID(0xFFFF)
 * 约束 : 与 DMA 采集互斥。本函数会重写规则序列(SQR)并抢读 DR，该 ADC 处于
 *        DMA/扫描/连续采集态时直接拒绝并返回 SYS_ADC_RAW_INVALID，同时累加计数
 *        （见 SYS_ADC_ConflictCount）。ReadOK() 返回 0 有超时和冲突两种原因，
 *        用 SYS_ADC_DmaBusy() 区分；要单次读先 SYS_ADC_DmaStop() */
uint16_t SYS_ADC_Read(ADC_TypeDef *adc, uint8_t channel);

/* 判定 SYS_ADC_Read 的返回值是否有效：1 = 有效，0 = 超时 */
uint8_t SYS_ADC_ReadOK(uint16_t raw);

/* 连续采样 times 次求平均（降低随机噪声）
 * 参数 : times: 次数（0 = 使用默认 SYS_ADC_AVG_TIMES 次）
 * 返回 : 平均值；中途任何一次失败（超时或与 DMA 采集冲突）整组作废，返回
 *        SYS_ADC_RAW_INVALID，不用 0xFFFF 参与求平均 */
uint16_t SYS_ADC_ReadAvg(ADC_TypeDef *adc, uint8_t channel, uint16_t times);

/* 读数 → 毫伏电压（按区块 1 的 SYS_ADC_VREF_MV 换算） */
uint32_t SYS_ADC_ToMilliVolt(uint16_t raw);


/* 区块 3：扩展功能 */
/* ---- 连续转换（后台自由跑，随时读最新值） ---- */

/* 连续转换初始化并启动（ADC_ContinuousConvMode = ENABLE）：转换完一个立刻自动开始下一个
 * 参数同 SYS_ADC_Init；转换后台进行，供求平均值、信号观察使用 */
void SYS_ADC_ContInit(ADC_TypeDef *adc, uint8_t channel,
                      GPIO_TypeDef *port, uint16_t pin);

/* 读连续转换的最新结果：读走数据寄存器，不停转换
 * 约束 : 与 DMA 采集互斥。读 DR 会清 EOC，DMA 搬运期间读它等于取走一个样本，
 *        所以采集期间返回 SYS_ADC_RAW_INVALID；要连续值请直接读 DMA 缓冲 */
uint16_t SYS_ADC_ContValue(ADC_TypeDef *adc);

/* 停止连续转换（关 ADC 使能位） */
void SYS_ADC_ContStop(ADC_TypeDef *adc);

/* ---- DMA 采集（数据自动搬运进内存，CPU 不参与） ---- */

/* 通道描述（多通道扫描用）：通道号 + 对应引脚 */
typedef struct {
    uint8_t       channel;      /* ADC_Channel_x */
    GPIO_TypeDef *port;         /* 引脚端口 */
    uint16_t      pin;          /* 引脚掩码 */
} SysAdcCh_t;

/* 单通道 DMA 采集：转换结果自动写入 buf（循环模式 DMA_Mode_Circular，一直刷新）
 * 参数 : buf: 数据缓冲；len: 缓冲长度（uint16 个数）
 * 说明 : 调用后 buf[] 被持续刷新，读 buf[任意下标] 即最新采样 */
void SYS_ADC_DmaInit(ADC_TypeDef *adc, uint8_t channel,
                     GPIO_TypeDef *port, uint16_t pin,
                     volatile uint16_t *buf, uint16_t len);

/* 多通道扫描 + DMA 采集
 * 参数 : chs  : 通道描述数组（每项含 channel/port/pin）
 *        count: 通道个数（≤16，按数组顺序轮流转换）
 *        buf  : 数据缓冲；len: 缓冲长度
 * 说明 : 循环模式（DMA_Mode_Circular）下 buf[0..count-1] 依次为
 *        通道0..通道count-1 的结果，按 count 个一组滚动刷新 */
void SYS_ADC_DmaScanInit(ADC_TypeDef *adc, const SysAdcCh_t *chs,
                         uint8_t count, volatile uint16_t *buf, uint16_t len);

/* 停止 DMA 采集（单通道/多通道通用）
 * 说明 : 同时注销运行态占用，停完 SYS_ADC_Read/ContValue 立刻恢复可用 */
void SYS_ADC_DmaStop(ADC_TypeDef *adc);

/* ---- 定时器触发采样（精确采样率 / 波形采集 / FFT 前级） ---- */
/* 用定时器 TRGO 硬件触发 ADC：采样间隔由硬件决定，不受主循环与中断影响
 * 软件循环触发会被中断和任务切换打乱，采样率不稳，时间轴不准，做 FFT 不成立
 * 数据流：定时器更新事件 → TRGO → ADC 启动一轮扫描(count 个通道)
 *         → 每转换完一个通道自动 DMA 搬进 buf → 循环覆盖
 *
 * 参数 : adc      : ADC1 / ADC2 / ADC3
 *        chs/count: 通道描述数组（同 DmaScanInit 的 SysAdcCh_t）
 *        buf/len  : DMA 目标缓冲（uint16_t 数组；建议 len 取 count 的整数倍）
 *        tim      : 触发定时器，只支持 SYS_TIM_2 / SYS_TIM_3 / SYS_TIM_8
 *                     （F4 的 ADC 外部触发源里只有 T2_TRGO/T3_TRGO/T8_TRGO，
 *                      TIM1/4/5 的 TRGO 不在 ADC 触发源列表里）
 *                     选中的定时器被本模块完全占用，不能再用于 PWM 等
 *        sample_hz: 采样率 Hz（每个通道的速率）
 * 返回 : 0 = 成功；1 = 参数非法；2 = 该定时器不支持作 ADC 触发 */
uint8_t SYS_ADC_DmaTimerTrigInit(ADC_TypeDef *adc, const SysAdcCh_t *chs,
                                 uint8_t count, volatile uint16_t *buf, uint16_t len,
                                 SysTimId_t tim, uint32_t sample_hz);

/* 停止定时器触发采集（停 ADC + DMA + 定时器） */
void SYS_ADC_DmaTimerTrigStop(ADC_TypeDef *adc, SysTimId_t tim);

/* 定时器触发 + 扫描 + DMA：固定采样率的自动数据流
 * 原理 : 触发源定时器（TIM2/3/8 的 TRGO，先用 SYS_TIM_TrgoInit 配好）每周期
 *        产生一个事件，ADC 自动启动一轮扫描，DMA 按序把结果搬进缓冲
 * 参数 : ext_trig: 外部触发源常量（标准库宏），与 SYS_TIM_TrgoInit 的定时器配对：
 *          TIM2 → ADC_ExternalTrigConv_T2_TRGO
 *          TIM3 → ADC_ExternalTrigConv_T3_TRGO
 *          TIM8 → ADC_ExternalTrigConv_T8_TRGO
 *        另支持 T1/T2/T3/T5/T8 的 CCx 等触发点，完整表见 stm32f4xx_adc.h 的
 *        ADC_ExternalTrigConv_ 宏；选错源永远不触发，缓冲不刷新
 *        chs/count/buf/len: 同 SYS_ADC_DmaScanInit（通道表/个数/缓冲/长度）
 * 说明 : 调用后不需要启动转换，触发事件一到就自动采样；
 *        停止用 SYS_TIM_Stop(触发源) 或 SYS_ADC_DmaStop */
void SYS_ADC_ExtTrigScanInit(ADC_TypeDef *adc, uint32_t ext_trig,
                             const SysAdcCh_t *chs, uint8_t count,
                             volatile uint16_t *buf, uint16_t len);

/* 一站式读电压（mV）：单次转换 + 按 VREF 换算
 * 等价于 SYS_ADC_ToMilliVolt(SYS_ADC_Read(adc, channel))
 * 前提 : 对应通道已 SYS_ADC_Init
 * 返回 : 底层 SYS_ADC_Read 失败时（超时或与 DMA 采集冲突）原始值为 0xFFFF，
 *        换算结果 52817mV 超出量程；判据是结果 > SYS_ADC_VREF_MV 即无效。
 *        要判原始值就用 SYS_ADC_Read + SYS_ADC_ReadOK */
uint32_t SYS_ADC_ReadMilliVolt(ADC_TypeDef *adc, uint8_t channel);


/*
 *        区块 4：运行态查询（配合"与 DMA 采集互斥"机制）
 *  单次读返回 0xFFFF 有真超时和与 DMA 抢占两种原因，返回值上无法区分，
 *  这几个接口用于区分 */
/* 该 ADC 当前是否处于 DMA 采集态：1 = 是（单次读会被拒），0 = 否 */
uint8_t  SYS_ADC_DmaBusy(ADC_TypeDef *adc);

/* 该 ADC 最近一次校准是否成功：1 = 成功，0 = 超时或还没跑过 Init
 * 读数整体偏一点或线性差时先查它 */
uint8_t  SYS_ADC_CalOK(ADC_TypeDef *adc);

/* "单次读被 DMA 采集拒绝"的累计次数（含 ADC1/ADC3 抢同一条 DMA 流的拒绝）
 * 跑一轮后看它是否非 0，非 0 说明代码里有 SQR/DR 争用 */
uint32_t SYS_ADC_ConflictCount(void);

/* 把上面那个计数清零（重新统计一段时间的冲突用） */
void     SYS_ADC_ConflictClear(void);


/*
 *  附:标准库结构体速查: ADC_TypeDef（stm32f4xx.h;每个 ADC 一套）
 *    SR      状态:ADC_FLAG_EOC = 转换完成（SYS_ADC_Read 轮询它）
 *    CR1     控制 1:分辨率（ADC_Resolution_xb）/ 扫描模式（ADC_ScanConvMode;ADC_Init 写它）
 *    CR2     控制 2:ADC_CR2_ADON 使能 / _SWSTART 软件启动 / _CONT 连续 /
 *            _CAL 校准（本 DFP 未定义 ADC_CR2_CAL,库内 .c 已 #ifndef 补定义）/
 *            _DMA 使能（ADC_Cmd、SoftwareStartConv、
 *            寄存器直写的校准都动它）
 *    SMPR1/2 采样时间:每通道 3 位（RegularChannelConfig 的采样时间）
 *    JOFR1~4 注入通道偏移:未用
 *    HTR/LTR 模拟看门狗上下限:未用
 *    SQR1~3  规则序列:哪个通道在第几位转换（选通道写这里）
 *    JSQR    注入序列:未用
 *    JDR1~4  注入数据:未用
 *    DR      规则数据:转换结果的 12 位（GetConversionValue 读它）
 *
 *  附:标准库结构体速查: ADC_Common_TypeDef（三个 ADC 共用一份）
 *    CSR    公共状态:多 ADC 模式标志,单 ADC 少用
 *    CCR    公共控制:ADC 时钟分频 ADC_Prescaler_Div4（84MHz÷4 = 21MHz,
 *           硬件上限 36MHz;ADC_CommonInit 写它）
 *    CDR    公共数据:多 ADC 同步采时合成,库未用
 */

#endif /* __FWLIB_SYS_ADC_H */
