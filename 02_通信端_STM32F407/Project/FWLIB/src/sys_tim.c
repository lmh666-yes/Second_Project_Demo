#include "sys_tim.h"
/* 接口说明见 sys_tim.h；本文件为实现层 */
#include "gpio_core.h"

/* 通用定时器(TIM)模块实现，14 个定时器的中断向量在本文件定义并分发到回调，均为弱定义。
 * 频率换算：定时器时钟经 PSC 分频得计数时钟，计数时钟 / ARR 计数次数 = PWM 频率。
 * 定时器时钟：TIM2~7/12~14 挂 APB1，TIM1/8~11 挂 APB2；APB 分频系数 ≠ 1 时
 * 定时器时钟 = PCLK × 2（见 STM32F4 参考手册时钟树章节）。 */

/* 独立向量：TIM2/3/4/5、TIM6_DAC、TIM7。共享向量只分发本库定时器的中断，
 * 其余来源直接返回：TIM1_UP_TIM10 与 TIM8_UP_TIM13 两个定时器都分发，
 * TIM1_BRK_TIM9、TIM1_TRG_COM_TIM11、TIM8_BRK_TIM12、TIM8_TRG_COM_TIM14 各一个。
 * TIM1_CC_IRQHandler / TIM8_CC_IRQHandler：TIM1/TIM8 捕获与比较向量。 */


/* 内部配置表 */

/* 定时器编号（SYS_TIM_x 枚举顺序，也是数组下标）到硬件资源的映射。
 * 增删定时器时改本表，项数由下方编译期护栏核对。 */
typedef struct {
    TIM_TypeDef *tim;     /* 外设指针（TIM1~TIM14） */
    uint32_t     clk;     /* 对应 RCC 时钟位 */
    IRQn_Type    irq;     /* 中断向量号 */
    uint8_t      apb2;    /* 1 = APB2，0 = APB1 */
} TimCfg_t;

static const TimCfg_t tim_cfg[SYS_TIM_COUNT] = {
    { TIM1,  RCC_APB2Periph_TIM1,  TIM1_UP_TIM10_IRQn,        1 },
    { TIM2,  RCC_APB1Periph_TIM2,  TIM2_IRQn,                 0 },
    { TIM3,  RCC_APB1Periph_TIM3,  TIM3_IRQn,                 0 },
    { TIM4,  RCC_APB1Periph_TIM4,  TIM4_IRQn,                 0 },
    { TIM5,  RCC_APB1Periph_TIM5,  TIM5_IRQn,                 0 },
    { TIM6,  RCC_APB1Periph_TIM6,  TIM6_DAC_IRQn,             0 },
    { TIM7,  RCC_APB1Periph_TIM7,  TIM7_IRQn,                 0 },
    { TIM8,  RCC_APB2Periph_TIM8,  TIM8_UP_TIM13_IRQn,        1 },
    { TIM9,  RCC_APB2Periph_TIM9,  TIM1_BRK_TIM9_IRQn,        1 },
    { TIM10, RCC_APB2Periph_TIM10, TIM1_UP_TIM10_IRQn,        1 },
    { TIM11, RCC_APB2Periph_TIM11, TIM1_TRG_COM_TIM11_IRQn,   1 },
    { TIM12, RCC_APB1Periph_TIM12, TIM8_BRK_TIM12_IRQn,       0 },
    { TIM13, RCC_APB1Periph_TIM13, TIM8_UP_TIM13_IRQn,        0 },
    { TIM14, RCC_APB1Periph_TIM14, TIM8_TRG_COM_TIM14_IRQn,   0 },
};

/* 编译期护栏：配置表项数必须与 SYS_TIM_COUNT 一致 */
typedef char tim_cfg_count_check[(sizeof(tim_cfg) / sizeof(tim_cfg[0]) == SYS_TIM_COUNT) ? 1 : -1];

/* 定时中断回调表（SYS_TIM_InitIT 注册） */
static void (*tim_callback[SYS_TIM_COUNT])(void);

/* 输入捕获回调表（SYS_TIM_CaptureInitIT 注册）与当前捕获信道（0 = 未启用） */
static void (*tim_cap_callback[SYS_TIM_COUNT])(uint32_t value);
static uint8_t tim_cap_ch[SYS_TIM_COUNT];

/* 输出比较中断回调表（SYS_TIM_OcInitIT 注册）与当前比较信道（0 = 未启用） */
static void (*tim_oc_callback[SYS_TIM_COUNT])(void);
static uint8_t tim_oc_ch[SYS_TIM_COUNT];


/* 内部辅助 */

/* 读取定时器计数时钟(Hz)：所在 APB 分频 ≠ 1 时定时器时钟 = PCLK × 2。
 * apb2：1 = APB2，0 = APB1 */
static uint32_t tim_get_clock(uint8_t apb2)
{
    RCC_ClocksTypeDef c;

    RCC_GetClocksFreq(&c);

    if (apb2) {
        if ((RCC->CFGR & RCC_CFGR_PPRE2) != 0U) return c.PCLK2_Frequency * 2U;
        return c.PCLK2_Frequency;
    }

    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U) return c.PCLK1_Frequency * 2U;
    return c.PCLK1_Frequency;
}

/* 由目标频率计算 PSC / ARR：PSC 寄存器值 = 分频系数 - 1，ARR = 计数次数 - 1。
 * 计数次数上限 65536（ARR 为 16 位），分频向上取整避免低频端 ARR 越界。 */
