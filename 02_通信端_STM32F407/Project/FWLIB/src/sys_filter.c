#include "sys_filter.h"
/* 本文件为 sys_filter 的实现层，接口说明见同名 .h */

#include <math.h>       /* expf：一阶低通 α 的精确换算 */

/* sys_filter.c: 数字滤波工具集实现
 * 排序用选择排序：数组只有几十个数，无递归、无额外栈，省 Flash
 * 除一阶低通 α 计算外全程无浮点，可用于中断上下文 */


/* 内部工具：浮点数是否为正常有限值
 * 返回 : 1 = 正常有限值；0 = NaN 或 ±Inf
 * 一阶低通是带记忆的递推式 y = α·x + (1-α)·y，一拍 NaN/Inf 会让后续输出永远为
 * NaN/Inf，重新 LpfInit/LpfReset 之前喂正常值也恢复不了。
 * 手写比较而不用 <math.h> 的 isnan/isfinite：NaN 的特征是与自身比较不相等
 * （IEEE754 定义）；±Inf 直接与 float 最大可表示值 3.402823466e38f 比较
 * NaN 的来源：I2C 读传感器失败时上层返回 NAN，或 0.0f/0.0f 之类的换算
 * 本文件不得用 -ffast-math 编译：该选项假设不存在 NaN，会优化掉第一个判断 */
static uint8_t filter_is_finite(float v)
{
    if (!(v == v)) return 0U;                   /* NaN */
    if (v >  3.402823466e38f) return 0U;        /* +Inf（也含溢出的超大值） */
    if (v < -3.402823466e38f) return 0U;        /* -Inf */
    return 1U;
}


/* 区块 2：无状态滤波 */
uint16_t FILTER_Limit(uint16_t prev, uint16_t now, uint16_t max_step)
{
    uint16_t diff;

    diff = (now > prev) ? (uint16_t)(now - prev) : (uint16_t)(prev - now);

    /* 变化量在允许范围内 → 采信本次；否则认为是被干扰，保持上次输出 */
    return (diff <= max_step) ? now : prev;
}

/* 完整升序排序（就地，选择排序）
 * 必须排到底而不是只排到中位位置：截尾均值要掐掉首尾各 trim 个，
 * 尾部没排好会把野值算进去。见 FILTER_TrimmedMean 的注释 */
