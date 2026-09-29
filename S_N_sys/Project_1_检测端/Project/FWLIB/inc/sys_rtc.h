#ifndef __FWLIB_SYS_RTC_H
#define __FWLIB_SYS_RTC_H

#include "stm32f4xx.h"
/* RTC 组件未随 RTE 勾选时 stm32f4xx_conf.h 不会带进来——这里显式包含
 * （header 自带包含守卫,后续若在 RTE 勾选 RTC 也不会重复包含） */
#include "stm32f4xx_rtc.h"

/* ================================================================
 *  sys_rtc.h —— 【系统】RTC 实时时钟模块  头文件
 * ================================================================
 *  设计定位 : 备份域"日历时钟"的薄封装（参照《26_RTC时钟源配置》
 *             练习移植）——走时/日历/闹钟/秒中断/备份寄存器
 *  标准库关键词 : PWR_BackupAccessCmd / RCC_LSEConfig / RCC_RTCCLKConfig /
 *                 RCC_RTCCLKCmd / RTC_Init / RTC_SetTime / RTC_GetTime /
 *                 RTC_SetDate / RTC_GetDate / RTC_SetAlarm / RTC_AlarmCmd /
 *                 RTC_WakeUpCmd / RTC_SetWakeUpCounter / RTC_ITConfig /
 *                 RTC_WriteBackupRegister / RTC_ReadBackupRegister
 *
 *  时钟源（区块 1 的 SYS_RTC_CLK_SRC 二选一）:
 *      LSE（默认）: 外部 32.768kHz 晶振——准,锂电池/纽扣电池保走时
 *      LSI        : 内部低速 RC（约 32kHz,偏差大）——没晶振时的兜底
 *  预分频 : 内部自动按时钟源填（LSE 128×256 / LSI 128×250 → 1Hz）
 *
 *  使用方式 :
 *      if (SYS_RTC_Init() == SYS_RTC_OK) {          // ① 起时钟 + 配置
 *          SYS_RTC_SetDate(2026, 9, 28);            // ② 日期(星期自动算)
 *          SYS_RTC_SetTime(17, 52, 0);              // ③ 时间(24 小时制)
 *      }
 *      SYS_RTC_SetWakeUpCallback(On1s);             // ④ 秒中断回调
 *      SYS_RTC_WakeUp1sOn();
 *
 *  与其它模块的联动（典型）:
 *   - 与 sys_usart：开机打印时间（GetTime/GetDate → SendFormat/SendLine）;
 *   - 与 beep/led：闹钟回调里"叫铃/闪灯"（Block3 回调机制,与 LED/BEEP 组合）;
 *   - 与 sys_softimer：秒中断(1s)驱动软定时器/节拍任务;
 *   - 备份寄存器：存"首次上电标记/校准值"（复位不丢,LSE 走时也不丢）。
 *  ⚠ 提醒 : 闹钟/秒中断的 ISR 已由本模块实现（__weak,回调机制）——
 *     你手写同名 RTC_Alarm_IRQHandler / RTC_WKUP_IRQHandler 时二选一。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* ---------------- RTC 时钟源 ----------------
 * 0 = LSE（外部 32.768kHz 晶振,推荐）;1 = LSI（内部 RC,兜底） */
#define SYS_RTC_CLK_SRC     0

/* 等待起振/就绪的最大循环次数（防晶振异常时死等） */
#define SYS_RTC_CLK_TIMEOUT 1000000UL

/* 闹钟/秒中断的 NVIC 优先级（配合库默认 NVIC_PriorityGroup_2） */
#define SYS_RTC_IRQ_PRE_PRIO   2
#define SYS_RTC_IRQ_SUB_PRIO   0

/* 备份寄存器个数（F4 共 20 个:编号 0 ~ 19） */
#define SYS_RTC_BKP_COUNT      20

/* 返回值 */
#define SYS_RTC_OK          0   /* 成功 */
#define SYS_RTC_ERR_CLK     1   /* 时钟源起振超时（查晶振/负载电容） */
#define SYS_RTC_ERR_PARAM   2   /* 参数非法（越界/空指针） */

