#include "sys_clock.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */

/* ================================================================
 *  sys_clock.c —— 【系统】时钟源切换模块  实现文件
 * ================================================================
 *  核心目标：在"任何时刻"安全地改变芯片主频——
 *  原则是"先降速、再改配置、后升速"，全程不产生取指/总线异常。
 * ================================================================ */

/* ================================================================
 *                    配置数据
 * ================================================================ */
/* PLL 频率"惰性记录"：
 *   上电时 SystemCoreClock = 168MHz（PLL 档）——本模块在首次切换前
 *   把它捕获保存。因为一旦切到低速源，SystemCoreClock 就变了，
 *   之后再想查询"PLL 档频率是多少"就无从得知，故提前留一份。 */
static uint32_t sys_clk_pll_freq = 0;

/* 时钟源配置表：每行描述某档时钟源的"完整参数包"
 *   freq          —— 该档频率（PLL 档动态记录，填 0 占位）
 *   flash_latency —— Flash 等待周期：主频越高需要越多等待周期，
 *                    168MHz 需 5 个（否则高速取指可能出错）
 *   hclk_div / pclk1_div / pclk2_div —— 总线分频
 *                    （APB1 上限 42MHz → ÷4；APB2 上限 84MHz → ÷2）
 *   sws_code      —— SWS 状态位中代表该时钟源的编码（切换完成判据）
 * 数值来源：STM32F407 数据手册"最大时钟频率"章节 */
typedef struct {
    SysClkSrc_t src;
    uint32_t    freq;
    uint32_t    flash_latency;
    uint32_t    hclk_div;
    uint32_t    pclk1_div;
    uint32_t    pclk2_div;
    uint32_t    sws_code;
} SysClkProfile_t;

static const SysClkProfile_t clock_profiles[] = {
    /* HSI：16MHz，低速运行，Flash 无需等待周期，总线不分频 */
    { SYS_CLK_HSI, SYS_CLK_FREQ_HSI, FLASH_Latency_0, RCC_SYSCLK_Div1, RCC_HCLK_Div1, RCC_HCLK_Div1, RCC_SYSCLKSource_HSI },
    /* HSE：8MHz，晶振直连，低速低功耗 */
    { SYS_CLK_HSE, SYS_CLK_FREQ_HSE, FLASH_Latency_0, RCC_SYSCLK_Div1, RCC_HCLK_Div1, RCC_HCLK_Div1, RCC_SYSCLKSource_HSE },
    /* PLL：168MHz（默认档），Flash 5 等待周期，APB1÷4=42MHz，APB2÷2=84MHz */
    { SYS_CLK_PLL, 0,                FLASH_Latency_5, RCC_SYSCLK_Div1, RCC_HCLK_Div4, RCC_HCLK_Div2, RCC_SYSCLKSource_PLLCLK },
};

#define SYS_CLK_PROFILE_COUNT  (sizeof(clock_profiles) / sizeof(clock_profiles[0]))

/* 编译期护栏：三档时钟源各自对应一行配置（增删档位时此处会提醒同步） */
typedef char sys_clk_profile_count_check[(SYS_CLK_PROFILE_COUNT == 3U) ? 1 : -1];


/* ================================================================
 *                    内部辅助
 * ================================================================ */
/* 按枚举值查配置表；找不到返回 NULL（调用方需要判空） */
static const SysClkProfile_t *sys_clk_lookup(SysClkSrc_t src)
{
    for (uint8_t i = 0; i < (uint8_t)SYS_CLK_PROFILE_COUNT; i++) {
        if (clock_profiles[i].src == src) return &clock_profiles[i];
    }
    return NULL;
}

/* 启动 HSI 并等待其稳定（HSIRDY 置位）
 * HSI 是芯片内部 RC，永远可用——因此作为"安全过渡源"和"保底时钟" */
static void sys_clk_start_hsi(void)
{
    RCC_HSICmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET);
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* ================================================================
 * 切换时钟源（模块核心函数）
 * ================================================================
 * 安全切换思路（为什么需要"三步走"）:
 *   高速运行中直接改 Flash 等待周期/总线分频，可能取指失败甚至死机，
 *   因此采用"先降速、再改配置、后升速"的流程：
 *     ① 先切到 HSI（16MHz 安全源）——此时改动任何配置都安全；
 *     ② 在低速状态下改 latency、分频，并启动目标时钟源；
 *     ③ 最后写 SW 位切到目标源，等 SWS 确认后更新 SystemCoreClock。
 *
 * 返回值：SYS_CLK_OK 或错误码（见 sys_clock.h）
 * ================================================================ */
