#include "key.h"
/* 接口约定与引脚映射见 key.h;本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_ms 等）独立文件 */
#include "sys_exti.h"   /* 按键中断组合（区块 3）基于外部中断模块 */

/* key.c — 板载按键模块实现
 * 对外只暴露 id（0 ~ KEY_COUNT-1），引脚映射见 key.h 的宏。
 * 边沿检测：key_last[] 记录上次电平，识别"刚按下"，长按期间不重复上报。
 * 消抖：检测到候选后延时 KEY_DEBOUNCE_MS 复测，仍按下才上报。 */


/* 硬件映射表：数组下标 = 对外暴露的 id（0 ~ KEY_COUNT-1）
 * key_port[i] 与 key_pin[i] 描述同一引脚，两张表顺序必须一致。
 * 表项数必须等于 key.h 的 KEY_COUNT：增删按键时两处同步修改；
 * 只改 KEY_COUNT 而不同步本表，两个方向都会编译报错。
 * 不写 [KEY_COUNT] 尺寸：写了尺寸时 C 会把缺失项静默补 0，
 * 不写尺寸 sizeof 才反映真实项数。 */
static GPIO_TypeDef* const key_port[] = {KEY1_PORT, KEY2_PORT, KEY3_PORT, KEY4_PORT};
static const uint16_t      key_pin [] = {KEY1_PIN,  KEY2_PIN,  KEY3_PIN,  KEY4_PIN};

/* 编译期护栏：表项数与 key.h 的 KEY_COUNT 不一致则编译报错 */
typedef char key_table_count_check[(sizeof(key_port) / sizeof(key_port[0]) == KEY_COUNT) ? 1 : -1];

/* 边沿检测状态表：记录每个按键上次扫描到的电平
 * KEY_Scan 由它区分"一直按着"与"刚刚按下"；
 * 必须由 KEY_Init 复位为松开电平，保证上电不误触发 */
static uint8_t             key_last[KEY_COUNT];


/* 电平极性换算：把按下/松开语义翻译成引脚电平
 * KEY_ACTIVE_LOW=1 → 按下=低电平(0)、松开=高电平(1)；=0 时相反。
 * 后续逻辑只比较 KEY_PRESS_LEVEL / KEY_IDLE_LEVEL，与极性解耦 */
#if KEY_ACTIVE_LOW
    #define KEY_PRESS_LEVEL 0
    #define KEY_IDLE_LEVEL  1
#else
    #define KEY_PRESS_LEVEL 1
    #define KEY_IDLE_LEVEL  0
#endif


/* 基础功能 */
/* 初始化：引脚配置为输入 + KEY_PULL 上下拉（内部自动开时钟）；
 * key_last[] 复位为松开电平，否则上电时引脚恰为按下电平会误报新按下 */
void KEY_Init(void)
{
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        GPIO_InInit(key_port[i], key_pin[i], KEY_PULL);
        key_last[i] = KEY_IDLE_LEVEL;
    }
}

/* 即时读取引脚电平并与按下电平比较；无消抖，适合按住持续生效的场景 */
uint8_t KEY_Read(uint8_t id)
{
    if (id >= KEY_COUNT) return 0;
    return (GPIO_InRead(key_port[id], key_pin[id]) == KEY_PRESS_LEVEL) ? 1 : 0;
}

/* 扫描"新按下"事件（含消抖）
 * 一次性采样全部按键，与 key_last[] 比较定位第一个"松开→按下"的键；
 * 候选键延时 KEY_DEBOUNCE_MS 复测，仍按下才有效（仅命中时阻塞）；
 * 返回：新按下按键的 id；无事件返回 KEY_NONE（0xFF） */
