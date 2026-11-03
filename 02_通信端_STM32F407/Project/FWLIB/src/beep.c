#include "beep.h"
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_ms 等）独立文件 */

/* beep.c: 蜂鸣器模块实现，只做通断电平控制，节拍靠软件延时组合
 * 单引脚设计，引脚与极性配置集中在 beep.h */


/* 电平极性换算：按 BEEP_ACTIVE_LOW 在预处理阶段把响/停翻译成寄存器操作，BEEP_On / BEEP_Off 共用此写入口 */
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


/* 基础功能 */
/* 初始化：配置为推挽输出（GPIO_OType_PP）后立即静音，GPIO_OutInit 自动开时钟
 * 先配置后写电平，避免上电瞬间引脚电平不确定而响一声 */
void BEEP_Init(void)
{
    GPIO_OutInit(BEEP_PORT, BEEP_PIN);
    BEEP_Off();
}

void BEEP_On    (void) { beep_write(1); }                        /* 鸣响（极性自动适配） */
void BEEP_Off   (void) { beep_write(0); }                        /* 静音（极性自动适配） */
void BEEP_Toggle(void) { GPIO_OutToggle(BEEP_PORT, BEEP_PIN); }  /* 翻转（与极性无关） */


/* 扩展功能 */
/* 固定节拍版：响 BEEP_ON_MS_DEFAULT + 停 BEEP_OFF_MS_DEFAULT（默认各 100ms），循环 times 次 */
void BEEP_Beep(uint32_t times)
{
    BEEP_BeepEx(times, BEEP_ON_MS_DEFAULT, BEEP_OFF_MS_DEFAULT);
}

/* 自定义节拍版：on_ms / off_ms 为 0 时修正为 1，再按响-停节拍循环
 * 节拍延时用 gpio_core 的粗延时，精度要求不高 */
void BEEP_BeepEx(uint32_t times, uint32_t on_ms, uint32_t off_ms)
{
    if (on_ms  == 0) on_ms  = 1;
    if (off_ms == 0) off_ms = 1;

    for (uint32_t i = 0; i < times; i++) {
        BEEP_On();  delay_ms(on_ms);
        BEEP_Off(); delay_ms(off_ms);
    }
}


/* 音效：基于 BEEP_On / BEEP_Off 加粗延时组合，全部为阻塞式，结束后回到静音 */

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

/* SOS 求救信号：三短、三长、三短，组间加长停顿便于分辨
 * 总时长 ≈ 单位时间 × 28；BEEP_SOS_UNIT_MS 默认 100ms 时约 2.8 秒 */
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

/* 非阻塞节拍引擎：响 on_ms → 停 off_ms，交替到 times 声数完自动静音
 * 这些变量由任务侧的 BEEP_AsyncStart / BEEP_AsyncStop 写、由 BEEP_Update 在定时器中断里按 1ms 读，故须加 volatile
 * 否则编译器会把 active / phase 缓存在寄存器中，任务刚写入的停止标志与新参数在中断里看不到 */
static volatile uint32_t beep_as_times;      /* 剩余声数 */
static volatile uint32_t beep_as_on;         /* 响 ms */
static volatile uint32_t beep_as_off;        /* 停 ms */
static volatile uint32_t beep_as_cnt;        /* 当前相位计时 */
static volatile uint8_t  beep_as_phase;      /* 1 = 处于响阶段 */
static volatile uint8_t  beep_as_active;     /* 1 = 引擎运行中 */

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
            if (--beep_as_times == 0U) {        /* 最后一声响完，结束 */
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

