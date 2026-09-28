#include "sys_rtc.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"      /* Delay_ms / DWT_GetUs / DWT_ElapsedUs */
#include "sys_nvic.h"       /* SYS_NVIC_SetPriority / EnableIRQ */

/* ================================================================
 *  sys_rtc.c —— RTC 实时时钟  实现文件
 * ================================================================
 *  上电流程（为什么必须这么绕）：
 *
 *      开 PWR 时钟 → 打开备份域写权限(PWR_BackupAccessCmd)
 *            ↓
 *      起 LSE 晶振 → 等 LSERDY → 选 LSE 做 RTC 时钟
 *            ↓
 *      读备份寄存器里的"魔数"
 *        ├── 魔数对 = 备份域没被复位 → 时间还在，直接用 ✔
 *        └── 魔数不对 = 断电/首次上电 → 复位备份域 → 重配 RTC → 写默认时间
 *
 *  ⚠ 为什么要用"魔数"：备份域一旦掉电（没装纽扣电池时就等于每次断电），
 *     RTC 寄存器全部回到 0x000000——读出来是 1970 年或者乱值。
 *     用一个掉电不丢的备份寄存器当"书签"，就能区分"真时间"和"垃圾值"。
 *
 *  ⚠ 备份域复位(RCC_BackupResetCmd)必须在 PWR_BackupAccessCmd(ENABLE) 之后，
 *     否则复位命令会被硬件静默忽略（这是最常见的"怎么都改不动时间"原因）。
 * ================================================================ */


/* ================================================================
 *                      模块内部状态
 * ================================================================ */
static void     (*rtc_wakeup_cb)(void) = 0;
static void     (*rtc_alarm_cb)(void)  = 0;
static volatile uint32_t rtc_wakeup_cnt = 0;


/* ================================================================
 *                      内部小工具
 * ================================================================ */

/* 等某个 RCC 标志置位，超时返回 0 */
static uint8_t rtc_wait_flag(uint8_t flag, uint32_t timeout_ms)
{
    uint32_t t0 = DWT_GetUs();

    while (RCC_GetFlagStatus(flag) == RESET) {
        if (DWT_ElapsedUs(t0) > (timeout_ms * 1000UL)) return 0U;
    }
    return 1U;
}

/* 给 RTC 配好时钟源与预分频（只在"首次上电"调用）
 * LSE 32768Hz : 128 × 256 = 32768 → 1Hz
 * LSI ~32000Hz: 128 × 250 = 32000 → 1Hz（约数，精度差） */
static uint8_t rtc_config(uint32_t rtc_clk_src)
{
    RTC_InitTypeDef ri;

    /* ⚠★ 这里**绝对不能**再 RCC_BackupResetCmd()！
     *    复位备份域会把 BDCR 整个清 0 —— 包括刚刚配好的
     *    LSEON + RTCSEL + RTCEN，RTC 就彻底没时钟了，
     *    RTC_Init 必然失败（症状：SYS_RTC_Init 返回 2）。
     *    备份域复位已挪到 SYS_RTC_Init() 里、配 LSE 之前执行。 */

    PWR_BackupAccessCmd(ENABLE);        /* 写 BDCR 前先解锁 DBP */
    RCC_RTCCLKCmd(ENABLE);
    (void)RTC_WaitForSynchro();

    ri.RTC_HourFormat   = RTC_HourFormat_24;
    ri.RTC_AsynchPrediv = 127U;         /* 异步分频 128 */
    ri.RTC_SynchPrediv  = (rtc_clk_src == RCC_RTCCLKSource_LSE) ? 255U : 249U;

    if (RTC_Init(&ri) != SUCCESS) return 1U;

    return 0U;
}

