#include "sys_clock.h"
#include "sys_rtos.h"   /* SYS_RTOS_PRESENT / SYS_RTOS_SYSTICK_RELOAD() */
#include "sys_tick.h"   /* 裸机时 SYS_RTOS_SYSTICK_RELOAD() 会用到 SYS_TICK_Init */

/* 系统时钟源切换：改变主频时先降速、再改配置、后升速，避免取指与总线异常 */

/* PLL 档频率记录：首次切换前捕获 SystemCoreClock。
 * 切到低速源后 SystemCoreClock 随之改变，无法再查回 PLL 档频率。 */
static uint32_t sys_clk_pll_freq = 0;

/* 时钟源配置表：每行一档时钟源的参数
 *   freq: 该档频率，PLL 档动态记录，填 0 占位
 *   flash_latency: Flash 等待周期，主频越高需要越多，168MHz 需 5 个，否则高速取指可能出错
 *   hclk_div / pclk1_div / pclk2_div: 总线分频，APB1 上限 42MHz 取 ÷4，APB2 上限 84MHz 取 ÷2
 *   sws_code: SWS 状态位中该时钟源的编码，切换完成判据
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


/* 按枚举值查配置表；找不到返回 NULL（调用方需要判空） */
static const SysClkProfile_t *sys_clk_lookup(SysClkSrc_t src)
{
    for (uint8_t i = 0; i < (uint8_t)SYS_CLK_PROFILE_COUNT; i++) {
        if (clock_profiles[i].src == src) return &clock_profiles[i];
    }
    return NULL;
}

/* 启动 HSI 并等待 HSIRDY 置位；HSI 为芯片内部 RC，切换时作安全过渡源 */
static void sys_clk_start_hsi(void)
{
    uint32_t to = SYS_CLK_READY_TIMEOUT;

    RCC_HSICmd(ENABLE);
    /* 带超时：RCC 时钟未使能或时钟树配置错误时 HSIRDY 不置位，无超时循环会停在此处 */
    while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET) {
        if (--to == 0U) return;
    }
}


/* 切换系统时钟源。
 * 高速运行中直接改 Flash 等待周期与总线分频可能取指失败，
 * 因此先切到 HSI 降速，再改配置并启动目标源，最后写 SW 位切换。
 * 返回值：SYS_CLK_OK 或错误码，见 sys_clock.h */
uint8_t SYS_CLK_Switch(SysClkSrc_t target)
{
    /* 1) 记录 PLL 档频率，只在首次调用执行。
     *    抓取前必须先 SystemCoreClockUpdate()：CMSIS 初值 16000000 是占位常量，
     *    不刷新会把 16MHz 记成 PLL 档频率。 */
    if (sys_clk_pll_freq == 0) {
        SystemCoreClockUpdate();
        sys_clk_pll_freq = SystemCoreClock;
    }

    /* 2) 查配置表，非法目标直接报错 */
    const SysClkProfile_t *p = sys_clk_lookup(target);
    if (p == NULL) return SYS_CLK_ERR_UNKNOWN;

#if SYS_CLK_SAFE_TRANSITION
    /* 3) 目标非 HSI 且当前非 HSI 时，先切到 HSI 过渡 */
    if (target != SYS_CLK_HSI && RCC_GetSYSCLKSource() != RCC_SYSCLKSource_HSI) {
        sys_clk_start_hsi();
        RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);
        /* 带超时：退回安全源失败说明 SWS 状态位读不回来，死等会停在初始化中 */
        {
            uint32_t to = SYS_CLK_READY_TIMEOUT;
            while (RCC_GetSYSCLKSource() != RCC_SYSCLKSource_HSI) {
                if (--to == 0U) return SYS_CLK_ERR_SW;
            }
        }
    }
