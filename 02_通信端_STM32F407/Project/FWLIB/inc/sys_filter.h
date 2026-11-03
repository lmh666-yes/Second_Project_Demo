#ifndef __FWLIB_SYS_FILTER_H
#define __FWLIB_SYS_FILTER_H

#include "stm32f4xx.h"

/* sys_filter.h 数字滤波工具集
 * 无状态函数加一个低通对象、一个滑动平均对象，不依赖硬件。
 *
 * 限幅滤波   FILTER_Limit       抑制偶发大跳变，即尖刺或野值
 * 中值滤波   FILTER_Median      抑制孤立野值，不损失阶跃响应
 * 滑动平均   FILTER_MovingAvg   平滑随机噪声，会带来滞后
 * 增量滑动   FILTER_Avg*        滑动平均的环形缓冲版，单步开销与窗口长度无关
 * 一阶低通   FILTER_Lpf*        计算量最小，参数直观
 * 去极值平均 FILTER_TrimmedMean 去掉两端极值后再平均
 *
 * 常用顺序：先限幅防野值，再低通或平均降噪声，最后进算法。
 */

/* 区块 1：定义与宏定义区 */
/* 采样缓冲区最大长度；滑动平均、中值、去极值平均的数组按这个长度定义 */
#define FILTER_BUF_MAX      32


/* 区块 2：无状态滤波，一次调用出结果 */
/* 限幅滤波：本次值与上次值之差超过 max_step 时保持上次值
 * 参数：prev 上次输出；now 本次采样；max_step 允许的最大变化量
 * 返回：滤波后的值 */
uint16_t FILTER_Limit(uint16_t prev, uint16_t now, uint16_t max_step);

/* 中值滤波：对 buf[0..n-1] 排序后取中间值，会修改 buf 内容
 * 参数：buf 数据缓冲；n 数据个数，建议取奇数，n 为 0 时返回 0
 * 返回：中位数 */
uint16_t FILTER_Median(uint16_t *buf, uint16_t n);

/* 滑动平均：求 buf[0..n-1] 的算术平均，不修改 buf
 * 参数：n 越大越平滑，滞后也越大
 * 返回：平均值，四舍五入取整 */
uint16_t FILTER_MovingAvg(const uint16_t *buf, uint16_t n);

/* 去极值平均：去掉最大的 trim 个和最小的 trim 个，对剩余求平均
 * 参数：buf 数据缓冲；n 数据个数，必须大于 2*trim；trim 两端各去掉几个
 * 返回：平均值，参数非法时退回普通平均 */
uint16_t FILTER_TrimmedMean(uint16_t *buf, uint16_t n, uint16_t trim);

/* 求 buf 的最大值与最小值，不修改 buf */
void FILTER_MinMax(const uint16_t *buf, uint16_t n, uint16_t *min_out, uint16_t *max_out);


/* 区块 3：一阶低通，IIR 有状态滤波
 * 递推式 y[n] = α·x[n] + (1-α)·y[n-1]
 * α 越大越信任新数据，响应快、平滑弱；α 越小越信任历史，平滑强、滞后大。
 * FILTER_LpfInit 按截止频率与采样率计算精确 α = 1 - exp(-2π·fc/fs)。
 * 教材近似式 α ≈ 2π·fc/fs 只在 fc << fs 时成立；fs = 1kHz、fc = 50Hz 时
 * 近似式给出 0.314，精确值约 0.270，实际截止频率偏高约 16%。 */
typedef struct {
    float  alpha;      /* 平滑系数 0~1，越大越灵敏 */
    float  y;          /* 上次输出 */
    uint8_t started;   /* 首次调用标志，第一拍直接取输入，避免从 0 爬升 */
} FilterLpf_t;

/* 初始化低通：按截止频率与采样率计算 α
 * 参数：f 滤波器对象
 *       fc_hz 截止频率 Hz，滤 50Hz 工频时取 10~20Hz
 *       fs_hz 采样率 Hz，即调用 FILTER_LpfUpdate 的频率
 * 参数为 NaN、负数或 0 时退回默认 α = 0.2 */
void  FILTER_LpfInit(FilterLpf_t *f, float fc_hz, float fs_hz);

/* 直接设置 α，取值范围 0.0~1.0
 * 0.1 平滑强，0.3 折中，0.5 以上滤波效果很弱
 * NaN 按 0.0 处理：否则与 0~1 比较均为假，α 原样为 NaN，滤波器永久失效 */
void  FILTER_LpfSetAlpha(FilterLpf_t *f, float alpha);

/* 喂一个新采样，返回滤波结果
 * NaN 和 ±Inf 整拍丢弃：返回上次输出且状态不变。
 * 原因是递推式 y = α·x + (1-α)·y 有记忆，任一拍为 NaN 或 Inf 后结果
 * 会永久保持 NaN 或 Inf，除非重新调用 LpfInit 或 LpfReset。
 * 需要区分坏值的调用方自行判断输入 */
float FILTER_LpfUpdate(FilterLpf_t *f, float x);

/* 复位，清除历史，下次调用重新起步 */
void  FILTER_LpfReset(FilterLpf_t *f);


/* 区块 4：滑动平均对象，环形缓冲，单步开销与窗口长度无关
 * 无状态的 FILTER_MovingAvg 每拍都要重新累加整个窗口，窗口内容与覆盖位置
 * 由调用方维护；本对象内部维护 running sum，每拍只减去被挤出的最老样本、
 * 加上新样本，开销与窗口长度无关，与一阶低通等递推算法便于横向比较。
 *
 * 预热期：样本数未达 size 时返回已有样本的平均值，不把空缺当作 0，
 * 因此开机第一拍就有合理输出。 */
/* 不指定窗口长度时用的默认值，取 2 的幂便于除法 */
#define FILTER_AVG_DEFAULT_SIZE 8U

typedef struct {
    uint16_t buf[FILTER_BUF_MAX];   /* 环形样本缓冲 */
    uint32_t sum;                   /* 窗口内样本之和，增量维护 */
    uint16_t size;                  /* 窗口长度 1 ~ FILTER_BUF_MAX */
    uint16_t count;                 /* 已填样本数，预热期小于 size */
    uint16_t idx;                   /* 下一个要写的下标 */
} FilterAvg_t;

/* 初始化：size 传 0 用 FILTER_AVG_DEFAULT_SIZE，大于 FILTER_BUF_MAX 时截到上限 */
void     FILTER_AvgInit(FilterAvg_t *a, uint16_t size);

/* 喂一个新样本，返回当前窗口的算术平均值，四舍五入取整
 * 单步开销 O(1)；窗口未满时返回已有样本的平均值 */
uint16_t FILTER_AvgUpdate(FilterAvg_t *a, uint16_t x);

/* 读取当前平均值，不喂新样本；一次都没喂过时返回 0 */
uint16_t FILTER_AvgGet(const FilterAvg_t *a);

/* 复位：清空历史，sum、count、idx 归零，窗口长度 size 不变 */
void     FILTER_AvgReset(FilterAvg_t *a);

#endif /* __FWLIB_SYS_FILTER_H */