static void tim_calc_psc_arr(uint32_t clk, uint32_t freq_hz, uint32_t *psc, uint32_t *arr)
{
    uint32_t div;
    uint32_t unit;

    if (freq_hz < 1U) freq_hz = 1U;

    if (freq_hz > clk / 65536U) {
        div = 1U;                           /* 高频区：不分频 ARR 也够小（并防乘法溢出） */
    } else {
        unit = freq_hz * 65536UL;           /* 该分支内乘积 ≤ clk，不会溢出 */
        div  = (clk + unit - 1U) / unit;    /* 向上取整：保证计数次数 ≤ 65536 */
        if (div == 0U) div = 1U;            /* 理论不可达，保险 */
    }

    *psc = div - 1U;
    *arr = clk / (freq_hz * div);
    if (*arr == 0U) *arr = 1U;              /* 超高频保护 */
    *arr -= 1U;
}

/* 引脚掩码到复用编号的换算由 gpio_core 的 GPIO_PinSource 提供 */

/* 按通道号初始化输出比较（含 CCR 预装载） */
static void tim_oc_init(TIM_TypeDef *tim, uint8_t ch_idx, TIM_OCInitTypeDef *oc)
{
    switch (ch_idx) {
        case 0:  TIM_OC1Init(tim, oc); TIM_OC1PreloadConfig(tim, TIM_OCPreload_Enable); break;
        case 1:  TIM_OC2Init(tim, oc); TIM_OC2PreloadConfig(tim, TIM_OCPreload_Enable); break;
        case 2:  TIM_OC3Init(tim, oc); TIM_OC3PreloadConfig(tim, TIM_OCPreload_Enable); break;
        default: TIM_OC4Init(tim, oc); TIM_OC4PreloadConfig(tim, TIM_OCPreload_Enable); break;
    }
}

/* 按通道号写比较值（CCR，即占空比对应的计数） */
static void tim_set_ccr(TIM_TypeDef *tim, uint8_t ch_idx, uint32_t ccr)
{
    switch (ch_idx) {
        case 0:  TIM_SetCompare1(tim, ccr); break;
        case 1:  TIM_SetCompare2(tim, ccr); break;
        case 2:  TIM_SetCompare3(tim, ccr); break;
        default: TIM_SetCompare4(tim, ccr); break;
    }
}

/* 按通道号读比较值（CCR）：读 CCR 会顺带清捕获标志 */
static uint32_t tim_get_ccr(TIM_TypeDef *tim, uint8_t ch_idx)
{
    switch (ch_idx) {
        case 0:  return TIM_GetCapture1(tim);
        case 1:  return TIM_GetCapture2(tim);
        case 2:  return TIM_GetCapture3(tim);
        default: return TIM_GetCapture4(tim);
    }
}

/* 运行中改频率的公共实现（PwmSetFreq 与 OcSetFreq 共用）
 * 重算 PSC/ARR 并直写寄存器，不重写 CR1/CR2；各通道 CCR 等比缩放，
 * 新值 = 旧值 × 新量程 ÷ 旧量程，占空比与相位占比不变；写后发更新事件立即装载。
 * 需定时器已启动；TIM6/7 无输出通道。缩放读全部 CCR，混用输入捕获时会清捕获标志。 */
static void tim_apply_freq(SysTimId_t id, uint32_t freq_hz)
{
    const TimCfg_t *p;
    TIM_TypeDef    *tim;
    uint32_t        old_arr;
    uint32_t        psc;
    uint32_t        arr;
    uint8_t         i;

    if (id >= SYS_TIM_COUNT || freq_hz == 0U) return;

    p   = &tim_cfg[id];
    tim = p->tim;

    if (tim == TIM6 || tim == TIM7) return;          /* 基本定时器无通道 */
    if ((tim->CR1 & TIM_CR1_CEN) == 0U) return;      /* 未启动(未 Init) → 不动 */

    old_arr = tim->ARR;
    tim_calc_psc_arr(tim_get_clock(p->apb2), freq_hz, &psc, &arr);

    /* CCR 等比缩放：两因子均 ≤ 65536，乘积上界 < 2^32，无溢出风险 */
    for (i = 0; i < 4U; i++) {
        uint32_t ccr = tim_get_ccr(tim, i);
        tim_set_ccr(tim, i, ccr * (arr + 1U) / (old_arr + 1U));
    }

    tim->PSC = (uint16_t)psc;
    tim->ARR = (uint16_t)arr;
    TIM_GenerateEvent(tim, TIM_EventSource_Update);  /* 立即装载新 PSC/ARR/CCR */
}

/* 信道号(1~4) → 标准库枚举/标志位/中断位/极性配置 四个小映射（输入捕获用） */
static uint16_t cap_channel(uint8_t ch)
{
    switch (ch) {
        case 1:  return TIM_Channel_1;
        case 2:  return TIM_Channel_2;
        case 3:  return TIM_Channel_3;
        default: return TIM_Channel_4;
    }
}

static uint16_t cap_flag(uint8_t ch)
{
    switch (ch) {
        case 1:  return TIM_FLAG_CC1;
        case 2:  return TIM_FLAG_CC2;
        case 3:  return TIM_FLAG_CC3;
        default: return TIM_FLAG_CC4;
    }
}

static uint16_t cap_it(uint8_t ch)
{
    switch (ch) {
        case 1:  return TIM_IT_CC1;
        case 2:  return TIM_IT_CC2;
        case 3:  return TIM_IT_CC3;
        default: return TIM_IT_CC4;
    }
}

