#include "sys_encoder.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"

/* ================================================================
 *  sys_encoder.c —— 正交编码器接口（TIM 编码器模式 TIM_EncoderMode_TI12 / _TI1）  实现文件
 * ================================================================
 *  为什么用硬件编码器接口而不是"外部中断 + 软件判相"：
 *      100 线编码器 4 倍频后每转 400 个脉冲，3000rpm 时就是 20kHz 边沿——
 *      软件中断根本扛不住（CPU 全被中断吃光）；
 *      TIM 编码器模式是硬件自己加减计数，CPU 只负责"隔 10ms 读一次"。
 *
 *  16 位回绕的处理（本文件关键之一）：
 *      TIM 计数器只有 16 位，跑到 65535 会回 0。
 *      做法：每次读时算 (now - last) 的**16 位差值**，再用符号扩展还原
 *      "实际走了多少" —— 这样只要相邻两次读数之间变化不超过 ±32767，
 *      就永远不会漏数，而且不用中断。
 * ================================================================ */


/* ================================================================
 *                      实例状态
 * ================================================================ */
typedef struct {
    TIM_TypeDef *tim;
    uint8_t      used;
    uint8_t      invert;
    uint16_t     last_raw;      /* 上次读到的 16 位原始计数 */
    int32_t      total;         /* 累计（32 位带符号） */
} EncState_t;

static EncState_t enc[SYS_ENCODER_MAX];


/* SysTimId_t → TIM_TypeDef*（只列带编码器接口的通用/高级定时器） */
static TIM_TypeDef *enc_tim_of(SysTimId_t id)
{
    switch (id) {
        case SYS_TIM_1: return TIM1;
        case SYS_TIM_2: return TIM2;
        case SYS_TIM_3: return TIM3;
        case SYS_TIM_4: return TIM4;
        case SYS_TIM_5: return TIM5;
        case SYS_TIM_8: return TIM8;
        default:        return 0;
    }
}

/* 开对应定时器的总线时钟 */
static void enc_clk_enable(TIM_TypeDef *t)
{
    if      (t == TIM1) RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
    else if (t == TIM8) RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM8, ENABLE);
    else if (t == TIM2) RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    else if (t == TIM3) RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    else if (t == TIM4) RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    else if (t == TIM5) RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5, ENABLE);
}

static uint8_t enc_valid(SysEncoderId_t id)
{
    return ((uint8_t)id < SYS_ENCODER_MAX && enc[id].used != 0U) ? 1U : 0U;
}

