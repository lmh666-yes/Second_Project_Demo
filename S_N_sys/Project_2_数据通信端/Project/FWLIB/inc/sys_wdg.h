#ifndef __FWLIB_SYS_WDG_H
#define __FWLIB_SYS_WDG_H

#include "stm32f4xx.h"

/* sys_wdg.h: 看门狗模块
 * IWDG 独立看门狗（区块 2）+ WWDG 窗口看门狗（区块 3）
 *
 * IWDG 时钟源为 LSI 内部低速 RC，约 32kHz，与主频无关：主时钟配置跑飞时看门狗仍工作。
 * 启动后无法关闭，只能复位。超时按 LSI 典型值 32kHz 换算，LSI 实际频率
 * 约 17k~47kHz，时间精度要求高时实测后改 SYS_WDG_LSI_HZ。
 * 初始化时自动开启调试暂停冻结，Keil 下断点、单步时看门狗不计数。
 *
 * 两种用法：
 *   1) SYS_WDG_Init(2000)，主循环里定期 SYS_WDG_Feed()。
 *   2) 心跳汇总：SYS_WDG_HEARTBEAT_COUNT 填任务数，各任务循环调
 *      SYS_WDG_Heartbeat(id)，主循环只留 SYS_WDG_HeartbeatPoll()，全员到齐才喂狗。
 *      喂狗不要分散到各任务，否则一个任务卡死、其它任务照常喂狗，看门狗失效。
 *
 * 超时选取参考：LED/按键轮询、串口收发、DWT 延时属毫秒级，超时 ≥ 1s；
 * LCD 整屏刷新、大块 SPI/FLASH 连续写属百毫秒级，超时 ≥ 2s；
 * FLASH 扇区擦除（128K 扇区可达约 1s）、SYS_ETH_Init 自协商等待最长数秒，超时 ≥ 3s。
 * 等外部事件（KEY_WaitPress）时限不可控，不要与喂狗同线程空等，改用非阻塞
 * （KEY_Scan）循环加心跳报到，或独立喂狗线程。
 *
 * 与其他模块的联动：
 *   sys_pwr：Stop 模式下 LSI 继续运行、看门狗继续计数，休眠超过超时会复位，
 *            休眠前评估超时，或唤醒后立即喂狗；
 *   sys_clock：WWDG 超时随 PCLK1 变化，切换主频后需重新 SYS_WDG_WwdgInit()；
 *              IWDG 用 LSI，不受切换影响；
 *   sys_fault：故障捕获配合看门狗复位，重启后用 SYS_WDG_ResetCause() 判断原因。 */


/* 区块 1：定义与宏 */
/* LSI 频率（Hz），用于超时换算。芯片手册典型值 32kHz；
 * 实测偏差大时改成实测值（一般落在 17k ~ 47k 之间） */
#define SYS_WDG_LSI_HZ          32000UL

/* 1 = 调试器暂停 CPU 时看门狗冻结；0 = 不冻结 */
#define SYS_WDG_DEBUG_FREEZE    1

/* 超时允许范围（ms），超出自动截断到边界 */
#define SYS_WDG_MIN_TIMEOUT_MS  1UL
#define SYS_WDG_MAX_TIMEOUT_MS  32768UL    /* 上限来自 256×4096/32kHz ≈ 32.8s */

/* 多任务心跳汇总（区块 3 心跳组使用）
 * 参与心跳喂狗的任务个数：0 = 不使用（默认）；1 ~ 32
 * 非 0 时每个任务分配一个 id（0 ~ 数量-1），任务循环里调 SYS_WDG_Heartbeat(id)，
 * 主循环只留 SYS_WDG_HeartbeatPoll()，全员到齐才喂狗；
 * 为 0 时 SYS_WDG_HeartbeatPoll() 退化为直接喂狗，返回 1。
 * 任一任务卡死则该位不置位，不喂狗，等看门狗复位 */
#define SYS_WDG_HEARTBEAT_COUNT   0

/* 复位原因标志（SYS_WDG_ResetCause 返回值，按位组合） */
#define SYS_WDG_RST_POR     (1UL << 0)     /* 上电复位          */
#define SYS_WDG_RST_PIN     (1UL << 1)     /* 复位引脚复位      */
#define SYS_WDG_RST_BOR     (1UL << 2)     /* 欠压复位          */
#define SYS_WDG_RST_SOFT    (1UL << 3)     /* 软件复位          */
#define SYS_WDG_RST_IWDG    (1UL << 4)     /* 独立看门狗复位    */
#define SYS_WDG_RST_WWDG    (1UL << 5)     /* 窗口看门狗复位    */
#define SYS_WDG_RST_LPWR    (1UL << 6)     /* 低功耗模式复位    */


