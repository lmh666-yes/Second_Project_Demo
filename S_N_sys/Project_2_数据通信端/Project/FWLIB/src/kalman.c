#include "kalman.h"

/* ================================================================
 *  kalman.c —— 一维卡尔曼滤波实现
 * ================================================================
 *  硬件映射表
 *  ---------------------------------------------------------------
 *   功能        资源      板上位置      改这里
 *  ---------------------------------------------------------------
 *   无          纯运算    ——            改不了（也不占任何外设）
 *  ---------------------------------------------------------------
 *
 *  【数学就是这四行，看着吓人其实很简单】
 *     ① 预测协方差  p = p + q
 *     ② 算增益      K = p / (p + r)
 *     ③ 修正估计    x = x + K × (z - x)
 *     ④ 更新协方差  p = (1 - K) × p
 *
 *  【逐行解读】
 *   ① 每过一拍，我们对自己"位置"的把握就变差一点点(q)——这是"模型不完美"
 *   ② 当前的不确定性(p) 和 传感器噪声(r) 一比：
 *        p 越大(把握差) 或 r 越小(传感器准) → K 越大 → 越听传感器的
 *   ③ 只按 K 的比例去"靠近"测量值，K=0.1 就只走十分之一 —— 抖动被压掉
 *   ④ 看过测量后把握变好，p 缩小 —— 于是下一拍 K 会变小，越来越稳
 *
 *  ⚠ 要点：K 是**每拍动态算**的，这正是它比固定 alpha 的一阶低通强的地方。
 *     传感器突然真变了 → 残差(z-x)变大 → 修正量虽然按 K 缩了，
 *     但连续几拍跟不上时 p 会顶着 K 往上走，很快咬住新值。
 * ================================================================ */

/* ================================================================
 *                      编译期护栏（配错立刻报错）
 * ================================================================
 *  ⚠ 本文件**故意没有** typedef char xxx_check[...] 护栏，原因：
 *     ARMCC 不允许浮点参与"数组长度"这类整型常量表达式
 *     （会报 error: #31: expression must have integral type），
 *     而 Q/R/P 天生就是浮点。所以改成了**运行时校验** ——
 *     见 KALMAN_Init() / KALMAN_SetQR()，q 或 r 传 0、负数会直接
 *     返回 KALMAN_ERR_PARAM，不会带着坏参数一路算下去。 */


/* ================================================================
 *                        区块 2：基础功能
 * ================================================================ */

uint8_t KALMAN_Init(Kalman_t *kf, float q, float r)
{
    if (kf == 0) return KALMAN_ERR_PARAM;
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

    /* 第一次：直接把测量值当初始估计。
     * 不然 x 从 0 开始，要爬好久才接近真实值，前面几百个输出都是废的。 */
    if (kf->inited == 0U) {
        kf->x      = z;
        kf->p      = KALMAN_INIT_P;
        kf->k      = 1.0f;
        kf->resid  = 0.0f;
        kf->inited = 1U;
        return kf->x;
    }

    /* ① 预测：模型不完美，把握变差 */
    kf->p = kf->p + kf->q;

    /* ② 算增益：p/(p+r)。分母兜个底，别让它变成 0 除 */
    if ((kf->p + kf->r) <= 0.0f) {
        kf->k = 0.0f;
    } else {
        kf->k = kf->p / (kf->p + kf->r);
    }
    if (kf->k < 0.0f) kf->k = 0.0f;
    if (kf->k > 1.0f) kf->k = 1.0f;

    /* ③ 修正：按增益朝测量值靠近一步；残差留着给人看 */
    kf->resid = z - kf->x;
    kf->x     = kf->x + (kf->k * kf->resid);

    /* ④ 更新协方差：看过测量后更有把握。
     *    （标准形式是 (1-K)P，K→1 时 P→0，再用下限兜住防止算出 0） */
    kf->p = (1.0f - kf->k) * kf->p;
    if (kf->p < KALMAN_MIN_P) kf->p = KALMAN_MIN_P;

    return kf->x;
}

void KALMAN_Reset(Kalman_t *kf, float x0)
{
    if (kf == 0) return;

    kf->x      = x0;
    kf->p      = KALMAN_INIT_P;
    kf->k      = 1.0f;
    kf->resid  = 0.0f;
    kf->inited = 1U;        /* 给定起点就算"有初值"了，不再拿下一个测量值当起点 */
}


/* ================================================================
 *                        区块 3：扩展功能
 * ================================================================ */

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
    if (q <= 0.0f || r <= 0.0f) return KALMAN_ERR_PARAM;

    kf->q = q;
    kf->r = r;
    /* 不动 x 和 p —— 在线调参时不该把已收敛的状态扔掉 */
    return KALMAN_OK;
}

uint8_t KALMAN_AutoTune(Kalman_t *kf, float jump, float smooth)
{
    if (kf == 0) return KALMAN_ERR_PARAM;

    /* 用绝对值容错：调用方传负数也当正数用 */
    if (jump   < 0.0f) jump   = -jump;
    if (smooth < 0.0f) smooth = -smooth;
    if (jump   <= 0.0f) return KALMAN_ERR_PARAM;
    if (smooth <= 0.0f) return KALMAN_ERR_PARAM;

    /* 方差 = 标准差的平方，所以这边把"幅度"直接平方拿来当协方差 */
    return KALMAN_SetQR(kf, jump * jump, smooth * smooth);
}

int32_t KALMAN_UpdateInt(Kalman_t *kf, int32_t z)
{
    float y;

    if (kf == 0) return z;

    y = KALMAN_Update(kf, (float)z);

    /* 四舍五入成整数（y 可能为负，所以正负分开处理） */
    return (int32_t)((y >= 0.0f) ? (y + 0.5f) : (y - 0.5f));
}

/* ==================== kalman.c end ==================== */
