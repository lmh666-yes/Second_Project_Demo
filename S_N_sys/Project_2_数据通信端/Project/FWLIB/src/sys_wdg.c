#include "sys_wdg.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include <stddef.h>     /* NULL（复位原因解码的指针判断） */
#include "gpio_core.h"  /* 心跳掩码用其位带宏 BITBAND_SRAM（SRAM 位带原子写） */

/* ================================================================
 *  sys_wdg.c —— 【系统】看门狗模块  实现文件
 * ================================================================
 *  换算公式 :
 *   IWDG：超时 = 4 × 2^PR × (RLR+1) / f_LSI
 *         （PR 0~6 → 分频 4~256；RLR 12 位 0~4095）
 *   WWDG：递减步长 = 4096 × 2^WDGTB / PCLK1
 *         超时 = (T − 63) × 步长（T 为 6 位计数器初值）
 *  两个模块的寄存器操作都走标准外设库，超时值按"当前真实时钟"
 *  自动换算（WWDG 用 PCLK1，切换主频后重新 Init 即可）。
 * ================================================================ */


/* ================================================================
 *                    配置数据
 * ================================================================ */
/* IWDG 分频表：索引 = PR 值 0~6 → 分频 4/8/16/32/64/128/256 */
static const uint8_t iwdg_presc_tbl[7] = {
    IWDG_Prescaler_4,   IWDG_Prescaler_8,   IWDG_Prescaler_16,
    IWDG_Prescaler_32,  IWDG_Prescaler_64,  IWDG_Prescaler_128,
    IWDG_Prescaler_256,
};

/* WWDG 分频表：索引 = WDGTB 值 0~3 → 分频 1/2/4/8 */
static const uint32_t wwdg_presc_tbl[4] = {
    WWDG_Prescaler_1, WWDG_Prescaler_2, WWDG_Prescaler_4, WWDG_Prescaler_8,
};

/* 编译期护栏：两张表的档位数固定 */
typedef char wdg_iwdg_tbl_check[(sizeof(iwdg_presc_tbl) / sizeof(iwdg_presc_tbl[0]) == 7U) ? 1 : -1];
typedef char wdg_wwdg_tbl_check[(sizeof(wwdg_presc_tbl) / sizeof(wwdg_presc_tbl[0]) == 4U) ? 1 : -1];

/* WWDG 当前装载值（6 位计数器，bit6 恒为 1），供喂狗使用；
 * 0 = 尚未初始化（喂狗直接忽略） */
