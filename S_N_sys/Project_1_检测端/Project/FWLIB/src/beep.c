#include "beep.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"

/* ================================================================
 *  beep.c —— 【板载】蜂鸣器模块  实现文件
 * ================================================================
 *  只做"通断"电平控制，节拍靠软件延时组合；
 *  单引脚设计，引脚/极性配置全部集中在 beep.h。
 * ================================================================ */


/* ================================================================
 *            电平极性换算
 * ================================================================
 * 与 led.c 同一套思路：预处理阶段把"响/停"翻译成寄存器操作，
 * 运行时零开销；BEEP_On / BEEP_Off 共用这一个写入口。 */
#if BEEP_ACTIVE_LOW
    #define BEEP_ON_LEVEL   GPIO_ResetBits
    #define BEEP_OFF_LEVEL  GPIO_SetBits
#else
    #define BEEP_ON_LEVEL   GPIO_SetBits
    #define BEEP_OFF_LEVEL  GPIO_ResetBits
#endif

/* 内部辅助：按"语义"写电平（on 非 0 = 鸣响） */
static void beep_write(uint8_t on)
{
    if (on) BEEP_ON_LEVEL (BEEP_PORT, BEEP_PIN);
    else    BEEP_OFF_LEVEL(BEEP_PORT, BEEP_PIN);
}


/* ================================================================
 *                    基础功能
 * ================================================================ */
/* 初始化：配置为推挽输出(GPIO_OType_PP)后立即静音（GPIO_OutInit 自动开时钟）
 * 先配置、后写电平——避免上电瞬间的引脚不确定电平导致响一声 */
void BEEP_Init(void)
{
    GPIO_OutInit(BEEP_PORT, BEEP_PIN);
    BEEP_Off();
}

void BEEP_On    (void) { beep_write(1); }                        /* 鸣响（极性自动适配） */
void BEEP_Off   (void) { beep_write(0); }                        /* 静音（极性自动适配） */
void BEEP_Toggle(void) { GPIO_OutToggle(BEEP_PORT, BEEP_PIN); }  /* 翻转（与极性无关） */


/* ================================================================
 *                    扩展功能
 * ================================================================ */
/* 固定节拍版：响 100ms + 停 100ms，循环 times 次 */
void BEEP_Beep(uint32_t times)
{
    BEEP_BeepEx(times, 100, 100);
}

/* 自定义节拍版：参数先做防零修正，再按"响-停"节拍循环
 * 说明 : 节拍延时直接复用 gpio_core 的粗延时（精度要求不高） */
void BEEP_BeepEx(uint32_t times, uint32_t on_ms, uint32_t off_ms)
{
    if (on_ms  == 0) on_ms  = 1;
    if (off_ms == 0) off_ms = 1;

    for (uint32_t i = 0; i < times; i++) {
        BEEP_On();  Delay_ms(on_ms);
        BEEP_Off(); Delay_ms(off_ms);
    }
}


/* ================================================================
 *                    扩展功能（音效）
 * ================================================================
 * 全部基于 BEEP_On / BEEP_Off + 粗延时组合，阻塞式；
 * 结束后均回到静音状态。 */

/* 按键提示音：短促一声（50ms） */
void BEEP_KeySound(void)
{
    BEEP_On();
    Delay_ms(50);
    BEEP_Off();
}

/* -------------------- SOS 内部节拍单元 -------------------- */

/* 点：响 1 单位 + 停 1 单位 */
static void beep_sos_dot(void)
{
    BEEP_On();  Delay_ms(BEEP_SOS_UNIT_MS);
    BEEP_Off(); Delay_ms(BEEP_SOS_UNIT_MS);
}

/* 划：响 3 单位 + 停 1 单位 */
static void beep_sos_dash(void)
{
    BEEP_On();  Delay_ms(BEEP_SOS_UNIT_MS * 3U);
    BEEP_Off(); Delay_ms(BEEP_SOS_UNIT_MS);
}

/* SOS 求救信号：三短 → 三长 → 三短，组间加长停顿便于分辨
 * （总时长 ≈ 单位时间 × 28，默认 100ms/单位 → 约 2.8 秒） */
void BEEP_SOS(void)
{
    /* S：三短 */
    beep_sos_dot();  beep_sos_dot();  beep_sos_dot();
    Delay_ms(BEEP_SOS_UNIT_MS * 2U);        /* 字母组间隔 */

    /* O：三长 */
    beep_sos_dash(); beep_sos_dash(); beep_sos_dash();
    Delay_ms(BEEP_SOS_UNIT_MS * 2U);        /* 字母组间隔 */

    /* S：三短 */
    beep_sos_dot();  beep_sos_dot();  beep_sos_dot();
    Delay_ms(BEEP_SOS_UNIT_MS * 3U);        /* 结束停顿 */
}


/* ================================================================
 *        扩展功能：非阻塞节拍引擎（报警声不阻塞主循环）
 * ================================================================
 * 状态机 : 响 on_ms → 停 off_ms → … 直到 times 声数完自动静音 */
static uint32_t beep_as_times;      /* 剩余声数 */
static uint32_t beep_as_on;         /* 响 ms */
static uint32_t beep_as_off;        /* 停 ms */
static uint32_t beep_as_cnt;        /* 当前相位计时 */
static uint8_t  beep_as_phase;      /* 1 = 响应处于"响"阶段 */
static uint8_t  beep_as_active;     /* 1 = 引擎运行中 */

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

