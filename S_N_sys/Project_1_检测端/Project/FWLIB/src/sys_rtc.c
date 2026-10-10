#include "sys_rtc.h"
/* 配套说明见同名 .h;本文件为实现层 */
#include "sys_clock.h"      /* LSE/LSI/RTC 时钟辅助 */

/* sys_rtc.c — RTC 实时时钟模块 实现层;模块说明见同名 .h
 *  时钟准备调用 sys_clock 的 SYS_CLK_LseOn/LsiOn/RtcClkSelect;
 *  日历/闹钟/秒中断调用标准库 RTC_xxx(stm32f4xx_rtc.c),由 RTE
 *  组件 StdPeriph Drivers:RTC 带入编译。
 *  分频依据:1Hz = 时钟 / ((异步+1) × (同步+1)),见 RM0090 第 26 章。 */

/* 按 SYS_RTC_CLK_SRC 选预分频 */
#if (SYS_RTC_CLK_SRC == 0)
    #define RTC_ASYNC_PREDIV   127U     /* LSE 32768 / 128 / 256 = 1Hz */
    #define RTC_SYNC_PREDIV    255U
#else
    #define RTC_ASYNC_PREDIV   127U     /* LSI 32000 / 128 / 250 = 1Hz */
    #define RTC_SYNC_PREDIV    249U
#endif

/* 内部工具 */

/* 十进制 → BCD（如 23 → 0x23） */
static uint8_t rtc_dec2bcd(uint8_t v)
{
    return (uint8_t)(((v / 10U) << 4) | (v % 10U));
}

/* BCD → 十进制 */
static uint8_t rtc_bcd2dec(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10U) + (v & 0x0FU));
}

/* 闹钟中断路径使能:RTC_IT_ALRA + EXTI 线 17 + NVIC;幂等 */
static void rtc_alarm_it_enable(void)
{
    EXTI_InitTypeDef ei;
    NVIC_InitTypeDef ni;

    RTC_ClearITPendingBit(RTC_IT_ALRA);
    EXTI_ClearITPendingBit(EXTI_Line17);
    RTC_ITConfig(RTC_IT_ALRA, ENABLE);

    ei.EXTI_Line    = EXTI_Line17;              /* RTC 闹钟的固定 EXTI 线 */
    ei.EXTI_Mode    = EXTI_Mode_Interrupt;
    ei.EXTI_Trigger = EXTI_Trigger_Rising;
    ei.EXTI_LineCmd = ENABLE;
    EXTI_Init(&ei);

    ni.NVIC_IRQChannel                   = RTC_Alarm_IRQn;
    ni.NVIC_IRQChannelPreemptionPriority = SYS_RTC_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_RTC_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);
}

/* 闹钟公共配置:关闭闹钟 A → 填时间/生效方式/屏蔽 → 再使能 */
static void rtc_alarm_apply(uint32_t mask, uint32_t sel, uint8_t day,
                            uint8_t h, uint8_t m, uint8_t s)
{
    RTC_AlarmTypeDef al;

    RTC_AlarmCmd(RTC_Alarm_A, DISABLE);

    al.RTC_AlarmTime.RTC_H12     = (h >= 12U) ? RTC_H12_PM : RTC_H12_AM;
    al.RTC_AlarmTime.RTC_Hours   = rtc_dec2bcd(h);
    al.RTC_AlarmTime.RTC_Minutes = rtc_dec2bcd(m);
    al.RTC_AlarmTime.RTC_Seconds = rtc_dec2bcd(s);

    al.RTC_AlarmMask            = mask;         /* 屏蔽"日期/星期"的匹配 */
    al.RTC_AlarmDateWeekDaySel  = sel;          /* 按日期 or 按星期 */
    al.RTC_AlarmDateWeekDay     = rtc_dec2bcd(day);

    RTC_SetAlarm(RTC_Format_BCD, RTC_Alarm_A, &al);
    RTC_AlarmCmd(RTC_Alarm_A, ENABLE);
}