static uint8_t wdg_wwdg_t = 0U;


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 启动独立看门狗 IWDG（超时 ms 自动换算分频与重载值） */
void SYS_WDG_Init(uint32_t timeout_ms)
{
    uint8_t  pr  = 6U;          /* 缺省用最大分频（配合最大重载） */
    uint32_t rlr = 4095U;
    uint32_t div;
    uint32_t cnt;

    /* ① 范围截断（1ms ~ 32768ms） */
    if (timeout_ms < SYS_WDG_MIN_TIMEOUT_MS) timeout_ms = SYS_WDG_MIN_TIMEOUT_MS;
    if (timeout_ms > SYS_WDG_MAX_TIMEOUT_MS) timeout_ms = SYS_WDG_MAX_TIMEOUT_MS;

    /* ② 优先选最小分频（换算精度最高），装不下再升一档：
     *    需要计数值 = 超时(s) × f_LSI / 分频 = ms×LSI / (1000×div) */
    for (pr = 0U; pr < 7U; pr++) {
        div = 4UL << pr;                                    /* 4 × 2^PR */
        cnt = (timeout_ms * SYS_WDG_LSI_HZ + (div * 1000UL) - 1UL) / (div * 1000UL);
        if (cnt <= 4096UL) {
            rlr = cnt - 1UL;
            break;
        }
    }
    if (pr == 7U) {         /* 全部分频都装不下（理论到不了，保险处理） */
        pr  = 6U;
        rlr = 4095U;
    }

    /* ③ 调试冻结：调试器暂停 CPU 时冻结看门狗计数（断点不会被复位） */
#if SYS_WDG_DEBUG_FREEZE
    DBGMCU->APB1FZ |= (1UL << 12);      /* DBG_IWDG_STOP */
    DBGMCU->APB1FZ |= (1UL << 11);      /* DBG_WWDG_STOP */
#endif

    /* ④ 配置并启动（启动后无法停止——官方特性） */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(iwdg_presc_tbl[pr]);
    IWDG_SetReload((uint16_t)rlr);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

/* 喂狗：把 IWDG 计数器复位到重载值 */
void SYS_WDG_Feed(void)
{
    IWDG_ReloadCounter();
}


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 读取上次复位原因（RCC_CSR 里的复位标志位） */
uint32_t SYS_WDG_ResetCause(void)
{
    uint32_t cause = 0U;

    if (RCC_GetFlagStatus(RCC_FLAG_PORRST)  != RESET) cause |= SYS_WDG_RST_POR;
    if (RCC_GetFlagStatus(RCC_FLAG_PINRST)  != RESET) cause |= SYS_WDG_RST_PIN;
    if (RCC_GetFlagStatus(RCC_FLAG_BORRST)  != RESET) cause |= SYS_WDG_RST_BOR;
    if (RCC_GetFlagStatus(RCC_FLAG_SFTRST)  != RESET) cause |= SYS_WDG_RST_SOFT;
    if (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET) cause |= SYS_WDG_RST_IWDG;
    if (RCC_GetFlagStatus(RCC_FLAG_WWDGRST) != RESET) cause |= SYS_WDG_RST_WWDG;
    if (RCC_GetFlagStatus(RCC_FLAG_LPWRRST) != RESET) cause |= SYS_WDG_RST_LPWR;

    return cause;
}

/* 清除复位标志（写 RMVF 位） */
void SYS_WDG_ClearResetFlags(void)
{
    RCC_ClearFlag();
}

/* 复位原因 → 短文字（ASCII，逗号分隔；无标志时输出 NONE） */
uint32_t SYS_WDG_ResetCauseDecode(uint32_t cause, char *buf, uint32_t size)
{
    static const struct {
        uint32_t    mask;
        const char *name;
    } tbl[7] = {
        { SYS_WDG_RST_LPWR, "LPWR"  },
        { SYS_WDG_RST_WWDG, "WWDG"  },
        { SYS_WDG_RST_IWDG, "IWDG"  },
        { SYS_WDG_RST_SOFT, "SOFT"  },
        { SYS_WDG_RST_POR,  "POR"   },
        { SYS_WDG_RST_PIN,  "PIN"   },
        { SYS_WDG_RST_BOR,  "BOR"   },
    };
    uint32_t pos = 0U;
    uint32_t max;
    uint32_t i;
    uint8_t  first = 1U;

    if ((buf == NULL) || (size < 2U)) return 0U;

    max = size - 1U;

    for (i = 0U; i < 7U; i++) {
        if ((cause & tbl[i].mask) != 0U) {
            if (first == 0U) {
                if (pos < max) buf[pos++] = ',';
            }
            first = 0U;
            /* 追加名称（逐字符，带长度保护） */
            {
                const char *s = tbl[i].name;
                while ((*s != '\0') && (pos < max)) buf[pos++] = *s++;
            }
        }
    }

    if (first != 0U) {      /* 没有任何标志位（异常情况） */
        const char *s = "NONE";
        while ((*s != '\0') && (pos < max)) buf[pos++] = *s++;
    }

    buf[pos] = '\0';
    return pos;
}

/* ================================================================
 *   区块 3 扩展：多任务心跳汇总喂狗（与 gpio_core 位带联动）
 * ================================================================ */
/* 编译期护栏：心跳任务数 0 ~ 32（0 = 不使用该功能） */
typedef char wdg_hb_cnt_check[(SYS_WDG_HEARTBEAT_COUNT <= 32U) ? 1 : -1];

/* 心跳掩码：bit id = 该任务本轮已报到（SRAM 位带原子写,任何上下文可报） */
static volatile uint32_t wdg_hb_mask = 0U;

#if   (SYS_WDG_HEARTBEAT_COUNT == 0U)
    /* 0 = 未启用:各函数直接返回,不参与汇总 */
#elif (SYS_WDG_HEARTBEAT_COUNT >= 32U)
    #define WDG_HB_ALLMASK  0xFFFFFFFFUL
#else
    #define WDG_HB_ALLMASK  ((1UL << SYS_WDG_HEARTBEAT_COUNT) - 1UL)
#endif

/* 任务报到:置自己的位（越界忽略;未启用时空操作） */
void SYS_WDG_Heartbeat(uint8_t id)
{
#if (SYS_WDG_HEARTBEAT_COUNT > 0U)
    if (id < SYS_WDG_HEARTBEAT_COUNT) {
        BITBAND_SRAM(&wdg_hb_mask, id) = 1U;    /* 单比特原子写,无需关中断 */
    }
#else
    (void)id;
#endif
}

/* 是否全员报到（不清掩码、不喂狗——WWDG 等自定义策略用） */
uint8_t SYS_WDG_HeartbeatAll(void)
{
#if (SYS_WDG_HEARTBEAT_COUNT > 0U)
    return (uint8_t)((wdg_hb_mask & WDG_HB_ALLMASK) == WDG_HB_ALLMASK);
#else
    return 0U;
#endif
}

/* 缺位掩码:bit = 1 表示该任务本轮还没报到（0 = 全到） */
uint32_t SYS_WDG_HeartbeatPending(void)
{
#if (SYS_WDG_HEARTBEAT_COUNT > 0U)
    return (~wdg_hb_mask) & WDG_HB_ALLMASK;
#else
    return 0U;
#endif
}

/* 心跳汇总喂狗:全员到 → 喂狗 + 清零（新一轮）返回 1;否则不喂返回 0 */
uint8_t SYS_WDG_HeartbeatPoll(void)
{
#if (SYS_WDG_HEARTBEAT_COUNT > 0U)
    if ((wdg_hb_mask & WDG_HB_ALLMASK) != WDG_HB_ALLMASK) return 0U;
    SYS_WDG_Feed();
    wdg_hb_mask = 0U;
    return 1U;
#else
    return 0U;
#endif
}

/* 手动清零掩码（配合 HeartbeatAll 自定义策略时用） */
void SYS_WDG_HeartbeatClear(void)
{
    wdg_hb_mask = 0U;
}

/* 启动窗口看门狗 WWDG（超时按当前 PCLK1 自动换算） */
void SYS_WDG_WwdgInit(uint32_t timeout_ms)
{
    RCC_ClocksTypeDef clocks;
    uint32_t pclk1;
    uint32_t limit_ms;
    uint8_t  tb = 3U;           /* 缺省最大分频 */
    uint32_t t  = 127U;         /* 缺省最大计数值 */
    uint32_t i;

    /* ① 使能 WWDG 时钟（挂 APB1）并读当前总线频率 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_WWDG, ENABLE);
    RCC_GetClocksFreq(&clocks);
    pclk1 = (clocks.PCLK1_Frequency != 0U) ? clocks.PCLK1_Frequency : 42000000UL;

    /* ② 范围截断：最大 = 64 × 4096 × 8 / PCLK1（毫秒） */
    limit_ms = 2097152000UL / pclk1;                /* 64×4096×8×1000 / PCLK1 */
    if (limit_ms < 1UL) limit_ms = 1UL;
    if (timeout_ms < 1UL)        timeout_ms = 1UL;
    if (timeout_ms > limit_ms)   timeout_ms = limit_ms;

    /* ③ 从最小分频开始试（精度最高），直到计数值装得下：
     *    超时 = (T − 63) × 步长，步长 = 4096 × 2^WDGTB / PCLK1 */
    for (i = 0U; i < 4U; i++) {
        uint64_t step = ((uint64_t)4096U << i) * 1000ULL;        /* 步长×1000 */
        uint64_t num  = (uint64_t)timeout_ms * (uint64_t)pclk1;  /* 分子     */
        uint32_t n    = (uint32_t)((num + step - 1ULL) / step);  /* 需几个步长 */
        if (n == 0U) n = 1U;
        if (n <= 64U) {         /* T = 63 + n ≤ 127，装得下 */
            tb = (uint8_t)i;
            t  = 63UL + n;
            break;
        }
    }

    /* ④ 调试冻结（与 IWDG 相同） */
#if SYS_WDG_DEBUG_FREEZE
    DBGMCU->APB1FZ |= (1UL << 12);
    DBGMCU->APB1FZ |= (1UL << 11);
#endif

    /* ⑤ 配置并启动：窗口全开（任何时刻都能喂），T6 恒为 1 */
    WWDG_SetPrescaler(wwdg_presc_tbl[tb]);
    WWDG_SetWindowValue(0x7FU);
    WWDG_Enable((uint8_t)(t & 0x7FU));

    wdg_wwdg_t = (uint8_t)(t & 0x7FU);      /* 记录装载值，供喂狗 */
}

/* 喂 WWDG：把计数器重装回初始值 */
void SYS_WDG_WwdgFeed(void)
{
    if (wdg_wwdg_t != 0U) {
        WWDG_SetCounter(wdg_wwdg_t);
    }
}
