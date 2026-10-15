#include "sys_adc.h"
#include "gpio_core.h"
#include "sys_dma.h"

/* DFP 器件头文件未定义校准启动位宏（F4 的 CR2.CAL，bit2），此处补定义；
 * 头文件将来补全该宏后本定义不生效 */
#ifndef ADC_CR2_CAL
#define ADC_CR2_CAL   ((uint32_t)0x00000004)
#endif

/* ADC 模数转换模块实现：单次、连续、DMA 三种模式，差别只在初始化参数
 * 每次初始化都做稳压器上电与校准（ST 官方例程流程）
 * DMA 部分复用 sys_dma（外设到内存、DMA_Mode_Circular），搬运不占 CPU
 * ADC 的 DMA 数据流固定映射，ADC1/ADC3 共用 DMA2_Stream0，两路不能同时使用 */


/* 内部辅助 */
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

/* ADC 公共部分：时钟分频与工作模式
 * 分频 4：84MHz/4 = 21MHz，低于 36MHz 上限；改主频后需重新 Init */
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

/* 稳压器上电 + 单次校准：减小零点与增益误差
 * 寄存器直写（本 DFP 包的 SPL 未提供校准 API）：ADON=1 给内部稳压器上电，
 * 等待稳定后置 CAL 启动校准，硬件校准完成后自动把 CAL 清 0
 * 返回 : 0 = 校准完成；1 = 等待超时（ADC 可能没时钟）；两处等待都设上限，避免死等 */
