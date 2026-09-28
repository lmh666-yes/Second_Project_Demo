#include "key.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "sys_exti.h"   /* 按键中断组合（区块 3）基于外部中断模块 */

/* ================================================================
 *  key.c —— 【板载】按键模块  实现文件
 * ================================================================
 *  对外只暴露 id（0 ~ KEY_COUNT-1），引脚/极性/上下拉都在 key.h 的宏里。
 *
 *  本文件三个设计重点：
 *   ① 每键独立极性：KEYx_ACTIVE_LOW 逐键配置（本板 KEY_UP 高有效、
 *      其余三键低有效），编译期算成 key_press_level[] 表；
 *   ② 边沿检测：靠 key_last[] 记录"上次电平"，识别"刚按下"瞬间，
 *      避免长按期间反复上报（行为特征见 key.h）；
 *   ③ 消抖：检测到候选后延时 10ms 复测，只有仍然按下才上报。
 * ================================================================ */


/* ================================================================
 *                    硬件映射表
 * ================================================================
 * 数组下标 = 对外暴露的 id（0 ~ KEY_COUNT-1）
 * 四张表一一对应、顺序必须一致（端口 / 引脚 / 上下拉 / 按下电平）
 *
 * ⚠ 数量一致性（重要）：
 *   表项数必须与 key.h 中的 KEY_COUNT 相同！
 *   增删按键时：key.h 加/删 KEYx 四件套并改 KEY_COUNT，
 *   然后同步在这四张表里加/删对应项。
 *   若只改 KEY_COUNT 而不同步本表（真护栏，两个方向都拦）：
 *     - 改大 → 表项少于数量，编译报错;
 *     - 改小 → 表项多于数量，编译报错。
 *   （数组故意不写 [KEY_COUNT] 尺寸：写了尺寸时 C 会把缺失项
 *     静默补 0（空指针），护栏形同虚设；不写尺寸 sizeof 才反映真实项数）
 * ================================================================ */
static GPIO_TypeDef* const key_port[] = {KEY0_PORT, KEY1_PORT, KEY2_PORT, KEY3_PORT};
static const uint16_t      key_pin [] = {KEY0_PIN,  KEY1_PIN,  KEY2_PIN,  KEY3_PIN};

/* 上下拉表：直接引用 key.h 的 KEYx_PULL（0=浮空 1=上拉 2=下拉） */
static const uint8_t       key_pull[] = {KEY0_PULL, KEY1_PULL, KEY2_PULL, KEY3_PULL};

/* 按下电平表：由每键 KEYx_ACTIVE_LOW 在编译期算好
 *   低电平按下（ACTIVE_LOW=1）→ 按下电平 = 0
 *   高电平按下（ACTIVE_LOW=0）→ 按下电平 = 1
 * ⚠ 本板 KEY_UP(id 3) 与其余三键极性相反，所以极性必须逐键存 */
static const uint8_t       key_press_level[] = {
    KEY0_ACTIVE_LOW ? 0U : 1U,
    KEY1_ACTIVE_LOW ? 0U : 1U,
    KEY2_ACTIVE_LOW ? 0U : 1U,
    KEY3_ACTIVE_LOW ? 0U : 1U
};

/* 编译期护栏：四张表项数必须与 key.h 的 KEY_COUNT 相同（不一致则此行直接编译不过） */
typedef char key_table_count_check[(sizeof(key_port) / sizeof(key_port[0]) == KEY_COUNT) ? 1 : -1];
typedef char key_pull_count_check [(sizeof(key_pull)  / sizeof(key_pull[0])  == KEY_COUNT) ? 1 : -1];
typedef char key_level_count_check[(sizeof(key_press_level) / sizeof(key_press_level[0]) == KEY_COUNT) ? 1 : -1];

/* 位带别名地址表（与上面两张表同源的"第二种映射"，供 KEY_BB_Read 使用）
 * 每项 = 该按键对应 IDR 位的别名地址：*key_bb[i] 读出的就是该位电平。
 * 地址在编译期全部算好，运行时零运算（原理见 gpio_core.h 位带小节） */
