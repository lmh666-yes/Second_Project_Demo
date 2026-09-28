#ifndef __FWLIB_KALMAN_H
#define __FWLIB_KALMAN_H

#include "stm32f4xx.h"

/* ================================================================
 *  kalman.h —— 【算法】一维卡尔曼滤波（给传感器做"去抖 + 跟得上"）
 * ================================================================
 *  设计定位 : 一阶低通(滤波器)会"又稳又慢"，中值滤波不认趋势——
 *             卡尔曼的好处是**自己算权**：数据稳的时候压低增益使劲滤，
 *             数据真变了的时候抬高增益马上跟上去，鱼和熊掌都要。
 *             面向【智能光电检测仪】的 ADC 采样，也适合温度/距离/电压。
 *  依赖     : 无（纯运算，不碰外设）
 *  标准库关键词 : 无——只用 float 加减乘除
 *
 *  【它和 sys_filter.h 里那几个的区别】
 *      FILTER_MovingAvg   : 稳，但阶跃响应慢，且要先存一窗数据
 *      FILTER_Lpf         : 一行搞定，但 alpha 固定 —— 要么跟得快要么滤得狠
 *      KALMAN_Update      : 同样一行，但 alpha(增益 K) 是**每次自动算**的
 *                          → 平滑时比 Lpf 更狠，跳变时比 Lpf 更快
 *
 *  【怎么调参（记住这一句就够了）】
 *      Q 调大 → 更信测量 → 跟得快、曲线毛
 *      R 调大 → 更信模型 → 更平滑、反应慢
 *      不想算这两货？用 KALMAN_AutoTune()，按"手感"说需求。
 *
 *  【使用方式】
 *      static Kalman_t kf;
 *
 *      KALMAN_Init(&kf, 0.01f, 4.0f);          // 或 KALMAN_AutoTune(&kf, 1.0f, 8.0f);
 *      for (;;) {
 *          float z = (float)SYS_ADC_Read();    // 传感器原始值
 *          float y = KALMAN_Update(&kf, z);    // 滤波后的值，直接用
 *          printf("raw=%.1f  kalman=%.1f  K=%.3f\r\n", z, y, KALMAN_GetGain(&kf));
 *      }
 *
 *  【调参小抄（看 K 值判断）】
 *      K 稳定在 0.0x  → 滤得很狠，跳变时跟不上，把 Q 调大或 R 调小
 *      K 稳定在 0.5+  → 基本等于没滤，把 R 调大或 Q 调小
 *      理想状态       → 平稳时 K 慢慢降到 0.1 附近，真跳变时瞬间冲到 0.8+
 *
 *  移植指引 : 纯算法，换芯片直接抄；
 *             要二维/加速度+位置融合那套，得扩成矩阵版，本文件不做。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换场景只改这里）
 * ================================================================ */
#define KALMAN_DEFAULT_Q        0.01f   /* 默认过程噪声：目标不动时给个小值 */
#define KALMAN_DEFAULT_R        4.00f   /* 默认测量噪声：按传感器抖动的平方估 */
#define KALMAN_INIT_P           1.00f   /* 初始估计误差协方差 */
#define KALMAN_MIN_P            0.000001f   /* 协方差下限，防止算到 0 除不开 */

/* 返回码（全库统一：0 成功） */
#define KALMAN_OK               0U      /* 成功 */
#define KALMAN_ERR_PARAM        1U      /* 空指针 / 参数非法 */


/* ================================================================
 *                        区块 2：基础功能
 * ================================================================ */

/* 滤波器对象。声明成 static 或全局都行，**别放栈上反复重建** */
typedef struct {
    float   x;          /* 状态估计值 —— 当前最优估计（对外要的就是它） */
    float   p;          /* 估计误差协方差 —— 内部量，越小越"确信" */
    float   q;          /* 过程噪声协方差 —— 调大 = 更信测量 = 跟得快、更毛 */
    float   r;          /* 测量噪声协方差 —— 调大 = 更信模型 = 更平滑、更慢 */
    float   k;          /* 上次算出的卡尔曼增益（0~1）—— 调参时盯这个 */
    float   resid;      /* 上次残差 = 测量值 - 估计值，突变检测可复用 */
    uint8_t inited;     /* 1 = 已经吃过第一个测量值 */
} Kalman_t;

/* 【初始化】上电调一次
 * 参数 : kf —— 滤波器对象；q/r —— 见文件头调参说明
 * 返回 : KALMAN_OK / KALMAN_ERR_PARAM
 * 示例 : KALMAN_Init(&kf, 0.01f, 4.0f); */
uint8_t KALMAN_Init(Kalman_t *kf, float q, float r);

/* 【喂一个测量值，拿一个滤波值】★核心，主循环里每次采样调一次
 * 参数 : kf —— 滤波器对象；z —— 本次传感器读数
 * 返回 : 滤波后的估计值
 * 说明 : 第一次调用会把 z 直接当初始估计（避免从 0 慢慢往上爬）；
 *        之后每次自动完成"预测 → 算增益 → 修正"三步。
 * 示例 : float y = KALMAN_Update(&kf, raw); */
float KALMAN_Update(Kalman_t *kf, float z);

/* 【重新开始】清掉历史，下一个测量值当新起点
 * 示例 : KALMAN_Reset(&kf, 0.0f);   // 重新标定后调用 */
void KALMAN_Reset(Kalman_t *kf, float x0);


/* ================================================================
 *                        区块 3：扩展功能
 * ================================================================ */

/* 【取当前估计值】不想每次都 Update 的时候用 */
float KALMAN_GetValue(const Kalman_t *kf);

/* 【取上次的卡尔曼增益 K】调试神器，见文件头"调参小抄" */
float KALMAN_GetGain(const Kalman_t *kf);

/* 【取上次残差 z-x】正负表示测量偏哪边；突变检测/丢点判定可用 */
float KALMAN_GetResid(const Kalman_t *kf);

/* 【改 Q/R】在线调参（比如"静止时滤狠点、运动时跟快点"）
 * 返回 : KALMAN_OK / KALMAN_ERR_PARAM */
uint8_t KALMAN_SetQR(Kalman_t *kf, float q, float r);

/* 【按"手感"自动换算 Q/R】不想算协方差就用这个
 * 参数 : jump   —— 目标真变了多少以内要跟得上（按传感器单位，如 1.0℃）
 *        smooth —— 希望把多大的抖动滤掉（如 ±8.0℃ 的野值）
 * 换算 : q = jump^2，r = smooth^2
 * 返回 : KALMAN_OK / KALMAN_ERR_PARAM
 * 示例 : KALMAN_AutoTune(&kf, 1.0f, 8.0f);  // 1℃的变化要跟上，8℃的乱跳要滤掉 */
uint8_t KALMAN_AutoTune(Kalman_t *kf, float jump, float smooth);

/* 【整数版更新】省掉调用方来回转 float，结果四舍五入
 * 返回 : 滤波后的整数估计值
 * 示例 : int32_t y = KALMAN_UpdateInt(&kf, SYS_ADC_Read()); */
int32_t KALMAN_UpdateInt(Kalman_t *kf, int32_t z);

#endif /* __FWLIB_KALMAN_H */