/* --- Unix 秒 ↔ 年月日（Howard Hinnant 的算法，无查表、无循环） --- */
static int32_t rtc_days_from_civil(int32_t y, int32_t m, int32_t d)
{
    int32_t  era;
    uint32_t yoe;
    uint32_t doy;
    uint32_t doe;

    y -= (m <= 2) ? 1 : 0;
    era = (y >= 0 ? y : (y - 399)) / 400;
    yoe = (uint32_t)(y - era * 400);                          /* [0, 399] */
    doy = (uint32_t)((153 * (m + ((m > 2) ? -3 : 9)) + 2) / 5 + d - 1);
    doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;           /* [0, 146096] */

    return era * 146097 + (int32_t)doe - 719468;
}

static void rtc_civil_from_days(int32_t z, int32_t *y_out, int32_t *m_out, int32_t *d_out)
{
    int32_t  era;
    uint32_t doe;
    uint32_t yoe;
    uint32_t doy;
    uint32_t mp;
    int32_t  y;
    int32_t  m;

    z += 719468;
    era = (z >= 0 ? z : (z - 146096)) / 146097;
    doe = (uint32_t)(z - era * 146097);                       /* [0, 146096] */
    yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    y   = (int32_t)yoe + era * 400;
    doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);         /* [0, 365] */
    mp  = (5U * doy + 2U) / 153U;                             /* [0, 11] */
    y  += (doy >= 306U) ? 1 : 0;                              /* 1/3 之后进下一年 */
    m   = (int32_t)mp + ((mp < 10U) ? 3 : -9);                /* [1, 12] */

    if (y_out != 0) *y_out = y;
    if (m_out != 0) *m_out = m;
    if (d_out != 0) *d_out = (int32_t)(doy - (153U * mp + 2U) / 5U + 1U);
}