static volatile uint32_t * const key_bb[] = {
    GPIO_BB_IN_ADDR(KEY0_PORT, GPIO_PIN_NUM(KEY0_PIN)),
    GPIO_BB_IN_ADDR(KEY1_PORT, GPIO_PIN_NUM(KEY1_PIN)),
    GPIO_BB_IN_ADDR(KEY2_PORT, GPIO_PIN_NUM(KEY2_PIN)),
    GPIO_BB_IN_ADDR(KEY3_PORT, GPIO_PIN_NUM(KEY3_PIN)),
};
/* 编译期护栏：与 key_port 同规则 —— 增删按键时本表同步增删 */
typedef char key_bb_count_check[(sizeof(key_bb) / sizeof(key_bb[0]) == KEY_COUNT) ? 1 : -1];

/* 边沿检测状态表：记录每个按键"上一次扫描到"的原始电平（0/1）
 * KEY_Scan 靠它区分"一直按着"与"刚刚按下"
 * 必须由 KEY_Init 复位为"松开电平"，保证上电不误触发 */
static uint8_t             key_last[KEY_COUNT];


/* ================================================================
 *            电平极性换算（每键独立）
 * ================================================================
 * 把"按下/松开"语义翻译成"引脚电平"：
 *   某键 KEYx_ACTIVE_LOW=1 → 按下电平 0、松开电平 1
 *   某键 KEYx_ACTIVE_LOW=0 → 按下电平 1、松开电平 0
 * 具体电平已在上方 key_press_level[] 表中编译期算好；
 * 后续逻辑只比较 key_press_level[i]，与极性彻底解耦 */
#define KEY_PRESS_LEVEL(id)  (key_press_level[(id)])
#define KEY_IDLE_LEVEL(id)   ((uint8_t)(key_press_level[(id)] ^ 1U))


/* ================================================================
 *                    基础功能
 * ================================================================ */
/* 初始化：① 配置引脚为输入 + 每键各自的上下拉（内部自动开时钟）
 *         ② 边沿记录复位为"松开电平"——若不复位，
 *            上电时若引脚恰好为按下电平，会被误判为一次新按下 */
void KEY_Init(void)
{
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        GPIO_InInit(key_port[i], key_pin[i], key_pull[i]);
        key_last[i] = KEY_IDLE_LEVEL(i);
    }
}

/* 查询某按键的按下极性（1 = 低电平按下；0 = 高电平按下） */
uint8_t KEY_ActiveLow(uint8_t id)
{
    if (id >= KEY_COUNT) return 0;
    return (uint8_t)(key_press_level[id] == 0U ? 1U : 0U);
}

/* 即时读取：读引脚真实电平并与"按下电平"比较
 * 说明 : 无消抖——按下瞬间可能读到抖动，适合"按住持续生效"的场景 */
uint8_t KEY_Read(uint8_t id)
{
    if (id >= KEY_COUNT) return 0;
    return (GPIO_InRead(key_port[id], key_pin[id]) == KEY_PRESS_LEVEL(id)) ? 1 : 0;
}

/* ================================================================
 * 扫描"新按下"事件（含消抖）—— 完整流程说明
 * ================================================================
 * ① 全量快照：一次性读回所有按键电平，
 *    后续判断基于"同一时刻"的数据，避免边读边判造成状态错乱；
 * ② 找候选：与 key_last[] 比较，定位第一个"松开→按下"的按键；
 * ③ 消抖复测：候选键延时 10ms 再读一次——仍是按下才算有效
 *    （代价：命中时阻塞约 10ms；无候选时不阻塞，速度极快）；
 * ④ 统一更新：把本次快照整体存为"上次电平"——
 *    采用"先全量快照、后统一更新"策略，多键同按也不会错乱；
 * ⑤ 上报结果：只有消抖通过的按键才上报，其余返回 KEY_NONE。
 *
 * 返回值：新按下按键的 id；无事件时返回 KEY_NONE（0xFF）
 * ================================================================ */
