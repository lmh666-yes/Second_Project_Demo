#include "led.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"

/* ================================================================
 *  led.c —— 【板载】LED 模块  实现文件
 * ================================================================
 *  对外只暴露 id（0 ~ LED_COUNT-1），调用者无需知道具体端口/引脚。
 *  端口引脚映射集中在 led.h 的宏里，本文件只做"用 id 查表 → 操作"。
 *
 *  换板子时本文件的修改规则：
 *    - 换引脚        → 不用改（表引用 led.h 的宏，自动跟随）
 *    - 改极性        → 不用改（下面按 LED_ACTIVE_LOW 自动换算）
 *    - 增删 LED 数量 → 必须同步修改下方引脚表！
 * ================================================================ */


/* ================================================================
 *                    硬件映射表
 * ================================================================
 * 数组下标 = 对外暴露的 id（0 ~ LED_COUNT-1）
 * 两张表一一对应、顺序必须一致：led_port[i] 与 led_pin[i] 描述同一引脚
 *
 * ⚠ 数量一致性（重要）：
 *   表项数必须与 led.h 中的 LED_COUNT 相同！
 *   增删 LED 时：led.h 加/删 LEDx 宏并改 LED_COUNT，
 *   然后同步在这里加/删对应项。
 *   若只改 LED_COUNT 而不同步本表：
 *     - 改小 → 多余初始化项，编译期即报错（下方有编译期护栏）
 *     - 改大 → 表尾出现零值项，编译期护栏同样会拦住
 * ================================================================ */
static GPIO_TypeDef* const led_port[LED_COUNT] = {LED0_PORT, LED1_PORT, LED2_PORT, LED3_PORT};
static const uint16_t      led_pin [LED_COUNT] = {LED0_PIN,  LED1_PIN,  LED2_PIN,  LED3_PIN};

/* 编译期护栏：表项数必须与 led.h 的 LED_COUNT 相同（不一致则此行直接编译不过） */
typedef char led_table_count_check[(sizeof(led_port) / sizeof(led_port[0]) == LED_COUNT) ? 1 : -1];

/* 位带别名地址表（与上面两张表同源的"第三种映射"，供 LED_BB_* 使用）
 * 每项 = 该 LED 对应 ODR 位的别名地址：*led_bb[i] = 0/1 即写该位。
 * 地址在编译期全部算好，运行时零运算（原理见 gpio_core.h 位带小节） */
static volatile uint32_t * const led_bb[LED_COUNT] = {
    GPIO_BB_OUT_ADDR(LED0_PORT, GPIO_PIN_NUM(LED0_PIN)),
    GPIO_BB_OUT_ADDR(LED1_PORT, GPIO_PIN_NUM(LED1_PIN)),
    GPIO_BB_OUT_ADDR(LED2_PORT, GPIO_PIN_NUM(LED2_PIN)),
    GPIO_BB_OUT_ADDR(LED3_PORT, GPIO_PIN_NUM(LED3_PIN)),
};
/* 编译期护栏：与 led_port 同规则 —— 增删 LED 时本表同步增删 */
typedef char led_bb_count_check[(sizeof(led_bb) / sizeof(led_bb[0]) == LED_COUNT) ? 1 : -1];

/* 流水单步位置（LED_FlowStep 的内部状态）：含义 = 下一次要点亮的 id
 * LED_Init() 复位为 0；其它阻塞式灯效不修改它 */
static uint8_t             led_flow_pos = 0;


/* ================================================================
 *            电平极性换算
 * ================================================================
 * 把"点亮/熄灭"语义翻译成"具体寄存器操作"：
 *   LED_ON_LEVEL / LED_OFF_LEVEL 是宏形式的函数名，
 *   预处理阶段按 LED_ACTIVE_LOW 二选一，运行时零开销。 */
#if LED_ACTIVE_LOW
    #define LED_ON_LEVEL    GPIO_ResetBits      /* 低电平点亮 */
    #define LED_OFF_LEVEL   GPIO_SetBits
    #define LED_ON_VALUE    0U                  /* 位带版写"点亮"用的电平值 */
    #define LED_OFF_VALUE   1U
#else
    #define LED_ON_LEVEL    GPIO_SetBits        /* 高电平点亮 */
    #define LED_OFF_LEVEL   GPIO_ResetBits
    #define LED_ON_VALUE    1U
    #define LED_OFF_VALUE   0U
#endif

/* 内部辅助：按"语义"写电平（on 非 0 = 点亮）
 * 所有公开接口最终都走这里——越界防护与极性适配只有一份实现 */
static void led_write(uint8_t id, uint8_t on)
{
    if (id >= LED_COUNT) return;

    if (on) LED_ON_LEVEL (led_port[id], led_pin[id]);
    else    LED_OFF_LEVEL(led_port[id], led_pin[id]);
}


/* ================================================================
 *                    基础功能
 * ================================================================ */
/* 初始化：逐个配置为推挽输出，并立即写入"熄灭"电平
 * （GPIO_OutInit 内部自动使能对应端口时钟，无需另行开时钟）
 * 先配置、后写电平，避免上电到初始化之间引脚不确定状态点亮 LED */
