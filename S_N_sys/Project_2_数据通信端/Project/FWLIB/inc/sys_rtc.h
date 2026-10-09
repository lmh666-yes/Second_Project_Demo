#ifndef __FWLIB_SYS_RTC_H
#define __FWLIB_SYS_RTC_H

#include "stm32f4xx.h"
/* RTC 组件未随 RTE 勾选时 stm32f4xx_conf.h 不会带进来——这里显式包含
 * （header 自带包含守卫,后续若在 RTE 勾选 RTC 也不会重复包含） */
#include "stm32f4xx_rtc.h"

/* ================================================================
 *  sys_rtc.h —— 【系统】RTC 实时时钟（带备份域）  头文件
 * ================================================================
 *  设计定位 : 系统服务（薄封装）—— 把"上电就跑的秒计数器"包装成
 *             能直接读写的日期时间，并管理备份域（掉电不丢的小数据）
 *  依赖     : StdPeriph 的 RTC + PWR + RCC（**工程 RTE 里必须勾上
 *             StdPeriph Drivers → RTC**，否则 stm32f4xx_rtc.c 不会被编译）
 *  标准库关键词 : RCC_LSEConfig / RCC_RTCCLKConfig / RCC_RTCCLKCmd /
 *                 PWR_BackupAccessCmd / RCC_BackupResetCmd /
 *                 RTC_Init / RTC_SetTime / RTC_GetTime / RTC_SetDate /
 *                 RTC_GetDate / RTC_WriteBackupRegister / RTC_ReadBackupRegister /
 *                 RTC_WakeUpCmd / RTC_SetWakeUpCounter / RTC_ITConfig
 *
 *  【RTC 和普通定时器有什么不一样（面试点）】
 *    ① 它跑在**备份域**：主电源断了还能靠纽扣电池(VBAT)继续走时；
 *    ② 备份域一旦被复位，"年月日时分秒"就全丢 —— 所以要用一个**备份寄存器
 *       写"魔数"**来判断是"又上电了"还是"时间还在"（本模块自动做）；
 *    ③ 它自带两个闹钟(A/B)、一个周期唤醒(WakeUp)和**32 位秒计数器**，
 *       秒计数器非常方便：做"日志时间戳/定时任务间隔"直接加减就行。
 *
 *  【接线（普中-天马 F407开发板）】
 *      Y2 = 32.768kHz 晶振 → PC14(OSC32_IN) / PC15(OSC32_OUT)，板上已焊好
 *      VBAT 引脚 —— 装了纽扣电池才能"断电继续走时"
 *      ⚠ PC13 是 RTC 的 TAMP/TAMP2 复用脚，本板被**触摸屏的 T_CS 占用**，
 *        所以本模块不启用 Tamper（防篡改），只做时钟
 *
 *  【使用方式（判断首次上电 + 读时间）】
 *      uint8_t fresh = SYS_RTC_Init();      // ① 起时钟（返回 1 = 时间被重置）
 *      if (fresh == 1) {
 *          SysRtc_t t = {2026, 9, 19, 10, 30, 0, 0};
 *          SYS_RTC_SetTime(&t);             // ② 首次上电才需要设时间
 *      }
 *      while (1) {
 *          SysRtc_t now;
 *          SYS_RTC_GetTime(&now);           // ③ 随时读（无需等待）
 *          delay_ms(1000);
 *      }
 *
 *  【常见坑】
 *    ① 不加纽扣电池：断电后时间**一定丢**，属于硬件限制，不是代码问题；
 *    ② 写入日期必须"读回一次"（F4 RTC 影子寄存器要求，本模块已处理）；
 *    ③ 备份域复位（RCC_BackupResetCmd）必须在 PWR_BackupAccessCmd(ENABLE)
 *       之后，否则复位命令被硬件静默忽略——表现为"怎么都改不动时间"；
 *    ④ 改完时间后要重新读一次日期，否则星期/日期可能过一拍才更新。
 *
 *  移植指引 : 无晶振的板子会自动回退到 LSI（见 SYS_RTC_LSI_FALLBACK）；
 *             换晶振频率只需改 SYS_RTC_LSE_HZ（默认 32768）。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
#define SYS_RTC_LSE_HZ          32768UL     /* 外部低速晶振频率（板上 Y2） */
#define SYS_RTC_LSE_TIMEOUT_MS  3000UL      /* 等 LSE 起振的时间；超时就回退 LSI。
                                             * 32768Hz 晶振起振可能到 2s 以上，
                                             * 原来给 1000ms 会误判为失败 */

/* 无纽扣晶振（或 LSE 焊坏）时是否自动用 LSI 兜底：1 = 是，0 = 直接报错
 * 注意 : LSI 精度差（±50%），只能保证"表在走"，不能当标准时间用 */