static void cap_polarity(TIM_TypeDef *tim, uint8_t ch, uint16_t polarity)
{
    switch (ch) {
        case 1:  TIM_OC1PolarityConfig(tim, polarity); break;
        case 2:  TIM_OC2PolarityConfig(tim, polarity); break;
        case 3:  TIM_OC3PolarityConfig(tim, polarity); break;
        default: TIM_OC4PolarityConfig(tim, polarity); break;
    }
}


/* 基础功能：PWM */

/* PWM 初始化：时钟 → 引脚 → 时基 → PWM 模式 */
void SYS_TIM_PwmInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                     uint8_t af, uint32_t freq_hz)
{
    const TimCfg_t *p;
    TIM_TimeBaseInitTypeDef tb;
    TIM_OCInitTypeDef       oc;
    GPIO_InitTypeDef        gi;
    uint32_t psc;
    uint32_t arr;
    uint8_t  ch_idx;

    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U || port == 0) return;
    if (tim_cfg[id].tim == TIM6 || tim_cfg[id].tim == TIM7) return;   /* 基本定时器无输出通道 */

    p = &tim_cfg[id];
    ch_idx = (uint8_t)(ch - 1U);

    /* 1) 时钟：定时器(按总线分支) + 引脚端口 */
    if (p->apb2) RCC_APB2PeriphClockCmd(p->clk, ENABLE);
    else         RCC_APB1PeriphClockCmd(p->clk, ENABLE);
    GPIO_ClockEnable(port);

    /* 2) 引脚：复用推挽输出(GPIO_OType_PP)，带上拉防悬空 */
    GPIO_PinAFConfig(port, GPIO_PinSource(pin), af);
    gi.GPIO_Pin   = pin;
    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(port, &gi);

    /* 3) 时基：按目标频率换算并装载 PSC / ARR */
    tim_calc_psc_arr(tim_get_clock(p->apb2), freq_hz, &psc, &arr);

    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Prescaler         = (uint16_t)psc;
    tb.TIM_Period            = (uint16_t)arr;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(p->tim, &tb);

    /* 4) 输出比较：PWM1 模式(宏 TIM_OCMode_PWM1)，初始占空比 0（输出保持低） */
    TIM_OCStructInit(&oc);
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OCPolarity  = TIM_OCPolarity_High;
    oc.TIM_Pulse       = 0;
    tim_oc_init(p->tim, ch_idx, &oc);

    /* 5) 自动重装预装载 */
    TIM_ARRPreloadConfig(p->tim, ENABLE);

    /* 6) 高级定时器(TIM1/TIM8)还要开主输出使能(MOE)，否则通道不出波形 */
    if (p->tim == TIM1 || p->tim == TIM8) TIM_CtrlPWMOutputs(p->tim, ENABLE);

    /* 7) 启动计数 */
    TIM_Cmd(p->tim, ENABLE);
}

/* 设置占空比（千分比 → CCR 计数） */
void SYS_TIM_PwmSetDuty(SysTimId_t id, uint8_t ch, uint16_t permille)
{
    const TimCfg_t *p;
    uint32_t ccr;
    uint8_t  ch_idx;

    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return;

    p = &tim_cfg[id];
    ch_idx = (uint8_t)(ch - 1U);

    if (permille > 1000U) permille = 1000U;

    /* CCR = 一个周期的计数次数(ARR+1) × 千分比 / 1000 */
    ccr = ((uint32_t)p->tim->ARR + 1U) * (uint32_t)permille / 1000U;
    tim_set_ccr(p->tim, ch_idx, ccr);
}

/* 停止该通道（占空比归零） */
void SYS_TIM_PwmStop(SysTimId_t id, uint8_t ch)
{
    SYS_TIM_PwmSetDuty(id, ch, 0);
}

/* 读取当前占空比（千分比）
 * 返回：0 ~ 1000，参数越界返回 0 */
uint16_t SYS_TIM_PwmGetDuty(SysTimId_t id, uint8_t ch)
{
    const TimCfg_t *p;
    uint8_t         ch_idx;

    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return 0;

    p      = &tim_cfg[id];
    ch_idx = (uint8_t)(ch - 1U);

    return (uint16_t)(((uint32_t)tim_get_ccr(p->tim, ch_idx) * 1000U) /
                      ((uint32_t)p->tim->ARR + 1U));
}

/* 运行中改 PWM 频率，占空比保持不变；需先 PwmInit 且定时器已启动。
 * 实现与注意事项见 sys_tim.h 声明处 */
void SYS_TIM_PwmSetFreq(SysTimId_t id, uint32_t freq_hz)
{
    tim_apply_freq(id, freq_hz);
}


