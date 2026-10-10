#ifndef __FWLIB_PID_H
#define __FWLIB_PID_H

#include "stm32f4xx.h"

/* pid.h : PID 控制器（位置式 / 增量式 + 抗积分饱和）
 * 纯算法模块，不依赖硬件，用 float 实现（F407 带 FPU，一次 Update 约 1us）
 *
 * 位置式(POSITION)：输出为绝对控制量，直接写 PWM 占空比，有积分、稳态无静差，需抗积分饱和
 * 增量式(INCREMENT)：输出为控制量增量，靠累加得到绝对量，天然抗积分饱和、切换无扰动
 *
 * 抗积分饱和手段：积分分离 PID_SetSeparateThreshold，积分限幅 PID_SetIntegralLimit，
 * 微分先行 PID_SetDerivOnMeasurement
 *
 * 调参顺序：Ki、Kd 置 0，Kp 加到小幅振荡后回退到 60%~70%；再加 Kd 压制振荡；
 * 最后加 Ki 消稳态误差；采样周期必须固定，dt 抖动会使 Ki/Kd 失效 */

/* 定义与宏 */
/* PID 模式 */
#define PID_MODE_POSITION   0       /* 位置式：输出就是绝对控制量 */
#define PID_MODE_INCREMENT  1       /* 增量式：输出是本次的增量 */

/* 默认输出限幅，上电瞬间防止满输出 */
#define PID_DEFAULT_OUT_MIN   (-100.0f)
#define PID_DEFAULT_OUT_MAX   ( 100.0f)

/* 浮点比较用的极小值（防除 0） */
#define PID_EPSILON          1e-6f


/* 结构与初始化 */
typedef struct {
    /* ---- 三个系数 ---- */
    float kp;                   /* 比例系数 */
    float ki;                   /* 积分系数 */
    float kd;                   /* 微分系数 */

    /* ---- 限幅 ---- */
    float out_min;              /* 输出下限 */
    float out_max;              /* 输出上限 */
    float integral_min;         /* 积分累计下限（抗积分饱和） */
    float integral_max;         /* 积分累计上限 */

    /* ---- 高级选项 ---- */
    float dead_zone;            /* 误差死区：|误差| 小于该值时按 0 处理，防执行器抖动 */
    float separate_threshold;   /* 积分分离阈值：|误差| 大于该值时暂停积分；0 = 不分离 */
    uint8_t deriv_on_meas;      /* 1 = 微分先行（对测量值求导，抗设定值突变） */
    uint8_t mode;               /* PID_MODE_POSITION / PID_MODE_INCREMENT */

    /* ---- 运行时状态（外部不要手动改） ---- */
    float integral;             /* 积分累计 */
    float prev_error;           /* 上次误差 */
    float prev_meas;            /* 上次测量值（微分先行用） */
    float output;               /* 上次输出（增量式累加用） */
    float p_term;               /* 上次的 P 分量（观察/调参用） */
    float i_term;               /* 上次的 I 分量 */
    float d_term;               /* 上次的 D 分量 */
    uint8_t first_run;          /* 首次调用标志（避免第一拍微分突变） */
} Pid_t;

/* 初始化：设置系数并清空所有状态，输出限幅取 PID_DEFAULT_OUT_*
 * 参数 : kp/ki/kd 三个系数，可先给 0，后续用 PID_SetTunings 修改 */
void PID_Init(Pid_t *pid, float kp, float ki, float kd);

/* 复位状态（清积分、历史误差、输出），系数保留
 * 用途 : 手动模式切回自动模式时清除残留积分，避免输出突跳 */
void PID_Reset(Pid_t *pid);

/* 运行中改系数（在线调参用；只改系数，不动积分状态） */
void PID_SetTunings(Pid_t *pid, float kp, float ki, float kd);

/* 设置输出限幅，上电前必须设置，否则可能满输出 */
void PID_SetOutputLimit(Pid_t *pid, float min, float max);

/* 设置积分限幅
 * 经验 : 取输出限幅的 50%~100%，设得太小会削弱消静差能力 */
void PID_SetIntegralLimit(Pid_t *pid, float min, float max);

/* 设置模式：PID_MODE_POSITION / PID_MODE_INCREMENT */
void PID_SetMode(Pid_t *pid, uint8_t mode);

/* 误差死区：|误差| 小于该值时按 0 处理，适合带死区的电机或阀门 */
void PID_SetDeadZone(Pid_t *pid, float dead_zone);

/* 积分分离阈值：|误差| 大于该值时暂停积分，0 表示关闭该功能
 * 用途 : 起步或大偏差时抑制积分增长，减小超调 */
void PID_SetSeparateThreshold(Pid_t *pid, float threshold);

/* 微分先行开关：1 = 微分对测量值求导，抑制设定值突变带来的微分冲击 */
void PID_SetDerivOnMeasurement(Pid_t *pid, uint8_t enable);


/* 核心计算与辅助 */
/* 执行一次 PID 计算
 * 参数 : pid PID 实例；setpoint 目标值；measure 测量值；
 *        dt 距上次调用的时间，单位秒，传 0 时按 0.01s(10ms) 处理
 * 返回 : 控制量，已按 out_min/out_max 限幅
 * 说明 : dt 需用固定周期调用保持稳定，否则 Ki/Kd 的效果会偏差 */
float PID_Update(Pid_t *pid, float setpoint, float measure, float dt);

/* 读回上一次的 P、I、D 三个分量，用于在线观察与调参 */
void PID_GetTerms(const Pid_t *pid, float *p_term, float *i_term, float *d_term);

/* 读回上一次输出 */
float PID_GetOutput(const Pid_t *pid);

/* 手动预置输出：积分清零并把输出设为 value，用于无扰动切换
 * 用途 : 手动开环切到自动闭环前，先让 PID 输出等于当前手动值，切换瞬间输出不跳变 */
void PID_PresetOutput(Pid_t *pid, float value);

#endif /* __FWLIB_PID_H */