static void rtc_put2(char *p, uint8_t v)
{
    p[0] = (char)('0' + ((v / 10U) % 10U));
    p[1] = (char)('0' + (v % 10U));
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
uint8_t SYS_RTC_Init(void)
{
    uint32_t src;
    uint8_t  lse_ok;

    /* ① 开 PWR 时钟 + 解锁备份域（顺序不能颠倒） */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    /* ② 备份域有效 → 时间还在，只补开唤醒中断线即可 */
    if (SYS_RTC_IsBackupValid()) {
        RCC_RTCCLKCmd(ENABLE);
        (void)RTC_WaitForSynchro();
        return 0U;
    }

    /* ③ 备份域无效 → 先整体复位一次，让 BDCR 回到干净状态。
     *    ★★ 必须在"配 LSE / 选 RTC 时钟源"之前做！
     *    （原来这句在 rtc_config() 里，等于把刚配好的 LSEON+RTCSEL
     *      又全清一遍 —— RTC 彻底没时钟，Init 一定失败）
     *    RCC_BackupResetCmd 自身会开关 CR_DBP，退出时可能是 0，
     *    所以后面每次写 BDCR 前都补一次解锁。 */
    PWR_BackupAccessCmd(ENABLE);
    RCC_BackupResetCmd(ENABLE);
    RCC_BackupResetCmd(DISABLE);
    PWR_BackupAccessCmd(ENABLE);

    /* ④ 起 LSE
     * ⚠★ RCC_LSEConfig() 内部会动 CR_DBP 位（备份域解锁位），
     *    退出时 DBP 可能已经是 0。DBP=0 时写 BDCR 完全无效，
     *    后面 RCC_RTCCLKConfig() 会静默失效 → RTC 拿不到时钟。
     *    所以每次碰完 LSE/LSI 配置都必须重新解锁一遍。 */
    RCC_LSEConfig(RCC_LSE_ON);
    PWR_BackupAccessCmd(ENABLE);                  /* ★ 补回解锁 */
    lse_ok = rtc_wait_flag(RCC_FLAG_LSERDY, SYS_RTC_LSE_TIMEOUT_MS);
    src    = RCC_RTCCLKSource_LSE;

    if (!lse_ok) {
#if (SYS_RTC_LSI_FALLBACK)
        RCC_LSICmd(ENABLE);
        PWR_BackupAccessCmd(ENABLE);              /* ★ 同样补回 */
        if (!rtc_wait_flag(RCC_FLAG_LSIRDY, SYS_RTC_LSE_TIMEOUT_MS)) return 2U;
        src = RCC_RTCCLKSource_LSI;
#else
        return 2U;
#endif
    }

    /* ⑤ 选时钟源并配置分频 */
    PWR_BackupAccessCmd(ENABLE);                  /* ★ 写 BDCR 前再保险一次 */
    RCC_RTCCLKConfig(src);
    if (rtc_config(src) != 0U) return 2U;

    /* ⑥ 写默认时间 + 打上"魔数"（下次上电就知道备份域是好的） */
    SYS_RTC_SetCounter(SYS_RTC_DEFAULT_UNIX);
    RTC_WriteBackupRegister(SYS_RTC_MAGIC_REG, SYS_RTC_MAGIC_VALUE);

    return 1U;
}

void SYS_RTC_GetTime(SysRtc_t *out)
{
    RTC_TimeTypeDef ts;
    RTC_DateTypeDef ds;

    if (out == 0) return;

    (void)RTC_WaitForSynchro();
    RTC_GetTime(RTC_Format_BIN, &ts);
    RTC_GetDate(RTC_Format_BIN, &ds);

    out->year    = (uint16_t)(2000U + (uint16_t)ds.RTC_Year);
    out->month   = ds.RTC_Month;
    out->day     = ds.RTC_Date;
    out->hour    = ts.RTC_Hours;
    out->minute  = ts.RTC_Minutes;
    out->second  = ts.RTC_Seconds;
    out->weekday = (ds.RTC_WeekDay >= 1U && ds.RTC_WeekDay <= 7U) ? ds.RTC_WeekDay : 0U;
}

uint8_t SYS_RTC_SetTime(const SysRtc_t *in)
{
    RTC_TimeTypeDef ts;
    RTC_DateTypeDef ds;

    if (in == 0) return 1U;
    if (in->year < 2000U || in->year > 2099U) return 1U;
    if (in->month < 1U || in->month > 12U) return 1U;
    if (in->day < 1U || in->day > SYS_RTC_DaysInMonth(in->year, in->month)) return 1U;
    if (in->hour > 23U || in->minute > 59U || in->second > 59U) return 1U;

    (void)RTC_WaitForSynchro();

    ts.RTC_H12     = RTC_H12_AM;
    ts.RTC_Hours   = in->hour;
    ts.RTC_Minutes = in->minute;
    ts.RTC_Seconds = in->second;
    if (RTC_SetTime(RTC_Format_BIN, &ts) != SUCCESS) return 1U;

    ds.RTC_Year    = (uint8_t)(in->year - 2000U);
    ds.RTC_Month   = in->month;
    ds.RTC_Date    = in->day;
    ds.RTC_WeekDay = (in->weekday >= 1U && in->weekday <= 7U)
                     ? in->weekday
                     : SYS_RTC_WeekdayOf(in->year, in->month, in->day);
    if (RTC_SetDate(RTC_Format_BIN, &ds) != SUCCESS) return 1U;

    /* F4 的现象：写完日历要再读一次，影子寄存器才立刻反映新值 */
    RTC_GetTime(RTC_Format_BIN, &ts);
    RTC_GetDate(RTC_Format_BIN, &ds);

    return 0U;
}

uint8_t SYS_RTC_SetOnlyTime(uint8_t hour, uint8_t minute, uint8_t second)
{
    SysRtc_t t;

    if (hour > 23U || minute > 59U || second > 59U) return 1U;

    SYS_RTC_GetTime(&t);
    t.hour   = hour;
    t.minute = minute;
    t.second = second;

    return SYS_RTC_SetTime(&t);
}

uint32_t SYS_RTC_GetCounter(void)
{
    /* ⚠ F4 的 StdPeriph 没有 RTC_GetCounter（那是 F1 的 API），
     *   所以用"读日历 → 转 Unix 秒"来实现，效果一样 */
    SysRtc_t t;

    SYS_RTC_GetTime(&t);

    return SYS_RTC_ToUnix(&t);
}

void SYS_RTC_SetCounter(uint32_t seconds)
{
    SysRtc_t t;

    SYS_RTC_FromUnix(seconds, &t);
    (void)SYS_RTC_SetTime(&t);
}

uint32_t SYS_RTC_GetUnix(void)
{
    return SYS_RTC_GetCounter();
}

void SYS_RTC_SetUnix(uint32_t unix_sec)
{
    SYS_RTC_SetCounter(unix_sec);
}

uint32_t SYS_RTC_ToUnix(const SysRtc_t *t)
{
    int32_t days;

    if (t == 0) return 0UL;

    days = rtc_days_from_civil((int32_t)t->year, (int32_t)t->month, (int32_t)t->day);

    return (uint32_t)((int64_t)days * 86400LL +
                      (int64_t)t->hour * 3600LL +
                      (int64_t)t->minute * 60LL +
                      (int64_t)t->second);
}

void SYS_RTC_FromUnix(uint32_t unix_sec, SysRtc_t *out)
{
    int32_t  days;
    uint32_t rem;
    int32_t  y;
    int32_t  m;
    int32_t  d;

    if (out == 0) return;

    days = (int32_t)(unix_sec / 86400UL);
    rem  = unix_sec % 86400UL;

    rtc_civil_from_days(days, &y, &m, &d);

    out->year    = (uint16_t)y;
    out->month   = (uint8_t)m;
    out->day     = (uint8_t)d;
    out->hour    = (uint8_t)(rem / 3600UL);
    out->minute  = (uint8_t)((rem % 3600UL) / 60UL);
    out->second  = (uint8_t)(rem % 60UL);
    out->weekday = SYS_RTC_WeekdayOf((uint16_t)y, (uint8_t)m, (uint8_t)d);
}

uint8_t SYS_RTC_WeekdayOf(uint16_t year, uint8_t month, uint8_t day)
{
    int32_t days;
    int32_t wd;

    days = rtc_days_from_civil((int32_t)year, (int32_t)month, (int32_t)day);

    wd = (days + 3) % 7;                /* 1970-01-01 是周四 → +3 后 0 = 周一 */
    if (wd < 0) wd += 7;

    return (uint8_t)(wd + 1);
}

uint8_t SYS_RTC_IsLeapYear(uint16_t year)
{
    if ((year % 400U) == 0U) return 1U;
    if ((year % 100U) == 0U) return 0U;
    return ((year % 4U) == 0U) ? 1U : 0U;
}

uint8_t SYS_RTC_DaysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t dm[12] = {31U, 28U, 31U, 30U, 31U, 30U,
                                   31U, 31U, 30U, 31U, 30U, 31U};

    if (month < 1U || month > 12U) return 0U;
    if (month == 2U && SYS_RTC_IsLeapYear(year)) return 29U;

    return dm[month - 1U];
}


