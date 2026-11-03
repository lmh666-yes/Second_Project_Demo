#include "sys_dac.h"
#include "gpio_core.h"

/* DAC 模拟输出实现。三条数据通路：
 * 1) 直写：CPU 写 DAC_DHR12R1 → DAC 转换 → PA4
 * 2) 内置波形：TIM6 更新事件 → TRGO → 触发内部三角/噪声发生器自动改数据 → PA4，CPU 不参与
 * 3) DMA 波形：TIM6 更新事件 → TRGO → DAC 请求 → DMA 搬数 → PA4，循环模式自动重播，CPU 不参与
 * 直写时须关闭触发（DAC_Trigger_None），否则写入的数据会被下一个触发覆盖 */


/* 内部状态 */
/* 32 点正弦表：y = 2048 + 2047*sin(2*pi*i/32)，中点 2048，约 1.65V（VREF 3300mV）
 * 表中最大值 3950、最小值 146，未用满 12 位量程 */
static const uint16_t sys_dac_sine[SYS_DAC_SINE_POINTS] = {
    2048U, 2447U, 2831U, 3178U, 3467U, 3697U, 3861U, 3950U,
    3950U, 3861U, 3697U, 3467U, 3178U, 2831U, 2447U, 2048U,
    1649U, 1265U,  918U,  629U,  399U,  235U,  146U,  146U,
     235U,  399U,  629U,  918U, 1265U, 1649U, 1974U, 2048U
};

/* 每通道状态 */
static uint8_t  sys_dac_inited[SYS_DAC_COUNT];
static uint8_t  sys_dac_wave  [SYS_DAC_COUNT];   /* 1 = 正在输出波形 */
static uint16_t sys_dac_len   [SYS_DAC_COUNT];   /* DMA 波形点数 */


/* 内部小工具 */
static uint8_t dac_valid(uint8_t ch)
{
    if (ch < 1U || ch > SYS_DAC_COUNT) return 0U;

#if (SYS_DAC2_ENABLE == 0)
    if (ch == SYS_DAC_2) return 0U;     /* 本板 PA5 被触摸/ADC 占用 */
#endif

    return 1U;
}

static uint32_t dac_channel(uint8_t ch)
{
    return (ch == SYS_DAC_1) ? DAC_Channel_1 : DAC_Channel_2;
}

/* 标准库只有 SetChannel1Data / SetChannel2Data，这里按通道号包一层 */
static void dac_write_data(uint8_t ch, uint16_t value)
{
    if (ch == SYS_DAC_1) DAC_SetChannel1Data((uint32_t)DAC_Align_12b_R, value);
    else                 DAC_SetChannel2Data((uint32_t)DAC_Align_12b_R, value);
}

/* 引脚设为模拟模式：漏配该步时输出恒为 0V，被数字推挽级拉死 */
static void dac_pin_init(uint8_t ch)
{
    GPIO_InitTypeDef gi;

    if (ch == SYS_DAC_1) GPIO_ClockEnable(SYS_DAC1_PORT);
    else                 GPIO_ClockEnable(SYS_DAC2_PORT);

    gi.GPIO_Mode  = GPIO_Mode_AN;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    gi.GPIO_Pin = (ch == SYS_DAC_1) ? SYS_DAC1_PIN : SYS_DAC2_PIN;
    GPIO_Init((ch == SYS_DAC_1) ? SYS_DAC1_PORT : SYS_DAC2_PORT, &gi);
}

/* TIM6 每 1/freq_hz 秒产生一次更新事件并输出 TRGO */
static uint8_t dac_tim_init(uint32_t freq_hz)
{
    TIM_TimeBaseInitTypeDef tb;
    RCC_ClocksTypeDef clk;
    uint32_t timclk;
    uint32_t div;
    uint32_t arr;

    if (freq_hz == 0U) return 1U;

    RCC_GetClocksFreq(&clk);
    /* TIM6 挂 APB1：APB1 分频不为 1 时定时器时钟 = PCLK1 × 2 */
    timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U) ? (clk.PCLK1_Frequency * 2U)
                                                  : clk.PCLK1_Frequency;

    /* 先分频到 1MHz 再设 ARR，低频时计数精度更高 */
    div = timclk / 1000000UL;
    if (div == 0U) div = 1U;
    if (div > 65536UL) div = 65536UL;

    arr = 1000000UL / freq_hz;
    if (arr == 0U) arr = 1U;
    if (arr > 65536UL) return 2U;       /* 频率太低，ARR 装不下 */

    RCC_APB1PeriphClockCmd(SYS_DAC_TIM_CLK, ENABLE);

    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Prescaler         = (uint16_t)(div - 1U);
    tb.TIM_Period            = (uint16_t)(arr - 1U);
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(SYS_DAC_TIM, &tb);

    /* 更新事件引到 TRGO，DAC 每个定时周期转换一次 */
    TIM_SelectOutputTrigger(SYS_DAC_TIM, SYS_DAC_TRGO_SOURCE);
    TIM_Cmd(SYS_DAC_TIM, ENABLE);

    return 0U;
}