/* 回调函数指针（0 = 未注册） */
static void (*rtc_alarm_cb)(void) = 0;
static void (*rtc_wkup_cb)(void)  = 0;


/* 区块 2:基础功能 */

/* 初始化:备份域 → 时钟源(带超时) → RTC 参数(24 小时制) */
uint8_t SYS_RTC_Init(void)
{
    RTC_InitTypeDef ri;

    /* 开 PWR 时钟与备份域访问(RTC/备份寄存器属备份域) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    /* 启动时钟源;超时由 sys_clock 辅助函数内部处理 */
#if (SYS_RTC_CLK_SRC == 0)
    if (SYS_CLK_LseOn() != SYS_CLK_OK) return SYS_RTC_ERR_CLK;
    SYS_CLK_RtcClkSelect(RCC_RTCCLKSource_LSE);
#else
    if (SYS_CLK_LsiOn() != SYS_CLK_OK) return SYS_RTC_ERR_CLK;
    SYS_CLK_RtcClkSelect(RCC_RTCCLKSource_LSI);
#endif

    /* RTC 参数:预分频 + 24 小时制;RTC_Init 内部处理写保护与初始化模式 */
    ri.RTC_AsynchPrediv = RTC_ASYNC_PREDIV;
    ri.RTC_SynchPrediv  = RTC_SYNC_PREDIV;
    ri.RTC_HourFormat   = RTC_HourFormat_24;
    (void)RTC_Init(&ri);

    return SYS_RTC_OK;
}

/* 设置时间;h/m/s 依次 0~23/0~59/0~59,越界截断 */
void SYS_RTC_SetTime(uint8_t h, uint8_t m, uint8_t s)
{
    RTC_TimeTypeDef t;

    if (h > 23U) h = 23U;
    if (m > 59U) m = 59U;
    if (s > 59U) s = 59U;

    t.RTC_H12     = (h >= 12U) ? RTC_H12_PM : RTC_H12_AM;   /* 24h 下无实际作用,仍填好 */
    t.RTC_Hours   = rtc_dec2bcd(h);
    t.RTC_Minutes = rtc_dec2bcd(m);
    t.RTC_Seconds = rtc_dec2bcd(s);

    (void)RTC_SetTime(RTC_Format_BCD, &t);
}

/* 读取时间;出参十进制,空指针忽略 */
void SYS_RTC_GetTime(uint8_t *h, uint8_t *m, uint8_t *s)
{
    RTC_TimeTypeDef t;

    (void)RTC_GetTime(RTC_Format_BCD, &t);

    if (h != 0) *h = rtc_bcd2dec(t.RTC_Hours);
    if (m != 0) *m = rtc_bcd2dec(t.RTC_Minutes);
    if (s != 0) *s = rtc_bcd2dec(t.RTC_Seconds);
}

static uint8_t rtc_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t dim[12] = {31U, 28U, 31U, 30U, 31U, 30U,
                                    31U, 31U, 30U, 31U, 30U, 31U};
    uint8_t d;

    if (month < 1U || month > 12U) return 31U;
    d = dim[month - 1U];

    if (month == 2U) {
        /* 闰年：4 年一闰、100 年不闰、400 年再闰 */
        if (((year % 4U) == 0U && (year % 100U) != 0U) || ((year % 400U) == 0U)) {
            d = 29U;
        }
    }
    return d;
}

/* 设置日期;year 2000~2099,month 1~12,day 按当月实际天数钳位(闰年 2 月为 29);
 *  RTC 硬件不校验日期,故写入前必须钳位。星期由日期推算后一并写入。 */

