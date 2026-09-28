#ifndef __FWLIB_EXT_IO_H
#define __FWLIB_EXT_IO_H

#include "stm32f4xx.h"

/* ================================================================
 *  ext_io.h —— 【外接】扩展模块封装库  头文件
 * ================================================================
 *  芯片平台 : STM32F407ZET6（标准外设库 StdPeriph）
 *  设计定位 : 板载排针上的外接模块抽象层
 *             —— 红外、循迹、触摸、声音（单引脚数字输入型）
 *             —— 板载焊死的外设（LED/KEY/BEEP）见 led.h / key.h / beep.h
 *  依赖     : gpio_core.h（底层 GPIO 工具）
 *  标准库关键词 : GPIO_ReadInputDataBit / GPIO_Init / RCC_AHB1PeriphClockCmd
 *
 *  两层初始化模型（本模块的设计要点）:
 *      第一层 EXT_IO_Init()  —— 通用打底：把"所有"外接引脚统一设为
 *                              "输入 + EXT_BASE_PULL 上下拉"，
 *                              保证引脚不悬空、不误触发；
 *      第二层 EXT_XXX_Init() —— 专属覆盖：只给需要特殊配置的模块
 *                              做个性化设置；默认态已满足时留空。
 *      调用顺序：先打底、后覆盖——顺序颠倒会把个性化配置改回默认。
 *
 *  使用方式 :
 *      EXT_IO_Init();              // ① 打底（必须）
 *      EXT_IR_Init();              // ② 按需覆盖（默认可省）
 *      EXT_IR_Detected(0);         // ③ 读检测结果
 *
 *  移植指引 :
 *      本头文件管"策略"（数量、极性、打底上下拉）；
 *      引脚表在 ext_io.c，改端口/引脚请改 .c（见其注释）。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* -------------------- 触发极性 -------------------- */
/* 1 = 低电平有效（检测到 → 引脚为低）；0 = 高电平有效
 * 判断方法：多数模块"有信号时输出低"，即 1；
 *          若读到的结果与实际相反，翻转对应开关即可。 */
#define EXT_IR_ACTIVE_LOW     1
#define EXT_TRACE_ACTIVE_LOW  1
#define EXT_TOUCH_ACTIVE_LOW  1
#define EXT_SOUND_ACTIVE_LOW  1

/* -------------------- 打底上下拉 -------------------- */
/* 打底时给所有外接引脚设置的上下拉（决定"无信号时"的静止电平）:
 *   模块低电平有效（ACTIVE_LOW=1）→ 配 GPIO_PuPd_UP（上拉）：
 *     无信号时引脚被拉高 = "未检测到"，不会误触发；
 *   模块高电平有效（ACTIVE_LOW=0）→ 配 GPIO_PuPd_DOWN（下拉）
 * 取值（等同标准库宏）: 0 = GPIO_PuPd_NOPULL, 1 = GPIO_PuPd_UP, 2 = GPIO_PuPd_DOWN */
#define EXT_BASE_PULL  1

/* -------------------- 数量常量 -------------------- */
/* 每类模块的检测路数：合法 id 为 0 ~ 数量-1
 * ⚠ 修改后必须同步修改 ext_io.c 中对应的引脚表（项数要一致） */
#define EXT_IR_COUNT    3
#define EXT_TRACE_COUNT 2
#define EXT_TOUCH_COUNT 1
#define EXT_SOUND_COUNT 1


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 第一层：通用打底——把四类外接引脚统一配置为"输入 + EXT_BASE_PULL"
 * 必须在第二层 EXT_XXX_Init() 之前调用
 * 标准库 : 经 gpio_core → RCC_AHB1PeriphClockCmd + GPIO_Init（输入+上下拉）
 * 示例 : EXT_IO_Init();            // main 开头打底一次
 * 扩展提示 : 加"新外接模块"三件套 —— ① 本头文件加 EXT_XXX_COUNT;
 *            ② ext_io.c 加引脚表(照红外那组抄);③ 加 EXT_XXX_Detected */
void    EXT_IO_Init(void);

/* ---- 红外避障（id：0 ~ EXT_IR_COUNT-1） ---- */

/* 第二层：专属覆盖——默认态已满足，当前函数体留空；
 * 若该模块需要特殊配置，在 .c 内对应函数里追加（打底之后执行） */
void    EXT_IR_Init   (void);

/* 读取检测结果：1 = 检测到（触发），0 = 未检测到或 id 越界
 * 说明 : 触发极性自动适配（见 EXT_IR_ACTIVE_LOW）；
 *       即时读取、不含消抖，输出抖动时请在上层做软件滤波
 * 标准库 : 经 gpio_core → GPIO_ReadInputDataBit（读 IDR;以下各 Detected 同）
 * 示例 : if (EXT_IR_Detected(0)) { ... }    // 0 号红外检测到障碍 */
uint8_t EXT_IR_Detected(uint8_t id);

/* ---- 循迹传感器（id：0 ~ EXT_TRACE_COUNT-1） ---- */
void    EXT_TRACE_Init   (void);
/* 示例 : if (EXT_TRACE_Detected(0)) { ... }   // 0 号探头压线 */
uint8_t EXT_TRACE_Detected(uint8_t id);

/* ---- 触摸/碰撞（id：0 ~ EXT_TOUCH_COUNT-1） ---- */
void    EXT_TOUCH_Init   (void);
/* 示例 : if (EXT_TOUCH_Detected(0)) { ... }   // 触摸/碰撞被触发 */
uint8_t EXT_TOUCH_Detected(uint8_t id);

/* ---- 声音检测（id：0 ~ EXT_SOUND_COUNT-1） ---- */
void    EXT_SOUND_Init   (void);
/* 示例 : if (EXT_SOUND_Detected(0)) { ... }   // 检测到声音 */
uint8_t EXT_SOUND_Detected(uint8_t id);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 多路计数：统计"检测到"的路数（0 ~ EXT_XXX_COUNT）
 * 典型用途 : 多路避障判断"障碍强度"、循迹统计压线数量等
 * 标准库 : 逐路复用各 Detected 路径（GPIO_ReadInputDataBit）
 * 示例 : uint8_t n = EXT_TRACE_CountDetected();   // 循迹压线数量(其余三类同) */
uint8_t EXT_IR_CountDetected   (void);   /* 例:n = EXT_IR_CountDetected() */
uint8_t EXT_TRACE_CountDetected(void);   /* 例:n = EXT_TRACE_CountDetected() */
uint8_t EXT_TOUCH_CountDetected(void);   /* 例:n = EXT_TOUCH_CountDetected() */
uint8_t EXT_SOUND_CountDetected(void);   /* 例:n = EXT_SOUND_CountDetected() */

/* 目前外接模块均为"读单引脚"型，以上为常用聚合接口。
 * 若以后加入需要协议/定时器的模块（超声波、蓝牙等），
 * 建议单独建 ext_xxx.c/.h，不放在本文件里。 */

#endif /* __FWLIB_EXT_IO_H */

