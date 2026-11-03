#ifndef __FWLIB_BEEP_H
#define __FWLIB_BEEP_H

#include "stm32f4xx.h"

/* 板载蜂鸣器控制：通断控制与节拍鸣叫
 * 依赖 gpio_core.h
 * 电平极性由 BEEP_ACTIVE_LOW 决定，函数内部自动适配
 * 本模块只做通断控制，不产生驱动频率：有源蜂鸣器（内置振荡电路）可直接通断发声；
 * 无源蜂鸣器需外部方波驱动，本模块无法驱动，需配合定时器 PWM 输出 */


#define BEEP_PORT   GPIOF
#define BEEP_PIN    GPIO_Pin_8

/* 1 = 低电平鸣响；0 = 高电平鸣响
 * 判断依据：驱动电路三极管基极低电平导通则填 1
 * 天马 F407 开发板：PF8 经 R37 驱动 S8050(NPN) 基极，集电极接蜂鸣器，
 * 高电平导通鸣响，故填 0；参考板 GEC-M4 同接法，同样填 0 */
#define BEEP_ACTIVE_LOW  0

/* BEEP_Beep() 的固定节拍（ms）：响 BEEP_ON_MS_DEFAULT → 停 BEEP_OFF_MS_DEFAULT
 * 单位 ms；BEEP_BeepEx 不使用这两个值 */
#define BEEP_ON_MS_DEFAULT    100
#define BEEP_OFF_MS_DEFAULT   100

/* 按键提示音时长（ms） */
#define BEEP_KEY_SOUND_MS     50

/* SOS 信号的基本时间单位（ms）：点 = 1 单位，划 = 3 单位 */
#define BEEP_SOS_UNIT_MS  100


/* 初始化：配置为推挽输出（GPIO_OType_PP），并立即写入静音电平（上电不响）
 * 标准库：经 gpio_core → RCC_AHB1PeriphClockCmd + GPIO_Init（GPIO_OType_PP）*/
void BEEP_Init  (void);

/* 开始鸣响 / 停止鸣响 / 翻转鸣响状态，极性自动适配
 * 标准库：GPIO_SetBits / GPIO_ResetBits（On/Off 按极性宏二选一，直接写 BSRR）；
 *         GPIO_ToggleBits（翻转，经 gpio_core） */
void BEEP_On    (void);
void BEEP_Off   (void);
void BEEP_Toggle(void);


/* 鸣叫 times 次，固定节拍：响 BEEP_ON_MS_DEFAULT → 停 BEEP_OFF_MS_DEFAULT（默认各 100ms）
 * 阻塞式，占用 CPU 约 (ON+OFF)ms × times */
void BEEP_Beep  (uint32_t times);

/* 可调版：自定义响 on_ms / 停 off_ms，单位 ms
 * on_ms / off_ms 传 0 会被修正为 1 */
void BEEP_BeepEx(uint32_t times, uint32_t on_ms, uint32_t off_ms);

/* 按键提示音：响 BEEP_KEY_SOUND_MS（默认 50ms） */
void BEEP_KeySound(void);

/* SOS 求救信号：三短 → 三长 → 三短，阻塞约 2~3 秒
 * 节拍速度由 BEEP_SOS_UNIT_MS 决定 */
void BEEP_SOS(void);

/* 非阻塞节拍鸣叫
 * 参数：times 声，每声响 on_ms / 停 off_ms，单位 ms
 * 驱动：循环里每 1ms 调一次 BEEP_Update()，或放软定时器 1ms 任务 */
void BEEP_AsyncStart(uint32_t times, uint32_t on_ms, uint32_t off_ms);
void BEEP_AsyncStop (void);            /* 立即停 + 静音 */
void BEEP_Update    (void);            /* 每 1ms 调一次 */

#endif /* __FWLIB_BEEP_H */

