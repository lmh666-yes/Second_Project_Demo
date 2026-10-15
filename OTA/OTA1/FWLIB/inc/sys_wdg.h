#ifndef __FWLIB_SYS_WDG_H
#define __FWLIB_SYS_WDG_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_wdg.h  看门狗模块：IWDG 独立看门狗（区块 2）+ WWDG 窗口看门狗（区块 3）
 *  IWDG 库函数 : IWDG_WriteAccessCmd / IWDG_SetPrescaler / IWDG_SetReload /
 *                IWDG_ReloadCounter / IWDG_Enable（复位原因 RCC_GetFlagStatus）
 *
 *  IWDG 特性 : 时钟源 LSI（约 32kHz），与主频无关，主时钟异常时仍计数；
 *   启动后无法停止；初始化时开启调试冻结，调试器暂停 CPU 时不计数；
 *   超时按 LSI 典型值 32kHz 换算，实际约 17k~47kHz，精度要求高时改
 *   SYS_WDG_LSI_HZ。
 *
 *  用法 : 简单式 SYS_WDG_Init(2000) + 主循环 SYS_WDG_Feed()；多任务用
 *   SYS_WDG_HEARTBEAT_COUNT 填任务数、各任务 SYS_WDG_Heartbeat(id) 报到、
 *   主循环只留 SYS_WDG_HeartbeatPoll()。
 *
 *  超时选取 : 毫秒级任务（LED/按键轮询、串口收发、DWT 延时）≥ 1s；百毫秒级
 *   （LCD 整屏刷新、大块 SPI/FLASH 连续写）≥ 2s；秒级（FLASH 扇区擦除、
 *   SYS_ETH_Init 自协商等待）≥ 3s；KEY_WaitPress 等外部事件时限不可控，改用
 *   非阻塞 KEY_Scan 循环 + 心跳报到，或喂狗线程独立。
 *
 *  与其它模块 : Stop 模式下 LSI 继续运行、看门狗继续计数，休眠超过超时会复位；
 *   WWDG 超时随 PCLK1 变化，切主频后需重新 SYS_WDG_WwdgInit()，IWDG 不受影响；
 *   重启后 SYS_WDG_ResetCause() 判断复位来源。
 * ================================================================ */


/* 区块 1：定义与宏定义区 */
/* LSI 频率（Hz）：超时换算用。芯片手册典型值 32kHz，
 * 实测偏差大时改为实测值（约 17k ~ 47k） */
#define SYS_WDG_LSI_HZ          32000UL

/* 1 = 调试器暂停 CPU 时看门狗冻结；0 = 不冻结 */
#define SYS_WDG_DEBUG_FREEZE    1

/* 超时允许范围（ms）：超出自动截断到边界 */
#define SYS_WDG_MIN_TIMEOUT_MS  1UL
#define SYS_WDG_MAX_TIMEOUT_MS  32768UL    /* 最大 ≈ 256×4096/32kHz ≈ 32.8s */

/* ------ 多任务心跳汇总（区块 3 心跳组使用） ------
 * 参与心跳喂狗的任务数：1 ~ 32，任务 id 取 0 ~ 计数-1（如 4）；0 = 禁用死任务检测 */
#define SYS_WDG_HEARTBEAT_COUNT   0

/* 复位原因标志（SYS_WDG_ResetCause 返回值，按位组合） */
#define SYS_WDG_RST_POR     (1UL << 0)     /* 上电复位 */
#define SYS_WDG_RST_PIN     (1UL << 1)     /* 复位引脚复位 */
#define SYS_WDG_RST_BOR     (1UL << 2)     /* 欠压复位 */
#define SYS_WDG_RST_SOFT    (1UL << 3)     /* 软件复位 */
#define SYS_WDG_RST_IWDG    (1UL << 4)     /* 独立看门狗复位 */
#define SYS_WDG_RST_WWDG    (1UL << 5)     /* 窗口看门狗复位 */
#define SYS_WDG_RST_LPWR    (1UL << 6)     /* 低功耗模式复位 */


/* 区块 2：基础功能 */
/* 启动独立看门狗 IWDG：timeout_ms 毫秒内必须喂狗一次，否则复位
 * 参数 : timeout_ms 超时时间（1 ~ 32768ms，超范围截断到边界）
 * 说明 : 内部换算分频与重载值（LSI 32kHz），启动后无法关闭
 * 示例 : SYS_WDG_Init(2000);      // 2 秒超时；主循环里定期 SYS_WDG_Feed() */