/* ================================================================
 *                    区块 3：备份域数据 + 唤醒/闹钟
 * ================================================================ */
void SYS_RTC_WriteBkp(uint8_t reg, uint32_t value)
{
    if (reg > SYS_RTC_BKP_REG_MAX) return;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    RTC_WriteBackupRegister((uint32_t)(RTC_BKP_DR0 + (uint32_t)reg), value);
}

uint32_t SYS_RTC_ReadBkp(uint8_t reg)
{
    if (reg > SYS_RTC_BKP_REG_MAX) return 0UL;

    return RTC_ReadBackupRegister((uint32_t)(RTC_BKP_DR0 + (uint32_t)reg));
}

uint8_t SYS_RTC_IsBackupValid(void)
{
    return (RTC_ReadBackupRegister(SYS_RTC_MAGIC_REG) == SYS_RTC_MAGIC_VALUE) ? 1U : 0U;
}

uint8_t SYS_RTC_ResetBackup(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    RCC_BackupResetCmd(ENABLE);
    RCC_BackupResetCmd(DISABLE);

    (void)RTC_WaitForSynchro();

    return 0U;
}

uint8_t SYS_RTC_WakeUpInit(uint16_t period_s, void (*callback)(void))
{
    EXTI_InitTypeDef ei;

    if (period_s == 0U) return 1U;

    rtc_wakeup_cb  = callback;
    rtc_wakeup_cnt = 0UL;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    RTC_WakeUpCmd(DISABLE);                                 /* 改配置前必须先关 */

    RTC_WakeUpClockConfig(RTC_WakeUpClock_CK_SPRE_16bits);  /* 1Hz = 1 秒一拍 */
    RTC_SetWakeUpCounter((uint32_t)period_s - 1UL);         /* WUT = (n+1) 秒 */

    RTC_ClearFlag(RTC_FLAG_WUTF);
    EXTI_ClearITPendingBit(EXTI_Line22);

    /* RTC 唤醒走 EXTI 线 22（不属于 EXTI0~15，所以不能走 sys_exti） */
    ei.EXTI_Line    = EXTI_Line22;
    ei.EXTI_Mode    = EXTI_Mode_Interrupt;
    ei.EXTI_Trigger = EXTI_Trigger_Rising;
    ei.EXTI_LineCmd = ENABLE;
    EXTI_Init(&ei);

    RTC_ITConfig(RTC_IT_WUT, ENABLE);
    SYS_NVIC_SetPriority(RTC_WKUP_IRQn, 2, 0);
    SYS_NVIC_EnableIRQ(RTC_WKUP_IRQn);

    RTC_WakeUpCmd(ENABLE);

    return 0U;
}

