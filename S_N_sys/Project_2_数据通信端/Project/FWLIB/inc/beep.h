#ifndef __FWLIB_BEEP_H
#define __FWLIB_BEEP_H

#include "stm32f4xx.h"

/* ================================================================
 *  beep.h —— 【板载】蜂鸣器模块  头文件
 * ================================================================
 *  设计定位 : 板载蜂鸣器抽象层（薄封装：通断控制 + 节拍鸣叫）
 *  依赖     : gpio_core.h（GPIO 输出工具）
 *  标准库关键词 : GPIO_SetBits / GPIO_ResetBits / GPIO_Init / RCC_AHB1PeriphClockCmd
 *  电平极性 : 由 BEEP_ACTIVE_LOW 决定，函数内部自动适配
 *
 *  硬件说明（重要）:
 *      本模块只做"通断"控制、不产生驱动频率——
 *      有源蜂鸣器（内置振荡电路）：直接通断即可发声 ✔
 *      无源蜂鸣器（需外部方波驱动）：本模块无法驱动，
 *                                    需配合定时器 PWM 输出 ✘
 *
 *  使用方式 :
 *      BEEP_Init();          // ① 初始化（上电静音）
 *      BEEP_Beep(2);         // ② 叫 2 声（默认节拍 100ms）
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
#define BEEP_PORT   GPIOF
#define BEEP_PIN    GPIO_Pin_8

/* 1 = 低电平鸣响；0 = 高电平鸣响
 * 判断方法：看驱动电路——三极管基极低电平导通 → 填 1
 * 【天马 F407 开发板】PF8 经 R37 驱动 S8050(NPN) 基极，
 *   集电极接蜂鸣器 → 高电平导通鸣响 → 填 0
 * 【参考板 GEC-M4】同接法，同样填 0 */
#define BEEP_ACTIVE_LOW  0

/* -------------------- SOS 节拍 -------------------- */
/* SOS 信号的基本时间单位（ms）：点 = 1 单位，划 = 3 单位
 * 改大变得缓慢庄重，改小变得急促 */
#define BEEP_SOS_UNIT_MS  100


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：配置为推挽输出（GPIO_OType_PP），并立即写入"静音"电平（上电不响）
 * 标准库 : 经 gpio_core → RCC_AHB1PeriphClockCmd + GPIO_Init（GPIO_OType_PP）*/
void BEEP_Init  (void);

/* 开始鸣响 / 停止鸣响 / 翻转鸣响状态（极性自动适配）
 * 标准库 : GPIO_SetBits / GPIO_ResetBits（On/Off 按极性宏二选一,直接写 BSRR）;
 *          GPIO_ToggleBits（翻转,经 gpio_core）
 * 示例 : BEEP_On();                // 开始鸣响
 *        BEEP_Off();               // 停止 */
void BEEP_On    (void);
void BEEP_Off   (void);
void BEEP_Toggle(void);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 鸣叫 times 次，固定节拍：响 100ms → 停 100ms
 * 阻塞式：鸣叫期间占用 CPU（times 次共约 200ms×times）
 * 示例 : BEEP_Beep(2);             // 嘀嘀两声 */
void BEEP_Beep  (uint32_t times);

/* 可调版：自定义"响 on_ms / 停 off_ms"（单位 ms）
 * on_ms / off_ms 传 0 会被修正为 1（保证节拍有效）
 * 示例 : BEEP_BeepEx(3, 50, 200);  // 快速三短音
 * 扩展提示 : 更复杂的节奏 → 用 BEEP_On/Off + Delay_ms 自行编排
 *            （先例:BEEP_SOS 就是纯组合写出来的,可照抄套路）*/
void BEEP_BeepEx(uint32_t times, uint32_t on_ms, uint32_t off_ms);

/* 按键提示音：短促"嘀"一声（约 50ms），适合按键反馈 */
void BEEP_KeySound(void);

/* SOS 求救信号：三短 → 三长 → 三短（阻塞约 2~3 秒）
 * 节拍速度由 BEEP_SOS_UNIT_MS 决定（见区块 1） */
void BEEP_SOS(void);

/* ----------------------------------------------------------------
 * 非阻塞节拍鸣叫（与 LED_BlinkUpdate 同款引擎，适合报警场景）
* ----------------------------------------------------------------
 * 场景 : 报警声不能阻塞主循环——叫你的,主循环照跑;
 * 节奏 : times 声,每声"响 on_ms / 停 off_ms"(毫秒)
 * 驱动 : 循环里每 1ms 调 BEEP_Update();或放软定时器 1ms 任务
 * 例 : BEEP_AsyncStart(3, 100, 200);   // 后台响三声,自动收尾静音 */
void BEEP_AsyncStart(uint32_t times, uint32_t on_ms, uint32_t off_ms);
void BEEP_AsyncStop (void);            /* 立即停 + 静音 */
void BEEP_Update    (void);            /* 每 1ms 调一次 */

#endif /* __FWLIB_BEEP_H */