static void filter_sort_full(uint16_t *buf, uint16_t n)
{
    uint16_t i;
    uint16_t j;
    uint16_t tmp;

    for (i = 0U; (uint16_t)(i + 1U) < n; i++) {
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
}

uint16_t FILTER_Median(uint16_t *buf, uint16_t n)
{
    uint16_t i;
    uint16_t j;
    uint16_t tmp;

    if (buf == 0 || n == 0U) return 0U;
    if (n == 1U) return buf[0];

    /* 选择排序（升序）：排到中间位置就够，省一半时间
     * 副作用：返回后数组只有首部有序，尾部仍乱；需要完整升序数组的算法
     * （如 FILTER_TrimmedMean）不能复用本函数，必须另调 filter_sort_full() */
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

    return buf[n / 2U];          /* 偶数个时取偏上的那个 */
}

uint16_t FILTER_MovingAvg(const uint16_t *buf, uint16_t n)
{
    uint32_t sum = 0;
    uint16_t i;

    if (buf == 0 || n == 0U) return 0U;

    for (i = 0; i < n; i++) sum += buf[i];

    /* 四舍五入：加 n/2 再整除，与 FILTER_AvgGet 口径一致 */
    return (uint16_t)((sum + (n / 2U)) / n);
}

uint16_t FILTER_TrimmedMean(uint16_t *buf, uint16_t n, uint16_t trim)
{
    uint32_t sum = 0;
    uint16_t i;
    uint16_t keep;

    if (buf == 0 || n == 0U) return 0U;
    if (2U * trim >= n) return FILTER_MovingAvg(buf, n);   /* trim 过大时退回普通平均 */

    /* 必须先全排序再求和：下面假设整个数组已升序
     * FILTER_Median 只做半程排序（for (i = 0; i <= n/2; i++)），返回后尾部仍是乱的，
     * 直接复用会把尾部野值计入求和，抗野值能力失效，只有 trim=0 时碰巧正确
     * 所以这里用 filter_sort_full() */
    filter_sort_full(buf, n);              /* 全排序，会改写 buf；见头文件说明 */

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


/* 区块 3：一阶低通 */
void FILTER_LpfInit(FilterLpf_t *f, float fc_hz, float fs_hz)
{
    float a;

    if (f == 0) return;

    /* NaN 也要走参数不对这条路：下面的 <= 0.0f 对 NaN 为假，会一路算到
     * expf(NaN) 得 NaN，被 SetAlpha 挡成 alpha=0，表现为滤波器永不更新 */
    if (filter_is_finite(fc_hz) == 0U || filter_is_finite(fs_hz) == 0U) {
        a = 0.2f;                                   /* 参数不对时的默认 α */
    } else if (fc_hz <= 0.0f || fs_hz <= 0.0f) {
        a = 0.2f;                                   /* 参数不对时的默认 α */
    } else if (fc_hz >= (fs_hz * 0.5f)) {
        /* 截止频率达到奈奎斯特频率以上，此采样率下滤不出这条曲线，一阶低通
         * 只能全通，取 α = 1.0 使输出等于输入，不取 0.5 之类的中间值 */
        a = 1.0f;
    } else {
        /* 用精确指数式 α = 1 - exp(-2π·fc/fs)，不用近似式 α ≈ 2π·fc/fs
         * 近似式仅在 fc << fs 时成立；fc 与 fs 同量级时偏差大，例如 fs=1kHz、
         * fc=50Hz：精确值 0.270，近似值 0.314，实际截止频率高约 16% */
        a = 1.0f - expf((0.0f - 2.0f * 3.14159265f * fc_hz) / fs_hz);

        if (a > 1.0f) a = 1.0f;
        if (a < 0.001f) a = 0.001f;                 /* 下限防止输出永不更新 */
    }

    FILTER_LpfSetAlpha(f, a);
}

void FILTER_LpfSetAlpha(FilterLpf_t *f, float alpha)
{
    if (f == 0) return;

    /* NaN 必须先挡掉：与 NaN 的比较都为假，下面两句钳位都不生效，
     * alpha 原样是 NaN，之后每一拍输出都是 NaN */
    if (!(alpha == alpha)) alpha = 0.0f;

    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    f->alpha   = alpha;
    f->y       = 0.0f;
    f->started = 0U;
}

float FILTER_LpfUpdate(FilterLpf_t *f, float x)
{
    if (f == 0) return x;

    /* 挡住 NaN / ±Inf（理由见 filter_is_finite 上方注释）：坏样本整拍丢弃，
     * 保持上次输出不变；不能当成 0 喂进去，否则曲线上会出现下冲；
     * 也不在此处重置滤波器。何时重新初始化由调用方判断 */
    if (filter_is_finite(x) == 0U) {
        return f->y;
    }

    if (!f->started) {
        /* 第一拍直接把输入当输出，否则会从 0 慢慢爬上来 */
        f->y       = x;
        f->started = 1U;
        return x;
    }

    f->y = f->alpha * x + (1.0f - f->alpha) * f->y;

    /* 兜底：α 为有限值且 x 已验有限，正常算不出 NaN/Inf；若调用方绕过
     * SetAlpha 直接改结构体导致输出坏值，则清零并等待下次重新起步 */
    if (filter_is_finite(f->y) == 0U) {
        f->y       = 0.0f;
        f->started = 0U;                /* 下次喂好样本时重新起步 */
        return 0.0f;
    }

    return f->y;
}

void FILTER_LpfReset(FilterLpf_t *f)
{
    if (f == 0) return;
    f->y       = 0.0f;
    f->started = 0U;
}


/* 滑动平均对象（环形缓冲，单步 O(1)）
 * 与 FILTER_MovingAvg（无状态、每次 O(n) 全量求和）的区别见头文件区块 4
 * 实现上两点约束：
 * 1) 只有窗口已满（count == size）时才做减法；预热期 buf[] 中未写过的槽为 0，
 *    减它们会算错 running sum
 * 2) 索引写完就回绕；否则 idx == size 时 buf[idx] 越界到 FILTER_BUF_MAX 之外 */
void FILTER_AvgInit(FilterAvg_t *a, uint16_t size)
{
    uint16_t i;

    if (a == 0) return;

    if (size == 0U)             size = FILTER_AVG_DEFAULT_SIZE;
    if (size > FILTER_BUF_MAX)  size = FILTER_BUF_MAX;   /* 截到上限，防越界 */

    for (i = 0U; i < FILTER_BUF_MAX; i++) a->buf[i] = 0U;
    a->sum   = 0UL;
    a->size  = size;
    a->count = 0U;
    a->idx   = 0U;
}

uint16_t FILTER_AvgUpdate(FilterAvg_t *a, uint16_t x)
{
    if (a == 0) return x;

    /* 未 Init 就调用时的兜底：size 为 0 时下面的比较都失效，表现为永远返回 0 */
    if (a->size == 0U || a->size > FILTER_BUF_MAX) {
        FILTER_AvgInit(a, FILTER_AVG_DEFAULT_SIZE);
    }
    if (a->idx >= a->size) a->idx = 0U;      /* 索引兜底，见上方说明 2) */

    if (a->count < a->size) {
        /* 预热期未填满：只写、只加，不做减法，见上方说明 1) */
        a->buf[a->idx] = x;
        a->sum += (uint32_t)x;
        a->count++;
    } else {
        /* 窗口已满：先减去将被覆盖的最老样本，再写新值并累加 */
        a->sum -= (uint32_t)a->buf[a->idx];
        a->buf[a->idx] = x;
        a->sum += (uint32_t)x;
    }

    a->idx = (uint16_t)(a->idx + 1U);
    if (a->idx >= a->size) a->idx = 0U;

    return FILTER_AvgGet(a);
}

uint16_t FILTER_AvgGet(const FilterAvg_t *a)
{
    if (a == 0 || a->count == 0U) return 0U;

    /* 四舍五入：加 count/2 再整除，与 FILTER_MovingAvg 口径一致 */
    return (uint16_t)((a->sum + (a->count / 2U)) / a->count);
}

void FILTER_AvgReset(FilterAvg_t *a)
{
    if (a == 0) return;
    a->sum   = 0UL;
    a->count = 0U;
    a->idx   = 0U;
    /* buf[] 不清：count 归零后旧值不参与运算（只有 count == size 时才做减法），
     * 预热期会被新样本逐个覆盖 */
}
