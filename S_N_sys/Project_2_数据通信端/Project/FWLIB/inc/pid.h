#ifndef __FWLIB_PID_H
#define __FWLIB_PID_H

#include "stm32f4xx.h"

/* ================================================================
 *  pid.h —— 【算法】PID 控制器（位置式 / 增量式 + 抗积分饱和）  头文件
 * ================================================================
 *  设计定位 : 纯算法模块，不碰任何硬件——把"测量值 + 目标值"喂进来，
 *             它吐回一个"控制量"，你拿去写 PWM / DAC / 阀门都行
 *  依赖     : 无（不依赖 gpio_core / sys_tim，单片可用）
 *  说明     : 用 float 实现（F407 带 FPU，硬件浮点，一次 Update 约 1µs）
 *
 *  【为什么用结构体传参】
 *      PID 要记住"上次误差、积分累计"等状态；用结构体把状态与参数打包，
 *      就能同时跑多个闭环（例如两轮小车的左轮、右轮各一个 PID），
 *      互不干扰——这是本模块比"全局变量式 PID 函数"实用的地方。
 *
 *  【两种模式的取舍（面试常问）】
 *      位置式（POSITION）：输出 = 绝对控制量，直接赋给 PWM 占空比。
 *                          特点是"有积分、稳态无静差"，但积分饱和需要抗；
 *      增量式（INCREMENT）：输出 = 控制量的"增量"，靠累加得到绝对量。
 *                          天然抗积分饱和、切换无扰动，适合带执行器记忆的场景。
 *      选哪个：舵机/阀门/温度 → 位置式；直流电机速度环 → 增量式也常见。
 *
 *  【抗积分饱和的三种常用手段，本模块都给了】
 *      ① 积分分离：误差大时先不积分（防"起步猛冲"）—— PID_SeparateThreshold
 *      ② 积分限幅：把积分累计夹在范围内 —— PID_SetIntegralLimit
 *      ③ 微分先行：微分只对测量值求导，设定值突变不产生"微分尖峰"
 *                                   —— PID_SetDerivOnMeasurement
 *
 *  使用方式（直流电机速度环，10ms 一次）:
 *      static Pid_t s_pid;
 *      static uint32_t s_tick;
 *
 *      PID_Init(&s_pid, 20.0f, 5.0f, 0.5f);        // ① 给一组初值
 *      PID_SetOutputLimit(&s_pid, -1000.0f, 1000.0f);   // PWM 占空比 ‰
 *      PID_SetIntegralLimit(&s_pid, -500.0f, 500.0f);   // 抗饱和
 *
 *      for (;;) {
 *          if (SYS_TICK_Timeout(s_tick, 10)) {            // ② 固定周期 10ms
 *              float dt = SYS_TICK_Elapsed(s_tick) / 1000.0f;
 *              s_tick = SYS_TICK_GetTick();
 *              float out = PID_Update(&s_pid, 100.0f,                // 目标 100
 *                                     (float)Encoder_GetSpeed());    // 实测
 *              SYS_TIM_PwmSetDuty(SYS_TIM_1, 1, duty_from(out));     // ③ 去执行
 *          }
 *      }
 *
 *  调参提示（工程经验，先记着少走弯路）:
 *      · 先把 Ki、Kd 置 0，只加 Kp 到"开始小幅振荡"，再回退到 60~70%；
 *      · 再加 Kd 压制振荡（注意 D 会放大噪声，传感器噪声大时先滤波）；
 *      · 最后加 Ki 消除稳态误差（Ki 要小，大了会"来回冲"）；
 *      · 采样周期必须固定！dt 抖动会让 Ki/Kd 的效果完全跑偏。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* PID 模式 */
#define PID_MODE_POSITION   0       /* 位置式：输出就是绝对控制量 */
#define PID_MODE_INCREMENT  1       /* 增量式：输出是本次的增量 */

/* 默认输出限幅（几乎总要改；先给个保守值防止上电瞬间满输出） */
#define PID_DEFAULT_OUT_MIN   (-100.0f)
#define PID_DEFAULT_OUT_MAX   ( 100.0f)

