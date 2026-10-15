#ifndef __FWLIB_BEEP_H
#define __FWLIB_BEEP_H

#include "stm32f4xx.h"

/* ================================================================
 *  beep.h — 板载蜂鸣器模块头文件
 * ================================================================
 *  依赖 gpio_core.h；BEEP_ACTIVE_LOW 选择有效电平，函数内部自动适配。
 *  硬件：仅通断控制，不产生驱动频率，只适用于有源蜂鸣器（内置振荡电路）；
 *        无源蜂鸣器需外部方波，须由定时器 PWM 驱动。
 * ================================================================ */


/* ======== 区块 1：定义与宏定义区（换板子只改这里） ======== */
#define BEEP_PORT   GPIOF
#define BEEP_PIN    GPIO_Pin_8

/* 1 = 低电平鸣响，0 = 高电平鸣响；依据驱动电路，三极管基极低电平导通则填 1 */
#define BEEP_ACTIVE_LOW  0

/* ---- 默认鸣叫节拍 ---- */
/* BEEP_Beep() 固定节拍（ms）：响 BEEP_ON_MS_DEFAULT → 停 BEEP_OFF_MS_DEFAULT */
#define BEEP_ON_MS_DEFAULT    100
#define BEEP_OFF_MS_DEFAULT   100

/* BEEP_KeySound 提示音时长（ms） */
#define BEEP_KEY_SOUND_MS     50

/* ---- SOS 节拍 ---- */
/* SOS 基本时间单位（ms）：点 = 1 单位，划 = 3 单位 */
#define BEEP_SOS_UNIT_MS  100


/* ======== 区块 2：基础功能 ======== */
/* 初始化：推挽输出 GPIO_OType_PP，上电即写入静音电平
 * 经 gpio_core → RCC_AHB1PeriphClockCmd + GPIO_Init */
void BEEP_Init  (void);

/* 开始鸣响 / 停止鸣响 / 翻转鸣响状态，极性按 BEEP_ACTIVE_LOW 自动适配
 * GPIO_SetBits / GPIO_ResetBits 写 BSRR；翻转经 gpio_core 的 GPIO_ToggleBits */
void BEEP_On    (void);
void BEEP_Off   (void);
void BEEP_Toggle(void);


/* ======== 区块 3：扩展功能 ======== */
/* 鸣叫 times 次，节拍为 BEEP_ON_MS_DEFAULT / BEEP_OFF_MS_DEFAULT（各 100ms）
 * 阻塞式，占用 CPU 约 (ON+OFF)ms × times */
void BEEP_Beep  (uint32_t times);

/* 鸣叫 times 次，自定义响 on_ms / 停 off_ms（ms）；传 0 修正为 1
 * 阻塞式 */
void BEEP_BeepEx(uint32_t times, uint32_t on_ms, uint32_t off_ms);

/* 按键提示音：单声短鸣 BEEP_KEY_SOUND_MS（默认 50ms） */
void BEEP_KeySound(void);

/* SOS 求救信号：三短 → 三长 → 三短，阻塞约 2~3 秒；节拍由 BEEP_SOS_UNIT_MS 决定 */
void BEEP_SOS(void);

/* 非阻塞节拍鸣叫，不阻塞主循环，用于报警场景
 * 鸣叫 times 声，每声响 on_ms / 停 off_ms（ms）
 * 需每 1ms 调用 BEEP_Update() 驱动（主循环或软定时器 1ms 任务） */
void BEEP_AsyncStart(uint32_t times, uint32_t on_ms, uint32_t off_ms);
void BEEP_AsyncStop (void);            /* 立即停 + 静音 */
void BEEP_Update    (void);            /* 每 1ms 调一次 */

#endif /* __FWLIB_BEEP_H */