static void dac_tim_stop(void)
{
    TIM_Cmd(SYS_DAC_TIM, DISABLE);
}


/* 基础功能（直写电压） */
uint8_t SYS_DAC_Init(uint8_t ch)
{
    DAC_InitTypeDef di;

    if (!dac_valid(ch)) return 1U;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);

    dac_pin_init(ch);

    DAC_StructInit(&di);
    di.DAC_Trigger          = DAC_Trigger_None;     /* 直写模式：不要触发 */
    di.DAC_WaveGeneration   = DAC_WaveGeneration_None;
    di.DAC_OutputBuffer     = DAC_OutputBuffer_Enable;  /* 开输出缓冲，能带负载 */
    di.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    DAC_Init(dac_channel(ch), &di);

    DAC_Cmd(dac_channel(ch), ENABLE);
    /* 初始输出 0V */
    dac_write_data(ch, 0U);

    sys_dac_inited[ch - 1U] = 1U;
    sys_dac_wave[ch - 1U]   = 0U;

    return 0U;
}

void SYS_DAC_Stop(uint8_t ch)
{
    if (!dac_valid(ch)) return;
    if (sys_dac_inited[ch - 1U] == 0U) return;   /* 没开过就不重复关 */

    SYS_DAC_WaveStop(ch);
    DAC_Cmd(dac_channel(ch), DISABLE);
    sys_dac_inited[ch - 1U] = 0U;
}

uint8_t SYS_DAC_IsInited(uint8_t ch)
{
    if (!dac_valid(ch)) return 0U;
    return sys_dac_inited[ch - 1U];
}

uint8_t SYS_DAC_IsWaveOn(uint8_t ch)
{
    if (!dac_valid(ch)) return 0U;
    return sys_dac_wave[ch - 1U];
}

void SYS_DAC_SetValue(uint8_t ch, uint16_t value)
{
    if (!dac_valid(ch)) return;
    if (sys_dac_inited[ch - 1U] == 0U) return;   /* 必须先 Init */
    if (value > 4095U) value = 4095U;

    dac_write_data(ch, value);
}

uint16_t SYS_DAC_GetValue(uint8_t ch)
{
    if (!dac_valid(ch)) return 0U;

    return (uint16_t)DAC_GetDataOutputValue(dac_channel(ch));
}

void SYS_DAC_SetMilliVolt(uint8_t ch, uint32_t millivolt)
{
    uint32_t v;

    if (!dac_valid(ch)) return;

    if (millivolt > SYS_DAC_VREF_MV) millivolt = SYS_DAC_VREF_MV;

    v = (millivolt * 4095UL + (SYS_DAC_VREF_MV / 2UL)) / SYS_DAC_VREF_MV;

    SYS_DAC_SetValue(ch, (uint16_t)v);
}

uint32_t SYS_DAC_ValueToMilliVolt(uint16_t value)
{
    if (value > 4095U) value = 4095U;

    return ((uint32_t)value * SYS_DAC_VREF_MV + 2047UL) / 4095UL;
}

uint16_t SYS_DAC_MilliVoltToValue(uint32_t millivolt)
{
    if (millivolt > SYS_DAC_VREF_MV) millivolt = SYS_DAC_VREF_MV;

    return (uint16_t)((millivolt * 4095UL + (SYS_DAC_VREF_MV / 2UL)) / SYS_DAC_VREF_MV);
}


/* 扩展功能（波形） */
uint8_t SYS_DAC_TriangleInit(uint8_t ch, uint32_t freq_hz, uint16_t amplitude)
{
    DAC_InitTypeDef di;

    if (!dac_valid(ch)) return 1U;
    if (amplitude == 0U) amplitude = 4095U;

    /* 三角波一周期 2×amplitude 个台阶，触发频率 = 频率 × 2 × 台阶数 */
    if (dac_tim_init(freq_hz * 2UL * (uint32_t)amplitude) != 0U) return 2U;

    DAC_StructInit(&di);
    di.DAC_Trigger        = DAC_Trigger_T6_TRGO;
    di.DAC_WaveGeneration = DAC_WaveGeneration_Triangle;
    di.DAC_OutputBuffer   = DAC_OutputBuffer_Enable;
    di.DAC_LFSRUnmask_TriangleAmplitude = (uint32_t)amplitude;

    DAC_Init(dac_channel(ch), &di);
    DAC_Cmd(dac_channel(ch), ENABLE);

    sys_dac_inited[ch - 1U] = 1U;
    sys_dac_wave[ch - 1U]   = 1U;

    return 0U;
}