#endif

    /* 4) 先降到最低等待周期，低速下改 latency 安全 */
    FLASH_SetLatency(FLASH_Latency_0);

    /* 5) 分频器先复位到 ÷1，低速下修改安全 */
    RCC_HCLKConfig (RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div1);
    RCC_PCLK2Config(RCC_HCLK_Div1);

    /* 6) 开启目标时钟源并等待就绪 */
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
            /* 等锁定带超时：HSE 掉振或 PLL 配置值非法时 PLLRDY 不置位，无超时循环会停在此处 */
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

    /* 7) 按目标频率重设 latency 与分频，升速前必须先完成 */
    FLASH_SetLatency(p->flash_latency);
    RCC_HCLKConfig (p->hclk_div);
    RCC_PCLK1Config(p->pclk1_div);
    RCC_PCLK2Config(p->pclk2_div);

    /* 8) 写 SW 位切换系统时钟源 */
    switch (target) {
        case SYS_CLK_HSI: RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);    break;
        case SYS_CLK_HSE: RCC_SYSCLKConfig(RCC_SYSCLKSource_HSE);    break;
        case SYS_CLK_PLL: RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK); break;
        default: return SYS_CLK_ERR_UNKNOWN;
    }

    /* 9) 等 SWS 确认，带超时 */
    uint32_t timeout = SYS_CLK_SWITCH_TIMEOUT;
    while (RCC_GetSYSCLKSource() != p->sws_code) {
        if (--timeout == 0) return SYS_CLK_ERR_SW;
    }

    /* 10) 更新 CMSIS 全局频率变量，依赖它的延时与计时同步修正 */
    SystemCoreClockUpdate();

    /* 11) 按新频率重装 SysTick。重装值 = configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ - 1，
     *     由编译期常量算出；SystemCoreClockUpdate() 只改内存变量，不改 SysTick->LOAD 的值。
     *     不重装则 tick 周期随主频变化，vTaskDelay、超时判断与软件定时器失准。
     *     见 sys_rtos.h：FreeRTOS 走 vPortSetupTimerInterrupt()，裸机走 SYS_TICK_Init()。 */
    SYS_RTOS_SYSTICK_RELOAD();

    return SYS_CLK_OK;
}

/* 读 SWS 状态位 → 时钟源枚举；未知编码按 HSI 兜底 */
SysClkSrc_t SYS_CLK_GetSource(void)
{
    /* 直接读 CFGR 的 SWS[1:0]（bit3:2）：0=HSI，1=HSE，2=PLL
     * 不用 StdPeriph 的 RCC_GetSYSCLKSource()：本工程 RTE 包内 rcc.c 会把 PLL 误报成 HSI，
     * 使 SYS_CLK_GetFreq() 把 168MHz 报成 16MHz。 */
    uint32_t sws = (RCC->CFGR & 0x0CUL) >> 2U;

    if (sws == 2UL) return SYS_CLK_PLL;
    if (sws == 1UL) return SYS_CLK_HSE;
    return SYS_CLK_HSI;
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


/* 切到高性能档 PLL 168MHz */
uint8_t SYS_CLK_ToHighSpeed(void)
{
    return SYS_CLK_Switch(SYS_CLK_PLL);
}

/* 切到低功耗档 HSI 16MHz；时基重装由 SYS_CLK_Switch 内部的 SYS_RTOS_SYSTICK_RELOAD() 完成 */
uint8_t SYS_CLK_ToLowPower(void)
{
    return SYS_CLK_Switch(SYS_CLK_HSI);
}


/* 分频常量与实际分频数对照表，查不到即传入非法常量 */
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

    /* 1) 分频常量查表，非法值直接拒绝 */
    hdiv  = sys_clk_div_lookup(sys_clk_ahb_div_tbl, (uint32_t)SYS_CLK_AHB_DIV_COUNT, hclk_div);
    p1div = sys_clk_div_lookup(sys_clk_apb_div_tbl, (uint32_t)SYS_CLK_APB_DIV_COUNT, pclk1_div);
    p2div = sys_clk_div_lookup(sys_clk_apb_div_tbl, (uint32_t)SYS_CLK_APB_DIV_COUNT, pclk2_div);
    if ((hdiv == 0U) || (p1div == 0U) || (p2div == 0U)) return SYS_CLK_ERR_DIV;

    /* 2) 按当前主频预演，超上限直接拒绝，不动硬件 */
    fsrc  = SYS_CLK_GetFreq();
    fhclk = fsrc / hdiv;
    fp1   = fhclk / p1div;
    fp2   = fhclk / p2div;
    if (fhclk > SYS_CLK_MAX_HCLK)  return SYS_CLK_ERR_RANGE;
    if (fp1   > SYS_CLK_MAX_PCLK1) return SYS_CLK_ERR_RANGE;
    if (fp2   > SYS_CLK_MAX_PCLK2) return SYS_CLK_ERR_RANGE;

    /* 3) 先降到 HSI，16MHz 下改分频安全 */
    cur = SYS_CLK_GetSource();
    if (cur != SYS_CLK_HSI) {
        uint32_t to = SYS_CLK_READY_TIMEOUT;

        sys_clk_start_hsi();
        RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);
        while (RCC_GetSYSCLKSource() != RCC_SYSCLKSource_HSI) {
            if (--to == 0U) return SYS_CLK_ERR_SW;      /* 退回安全源都失败 → 报错 */
        }
    }

    /* 4) 写三个分频寄存器 */
    RCC_HCLKConfig(hclk_div);
    RCC_PCLK1Config(pclk1_div);
    RCC_PCLK2Config(pclk2_div);

    /* 5) 切回原时钟源，带超时确认 */
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

/* 启动 LSE 并等起振，超时上限同 SYS_CLK_SWITCH_TIMEOUT */
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