void SYS_RTC_WakeUpStop(void)
{
    RTC_WakeUpCmd(DISABLE);
    RTC_ITConfig(RTC_IT_WUT, DISABLE);
    RTC_ClearFlag(RTC_FLAG_WUTF);
    EXTI_ClearITPendingBit(EXTI_Line22);
    SYS_NVIC_DisableIRQ(RTC_WKUP_IRQn);
}

uint32_t SYS_RTC_WakeUpCount(void)
{
    return rtc_wakeup_cnt;
}

uint8_t SYS_RTC_AlarmSet(uint8_t hour, uint8_t minute, uint8_t second,
                         uint8_t mask, void (*callback)(void))
{
    RTC_AlarmTypeDef as;
    EXTI_InitTypeDef ei;

    if (hour > 23U || minute > 59U || second > 59U) return 1U;

    rtc_alarm_cb = callback;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    RTC_AlarmCmd(RTC_Alarm_A, DISABLE);

    RTC_AlarmStructInit(&as);
    as.RTC_AlarmTime.RTC_H12     = RTC_H12_AM;
    as.RTC_AlarmTime.RTC_Hours   = hour;
    as.RTC_AlarmTime.RTC_Minutes = minute;
    as.RTC_AlarmTime.RTC_Seconds = second;

    /* 掩码位为 0 → 该字段"不比较"（Mask 里对应位置 1） */
    as.RTC_AlarmMask = RTC_AlarmMask_DateWeekDay;   /* 日期不参与（做"每天"闹钟） */
    if ((mask & SYS_RTC_ALARM_MASK_SEC)  == 0U) as.RTC_AlarmMask |= RTC_AlarmMask_Seconds;
    if ((mask & SYS_RTC_ALARM_MASK_MIN)  == 0U) as.RTC_AlarmMask |= RTC_AlarmMask_Minutes;
    if ((mask & SYS_RTC_ALARM_MASK_HOUR) == 0U) as.RTC_AlarmMask |= RTC_AlarmMask_Hours;

    as.RTC_AlarmDateWeekDaySel = RTC_AlarmDateWeekDaySel_Date;
    as.RTC_AlarmDateWeekDay    = 1U;

    RTC_SetAlarm(RTC_Format_BIN, RTC_Alarm_A, &as);

    RTC_ClearFlag(RTC_FLAG_ALRAF);
    EXTI_ClearITPendingBit(EXTI_Line17);

    ei.EXTI_Line    = EXTI_Line17;
    ei.EXTI_Mode    = EXTI_Mode_Interrupt;
    ei.EXTI_Trigger = EXTI_Trigger_Rising;
    ei.EXTI_LineCmd = ENABLE;
    EXTI_Init(&ei);

    RTC_ITConfig(RTC_IT_ALRA, ENABLE);
    SYS_NVIC_SetPriority(RTC_Alarm_IRQn, 2, 0);
    SYS_NVIC_EnableIRQ(RTC_Alarm_IRQn);

    RTC_AlarmCmd(RTC_Alarm_A, ENABLE);

    return 0U;
}