uint8_t KEY_Scan(void)
{
    uint8_t level[KEY_COUNT];
    uint8_t hit  = KEY_NONE;
    uint8_t down = 0;

    /* ① 一次性采样全部按键 */
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        level[i] = GPIO_InRead(key_port[i], key_pin[i]);
    }

    /* ② 查找新按下事件（每键比较各自的按下电平） */
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        if (key_last[i] != KEY_PRESS_LEVEL(i) && level[i] == KEY_PRESS_LEVEL(i)) {
            hit = i;
            break;
        }
    }

    /* ③ 消抖：延时 10ms 后复测 */
    if (hit != KEY_NONE) {
        Delay_ms(10);
        level[hit] = GPIO_InRead(key_port[hit], key_pin[hit]);
        down = (level[hit] == KEY_PRESS_LEVEL(hit)) ? 1 : 0;
    }

    /* ④ 统一更新所有按键历史电平 */
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        key_last[i] = level[i];
    }

    /* ⑤ 只有消抖通过才上报 */
    return down ? hit : KEY_NONE;
}


/* ================================================================
 *                    扩展功能
 * ================================================================ */

/* 位掩码读取所有按键：逐键即时读取，拼成一个整数
 * （KEY_COUNT ≤ 32 时全覆盖；超过部分不参与） */
uint32_t KEY_ReadAll(void)
{
    uint32_t mask = 0;
    uint8_t  n    = (KEY_COUNT > 32) ? 32 : KEY_COUNT;

    for (uint8_t i = 0; i < n; i++) {
        if (KEY_Read(i)) mask |= (1UL << i);
    }
    return mask;
}

/* 位带直读（与 KEY_Read 结果相同、实现不同，对照说明见 key.h 声明处） */
uint8_t KEY_BB_Read(uint8_t id)
{
    if (id >= KEY_COUNT) return 0;
    return (*key_bb[id] == (uint32_t)KEY_PRESS_LEVEL(id)) ? 1 : 0;
}

/* 阻塞等待任意键按下：反复扫描，直到出现"新按下"事件
 * （借助 KEY_Scan 的消抖与边沿特性，天然过滤抖动） */
uint8_t KEY_WaitPress(void)
{
    uint8_t k;

    do {
        k = KEY_Scan();
    } while (k == KEY_NONE);

    return k;
}

/* 长按检测（阻塞）：以 10ms 为步进累计"持续按住"的时长 */
uint8_t KEY_LongPress(uint8_t id, uint32_t hold_ms)
{
    uint32_t held = 0;

    if (id >= KEY_COUNT) return 0;
    if (!KEY_Read(id))   return 0;          /* 当前未按下 → 直接失败 */

    while (held < hold_ms) {
        Delay_ms(10);
        held += 10;

        if (!KEY_Read(id)) return 0;        /* 中途松开 → 不是长按 */
    }
    return 1;
}


/* ================================================================
 *      扩展功能：按键中断组合（基于 sys_exti，中断只置标志）
 * ================================================================
 * 设计要点 :
 *   ① 不新增任何 ISR —— EXTI 向量统一归 sys_exti 所有（见 README 5.22），
 *      这里只是给每个按键注册一个"置标志"小回调；
 *   ② 触发沿按每键 KEYx_ACTIVE_LOW 自动配：低电平按下→下降沿，
 *      高电平按下→上升沿（即"进入按下状态"的那条边）；
 *   ③ 标志数组 volatile：ISR 置位、主循环清零。
 * ================================================================ */
/* 每键触发沿表（与 key.h 的按下电平配套，编译期算好）
 * ⚠ 本板 KEY_UP 为高有效，所以它的沿是上升沿、其余三键是下降沿 */
#define KEY_EXTI_EDGE_OF(active_low)  ((active_low) ? SYS_EXTI_FALLING : SYS_EXTI_RISING)
static const SysExtiTrigger_t key_exti_edge[] = {
    KEY_EXTI_EDGE_OF(KEY0_ACTIVE_LOW),
    KEY_EXTI_EDGE_OF(KEY1_ACTIVE_LOW),
    KEY_EXTI_EDGE_OF(KEY2_ACTIVE_LOW),
    KEY_EXTI_EDGE_OF(KEY3_ACTIVE_LOW)
};
typedef char key_edge_count_check[(sizeof(key_exti_edge) / sizeof(key_exti_edge[0]) == KEY_COUNT) ? 1 : -1];