/* 基础功能：定时中断 */
void SYS_TIM_InitIT(SysTimId_t id, uint32_t freq_hz, void (*callback)(void))
{
    const TimCfg_t *p;
    TIM_TimeBaseInitTypeDef tb;
    NVIC_InitTypeDef        ni;
    uint32_t psc;
    uint32_t arr;

    if (id >= SYS_TIM_COUNT) return;

    p = &tim_cfg[id];

    if (p->apb2) RCC_APB2PeriphClockCmd(p->clk, ENABLE);
    else         RCC_APB1PeriphClockCmd(p->clk, ENABLE);

    tim_calc_psc_arr(tim_get_clock(p->apb2), freq_hz, &psc, &arr);

    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Prescaler         = (uint16_t)psc;
    tb.TIM_Period            = (uint16_t)arr;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(p->tim, &tb);

    /* 更新中断 + NVIC 使能 */
    TIM_ClearITPendingBit(p->tim, TIM_IT_Update);
    TIM_ITConfig(p->tim, TIM_IT_Update, ENABLE);

    ni.NVIC_IRQChannel                   = p->irq;
    ni.NVIC_IRQChannelPreemptionPriority = SYS_TIM_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_TIM_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);

    tim_callback[id] = callback;

    TIM_Cmd(p->tim, ENABLE);
}

/* 停止定时器（计数停止 + 关更新中断） */
void SYS_TIM_Stop(SysTimId_t id)
{
    if (id >= SYS_TIM_COUNT) return;

    TIM_Cmd(tim_cfg[id].tim, DISABLE);
    TIM_ITConfig(tim_cfg[id].tim, TIM_IT_Update, DISABLE);
}


/* 扩展功能：定时器触发输出（TRGO 到 ADC 采样） */
/* 每个周期输出一次 TRGO(更新事件)。只有 TIM2/3/8 连到 ADC 触发选择器
 * （RM0090 触发映射表），其余定时器有 TRGO 也不通 ADC */
void SYS_TIM_TrgoInit(SysTimId_t id, uint32_t freq_hz)
{
    const TimCfg_t *p;
    uint32_t psc;
    uint32_t arr;

    if (id >= SYS_TIM_COUNT || freq_hz == 0U) return;

    p = &tim_cfg[id];

    /* 只支持 TIM2 / TIM3 / TIM8（芯片内部触发线固定映射） */
    if (p->tim != TIM2 && p->tim != TIM3 && p->tim != TIM8) return;

    if (p->apb2) RCC_APB2PeriphClockCmd(p->clk, ENABLE);
    else         RCC_APB1PeriphClockCmd(p->clk, ENABLE);

    TIM_Cmd(p->tim, DISABLE);          /* 先停，重配时基与触发源 */

    tim_calc_psc_arr(tim_get_clock(p->apb2), freq_hz, &psc, &arr);
    p->tim->PSC = (uint16_t)psc;
    p->tim->ARR = (uint16_t)arr;
    TIM_GenerateEvent(p->tim, TIM_EventSource_Update);      /* 立即装载 */

    TIM_SelectOutputTrigger(p->tim, TIM_TRGOSource_Update); /* 每周期发一次 TRGO */

    TIM_Cmd(p->tim, ENABLE);
}

/* 中断统一处理：捕获/更新 标志 → 清标志 → 执行回调 */
static void tim_isr(SysTimId_t id)
{
    TIM_TypeDef *tim = tim_cfg[id].tim;

    /* 输入捕获：捕到一个边沿 → 清标志，读 CCR（顺带清 CCxIF）交回调 */
    if ((tim_cap_callback[id] != 0) && (tim_cap_ch[id] != 0)) {
        uint16_t it = cap_it(tim_cap_ch[id]);

        if (TIM_GetITStatus(tim, it) != RESET) {
            TIM_ClearITPendingBit(tim, it);
            tim_cap_callback[id](tim_get_ccr(tim, (uint8_t)(tim_cap_ch[id] - 1U)));
        }
    }

    /* 输出比较(中断版)：一次匹配 → 清标志，执行回调 */
    if ((tim_oc_callback[id] != 0) && (tim_oc_ch[id] != 0)) {
        uint16_t it = cap_it(tim_oc_ch[id]);

        if (TIM_GetITStatus(tim, it) != RESET) {
            TIM_ClearITPendingBit(tim, it);
            tim_oc_callback[id]();
        }
    }

    /* 更新中断：数满一轮 → 周期回调 */
    if (TIM_GetITStatus(tim, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(tim, TIM_IT_Update);

        if (tim_callback[id] != 0) tim_callback[id]();
    }
}

/* 本组 ISR 均为弱定义(__weak)，用户手写同名强定义即可顶替库版本。
 * 共存条件：库 ISR 为 __weak，启动文件兜底桩为 [WEAK]，链接器已配
 * --muldefweak --diag_suppress=L6439W（模板工程已内置，手动建工程时加在 Misc）。
 * 顶替后该定时器的库回调（SYS_TIM_InitIT 注册的 callback）不再工作。 */
__weak void TIM2_IRQHandler(void) { tim_isr(SYS_TIM_2); }
__weak void TIM3_IRQHandler(void) { tim_isr(SYS_TIM_3); }
__weak void TIM4_IRQHandler(void) { tim_isr(SYS_TIM_4); }
__weak void TIM5_IRQHandler(void) { tim_isr(SYS_TIM_5); }
__weak void TIM6_DAC_IRQHandler(void) { tim_isr(SYS_TIM_6); }   /* 只处理 TIM6，DAC 部分不动 */
__weak void TIM7_IRQHandler(void) { tim_isr(SYS_TIM_7); }

/* 共享向量：每个只分发本库定时器的更新中断，其余来源直接返回 */
__weak void TIM1_UP_TIM10_IRQHandler(void)      { tim_isr(SYS_TIM_1);  tim_isr(SYS_TIM_10); }
__weak void TIM1_BRK_TIM9_IRQHandler(void)      { tim_isr(SYS_TIM_9); }
__weak void TIM1_TRG_COM_TIM11_IRQHandler(void) { tim_isr(SYS_TIM_11); }
__weak void TIM8_BRK_TIM12_IRQHandler(void)     { tim_isr(SYS_TIM_12); }
__weak void TIM8_UP_TIM13_IRQHandler(void)      { tim_isr(SYS_TIM_8);  tim_isr(SYS_TIM_13); }
__weak void TIM8_TRG_COM_TIM14_IRQHandler(void) { tim_isr(SYS_TIM_14); }

/* 捕获/比较向量：TIM1/TIM8 的输入捕获与输出比较中断走这两个，更新中断走上方 UP 向量 */
__weak void TIM1_CC_IRQHandler(void) { tim_isr(SYS_TIM_1); }
__weak void TIM8_CC_IRQHandler(void) { tim_isr(SYS_TIM_8); }


/* 扩展功能：舵机 / 发声 */
/* 舵机初始化：50Hz PWM，初始 90° */
void SYS_TIM_ServoInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin, uint8_t af)
{
    SYS_TIM_PwmInit(id, ch, port, pin, af, SYS_TIM_SERVO_FREQ_HZ);
    SYS_TIM_ServoSetAngle(id, ch, 90);
}

