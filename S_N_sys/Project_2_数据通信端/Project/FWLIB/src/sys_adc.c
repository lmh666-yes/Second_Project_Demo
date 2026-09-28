#include "sys_adc.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "sys_dma.h"

/* 本工程的 DFP 器件头文件未定义"校准启动位"宏（F4 的 CR2.CAL，bit2），
 * 这里等价补一个；若将来头文件补全了该宏则以下定义自动让位 */
#ifndef ADC_CR2_CAL
#define ADC_CR2_CAL   ((uint32_t)0x00000004)
#endif

/* ================================================================
 *  sys_adc.c —— 【系统】ADC 模数转换模块  实现文件
 * ================================================================
 *  实现要点 :
 *    ① 三种模式只是"初始化参数不同"：单次(不连续)、连续、DMA
 *       —— 上层看到的差别就是"读一次 / 读最新 / 读缓冲"；
 *    ② 每次初始化都做一遍"稳压器 + 校准"，读数更准（ST 官方
 *       例程的标准流程）；
 *    ③ DMA 部分复用 sys_dma 模块（外设→内存、DMA_Mode_Circular），
 *       数据搬运全程不占 CPU；
 *    ④ 同一个 ADC 的 DMA 数据流固定（如 ADC1/ADC3 → DMA2_Stream0），
 *       两路 ADC 不会同时占用——详见 README 的冲突表。
 * ================================================================ */


/* ================================================================
 *                    内部辅助
 * ================================================================ */
/* 打开 ADC 所在 APB2 总线的时钟 */
static void adc_clk_enable(ADC_TypeDef *adc)
{
    if      (adc == ADC1) RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    else if (adc == ADC2) RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC2, ENABLE);
    else if (adc == ADC3) RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC3, ENABLE);
}

/* 引脚 → 模拟输入(AIN)：施密特触发器关闭，输入阻抗最高 */
static void adc_pin_analog(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gi;

    if (port == 0) return;

    GPIO_ClockEnable(port);
    gi.GPIO_Pin   = pin;
    gi.GPIO_Mode  = GPIO_Mode_AN;
    gi.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(port, &gi);
}

/* ADC 公共部分：时钟分频 / 工作模式
 * 分频 4 → 84MHz/4 = 21MHz，低于 36MHz 上限（改主频后重新 Init 即可） */
static void adc_common_config(void)
{
    ADC_CommonInitTypeDef ci;

    ADC_CommonStructInit(&ci);
    ci.ADC_Mode             = ADC_Mode_Independent;      /* 每个 ADC 独立工作 */
    ci.ADC_Prescaler        = ADC_Prescaler_Div4;
    ci.ADC_DMAAccessMode    = ADC_DMAAccessMode_Disabled;/* 多 ADC 模式才用 */
    ci.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;
    ADC_CommonInit(&ci);
}

/* 稳压器 + 单次校准：显著减小零点/增益误差
 * 说明 : 上电或外围参数变动后做一次即可；本模块在每次 Init 里都做，
 *        代价只有几十微秒，换来读数更准
 * 实现 : 寄存器直写（本 DFP 包的 SPL 未提供校准 API）——
 *        ADON=1 给内部稳压器上电，等待稳定后再置 CAL 启动校准，
 *        硬件校准完成后自动把 CAL 清 0 */
static void adc_calibrate(ADC_TypeDef *adc)
{
    volatile uint32_t wait = 100000UL;

    adc->CR2 |= ADC_CR2_ADON;                /* 上电（若已开也无妨） */
    while (wait-- != 0U);                    /* 等稳压器稳定 tSTAB */

    adc->CR2 |= ADC_CR2_CAL;                 /* 启动校准 */
    while ((adc->CR2 & ADC_CR2_CAL) != 0U);  /* 等校准完成 */
}

/* 填 ADC 初始化结构体（单次/连续/扫描 由参数决定） */
static void adc_config_write(ADC_TypeDef *adc, uint8_t channel,
                             uint8_t scan, uint8_t continuous)
{
    ADC_InitTypeDef ai;

    ADC_StructInit(&ai);
    ai.ADC_Resolution           = ADC_Resolution_12b;    /* 12 位：0~4095 */
    ai.ADC_ScanConvMode         = scan ? ENABLE : DISABLE;
    ai.ADC_ContinuousConvMode   = continuous ? ENABLE : DISABLE;
    ai.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None; /* 软件触发 */
    ai.ADC_ExternalTrigConv     = ADC_ExternalTrigConv_T1_CC1;   /* 未用 */
    ai.ADC_DataAlign            = ADC_DataAlign_Right;
    ai.ADC_NbrOfConversion      = 1;
    ADC_Init(adc, &ai);

    if (scan == 0U) {
        ADC_RegularChannelConfig(adc, channel, 1, SYS_ADC_SAMPLE_TIME);
    }
}

