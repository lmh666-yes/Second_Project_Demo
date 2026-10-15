#include "sys_clock.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "sys_rtos.h"   /* SYS_RTOS_PRESENT / SYS_RTOS_SYSTICK_RELOAD() */
#include "sys_tick.h"   /* 裸机时 SYS_RTOS_SYSTICK_RELOAD() 会用到 SYS_TICK_Init */

/* sys_clock.c — 【系统】时钟源切换模块 实现文件
 * 运行中修改 SYSCLK 时按"先降速、再改配置、后升速"执行，
 * 避免改 Flash 等待周期与总线分频时取指异常。 */

/* ---- 配置数据 ---- */
/* PLL 档频率记录：上电时 SystemCoreClock = 168MHz，于首次切换前保存。
 * 切到低速源后 SystemCoreClock 随之改变，之后无法再查得 PLL 档频率。 */
static uint32_t sys_clk_pll_freq = 0;

/* 时钟源配置表：每行一档时钟源的完整参数
 *   freq          该档频率（PLL 档动态记录，填 0 占位）
 *   flash_latency Flash 等待周期，168MHz 需 5 个
 *   hclk_div/pclk1_div/pclk2_div  总线分频（APB1 上限 42MHz 故 ÷4，APB2 上限 84MHz 故 ÷2）
 *   sws_code      SWS 状态位编码，切换完成判据；数值来源：数据手册时钟章节 */
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
    /* HSI：16MHz，Flash 无等待周期，总线不分频 */
    { SYS_CLK_HSI, SYS_CLK_FREQ_HSI, FLASH_Latency_0, RCC_SYSCLK_Div1, RCC_HCLK_Div1, RCC_HCLK_Div1, RCC_SYSCLKSource_HSI },
    /* HSE：8MHz 外部晶振 */
    { SYS_CLK_HSE, SYS_CLK_FREQ_HSE, FLASH_Latency_0, RCC_SYSCLK_Div1, RCC_HCLK_Div1, RCC_HCLK_Div1, RCC_SYSCLKSource_HSE },
    /* PLL：168MHz 默认档，Flash 5 等待周期，APB1÷4=42MHz，APB2÷2=84MHz */
    { SYS_CLK_PLL, 0,                FLASH_Latency_5, RCC_SYSCLK_Div1, RCC_HCLK_Div4, RCC_HCLK_Div2, RCC_SYSCLKSource_PLLCLK },
};

#define SYS_CLK_PROFILE_COUNT  (sizeof(clock_profiles) / sizeof(clock_profiles[0]))

/* 编译期护栏：三档时钟源各一行配置，增删档位时在此报错提醒同步 */
typedef char sys_clk_profile_count_check[(SYS_CLK_PROFILE_COUNT == 3U) ? 1 : -1];


/* ---- 内部辅助 ---- */
/* 按枚举值查配置表；找不到返回 NULL，调用方须判空 */
static const SysClkProfile_t *sys_clk_lookup(SysClkSrc_t src)
{
    for (uint8_t i = 0; i < (uint8_t)SYS_CLK_PROFILE_COUNT; i++) {
        if (clock_profiles[i].src == src) return &clock_profiles[i];
    }
    return NULL;
}

/* 启动 HSI 并等待 HSIRDY 置位。HSI 为片内 RC，总是可用，作安全过渡源与保底时钟 */
static void sys_clk_start_hsi(void)
{
    uint32_t to = SYS_CLK_READY_TIMEOUT;

    RCC_HSICmd(ENABLE);
    /* RCC 外设时钟未开或时钟树配错时 HSIRDY 不置位，须带超时等待 */
    while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET) {
        if (--to == 0U) return;
    }
}


/* ---- 区块 2：基础功能 ---- */
/* 切换时钟源（模块核心函数）
 * 高速下直接改 Flash 等待周期与总线分频可能取指失败或死机，故按三步执行：
 * 先切 HSI（16MHz），再在低速下改 latency、分频并启动目标源，最后写 SW 位
 * 切换、等 SWS 确认后更新 SystemCoreClock。
 * 返回值：SYS_CLK_OK 或错误码（见 sys_clock.h） */
uint8_t SYS_CLK_Switch(SysClkSrc_t target)
{
    /* 首次调用时记录 PLL 档频率 */
    if (sys_clk_pll_freq == 0) {
        sys_clk_pll_freq = SystemCoreClock;
    }

    /* 查配置表，非法目标直接报错 */
    const SysClkProfile_t *p = sys_clk_lookup(target);
    if (p == NULL) return SYS_CLK_ERR_UNKNOWN;

#if SYS_CLK_SAFE_TRANSITION
    /* 安全过渡：目标非 HSI 且当前非 HSI 时先切到 HSI */
    if (target != SYS_CLK_HSI && RCC_GetSYSCLKSource() != RCC_SYSCLKSource_HSI) {
        uint32_t to = SYS_CLK_READY_TIMEOUT;

        sys_clk_start_hsi();
        RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);
        /* 退回安全源须带超时，否则状态位读不回时会停在初始化里 */
        while (RCC_GetSYSCLKSource() != RCC_SYSCLKSource_HSI) {
            if (--to == 0U) return SYS_CLK_ERR_SW;
        }
    }
