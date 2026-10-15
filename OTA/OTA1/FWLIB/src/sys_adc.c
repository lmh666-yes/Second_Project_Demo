#include "sys_adc.h"
/* 接口用法与参数约束见 sys_adc.h */
#include "gpio_core.h"
#include "sys_dma.h"

/* 本 DFP 器件头文件未定义 CR2.CAL（F4 校准启动位，bit2），此处补定义 */
#ifndef ADC_CR2_CAL
#define ADC_CR2_CAL   ((uint32_t)0x00000004)
#endif

/* ================================================================
 *  sys_adc.c — 【系统】ADC 模数转换模块  实现文件
 * ================================================================
 *  三种模式只差初始化参数：单次(不连续) / 连续 / DMA，每次 Init 都做稳压器上电与校准。
 *  DMA 复用 sys_dma（外设→内存，循环模式）；流映射 ADC1/ADC3 → DMA2_Stream0。
 * ================================================================ */


/* ============================= 内部辅助 ============================= */
/* 打开 ADC 所在 APB2 总线的时钟 */
static void adc_clk_enable(ADC_TypeDef *adc)
{
    if      (adc == ADC1) RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    else if (adc == ADC2) RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC2, ENABLE);
    else if (adc == ADC3) RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC3, ENABLE);
}

/* 引脚 → 模拟输入(AIN)：关闭施密特触发器，输入阻抗最高 */
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

/* ADC 公共部分：时钟分频 / 工作模式；分频 4 → 21MHz，硬件上限 36MHz */
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

/* 稳压器上电 + 校准（寄存器直写，SPL 无校准 API）；ADON=1 上电，稳定后置 CAL，校准完成硬件自动清 CAL；
 * 返回 0 = 完成，1 = 等待超时（ADC 无时钟） */
static uint8_t adc_calibrate(ADC_TypeDef *adc)
{
    volatile uint32_t wait = 100000UL;

    adc->CR2 |= ADC_CR2_ADON;                /* 上电 */
    while (wait-- != 0U);                    /* 等稳压器稳定 tSTAB，F4 手册约 3µs */

    adc->CR2 |= ADC_CR2_CAL;                 /* 启动校准 */
    wait = (uint32_t)SYS_ADC_CAL_TIMEOUT;    /* 防死等上限 */
    while ((adc->CR2 & ADC_CR2_CAL) != 0U) {
        if (wait-- == 0U) return 1U;         /* 超时：ADC 没起来 */
    }
    return 0U;
}

/* 填 ADC 初始化结构体，单次/连续/扫描由参数决定 */
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

/* ---- 运行态占用登记（SQR / DR 争用防护）----------------------------
 * SYS_ADC_Read() 改 SQR 并启动转换，与 DMA/连续/扫描模式争用 SQR 与 DR。
 * 后果：DMA 缓冲混入临时单次转换结果，或 ADC_GetConversionValue() 抢走 DMA 样本。
 * DMA 采集期间拒绝单次读并计数；上层先 DmaStop 或直读缓冲。
 * 三个 ADC 独立登记，变量 volatile；单字节读写本身原子，无需临界区。 */
#define ADC_IDX_NONE   3U
static volatile uint8_t  adc_dma_busy[3];    /* 该 ADC 是否正被 DMA 采集占用 */
static volatile uint8_t  adc_cal_ok[3];      /* 该 ADC 最近一次校准是否成功 */
static volatile uint32_t adc_conflict_cnt;   /* 因"与 DMA 采集冲突"被拒的次数 */

/* 数据流归属登记（0 = DMA2_Stream0，1 = DMA2_Stream2；0xFF = 空闲）
 * ADC1 与 ADC3 共用 DMA2_Stream0：先 DmaInit(ADC1) 再 DmaInit(ADC3) 时，
 * 后者会覆盖前者的 DMA 配置，前者缓冲不再更新且不报错，故冲突即拒绝。 */
#define ADC_STREAM_FREE   0xFFU
static volatile uint8_t adc_dma_owner[2] = { ADC_STREAM_FREE, ADC_STREAM_FREE };

static uint8_t adc_index(ADC_TypeDef *adc)
{
    if (adc == ADC1) return 0U;
    if (adc == ADC2) return 1U;
    if (adc == ADC3) return 2U;
    return ADC_IDX_NONE;
}

