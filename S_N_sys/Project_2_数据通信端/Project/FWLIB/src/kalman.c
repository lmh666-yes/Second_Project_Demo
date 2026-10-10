#include "kalman.h"

/* 一维卡尔曼滤波实现
 * 每拍四步：
 * 1) 预测协方差  p = p + q
 * 2) 算增益      K = p / (p + r)
 * 3) 修正估计    x = x + K * (z - x)
 * 4) 更新协方差  p = (1 - K) * p
 * K 每拍动态计算；q、r 为噪声协方差，必须为正的有限值
 * 本文件为纯运算，不占用任何外设 */

/* 参数校验放在运行时：KALMAN_Init / KALMAN_SetQR 对非有限值、非正值
 * 返回 KALMAN_ERR_PARAM。原因是 ARMCC 不允许浮点参与数组长度这类整型
 * 常量表达式（报 error: #31: expression must have integral type），
 * 无法用 typedef char xxx_check[...] 做编译期护栏 */


/* 判断浮点数是否为正常有限值
 * 返回 : 1 = 正常有限值；0 = NaN 或 ±Inf
 * 卡尔曼是带记忆的递推，NaN 一旦进入状态就出不去；参数里的 NaN 同样致命，因为 q <= 0.0f 与 NaN 比较为假
 * 手写比较，不依赖 <math.h>；不要用 -ffast-math 编译，该选项假设没有 NaN，会把首行判断优化掉 */
static uint8_t kalman_is_finite(float v)
{
    if (!(v == v)) return 0U;                   /* NaN */
    if (v >  3.402823466e38f) return 0U;        /* +Inf */
    if (v < -3.402823466e38f) return 0U;        /* -Inf */
    return 1U;
}


/* 基础功能 */

uint8_t KALMAN_Init(Kalman_t *kf, float q, float r)
{
    if (kf == 0) return KALMAN_ERR_PARAM;
    /* NaN 必须显式挡:q <= 0.0f 对 NaN 为假,会放它过去 */
    if (kalman_is_finite(q) == 0U) return KALMAN_ERR_PARAM;
    if (kalman_is_finite(r) == 0U) return KALMAN_ERR_PARAM;
    if (q <= 0.0f) return KALMAN_ERR_PARAM;     /* 噪声协方差必须为正 */
    if (r <= 0.0f) return KALMAN_ERR_PARAM;

    kf->x      = 0.0f;
    kf->p      = KALMAN_INIT_P;
    kf->q      = q;
    kf->r      = r;
    kf->k      = 0.0f;
    kf->resid  = 0.0f;
    kf->inited = 0U;

    return KALMAN_OK;
}

float KALMAN_Update(Kalman_t *kf, float z)
{
    if (kf == 0) return 0.0f;

    /* 测量值非有限时整拍丢弃：返回上次估计，残差保持上次的值
     * 残差不要填 0，上层用残差判异常，填 0 会被当成这次测量很准
     * 不要按 z=0 继续算：估计值会被往 0 拉一步，且永久留在状态里 */
    if (kalman_is_finite(z) == 0U) {
        return kf->x;
    }

    /* 首次调用把测量值当初始估计，同时置 inited，避免 x 从 0 缓慢爬升 */
    if (kf->inited == 0U) {
        kf->x      = z;
        kf->p      = KALMAN_INIT_P;
        kf->k      = 1.0f;
        kf->resid  = 0.0f;
        kf->inited = 1U;
        return kf->x;
    }

    /* 1) 预测：模型不完美，把握变差 */
    kf->p = kf->p + kf->q;

    /* 2) 算增益：p/(p+r)，分母兜底，避免 0 除 */
    if ((kf->p + kf->r) <= 0.0f) {
        kf->k = 0.0f;
    } else {
        kf->k = kf->p / (kf->p + kf->r);
    }
    if (kf->k < 0.0f) kf->k = 0.0f;
    if (kf->k > 1.0f) kf->k = 1.0f;

    /* 3) 修正：按增益朝测量值靠近一步，残差供上层判异常 */
    kf->resid = z - kf->x;
    kf->x     = kf->x + (kf->k * kf->resid);

    /* 4) 更新协方差 (1-K)P，K 趋近 1 时 P 趋近 0，用 KALMAN_MIN_P 兜住 */
    kf->p = (1.0f - kf->k) * kf->p;
    if (kf->p < KALMAN_MIN_P) kf->p = KALMAN_MIN_P;

    /* 兜底：输入与参数均已验有限，正常算不出 NaN/Inf
     * 万一仍然坏了就退回未初始化状态，让下一个正常样本重新起步 */
    if (kalman_is_finite(kf->x) == 0U || kalman_is_finite(kf->p) == 0U) {
        kf->x      = 0.0f;
        kf->p      = KALMAN_INIT_P;
        kf->k      = 0.0f;
        kf->resid  = 0.0f;
        kf->inited = 0U;
        return 0.0f;
    }

    return kf->x;
}

