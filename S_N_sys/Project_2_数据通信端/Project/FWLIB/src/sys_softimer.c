#include "sys_softimer.h"
#include "sys_tick.h"       /* 时基:1ms 计数 SYS_TICK_GetTick */

/* 软定时器实现：表 + 编译期数量护栏
 * 每条记录 = 回调指针 + 周期 + 下次到点时刻（uint32 毫秒）
 * 判到点用无符号差值，时基 2^32ms（约 49 天）回绕后仍正确 */

/* 表项字段见结构体成员注释；改 SYS_SOFTIMER_MAX 时需同步增减表项
 * 表数组大小与 SYS_SOFTIMER_MAX 不一致时编译报错 */
typedef struct {
    void   (*cb)(void);     /* 回调（0 = 空槽） */
    uint32_t period;        /* 周期 ms */
    uint32_t due;           /* 下次到点时刻（SYS_TICK 毫秒时基） */
} Softimer_t;

static Softimer_t softimer_tbl[SYS_SOFTIMER_MAX];

/* 表数组大小与 SYS_SOFTIMER_MAX 不一致时编译报错 */
typedef char softimer_count_check[(sizeof(softimer_tbl) / sizeof(softimer_tbl[0]) == SYS_SOFTIMER_MAX) ? 1 : -1];


void SYS_SOFTIMER_Init(void)
{
    for (uint8_t i = 0U; i < SYS_SOFTIMER_MAX; i++) {
        softimer_tbl[i].cb = 0;
    }
}

/* 轮询：逐条判到点，到点则执行回调 */
uint8_t SYS_SOFTIMER_Poll(void)
{
    uint32_t now = SYS_TICK_GetTick();
    uint8_t  run = 0U;

    for (uint8_t i = 0U; i < SYS_SOFTIMER_MAX; i++) {
        if (softimer_tbl[i].cb == 0) continue;          /* 空槽 */

        if ((int32_t)(now - softimer_tbl[i].due) >= 0) {
            /* 先排下一轮再执行回调，回调里 Remove 自身不会出错 */
            softimer_tbl[i].due = now + softimer_tbl[i].period;
            softimer_tbl[i].cb();
            run++;
        }
    }
    return run;
}


/* 新增一条定时器：callback 非空且 period_ms > 0
 * 返回槽位号 0~SYS_SOFTIMER_MAX-1；表满或参数非法返回 0xFF */
uint8_t SYS_SOFTIMER_Add(void (*callback)(void), uint32_t period_ms)
{
    if (callback == 0 || period_ms == 0U) return 0xFFU;

    for (uint8_t i = 0U; i < SYS_SOFTIMER_MAX; i++) {
        if (softimer_tbl[i].cb == 0) {                  /* 找空槽 */
            softimer_tbl[i].cb     = callback;
            softimer_tbl[i].period = period_ms;
            softimer_tbl[i].due    = SYS_TICK_GetTick() + period_ms;
            return i;
        }
    }
    return 0xFFU;                                       /* 表满 */
}

/* 删除定时器：id 越界直接返回，0 表示空槽不再触发 */
void SYS_SOFTIMER_Remove(uint8_t id)
{
    if (id >= SYS_SOFTIMER_MAX) return;
    softimer_tbl[id].cb = 0;
}

/* 当前已注册的定时器条数 */
uint8_t SYS_SOFTIMER_Count(void)
{
    uint8_t n = 0U;

    for (uint8_t i = 0U; i < SYS_SOFTIMER_MAX; i++) {
        if (softimer_tbl[i].cb != 0) n++;
    }
    return n;
}
