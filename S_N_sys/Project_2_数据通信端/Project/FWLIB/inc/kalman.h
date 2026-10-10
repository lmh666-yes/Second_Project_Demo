#ifndef __FWLIB_KALMAN_H
#define __FWLIB_KALMAN_H

#include "stm32f4xx.h"

/* kalman.h : 一维卡尔曼滤波，用于 ADC 采样去抖，也适用于温度、距离、电压
 * 纯运算，不访问外设，只用 float 加减乘除
 * Q 调大更信任测量，跟得快、曲线毛；R 调大更信任模型，更平滑、响应慢
 * 与固定 alpha 的一阶低通相比，增益 K 每次由协方差自动算出
 * 调参依据：平稳时 K 趋近 0.1，跳变时上升到 0.8 以上；K 长期接近 0 则响应偏慢，接近 1 则滤波无效 */


/* 定义与宏定义区 */
#define KALMAN_DEFAULT_Q        0.01f   /* 默认过程噪声：目标不动时给个小值 */
#define KALMAN_DEFAULT_R        4.00f   /* 默认测量噪声：按传感器抖动的平方估 */
#define KALMAN_INIT_P           1.00f   /* 初始估计误差协方差 */
#define KALMAN_MIN_P            0.000001f   /* 协方差下限，防止算到 0 除不开 */

/* 返回码（全库统一：0 成功） */
#define KALMAN_OK               0U      /* 成功 */
#define KALMAN_ERR_PARAM        1U      /* 空指针 / 参数非法 */


/* 基础功能 */

/* 滤波器对象：静态或全局声明，不要在栈上反复重建 */
typedef struct {
    float   x;          /* 状态估计值：当前最优估计，即对外输出量 */
    float   p;          /* 估计误差协方差：内部量，越小表示越确信 */
    float   q;          /* 过程噪声协方差：调大更信任测量，跟得快、曲线毛 */
    float   r;          /* 测量噪声协方差：调大更信任模型，更平滑、响应慢 */
    float   k;          /* 上次算出的卡尔曼增益，范围 0~1 */
    float   resid;      /* 上次残差 = 测量值 - 估计值，可用于突变检测 */
    uint8_t inited;     /* 1 = 已接收第一个测量值 */
} Kalman_t;

/* 初始化，上电调用一次；参数 kf 为滤波器对象，q/r 为过程与测量噪声协方差
 * 返回 : KALMAN_OK / KALMAN_ERR_PARAM
 * 说明 : q/r 为 NaN 时同样返回 KALMAN_ERR_PARAM；q <= 0.0f 这类判断拦不住 NaN（与 NaN 比较恒为假）
 *        k 一旦算成 NaN，钳位无法恢复，x 会永久保持 NaN */
uint8_t KALMAN_Init(Kalman_t *kf, float q, float r);

/* 输入一个测量值，返回滤波后的估计值；主循环每次采样调用一次
 * 参数 : z 为本次传感器读数
 * 说明 : 第一次调用直接把 z 作为初始估计，之后完成预测、算增益、修正三步
 * 无效值 : z 为 NaN/±Inf 时整拍丢弃，原样返回上次估计值，状态与残差不变；递推滤波有一拍坏值即永久失效，坏值当 0 会永久污染状态 */
float KALMAN_Update(Kalman_t *kf, float z);

/* 清掉历史，下一个测量值当新起点，重新标定后调用
 * 参数 : x0 为新起点估计值；传入 NaN/±Inf 时按 0.0 处理，避免滤波器永久失效 */
void KALMAN_Reset(Kalman_t *kf, float x0);


/* 扩展功能 */

/* 取当前估计值，不更新状态 */
float KALMAN_GetValue(const Kalman_t *kf);

/* 取上次卡尔曼增益 K，范围 0~1，调参时观察此值 */
float KALMAN_GetGain(const Kalman_t *kf);

/* 取上次残差 z-x，正负表示测量偏哪边，可用于突变检测与丢点判定 */
float KALMAN_GetResid(const Kalman_t *kf);

/* 在线修改 Q/R，可用于静止时滤重、运动时跟快的场景
 * 返回 : KALMAN_OK / KALMAN_ERR_PARAM */
uint8_t KALMAN_SetQR(Kalman_t *kf, float q, float r);

/* 按目标跳变与抖动幅度换算 Q/R，不想手算协方差时使用
 * 参数 : jump 为需要跟上的目标变化量，单位同传感器读数，如 1.0 摄氏度
 *        smooth 为需要滤掉的抖动幅度，如 ±8.0 摄氏度
 * 换算 : q = jump^2，r = smooth^2；返回 KALMAN_OK / KALMAN_ERR_PARAM */
uint8_t KALMAN_AutoTune(Kalman_t *kf, float jump, float smooth);

/* 整数版更新，结果四舍五入，省去调用方转换 float
 * 返回 : 滤波后的整数估计值 */
int32_t KALMAN_UpdateInt(Kalman_t *kf, int32_t z);

#endif /* __FWLIB_KALMAN_H */