/* ADC → DMA 数据流/通道映射（芯片数据手册固定映射） */
static DMA_Stream_TypeDef *adc_dma_stream(ADC_TypeDef *adc)
{
    if (adc == ADC1 || adc == ADC3) return DMA2_Stream0;   /* 同流，二选一 */
    if (adc == ADC2)                return DMA2_Stream2;
    return 0;
}

static uint32_t adc_dma_channel(ADC_TypeDef *adc)
{
    if (adc == ADC1) return DMA_Channel_0;
    if (adc == ADC2) return DMA_Channel_1;
    if (adc == ADC3) return DMA_Channel_2;
    return 0;
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
void SYS_ADC_Init(ADC_TypeDef *adc, uint8_t channel,
                  GPIO_TypeDef *port, uint16_t pin)
{
    if (adc == 0 || port == 0) return;

    adc_clk_enable(adc);
    adc_pin_analog(port, pin);

    ADC_Cmd(adc, DISABLE);
    adc_common_config();                                 /* 公共部分(分频等)统一重配 */

    adc_config_write(adc, channel, 0, 0);                /* 单次模式 */
    ADC_Cmd(adc, ENABLE);
    adc_calibrate(adc);
}

uint16_t SYS_ADC_Read(ADC_TypeDef *adc, uint8_t channel)
{
    if (adc == 0) return 0;

    /* 每次读都重设一下通道 → 同一个 ADC 可以轮流读多个通道 */
    ADC_RegularChannelConfig(adc, channel, 1, SYS_ADC_SAMPLE_TIME);

    ADC_SoftwareStartConv(adc);
    while (ADC_GetFlagStatus(adc, ADC_FLAG_EOC) == RESET);   /* 等转换完成 */

    return ADC_GetConversionValue(adc);                  /* 读 DR 顺带清 EOC */
}

uint16_t SYS_ADC_ReadAvg(ADC_TypeDef *adc, uint8_t channel, uint16_t times)
{
    uint32_t sum = 0;
    uint16_t i;

    if (times == 0U) times = SYS_ADC_AVG_TIMES;

    for (i = 0; i < times; i++) {
        sum += SYS_ADC_Read(adc, channel);
    }
    return (uint16_t)(sum / times);
}

uint32_t SYS_ADC_ToMilliVolt(uint16_t raw)
{
    return ((uint32_t)raw * SYS_ADC_VREF_MV) / 4095UL;
}


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
void SYS_ADC_ContInit(ADC_TypeDef *adc, uint8_t channel,
                      GPIO_TypeDef *port, uint16_t pin)
{
    if (adc == 0 || port == 0) return;

    adc_clk_enable(adc);
    adc_pin_analog(port, pin);

    ADC_Cmd(adc, DISABLE);
    adc_common_config();

    adc_config_write(adc, channel, 0, 1);                /* 连续模式 */
    ADC_Cmd(adc, ENABLE);
    adc_calibrate(adc);

    ADC_SoftwareStartConv(adc);      /* 启动一发，之后硬件自动连续转换 */
}

uint16_t SYS_ADC_ContValue(ADC_TypeDef *adc)
{
    if (adc == 0) return 0;
    return ADC_GetConversionValue(adc);                  /* 读最新结果 */
}

void SYS_ADC_ContStop(ADC_TypeDef *adc)
{
    if (adc == 0) return;
    ADC_Cmd(adc, DISABLE);                               /* 关使能即停 */
}

/* 一站式读电压（mV）：单次转换 + 换算，一步到位
 * 前提 : 对应通道已 SYS_ADC_Init（同 SYS_ADC_Read） */
uint32_t SYS_ADC_ReadMilliVolt(ADC_TypeDef *adc, uint8_t channel)
{
    return SYS_ADC_ToMilliVolt(SYS_ADC_Read(adc, channel));
}

/* 单通道 DMA 采集：连续转换 + DMA 循环搬运 */
void SYS_ADC_DmaInit(ADC_TypeDef *adc, uint8_t channel,
                     GPIO_TypeDef *port, uint16_t pin,
                     uint16_t *buf, uint16_t len)
{
    DMA_Stream_TypeDef *stream;

    if (adc == 0 || port == 0 || buf == 0 || len == 0U) return;
    stream = adc_dma_stream(adc);
    if (stream == 0) return;

    adc_clk_enable(adc);
    adc_pin_analog(port, pin);

    /* 先停旧配置（DMA + ADC），保证重配干净 */
    SYS_DMA_Stop(stream);
    ADC_Cmd(adc, DISABLE);
    adc_common_config();

    adc_config_write(adc, channel, 0, 1);                /* 连续模式 */

    /* DMA：外设 = ADC 数据寄存器(半字)、循环模式（DMA_Mode_Circular）→ 缓冲一直滚动刷新 */
    SYS_DMA_PeriphToMem(stream, adc_dma_channel(adc),
                        (uint32_t)&adc->DR, buf, len, 2, 1);
    ADC_DMACmd(adc, ENABLE);                             /* 打开 ADC 的 DMA 请求 */

    ADC_Cmd(adc, ENABLE);
    adc_calibrate(adc);
    ADC_SoftwareStartConv(adc);      /* 启动，之后 CPU 无需再管 */
}

/* 多通道扫描 + DMA：按数组顺序扫描每个通道，结果依次进缓冲 */
void SYS_ADC_DmaScanInit(ADC_TypeDef *adc, const SysAdcCh_t *chs,
                         uint8_t count, uint16_t *buf, uint16_t len)
{
    DMA_Stream_TypeDef *stream;
    ADC_InitTypeDef ai;
    uint8_t i;

    if (adc == 0 || chs == 0 || buf == 0 || len == 0U) return;
    if (count == 0U || count > 16U) return;
    stream = adc_dma_stream(adc);
    if (stream == 0) return;

    adc_clk_enable(adc);
    for (i = 0; i < count; i++) {
        adc_pin_analog(chs[i].port, chs[i].pin);
    }

    SYS_DMA_Stop(stream);
    ADC_Cmd(adc, DISABLE);
    adc_common_config();

    /* 扫描 + 连续：一轮扫完 count 个通道，马上开始下一轮 */
    ADC_StructInit(&ai);
    ai.ADC_Resolution           = ADC_Resolution_12b;
    ai.ADC_ScanConvMode         = ENABLE;
    ai.ADC_ContinuousConvMode   = ENABLE;
    ai.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
    ai.ADC_ExternalTrigConv     = ADC_ExternalTrigConv_T1_CC1;
    ai.ADC_DataAlign            = ADC_DataAlign_Right;
    ai.ADC_NbrOfConversion      = count;
    ADC_Init(adc, &ai);

    /* 给每个通道排"转换顺序"(rank 从 1 开始) */
    for (i = 0; i < count; i++) {
        ADC_RegularChannelConfig(adc, chs[i].channel, (uint8_t)(i + 1U),
                                 SYS_ADC_SAMPLE_TIME);
    }

    /* DMA 循环搬运：每转换完一个通道自动写一个半字 */
    SYS_DMA_PeriphToMem(stream, adc_dma_channel(adc),
                        (uint32_t)&adc->DR, buf, len, 2, 1);
    ADC_DMACmd(adc, ENABLE);

    ADC_Cmd(adc, ENABLE);
    adc_calibrate(adc);
    ADC_SoftwareStartConv(adc);
}

void SYS_ADC_DmaStop(ADC_TypeDef *adc)
{
    DMA_Stream_TypeDef *stream;

    if (adc == 0) return;
    stream = adc_dma_stream(adc);
    if (stream == 0) return;

    ADC_Cmd(adc, DISABLE);           /* 先停转换（不然还在发 DMA 请求） */
    SYS_DMA_Stop(stream);
}


/* ================================================================
 *        扩展：定时器触发采样（精确采样率，项目5 波形采集）
 * ================================================================
 * 定时器 → TRGO → ADC 硬件触发，采样点间隔严格相等（软件触发做不到这点）。
 * 下面三张小表把"库的枚举编号"翻译成标准库的寄存器常量。
 * ================================================================ */
/* SysTimId_t → TIM_TypeDef*
 * ⚠ F4 标准库的 ADC 外部触发源里，只有 T2_TRGO / T3_TRGO / T8_TRGO 这三个 TRGO
 *   （TIM1/4/5 的 TRGO 没被列进 ADC 触发源，只列了它们的 CC 通道）——
 *   所以本函数只接受这三个定时器。 */
static TIM_TypeDef *adc_trig_tim(SysTimId_t id)
{
    switch (id) {
        case SYS_TIM_2: return TIM2;
        case SYS_TIM_3: return TIM3;
        case SYS_TIM_8: return TIM8;
        default:        return 0;
    }
}

/* SysTimId_t → 该定时器的时钟频率 Hz（APB1 定时器 84MHz，APB2 定时器 168MHz）
 * 说明 : 按本库 sys_clock 的 PLL 档（168MHz、APB1÷4、APB2÷2）与
 *        STM32F4 的“APB 分频≠1 时定时器时钟 = PCLK×2”规则算出 */
static uint32_t adc_trig_tim_clk(SysTimId_t id)
{
    switch (id) {
        case SYS_TIM_8:  return 168000000UL;   /* APB2：84MHz × 2 */
        case SYS_TIM_2:
        case SYS_TIM_3:  return  84000000UL;   /* APB1：42MHz × 2 */
        default:         return  84000000UL;
    }
}

/* SysTimId_t → ADC 的外部触发源常量 */
static uint32_t adc_trig_src(SysTimId_t id)
{
    switch (id) {
        case SYS_TIM_2: return ADC_ExternalTrigConv_T2_TRGO;
        case SYS_TIM_3: return ADC_ExternalTrigConv_T3_TRGO;
        case SYS_TIM_8: return ADC_ExternalTrigConv_T8_TRGO;
        default:        return 0;
    }
}

uint8_t SYS_ADC_DmaTimerTrigInit(ADC_TypeDef *adc, const SysAdcCh_t *chs,
                                 uint8_t count, uint16_t *buf, uint16_t len,
                                 SysTimId_t tim, uint32_t sample_hz)
{
    DMA_Stream_TypeDef *stream;
    ADC_InitTypeDef ai;
    TIM_TimeBaseInitTypeDef tb;
    TIM_TypeDef *t;
    uint32_t src;
    uint32_t tclk;
    uint32_t period;
    uint32_t psc;
    uint8_t  i;

    if (adc == 0 || chs == 0 || buf == 0 || len == 0U) return 1U;
    if (count == 0U || count > 16U)                    return 1U;
    if (sample_hz == 0U)                               return 1U;

    t   = adc_trig_tim(tim);
    src = adc_trig_src(tim);
    if (t == 0 || src == 0U) return 2U;                /* 该定时器不作为触发源 */

    stream = adc_dma_stream(adc);
    if (stream == 0) return 1U;

    /* ① 模拟引脚 */
    adc_clk_enable(adc);
    for (i = 0; i < count; i++) {
        adc_pin_analog(chs[i].port, chs[i].pin);
    }

    /* ② ADC：扫描 + **单次**（每次触发只扫一轮）+ 上升沿外部触发 */
    SYS_DMA_Stop(stream);
    ADC_Cmd(adc, DISABLE);
    adc_common_config();

    ADC_StructInit(&ai);
    ai.ADC_Resolution           = ADC_Resolution_12b;
    ai.ADC_ScanConvMode         = ENABLE;
    ai.ADC_ContinuousConvMode   = DISABLE;             /* 关键：由触发事件驱动 */
    ai.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_Rising;
    ai.ADC_ExternalTrigConv     = src;
    ai.ADC_DataAlign            = ADC_DataAlign_Right;
    ai.ADC_NbrOfConversion      = count;
    ADC_Init(adc, &ai);

    for (i = 0; i < count; i++) {
        ADC_RegularChannelConfig(adc, chs[i].channel, (uint8_t)(i + 1U),
                                 SYS_ADC_SAMPLE_TIME);
    }

    /* ③ DMA：循环模式，ADC 每出一个结果自动搬一个半字 */
    SYS_DMA_PeriphToMem(stream, adc_dma_channel(adc),
                        (uint32_t)&adc->DR, buf, len, 2, 1);
    ADC_DMACmd(adc, ENABLE);

    ADC_Cmd(adc, ENABLE);
    adc_calibrate(adc);

    /* ④ 定时器：计数到 sample_hz 就产生一次更新事件，并把更新事件送上 TRGO
     *    分频器先归一到 1MHz，再数 1000000/sample_hz 个数 —— 这样
     *    任意采样率都能精确得到（不用浮点） */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM8, ENABLE);

    tclk = adc_trig_tim_clk(tim);
    psc  = (tclk / 1000000UL) - 1UL;                   /* → 1MHz 计数 */

    if (sample_hz > 1000000UL) sample_hz = 1000000UL;  /* 上限保护 */
    period = (1000000UL / sample_hz) - 1UL;

    tb.TIM_Prescaler         = (uint16_t)psc;
    tb.TIM_Period            = (uint16_t)period;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(t, &tb);

    /* TRGO 选"更新事件"：每次计数溢出就发一个触发脉冲给 ADC */
    TIM_SelectOutputTrigger(t, TIM_TRGOSource_Update);
    TIM_Cmd(t, ENABLE);

    return 0U;
}

void SYS_ADC_DmaTimerTrigStop(ADC_TypeDef *adc, SysTimId_t tim)
{
    TIM_TypeDef *t = adc_trig_tim(tim);

    if (t != 0) TIM_Cmd(t, DISABLE);     /* 先停触发源，再停 ADC/DMA */
    SYS_ADC_DmaStop(adc);
}
