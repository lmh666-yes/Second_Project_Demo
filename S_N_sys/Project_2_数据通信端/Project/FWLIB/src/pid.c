#include "pid.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */

/* ================================================================
 *  pid.c —— PID 控制器  实现文件
 * ================================================================
 *  本文件实现的是"带工程防护"的 PID，核心只有三行，其余都是坑位的补丁：
 *      P = kp * e
 *      I = ki * ∫e dt      ← 要防积分饱和 / 起步猛冲
 *      D = kd * de/dt      ← 要防设定值突变尖峰 / 噪声放大
 *
 *  两条关键公式（位置式 / 增量式）:
 *      位置式: out = Kp·e + Ki·Σ(e·dt) + Kd·(e - e_prev)/dt
 *      增量式: Δout = Kp·(e-e_prev) + Ki·e·dt + Kd·(e-2e_prev+e_prev2)/dt
 *              其中增量式的"历史"由 prev_error 与 integral 两个变量承担
 *              （增量式本身不需要积分项，但为了限幅与显示，这里仍跟踪 integral）
 * ================================================================ */


/* 内部辅助：把值限制到 [lo, hi] */
static float pid_clamp(float v, float lo, float hi)
{
    if (lo > hi) { float t = lo; lo = hi; hi = t; }
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


/* ================================================================
 *                    区块 2：初始化与配置
 * ================================================================ */
void PID_Init(Pid_t *pid, float kp, float ki, float kd)
{
    if (pid == 0) return;

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;

    /* 限幅给保守默认值：宁可输出小一点，也不要上电满输出 */
    pid->out_min        = PID_DEFAULT_OUT_MIN;
    pid->out_max        = PID_DEFAULT_OUT_MAX;
    pid->integral_min   = PID_DEFAULT_OUT_MIN;
    pid->integral_max   = PID_DEFAULT_OUT_MAX;

    pid->dead_zone          = 0.0f;
    pid->separate_threshold = 0.0f;      /* 0 = 不做积分分离 */
    pid->deriv_on_meas      = 1U;         /* 默认微分先行（更稳） */
    pid->mode               = PID_MODE_POSITION;

    PID_Reset(pid);
}

void PID_Reset(Pid_t *pid)
{
    if (pid == 0) return;

    pid->integral   = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev_meas  = 0.0f;
    pid->output     = 0.0f;
    pid->p_term     = 0.0f;
    pid->i_term     = 0.0f;
    pid->d_term     = 0.0f;
    pid->first_run  = 1U;
}

void PID_SetTunings(Pid_t *pid, float kp, float ki, float kd)
{
    if (pid == 0) return;
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

void PID_SetOutputLimit(Pid_t *pid, float min, float max)
{
    if (pid == 0) return;
    pid->out_min = min;
    pid->out_max = max;

    /* 输出限幅变小了，积分限幅跟着收缩，避免"积分还能涨、输出出不来" */
    if (pid->integral_min < min) pid->integral_min = min;
    if (pid->integral_max > max) pid->integral_max = max;
}

void PID_SetIntegralLimit(Pid_t *pid, float min, float max)
{
    if (pid == 0) return;
    pid->integral_min = min;
    pid->integral_max = max;
}

void PID_SetMode(Pid_t *pid, uint8_t mode)
{
    if (pid == 0) return;
    if (pid->mode != mode) {
        /* 换模式时清状态：否则增量式的累加基准会对不上 */
        pid->mode = mode;
        PID_Reset(pid);
    }
}

void PID_SetDeadZone(Pid_t *pid, float dead_zone)
{
    if (pid == 0) return;
    pid->dead_zone = (dead_zone < 0.0f) ? -dead_zone : dead_zone;
}

void PID_SetSeparateThreshold(Pid_t *pid, float threshold)
{
    if (pid == 0) return;
    pid->separate_threshold = (threshold < 0.0f) ? 0.0f : threshold;
}

void PID_SetDerivOnMeasurement(Pid_t *pid, uint8_t enable)
{
    if (pid == 0) return;
    pid->deriv_on_meas = enable ? 1U : 0U;
}


/* ================================================================
 *                    区块 3：核心计算
 * ================================================================ */
float PID_Update(Pid_t *pid, float setpoint, float measure, float dt)
{
    float error;
    float error_used;
    float p;
    float i;
    float d;
    float out;

    if (pid == 0) return 0.0f;

    /* dt 容错：传 0（忘了传）时按 10ms 处理，避免除 0 与积分暴走 */
    if (dt < PID_EPSILON) dt = 0.01f;

    error = setpoint - measure;

    /* ① 死区：小误差直接当 0，防止执行器在目标点附近抖 */
    error_used = error;
    if (pid->dead_zone > 0.0f) {
        float ae = (error < 0.0f) ? -error : error;
        if (ae < pid->dead_zone) error_used = 0.0f;
    }

    /* ② 比例项 */
    p = pid->kp * error_used;

    /* ③ 积分项：先累加再限幅（连累加值一起夹住 = 抗积分饱和） */
    {
        float ae = (error < 0.0f) ? -error : error;

        /* 积分分离：误差太大时"暂停积分"，改善起步超调 */
        if (pid->separate_threshold <= 0.0f || ae <= pid->separate_threshold) {
            pid->integral += error_used * dt;
            pid->integral = pid_clamp(pid->integral, pid->integral_min, pid->integral_max);
        }
    }
    i = pid->ki * pid->integral;

    /* ④ 微分项 */
    if (pid->first_run) {
        /* 第一拍没有历史数据：微分给 0，否则会产生一个巨大的尖峰 */
        d = 0.0f;
        pid->first_run = 0U;
    } else if (pid->deriv_on_meas) {
        /* 微分先行：对"测量值"求导（设定值跳变时不产生冲击）
         *   等价于 d(误差)/dt = -d(测量值)/dt */
        d = -pid->kd * (measure - pid->prev_meas) / dt;
    } else {
        d = pid->kd * (error_used - pid->prev_error) / dt;
    }

    /* ⑤ 合成输出 */
    if (pid->mode == PID_MODE_INCREMENT) {
        /* 增量式：本次增量累加到上次输出上
         *   Δout = Kp·Δe + Ki·e·dt + Kd·Δ²e/dt
         * 这里用"分项做差"的等价写法，省掉再维护 e_prev2：
         *   Δout = (p - p_prev) + i + (d - d_prev)
         * 注意：结构体每个实例各存各的 p_term/d_term，
         *       所以多个 PID 并行时不会互相串味。 */
        out = pid->output
            + (p - pid->p_term)
            +  i
            + (d - pid->d_term);
    } else {
        /* 位置式 */
        out = p + i + d;
    }

    /* ⑥ 输出限幅 */
    out = pid_clamp(out, pid->out_min, pid->out_max);

    /* ⑦ 保存历史（顺序很重要：必须用"本拍结果"更新，供下一拍做差） */
    pid->p_term     = p;
    pid->i_term     = i;
    pid->d_term     = d;
    pid->prev_error = error_used;
    pid->prev_meas  = measure;
    pid->output     = out;

    return out;
}


/* ================================================================
 *                    区块 3：辅助接口
 * ================================================================ */
void PID_GetTerms(const Pid_t *pid, float *p_term, float *i_term, float *d_term)
{
    if (pid == 0) return;
    if (p_term != 0) *p_term = pid->p_term;
    if (i_term != 0) *i_term = pid->i_term;
    if (d_term != 0) *d_term = pid->d_term;
}

float PID_GetOutput(const Pid_t *pid)
{
    return (pid == 0) ? 0.0f : pid->output;
}

void PID_PresetOutput(Pid_t *pid, float value)
{
    if (pid == 0) return;

    /* 把积分调整到"能撑起这个输出"的水平，实现无扰动切换 */
    if (pid->ki > PID_EPSILON || pid->ki < -PID_EPSILON) {
        pid->integral = pid_clamp(value / pid->ki, pid->integral_min, pid->integral_max);
    } else {
        pid->integral = 0.0f;
    }

    pid->output     = pid_clamp(value, pid->out_min, pid->out_max);
    pid->prev_error = 0.0f;
    pid->p_term     = 0.0f;
    pid->i_term     = pid->ki * pid->integral;
    pid->d_term     = 0.0f;
    pid->first_run  = 1U;         /* 让下一拍微分重新起步 */
}
