#ifndef __FWLIB_SYS_RTC_H
#define __FWLIB_SYS_RTC_H

#include "stm32f4xx.h"
/* RTC 未在 RTE 勾选时 stm32f4xx_conf.h 不带入本头文件,故显式包含 */
#include "stm32f4xx_rtc.h"

/* sys_rtc.h — RTC 实时时钟模块 头文件
 *  功能 : 备份域日历时钟封装,含走时/日历/闹钟/秒中断/备份寄存器。
 *  时钟源 : LSE 外部 32.768kHz 晶振;LSI 内部约 32kHz RC。
 *  预分频 : 按时钟源自动填,LSE 128×256 / LSI 128×250 → 1Hz。
 *  用法 :
 *      if (SYS_RTC_Init() == SYS_RTC_OK) {   // 起时钟 + 配置
 *          SYS_RTC_SetDate(2026, 9, 28);
 *          SYS_RTC_SetTime(17, 52, 0);
 *      }
 *      SYS_RTC_SetWakeUpCallback(On1s);
 *      SYS_RTC_WakeUp1sOn();
 *  闹钟与秒中断的 ISR 由本模块以 __weak 提供并转回调;手写同名
 *  RTC_Alarm_IRQHandler / RTC_WKUP_IRQHandler 时二选一。 */


/* 区块 1：定义与宏定义区 */
/* RTC 时钟源 : 0 = LSE(外部 32.768kHz 晶振);1 = LSI(内部 RC) */
#define SYS_RTC_CLK_SRC     0

/* 等待时钟源起振/就绪的最大循环次数,超时返回 SYS_RTC_ERR_CLK */
#define SYS_RTC_CLK_TIMEOUT 1000000UL

/* 中断优先级 : 越小越高。范围随 sys_nvic 分组:NVIC_PriorityGroup_4(默认)
 *   抢占 0~15、子固定 0;NVIC_PriorityGroup_2 抢占 0~3、子 0~3。
 *   用 FreeRTOS 时抢占 ≥5:内核 BASEPRI = MAX_SYSCALL_INTERRUPT_PRIORITY<<4,
 *   本工程 5<<4 = 0x50;抢占 < 5 会打断临界区,其中禁用 ...FromISR()。 */
#define SYS_RTC_IRQ_PRE_PRIO   5
#define SYS_RTC_IRQ_SUB_PRIO   0

/* 备份寄存器用户可用个数。F4 共 20 个 DR0~DR19,DR0 被 SPL 占用,故为 19 */
#define SYS_RTC_BKP_COUNT      19

/* 用户编号 n → 物理 DR(n+1) 的偏移,跳过 SPL 占用的 DR0 */
#define SYS_RTC_BKP_BASE_OFF   1

/* 返回值 */
#define SYS_RTC_OK          0   /* 成功 */
#define SYS_RTC_ERR_CLK     1   /* 时钟源起振超时,查晶振与负载电容 */
#define SYS_RTC_ERR_PARAM   2   /* 参数非法,越界或空指针 */

/* 编译期检查 SYS_RTC_CLK_SRC 只能为 0 / 1 */
#if (SYS_RTC_CLK_SRC != 0) && (SYS_RTC_CLK_SRC != 1)
/* #error 文本须为 ASCII */
#error "SYS_RTC_CLK_SRC must be 0 (LSE) or 1 (LSI)"
#endif


/* 区块 2：基础功能 */
/* 初始化 : 开 PWR 备份域访问 + 启动时钟源 + RTC 参数(24 小时制)。库内部
 * 顺序 : RCC_APB1PeriphClockCmd(PWR) + PWR_BackupAccessCmd(ENABLE);
 *   RCC_LSEConfig / RCC_LSICmd 等就绪,超时见 SYS_RTC_CLK_TIMEOUT;
 *   RCC_RTCCLKConfig + RCC_RTCCLKCmd;RTC_Init 预分频(异步 127,同步自动)。
 * 返回 SYS_RTC_OK / SYS_RTC_ERR_CLK(起振超时)。可重复调用;时钟与备份域由
 *   sys_clock 的 LSE/LSI/RTC 辅助函数管理 */
