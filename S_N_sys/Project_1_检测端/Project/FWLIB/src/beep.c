#include "beep.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_ms 等）独立文件 */

/* beep.c — 板载蜂鸣器实现 : 单引脚，只做通断电平控制，节拍由软件延时组合；
 * 引脚与极性配置集中在 beep.h。 */


/* 电平极性换算 : 预处理阶段把响/停映射为寄存器操作，运行时零开销；
 * BEEP_On / BEEP_Off 共用此写入口。 */
#if BEEP_ACTIVE_LOW
    #define BEEP_ON_LEVEL   GPIO_ResetBits
    #define BEEP_OFF_LEVEL  GPIO_SetBits
#else
    #define BEEP_ON_LEVEL   GPIO_SetBits
    #define BEEP_OFF_LEVEL  GPIO_ResetBits
#endif

/* 内部辅助：按语义写电平，on 非 0 为鸣响 */
static void beep_write(uint8_t on)
{
    if (on) BEEP_ON_LEVEL (BEEP_PORT, BEEP_PIN);
    else    BEEP_OFF_LEVEL(BEEP_PORT, BEEP_PIN);
}


/* ---- 基础功能 ---- */
/* 初始化：推挽输出(GPIO_OType_PP)，配置后立即静音；GPIO_OutInit 自动开时钟。
 * 必须先配置再写电平，否则上电瞬间引脚电平不确定会响一声。 */
void BEEP_Init(void)
{
    GPIO_OutInit(BEEP_PORT, BEEP_PIN);
    BEEP_Off();
}

void BEEP_On    (void) { beep_write(1); }                        /* 鸣响（极性自动适配） */
void BEEP_Off   (void) { beep_write(0); }                        /* 静音（极性自动适配） */
void BEEP_Toggle(void) { GPIO_OutToggle(BEEP_PORT, BEEP_PIN); }  /* 翻转（与极性无关） */


/* ---- 扩展功能 ---- */
/* 固定节拍：响 BEEP_ON_MS_DEFAULT + 停 BEEP_OFF_MS_DEFAULT(默认各 100ms)，循环 times 次 */
void BEEP_Beep(uint32_t times)
{
    BEEP_BeepEx(times, BEEP_ON_MS_DEFAULT, BEEP_OFF_MS_DEFAULT);
}

/* 自定义节拍：on_ms/off_ms 传 0 会被修正为 1；节拍延时用 delay.h 粗延时，阻塞，
 * 总耗时约为 times × (on_ms + off_ms) */
void BEEP_BeepEx(uint32_t times, uint32_t on_ms, uint32_t off_ms)
{
    if (on_ms  == 0) on_ms  = 1;
    if (off_ms == 0) off_ms = 1;

    for (uint32_t i = 0; i < times; i++) {
        BEEP_On();  delay_ms(on_ms);
        BEEP_Off(); delay_ms(off_ms);
    }
}


/* ---- 扩展功能（音效）：基于 BEEP_On/BEEP_Off + 粗延时，阻塞式，结束回到静音 ---- */

/* 按键提示音：短促一声（BEEP_KEY_SOUND_MS，默认 50ms） */
void BEEP_KeySound(void)
{
    BEEP_On();
    delay_ms(BEEP_KEY_SOUND_MS);
    BEEP_Off();
}

/* SOS 内部节拍单元 */

/* 点：响 1 单位 + 停 1 单位 */
static void beep_sos_dot(void)
{
    BEEP_On();  delay_ms(BEEP_SOS_UNIT_MS);
    BEEP_Off(); delay_ms(BEEP_SOS_UNIT_MS);
}

/* 划：响 3 单位 + 停 1 单位 */
static void beep_sos_dash(void)
{
    BEEP_On();  delay_ms(BEEP_SOS_UNIT_MS * 3U);
    BEEP_Off(); delay_ms(BEEP_SOS_UNIT_MS);
}

/* SOS 求救信号：三短 → 三长 → 三短，组间加长停顿。字母组间隔 2 单位，结束停顿 3 单位；
 * 总时长 = 单位时间 × 28(BEEP_SOS_UNIT_MS 默认 100ms 时约 2.8s) */
void BEEP_SOS(void)
{
    /* S：三短 */
    beep_sos_dot();  beep_sos_dot();  beep_sos_dot();
    delay_ms(BEEP_SOS_UNIT_MS * 2U);        /* 字母组间隔 */

    /* O：三长 */
    beep_sos_dash(); beep_sos_dash(); beep_sos_dash();
    delay_ms(BEEP_SOS_UNIT_MS * 2U);        /* 字母组间隔 */

    /* S：三短 */
    beep_sos_dot();  beep_sos_dot();  beep_sos_dot();
    delay_ms(BEEP_SOS_UNIT_MS * 3U);        /* 结束停顿 */
}


/* ---- 非阻塞节拍引擎（报警声不阻塞主循环）----
 * 状态机 : 响 on_ms → 停 off_ms → … 直到 times 声数完自动静音。
 * 这组变量由任务侧(BEEP_AsyncStart/Stop)写、BEEP_Update 读，后者在 1ms 定时器
 * 中断中调用，必须 volatile，否则编译器将变量缓存于寄存器后中断看不到新参数。 */
static volatile uint32_t beep_as_times;      /* 剩余声数 */
static volatile uint32_t beep_as_on;         /* 响 ms */
static volatile uint32_t beep_as_off;        /* 停 ms */
static volatile uint32_t beep_as_cnt;        /* 当前相位计时 */
static volatile uint8_t  beep_as_phase;      /* 1 = 响应处于"响"阶段 */
static volatile uint8_t  beep_as_active;     /* 1 = 引擎运行中 */

/* 非阻塞启动：times/on_ms/off_ms 任一为 0 则直接返回、不启动引擎 */
void BEEP_AsyncStart(uint32_t times, uint32_t on_ms, uint32_t off_ms)
{
    if (times == 0U || on_ms == 0U || off_ms == 0U) return;

    beep_as_times  = times;
    beep_as_on     = on_ms;
    beep_as_off    = off_ms;
    beep_as_cnt    = 0U;
    beep_as_phase  = 1U;
    beep_as_active = 1U;
    BEEP_On();
}

void BEEP_AsyncStop(void)
{
    beep_as_active = 0U;
    BEEP_Off();
}

void BEEP_Update(void)
{
    if (beep_as_active == 0U) return;

    beep_as_cnt++;

    if (beep_as_phase != 0U) {                  /* 响阶段 */
        if (beep_as_cnt >= beep_as_on) {
            beep_as_cnt = 0U;
            BEEP_Off();
            if (--beep_as_times == 0U) {        /* 最后一声响完 → 收工 */
                beep_as_active = 0U;
            } else {
                beep_as_phase = 0U;
            }
        }
    } else {                                    /* 停阶段 */
        if (beep_as_cnt >= beep_as_off) {
            beep_as_cnt   = 0U;
            beep_as_phase = 1U;
            BEEP_On();
        }
    }
}