uint8_t SYS_DAC_NoiseInit(uint8_t ch, uint32_t freq_hz, uint32_t unmask_bits)
{
    DAC_InitTypeDef di;

    if (!dac_valid(ch)) return 1U;
    if (dac_tim_init(freq_hz) != 0U) return 2U;

    DAC_StructInit(&di);
    di.DAC_Trigger        = DAC_Trigger_T6_TRGO;
    di.DAC_WaveGeneration = DAC_WaveGeneration_Noise;
    di.DAC_OutputBuffer   = DAC_OutputBuffer_Enable;
    di.DAC_LFSRUnmask_TriangleAmplitude = unmask_bits;

    DAC_Init(dac_channel(ch), &di);
    DAC_Cmd(dac_channel(ch), ENABLE);

    sys_dac_inited[ch - 1U] = 1U;
    sys_dac_wave[ch - 1U]   = 1U;

    return 0U;
}

uint8_t SYS_DAC_DmaInit(uint8_t ch, const uint16_t *buf, uint16_t len, uint32_t freq_hz)
{
    DAC_InitTypeDef di;
    DMA_InitTypeDef dst;

    if (!dac_valid(ch) || buf == 0 || len == 0U) return 1U;

    /* 触发频率 = 波形频率 × 点数（每个点占一次触发） */
    if (dac_tim_init(freq_hz * (uint32_t)len) != 0U) return 2U;

    /* DMA 通道：DAC1 固定映射 DMA1_Stream5_CH7（F4 硬件映射，不可改） */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
    DMA_DeInit(SYS_DAC1_DMA_STREAM);
    while (DMA_GetCmdStatus(SYS_DAC1_DMA_STREAM) != DISABLE) { }

    DMA_StructInit(&dst);
    dst.DMA_Channel            = SYS_DAC1_DMA_CHANNEL;
    dst.DMA_PeripheralBaseAddr = (uint32_t)&(DAC->DHR12R1);   /* DAC1 的数据寄存器 */
    dst.DMA_Memory0BaseAddr    = (uint32_t)buf;
    dst.DMA_DIR                = DMA_DIR_MemoryToPeripheral;
    dst.DMA_BufferSize         = len;
    dst.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;   /* 外设地址固定 */
    dst.DMA_MemoryInc          = DMA_MemoryInc_Enable;        /* 内存地址递进 */
    dst.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dst.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dst.DMA_Mode               = DMA_Mode_Circular;           /* 循环播放 */
    dst.DMA_Priority           = DMA_Priority_High;
    dst.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    DMA_Init(SYS_DAC1_DMA_STREAM, &dst);

    /* DAC：触发选 TIM6，关波形发生器，数据由 DMA 提供 */
    DAC_StructInit(&di);
    di.DAC_Trigger        = DAC_Trigger_T6_TRGO;
    di.DAC_WaveGeneration = DAC_WaveGeneration_None;
    di.DAC_OutputBuffer   = DAC_OutputBuffer_Enable;
    DAC_Init(dac_channel(ch), &di);
    DAC_Cmd(dac_channel(ch), ENABLE);

    sys_dac_inited[ch - 1U] = 1U;
    sys_dac_wave[ch - 1U]   = 1U;
    sys_dac_len [ch - 1U]   = len;

    return 0U;
}

void SYS_DAC_DmaStart(uint8_t ch)
{
    if (!dac_valid(ch)) return;
    if (sys_dac_len[ch - 1U] == 0U) return;

    DMA_Cmd(SYS_DAC1_DMA_STREAM, ENABLE);
    DAC_DMACmd(dac_channel(ch), ENABLE);
    DAC_Cmd(dac_channel(ch), ENABLE);
}

void SYS_DAC_DmaStop(uint8_t ch)
{
    if (!dac_valid(ch)) return;

    DAC_DMACmd(dac_channel(ch), DISABLE);
    DMA_Cmd(SYS_DAC1_DMA_STREAM, DISABLE);
}

uint8_t SYS_DAC_SineInit(uint8_t ch, uint32_t freq_hz)
{
    /* 频率 × 32 点不能超过 TIM6 触发上限，约 1MHz */
    return SYS_DAC_DmaInit(ch, sys_dac_sine, SYS_DAC_SINE_POINTS, freq_hz);
}

void SYS_DAC_WaveStop(uint8_t ch)
{
    DAC_InitTypeDef di;

    if (!dac_valid(ch)) return;
    if (sys_dac_wave[ch - 1U] == 0U) return;     /* 本来就没在发波 */

    /* 先关 DMA 与触发源，再关波形发生器 */
    DAC_DMACmd(dac_channel(ch), DISABLE);
    DMA_Cmd(SYS_DAC1_DMA_STREAM, DISABLE);
    dac_tim_stop();

    /* 回到直写配置：触发 None + 波形 None */
    DAC_StructInit(&di);
    di.DAC_Trigger        = DAC_Trigger_None;
    di.DAC_WaveGeneration = DAC_WaveGeneration_None;
    di.DAC_OutputBuffer   = DAC_OutputBuffer_Enable;
    DAC_Init(dac_channel(ch), &di);
    DAC_Cmd(dac_channel(ch), ENABLE);

    sys_dac_wave[ch - 1U] = 0U;
    sys_dac_len [ch - 1U] = 0U;
}

const uint16_t *SYS_DAC_GetSineTable(void)
{
    return sys_dac_sine;
}