uint8_t KEY_Scan(void)
{
    uint8_t level[KEY_COUNT];
    uint8_t hit  = KEY_NONE;
    uint8_t down = 0;

    /* 一次性采样全部按键 */
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        level[i] = GPIO_InRead(key_port[i], key_pin[i]);
    }

    /* 查找新按下事件 */
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        if (key_last[i] == KEY_IDLE_LEVEL && level[i] == KEY_PRESS_LEVEL) {
            hit = i;
            break;
        }
    }

    /* 消抖：延时 KEY_DEBOUNCE_MS 后复测 */
    if (hit != KEY_NONE) {
        delay_ms(KEY_DEBOUNCE_MS);
        level[hit] = GPIO_InRead(key_port[hit], key_pin[hit]);
        down = (level[hit] == KEY_PRESS_LEVEL) ? 1 : 0;
    }

    /* 统一更新所有按键历史电平 */
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        key_last[i] = level[i];
    }

    /* 仅消抖通过才上报 */
    return down ? hit : KEY_NONE;
}


/* 扩展功能 */

/* 位掩码读取所有按键：逐键即时读取合成整数；KEY_COUNT > 32 的部分不参与 */
uint32_t KEY_ReadAll(void)
{
    uint32_t mask = 0;
    uint8_t  n    = (KEY_COUNT > 32) ? 32 : KEY_COUNT;

    for (uint8_t i = 0; i < n; i++) {
        if (KEY_Read(i)) mask |= (1UL << i);
    }
    return mask;
}

/* 阻塞等待任意键按下：反复调用 KEY_Scan，其消抖与边沿特性天然过滤抖动 */
uint8_t KEY_WaitPress(void)
{
    uint8_t k;

    do {
        k = KEY_Scan();
    } while (k == KEY_NONE);

    return k;
}

/* 长按检测（阻塞）：以 KEY_HOLD_STEP_MS 为步进累计按住时长 */
uint8_t KEY_LongPress(uint8_t id, uint32_t hold_ms)
{
    uint32_t held = 0;

    if (id >= KEY_COUNT) return 0;
    if (!KEY_Read(id))   return 0;          /* 当前未按下 → 直接失败 */

    while (held < hold_ms) {
        delay_ms(KEY_HOLD_STEP_MS);
        held += KEY_HOLD_STEP_MS;

        if (!KEY_Read(id)) return 0;        /* 中途松开 → 不是长按 */
    }
    return 1;
}


/* 按键中断组合（基于 sys_exti，ISR 只置标志）
 * 不新增 ISR：EXTI 向量归 sys_exti 所有（见 README 5.22），此处仅注册置标志回调。
 * 触发沿按 KEY_ACTIVE_LOW 自动配置；标志数组 volatile，ISR 置位、主循环清零。
 * 回调预置 8 键：KEY_COUNT ≤ 8 自动适配，超过 8 键按 KEY_EXTI_CB 样式补回调与表项。 */
/* 触发沿换算（与 key.h 的按下电平配套） */
#if KEY_ACTIVE_LOW
    #define KEY_EXTI_EDGE   SYS_EXTI_FALLING    /* 空闲高、按下低 → 下降沿 */
#else
    #define KEY_EXTI_EDGE   SYS_EXTI_RISING     /* 空闲低、按下高 → 上升沿 */
#endif

/* 每个按键一个标志（ISR 置 1，主循环取走清零） */
static volatile uint8_t key_exti_flag[KEY_COUNT];

/* 每键一个回调（sys_exti 回调不带参数，故一键一函数）
 * 预置 8 键：KEY_COUNT ≤ 8 全自动适配；超过 8 键按样式补回调与表项 */
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

/* 回调表（数组不写尺寸：按真实项数核对） */
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

/* 引脚掩码转中断线号（线号 = 引脚号）：由 GPIO_PinSource 完成
 * 非法掩码返回 0xFF，SYS_EXTI_InitLine 会拒绝 */

/* 开启全部按键中断：GPIO 打底 → 逐键注册 EXTI 线 → 返回成功数 */
uint8_t KEY_EXTI_Enable(void)
{
    uint8_t i;
    uint8_t ok = 0;

    for (i = 0; i < KEY_COUNT; i++) {
        uint8_t line = GPIO_PinSource(key_pin[i]);

        /* 引脚打底（输入 + KEY_PULL，与 KEY_Init 相同） */
        GPIO_InInit(key_port[i], key_pin[i], KEY_PULL);

        /* 注册：线号 = 引脚号；回调只置标志 */
        if (SYS_EXTI_InitLine(line, key_port[i], key_pin[i],
                              KEY_EXTI_EDGE, key_exti_cb[i])) {
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