void SYS_RTC_SetDate(uint16_t year, uint8_t month, uint8_t day)
{
    RTC_DateTypeDef d;
    uint8_t dmax;

    if (year < 2000U) year = 2000U;
    if (year > 2099U) year = 2099U;
    if (month < 1U)   month = 1U;
    if (month > 12U)  month = 12U;

    dmax = rtc_days_in_month(year, month);      /* 本月天数(含闰年 2 月) */
    if (day < 1U)     day = 1U;
    if (day > dmax)   day = dmax;               /* 钳到本月实际天数 */

    d.RTC_Year    = rtc_dec2bcd((uint8_t)(year - 2000U));   /* 存 00~99 */
    d.RTC_Month   = rtc_dec2bcd(month);
    d.RTC_Date    = rtc_dec2bcd(day);
    d.RTC_WeekDay = SYS_RTC_WeekdayFromDate(year, month, day);

    (void)RTC_SetDate(RTC_Format_BCD, &d);
}

/* 读取日期;year 输出 20xx,weekday 1~7 对应周一~周日 */
void SYS_RTC_GetDate(uint16_t *year, uint8_t *month, uint8_t *day, uint8_t *weekday)
{
    RTC_DateTypeDef d;

    (void)RTC_GetDate(RTC_Format_BCD, &d);

    if (year    != 0) *year    = (uint16_t)(2000U + rtc_bcd2dec(d.RTC_Year));
    if (month   != 0) *month   = rtc_bcd2dec(d.RTC_Month);
    if (day     != 0) *day     = rtc_bcd2dec(d.RTC_Date);
    if (weekday != 0) *weekday = d.RTC_WeekDay;             /* 1=周一 … 7=周日 */
}


/* 区块 3:扩展功能 */

/* Zeller 公式(公历),输出 1=周一 … 7=周日;例:2026-09-28 → 1(周一) */
uint8_t SYS_RTC_WeekdayFromDate(uint16_t year, uint8_t month, uint8_t day)
{
    int32_t  c;
    int32_t  yy;
    int32_t  sum;
    uint32_t h;

    if (month < 3U) {           /* 1/2 月按上一年的 13/14 月算 */
        month = (uint8_t)(month + 12U);
        year--;
    }

    c  = (int32_t)year / 100;
    yy = (int32_t)year % 100;

    sum = (int32_t)day + (13 * ((int32_t)month + 1)) / 5 + yy + (yy / 4) + (c / 4) - (2 * c);
    h   = (uint32_t)(((sum % 7) + 7) % 7);          /* 0=周六 … 6=周五 */

    return (uint8_t)(((h + 5U) % 7U) + 1U);         /* → 1=周一 … 7=周日 */
}

/* 每日定点闹钟 */
void SYS_RTC_SetAlarmDaily(uint8_t h, uint8_t m, uint8_t s)
{
    rtc_alarm_it_enable();
    rtc_alarm_apply(RTC_AlarmMask_DateWeekDay, RTC_AlarmDateWeekDaySel_Date, 1U, h, m, s);
}

/* 按星期几触发的闹钟;weekday 1~7,越界钳位 */
void SYS_RTC_SetAlarmWeekday(uint8_t weekday, uint8_t h, uint8_t m, uint8_t s)
{
    if (weekday < 1U) weekday = 1U;
    if (weekday > 7U) weekday = 7U;

    rtc_alarm_it_enable();
    rtc_alarm_apply(RTC_AlarmMask_None, RTC_AlarmDateWeekDaySel_WeekDay, weekday, h, m, s);
}

/* 按每月日期触发的闹钟;date 1~31,越界钳位 */
void SYS_RTC_SetAlarmDate(uint8_t date, uint8_t h, uint8_t m, uint8_t s)
{
    if (date < 1U)  date = 1U;
    if (date > 31U) date = 31U;

    rtc_alarm_it_enable();
    rtc_alarm_apply(RTC_AlarmMask_None, RTC_AlarmDateWeekDaySel_Date, date, h, m, s);
}

/* 关闭闹钟 A */
void SYS_RTC_AlarmOff(void)
{
    RTC_AlarmCmd(RTC_Alarm_A, DISABLE);
}

