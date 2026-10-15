#include "sys_str.h"
/* 配套指引见同名 .h;本文件为实现层 */

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

/*  sys_str.c — 【工具】字符串 / 命令解析工具  实现文件
 *  不依赖 C 字符串库,逐字符实现;仅 Format 用 vsnprintf 格式化。
 *  就地操作,无动态内存。溢出防护:ParseInt 饱和、Split 受 max 限制、
 *  Format 受 size 限制;用法见 sys_str.h。 */


/* ============== 基础功能 ============== */
char *SYS_STR_Find(const char *str, const char *sub)
{
    const char *a;
    const char *b;

    if (str == 0 || sub == 0) return 0;
    if (*sub == '\0') return (char *)str;       /* 空串按首次匹配返回 */

    for (; *str != '\0'; str++) {
        a = str;
        b = sub;
        while ((*a != '\0') && (*b != '\0') && (*a == *b)) {
            a++;
            b++;
        }
        if (*b == '\0') return (char *)str;     /* sub 走完即命中 */
    }
    return 0;
}

/* ch 属于 delims 返回 1 */
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
            if (in_tok) {                       /* 段结束,就地切断 */
                *str = '\0';
                in_tok = 0U;
            }
            /* 连续分隔符跳过,不产生空段 */
        } else if (in_tok == 0U) {              /* 新段 */
            if (n >= max) break;                /* 段数满则停,余下内容不动 */
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
    uint32_t lim;                                       /* 本符号方向上限 */
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

    /*  正负方向上限不同,分开取:正方向 2147483647,负方向 2147483648
     *   （-2147483648 可表示,+2147483648 不可）。按 2147483647 饱和时
     *   "-2147483648" 得 -2147483647,差 1。 */
    lim = (neg != 0U) ? 2147483648UL : 2147483647UL;

    /* 无符号累加:乘 10 必超则饱和停 */
    while ((*str >= '0') && (*str <= '9')) {
        any = 1U;
        if (u > (lim / 10UL)) {                         /* u*10 必越界 */
            sat = 1U;
            break;
        }
        u = u * 10U + (uint32_t)(*str - '0');
        if (u > lim) {                                  /* 加个位后越界,饱和 */
            sat = 1U;
            break;
        }
        str++;
    }

    if (any == 0U) return 0;                            /* 非数字串 */

    if (sat != 0U) {
        v = (neg != 0U) ? (-2147483647L - 1L) : 2147483647L;   /* 饱和到本方向极限值 */
        if (ok) *ok = 1U;
        return v;
    }

    /* 负方向多出的 2147483648 须显式构造 INT32_MIN,不可先 (int32_t)
     * 再取负（未定义行为）。 */
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


/* ============== 扩展功能 ============== */
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
    if ((size_t)len >= size) len = (int)size - 1;       /* 截断,返回写入长度 */
    return len;
}