static uint8_t adc_calibrate(ADC_TypeDef *adc)
{
    volatile uint32_t wait = 100000UL;

    adc->CR2 |= ADC_CR2_ADON;                /* 上电 */
    while (wait-- != 0U);                    /* 等稳压器稳定 tSTAB */

    adc->CR2 |= ADC_CR2_CAL;                 /* 启动校准 */
    wait = (uint32_t)SYS_ADC_CAL_TIMEOUT;    /* 防死等上限 */
    while ((adc->CR2 & ADC_CR2_CAL) != 0U) {
        if (wait-- == 0U) return 1U;         /* 超时：ADC 没起来 */
    }
    return 0U;
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

/* 运行态占用登记：规则组序列 SQR 与数据寄存器 DR 的争用防护
 * SYS_ADC_Read() 会改 SQR 并软件启动一次转换，与 DMA/连续/扫描模式争用同一个 ADC，
 * 会在 DMA 缓冲里混入这次单次结果，或抢走 DMA 本该搬走的样本
 * 做法：DMA 采集期间拒绝单次读并计数；三个 ADC 各自独立登记
 * 变量会被 ISR 与任务访问，用 volatile；只做单字节/单字读写，不需要临界区 */
#define ADC_IDX_NONE   3U
static volatile uint8_t  adc_dma_busy[3];    /* 该 ADC 是否正被 DMA 采集占用 */
static volatile uint8_t  adc_cal_ok[3];      /* 该 ADC 最近一次校准是否成功 */
static volatile uint32_t adc_conflict_cnt;   /* 因与 DMA 采集冲突被拒的次数 */

/* 数据流归属登记（0 = DMA2_Stream0，1 = DMA2_Stream2；0xFF = 空闲）
 * ADC1 与 ADC3 共用 DMA2_Stream0，后初始化的 ADC 会改掉前者的 DMA 配置，
 * 前者的缓冲从此不再更新；此处登记，冲突时直接拒绝 */
#define ADC_STREAM_FREE   0xFFU
static volatile uint8_t adc_dma_owner[2] = { ADC_STREAM_FREE, ADC_STREAM_FREE };

static uint8_t adc_index(ADC_TypeDef *adc)
{
    if (adc == ADC1) return 0U;
    if (adc == ADC2) return 1U;
    if (adc == ADC3) return 2U;
    return ADC_IDX_NONE;
}

/* ADC 对应的流下标（不是 DMA 通道号） */
static uint8_t adc_stream_idx(ADC_TypeDef *adc)
{
    if (adc == ADC1 || adc == ADC3) return 0U;
    if (adc == ADC2)                return 1U;
    return ADC_STREAM_FREE;
}

/* 该 ADC 正在 DMA 采集则计数并返回 1，调用方据此拒绝本次操作 */
static uint8_t adc_busy_guard(ADC_TypeDef *adc)
{
    uint8_t i = adc_index(adc);

    if (i != ADC_IDX_NONE && adc_dma_busy[i] != 0U) {
        adc_conflict_cnt++;
        return 1U;
    }
    return 0U;
}

/* 本 ADC 要用的 DMA 流是否已被另一个 ADC 占用 */
static uint8_t adc_stream_conflict(ADC_TypeDef *adc)
{
    uint8_t s = adc_stream_idx(adc);
    uint8_t i = adc_index(adc);

    if (s == ADC_STREAM_FREE || i == ADC_IDX_NONE) return 0U;
    if (adc_dma_owner[s] != ADC_STREAM_FREE && adc_dma_owner[s] != i) {
        adc_conflict_cnt++;
        return 1U;
    }
    return 0U;
}

/* DMA 采集起停时同步登记/注销（起：busy=1 且流归属=自己；停：两者都放开） */
static void adc_dma_mark(ADC_TypeDef *adc, uint8_t busy)
{
    uint8_t s = adc_stream_idx(adc);
    uint8_t i = adc_index(adc);

    if (i == ADC_IDX_NONE) return;
    adc_dma_busy[i] = busy;
    if (s != ADC_STREAM_FREE) {
        adc_dma_owner[s] = busy ? i : ADC_STREAM_FREE;
    }
}

/* 做一次校准并把结果存下来，供 SYS_ADC_CalOK() 查询
 * 返回 : 0 = 校准完成；1 = 等待超时（ADC 可能没时钟） */
static uint8_t adc_calibrate_record(ADC_TypeDef *adc)
{
    uint8_t r = adc_calibrate(adc);
    uint8_t i = adc_index(adc);

    if (i != ADC_IDX_NONE) adc_cal_ok[i] = (r == 0U) ? 1U : 0U;
    return r;
}


/* 基础功能 */
void SYS_ADC_Init(ADC_TypeDef *adc, uint8_t channel,
                  GPIO_TypeDef *port, uint16_t pin)
{
    if (adc == 0 || port == 0) return;
    if (adc_busy_guard(adc) != 0U) return;   /* 该 ADC 正在 DMA 采集，不改它的 SQR */

    adc_clk_enable(adc);
    adc_pin_analog(port, pin);

    ADC_Cmd(adc, DISABLE);
    adc_common_config();                                 /* 公共部分(分频等)统一重配 */

    adc_config_write(adc, channel, 0, 0);                /* 单次模式 */
    ADC_Cmd(adc, ENABLE);
    (void)adc_calibrate_record(adc);   /* 返回码：0=校准完成 1=超时（ADC 没时钟） */
}

uint16_t SYS_ADC_Read(ADC_TypeDef *adc, uint8_t channel)
{
    if (adc == 0) return 0;
    /* 与 DMA 采集互斥：本函数会改 SQR 并读 DR，被拒时返回 SYS_ADC_RAW_INVALID
     * 即 0xFFFF，可用 SYS_ADC_ReadOK() 判定，被拒次数见 SYS_ADC_ConflictCount() */
    if (adc_busy_guard(adc) != 0U) return SYS_ADC_RAW_INVALID;

    /* 每次读都重设一下通道 → 同一个 ADC 可以轮流读多个通道 */
    ADC_RegularChannelConfig(adc, channel, 1, SYS_ADC_SAMPLE_TIME);

    ADC_SoftwareStartConv(adc);
    /* 必须带超时：ADC 时钟未开/通道配错/未上电时，等待 EOC 会永久挂死
     * 超时返回 SYS_ADC_RAW_INVALID（0xFFFF），SYS_ADC_ReadOK() 可区分超时与满量程 */
    {
        uint32_t to = (uint32_t)SYS_ADC_EOC_TIMEOUT;
        while (ADC_GetFlagStatus(adc, ADC_FLAG_EOC) == RESET) {
            if (to-- == 0U) return SYS_ADC_RAW_INVALID;   /* 超时 */
        }
    }

    return ADC_GetConversionValue(adc);                  /* 读 DR 顺带清 EOC */
}

/* 判定 SYS_ADC_Read 的结果是否有效：仅在超时或被拒时返回 0xFFFF
 * 12 位满量程为 4095，不会与 0xFFFF 混淆 */
uint8_t SYS_ADC_ReadOK(uint16_t raw)
{
    return (raw == SYS_ADC_RAW_INVALID) ? 0U : 1U;
}

uint16_t SYS_ADC_ReadAvg(ADC_TypeDef *adc, uint8_t channel, uint16_t times)
{
    uint32_t sum = 0;
    uint16_t i;
    uint16_t v;

    if (times == 0U) times = SYS_ADC_AVG_TIMES;
    /* 先做一次互斥检查：否则每次调用都返回 0xFFFF，无法与单次失败区分 */
    if (adc_busy_guard(adc) != 0U) return SYS_ADC_RAW_INVALID;

    for (i = 0; i < times; i++) {
        v = SYS_ADC_Read(adc, channel);
        /* 任何一次超时或被拒即整组无效，不拿 0xFFFF 参与平均 */
        if (v == SYS_ADC_RAW_INVALID) return SYS_ADC_RAW_INVALID;
        sum += v;
    }
    return (uint16_t)(sum / times);
}

uint32_t SYS_ADC_ToMilliVolt(uint16_t raw)
{
    return ((uint32_t)raw * SYS_ADC_VREF_MV) / 4095UL;
}


/* 扩展功能 */
void SYS_ADC_ContInit(ADC_TypeDef *adc, uint8_t channel,
                      GPIO_TypeDef *port, uint16_t pin)
{
    if (adc == 0 || port == 0) return;
    if (adc_busy_guard(adc) != 0U) return;   /* 该 ADC 正在 DMA 采集，不改它的 SQR */

    adc_clk_enable(adc);
    adc_pin_analog(port, pin);

    ADC_Cmd(adc, DISABLE);
    adc_common_config();

    adc_config_write(adc, channel, 0, 1);                /* 连续模式 */
    ADC_Cmd(adc, ENABLE);
    (void)adc_calibrate_record(adc);   /* 返回码：0=校准完成 1=超时（ADC 没时钟） */

    ADC_SoftwareStartConv(adc);      /* 启动后硬件自动连续转换 */
}

uint16_t SYS_ADC_ContValue(ADC_TypeDef *adc)
{
    if (adc == 0) return 0;
    /* 读 DR 会清 EOC，DMA 搬运期间读它等于取走一个样本，同样拒绝；连续值请读 DMA 缓冲 */
    if (adc_busy_guard(adc) != 0U) return SYS_ADC_RAW_INVALID;
    return ADC_GetConversionValue(adc);                  /* 读最新结果 */
}

void SYS_ADC_ContStop(ADC_TypeDef *adc)
{
    if (adc == 0) return;
    ADC_Cmd(adc, DISABLE);                               /* 关使能即停 */
}

/* 单次转换并换算为 mV；前提是对应通道已 SYS_ADC_Init（同 SYS_ADC_Read） */
uint32_t SYS_ADC_ReadMilliVolt(ADC_TypeDef *adc, uint8_t channel)
{
    return SYS_ADC_ToMilliVolt(SYS_ADC_Read(adc, channel));
}

/* 单通道 DMA 采集：连续转换 + DMA 循环搬运 */
void SYS_ADC_DmaInit(ADC_TypeDef *adc, uint8_t channel,
                     GPIO_TypeDef *port, uint16_t pin,
                     volatile uint16_t *buf, uint16_t len)
{
    DMA_Stream_TypeDef *stream;

    if (adc == 0 || port == 0 || buf == 0 || len == 0U) return;
    stream = adc_dma_stream(adc);
    if (stream == 0) return;
    /* ADC1/ADC3 共用 DMA2_Stream0：已被另一个 ADC 占用就不能再配，否则会改掉
     * 对方的 DMA 配置且对方无从察觉（缓冲不再更新）；切换前先对占用方调 SYS_ADC_DmaStop() */
    if (adc_stream_conflict(adc) != 0U) return;

    adc_clk_enable(adc);
    adc_pin_analog(port, pin);

    /* 先停旧配置（DMA + ADC），保证重配干净 */
    SYS_DMA_Stop(stream);
    ADC_Cmd(adc, DISABLE);
    adc_common_config();

    adc_config_write(adc, channel, 0, 1);                /* 连续模式 */

    /* DMA：外设 = ADC 数据寄存器（半字），循环模式，缓冲持续滚动刷新 */
    SYS_DMA_PeriphToMem(stream, adc_dma_channel(adc),
                        (uint32_t)&adc->DR, (uint16_t *)buf, len, 2, 1);
    ADC_DMACmd(adc, ENABLE);                             /* 打开 ADC 的 DMA 请求 */

    ADC_Cmd(adc, ENABLE);
    (void)adc_calibrate_record(adc);   /* 返回码：0=校准完成 1=超时（ADC 没时钟） */
    ADC_SoftwareStartConv(adc);      /* 启动，之后由硬件连续转换 */
    adc_dma_mark(adc, 1U);           /* 登记占用：此后 SYS_ADC_Read 会被拒绝并计数 */
}

/* 多通道扫描 + DMA：按数组顺序扫描每个通道，结果依次进缓冲 */
void SYS_ADC_DmaScanInit(ADC_TypeDef *adc, const SysAdcCh_t *chs,
                         uint8_t count, volatile uint16_t *buf, uint16_t len)
{
    DMA_Stream_TypeDef *stream;
    ADC_InitTypeDef ai;
    uint8_t i;

    if (adc == 0 || chs == 0 || buf == 0 || len == 0U) return;
    if (count == 0U || count > 16U) return;
    /* 缓冲长度必须是通道数的整数倍，否则最后一轮扫描绕回开头，
     * 前几个通道的值会被上一轮的尾部数据覆盖，理由见 SYS_ADC_DmaTimerTrigInit */
    if ((len % (uint32_t)count) != 0U) return;
    stream = adc_dma_stream(adc);
    if (stream == 0) return;
    if (adc_stream_conflict(adc) != 0U) return;   /* 流被另一个 ADC 占了，理由见 DmaInit */

    adc_clk_enable(adc);
    for (i = 0; i < count; i++) {
        adc_pin_analog(chs[i].port, chs[i].pin);
    }

    SYS_DMA_Stop(stream);
    ADC_Cmd(adc, DISABLE);
    adc_common_config();

    /* 扫描 + 连续：扫完 count 个通道后立即开始下一轮 */
    ADC_StructInit(&ai);
    ai.ADC_Resolution           = ADC_Resolution_12b;
    ai.ADC_ScanConvMode         = ENABLE;
    ai.ADC_ContinuousConvMode   = ENABLE;
    ai.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
    ai.ADC_ExternalTrigConv     = ADC_ExternalTrigConv_T1_CC1;
    ai.ADC_DataAlign            = ADC_DataAlign_Right;
    ai.ADC_NbrOfConversion      = count;
    ADC_Init(adc, &ai);

    /* 设置各通道的转换顺序，rank 从 1 开始 */
    for (i = 0; i < count; i++) {
        ADC_RegularChannelConfig(adc, chs[i].channel, (uint8_t)(i + 1U),
                                 SYS_ADC_SAMPLE_TIME);
    }

    /* DMA 循环搬运：每转换完一个通道自动写一个半字 */
    SYS_DMA_PeriphToMem(stream, adc_dma_channel(adc),
                        (uint32_t)&adc->DR, (uint16_t *)buf, len, 2, 1);
    ADC_DMACmd(adc, ENABLE);

    ADC_Cmd(adc, ENABLE);
    (void)adc_calibrate_record(adc);   /* 返回码：0=校准完成 1=超时（ADC 没时钟） */
    ADC_SoftwareStartConv(adc);
    adc_dma_mark(adc, 1U);
}

void SYS_ADC_DmaStop(ADC_TypeDef *adc)
{
    DMA_Stream_TypeDef *stream;

    if (adc == 0) return;

    /* 先注销占用再判断流：若因 stream == 0 提前返回，busy 标志会一直保留，
     * 此后 SYS_ADC_Read 全部被拒 */
    adc_dma_mark(adc, 0U);

    stream = adc_dma_stream(adc);
    if (stream == 0) return;

    ADC_Cmd(adc, DISABLE);           /* 先停转换，否则仍会发 DMA 请求 */
    SYS_DMA_Stop(stream);
}


/* 定时器触发采样：定时器 TRGO 硬件触发 ADC，采样间隔由触发源频率决定
 * 下面三张表把库的枚举编号翻译成标准库的寄存器常量 */
/* SysTimId_t 到 TIM_TypeDef*
 * F4 标准库的 ADC 外部触发源只列了 T2_TRGO / T3_TRGO / T8_TRGO，
 * TIM1/4/5 的 TRGO 未列入，只列了它们的 CC 通道，故只接受这三个定时器 */
static TIM_TypeDef *adc_trig_tim(SysTimId_t id)
{
    switch (id) {
        case SYS_TIM_2: return TIM2;
        case SYS_TIM_3: return TIM3;
        case SYS_TIM_8: return TIM8;
        default:        return 0;
    }
}

/* SysTimId_t 对应的定时器时钟频率 Hz
 * 依据本库 sys_clock 的 PLL 档（168MHz、APB1 分频 4、APB2 分频 2）与
 * STM32F4 规则：APB 分频不等于 1 时定时器时钟 = PCLK × 2 */
static uint32_t adc_trig_tim_clk(SysTimId_t id)
{
    switch (id) {
        case SYS_TIM_8:  return 168000000UL;   /* APB2：84MHz × 2 */
        case SYS_TIM_2:
        case SYS_TIM_3:  return  84000000UL;   /* APB1：42MHz × 2 */
        default:         return  84000000UL;
    }
}

/* SysTimId_t 对应的 ADC 外部触发源常量 */
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
                                 uint8_t count, volatile uint16_t *buf, uint16_t len,
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

    /* DMA 缓冲长度必须是通道数的整数倍
     * DMA 按每转完 count 个半字一轮扫描工作，len 不是整数倍时最后一轮只写一半
     * 就到缓冲末尾，剩余通道数据绕回缓冲开头，buf[0..k] 被最后一轮尾部覆盖 */
    if ((len % (uint32_t)count) != 0U)                 return 1U;

    t   = adc_trig_tim(tim);
    src = adc_trig_src(tim);
    if (t == 0 || src == 0U) return 2U;                /* 该定时器不作为触发源 */

    stream = adc_dma_stream(adc);
    if (stream == 0) return 1U;
    if (adc_stream_conflict(adc) != 0U) return 1U;     /* 流被另一个 ADC 占了 */

    /* 模拟引脚配置 */
    adc_clk_enable(adc);
    for (i = 0; i < count; i++) {
        adc_pin_analog(chs[i].port, chs[i].pin);
    }

    /* ADC：扫描 + 单次（每次触发只扫一轮）+ 上升沿外部触发 */
    SYS_DMA_Stop(stream);
    ADC_Cmd(adc, DISABLE);
    adc_common_config();

    ADC_StructInit(&ai);
    ai.ADC_Resolution           = ADC_Resolution_12b;
    ai.ADC_ScanConvMode         = ENABLE;
    ai.ADC_ContinuousConvMode   = DISABLE;             /* 由触发事件驱动 */
    ai.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_Rising;
    ai.ADC_ExternalTrigConv     = src;
    ai.ADC_DataAlign            = ADC_DataAlign_Right;
    ai.ADC_NbrOfConversion      = count;
    ADC_Init(adc, &ai);

    for (i = 0; i < count; i++) {
        ADC_RegularChannelConfig(adc, chs[i].channel, (uint8_t)(i + 1U),
                                 SYS_ADC_SAMPLE_TIME);
    }

    /* DMA：循环模式，ADC 每产生一个结果搬运一个半字 */
    SYS_DMA_PeriphToMem(stream, adc_dma_channel(adc),
                        (uint32_t)&adc->DR, (uint16_t *)buf, len, 2, 1);
    ADC_DMACmd(adc, ENABLE);

    ADC_Cmd(adc, ENABLE);
    (void)adc_calibrate_record(adc);   /* 返回码：0=校准完成 1=超时（ADC 没时钟） */

    /* 定时器：计数到 sample_hz 产生一次更新事件并送上 TRGO
     * 分频先归一到 1MHz，再计 1000000/sample_hz 个数，全整数运算 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM8, ENABLE);

    tclk = adc_trig_tim_clk(tim);
    psc  = (tclk / 1000000UL) - 1UL;                   /* 计数频率归一到 1MHz */

    if (sample_hz > 1000000UL) sample_hz = 1000000UL;  /* 上限保护 */
    period = (1000000UL / sample_hz) - 1UL;

    tb.TIM_Prescaler         = (uint16_t)psc;
    tb.TIM_Period            = (uint16_t)period;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(t, &tb);

    /* TRGO 选更新事件：每次计数溢出发一个触发脉冲给 ADC */
    TIM_SelectOutputTrigger(t, TIM_TRGOSource_Update);
    TIM_Cmd(t, ENABLE);

    adc_dma_mark(adc, 1U);             /* 登记占用，理由见 DmaInit */
    return 0U;
}