/* 设置舵机角度：角度 → 脉宽(500~2500us) → CCR
 * 入参范围 0 ~ 180（度），超出按 180 处理 */
void SYS_TIM_ServoSetAngle(SysTimId_t id, uint8_t ch, uint16_t angle)
{
    const TimCfg_t *p;
    uint32_t pulse_us;
    uint32_t period_us;
    uint32_t ccr;
    uint8_t  ch_idx;

    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return;

    p = &tim_cfg[id];
    ch_idx = (uint8_t)(ch - 1U);

    if (angle > 180U) angle = 180U;

/* 角度 → 脉宽：0° = 500us，180° = 2500us，线性映射 */
    pulse_us = SYS_TIM_SERVO_MIN_US +
               ((uint32_t)angle * (SYS_TIM_SERVO_MAX_US - SYS_TIM_SERVO_MIN_US)) / 180U;

    /* 脉宽(us) → CCR：CCR = (ARR+1) × 脉宽 / 周期；50Hz 周期 = 20000us */
    period_us = 1000000UL / SYS_TIM_SERVO_FREQ_HZ;
    ccr = ((uint32_t)p->tim->ARR + 1U) * pulse_us / period_us;

    tim_set_ccr(p->tim, ch_idx, ccr);
}

/* 发声通道初始化：1kHz PWM（占空比 0 = 静音） */
void SYS_TIM_ToneInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin, uint8_t af)
{
    SYS_TIM_PwmInit(id, ch, port, pin, af, 1000U);
}

/* 播放指定频率方波（50% 占空比）
 * 需先调用 SYS_TIM_ToneInit 完成引脚与定时器初始化 */
void SYS_TIM_TonePlay(SysTimId_t id, uint8_t ch, uint32_t freq_hz)
{
    const TimCfg_t *p;

    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return;

    p = &tim_cfg[id];

    if (freq_hz == 0U) {                         /* 0 → 停止发声 */
        SYS_TIM_PwmStop(id, ch);
        return;
    }

    /* 定时器尚未启动(未 Init)时直接返回，避免写到未配置的寄存器 */
    if ((p->tim->CR1 & TIM_CR1_CEN) == 0U) return;

    /* 重算 PSC/ARR 并立即装载新频率（内部同时等比缩放各通道 CCR） */
    SYS_TIM_PwmSetFreq(id, freq_hz);

    SYS_TIM_PwmSetDuty(id, ch, 500);             /* 50% 方波 */
}

/* 指定频率 + 占空比（‰）发声，用于无源蜂鸣器调节音量与音色 */
void SYS_TIM_TonePlayDuty(SysTimId_t id, uint8_t ch, uint32_t freq_hz, uint16_t duty_permille)
{
    const TimCfg_t *p;

    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return;

    p = &tim_cfg[id];

    if (freq_hz == 0U) {                         /* 0 → 停止发声 */
        SYS_TIM_PwmStop(id, ch);
        return;
    }

    /* 定时器尚未启动(未 Init)时直接返回，避免写到未配置的寄存器 */
    if ((p->tim->CR1 & TIM_CR1_CEN) == 0U) return;

    if (duty_permille > 1000U) duty_permille = 1000U;

    SYS_TIM_PwmSetFreq(id, freq_hz);
    SYS_TIM_PwmSetDuty(id, ch, duty_permille);
}

/* 停止发声 */
void SYS_TIM_ToneStop(SysTimId_t id, uint8_t ch)
{
    SYS_TIM_PwmStop(id, ch);
}


/* 扩展功能：外部脉冲计数（ETR） */
/* ETR 公共配置：时钟 → 引脚复用 → 时基 → 外部时钟 → 启动
 * 计数时钟来自 ETR 引脚脉冲，PSC 固定 0（不分频）；收到 period_n 个脉冲
 * 计数器归零一轮，产生一次更新事件。通路 Mode1 / Mode2 由宏
 * SYS_TIM_ETR_CLKMODE 选择（默认 2，即 Mode2Config） */
