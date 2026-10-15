#include "pid.h"
/* pid.c : PID 控制器实现
 *
 * 位置式: out = Kp·e + Ki·Σ(e·dt) + Kd·(e - e_prev)/dt
 * 增量式: Δout = Δout_prev + (p - p_prev) + i + (d - d_prev)
 * 增量式的历史量由 prev_error 与 integral 承担；仍跟踪 integral 供限幅与显示
 * 使用。三项附带死区、抗积分饱和、微分先行 */


/* 把值限制到 [lo, hi] */
static float pid_clamp(float v, float lo, float hi)
{
    if (lo > hi) { float t = lo; lo = hi; hi = t; }
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


/* 区块 2：初始化与配置 */
void PID_Init(Pid_t *pid, float kp, float ki, float kd)
{
    if (pid == 0) return;

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;

    /* 默认限幅取保守值：上电不会满输出 */
    pid->out_min        = PID_DEFAULT_OUT_MIN;
    pid->out_max        = PID_DEFAULT_OUT_MAX;
    pid->integral_min   = PID_DEFAULT_OUT_MIN;
    pid->integral_max   = PID_DEFAULT_OUT_MAX;

    pid->dead_zone          = 0.0f;
    pid->separate_threshold = 0.0f;      /* 0 = 不做积分分离 */
    pid->deriv_on_meas      = 1U;         /* 默认微分先行 */
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

    /* 输出限幅变小时积分限幅跟着收缩：避免积分继续增长但输出被限住 */
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
        /* 换模式时清状态：增量式的累加基准依赖上一拍结果 */
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


/* 区块 3：核心计算 */
float PID_Update(Pid_t *pid, float setpoint, float measure, float dt)
{
    float error;
    float error_used;
    float p;
    float i;
    float d;
    float out;

    if (pid == 0) return 0.0f;

    /* dt 传 0 或小于 PID_EPSILON 时按 10ms 处理：避免除零与积分异常累计 */
    if (dt < PID_EPSILON) dt = 0.01f;

    error = setpoint - measure;

    /* 1) 死区：误差绝对值小于死区时按 0 处理，抑制执行器在目标点附近抖动 */
    error_used = error;
    if (pid->dead_zone > 0.0f) {
        float ae = (error < 0.0f) ? -error : error;
        if (ae < pid->dead_zone) error_used = 0.0f;
    }

    /* 2) 比例项 */
    p = pid->kp * error_used;

    /* 3) 积分项：先累加再限幅，累加值本身也受限 */
    {
        float ae = (error < 0.0f) ? -error : error;

    /* 积分分离：误差大于阈值时暂停积分，减小起步超调 */
        if (pid->separate_threshold <= 0.0f || ae <= pid->separate_threshold) {
            pid->integral += error_used * dt;
            pid->integral = pid_clamp(pid->integral, pid->integral_min, pid->integral_max);
        }
    }
    i = pid->ki * pid->integral;

    /* 4) 微分项 */
    if (pid->first_run) {
        /* 第一拍没有历史数据，微分给 0，避免产生尖峰 */
        d = 0.0f;
        pid->first_run = 0U;
    } else if (pid->deriv_on_meas) {
        /* 微分先行：对测量值求导，设定值跳变时无冲击，等价于 d(误差)/dt = -d(测量值)/dt */
        d = -pid->kd * (measure - pid->prev_meas) / dt;
    } else {
        d = pid->kd * (error_used - pid->prev_error) / dt;
    }

    /* 5) 合成输出 */
    if (pid->mode == PID_MODE_INCREMENT) {
        /* 增量式：本次增量累加到上次输出上
         * Δout = (p - p_prev) + i + (d - d_prev)，与 Kp·Δe + Ki·e·dt + Kd·Δ²e/dt 等价
         * p_term/d_term 存在各实例内，多个 PID 并行互不影响 */
        out = pid->output
            + (p - pid->p_term)
            +  i
            + (d - pid->d_term);
    } else {
        /* 位置式 */
        out = p + i + d;
    }

    /* 6) 输出限幅 */
    out = pid_clamp(out, pid->out_min, pid->out_max);

    /* 7) 保存历史：用本拍结果更新 p_term/d_term，供下一拍做差 */
    pid->p_term     = p;
    pid->i_term     = i;
    pid->d_term     = d;
    pid->prev_error = error_used;
    pid->prev_meas  = measure;
    pid->output     = out;

    return out;
}


/* 区块 3：辅助接口 */
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

    /* 按 value / ki 反算积分值并限幅，实现输出预置的无扰动切换 */
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
    pid->first_run  = 1U;         /* 下一拍微分重新起算 */
}