void SYS_ADC_DmaTimerTrigStop(ADC_TypeDef *adc, SysTimId_t tim)
{
    TIM_TypeDef *t = adc_trig_tim(tim);

    if (t != 0) TIM_Cmd(t, DISABLE);     /* 先停触发源，再停 ADC/DMA */
    SYS_ADC_DmaStop(adc);
}

/* 定时器触发 + 扫描 + DMA：与 DmaScanInit 的区别是触发源为外部事件（定时器 TRGO），
 * 调用后不主动启动转换，其余配置相同 */
void SYS_ADC_ExtTrigScanInit(ADC_TypeDef *adc, uint32_t ext_trig,
                             const SysAdcCh_t *chs, uint8_t count,
                             volatile uint16_t *buf, uint16_t len)
{
    DMA_Stream_TypeDef *stream;
    ADC_InitTypeDef ai;
    uint8_t i;

    if (adc == 0 || chs == 0 || buf == 0 || len == 0U) return;
    if (count == 0U || count > 16U) return;
    /* 缓冲长度必须是通道数的整数倍，理由见 SYS_ADC_DmaTimerTrigInit */
    if ((len % (uint32_t)count) != 0U) return;
    stream = adc_dma_stream(adc);
    if (stream == 0) return;
    if (adc_stream_conflict(adc) != 0U) return;   /* 流被另一个 ADC 占了 */

    adc_clk_enable(adc);
    for (i = 0; i < count; i++) {
        adc_pin_analog(chs[i].port, chs[i].pin);
    }

    SYS_DMA_Stop(stream);
    ADC_Cmd(adc, DISABLE);
    adc_common_config();

    /* 扫描 + 外部触发：上升沿事件到即开始一轮转换
     * 不打开 ContinuousConvMode，触发一次采一轮，采样率完全由触发源频率决定 */
    ADC_StructInit(&ai);
    ai.ADC_Resolution           = ADC_Resolution_12b;
    ai.ADC_ScanConvMode         = ENABLE;
    ai.ADC_ContinuousConvMode   = DISABLE;
    ai.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_Rising;
    ai.ADC_ExternalTrigConv     = ext_trig;
    ai.ADC_DataAlign            = ADC_DataAlign_Right;
    ai.ADC_NbrOfConversion      = count;
    ADC_Init(adc, &ai);

    /* 设置转换顺序，rank 从 1 起 */
    for (i = 0; i < count; i++) {
        ADC_RegularChannelConfig(adc, chs[i].channel, (uint8_t)(i + 1U),
                                 SYS_ADC_SAMPLE_TIME);
    }

    SYS_DMA_PeriphToMem(stream, adc_dma_channel(adc),
                        (uint32_t)&adc->DR, (uint16_t *)buf, len, 2, 1);
    ADC_DMACmd(adc, ENABLE);

    ADC_Cmd(adc, ENABLE);
    (void)adc_calibrate_record(adc);   /* 返回码：0=校准完成 1=超时（ADC 没时钟） */
    adc_dma_mark(adc, 1U);
    /* 不调用 SoftwareStartConv，等触发源第一个事件；外部触发已使能 */
}


/* 运行态查询：只读快照，不参与采集，可在任务中直接调用
 * DmaBusy() 查该 ADC 是否仍在 DMA 采集；CalOK() 查最近一次校准是否成功
 * ConflictCount() 查被拒次数，ConflictClear() 清零 */
uint8_t SYS_ADC_DmaBusy(ADC_TypeDef *adc)
{
    uint8_t i = adc_index(adc);

    return (i != ADC_IDX_NONE) ? adc_dma_busy[i] : 0U;
}

uint8_t SYS_ADC_CalOK(ADC_TypeDef *adc)
{
    uint8_t i = adc_index(adc);

    return (i != ADC_IDX_NONE) ? adc_cal_ok[i] : 0U;
}

uint32_t SYS_ADC_ConflictCount(void)
{
    return adc_conflict_cnt;
}

void SYS_ADC_ConflictClear(void)
{
    adc_conflict_cnt = 0UL;
}