uint8_t SYS_CLK_Switch(SysClkSrc_t target)
{
    /* ① 惰性记录 PLL 频率（只在首次调用时真正执行一次） */
    if (sys_clk_pll_freq == 0) {
        sys_clk_pll_freq = SystemCoreClock;
    }

    /* ② 查配置表：非法目标直接报错 */
    const SysClkProfile_t *p = sys_clk_lookup(target);
    if (p == NULL) return SYS_CLK_ERR_UNKNOWN;

#if SYS_CLK_SAFE_TRANSITION
    /* ③ 安全过渡：若目标不是 HSI 且当前不在 HSI，先切到 HSI */
    if (target != SYS_CLK_HSI && RCC_GetSYSCLKSource() != RCC_SYSCLKSource_HSI) {
        sys_clk_start_hsi();
        RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);
        while (RCC_GetSYSCLKSource() != RCC_SYSCLKSource_HSI);
    }
#endif

    /* ④ 低速区安全改 latency：先降到最低档（此刻降速是安全的） */
    FLASH_SetLatency(FLASH_Latency_0);

    /* ⑤ 分频器也先复位到 ÷1（低速下同样安全） */
    RCC_HCLKConfig (RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div1);
    RCC_PCLK2Config(RCC_HCLK_Div1);

    /* ⑥ 按目标开启对应时钟源，并等待其就绪 */
    switch (target) {
        case SYS_CLK_HSI:
            sys_clk_start_hsi();                    /* 等 HSIRDY */
            break;
        case SYS_CLK_HSE:
            RCC_HSEConfig(RCC_HSE_ON);
            if (RCC_WaitForHSEStartUp() != SUCCESS) return SYS_CLK_ERR_HSE;
            break;                                  /* HSE 起振失败报错返回 */
        case SYS_CLK_PLL:
            RCC_PLLCmd(ENABLE);
            while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);  /* 等锁定 */
            break;
        default:
            return SYS_CLK_ERR_UNKNOWN;
    }

    /* ⑦ 按目标频率重设 latency 与分频（升速前的必要准备） */
    FLASH_SetLatency(p->flash_latency);
    RCC_HCLKConfig (p->hclk_div);
    RCC_PCLK1Config(p->pclk1_div);
    RCC_PCLK2Config(p->pclk2_div);

    /* ⑧ 写 SW 位：把系统时钟切换到目标源 */
    switch (target) {
        case SYS_CLK_HSI: RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);    break;
        case SYS_CLK_HSE: RCC_SYSCLKConfig(RCC_SYSCLKSource_HSE);    break;
        case SYS_CLK_PLL: RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK); break;
        default: return SYS_CLK_ERR_UNKNOWN;
    }

    /* ⑨ 等待 SWS 硬件确认（带超时，防止异常时死等） */
    uint32_t timeout = SYS_CLK_SWITCH_TIMEOUT;
    while (RCC_GetSYSCLKSource() != p->sws_code) {
        if (--timeout == 0) return SYS_CLK_ERR_SW;
    }

    /* ⑩ 更新 CMSIS 全局频率变量（依赖它的延时/计时会同步修正） */
    SystemCoreClockUpdate();
    return SYS_CLK_OK;
}

/* 读 SWS 状态位 → 时钟源枚举；未知编码按 HSI 兜底（正常不会发生） */
SysClkSrc_t SYS_CLK_GetSource(void)
{
    switch (RCC_GetSYSCLKSource()) {
        case RCC_SYSCLKSource_HSI:    return SYS_CLK_HSI;
        case RCC_SYSCLKSource_HSE:    return SYS_CLK_HSE;
        case RCC_SYSCLKSource_PLLCLK: return SYS_CLK_PLL;
        default:                      return SYS_CLK_HSI;
    }
}

/* 当前 SYSCLK 频率：PLL 档返回记录值，其它档查表返回 */
uint32_t SYS_CLK_GetFreq(void)
{
    SysClkSrc_t src = SYS_CLK_GetSource();
    if (src == SYS_CLK_PLL) return SYS_CLK_GetPllFreq();

    const SysClkProfile_t *p = sys_clk_lookup(src);
    return (p != NULL) ? p->freq : 0U;
}