/* 浮点比较用的极小值（防除 0） */
#define PID_EPSILON          1e-6f


/* ================================================================
 *                    区块 2：基础功能（结构与初始化）
 * ================================================================ */
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
    float dead_zone;            /* 误差死区：|误差| < 死区时视为 0（防执行器抖动） */
    float separate_threshold;   /* 积分分离阈值：|误差| > 该值时暂停积分；0 = 不分离 */
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

/* 初始化：给系数并清空所有状态（内部输出限幅给 PID_DEFAULT_OUT_*）
 * 参数 : kp/ki/kd —— 三个系数（先给 0 也行，后面用 PID_SetTunings 再调）
 * 示例 : PID_Init(&s_pid, 20.0f, 5.0f, 0.5f); */
void PID_Init(Pid_t *pid, float kp, float ki, float kd);

/* 复位状态（清积分/历史误差/输出），系数保留
 * 用途 : 切换到手动模式再切回自动时用，避免残留积分造成"突跳" */
void PID_Reset(Pid_t *pid);

/* 运行中改系数（在线调参用；只改系数，不动积分状态） */
void PID_SetTunings(Pid_t *pid, float kp, float ki, float kd);

/* 设置输出限幅（**必须设**，否则上电瞬间可能满输出）
 * 示例 : PID_SetOutputLimit(&s_pid, -1000.0f, 1000.0f);   // ±100% 占空比‰ */
void PID_SetOutputLimit(Pid_t *pid, float min, float max);

/* 设置积分限幅（抗积分饱和的核心手段）
 * 经验 : 取输出限幅的 50%~100%；设得太小会削弱消静差能力 */
void PID_SetIntegralLimit(Pid_t *pid, float min, float max);

/* 设置模式：PID_MODE_POSITION / PID_MODE_INCREMENT */
void PID_SetMode(Pid_t *pid, uint8_t mode);

/* 误差死区：|误差| 小于该值时按 0 处理（适合带死区的电机/阀门） */
void PID_SetDeadZone(Pid_t *pid, float dead_zone);

/* 积分分离阈值：|误差| 大于该值时暂停积分（0 = 关闭该功能）
 * 用途 : 起步/大偏差时不让积分疯涨，显著改善"超调" */
void PID_SetSeparateThreshold(Pid_t *pid, float threshold);

/* 微分先行开关：1 = 微分对测量值求导（推荐，抗设定值突变冲击） */
void PID_SetDerivOnMeasurement(Pid_t *pid, uint8_t enable);


/* ================================================================
 *                    区块 3：核心计算与辅助
 * ================================================================ */
/* 执行一次 PID 计算
 * 参数 : pid      —— PID 实例
 *        setpoint —— 目标值（期望）
 *        measure  —— 测量值（实际）
 *        dt       —— 距上次调用的时间（秒）；传 0 时按 0.01s(10ms) 处理
 * 返回 : 控制量（已按 out_min/out_max 限幅）
 * 说明 : **dt 必须尽量稳定**（用固定周期调用），否则 Ki/Kd 的效果会跑偏
 * 示例 : float out = PID_Update(&s_pid, target, actual, 0.01f);   // 10ms 一次 */
float PID_Update(Pid_t *pid, float setpoint, float measure, float dt);

/* 读回上一次的三个分量（在线观察哪个分量在"使劲"，调参神器）
 * 示例 : printf("P=%.2f I=%.2f D=%.2f\r\n", ...); */
void PID_GetTerms(const Pid_t *pid, float *p_term, float *i_term, float *d_term);

/* 读回上一次输出 */
float PID_GetOutput(const Pid_t *pid);

/* 手动预置输出（无扰动切换用）：把积分清零并把输出设为 value
 * 用途 : 从"手动开环"切到"自动闭环"时，先让 PID 输出 = 当前手动值，
 *        这样切换瞬间不会"抽一下" */
void PID_PresetOutput(Pid_t *pid, float value);

#endif /* __FWLIB_PID_H */
