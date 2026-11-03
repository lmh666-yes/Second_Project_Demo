#include "sys_softimer.h"
#include "sys_tick.h"       /* 时基:1ms 计数 SYS_TICK_GetTick */

/* ================================================================
 *  sys_softimer.c  软定时器实现
 *  表项:回调指针 + 周期 ms + 下次到点时刻(uint32 毫秒);回绕用无符号差值比较(2^32ms 约 49 天)
 * ================================================================ */

/* 表项:增删 SYS_SOFTIMER_MAX 时同步修改;表项数不匹配由编译期护栏拦截 */
typedef struct {
    void   (*cb)(void);     /* 回调（0 = 空槽） */
    uint32_t period;        /* 周期 ms */
    uint32_t due;           /* 下次到点时刻（SYS_TICK 毫秒时基） */
} Softimer_t;

static Softimer_t softimer_tbl[SYS_SOFTIMER_MAX];

/* 编译期护栏：表项数与宏一致 */
typedef char softimer_count_check[(sizeof(softimer_tbl) / sizeof(softimer_tbl[0]) == SYS_SOFTIMER_MAX) ? 1 : -1];


/* ================ 区块 2：基础功能 ================ */
void SYS_SOFTIMER_Init(void)
{
    for (uint8_t i = 0U; i < SYS_SOFTIMER_MAX; i++) {
        softimer_tbl[i].cb = 0;
    }
}

/* 轮询:无符号差值判断到点,回绕安全;返回本轮执行的回调数 */
uint8_t SYS_SOFTIMER_Poll(void)
{
    uint32_t now = SYS_TICK_GetTick();
    uint8_t  run = 0U;

    for (uint8_t i = 0U; i < SYS_SOFTIMER_MAX; i++) {
        if (softimer_tbl[i].cb == 0) continue;          /* 空槽 */

        if ((int32_t)(now - softimer_tbl[i].due) >= 0) {
            /* 先排下一轮再执行回调:回调内 Remove 自身安全 */
            softimer_tbl[i].due = now + softimer_tbl[i].period;
            softimer_tbl[i].cb();
            run++;
        }
    }
    return run;
}


/* ================ 区块 3：扩展功能 ================ */
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

void SYS_SOFTIMER_Remove(uint8_t id)
{
    if (id >= SYS_SOFTIMER_MAX) return;
    softimer_tbl[id].cb = 0;
}

uint8_t SYS_SOFTIMER_Count(void)
{
    uint8_t n = 0U;

    for (uint8_t i = 0U; i < SYS_SOFTIMER_MAX; i++) {
        if (softimer_tbl[i].cb != 0) n++;
    }
    return n;
}