uint8_t SYS_RTC_Init(void);

/* 设置时间。h 0~23 / m 0~59 / s 0~59,越界截断到上限。十进制入参,
 * 经 RTC_SetTime(RTC_Format_BCD, …) 转 BCD 写入,H12 的 AM/PM 自动填 */
void SYS_RTC_SetTime(uint8_t h, uint8_t m, uint8_t s);

/* 读取当前时间,输出十进制,经 RTC_GetTime(RTC_Format_BCD, …) 取得;
 * 指针为空则忽略该项 */
void SYS_RTC_GetTime(uint8_t *h, uint8_t *m, uint8_t *s);

/* 设置日期。year 2000~2099 / month 1~12 / day 1~31,越界截断。
 * 经 RTC_SetDate(RTC_Format_BCD, …) 写入;星期按蔡勒公式推算 */
void SYS_RTC_SetDate(uint16_t year, uint8_t month, uint8_t day);

/* 读取日期 : year 输出 20xx;weekday 1=周一 … 7=周日;空指针忽略该项 */
void SYS_RTC_GetDate(uint16_t *year, uint8_t *month, uint8_t *day, uint8_t *weekday);


/* 区块 3：扩展功能 */
/* 星期推算(蔡勒公式,公历),返回 1=周一 … 7=周日 */
uint8_t SYS_RTC_WeekdayFromDate(uint16_t year, uint8_t month, uint8_t day);

/* ---- 闹钟 A : 三种生效方式任选,均经回调转出;前提是已调用 SYS_RTC_Init ---- */

/* 每天 hh:mm:ss 触发一次 */
void SYS_RTC_SetAlarmDaily(uint8_t h, uint8_t m, uint8_t s);

/* 指定星期几生效。weekday 1=周一 … 7=周日,取值见 RTC_Weekday_x */
void SYS_RTC_SetAlarmWeekday(uint8_t weekday, uint8_t h, uint8_t m, uint8_t s);

/* 指定每月几号生效。date 1~31 */
void SYS_RTC_SetAlarmDate(uint8_t date, uint8_t h, uint8_t m, uint8_t s);

/* 关闭闹钟 A */
void SYS_RTC_AlarmOff(void);

/* ---- 秒中断(唤醒定时器) ---- */
/* 开启 1 秒周期中断 : 唤醒定时器 CK_SPRE 16 位,计数 0 → 1s。返回
 * SYS_RTC_OK。库调用 : RTC_WakeUpClockConfig + RTC_SetWakeUpCounter
 *   + RTC_ITConfig(WUT) + EXTI(Line22) + NVIC(RTC_WKUP_IRQn) */
uint8_t SYS_RTC_WakeUp1sOn(void);

/* 关闭秒中断 */
void SYS_RTC_WakeUpOff(void);

/* ---- 回调注册 : 需在开启对应中断之前完成 ---- */
void SYS_RTC_SetAlarmCallback (void (*callback)(void));   /* 闹钟触发时执行 */
void SYS_RTC_SetWakeUpCallback(void (*callback)(void));   /* 1 秒中断触发时执行 */

/* ---- 备份寄存器 : 复位及有电池时断电不丢,存首次标记/校准值 ---- */
/* 写 / 读 : n = 0 ~ 18,越界时写忽略、读返回 0。
 * 依据 : SPL 在 RTC_Init() 用 RTC_BKP_DR0 存首次上电魔数 0x32F2,判断
 *   备份域刚上电、RTC 未配置,故物理 DR0 不开放,n 映射到 DR(n+1);直接
 *   写 DR0 会使下次冷启动的首次上电判断失效,时间与预分频重配不确定。
 * 例 : SYS_RTC_BackupWrite(0, 0x8888);          // 写物理 DR1
 *      if (SYS_RTC_BackupRead(0) != 0x8888) {}  // 判断首次上电 */
void     SYS_RTC_BackupWrite(uint8_t n, uint16_t value);
uint16_t SYS_RTC_BackupRead (uint8_t n);

#endif /* __FWLIB_SYS_RTC_H */