/* 编译期自检 : 时钟源只能填 0 / 1 */
#if (SYS_RTC_CLK_SRC != 0) && (SYS_RTC_CLK_SRC != 1)
/* 注意 : #error 文本必须是 ASCII */
#error "SYS_RTC_CLK_SRC must be 0 (LSE) or 1 (LSI)"
#endif


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：PWR 备份域访问 + 起时钟源 + RTC 参数（24 小时制）
 * 标准库调用链（库内部依次调用，可对照学习）:
 *   ① RCC_APB1PeriphClockCmd(PWR) + PWR_BackupAccessCmd(ENABLE)  开备份域
 *   ② RCC_LSEConfig / RCC_LSICmd + 等就绪（带超时,见 SYS_RTC_CLK_TIMEOUT）
 *   ③ RCC_RTCCLKConfig + RCC_RTCCLKCmd   选 RTC 时钟
 *   ④ RTC_Init  预分频(异步 127 / 同步按源自动) + 24 小时制
 * 返回 : SYS_RTC_OK / SYS_RTC_ERR_CLK（时钟起振失败）
 * 说明 : 可重复调用（重新配置参数,不清走时）;
 *        时钟与备份域的管理借力 sys_clock 的 LSE/LSI/RTC 辅助函数
 * 示例 : if (SYS_RTC_Init() != SYS_RTC_OK) { 排查晶振; } */
uint8_t SYS_RTC_Init(void);

/* 设置时间（十进制入参，库内部转 BCD 写入）
 * 参数 : h 0~23 / m 0~59 / s 0~59（越界自动截断到上限）
 * 标准库 : RTC_SetTime(RTC_Format_BCD, …)（H12 字段自动 AM/PM 填好） */
void SYS_RTC_SetTime(uint8_t h, uint8_t m, uint8_t s);

/* 读取当前时间（输出十进制,可直接打印;指针为空则忽略该项）
 * 标准库 : RTC_GetTime(RTC_Format_BCD, …) */
void SYS_RTC_GetTime(uint8_t *h, uint8_t *m, uint8_t *s);

/* 设置日期（星期由蔡勒公式自动推算,无需手填）
 * 参数 : year 2000~2099 / month 1~12 / day 1~31（越界自动截断）
 * 标准库 : RTC_SetDate(RTC_Format_BCD, …) */
void SYS_RTC_SetDate(uint16_t year, uint8_t month, uint8_t day);

/* 读取日期（year 输出 20xx;weekday 1=周一 … 7=周日;指针为空则忽略） */
void SYS_RTC_GetDate(uint16_t *year, uint8_t *month, uint8_t *day, uint8_t *weekday);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 星期推算（蔡勒公式,公历）:返回 1=周一 … 7=周日 */
uint8_t SYS_RTC_WeekdayFromDate(uint16_t year, uint8_t month, uint8_t day);

/* ---- 闹钟 A（三种生效方式任选,回调机制）----
 * 前提 : 调用过 SYS_RTC_Init;回调里"叫铃/亮灯/发串口"随你组合 */

/* 每天生效：每天 hh:mm:ss 触发一次回调 */
void SYS_RTC_SetAlarmDaily(uint8_t h, uint8_t m, uint8_t s);

/* 指定星期几生效（weekday: 1=周一 … 7=周日,见 RTC_Weekday_x） */
void SYS_RTC_SetAlarmWeekday(uint8_t weekday, uint8_t h, uint8_t m, uint8_t s);

/* 指定"每月几号"生效（date: 1~31） */
void SYS_RTC_SetAlarmDate(uint8_t date, uint8_t h, uint8_t m, uint8_t s);

/* 关闭闹钟 A */
void SYS_RTC_AlarmOff(void);

/* ---- 秒中断（唤醒定时器 1 秒一响）---- */
/* 开启 1 秒周期中断（唤醒定时器 CK_SPRE 16 位 / 计数 0 → 1s）
 * 返回 : SYS_RTC_OK;标准库 : RTC_WakeUpClockConfig + RTC_SetWakeUpCounter
 *        + RTC_ITConfig(WUT) + EXTI(Line22) + NVIC(RTC_WKUP_IRQn) */
uint8_t SYS_RTC_WakeUp1sOn(void);

/* 关闭秒中断 */
void SYS_RTC_WakeUpOff(void);

/* ---- 回调注册（在开启中断之前注册好）---- */
void SYS_RTC_SetAlarmCallback (void (*callback)(void));   /* 闹钟触发时执行 */
void SYS_RTC_SetWakeUpCallback(void (*callback)(void));   /* 每秒触发时执行 */

/* ---- 备份寄存器（复位/断电(有电池)不丢,存"首次标记/校准值"）---- */
/* 写 / 读：n = 0 ~ 19（越界忽略 / 返回 0）
 * 例 : SYS_RTC_BackupWrite(0, 0x8888);          // 首次配置完打个标
 *      if (SYS_RTC_BackupRead(0) != 0x8888) {}  // 上电判断"首次上电" */
void     SYS_RTC_BackupWrite(uint8_t n, uint16_t value);
uint16_t SYS_RTC_BackupRead (uint8_t n);

#endif /* __FWLIB_SYS_RTC_H */