void KALMAN_Reset(Kalman_t *kf, float x0)
{
    if (kf == 0) return;

    /* 起点也不能是 NaN/Inf，否则滤波器等于永久置坏 */
    if (kalman_is_finite(x0) == 0U) x0 = 0.0f;

    kf->x      = x0;
    kf->p      = KALMAN_INIT_P;
    kf->k      = 1.0f;
    kf->resid  = 0.0f;
    kf->inited = 1U;        /* 给定起点即视为有初值，下一个测量值不再当起点 */
}


/* 扩展功能 */

float KALMAN_GetValue(const Kalman_t *kf)
{
    return (kf == 0) ? 0.0f : kf->x;
}

float KALMAN_GetGain(const Kalman_t *kf)
{
    return (kf == 0) ? 0.0f : kf->k;
}

float KALMAN_GetResid(const Kalman_t *kf)
{
    return (kf == 0) ? 0.0f : kf->resid;
}

uint8_t KALMAN_SetQR(Kalman_t *kf, float q, float r)
{
    if (kf == 0) return KALMAN_ERR_PARAM;
    if (kalman_is_finite(q) == 0U || kalman_is_finite(r) == 0U) return KALMAN_ERR_PARAM;
    if (q <= 0.0f || r <= 0.0f) return KALMAN_ERR_PARAM;

    kf->q = q;
    kf->r = r;
    /* 不动 x 和 p：在线调参不应丢弃已收敛的状态 */
    return KALMAN_OK;
}

uint8_t KALMAN_AutoTune(Kalman_t *kf, float jump, float smooth)
{
    if (kf == 0) return KALMAN_ERR_PARAM;

    /* 先挡 NaN:jump < 0.0f 和 jump <= 0.0f 对 NaN 都为假,两个判断都拦不住
     * 这里不挡，错误会由 SetQR 报成 q 不对，来源难以定位 */
    if (kalman_is_finite(jump) == 0U || kalman_is_finite(smooth) == 0U) return KALMAN_ERR_PARAM;

    /* 用绝对值容错：调用方传负数也当正数用 */
    if (jump   < 0.0f) jump   = -jump;
    if (smooth < 0.0f) smooth = -smooth;
    if (jump   <= 0.0f) return KALMAN_ERR_PARAM;
    if (smooth <= 0.0f) return KALMAN_ERR_PARAM;

    /* 方差 = 标准差的平方，所以把幅度平方后当协方差
     * 平方会溢出：jump = 1e30 时平方为 Inf（float 上限约 3.4e38），由 SetQR 拒掉 */
    return KALMAN_SetQR(kf, jump * jump, smooth * smooth);
}

int32_t KALMAN_UpdateInt(Kalman_t *kf, int32_t z)
{
    float y;

    if (kf == 0) return z;

    y = KALMAN_Update(kf, (float)z);

    /* 四舍五入成整数，y 可能为负，正负分开处理 */
    return (int32_t)((y >= 0.0f) ? (y + 0.5f) : (y - 0.5f));
}