/* 读 16 位计数，按 invert 取反 */
static uint16_t enc_read_raw(SysEncoderId_t id)
{
    uint16_t v = (uint16_t)TIM_GetCounter(enc[id].tim);
    return enc[id].invert ? (uint16_t)(0xFFFFU - v) : v;
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
uint8_t SYS_ENCODER_Init(SysEncoderId_t id, SysTimId_t tim,
                         GPIO_TypeDef *ch1_port, uint16_t ch1_pin, uint8_t ch1_af,
                         GPIO_TypeDef *ch2_port, uint16_t ch2_pin, uint8_t ch2_af,
                         uint8_t invert)
{
    TIM_TimeBaseInitTypeDef tb;
    GPIO_InitTypeDef gi;
    TIM_TypeDef *t;

    if ((uint8_t)id >= SYS_ENCODER_MAX)     return 1U;
    if (ch1_port == 0 || ch2_port == 0)     return 1U;

    t = enc_tim_of(tim);
    if (t == 0) return 1U;

    /* ① 引脚：复用输入 + 上拉（编码器多为开集/开漏输出，内部上拉更稳） */
    GPIO_ClockEnable(ch1_port);
    GPIO_ClockEnable(ch2_port);

    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;

    gi.GPIO_Pin = ch1_pin;
    GPIO_PinAFConfig(ch1_port, (uint8_t)GPIO_PinSource(ch1_pin), ch1_af);
    GPIO_Init(ch1_port, &gi);

    gi.GPIO_Pin = ch2_pin;
    GPIO_PinAFConfig(ch2_port, (uint8_t)GPIO_PinSource(ch2_pin), ch2_af);
    GPIO_Init(ch2_port, &gi);

    /* ② 时基：满分频、最大重装（编码器模式不关心"频率"，只关心计数范围） */
    enc_clk_enable(t);
    TIM_DeInit(t);

    tb.TIM_Prescaler         = 0;
    tb.TIM_Period            = 0xFFFFU;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;   /* 编码器模式下由硬件接管 */
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(t, &tb);

    /* ③ 编码器接口：CH1=A 相、CH2=B 相，都按上升沿采样（Ti12 双边沿计数） */
    TIM_EncoderInterfaceConfig(t, SYS_ENCODER_MODE,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);

    /* ④ 输入滤波：编码器线长/电机噪声大时把滤波档位调大更稳
     *    0x0F = 采样频率 fDTS/32、连续 8 次一致才认（最稳） */
    TIM_ICInitTypeDef ic;
    TIM_ICStructInit(&ic);
    ic.TIM_Channel     = TIM_Channel_1;
    ic.TIM_ICFilter    = 0x0F;
    TIM_ICInit(t, &ic);
    ic.TIM_Channel     = TIM_Channel_2;
    TIM_ICInit(t, &ic);

    TIM_SetCounter(t, 0);
    TIM_Cmd(t, ENABLE);

    /* ⑤ 记录实例状态 */
    enc[id].tim      = t;
    enc[id].used     = 1U;
    enc[id].invert   = invert ? 1U : 0U;
    enc[id].last_raw = enc_read_raw(id);
    enc[id].total    = 0;

    return 0U;
}

/* 读计数（16 位有符号扩展） */
int32_t SYS_ENCODER_GetCount(SysEncoderId_t id)
{
    if (!enc_valid(id)) return 0;
    return (int32_t)(int16_t)enc_read_raw(id);
}

/* 读"两次调用之间"的增量（内部累计 + 回绕安全） */
int32_t SYS_ENCODER_GetDelta(SysEncoderId_t id)
{
    uint16_t now;
    int16_t  d;

    if (!enc_valid(id)) return 0;

    now = enc_read_raw(id);

    /* 关键：用"16 位差值 + 符号扩展"，自动处理 65535→0 的回绕 */
    d = (int16_t)(now - enc[id].last_raw);

    enc[id].last_raw  = now;
    enc[id].total    += d;

    return (int32_t)d;
}

int32_t SYS_ENCODER_GetTotal(SysEncoderId_t id)
{
    if (!enc_valid(id)) return 0;

    /* 先同步一次未读走的增量，保证 total 反映"到此刻为止" */
    (void)SYS_ENCODER_GetDelta(id);
    return enc[id].total;
}

uint8_t SYS_ENCODER_GetDir(SysEncoderId_t id)
{
    uint16_t now;
    int16_t  d;

    if (!enc_valid(id)) return 0;

    now = enc_read_raw(id);
    d   = (int16_t)(now - enc[id].last_raw);

    if (d != 0) return (d > 0) ? 1U : 0U;

    /* 本周期没动：退回问硬件"计数方向位"（TIM_CR1_DIR 位，1 = 向下计数） */
    return (enc[id].tim->CR1 & TIM_CR1_DIR) ? 0U : 1U;
}

void SYS_ENCODER_Reset(SysEncoderId_t id)
{
    if (!enc_valid(id)) return;

    TIM_SetCounter(enc[id].tim, 0);
    enc[id].last_raw = enc_read_raw(id);
    enc[id].total    = 0;
}

void SYS_ENCODER_Enable(SysEncoderId_t id, uint8_t enable)
{
    if (!enc_valid(id)) return;

    if (enable) {
        TIM_Cmd(enc[id].tim, ENABLE);
        /* 重新使能时以当前值为基准，避免停用期间的"假大增量" */
        enc[id].last_raw = enc_read_raw(id);
    } else {
        TIM_Cmd(enc[id].tim, DISABLE);
    }
}


/* ================================================================
 *                    区块 3：换算
 * ================================================================ */
int32_t SYS_ENCODER_DeltaToCps(int32_t delta, uint16_t period_ms)
{
    if (period_ms == 0U) return 0;
    return (delta * 1000L) / (int32_t)period_ms;      /* 脉冲/秒 */
}

int32_t SYS_ENCODER_DeltaToRpm(int32_t delta, uint16_t lines, uint8_t multiple,
                               uint16_t period_ms)
{
    int32_t pulses_per_rev;
    int32_t cps;

    if (period_ms == 0U || lines == 0U || multiple == 0U) return 0;

    pulses_per_rev = (int32_t)lines * (int32_t)multiple;   /* 每转的脉冲数 */
    cps = SYS_ENCODER_DeltaToCps(delta, period_ms);        /* 脉冲/秒 */

    /* rpm = (脉冲/秒 ÷ 每转脉冲) × 60 —— 用整数运算，避免浮点 */
    return (cps * 60L) / pulses_per_rev;
}