void LED_Init(void)
{
    led_flow_pos = 0;                             /* 流水单步位置复位 */

    for (uint8_t i = 0; i < LED_COUNT; i++) {
        GPIO_OutInit(led_port[i], led_pin[i]);
        led_write(i, 0);                          /* 初始熄灭 */
    }
}

/* 点亮 / 熄灭：核心逻辑集中在 led_write（含越界防护与极性适配） */
void LED_On(uint8_t id)
{
    led_write(id, 1);
}

void LED_Off(uint8_t id)
{
    led_write(id, 0);
}

/* 翻转：直接操作 ODR，无需经过极性换算（翻转结果与极性无关） */
void LED_Toggle(uint8_t id)
{
    if (id >= LED_COUNT) return;
    GPIO_OutToggle(led_port[id], led_pin[id]);
}


/* ================================================================
 *                    扩展功能
 * ================================================================ */
/* 点亮全部：逐个调用 LED_On，保持与单灯操作一致的语义与极性 */
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
 * LED_SHOW_REVERSE 在编译期决定位序（正序：bit i → LED i） */
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


/* ================================================================
 *        扩展功能：位带直写版本（与区块 2 三连功能等价）
 * ================================================================
 * 结果相同、过程不同 —— 对照说明见 led.h 的声明注释与 gpio_core.h：
 *   库函数版：查 port/pin 表 → GPIO_SetBits/ResetBits（写 BSRR）
 *   位带版  ：查别名地址表 → *led_bb[id] = 电平（单条 STR 直写 ODR 位） */
void LED_BB_On(uint8_t id)
{
    if (id >= LED_COUNT) return;
    *led_bb[id] = LED_ON_VALUE;
}

void LED_BB_Off(uint8_t id)
{
    if (id >= LED_COUNT) return;
    *led_bb[id] = LED_OFF_VALUE;
}

/* 翻转：读别名（0/1）→ 取反 → 写回；与库版一样是"读-改-写"两步，
 * 中断恰好穿插时可能丢一次翻转（两版行为完全一致） */
void LED_BB_Toggle(uint8_t id)
{
    if (id >= LED_COUNT) return;
    *led_bb[id] ^= 1U;
}


/* ================================================================
 *                    扩展功能（灯效）
 * ================================================================
 * 全部基于 LED_On / LED_Off / LED_AllOn / LED_AllOff 组合，
 * 节奏由 gpio_core 的粗延时控制（阻塞式）；结束后统一全部熄灭。 */

/* 闪烁：指定灯亮/灭各 interval_ms，重复 times 次 */
void LED_Blink(uint8_t id, uint32_t times, uint32_t interval_ms)
{
    if (id >= LED_COUNT || interval_ms == 0) return;

    for (uint32_t i = 0; i < times; i++) {
        LED_On (id); Delay_ms(interval_ms);
        LED_Off(id); Delay_ms(interval_ms);
    }
}

/* 全部闪烁：所有 LED 同步亮灭 */
void LED_AllBlink(uint32_t times, uint32_t interval_ms)
{
    if (interval_ms == 0) return;

    for (uint32_t i = 0; i < times; i++) {
        LED_AllOn (); Delay_ms(interval_ms);
        LED_AllOff(); Delay_ms(interval_ms);
    }
}

/* 交替闪烁：按 id 奇偶分两组，两拍互换，共 times 轮 */
void LED_Alternate(uint32_t times, uint32_t interval_ms)
{
    if (interval_ms == 0) return;

    for (uint32_t i = 0; i < times; i++) {
        /* 第一拍：偶 id 亮、奇 id 灭 */
        for (uint8_t j = 0; j < LED_COUNT; j++) {
            if ((j & 1) == 0) LED_On (j);
            else              LED_Off(j);
        }
        Delay_ms(interval_ms);

        /* 第二拍：奇 id 亮、偶 id 灭 */
        for (uint8_t j = 0; j < LED_COUNT; j++) {
            if ((j & 1) == 0) LED_Off(j);
            else              LED_On (j);
        }
        Delay_ms(interval_ms);
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
            Delay_ms(interval_ms);
        }
    }
    LED_AllOff();
}

/* 跑马灯（往返）：去程 0→N-1、回程 N-2→1，"去回"为一趟 */
void LED_Marquee(uint32_t times, uint32_t interval_ms)
{
    if (interval_ms == 0) return;

    for (uint32_t trip = 0; trip < times; trip++) {
        /* 去程：LED0 → 最后一个 */
        for (uint8_t i = 0; i < LED_COUNT; i++) {
            LED_AllOff();
            LED_On(i);
            Delay_ms(interval_ms);
        }
        /* 回程：倒数第二个 → LED1（两端不重复点亮） */
        for (int8_t i = (int8_t)(LED_COUNT - 2); i >= 1; i--) {
            LED_AllOff();
            LED_On((uint8_t)i);
            Delay_ms(interval_ms);
        }
    }
    LED_AllOff();
}

/* 流水单步（非阻塞）：点亮当前位置并预计算下一格
 * 位置含义：下一次要点亮的 id（LED_Init 复位为 0 → 首次点亮 LED0） */
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

