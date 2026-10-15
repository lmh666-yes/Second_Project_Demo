#include "led.h"
/* 接口说明见同名 .h，本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_ms 等）独立文件 */

/* 对外只暴露 id，取值范围 0 ~ LED_COUNT-1
 * 端口和引脚映射集中在 led.h 的宏，本文件按 id 查表操作；换引脚、改极性不需要改本文件 */


/* 数组下标 = 对外暴露的 id，两张表一一对应，顺序必须一致：led_port[i] 与 led_pin[i] 描述同一引脚
 * 表项数必须与 led.h 中的 LED_COUNT 相同，增删 LED 时两处同步修改，只改 LED_COUNT 会被下方编译期护栏拦下 */
static GPIO_TypeDef* const led_port[LED_COUNT] = {LED0_PORT, LED1_PORT};
static const uint16_t      led_pin [LED_COUNT] = {LED0_PIN,  LED1_PIN};

/* 编译期护栏：表项数与 LED_COUNT 不一致时此行编译报错 */
typedef char led_table_count_check[(sizeof(led_port) / sizeof(led_port[0]) == LED_COUNT) ? 1 : -1];

/* 流水单步位置，含义为下一次要点亮的 id
 * LED_Init 复位为 0，其它阻塞式灯效不修改它 */
static uint8_t             led_flow_pos = 0;


/* 点亮与熄灭电平换算：LED_ON_LEVEL / LED_OFF_LEVEL 是宏形式的函数名，
 * 预处理阶段按 LED_ACTIVE_LOW 二选一，运行时无额外开销 */
#if LED_ACTIVE_LOW
    #define LED_ON_LEVEL    GPIO_ResetBits      /* 低电平点亮 */
    #define LED_OFF_LEVEL   GPIO_SetBits
#else
    #define LED_ON_LEVEL    GPIO_SetBits        /* 高电平点亮 */
    #define LED_OFF_LEVEL   GPIO_ResetBits
#endif

/* 内部辅助：按语义写电平，on 非 0 点亮
 * 所有公开接口最终都走这里，越界防护与极性适配只有一份实现 */
static void led_write(uint8_t id, uint8_t on)
{
    if (id >= LED_COUNT) return;

    if (on) LED_ON_LEVEL (led_port[id], led_pin[id]);
    else    LED_OFF_LEVEL(led_port[id], led_pin[id]);
}


/* 逐个配置为推挽输出并立即写熄灭电平
 * GPIO_OutInit 内部自动使能端口时钟，无需另行开时钟
 * 先配置后写电平，避免引脚不确定状态点亮 LED */
void LED_Init(void)
{
    led_flow_pos = 0;                             /* 位置复位 */

    for (uint8_t i = 0; i < LED_COUNT; i++) {
        GPIO_OutInit(led_port[i], led_pin[i]);
        led_write(i, 0);                          /* 初始熄灭 */
    }
}

/* 点亮：核心逻辑在 led_write，含越界防护与极性适配 */
void LED_On(uint8_t id)
{
    led_write(id, 1);
}

/* 熄灭：核心逻辑在 led_write */
void LED_Off(uint8_t id)
{
    led_write(id, 0);
}

/* 翻转：直接操作 ODR，结果是电平取反，与极性无关 */
void LED_Toggle(uint8_t id)
{
    if (id >= LED_COUNT) return;
    GPIO_OutToggle(led_port[id], led_pin[id]);
}


/* 点亮全部：逐个调用 LED_On */
void LED_AllOn(void)
{
    for (uint8_t i = 0; i < LED_COUNT; i++) LED_On(i);
}

/* 熄灭全部：逐个调用 LED_Off */
void LED_AllOff(void)
{
    for (uint8_t i = 0; i < LED_COUNT; i++) LED_Off(i);
}

/* 按位显示：逐位检查 value，位为 1 点亮对应 LED
 * LED_SHOW_REVERSE 在编译期决定位序，正序为 bit i 对应 LED i */
void LED_ShowHex(uint8_t value)
{
    for (uint8_t i = 0; i < LED_COUNT; i++) {
#if LED_SHOW_REVERSE
        uint8_t bit = (uint8_t)(LED_COUNT - 1 - i);
#else
        uint8_t bit = i;
#endif
        if (value & (1 << bit)) LED_On(i);
        else                    LED_Off(i);
    }
}


/* 灯效：全部由 LED_On / LED_Off / LED_AllOn / LED_AllOff 组合而成
 * 节奏由 delay_ms 阻塞延时控制，结束后统一全部熄灭 */

/* 闪烁：指定灯亮、灭各 interval_ms，重复 times 次 */
void LED_Blink(uint8_t id, uint32_t times, uint32_t interval_ms)
{
    if (id >= LED_COUNT || interval_ms == 0) return;

    for (uint32_t i = 0; i < times; i++) {
        LED_On (id); delay_ms(interval_ms);
        LED_Off(id); delay_ms(interval_ms);
    }
}

/* 全部闪烁：所有 LED 同步亮灭 */
void LED_AllBlink(uint32_t times, uint32_t interval_ms)
{
    if (interval_ms == 0) return;

    for (uint32_t i = 0; i < times; i++) {
        LED_AllOn (); delay_ms(interval_ms);
        LED_AllOff(); delay_ms(interval_ms);
    }
}

