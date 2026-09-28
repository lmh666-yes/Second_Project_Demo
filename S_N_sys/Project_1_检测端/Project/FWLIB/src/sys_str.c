#include "sys_str.h"
/* 配套指引 : "标准库对照 / 示例"注记见同名 .h;本文件为实现层 */

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

/* ================================================================
 *  sys_str.c —— 【工具】字符串 / 命令解析工具  实现文件
 * ================================================================
 *  实现要点 :
 *    ① 不依赖 C 字符串库（手写逐字符算法,便于对照学习）;
 *       仅 Format 借助编译器 stdio 的 vsnprintf 做"安全格式化";
 *    ② 全部就地操作、无动态内存——只用调用者给的缓冲与数组;
 *    ③ 溢出/越界全部防护:ToInt 饱和、Split 受 max 限制、Format 有 size
 * ================================================================ */


/* ================================================================
 *                    基础功能
 * ================================================================ */
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
    uint32_t u = 0U;
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

    /* 用无符号累加:先查"再乘 10 是否必超",超了就饱和并停 */
    while ((*str >= '0') && (*str <= '9')) {
        any = 1U;
        if (u > 214748364UL) {                          /* 214748364×10 > INT32_MAX */
            sat = 1U;
            break;
        }
        u = u * 10U + (uint32_t)(*str - '0');
        if (u > 2147483647UL) {                         /* 加上个位后越界:饱和 */
            sat = 1U;
            break;
        }
        str++;
    }

    if (any == 0U) return 0;                            /* 不是数字串 */
    v = (int32_t)u;
    if (sat != 0U) v = 2147483647L;                     /* 饱和到上限 */
    if (neg != 0U) v = -v;
    if (ok) *ok = 1U;
    return v;
}

int32_t SYS_STR_ToInt(const char *str)
{
    return SYS_STR_ParseInt(str, 0);
}


/* ================================================================
 *                    扩展功能
 * ================================================================ */
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