void SYS_RTC_AlarmStop(void)
{
    RTC_AlarmCmd(RTC_Alarm_A, DISABLE);
    RTC_ITConfig(RTC_IT_ALRA, DISABLE);
    RTC_ClearFlag(RTC_FLAG_ALRAF);
    EXTI_ClearITPendingBit(EXTI_Line17);
    SYS_NVIC_DisableIRQ(RTC_Alarm_IRQn);
}

char *SYS_RTC_Format(char *buf, const SysRtc_t *t, uint8_t with_week)
{
    static const char *wd_name[8] = {"---", "Mon", "Tue", "Wed",
                                     "Thu", "Fri", "Sat", "Sun"};
    uint8_t wd;
    uint8_t i;

    if (buf == 0 || t == 0) return buf;

    rtc_put2(&buf[0], (uint8_t)(t->year / 100U));
    rtc_put2(&buf[2], (uint8_t)(t->year % 100U));
    buf[4] = '-'; rtc_put2(&buf[5], t->month);
    buf[7] = '-'; rtc_put2(&buf[8], t->day);
    buf[10] = ' ';
    rtc_put2(&buf[11], t->hour);
    buf[13] = ':'; rtc_put2(&buf[14], t->minute);
    buf[16] = ':'; rtc_put2(&buf[17], t->second);
    buf[19] = '\0';

    if (with_week) {
        wd = (t->weekday >= 1U && t->weekday <= 7U) ? t->weekday : 0U;
        buf[19] = ' ';
        for (i = 0; i < 3U; i++) buf[20U + i] = wd_name[wd][i];
        buf[23] = '\0';
    }

    return buf;
}


/* ================================================================
 *                    中断服务（库里唯一的 RTC 向量）
 * ================================================================ */
/* 周期唤醒：IRQ 3（向量名与启动文件逐一核对过）
 * 典型用途：Stop 模式下的定时唤醒采样 */
void RTC_WKUP_IRQHandler(void)
{
    if (RTC_GetITStatus(RTC_IT_WUT) != RESET) {
        RTC_ClearITPendingBit(RTC_IT_WUT);
        EXTI_ClearITPendingBit(EXTI_Line22);

        rtc_wakeup_cnt++;

        if (rtc_wakeup_cb != 0) rtc_wakeup_cb();
    }
}

/* 闹钟 A/B：IRQ 41（共用同一向量） */
void RTC_Alarm_IRQHandler(void)
{
    if (RTC_GetITStatus(RTC_IT_ALRA) != RESET) {
        RTC_ClearITPendingBit(RTC_IT_ALRA);
        EXTI_ClearITPendingBit(EXTI_Line17);

        if (rtc_alarm_cb != 0) rtc_alarm_cb();
    }

    if (RTC_GetITStatus(RTC_IT_ALRB) != RESET) {
        RTC_ClearITPendingBit(RTC_IT_ALRB);
        EXTI_ClearITPendingBit(EXTI_Line17);
    }
}