void SYS_WDG_Init(uint32_t timeout_ms);

/* 喂狗（复位 IWDG 计数，须在超时前重复调用）
 * 示例 : while (1) { 关键任务们(); SYS_WDG_Feed(); } */
void SYS_WDG_Feed(void);


/* 区块 3：扩展功能 */
/* ---- 复位原因诊断（与 sys_fault 配合）---- */

/* 读上次复位原因（返回 SYS_WDG_RST_xxx 的位组合）
 * 标准库 : RCC_GetFlagStatus（逐个查 RCC_FLAG_xxxRST） */
uint32_t SYS_WDG_ResetCause(void);

/* 清复位原因标志（判定后调用，避免影响下次判断） */
void SYS_WDG_ClearResetFlags(void);

/* 复位原因解码为短文字（纯 ASCII），如 "POR" / "IWDG" / "SOFT,PIN"
 * 返回 : 实际写入长度（不含结尾 '\0'）；缓冲区不足截断
 * 示例 : SYS_WDG_ResetCauseDecode(SYS_WDG_ResetCause(), line, sizeof(line)); */
uint32_t SYS_WDG_ResetCauseDecode(uint32_t cause, char *buf, uint32_t size);

/* ---- 多任务心跳汇总喂狗 ----
 * 原理 : 32 位掩码（短临界区保护置位）记录报到；Poll 全员到齐后喂狗并清零，
 *        开始新一轮。 */

/* 任务报到（可在任意上下文调用：内部短临界区保护，无需外部关中断）
 * 参数 : id 任务编号（0 ~ SYS_WDG_HEARTBEAT_COUNT-1，越界忽略）
 * 标准库 : 无对应函数，掩码置位（短临界区：__disable_irq / __enable_irq）
 * 示例 : void TaskA(void) { while (1) { 干活(); SYS_WDG_Heartbeat(0); } } */
void SYS_WDG_Heartbeat(uint8_t id);

/* 是否全员报到（1 = 都到；不清掩码、不喂狗，供 WWDG/自定义策略用）
 * 示例 : if (SYS_WDG_HeartbeatAll()) { SYS_WDG_WwdgFeed(); SYS_WDG_HeartbeatClear(); } */
uint8_t SYS_WDG_HeartbeatAll(void);

/* 未报到任务掩码（bit id = 1 = 该任务未到；0 = 全到）
 * 用途 : 复位原因上报或联调时定位卡死任务。 */
uint32_t SYS_WDG_HeartbeatPending(void);

/* 心跳汇总喂狗（主循环/最低优先级任务里反复调用）:
 *   SYS_WDG_HEARTBEAT_COUNT > 0：全员报到 → SYS_WDG_Feed() + 清零掩码，返回 1；
 *   有缺位 → 不喂狗，返回 0。计数 == 0 时退化为直接喂狗，恒返回 1。
 * 注 : 清零瞬间的报到可能丢失，任务下一轮再报。
 * 示例 : while (1) { 自己的活(); SYS_WDG_Heartbeat(1); SYS_WDG_HeartbeatPoll(); } */
uint8_t SYS_WDG_HeartbeatPoll(void);

/* 手动清零掩码，开始新一轮（SYS_WDG_HEARTBEAT_COUNT = 0 时无动作） */
void SYS_WDG_HeartbeatClear(void);

/* ---- 窗口看门狗 WWDG（需在 Keil RTE 勾选 WWDG 组件）----
 * 喂太早（计数器在窗口上方）或喂太晚都复位，用于检测卡死前的异常快循环；
 * 超时随 PCLK1 变化 */

/* 启动 WWDG：timeout_ms 为从启动到复位的允许时间窗上限
 * 说明 : 超范围截断；窗口默认全开，需严格窗口时自行调用 WWDG_SetWindowValue
 * 标准库 : RCC_APB1PeriphClockCmd(WWDG) + RCC_GetClocksFreq + WWDG_SetPrescaler
 *          + WWDG_SetWindowValue + WWDG_Enable */
void SYS_WDG_WwdgInit(uint32_t timeout_ms);

/* 喂 WWDG（保持与初始化相同的计数值；标准库 WWDG_SetCounter） */
void SYS_WDG_WwdgFeed(void);

#endif /* __FWLIB_SYS_WDG_H */