/* ADC → 流下标（不是 DMA 通道号） */
static uint8_t adc_stream_idx(ADC_TypeDef *adc)
{
    if (adc == ADC1 || adc == ADC3) return 0U;
    if (adc == ADC2)                return 1U;
    return ADC_STREAM_FREE;
}

/* 该 ADC 正忙则计数并返回 1 */
static uint8_t adc_busy_guard(ADC_TypeDef *adc)
{
    uint8_t i = adc_index(adc);

    if (i != ADC_IDX_NONE && adc_dma_busy[i] != 0U) {
        adc_conflict_cnt++;
        return 1U;
    }
    return 0U;
}

/* 所需 DMA 流是否已被另一个 ADC 占用 */
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

/* DMA 采集起停时登记/注销（起：busy=1、流归属=自己；停：都放开） */
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

/* 校准并记录结果供 SYS_ADC_CalOK() 查询 */
static uint8_t adc_calibrate_record(ADC_TypeDef *adc)
{
    uint8_t r = adc_calibrate(adc);
    uint8_t i = adc_index(adc);

    if (i != ADC_IDX_NONE) adc_cal_ok[i] = (r == 0U) ? 1U : 0U;
    return r;
}


/* ============================= 区块 2：基础功能 ============================= */
void SYS_ADC_Init(ADC_TypeDef *adc, uint8_t channel,
                  GPIO_TypeDef *port, uint16_t pin)
{
    if (adc == 0 || port == 0) return;
    if (adc_busy_guard(adc) != 0U) return;   /* DMA 采集中，不改 SQR */

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
    /* 与 DMA 采集互斥（本函数改 SQR 并抢读 DR），被拒返回无效值 */
    if (adc_busy_guard(adc) != 0U) return SYS_ADC_RAW_INVALID;

    ADC_RegularChannelConfig(adc, channel, 1, SYS_ADC_SAMPLE_TIME);

    ADC_SoftwareStartConv(adc);
    /* EOC 忙等须带超时，否则 ADC 无时钟或通道配错时永久挂死 */
    {
        uint32_t to = (uint32_t)SYS_ADC_EOC_TIMEOUT;
        while (ADC_GetFlagStatus(adc, ADC_FLAG_EOC) == RESET) {
            if (to-- == 0U) return SYS_ADC_RAW_INVALID;
        }
    }

    return ADC_GetConversionValue(adc);                  /* 读 DR 顺带清 EOC */
}

/* 判定 SYS_ADC_Read 结果是否有效（仅超时返回 SYS_ADC_RAW_INVALID） */
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
    /* 先做互斥检查，否则 times 次全为无效值，平均值会误导上层 */
    if (adc_busy_guard(adc) != 0U) return SYS_ADC_RAW_INVALID;

    for (i = 0; i < times; i++) {
        v = SYS_ADC_Read(adc, channel);
        if (v == SYS_ADC_RAW_INVALID) return SYS_ADC_RAW_INVALID;
        sum += v;
    }
    return (uint16_t)(sum / times);
}

uint32_t SYS_ADC_ToMilliVolt(uint16_t raw)
{
    return ((uint32_t)raw * SYS_ADC_VREF_MV) / 4095UL;
}


/* ============================= 区块 3：扩展功能 ============================= */
void SYS_ADC_ContInit(ADC_TypeDef *adc, uint8_t channel,
                      GPIO_TypeDef *port, uint16_t pin)
{
    if (adc == 0 || port == 0) return;
    if (adc_busy_guard(adc) != 0U) return;   /* DMA 采集中，不改 SQR */

    adc_clk_enable(adc);
    adc_pin_analog(port, pin);

    ADC_Cmd(adc, DISABLE);
    adc_common_config();

    adc_config_write(adc, channel, 0, 1);                /* 连续模式 */
    ADC_Cmd(adc, ENABLE);
    (void)adc_calibrate_record(adc);   /* 返回码：0=校准完成 1=超时（ADC 没时钟） */

    ADC_SoftwareStartConv(adc);      /* 启动一发，之后硬件自动连续转换 */
}

uint16_t SYS_ADC_ContValue(ADC_TypeDef *adc)
{
    if (adc == 0) return 0;
    /* 读 DR 清 EOC 并抢走 DMA 样本，DMA 采集期间同样拒绝 */
    if (adc_busy_guard(adc) != 0U) return SYS_ADC_RAW_INVALID;
    return ADC_GetConversionValue(adc);                  /* 读最新结果 */
}

