#ifndef __FWLIB_SYS_RTC_H
#define __FWLIB_SYS_RTC_H

#include "stm32f4xx.h"
/* RTC 组件未随 RTE 勾选时 stm32f4xx_conf.h 不会带进来，这里显式包含
 * header 自带包含守卫，后续在 RTE 勾选 RTC 也不会重复包含 */
#include "stm32f4xx_rtc.h"

/* sys_rtc.h：RTC 实时时钟驱动头文件，含备份域管理
 * 接线：Y2 = 32.768kHz 晶振，经 PC14(OSC32_IN) / PC15(OSC32_OUT) 接入；VBAT 接纽扣电池才能断电继续走时
 * PC13 是 RTC 的 TAMP/TAMP2 复用脚，本板被触摸屏 T_CS 占用，故不启用 Tamper
 * 工程 RTE 必须勾选 StdPeriph Drivers → RTC，否则 stm32f4xx_rtc.c 不参与编译 */


/* 换板子只改本区的宏 */
#define SYS_RTC_LSE_HZ          32768UL     /* 外部低速晶振频率（板上 Y2） */
#define SYS_RTC_LSE_TIMEOUT_MS  3000UL      /* 等 LSE 起振的时间，超时回退 LSI
                                             * 32768Hz 晶振起振可能到 2s 以上，
                                             * 1000ms 会把起振中的晶振误判为失败 */

/* 无纽扣晶振或 LSE 焊坏时是否自动用 LSI 兜底：1 = 是，0 = 直接报错
 * LSI 精度差（±50%），只保证表在走，不能当标准时间用 */
#define SYS_RTC_LSI_FALLBACK    1

/* 中断优先级：数值越小优先级越高，可用范围随 sys_nvic 的分组变化
 * NVIC_PriorityGroup_4（本库默认）：抢占 0~15、子固定 0
 * NVIC_PriorityGroup_2（纯裸机）：抢占 0~3、子 0~3
 * 用 FreeRTOS 时抢占必须 ≥5（默认已给 5）
 * FreeRTOS 用 BASEPRI = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY<<4
 * （本工程 = 5<<4 = 0x50）屏蔽允许调 FromISR 的中断
 * 抢占数值 < 5 的中断能打断内核临界区，其中禁止调用任何 ...FromISR() 接口
 * 想更急只能取 5，想更缓取 6~15 皆可 */
#define SYS_RTC_IRQ_PRE_PRIO   5
#define SYS_RTC_IRQ_SUB_PRIO   0

/* 判断备份域是否有效的魔数，写在备份寄存器里，掉电不丢 */
#define SYS_RTC_MAGIC_REG       RTC_BKP_DR0
#define SYS_RTC_MAGIC_VALUE     0x32F2U

/* 首次上电的默认时间：2026-01-01 00:00:00 的 Unix 秒
 * 万一没设置时间，读出来也是合理的过去时间，不会是 1970 或乱码 */
#define SYS_RTC_DEFAULT_UNIX    1767225600UL

/* 备份寄存器编号范围：F4 有 DR0~DR19，共 20 个 32 位 */
#define SYS_RTC_BKP_REG_MAX     19

/* 编译期检查：默认时间不能早于 2000 年，否则换算必错 */
#if (SYS_RTC_DEFAULT_UNIX < 946684800UL)
#error "SYS_RTC_DEFAULT_UNIX is earlier than 2000-01-01"
#endif


/* 统一的时间结构，字段为十进制 */
typedef struct {
    uint16_t year;      /* 完整年份，2000 ~ 2099 */
    uint8_t  month;     /* 1 ~ 12 */
    uint8_t  day;       /* 1 ~ 31 */
    uint8_t  hour;      /* 0 ~ 23 */
    uint8_t  minute;    /* 0 ~ 59 */
    uint8_t  second;    /* 0 ~ 59 */
    uint8_t  weekday;   /* 1 = 周一 ... 7 = 周日（不用自己填，写时间时自动算） */
} SysRtc_t;

/* 初始化 RTC：开 PWR 时钟、开备份域、起 LSE、配分频、开中断源
 * 返回 : 0 = 备份域有效，时间还在，可直接用
 *        1 = 首次上电或断电过，时间已设为 SYS_RTC_DEFAULT_UNIX，需自行 SetTime
 *        2 = 失败，LSE 和 LSI 都没起来
 * 说明 : 幂等，可重复调用；内部最长阻塞约 SYS_RTC_LSE_TIMEOUT_MS 毫秒 */
uint8_t SYS_RTC_Init(void);

/* 读当前日期时间，字段已是十进制，可直接显示或打印 */
void SYS_RTC_GetTime(SysRtc_t *out);

/* 设置日期时间，weekday 字段可填 0，函数自动算
 * 返回 : 0 = 成功；1 = 参数非法（空指针 / 字段越界 / 日期不存在） */