static void tim_etr_setup(SysTimId_t id, GPIO_TypeDef *port, uint16_t pin,
                          uint8_t af, uint32_t period_n)
{
    const TimCfg_t *p;
    TIM_TimeBaseInitTypeDef tb;
    GPIO_InitTypeDef gi;

    if (id >= SYS_TIM_COUNT || port == 0 || period_n == 0U) return;
    if (period_n > 65536U) period_n = 65536U;   /* ARR 为 16 位，计数次数上限 65536 */

    p = &tim_cfg[id];

    /* 1) 时钟：定时器(按总线分支) + 引脚端口 */
    if (p->apb2) RCC_APB2PeriphClockCmd(p->clk, ENABLE);
    else         RCC_APB1PeriphClockCmd(p->clk, ENABLE);
    GPIO_ClockEnable(port);

    /* 2) 引脚：复用为 TIMx_ETR，带上拉，悬空输入会乱计数 */
    GPIO_PinAFConfig(port, GPIO_PinSource(pin), af);
    gi.GPIO_Pin   = pin;
    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(port, &gi);

    /* 3) 时基：ARR = period_n-1（数满一轮自动归零），PSC = 0 */
    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Prescaler         = 0;
    tb.TIM_Period            = (uint16_t)(period_n - 1U);
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(p->tim, &tb);

    /* 4) ETR 外部时钟：脉冲作为计数时钟。通路选择与滤波强度见头文件宏；
     *    Mode2Config（默认）= ECE 直通，Mode1Config = 经触发控制器（SMS + TS） */
#if (SYS_TIM_ETR_CLKMODE == 1)
    TIM_ETRClockMode1Config(p->tim, TIM_ExtTRGPSC_OFF,
                            TIM_ExtTRGPolarity_NonInverted, SYS_TIM_ETR_FILTER);
#else
    TIM_ETRClockMode2Config(p->tim, TIM_ExtTRGPSC_OFF,
                            TIM_ExtTRGPolarity_NonInverted, SYS_TIM_ETR_FILTER);
#endif

    /* 5) 启动计数 */
    TIM_Cmd(p->tim, ENABLE);
}

/* 外部脉冲计数初始化（纯计数，无中断回调） */
void SYS_TIM_EtrInit(SysTimId_t id, GPIO_TypeDef *port, uint16_t pin,
                     uint8_t af, uint32_t period_n)
{
    tim_etr_setup(id, port, pin, af, period_n);

    if (id < SYS_TIM_COUNT) tim_callback[id] = 0;   /* 纯计数：清掉旧回调 */
}

/* 外部脉冲计数初始化 + 中断回调（每 period_n 个脉冲回调一次） */
void SYS_TIM_EtrInitIT(SysTimId_t id, GPIO_TypeDef *port, uint16_t pin,
                       uint8_t af, uint32_t period_n, void (*callback)(void))
{
    NVIC_InitTypeDef ni;

    if (id >= SYS_TIM_COUNT) return;

    /* 空回调 → 降级为纯计数 */
    if (callback == 0) {
        SYS_TIM_EtrInit(id, port, pin, af, period_n);
        return;
    }

    tim_etr_setup(id, port, pin, af, period_n);

    /* 更新中断 + NVIC + 回调，与 SYS_TIM_InitIT 同一套机制 */
    TIM_ClearITPendingBit(tim_cfg[id].tim, TIM_IT_Update);
    TIM_ITConfig(tim_cfg[id].tim, TIM_IT_Update, ENABLE);

    ni.NVIC_IRQChannel                   = tim_cfg[id].irq;
    ni.NVIC_IRQChannelPreemptionPriority = SYS_TIM_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_TIM_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);

    tim_callback[id] = callback;
}

/* 读本轮已收到的脉冲数（计数满 period_n 后归零重数） */
uint32_t SYS_TIM_EtrCount(SysTimId_t id)
{
    if (id >= SYS_TIM_COUNT) return 0;

    return TIM_GetCounter(tim_cfg[id].tim);
}

/* 清零脉冲计数 */
void SYS_TIM_EtrReset(SysTimId_t id)
{
    if (id >= SYS_TIM_COUNT) return;

    TIM_SetCounter(tim_cfg[id].tim, 0);
}


/* 扩展功能：输入捕获（Input Capture） */
/* 公共配置：时钟 → 引脚复用(输入) → 时基 → 信道捕获模式 → 启动
 * ARR 固定 0xFFFF（16 位满量程，计满从 0 重数）；PSC 按 tick_hz 换算，捕获值 ÷ tick_hz = 时间(秒) */
static void tim_cap_setup(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                          uint8_t af, uint16_t polarity, uint32_t tick_hz)
{
    const TimCfg_t *p;
    TIM_TimeBaseInitTypeDef tb;
    TIM_ICInitTypeDef       ic;
    GPIO_InitTypeDef        gi;
    uint32_t psc_div;

    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U || port == 0 || tick_hz == 0U) return;
    if (tim_cfg[id].tim == TIM6 || tim_cfg[id].tim == TIM7) return;   /* 基本定时器无捕获信道 */

    p = &tim_cfg[id];

    /* 1) 时钟：定时器(按总线分支) + 引脚端口 */
    if (p->apb2) RCC_APB2PeriphClockCmd(p->clk, ENABLE);
    else         RCC_APB1PeriphClockCmd(p->clk, ENABLE);
    GPIO_ClockEnable(port);

    /* 2) 引脚：复用为 TIMx_CHx 输入，带上拉，悬空输入会乱捕获 */
    GPIO_PinAFConfig(port, GPIO_PinSource(pin), af);
    gi.GPIO_Pin   = pin;
    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(port, &gi);

    /* 3) 时基：PSC 按 tick_hz 换算，ARR = 0xFFFF（满量程） */
    psc_div = tim_get_clock(p->apb2) / tick_hz;
    if (psc_div < 1U)     psc_div = 1U;
    if (psc_div > 65536U) psc_div = 65536U;

    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Prescaler         = (uint16_t)(psc_div - 1U);
    tb.TIM_Period            = 0xFFFFU;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(p->tim, &tb);

    /* 4) 信道：输入捕获，引脚直连且不分频；滤波强度见头文件宏 */
    TIM_ICStructInit(&ic);
    ic.TIM_Channel     = cap_channel(ch);
    ic.TIM_ICPolarity  = polarity;
    ic.TIM_ICSelection = TIM_ICSelection_DirectTI;
    ic.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    ic.TIM_ICFilter    = SYS_TIM_IC_FILTER;
    TIM_ICInit(p->tim, &ic);

    /* 5) 启动计数 */
    TIM_Cmd(p->tim, ENABLE);
}