void SYS_ADC_ContStop(ADC_TypeDef *adc)
{
    if (adc == 0) return;
    ADC_Cmd(adc, DISABLE);                               /* 关使能即停 */
}

/* 单次转换 + mV 换算 */
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
    /* ADC1/ADC3 共用 DMA2_Stream0，已占用时不能再配（会覆盖对方配置）；切换前先 DmaStop 占用方 */
    if (adc_stream_conflict(adc) != 0U) return;

    adc_clk_enable(adc);
    adc_pin_analog(port, pin);

    /* 先停旧配置（DMA + ADC）再重配 */
    SYS_DMA_Stop(stream);
    ADC_Cmd(adc, DISABLE);
    adc_common_config();

    adc_config_write(adc, channel, 0, 1);                /* 连续模式 */

    /* DMA：外设 = ADC 数据寄存器(半字)，循环模式（DMA_Mode_Circular） */
    SYS_DMA_PeriphToMem(stream, adc_dma_channel(adc),
                        (uint32_t)&adc->DR, (uint16_t *)buf, len, 2, 1);
    ADC_DMACmd(adc, ENABLE);                             /* 打开 ADC 的 DMA 请求 */


    ADC_Cmd(adc, ENABLE);
    (void)adc_calibrate_record(adc);   /* 返回码：0=校准完成 1=超时（ADC 没时钟） */
    ADC_SoftwareStartConv(adc);      /* 启动一发，之后硬件自动连续转换 */
    adc_dma_mark(adc, 1U);           /* 登记占用：此后 SYS_ADC_Read 被拒并计数 */
}

/* 多通道扫描 + DMA：按数组顺序扫描各通道，结果依次进缓冲 */
void SYS_ADC_DmaScanInit(ADC_TypeDef *adc, const SysAdcCh_t *chs,
                         uint8_t count, volatile uint16_t *buf, uint16_t len)
{
    DMA_Stream_TypeDef *stream;
    ADC_InitTypeDef ai;
    uint8_t i;

    if (adc == 0 || chs == 0 || buf == 0 || len == 0U) return;
    if (count == 0U || count > 16U) return;
    /* 缓冲长度须为通道数整数倍，理由见 SYS_ADC_DmaTimerTrigInit */
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

    /* 扫描 + 连续：扫完 count 个通道立即开始下一轮 */
    ADC_StructInit(&ai);
    ai.ADC_Resolution           = ADC_Resolution_12b;
    ai.ADC_ScanConvMode         = ENABLE;
    ai.ADC_ContinuousConvMode   = ENABLE;
    ai.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
    ai.ADC_ExternalTrigConv     = ADC_ExternalTrigConv_T1_CC1;
    ai.ADC_DataAlign            = ADC_DataAlign_Right;
    ai.ADC_NbrOfConversion      = count;
    ADC_Init(adc, &ai);

    /* 排转换顺序（rank 从 1 起） */
    for (i = 0; i < count; i++) {
        ADC_RegularChannelConfig(adc, chs[i].channel, (uint8_t)(i + 1U),
                                 SYS_ADC_SAMPLE_TIME);
    }

    /* DMA 循环搬运：每转换完一个通道写一个半字 */
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

    /*  先注销占用再判断流，提前返回时不留 busy 标志 */
    adc_dma_mark(adc, 0U);

    stream = adc_dma_stream(adc);
    if (stream == 0) return;

    ADC_Cmd(adc, DISABLE);           /* 先停转换，否则仍在发 DMA 请求 */
    SYS_DMA_Stop(stream);
}


/* ================= 定时器触发采样（精确采样率，项目5 波形采集） =================
 * 定时器 → TRGO → ADC 硬件触发，采样点间隔严格相等。
 * 下面三张表把库枚举编号翻译成标准库寄存器常量。 */
/* SysTimId_t → TIM_TypeDef*；F4 标准库 ADC 触发源只列了 T2_TRGO / T3_TRGO / T8_TRGO */
static TIM_TypeDef *adc_trig_tim(SysTimId_t id)
{
    switch (id) {
        case SYS_TIM_2: return TIM2;
        case SYS_TIM_3: return TIM3;
        case SYS_TIM_8: return TIM8;
        default:        return 0;
    }
}

/* SysTimId_t → 定时器时钟 Hz；按 sys_clock 的 PLL 档（168MHz、APB1÷4、APB2÷2）与
 * F4 的 APB 分频≠1 时定时器时钟 ×2 规则算出 */
