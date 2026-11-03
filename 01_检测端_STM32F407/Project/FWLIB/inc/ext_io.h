#ifndef __FWLIB_EXT_IO_H
#define __FWLIB_EXT_IO_H

#include "stm32f4xx.h"

/* ext_io.h  外接模块封装库（红外/循迹/触摸/声音，单引脚数字输入）
 * 平台 STM32F407ZET6 标准外设库；依赖 gpio_core.h
 * 调用顺序 EXT_IO_Init() 打底在前，EXT_XXX_Init() 覆盖在后 */

/* 区块 1：宏定义区（换板子只改这里） */

/* 触发极性：1 = 低电平有效（检测到为低），0 = 高电平有效
 * 读到的结果与实际相反时翻转对应开关 */
#define EXT_IR_ACTIVE_LOW     1
#define EXT_TRACE_ACTIVE_LOW  1
#define EXT_TOUCH_ACTIVE_LOW  1
#define EXT_SOUND_ACTIVE_LOW  1

/* 打底时全部外接引脚的上下拉（决定无信号时的静止电平）
 * 低电平有效配 GPIO_PuPd_UP，无信号时拉高，避免悬空误触发；高电平有效配 GPIO_PuPd_DOWN
 * 取值 0 = GPIO_PuPd_NOPULL, 1 = GPIO_PuPd_UP, 2 = GPIO_PuPd_DOWN */
#define EXT_BASE_PULL  1

/* 各模块检测路数，合法 id 为 0 ~ 数量-1
 * 修改后必须同步修改 ext_io.c 中对应的引脚表，项数保持一致 */
#define EXT_IR_COUNT    3
#define EXT_TRACE_COUNT 2
#define EXT_TOUCH_COUNT 1
#define EXT_SOUND_COUNT 1


/* 区块 2：基础功能 */

/* 打底：把四类外接引脚统一配置为输入 + EXT_BASE_PULL，时钟经 gpio_core 统一使能
 * 调用链 RCC_AHB1PeriphClockCmd + GPIO_Init；应在各 EXT_XXX_Init() 之前调用 */
void    EXT_IO_Init(void);

/* ---- 红外避障（id：0 ~ EXT_IR_COUNT-1） ---- */

/* 专属覆盖：默认态已满足，函数体留空；需特殊配置时在 .c 内追加，在打底之后执行 */
void    EXT_IR_Init   (void);

/* 读取检测结果：1 = 检测到，0 = 未检测到或 id 越界
 * 触发极性按 EXT_IR_ACTIVE_LOW 适配；即时读取不含去抖，输出抖动需在上层去抖
 * 经 gpio_core 调用 GPIO_ReadInputDataBit 读 IDR，以下各 Detected 同
 * 其余三类功能同此：id 合法范围见对应 EXT_XXX_COUNT */
uint8_t EXT_IR_Detected(uint8_t id);

/* ---- 循迹传感器（id：0 ~ EXT_TRACE_COUNT-1，检测到 = 探头压线） ---- */
void    EXT_TRACE_Init   (void);
uint8_t EXT_TRACE_Detected(uint8_t id);

/* ---- 触摸/碰撞（id：0 ~ EXT_TOUCH_COUNT-1，检测到 = 被触发） ---- */
void    EXT_TOUCH_Init   (void);
uint8_t EXT_TOUCH_Detected(uint8_t id);

/* ---- 声音检测（id：0 ~ EXT_SOUND_COUNT-1，检测到 = 有声音） ---- */
void    EXT_SOUND_Init   (void);
uint8_t EXT_SOUND_Detected(uint8_t id);


/* 区块 3：扩展功能 */

/* 统计检测到的路数，返回值 0 ~ EXT_XXX_COUNT
 * 逐路复用各 Detected 路径（GPIO_ReadInputDataBit） */
uint8_t EXT_IR_CountDetected   (void);   /* 例:n = EXT_IR_CountDetected() */
uint8_t EXT_TRACE_CountDetected(void);   /* 例:n = EXT_TRACE_CountDetected() */
uint8_t EXT_TOUCH_CountDetected(void);   /* 例:n = EXT_TOUCH_CountDetected() */
uint8_t EXT_SOUND_CountDetected(void);   /* 例:n = EXT_SOUND_CountDetected() */

/* 本文件仅覆盖读单引脚型模块；需协议或定时器的模块（超声波、蓝牙等）另建 ext_xxx.c/.h */

#endif /* __FWLIB_EXT_IO_H */