/* 输入捕获初始化（纯轮询，无中断） */
void SYS_TIM_CaptureInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                         uint8_t af, uint16_t polarity, uint32_t tick_hz)
{
    tim_cap_setup(id, ch, port, pin, af, polarity, tick_hz);

    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return;

    /* 纯轮询版：清回调；该定时器若之前开过捕获中断，按旧信道关掉 */
    if (tim_cap_ch[id] != 0) TIM_ITConfig(tim_cfg[id].tim, cap_it(tim_cap_ch[id]), DISABLE);
    tim_cap_callback[id] = 0;
    tim_cap_ch[id]       = 0;
}

/* 输入捕获初始化 + 中断回调（每捕到一个边沿调用 callback(捕获值)） */
void SYS_TIM_CaptureInitIT(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                           uint8_t af, uint16_t polarity, uint32_t tick_hz,
                           void (*callback)(uint32_t value))
{
    NVIC_InitTypeDef ni;
    IRQn_Type irq;

    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return;

    /* 空回调 → 降级为纯轮询版 */
    if (callback == 0) {
        SYS_TIM_CaptureInit(id, ch, port, pin, af, polarity, tick_hz);
        return;
    }

    tim_cap_setup(id, ch, port, pin, af, polarity, tick_hz);

    /* 6) 捕获中断：CCx 标志 → NVIC → 回调；TIM1/8 走单独的 _CC 向量 */
    TIM_ClearITPendingBit(tim_cfg[id].tim, cap_it(ch));
    TIM_ITConfig(tim_cfg[id].tim, cap_it(ch), ENABLE);

    if      (tim_cfg[id].tim == TIM1) irq = TIM1_CC_IRQn;
    else if (tim_cfg[id].tim == TIM8) irq = TIM8_CC_IRQn;
    else                              irq = tim_cfg[id].irq;

    ni.NVIC_IRQChannel                   = irq;
    ni.NVIC_IRQChannelPreemptionPriority = SYS_TIM_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_TIM_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);

    tim_cap_ch[id]       = ch;
    tim_cap_callback[id] = callback;
}

/* 查捕获标志（1 = 有新捕获值） */
uint8_t SYS_TIM_CaptureFlag(SysTimId_t id, uint8_t ch)
{
    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return 0;

    return (TIM_GetFlagStatus(tim_cfg[id].tim, cap_flag(ch)) != RESET) ? 1U : 0U;
}

/* 读捕获值（CCRx）；读操作会顺带清掉捕获标志，硬件行为 */
uint32_t SYS_TIM_CaptureGet(SysTimId_t id, uint8_t ch)
{
    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return 0;

    return tim_get_ccr(tim_cfg[id].tim, (uint8_t)(ch - 1U));
}

/* 清捕获标志，用于不读值只清标志的场合，如丢弃干扰 */
void SYS_TIM_CaptureClear(SysTimId_t id, uint8_t ch)
{
    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return;

    TIM_ClearFlag(tim_cfg[id].tim, cap_flag(ch));
}

/* 动态切换捕获边沿（Rising 与 Falling 之间）；BothEdge 在 CaptureInit 时配置 */
void SYS_TIM_CaptureSetPolarity(SysTimId_t id, uint8_t ch, uint16_t polarity)
{
    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return;

    cap_polarity(tim_cfg[id].tim, ch, polarity);
}


/* 扩展功能：输出比较（Output Compare） */
/* 公共配置：时钟 → 引脚复用(可省) → 时基 → 输出比较模式 → 启动
 * cycle_hz 是计数一轮(ARR+1 个数)的频率，匹配事件固定在每轮第 ccr 个计数处；port 传 0 只产生比较事件 */