/* 区块 2：基础功能 */
/* 启动 IWDG：timeout_ms 毫秒内必须喂一次狗，否则复位
 * 参数 : timeout_ms : 超时时间（1 ~ 32768ms，超范围自动截断）
 * 说明 : 内部换算分频与重载值；启动后无法关闭
 * 标准库 : IWDG_WriteAccessCmd + IWDG_SetPrescaler + IWDG_SetReload
 *          + IWDG_ReloadCounter + IWDG_Enable */
void SYS_WDG_Init(uint32_t timeout_ms);

/* 喂狗：复位 IWDG 计数，须在超时前反复调用
 * 标准库 : IWDG_ReloadCounter */
void SYS_WDG_Feed(void);


/* 区块 3：扩展功能 */

/* 复位原因诊断（与 sys_fault 配合，判断重启原因） */

/* 读取上次复位原因，返回 SYS_WDG_RST_xxx 的位组合
 * 标准库 : RCC_GetFlagStatus（逐个查 RCC_FLAG_xxxRST） */
uint32_t SYS_WDG_ResetCause(void);

/* 清除复位原因标志，判定之后调用，避免影响下次判断
 * 标准库 : RCC_ClearFlag */
void SYS_WDG_ClearResetFlags(void);

/* 把复位原因解码成短文字（纯 ASCII），如 "POR" / "IWDG" / "SOFT,PIN"
 * 返回 : 实际写入长度（不含结尾 '\0'）；缓冲区不足自动截断 */
uint32_t SYS_WDG_ResetCauseDecode(uint32_t cause, char *buf, uint32_t size);

/* 多任务心跳汇总喂狗（联动各业务任务）
 * 用 32 位掩码记录各任务是否报到，置位在短临界区内完成；
 * Poll 里全员到齐则喂狗并清零掩码，开始新一轮。 */

/* 任务报到，可在任意上下文调用（内部短临界区保护，无需外部关中断）
 * 参数 : id : 任务编号（0 ~ SYS_WDG_HEARTBEAT_COUNT-1，越界忽略）
 * 标准库 : 无直接对应，掩码置位（__disable_irq / __enable_irq） */
void SYS_WDG_Heartbeat(uint8_t id);

/* 是否全员报到：1 = 都到；不清掩码、不喂狗（WWDG、自定义策略用） */
uint8_t SYS_WDG_HeartbeatAll(void);

/* 还有哪些任务没报到：返回缺位掩码，bit id = 1 表示该任务没到，0 = 全到
 * 用于上报复位原因或定位卡住的任务 */
uint32_t SYS_WDG_HeartbeatPending(void);

/* 心跳汇总喂狗，在主循环或最低优先级任务里反复调用：
 *   SYS_WDG_HEARTBEAT_COUNT > 0：全员报到 → SYS_WDG_Feed() + 清零掩码，返回 1；
 *       仍有缺位 → 不喂狗，返回 0，缺位持续则等看门狗复位
 *   SYS_WDG_HEARTBEAT_COUNT == 0：退化为直接喂狗（等价 SYS_WDG_Feed()），恒返回 1
 * 清零瞬间的报到可能丢失，任务下一轮会再报，属正常 */
uint8_t SYS_WDG_HeartbeatPoll(void);

/* 手动清零掩码，开始新一轮；SYS_WDG_HEARTBEAT_COUNT = 0 时无动作 */
void SYS_WDG_HeartbeatClear(void);

/* 窗口看门狗 WWDG（需在 Keil RTE 勾选 WWDG 组件）
 * 比 IWDG 严格：喂太早（计数器还在窗口上方）或喂太晚都复位，
 * 用于检测卡死前的异常快循环。超时随 PCLK1 变化 */

/* 启动 WWDG：timeout_ms 为从启动到复位的允许时间窗上限
 * 说明 : 越界自动截断；窗口默认全开（任何时刻都能喂），
 *        需要严格窗口时自行调用 WWDG_SetWindowValue 收紧
 * 标准库 : RCC_APB1PeriphClockCmd(WWDG) + RCC_GetClocksFreq +
 *          WWDG_SetPrescaler + WWDG_SetWindowValue + WWDG_Enable */
void SYS_WDG_WwdgInit(uint32_t timeout_ms);

/* 喂 WWDG，保持与初始化相同的计数值
 * 标准库 : WWDG_SetCounter */
void SYS_WDG_WwdgFeed(void);

#endif /* __FWLIB_SYS_WDG_H */
