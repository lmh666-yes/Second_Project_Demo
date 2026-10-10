#ifndef __FWLIB_KEY_H
#define __FWLIB_KEY_H

#include "stm32f4xx.h"

/* ================================================================
 *  key.h — 【板载】按键模块  头文件
 * ================================================================
 *  依赖 : gpio_core.h；中断组基于 sys_exti.h
 *  标准库 : GPIO_ReadInputDataBit / GPIO_Init / RCC_AHB1PeriphClockCmd；
 *           中断组 SYSCFG_EXTILineConfig / EXTI_Init / NVIC_Init
 *  极性由 KEY_ACTIVE_LOW 决定；KEY_Read 即时无消抖，KEY_Scan 带消抖
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ======================== */
/* -------------------- 按键引脚 -------------------- */
/* KEYx_PORT / KEYx_PIN 成对出现，x-1 为对外 id；key.c 引脚表引用 */
#define KEY1_PORT   GPIOA
#define KEY1_PIN    GPIO_Pin_0
#define KEY2_PORT   GPIOE
#define KEY2_PIN    GPIO_Pin_2
#define KEY3_PORT   GPIOE
#define KEY3_PIN    GPIO_Pin_3
#define KEY4_PORT   GPIOE
#define KEY4_PIN    GPIO_Pin_4

/* -------------------- 数量常量 -------------------- */
/* 合法 id 为 0 ~ KEY_COUNT-1；改后须同步 key.c 引脚表项数，不一致编译期报错 */
#define KEY_COUNT   4

/* KEY_Scan 的"无按键"返回值：0xFF 超出任何合法 id */
#define KEY_NONE    0xFF

/* -------------------- 电平极性 -------------------- */
/* 1 = 低电平按下（空闲上拉为高，按下拉到低）；0 = 反之
 * 依据原理图：按键一端接地 → 取 1 */
#define KEY_ACTIVE_LOW   1

/* -------------------- 按键上下拉 -------------------- */
/* 与 KEY_ACTIVE_LOW 配套，输入引脚不得悬空：= 1 配 GPIO_PuPd_UP，= 0 配 DOWN
 * 取值：0 = GPIO_PuPd_NOPULL, 1 = GPIO_PuPd_UP, 2 = GPIO_PuPd_DOWN */
#define KEY_PULL         1

/* -------------------- 消抖与长按步进 -------------------- */
/* KEY_Scan 软件消抖延时（ms）：检测到新按下后延时复测，兼判定粒度 */
#define KEY_DEBOUNCE_MS      10

/* KEY_LongPress 累计步进（ms）：每轮"延时 + 计时"的粒度，调小则轮询更密 */
#define KEY_HOLD_STEP_MS     10

/* -------------------- 编译期自检 -------------------- */
/* KEY_NONE 固定为 0xFF，所以按键 id 最多到 253（共 254 个） */
#if (KEY_COUNT < 1) || (KEY_COUNT > 254)
/* 注意 : #error 文本必须是 ASCII（AC5 对中文报错信息解析不稳） */
#error "KEY_COUNT must be 1..254 (KEY_NONE occupies 0xFF)"
#endif  /* KEY_COUNT 范围自检 */


/* ================================================================
 *                    区块 2：基础功能
 * ======================== */
/* 初始化：配置为输入 + KEY_PULL 上下拉，并复位内部边沿记录
 * 标准库 : 经 gpio_core → RCC_AHB1PeriphClockCmd + GPIO_Init */
void    KEY_Init(void);

/* 即时读取：1 = 按下，0 = 松开，无消抖；id 越界返回 0
 * 标准库 : 经 gpio_core → GPIO_ReadInputDataBit（读 IDR） */
uint8_t KEY_Read(uint8_t id);

/* 事件扫描：返回本次新按下的按键 id；无事件返回 KEY_NONE
 * 行为 : KEY_DEBOUNCE_MS 消抖；边沿触发，只在"松开→按下"瞬间上报一次，
 *        长按不连发；阻塞式，命中时占用约 KEY_DEBOUNCE_MS
 * 标准库 : GPIO_ReadInputDataBit（读 IDR,经 gpio_core）+ 粗延时消抖 */
uint8_t KEY_Scan(void);


/* ================================================================
 *                    区块 3：扩展功能
 * ======================== */
/* 位掩码读取所有按键即时状态：bit i = 1 按下，0 松开；无消抖，最多 32 键
 * 标准库 : 逐键复用 KEY_Read 路径（GPIO_ReadInputDataBit） */
uint32_t KEY_ReadAll(void);

/* 位带直读版（KEY_BB_Read）已撤除—见 2026-10-08 拆分：
 * 位带统一由独立文件 sys_bitband.h 提供宏（如 PAin(0) 直读电平）。 */

/* 阻塞等待任意按键按下：返回按键 id（不会返回 KEY_NONE）
 * 说明 : 内部循环调用 KEY_Scan()，等待期间 CPU 空转 */
uint8_t KEY_WaitPress(void);

/* 长按检测：判断已按下的指定按键能否保持满 hold_ms
 *  返回 : 1 = 保持到 hold_ms；0 = 中途松开或 id 越界
 *  前提 : 调用时该键已按下（由 KEY_Scan 事件触发）
 *  说明 : 阻塞式，最长占用 hold_ms，以 KEY_HOLD_STEP_MS 为步进
 *  用法 : 先 KEY_Scan 取 id，再 KEY_LongPress(id, 1000) 确认长按 */
uint8_t KEY_LongPress(uint8_t id, uint32_t hold_ms);

/* ----------------------------------------------------------------
 * 按键中断组合（基于 sys_exti；中断只"置标志"，主循环取事件）
 * ----------------------------------------------------------------
 * 与轮询分工 : KEY_Scan 系主循环轮询，含消抖，命中时阻塞；
 *   本组硬件中断触发、置标志、非阻塞取走，Sleep 模式下可唤醒系统。
 * 选择 : 要零延迟响应 / 低功耗唤醒 → 本组；要消抖、长按语义 → KEY_Scan 系。 */

/* 开启全部按键外部中断（触发沿按 KEY_ACTIVE_LOW 适配）
 * 返回 : 成功绑定的按键数（正常 = KEY_COUNT）
 * 说明 : 内部完成 GPIO + SYSCFG 映射 + EXTI + NVIC；中断只置标志，
 *      主循环用 HasEvent 判断后 GetEvent 取走；
 *      每键占一条 EXTI 线（线号 = 引脚号），同一线勿再被 SYS_EXTI_InitLine 绑定；
 *      无消抖，抖动/连按产生多次事件，需上层滤波；
 *      事件回调预置 8 键，KEY_COUNT ≤ 8 无需改 key.c
 * 标准库 : 经 sys_exti → SYSCFG_EXTILineConfig + EXTI_Init + NVIC_Init */
uint8_t KEY_EXTI_Enable(void);

/* 判断有无待处理事件（非阻塞、不取走，仍需 GetEvent 取走）
 * 返回 : 1 = 有事件未取走；0 = 无 */
uint8_t KEY_EXTI_HasEvent(void);

/* 取出被中断触发并已置标志的按键（非阻塞），无事件返回 KEY_NONE */
uint8_t KEY_EXTI_GetEvent(void);

/* 关闭按键外部中断（注销回调并清空标志） */
void    KEY_EXTI_Disable(void);

#endif /* __FWLIB_KEY_H */