static void tim_oc_setup(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                         uint8_t af, uint16_t oc_mode, uint32_t cycle_hz, uint32_t ccr)
{
    const TimCfg_t *p;
    TIM_TimeBaseInitTypeDef tb;
    TIM_OCInitTypeDef       oc;
    GPIO_InitTypeDef        gi;
    uint32_t psc;
    uint32_t arr;

    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U || cycle_hz == 0U) return;
    if (tim_cfg[id].tim == TIM6 || tim_cfg[id].tim == TIM7) return;   /* 基本定时器无信道 */

    p = &tim_cfg[id];

    /* 1) 时钟：定时器(按总线分支)；port == 0 表示纯比较事件，不配引脚 */
    if (p->apb2) RCC_APB2PeriphClockCmd(p->clk, ENABLE);
    else         RCC_APB1PeriphClockCmd(p->clk, ENABLE);

    /* 2) 引脚：复用推挽输出(GPIO_OType_PP) */
    if (port != 0) {
        GPIO_ClockEnable(port);
        GPIO_PinAFConfig(port, GPIO_PinSource(pin), af);
        gi.GPIO_Pin   = pin;
        gi.GPIO_Mode  = GPIO_Mode_AF;
        gi.GPIO_OType = GPIO_OType_PP;
        gi.GPIO_Speed = GPIO_Speed_100MHz;
        gi.GPIO_PuPd  = GPIO_PuPd_UP;
        GPIO_Init(port, &gi);
    }

    /* 3) 时基：按"一轮计数频率"(cycle_hz) 换算 PSC/ARR（同 PWM 算法） */
    tim_calc_psc_arr(tim_get_clock(p->apb2), cycle_hz, &psc, &arr);

    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Prescaler         = (uint16_t)psc;
    tb.TIM_Period            = (uint16_t)arr;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(p->tim, &tb);

    /* 4) 输出比较：六种模式任选，ccr 超出整轮时按整轮封顶 */
    if (ccr > (uint32_t)arr) ccr = (uint32_t)arr;

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode      = oc_mode;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OCPolarity  = TIM_OCPolarity_High;
    oc.TIM_Pulse       = ccr;
    tim_oc_init(p->tim, (uint8_t)(ch - 1U), &oc);

    /* 5) 自动重装预装载 */
    TIM_ARRPreloadConfig(p->tim, ENABLE);

    /* 6) 高级定时器(TIM1/TIM8)开主输出使能，纯比较事件下无副作用 */
    if (p->tim == TIM1 || p->tim == TIM8) TIM_CtrlPWMOutputs(p->tim, ENABLE);

    /* 7) 启动计数 */
    TIM_Cmd(p->tim, ENABLE);
}

/* 输出比较初始化（六种模式任选，无中断） */
void SYS_TIM_OcInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                    uint8_t af, uint16_t oc_mode, uint32_t cycle_hz, uint32_t ccr)
{
    tim_oc_setup(id, ch, port, pin, af, oc_mode, cycle_hz, ccr);

    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return;

    /* 非中断版：同一信道若之前开过比较中断，中断与回调一起清除 */
    if (tim_oc_ch[id] == ch) {
        TIM_ITConfig(tim_cfg[id].tim, cap_it(ch), DISABLE);
        tim_oc_callback[id] = 0;
        tim_oc_ch[id]       = 0;
    }
}

/* 输出比较初始化 + 中断回调（每次匹配事件调用 callback） */
void SYS_TIM_OcInitIT(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                      uint8_t af, uint16_t oc_mode, uint32_t cycle_hz, uint32_t ccr,
                      void (*callback)(void))
{
    NVIC_InitTypeDef ni;
    IRQn_Type irq;

    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return;

    /* 空回调 → 降级为无中断版 */
    if (callback == 0) {
        SYS_TIM_OcInit(id, ch, port, pin, af, oc_mode, cycle_hz, ccr);
        return;
    }

    tim_oc_setup(id, ch, port, pin, af, oc_mode, cycle_hz, ccr);

    /* 8) 比较中断：CCx 标志 → NVIC → 回调；TIM1/8 走单独的 _CC 向量 */
    TIM_ClearITPendingBit(tim_cfg[id].tim, cap_it(ch));
    TIM_ITConfig(tim_cfg[id].tim, cap_it(ch), ENABLE);

    if      (tim_cfg[id].tim == TIM1) irq = TIM1_CC_IRQn;
    else if (tim_cfg[id].tim == TIM8) irq = TIM8_CC_IRQn;
    else                              irq = tim_cfg[id].irq;

    ni.NVIC_IRQChannel                   = irq;
    ni.NVIC_IRQChannelPreemptionPriority = SYS_TIM_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_TIM_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);

    tim_oc_ch[id]       = ch;
    tim_oc_callback[id] = callback;
}

/* 改比较值（CCRx）；超出整轮时按整轮封顶 */
void SYS_TIM_OcSetCompare(SysTimId_t id, uint8_t ch, uint32_t ccr)
{
    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return;

    if (ccr > (uint32_t)tim_cfg[id].tim->ARR) ccr = (uint32_t)tim_cfg[id].tim->ARR;
    tim_set_ccr(tim_cfg[id].tim, (uint8_t)(ch - 1U), ccr);
}

/* 运行中改一轮频率（翻转方波频率 = cycle_hz ÷ 2）
 * 实现与注意事项见 sys_tim.h 声明处 */
void SYS_TIM_OcSetFreq(SysTimId_t id, uint32_t cycle_hz)
{
    tim_apply_freq(id, cycle_hz);
}

/* 停该信道输出（关信道 + 关比较中断，其它信道不受影响） */
void SYS_TIM_OcStop(SysTimId_t id, uint8_t ch)
{
    if (id >= SYS_TIM_COUNT || ch < 1U || ch > 4U) return;

    TIM_CCxCmd(tim_cfg[id].tim, cap_channel(ch), TIM_CCx_Disable);

    if (tim_oc_ch[id] == ch) {
        TIM_ITConfig(tim_cfg[id].tim, cap_it(ch), DISABLE);
        tim_oc_callback[id] = 0;
        tim_oc_ch[id]       = 0;
    }
}