#define SYS_RTC_LSI_FALLBACK    1

/* 闹钟/秒中断的 NVIC 优先级（配合库默认 NVIC_PriorityGroup_2） */
#define SYS_RTC_IRQ_PRE_PRIO   2
#define SYS_RTC_IRQ_SUB_PRIO   0

/* 判断"备份域是否还有效"用的魔数（写在备份寄存器里，掉电不丢） */
#define SYS_RTC_MAGIC_REG       RTC_BKP_DR0
#define SYS_RTC_MAGIC_VALUE     0x32F2U

/* 首次上电的默认时间（2026-01-01 00:00:00 的 Unix 秒）
 * 好处：万一没设置时间，读出来也是个"合理的过去时间"，不会出现 1970 或乱码 */
#define SYS_RTC_DEFAULT_UNIX    1767225600UL

/* 备份寄存器编号范围（F4 有 DR0~DR19，共 20 个 32 位） */
#define SYS_RTC_BKP_REG_MAX     19

/* 编译期自检：默认时间不能早于 2000 年（否则换算必错） */
#if (SYS_RTC_DEFAULT_UNIX < 946684800UL)
#error "SYS_RTC_DEFAULT_UNIX is earlier than 2000-01-01"
#endif


/* ================================================================
 *                    区块 2：日期时间类型与基础功能
 * ================================================================ */
/* 统一的时间结构（比标准库的 BCD 结构体好用：字段就是人看着的十进制） */
typedef struct {
    uint16_t year;      /* 完整年份，2000 ~ 2099 */
    uint8_t  month;     /* 1 ~ 12 */
    uint8_t  day;       /* 1 ~ 31 */
    uint8_t  hour;      /* 0 ~ 23 */
    uint8_t  minute;    /* 0 ~ 59 */
    uint8_t  second;    /* 0 ~ 59 */
    uint8_t  weekday;   /* 1 = 周一 ... 7 = 周日（不用自己填，写时间时自动算） */
} SysRtc_t;

/* 初始化 RTC（含：开 PWR 时钟 → 开备份域 → 起 LSE → 配分频 → 开中断源）
 * 返回 : 0 = 备份域有效（时间还在，直接用）
 *        1 = 首次上电 / 断电过（时间已设为 SYS_RTC_DEFAULT_UNIX，请自行 SetTime）
 *        2 = 失败（LSE 和 LSI 都没起来）
 * 说明 : 幂等，可重复调用；内部最长阻塞约 SYS_RTC_LSE_TIMEOUT_MS 毫秒
 * 示例 : if (SYS_RTC_Init() == 1) { 说明这是首次上电,需要设置时间 }
 */
uint8_t SYS_RTC_Init(void);

/* 读当前日期时间（字段已经是十进制，直接显示/打印） */
void SYS_RTC_GetTime(SysRtc_t *out);

/* 设置日期时间（weekday 字段可填 0，函数会自动算）
 * 返回 : 0 = 成功；1 = 参数非法（空指针 / 字段越界 / 日期不存在） */
uint8_t SYS_RTC_SetTime(const SysRtc_t *in);

/* 只改时间（不动日期）—— 校时时最常用 */
uint8_t SYS_RTC_SetOnlyTime(uint8_t hour, uint8_t minute, uint8_t second);

/* 读/写 32 位秒计数器（RTC 的"心跳"，做时间戳最方便）
 * 示例 : uint32_t t0 = SYS_RTC_GetCounter();  …  SYS_RTC_GetCounter() - t0 = 经过秒数 */
uint32_t SYS_RTC_GetCounter(void);
void     SYS_RTC_SetCounter(uint32_t seconds);

/* Unix 时间戳换算（1970-01-01 00:00:00 起算的秒数）
 * 用途 : 存日志、和上位机对时间、算间隔
 * 示例 : uint32_t u = SYS_RTC_GetUnix();
 *        SysRtc_t t; SYS_RTC_FromUnix(u, &t); */
uint32_t SYS_RTC_GetUnix(void);
void     SYS_RTC_SetUnix(uint32_t unix_sec);
uint32_t SYS_RTC_ToUnix(const SysRtc_t *t);
void     SYS_RTC_FromUnix(uint32_t unix_sec, SysRtc_t *out);

/* 算星期：1 = 周一 ... 7 = 周日（蔡勒/基姆拉尔森思路的"儒略日"算法）
 * 示例 : uint8_t wd = SYS_RTC_WeekdayOf(2026, 9, 19);   // 返回 6 = 周六 */
uint8_t SYS_RTC_WeekdayOf(uint16_t year, uint8_t month, uint8_t day);

/* 判断是不是闰年（自己做日历/天数统计时用） */
uint8_t SYS_RTC_IsLeapYear(uint16_t year);