uint8_t SYS_RTC_SetTime(const SysRtc_t *in);

/* 只改时间，不动日期 */
uint8_t SYS_RTC_SetOnlyTime(uint8_t hour, uint8_t minute, uint8_t second);

/* 读/写 32 位秒计数器，RTC 的心跳，做时间戳可用 */
uint32_t SYS_RTC_GetCounter(void);
void     SYS_RTC_SetCounter(uint32_t seconds);

/* Unix 时间戳换算：1970-01-01 00:00:00 起算的秒数
 * 用于存日志、和上位机对时间、算间隔 */
uint32_t SYS_RTC_GetUnix(void);
void     SYS_RTC_SetUnix(uint32_t unix_sec);
uint32_t SYS_RTC_ToUnix(const SysRtc_t *t);
void     SYS_RTC_FromUnix(uint32_t unix_sec, SysRtc_t *out);

/* 算星期：1 = 周一 ... 7 = 周日，用儒略日算法 */
uint8_t SYS_RTC_WeekdayOf(uint16_t year, uint8_t month, uint8_t day);

/* 判断是否闰年 */
uint8_t SYS_RTC_IsLeapYear(uint16_t year);

/* 某月有多少天 */
uint8_t SYS_RTC_DaysInMonth(uint16_t year, uint8_t month);


/* 备份域数据与唤醒/闹钟 */

/* 备份寄存器读写：主电源断开也不丢，前提是 VBAT 有电
 * 参数 : reg 取 0 ~ 19（SYS_RTC_BKP_REG_MAX）
 * 注意 : DR0 已被本模块用作时间有效性标记，自己用请从 DR1 开始 */
void     SYS_RTC_WriteBkp(uint8_t reg, uint32_t value);
uint32_t SYS_RTC_ReadBkp(uint8_t reg);

/* 备份域是否有效，即时间没丢，返回 1 = 有效
 * 说明 : SYS_RTC_Init 内部用本函数判断；单独暴露给想自己决定是否重设时间的场合 */
uint8_t SYS_RTC_IsBackupValid(void);

/* 备份域复位，会清掉备份寄存器和时间，只在恢复出厂设置时用
 * 返回 : 0 = 已复位；调用后应重新 SYS_RTC_Init + SetTime */
uint8_t SYS_RTC_ResetBackup(void);

/* 周期唤醒中断：每 period_s 秒进一次回调（1 ~ 65535 秒）
 * 参数 : period_s 为周期秒数；callback 为回调，运行在中断上下文，保持短小
 * 返回 : 0 = 成功；1 = 参数非法
 * 说明 : Stop 模式下用 RTC 唤醒是较省电的定时唤醒方式 */
uint8_t SYS_RTC_WakeUpInit(uint16_t period_s, void (*callback)(void));

/* 停止周期唤醒 */
void SYS_RTC_WakeUpStop(void);

/* 查看唤醒中断累计触发次数，不写回调时可用轮询方式
 * 返回 : 自 SYS_RTC_WakeUpInit 起的累计次数 */
uint32_t SYS_RTC_WakeUpCount(void);

/* 闹钟 A：到指定时:分:秒触发一次回调，每天同一时刻都会触发
 * 参数 : hour / minute / second 为目标时刻（0~23 / 0~59 / 0~59）
 *        mask 取 SYS_RTC_ALARM_MASK_HOUR / _MIN / _SEC 的位组合
 *        掩码位为 0 表示该字段不参与比较，可做每分钟或每小时闹钟
 * 返回 : 0 = 成功；1 = 参数非法 */
#define SYS_RTC_ALARM_MASK_SEC   0x01U
#define SYS_RTC_ALARM_MASK_MIN   0x02U
#define SYS_RTC_ALARM_MASK_HOUR  0x04U
#define SYS_RTC_ALARM_MASK_ALL   (SYS_RTC_ALARM_MASK_SEC | SYS_RTC_ALARM_MASK_MIN | \
                                  SYS_RTC_ALARM_MASK_HOUR)

uint8_t SYS_RTC_AlarmSet(uint8_t hour, uint8_t minute, uint8_t second,
                         uint8_t mask, void (*callback)(void));

/* 关闭闹钟 A */
void SYS_RTC_AlarmStop(void);

/* 格式化成字符串，自己传缓冲，不用 snprintf 以省 Flash
 * 参数 : buf 至少 24 字节；t 为时间；with_week 为是否带周X
 * 输出 : "2026-09-19 10:30:00"（19 字符）或 "2026-09-19 10:30:00 Sat"（23 字符）
 * 返回 : buf，方便直接套 printf("%s", ...)
 * 说明 : 星期缩写成 ASCII（Mon~Sun），库内不出现中文，避免编译器编码问题 */
char *SYS_RTC_Format(char *buf, const SysRtc_t *t, uint8_t with_week);

#endif /* __FWLIB_SYS_RTC_H */