/* 交替闪烁：按 id 奇偶分两组，每轮两拍互换 */
void LED_Alternate(uint32_t times, uint32_t interval_ms)
{
    if (interval_ms == 0) return;

    for (uint32_t i = 0; i < times; i++) {
        /* 第一拍：偶 id 亮、奇 id 灭 */
        for (uint8_t j = 0; j < LED_COUNT; j++) {
            if ((j & 1) == 0) LED_On (j);
            else              LED_Off(j);
        }
        delay_ms(interval_ms);

        /* 第二拍：奇 id 亮、偶 id 灭 */
        for (uint8_t j = 0; j < LED_COUNT; j++) {
            if ((j & 1) == 0) LED_Off(j);
            else              LED_On (j);
        }
        delay_ms(interval_ms);
    }
    LED_AllOff();
}

/* 流水灯（单向）：单灯依次移动，共 times 圈 */
void LED_Flow(uint32_t times, uint32_t interval_ms)
{
    if (interval_ms == 0) return;

    for (uint32_t lap = 0; lap < times; lap++) {
        for (uint8_t i = 0; i < LED_COUNT; i++) {
            LED_AllOff();               /* 只保留当前灯 */
            LED_On(i);
            delay_ms(interval_ms);
        }
    }
    LED_AllOff();
}

/* 跑马灯（往返）：去程 LED0 到最后一个，回程倒数第二个到 LED1，一去一回为一趟 */
void LED_Marquee(uint32_t times, uint32_t interval_ms)
{
    if (interval_ms == 0) return;

    for (uint32_t trip = 0; trip < times; trip++) {
        /* 去程：LED0 → 最后一个 */
        for (uint8_t i = 0; i < LED_COUNT; i++) {
            LED_AllOff();
            LED_On(i);
            delay_ms(interval_ms);
        }
        /* 跑马灯回程：LED_COUNT-2 到 1，两端不重复点亮 */
        for (int8_t i = (int8_t)(LED_COUNT - 2); i >= 1; i--) {
            LED_AllOff();
            LED_On((uint8_t)i);
            delay_ms(interval_ms);
        }
    }
    LED_AllOff();
}

/* 流水单步（非阻塞）：点亮当前位置并预计算下一格
 * 位置含义为下一次要点亮的 id，LED_Init 复位为 0，首次点亮 LED0 */
uint8_t LED_FlowStep(int8_t dir)
{
    uint8_t cur = led_flow_pos;

    /* 只亮当前位置的灯，其余熄灭 */
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        if (i == cur) LED_On(i);
        else          LED_Off(i);
    }

    /* 预计算下一步位置（端点回卷） */
    if (dir < 0) {
        led_flow_pos = (cur == 0) ? (uint8_t)(LED_COUNT - 1) : (uint8_t)(cur - 1);
    } else {
        led_flow_pos = (uint8_t)((cur + 1) % LED_COUNT);
    }

    return cur;
}

/* 扩展功能：目标点亮 + 频率/占空比闪灯引擎（非阻塞） */
/* 点亮 0 ~ n 号
 * n 超范围则全部点亮 */
void LED_OnTo(uint8_t n)
{
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        if (i <= n) LED_On(i);
        else        LED_Off(i);
    }
}

/* 闪灯引擎状态，按 id 一路一套，数组随 LED_COUNT 自动扩
 * 全部加 volatile：任务侧改（Start/Stop），中断侧读（BlinkUpdate）
 * 无 volatile 时编译器会把循环内读到的 led_bl_en 缓存在寄存器，中断里看不到任务写入的新周期或使能位 */
static volatile uint16_t led_bl_period[LED_COUNT];   /* 周期 ms */
static volatile uint16_t led_bl_on    [LED_COUNT];   /* 一个周期内"亮"的 ms */
static volatile uint16_t led_bl_cnt   [LED_COUNT];   /* 当前周期内计到第几 ms */
static volatile uint8_t  led_bl_en    [LED_COUNT];   /* 1 = 该路闪灯启用中 */

/* 启动：周期与占空比（千分比）换算成一个周期内亮的毫秒数 */
void LED_BlinkStart(uint8_t id, uint16_t period_ms, uint16_t duty_permille)
{
    if (id >= LED_COUNT || period_ms == 0U) return;
    if (duty_permille > 1000U) duty_permille = 1000U;

    led_bl_period[id] = period_ms;
    led_bl_on[id]     = (uint16_t)(((uint32_t)period_ms * duty_permille) / 1000U);
    led_bl_cnt[id]    = 0U;
    led_bl_en[id]     = 1U;
}

/* 停止该路并熄灭 */
void LED_BlinkStop(uint8_t id)
{
    if (id >= LED_COUNT) return;
    led_bl_en[id] = 0U;
    LED_Off(id);
}

/* 每 1ms 调用一次，按亮的毫秒数逐路翻转，不阻塞 */
void LED_BlinkUpdate(void)
{
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        if (led_bl_en[i] == 0U) continue;

        led_bl_cnt[i]++;
        if (led_bl_cnt[i] >= led_bl_period[i]) led_bl_cnt[i] = 0U;

        led_write(i, (led_bl_cnt[i] < led_bl_on[i]) ? 1U : 0U);
    }
}