/* 某月有多少天（做日历界面时用） */
uint8_t SYS_RTC_DaysInMonth(uint16_t year, uint8_t month);


/* ================================================================
 *                    区块 3：备份域数据 + 唤醒/闹钟
 * ================================================================ */

/* 备份寄存器读写（**主电源断开也不丢**，前提是 VBAT 有电）
 * 参数 : reg —— 0 ~ 19（SYS_RTC_BKP_REG_MAX）
 * 用途 : 存"设备运行次数/校准标记/上次状态"，比写 EEPROM 快得多
 * 注意 : DR0 已被本模块用作"时间有效性标记"，自己用请从 DR1 开始
 * 示例 : SYS_RTC_WriteBkp(1, 12345);  uint32_t n = SYS_RTC_ReadBkp(1); */
void     SYS_RTC_WriteBkp(uint8_t reg, uint32_t value);
uint32_t SYS_RTC_ReadBkp(uint8_t reg);

/* 备份域是否有效（时间没丢）—— 返回 1 = 有效
 * 说明 : SYS_RTC_Init 内部就是用这个判断的，单独暴露给"想自己决定
 *        是否重设时间"的场合 */
uint8_t SYS_RTC_IsBackupValid(void);

/* 备份域复位（**会清掉备份寄存器 + 时间**，只在"恢复出厂设置"时用）
 * 返回 : 0 = 已复位（调用后应重新 SYS_RTC_Init + SetTime） */
uint8_t SYS_RTC_ResetBackup(void);

/* 周期唤醒中断：每 period_s 秒进一次回调（1 ~ 65535 秒）
 * 参数 : period_s —— 周期秒数；callback —— 回调（中断上下文，保持短小）
 * 返回 : 0 = 成功；1 = 参数非法
 * 用途 : ★ 低功耗采集的"心跳"——Stop 模式下 RTC 唤醒是最省电的定时唤醒方式
 * 示例 : SYS_RTC_WakeUpInit(1, OnSecondTick);      // 每秒回调一次
 *        SYS_RTC_WakeUpInit(600, OnReport);        // 10 分钟上报一次 */
uint8_t SYS_RTC_WakeUpInit(uint16_t period_s, void (*callback)(void));

/* 停止周期唤醒 */
void SYS_RTC_WakeUpStop(void);

/* 查看唤醒中断被触发的次数（不写回调时用轮询方式也能跑）
 * 返回 : 自 SYS_RTC_WakeUpInit 起的累计次数
 * 示例 : if (SYS_RTC_WakeUpCount() != last) { last = ...; 干活; } */
uint32_t SYS_RTC_WakeUpCount(void);

/* 闹钟 A：到指定"时:分:秒"触发一次回调（每天同一时刻都会触发）
 * 参数 : hour/minute/second —— 目标时刻（0~23 / 0~59 / 0~59）
 *        mask 位组合: SYS_RTC_ALARM_MASK_HOUR / _MIN / _SEC
 *        掩码位为 0 表示"该字段不参与比较"→ 可做出"每分钟/每小时"闹钟
 * 返回 : 0 = 成功；1 = 参数非法
 * 示例 : SYS_RTC_AlarmSet(6, 30, 0, SYS_RTC_ALARM_MASK_ALL, OnAlarm);  // 每天 6:30
 *        SYS_RTC_AlarmSet(0, 0, 0, SYS_RTC_ALARM_MASK_SEC, OnAlarm);   // 每分钟
 */
#define SYS_RTC_ALARM_MASK_SEC   0x01U
#define SYS_RTC_ALARM_MASK_MIN   0x02U
#define SYS_RTC_ALARM_MASK_HOUR  0x04U
#define SYS_RTC_ALARM_MASK_ALL   (SYS_RTC_ALARM_MASK_SEC | SYS_RTC_ALARM_MASK_MIN | \
                                  SYS_RTC_ALARM_MASK_HOUR)

uint8_t SYS_RTC_AlarmSet(uint8_t hour, uint8_t minute, uint8_t second,
                         uint8_t mask, void (*callback)(void));

/* 关闭闹钟 A */
void SYS_RTC_AlarmStop(void);

/* 格式化成字符串（自己传缓冲，避免用 snprintf 吃 Flash）
 * 参数 : buf —— 至少 **24 字节**；t —— 时间；with_week —— 是否带"周X"
 * 输出 : "2026-09-19 10:30:00"（19 字符）或 "2026-09-19 10:30:00 Sat"（23 字符）
 * 返回 : buf（方便 printf("%s", ...) 直接套）
 * 说明 : 星期缩写成 ASCII（Mon~Sun）——库内不出现中文，避免编译器编码问题 */
char *SYS_RTC_Format(char *buf, const SysRtc_t *t, uint8_t with_week);

#endif /* __FWLIB_SYS_RTC_H */