/* 每个按键一个标志（ISR 置 1，主循环取走清零） */
static volatile uint8_t key_exti_flag[KEY_COUNT];

/* 每键一个极小回调（sys_exti 的回调不带参数，所以要一键一个函数）
 * 用 X-Macro 批量生成：预置到 8 键——KEY_COUNT ≤ 8 时本段全自动适配，
 * 无需手改;超过 8 键：照样式补 KEY_EXTI_CB(8)… 与表项，护栏会核对数量 */
#define KEY_EXTI_CB(n)  static void key_exti_cb##n(void) { key_exti_flag[n] = 1U; }

KEY_EXTI_CB(0)
#if KEY_COUNT > 1
KEY_EXTI_CB(1)
#endif
#if KEY_COUNT > 2
KEY_EXTI_CB(2)
#endif
#if KEY_COUNT > 3
KEY_EXTI_CB(3)
#endif
#if KEY_COUNT > 4
KEY_EXTI_CB(4)
#endif
#if KEY_COUNT > 5
KEY_EXTI_CB(5)
#endif
#if KEY_COUNT > 6
KEY_EXTI_CB(6)
#endif
#if KEY_COUNT > 7
KEY_EXTI_CB(7)
#endif

/* 回调表（数组不写尺寸：护栏按真实项数核对，防增键时静默空回调） */
static void (*const key_exti_cb[])(void) = {
    key_exti_cb0,
#if KEY_COUNT > 1
    key_exti_cb1,
#endif
#if KEY_COUNT > 2
    key_exti_cb2,
#endif
#if KEY_COUNT > 3
    key_exti_cb3,
#endif
#if KEY_COUNT > 4
    key_exti_cb4,
#endif
#if KEY_COUNT > 5
    key_exti_cb5,
#endif
#if KEY_COUNT > 6
    key_exti_cb6,
#endif
#if KEY_COUNT > 7
    key_exti_cb7,
#endif
};
typedef char key_exti_count_check[(sizeof(key_exti_cb) / sizeof(key_exti_cb[0]) == KEY_COUNT) ? 1 : -1];

#undef KEY_EXTI_CB

/* 引脚掩码 → 中断线号（线号 = 引脚号）：统一走 gpio_core 的 GPIO_PinSource
 * （非法掩码返回 0xFF，SYS_EXTI_InitLine 会拒绝） */

/* 开启全部按键中断：GPIO 打底 → 逐键注册 EXTI 线 → 返回成功数 */
uint8_t KEY_EXTI_Enable(void)
{
    uint8_t i;
    uint8_t ok = 0;

    for (i = 0; i < KEY_COUNT; i++) {
        uint8_t line = GPIO_PinSource(key_pin[i]);

        /* 引脚打底（输入 + 该键自己的上下拉，与 KEY_Init 相同） */
        GPIO_InInit(key_port[i], key_pin[i], key_pull[i]);

        /* 注册：线号 = 引脚号；触发沿随该键极性；回调只置标志 */
        if (SYS_EXTI_InitLine(line, key_port[i], key_pin[i],
                              key_exti_edge[i], key_exti_cb[i])) {
            key_exti_flag[i] = 0U;
            ok++;
        }
    }
    return ok;
}

/* 快速判断"有没有待处理事件"（非阻塞、不取走）：主循环先查有无再取具体键 */
uint8_t KEY_EXTI_HasEvent(void)
{
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        if (key_exti_flag[i] != 0U) return 1U;
    }
    return 0U;
}

/* 取事件（非阻塞）：有则返回 id 并清零，无则 KEY_NONE */
uint8_t KEY_EXTI_GetEvent(void)
{
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        if (key_exti_flag[i] != 0U) {
            key_exti_flag[i] = 0U;      /* 取走即清，避免重复上报 */
            return i;
        }
    }
    return KEY_NONE;
}

/* 关闭按键中断并清空标志 */
void KEY_EXTI_Disable(void)
{
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        SYS_EXTI_Disable(GPIO_PinSource(key_pin[i]));
        key_exti_flag[i] = 0U;
    }
}