static uint32_t adc_trig_tim_clk(SysTimId_t id)
{
    switch (id) {
        case SYS_TIM_8:  return 168000000UL;   /* APB2：84MHz × 2 */
        case SYS_TIM_2:
        case SYS_TIM_3:  return  84000000UL;   /* APB1：42MHz × 2 */
        default:         return  84000000UL;
    }
}

/* ADC → 外部触发源常量 */
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

    /* DMA 缓冲长度须为通道数整数倍（DMA 按传输个数回卷地址，否则末轮剩余通道写回 buf[0..k]） */
    if ((len % (uint32_t)count) != 0U)                 return 1U;

    t   = adc_trig_tim(tim);
    src = adc_trig_src(tim);
    if (t == 0 || src == 0U) return 2U;                /* 该定时器不作为触发源 */

    stream = adc_dma_stream(adc);
    if (stream == 0) return 1U;
    if (adc_stream_conflict(adc) != 0U) return 1U;     /* 流被另一个 ADC 占了 */

    /* 模拟引脚 */
    adc_clk_enable(adc);
    for (i = 0; i < count; i++) {
        adc_pin_analog(chs[i].port, chs[i].pin);
    }

    /* ADC：扫描 + 单次（每次触发扫一轮）+ 上升沿外部触发 */
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

    /* DMA 循环模式，ADC 每出一个结果搬一个半字 */
    SYS_DMA_PeriphToMem(stream, adc_dma_channel(adc),
                        (uint32_t)&adc->DR, (uint16_t *)buf, len, 2, 1);
    ADC_DMACmd(adc, ENABLE);

    ADC_Cmd(adc, ENABLE);
    (void)adc_calibrate_record(adc);   /* 返回码：0=校准完成 1=超时（ADC 没时钟） */

    /* 分频到 1MHz 后计数 1000000/sample_hz，整数运算精确得到采样率 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM8, ENABLE);

    tclk = adc_trig_tim_clk(tim);
    psc  = (tclk / 1000000UL) - 1UL;                   /* → 1MHz 计数 */

    if (sample_hz > 1000000UL) sample_hz = 1000000UL;  /* 上限 1MHz */
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

    adc_dma_mark(adc, 1U);             /* 登记占用 */
    return 0U;
}

void SYS_ADC_DmaTimerTrigStop(ADC_TypeDef *adc, SysTimId_t tim)
{
    TIM_TypeDef *t = adc_trig_tim(tim);

    if (t != 0) TIM_Cmd(t, DISABLE);     /* 先停触发源，再停 ADC 与 DMA */
    SYS_ADC_DmaStop(adc);
}

/* 定时器触发 + 扫描 + DMA，触发源为定时器 TRGO */
void SYS_ADC_ExtTrigScanInit(ADC_TypeDef *adc, uint32_t ext_trig,
                             const SysAdcCh_t *chs, uint8_t count,
                             volatile uint16_t *buf, uint16_t len)
{
    DMA_Stream_TypeDef *stream;
    ADC_InitTypeDef ai;
    uint8_t i;

    if (adc == 0 || chs == 0 || buf == 0 || len == 0U) return;
    if (count == 0U || count > 16U) return;
    /* 缓冲长度须为通道数整数倍（理由见 DmaTimerTrigInit） */
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

    /* 扫描 + 外部触发：上升沿事件到即扫一轮，采样率由触发源频率决定 */
    ADC_StructInit(&ai);
    ai.ADC_Resolution           = ADC_Resolution_12b;
    ai.ADC_ScanConvMode         = ENABLE;
    ai.ADC_ContinuousConvMode   = DISABLE;
    ai.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_Rising;
    ai.ADC_ExternalTrigConv     = ext_trig;
    ai.ADC_DataAlign            = ADC_DataAlign_Right;
    ai.ADC_NbrOfConversion      = count;
    ADC_Init(adc, &ai);

    /* 排转换顺序（rank 从 1 起） */
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
    /* 无需 SoftwareStartConv：外部触发已使能，等触发源第一个事件 */
}


/* ================= 区块 4：运行态查询（配合冲突拒绝机制） =================
 *  只读快照，无锁无阻塞，不参与采集：
 *    · Read 返回 SYS_ADC_RAW_INVALID → DmaBusy() 查采集是否未停；
 *    · 读数系统性偏差                 → CalOK() 查校准是否超时；
 *    · 被拒次数                       → ConflictCount()/ConflictClear()。 */
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