/* PLL 档频率：优先返回惰性记录值；记录尚未建立时读 SystemCoreClock */
uint32_t SYS_CLK_GetPllFreq(void)
{
    return (sys_clk_pll_freq != 0) ? sys_clk_pll_freq : SystemCoreClock;
}


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 便捷封装：切到高性能档（PLL 168MHz） */
uint8_t SYS_CLK_ToHighSpeed(void)
{
    return SYS_CLK_Switch(SYS_CLK_PLL);
}

/* 便捷封装：切到低功耗档（HSI 16MHz）
 * 注意 : 按约定切换后需重新 SYS_TICK_Init() 校准时基（见模块头说明） */
uint8_t SYS_CLK_ToLowPower(void)
{
    return SYS_CLK_Switch(SYS_CLK_HSI);
}


/* ================================================================
 *           区块 3：总线分频自定义与查询（扩展功能）
 * ================================================================ */
/* 分频常量 → 实际分频数 的对照表（查不到 = 传入非法常量） */
typedef struct {
    uint32_t code;      /* SPL 分频常量 */
    uint32_t div;       /* 实际分频数   */
} SysClkDiv_t;

/* AHB 分频（HPRE）：RCC_SYSCLK_Divx，最大 /512 */
static const SysClkDiv_t sys_clk_ahb_div_tbl[] = {
    { RCC_SYSCLK_Div1,   1UL   },
    { RCC_SYSCLK_Div2,   2UL   },
    { RCC_SYSCLK_Div4,   4UL   },
    { RCC_SYSCLK_Div8,   8UL   },
    { RCC_SYSCLK_Div16,  16UL  },
    { RCC_SYSCLK_Div64,  64UL  },
    { RCC_SYSCLK_Div128, 128UL },
    { RCC_SYSCLK_Div256, 256UL },
    { RCC_SYSCLK_Div512, 512UL },
};

/* APB 分频（PPRE1/PPRE2）：RCC_HCLK_Divx，最大 /16 */
static const SysClkDiv_t sys_clk_apb_div_tbl[] = {
    { RCC_HCLK_Div1,  1UL  },
    { RCC_HCLK_Div2,  2UL  },
    { RCC_HCLK_Div4,  4UL  },
    { RCC_HCLK_Div8,  8UL  },
    { RCC_HCLK_Div16, 16UL },
};

#define SYS_CLK_AHB_DIV_COUNT  (sizeof(sys_clk_ahb_div_tbl) / sizeof(sys_clk_ahb_div_tbl[0]))
#define SYS_CLK_APB_DIV_COUNT  (sizeof(sys_clk_apb_div_tbl) / sizeof(sys_clk_apb_div_tbl[0]))

/* 编译期护栏：两表档位数固定（增删时提醒同步） */
typedef char sys_clk_ahb_div_check[(SYS_CLK_AHB_DIV_COUNT == 9U) ? 1 : -1];
typedef char sys_clk_apb_div_check[(SYS_CLK_APB_DIV_COUNT == 5U) ? 1 : -1];

/* 查表：常量 → 分频数；非法常量返回 0 */
static uint32_t sys_clk_div_lookup(const SysClkDiv_t *tbl, uint32_t count, uint32_t code)
{
    for (uint8_t i = 0U; i < (uint8_t)count; i++) {
        if (tbl[i].code == code) return tbl[i].div;
    }
    return 0U;
}

/* 读回三条总线频率（Hz） */
void SYS_CLK_GetBusFreq(uint32_t *hclk, uint32_t *pclk1, uint32_t *pclk2)
{
    RCC_ClocksTypeDef c;

    RCC_GetClocksFreq(&c);

    if (hclk  != NULL) *hclk  = c.HCLK_Frequency;
    if (pclk1 != NULL) *pclk1 = c.PCLK1_Frequency;
    if (pclk2 != NULL) *pclk2 = c.PCLK2_Frequency;
}