#endif

    /* Flash latency 先降到最低档，低速下降速安全 */
    FLASH_SetLatency(FLASH_Latency_0);

    /* 分频器先复位到 ÷1 */
    RCC_HCLKConfig (RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div1);
    RCC_PCLK2Config(RCC_HCLK_Div1);

    /* 按目标启动对应时钟源并等就绪 */
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
            /* 等锁定须带超时：HSE 掉振或 PLL 配置非法时 PLLRDY 不置位 */
            {
                uint32_t to = SYS_CLK_READY_TIMEOUT;
                while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET) {
                    if (--to == 0U) return SYS_CLK_ERR_HSE;
                }
            }
            break;
        default:
            return SYS_CLK_ERR_UNKNOWN;
    }

    /* 按目标频率重设 latency 与分频，升速前必须完成 */
    FLASH_SetLatency(p->flash_latency);
    RCC_HCLKConfig (p->hclk_div);
    RCC_PCLK1Config(p->pclk1_div);
    RCC_PCLK2Config(p->pclk2_div);

    /* 写 SW 位切换到目标源 */
    switch (target) {
        case SYS_CLK_HSI: RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);    break;
        case SYS_CLK_HSE: RCC_SYSCLKConfig(RCC_SYSCLKSource_HSE);    break;
        case SYS_CLK_PLL: RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK); break;
        default: return SYS_CLK_ERR_UNKNOWN;
    }

    /* 等 SWS 硬件确认，带超时 */
    uint32_t timeout = SYS_CLK_SWITCH_TIMEOUT;
    while (RCC_GetSYSCLKSource() != p->sws_code) {
        if (--timeout == 0) return SYS_CLK_ERR_SW;
    }

    /* 更新 CMSIS 全局频率变量（依赖它的延时/计时同步修正） */
    SystemCoreClockUpdate();

    /* 按新频率重装 SysTick。重装值由编译期常量算出（port.c 中
     * `portNVIC_SYSTICK_LOAD_REG = (configSYSTICK_CLOCK_HZ /
     *  configTICK_RATE_HZ) - 1UL`，configSYSTICK_CLOCK_HZ =
     * configCPU_CLOCK_HZ = 168000000），SystemCoreClockUpdate() 只改内存
     * 变量，改不动已写入 SysTick->LOAD 的值；不重装则 168MHz→HSI 16MHz 后
     * LOAD 仍为 167999，tick 周期约 10.5 秒，vTaskDelay、超时判断与软件
     * 定时器近乎停摆。本宏在 FreeRTOS 工程走 vPortSetupTimerInterrupt()，
     * 裸机工程走 SYS_TICK_Init()。 */
    SYS_RTOS_SYSTICK_RELOAD();

    return SYS_CLK_OK;
}

/* 读 SWS 状态位转时钟源枚举；未知编码按 HSI 兜底 */
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

/* PLL 档频率：有记录值返回记录值，否则读 SystemCoreClock */
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

/* 便捷封装：切到低功耗档（HSI 16MHz）。SysTick 已由 SYS_CLK_Switch() 重装，
 * 调用方不要再调 SYS_TICK_Init()，FreeRTOS 下会与内核争抢 SysTick。 */
uint8_t SYS_CLK_ToLowPower(void)
{
    return SYS_CLK_Switch(SYS_CLK_HSI);
}


/* ---- 区块 3：总线分频自定义与查询（扩展功能） ---- */
/* 分频常量 → 实际分频数的对照表；查不到说明传入非法常量 */
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

    /* 分频常量查表，非法值直接拒绝 */
    hdiv  = sys_clk_div_lookup(sys_clk_ahb_div_tbl, (uint32_t)SYS_CLK_AHB_DIV_COUNT, hclk_div);
    p1div = sys_clk_div_lookup(sys_clk_apb_div_tbl, (uint32_t)SYS_CLK_APB_DIV_COUNT, pclk1_div);
    p2div = sys_clk_div_lookup(sys_clk_apb_div_tbl, (uint32_t)SYS_CLK_APB_DIV_COUNT, pclk2_div);
    if ((hdiv == 0U) || (p1div == 0U) || (p2div == 0U)) return SYS_CLK_ERR_DIV;

    /* 按当前主频预演，超上限直接拒绝，不动硬件 */
    fsrc  = SYS_CLK_GetFreq();
    fhclk = fsrc / hdiv;
    fp1   = fhclk / p1div;
    fp2   = fhclk / p2div;
    if (fhclk > SYS_CLK_MAX_HCLK)  return SYS_CLK_ERR_RANGE;
    if (fp1   > SYS_CLK_MAX_PCLK1) return SYS_CLK_ERR_RANGE;
    if (fp2   > SYS_CLK_MAX_PCLK2) return SYS_CLK_ERR_RANGE;

    /* 安全过渡：先降到 HSI，16MHz 下改分频安全 */
    cur = SYS_CLK_GetSource();
    if (cur != SYS_CLK_HSI) {
        uint32_t to = SYS_CLK_READY_TIMEOUT;

        sys_clk_start_hsi();
        RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);
        /* 退回安全源须带超时，状态位读不回时不致停在初始化里 */
        while (RCC_GetSYSCLKSource() != RCC_SYSCLKSource_HSI) {
            if (--to == 0U) return SYS_CLK_ERR_SW;
        }
    }

    /* 写 HPRE/PPRE1/PPRE2 三个分频寄存器 */
    RCC_HCLKConfig(hclk_div);
    RCC_PCLK1Config(pclk1_div);
    RCC_PCLK2Config(pclk2_div);

    /* 切回原时钟源，带超时确认 */
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


/* ---- 扩展功能：LSE / LSI / RTC 时钟源（sys_rtc 底座） ---- */
/* 启动 LSE 并等 LSERDY，超时上限同 SYS_CLK_SWITCH_TIMEOUT */
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

