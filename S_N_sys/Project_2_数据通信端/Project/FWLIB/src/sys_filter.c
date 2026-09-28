#include "sys_filter.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */

/* ================================================================
 *  sys_filter.c —— 数字滤波工具集  实现文件
 * ================================================================
 *  实现说明 :
 *    · 排序用最简单的"选择排序"——数组只有几个到几十个数，
 *      冒泡/选择排序比快排更快（无递归、无额外栈），也更省 Flash；
 *    · 全程无浮点（除一阶低通的 α 计算），适合放到中断里跑。
 * ================================================================ */


/* ================================================================
 *                    区块 2：无状态滤波
 * ================================================================ */
uint16_t FILTER_Limit(uint16_t prev, uint16_t now, uint16_t max_step)
{
    uint16_t diff;

    diff = (now > prev) ? (uint16_t)(now - prev) : (uint16_t)(prev - now);

    /* 变化量在允许范围内 → 采信本次；否则认为是被干扰，保持上次输出 */
    return (diff <= max_step) ? now : prev;
}

uint16_t FILTER_Median(uint16_t *buf, uint16_t n)
{
    uint16_t i;
    uint16_t j;
    uint16_t tmp;

    if (buf == 0 || n == 0U) return 0U;
    if (n == 1U) return buf[0];

    /* 选择排序（升序）：只排到中间那个位置就够，省一半时间 */
    for (i = 0; i <= (n / 2U); i++) {
        uint16_t min_i = i;
        for (j = (uint16_t)(i + 1U); j < n; j++) {
            if (buf[j] < buf[min_i]) min_i = j;
        }
        if (min_i != i) {
            tmp = buf[i];
            buf[i] = buf[min_i];
            buf[min_i] = tmp;
        }
    }

    return buf[n / 2U];          /* 偶数个时取偏上的那个（工程上无差别） */
}

uint16_t FILTER_MovingAvg(const uint16_t *buf, uint16_t n)
{
    uint32_t sum = 0;
    uint16_t i;

    if (buf == 0 || n == 0U) return 0U;

    for (i = 0; i < n; i++) sum += buf[i];

    /* 四舍五入：加 n/2 再整除 */
    return (uint16_t)((sum + (n / 2U)) / n);
}

uint16_t FILTER_TrimmedMean(uint16_t *buf, uint16_t n, uint16_t trim)
{
    uint32_t sum = 0;
    uint16_t i;
    uint16_t keep;

    if (buf == 0 || n == 0U) return 0U;
    if (2U * trim >= n) return FILTER_MovingAvg(buf, n);   /* 参数不合理：退回普通平均 */

    FILTER_Median(buf, n);                 /* 借用同一个排序（会改 buf） */

    keep = (uint16_t)(n - 2U * trim);
    for (i = trim; i < (uint16_t)(n - trim); i++) sum += buf[i];

    return (uint16_t)((sum + (keep / 2U)) / keep);
}

void FILTER_MinMax(const uint16_t *buf, uint16_t n, uint16_t *min_out, uint16_t *max_out)
{
    uint16_t mn;
    uint16_t mx;
    uint16_t i;

    if (buf == 0 || n == 0U) {
        if (min_out != 0) *min_out = 0;
        if (max_out != 0) *max_out = 0;
        return;
    }

    mn = buf[0];
    mx = buf[0];
    for (i = 1U; i < n; i++) {
        if (buf[i] < mn) mn = buf[i];
        if (buf[i] > mx) mx = buf[i];
    }

    if (min_out != 0) *min_out = mn;
    if (max_out != 0) *max_out = mx;
}


/* ================================================================
 *                    区块 3：一阶低通
 * ================================================================ */
void FILTER_LpfInit(FilterLpf_t *f, float fc_hz, float fs_hz)
{
    float a;

    if (f == 0) return;

    if (fc_hz <= 0.0f || fs_hz <= 0.0f) {
        a = 0.2f;                                   /* 参数不对给个安全的默认值 */
    } else {
        /* α ≈ 2π·fc/fs（fc << fs 时的一阶近似） */
        a = (2.0f * 3.14159265f * fc_hz) / fs_hz;
        if (a > 1.0f) a = 1.0f;
        if (a < 0.001f) a = 0.001f;                 /* 下限防止"永不更新" */
    }

    FILTER_LpfSetAlpha(f, a);
}

void FILTER_LpfSetAlpha(FilterLpf_t *f, float alpha)
{
    if (f == 0) return;

    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    f->alpha   = alpha;
    f->y       = 0.0f;
    f->started = 0U;
}

float FILTER_LpfUpdate(FilterLpf_t *f, float x)
{
    if (f == 0) return x;

    if (!f->started) {
        /* 第一拍：直接把输入当输出 —— 否则会从 0 慢慢爬上来，
         * 对"一开始就要准"的场合（如开机测电压）是致命的 */
        f->y       = x;
        f->started = 1U;
        return x;
    }

    f->y = f->alpha * x + (1.0f - f->alpha) * f->y;
    return f->y;
}

void FILTER_LpfReset(FilterLpf_t *f)
{
    if (f == 0) return;
    f->y       = 0.0f;
    f->started = 0U;
}