/* 秒中断(1 秒一次):唤醒定时器 + EXTI 线 22 + NVIC */
uint8_t SYS_RTC_WakeUp1sOn(void)
{
    EXTI_InitTypeDef ei;
    NVIC_InitTypeDef ni;

    RTC_WakeUpCmd(DISABLE);                                     /* 改写计数器前先关 */
    RTC_WakeUpClockConfig(RTC_WakeUpClock_CK_SPRE_16bits);      /* 1Hz 基准 */
    RTC_SetWakeUpCounter(0);                                    /* 0 → 1 秒 */

    RTC_ClearITPendingBit(RTC_IT_WUT);
    EXTI_ClearITPendingBit(EXTI_Line22);
    RTC_ITConfig(RTC_IT_WUT, ENABLE);

    ei.EXTI_Line    = EXTI_Line22;              /* RTC 秒中断的固定 EXTI 线 */
    ei.EXTI_Mode    = EXTI_Mode_Interrupt;
    ei.EXTI_Trigger = EXTI_Trigger_Rising;
    ei.EXTI_LineCmd = ENABLE;
    EXTI_Init(&ei);

    ni.NVIC_IRQChannel                   = RTC_WKUP_IRQn;
    ni.NVIC_IRQChannelPreemptionPriority = SYS_RTC_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_RTC_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);

    RTC_WakeUpCmd(ENABLE);
    return SYS_RTC_OK;
}

/* 关闭秒中断 */
void SYS_RTC_WakeUpOff(void)
{
    RTC_WakeUpCmd(DISABLE);
}

/* 回调注册 */
void SYS_RTC_SetAlarmCallback (void (*callback)(void)) { rtc_alarm_cb = callback; }
void SYS_RTC_SetWakeUpCallback(void (*callback)(void)) { rtc_wkup_cb  = callback; }

/* 备份寄存器:用户编号 0~18 → 物理 DR1~DR19(不映射 DR0)
 *  SPL 的 RTC_Init() 用 RTC_BKP_DR0 存首次上电魔数 0x32F2,写 DR0 会覆盖它,
 *  使下次冷启动的首次上电判断出错;用户可见说明见同名 .h */

/* 用户编号 → 物理 DR 映射表(读/写共用) */
static const uint32_t rtc_bkp_reg[SYS_RTC_BKP_COUNT] = {
    RTC_BKP_DR1,  RTC_BKP_DR2,  RTC_BKP_DR3,  RTC_BKP_DR4,
    RTC_BKP_DR5,  RTC_BKP_DR6,  RTC_BKP_DR7,  RTC_BKP_DR8,  RTC_BKP_DR9,
    RTC_BKP_DR10, RTC_BKP_DR11, RTC_BKP_DR12, RTC_BKP_DR13, RTC_BKP_DR14,
    RTC_BKP_DR15, RTC_BKP_DR16, RTC_BKP_DR17, RTC_BKP_DR18, RTC_BKP_DR19,
};

void SYS_RTC_BackupWrite(uint8_t n, uint16_t value)
{
    if (n >= SYS_RTC_BKP_COUNT) return;
    RTC_WriteBackupRegister(rtc_bkp_reg[n], value);
}

uint16_t SYS_RTC_BackupRead(uint8_t n)
{
    if (n >= SYS_RTC_BKP_COUNT) return 0U;
    return (uint16_t)RTC_ReadBackupRegister(rtc_bkp_reg[n]);
}

/* 中断服务(__weak,转回调);与本库其它模块同约定:手写同名 ISR 时
 *  本弱定义让位,回调机制随之停用。 */
__weak void RTC_WKUP_IRQHandler(void)
{
    if (RTC_GetITStatus(RTC_IT_WUT) != RESET) {
        RTC_ClearITPendingBit(RTC_IT_WUT);
        EXTI_ClearITPendingBit(EXTI_Line22);
        if (rtc_wkup_cb != 0) rtc_wkup_cb();
    }
}

__weak void RTC_Alarm_IRQHandler(void)
{
    if (RTC_GetITStatus(RTC_IT_ALRA) != RESET) {
        RTC_ClearITPendingBit(RTC_IT_ALRA);
        EXTI_ClearITPendingBit(EXTI_Line17);
        if (rtc_alarm_cb != 0) rtc_alarm_cb();
    }
}
