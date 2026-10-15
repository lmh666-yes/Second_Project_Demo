#include "sys_str.h"

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

/* 字符串与命令解析工具，全部就地操作，不分配内存，只用调用者传入的缓冲区
 * ToInt 越界饱和，Split 受 max 限制，Format 受 size 限制 */


/* 基础功能 */
char *SYS_STR_Find(const char *str, const char *sub)
{
    const char *a;
    const char *b;

    if (str == 0 || sub == 0) return 0;
    if (*sub == '\0') return (char *)str;       /* 空串是任何串的子串 */

    for (; *str != '\0'; str++) {
        a = str;
        b = sub;
        while ((*a != '\0') && (*b != '\0') && (*a == *b)) {
            a++;
            b++;
        }
        if (*b == '\0') return (char *)str;     /* sub 走完 = 命中 */
    }
    return 0;
}

/* 判断 ch 是否属于分隔符集合 */
static uint8_t str_is_delim(char ch, const char *delims)
{
    for (; *delims != '\0'; delims++) {
        if (*delims == ch) return 1U;
    }
    return 0U;
}

int SYS_STR_Split(char *str, const char *delims, char *argv[], int max)
{
    int     n = 0;
    uint8_t in_tok = 0U;

    if (str == 0 || delims == 0 || argv == 0 || max <= 0) return 0;

    for (;;) {
        char c = *str;

        if (c == '\0') break;

        if (str_is_delim(c, delims)) {
            if (in_tok) {                       /* 段结束:就地切断 */
                *str = '\0';
                in_tok = 0U;
            }
            /* 连续分隔符:跳过（不产生空段） */
        } else if (in_tok == 0U) {              /* 新段开始 */
            if (n >= max) break;                /* 段数满:停下（多余内容不动） */
            argv[n] = str;
            n++;
            in_tok = 1U;
        }

        str++;
    }
    return n;
}

int32_t SYS_STR_ParseInt(const char *str, uint8_t *ok)
{
    uint32_t u   = 0U;
    uint32_t lim;                                       /* 本符号下允许的最大绝对值 */
    uint8_t  neg = 0U;
    uint8_t  any = 0U;
    uint8_t  sat = 0U;
    int32_t  v;

    if (ok) *ok = 0U;
    if (str == 0) return 0;

    while ((*str == ' ') || (*str == '\t')) str++;      /* 跳过前导空白 */
    if ((*str == '+') || (*str == '-')) {               /* 符号 */
        neg = (*str == '-') ? 1U : 0U;
        str++;
    }

    /* 正负方向上限不同：正向上限 2147483647，负向上限 2147483648
     * 统一按 2147483647 饱和会把 "-2147483648" 解析成 -2147483647 */
    lim = (neg != 0U) ? 2147483648UL : 2147483647UL;

    /* 无符号累加：先判断乘 10 是否越界，越界即饱和并停止 */
    while ((*str >= '0') && (*str <= '9')) {
        any = 1U;
        if (u > (lim / 10UL)) {                         /* u*10 一定越界 */
            sat = 1U;
            break;
        }
        u = u * 10U + (uint32_t)(*str - '0');
        if (u > lim) {                                  /* 加上个位后越界:饱和 */
            sat = 1U;
            break;
        }
        str++;
    }

    if (any == 0U) return 0;                            /* 不是数字串 */

    if (sat != 0U) {
        v = (neg != 0U) ? (-2147483647L - 1L) : 2147483647L;   /* 饱和到本方向极限 */
        if (ok) *ok = 1U;
        return v;
    }

    /* 此处 u 不超 lim；负向多出的 2147483648 先强转 int32 再取负属未定义行为
     * 需显式构造 INT32_MIN */
    if (neg != 0U) {
        v = (u == 2147483648UL) ? (-2147483647L - 1L) : -(int32_t)u;
    } else {
        v = (int32_t)u;
    }
    if (ok) *ok = 1U;
    return v;
}

int32_t SYS_STR_ToInt(const char *str)
{
    return SYS_STR_ParseInt(str, 0);
}


/* 扩展功能 */
int SYS_STR_Format(char *buf, uint16_t size, const char *format, ...)
{
    va_list ap;
    int     len;

    if (buf == 0 || size == 0U || format == 0) return 0;
    buf[0] = '\0';

    va_start(ap, format);
    len = vsnprintf(buf, size, format, ap);
    va_end(ap);

    if (len < 0) { buf[0] = '\0'; return 0; }
    if ((size_t)len >= size) len = (int)size - 1;       /* 截断保护 */
    return len;
}