/* 自定义总线分频（预演校验 → 降 HSI → 改分频 → 切回原源） */
uint8_t SYS_CLK_SetBusDiv(uint32_t hclk_div, uint32_t pclk1_div, uint32_t pclk2_div)
{
    uint32_t    hdiv;
    uint32_t    p1div;
    uint32_t    p2div;
    uint32_t    fsrc;
    uint32_t    fhclk;
    uint32_t    fp1;
    uint32_t    fp2;
    SysClkSrc_t cur;

    /* ① 分频常量查表（非法值直接拒绝） */
    hdiv  = sys_clk_div_lookup(sys_clk_ahb_div_tbl, (uint32_t)SYS_CLK_AHB_DIV_COUNT, hclk_div);
    p1div = sys_clk_div_lookup(sys_clk_apb_div_tbl, (uint32_t)SYS_CLK_APB_DIV_COUNT, pclk1_div);
    p2div = sys_clk_div_lookup(sys_clk_apb_div_tbl, (uint32_t)SYS_CLK_APB_DIV_COUNT, pclk2_div);
    if ((hdiv == 0U) || (p1div == 0U) || (p2div == 0U)) return SYS_CLK_ERR_DIV;

    /* ② 按当前主频预演结果，超上限直接拒绝（不动硬件） */
    fsrc  = SYS_CLK_GetFreq();
    fhclk = fsrc / hdiv;
    fp1   = fhclk / p1div;
    fp2   = fhclk / p2div;
    if (fhclk > SYS_CLK_MAX_HCLK)  return SYS_CLK_ERR_RANGE;
    if (fp1   > SYS_CLK_MAX_PCLK1) return SYS_CLK_ERR_RANGE;
    if (fp2   > SYS_CLK_MAX_PCLK2) return SYS_CLK_ERR_RANGE;

    /* ③ 安全过渡：先降到 HSI（16MHz 下改分频绝对安全） */
    cur = SYS_CLK_GetSource();
    if (cur != SYS_CLK_HSI) {
        sys_clk_start_hsi();
        RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);
        while (RCC_GetSYSCLKSource() != RCC_SYSCLKSource_HSI);
    }

    /* ④ 写三个分频寄存器 */
    RCC_HCLKConfig(hclk_div);
    RCC_PCLK1Config(pclk1_div);
    RCC_PCLK2Config(pclk2_div);

    /* ⑤ 切回原来的时钟源（带超时确认） */
    if (cur != SYS_CLK_HSI) {
        uint32_t timeout = SYS_CLK_SWITCH_TIMEOUT;
        uint32_t sws;

        if (cur == SYS_CLK_PLL) {
            RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
            sws = RCC_SYSCLKSource_PLLCLK;
        } else {
            RCC_SYSCLKConfig(RCC_SYSCLKSource_HSE);
            sws = RCC_SYSCLKSource_HSE;
        }
        while (RCC_GetSYSCLKSource() != sws) {
            if (--timeout == 0U) return SYS_CLK_ERR_SW;
        }
    }

    SystemCoreClockUpdate();
    return SYS_CLK_OK;
}


/* ================================================================
 *        扩展功能：LSE / LSI / RTC 时钟源（sys_rtc 的底座）
 * ================================================================ */
/* 启动 LSE 并等起振（超时上限同 SYS_CLK_SWITCH_TIMEOUT） */
uint8_t SYS_CLK_LseOn(void)
{
    uint32_t timeout = SYS_CLK_SWITCH_TIMEOUT;

    RCC_LSEConfig(RCC_LSE_ON);
    while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET) {
        if (timeout-- == 0U) return SYS_CLK_ERR_LSE;
    }
    return SYS_CLK_OK;
}

uint8_t SYS_CLK_LseReady(void)
{
    return (RCC_GetFlagStatus(RCC_FLAG_LSERDY) != RESET) ? 1U : 0U;
}

uint8_t SYS_CLK_LsiReady(void)
{
    return (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) != RESET) ? 1U : 0U;
}

/* 启动 LSI 并等就绪 */
uint8_t SYS_CLK_LsiOn(void)
{
    uint32_t timeout = SYS_CLK_SWITCH_TIMEOUT;

    RCC_LSICmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET) {
        if (timeout-- == 0U) return SYS_CLK_ERR_LSI;
    }
    return SYS_CLK_OK;
}

/* 选 RTC 时钟源并启用 */
void SYS_CLK_RtcClkSelect(uint32_t src)
{
    RCC_RTCCLKConfig(src);
    RCC_RTCCLKCmd(ENABLE);
}

