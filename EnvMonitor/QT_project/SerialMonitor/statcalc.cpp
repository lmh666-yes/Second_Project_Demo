/* ============================================================================
 *  statcalc.cpp —— 【本文件由用户自己实现】
 *
 *  这个文件是留给你自己的实现起点，不是参考实现：
 *  滑动窗口的结构体字段与全部函数签名都在 statcalc.h 里，
 *  本文件只保留每个函数的空桩，以及一行"这一步要做什么"的提示。
 *  实现清单 = statcalc.h 里的全部声明，逐条对照补完即可。
 *
 *  学习路线：标准库速查 → 项目引导 → Qt
 *  对应阶段：阶段 3/4（统计量计算与滑动窗口；
 *           窗口长度 kWindow 是整份工程的统一口径，界面上所有均值/极差都依赖它）
 *
 *  参考实现备份（自己写完再对照，不要照抄一遍就交）：
 *      D:\GitHub_Project\_参考实现备份\20261012_195704\Qt\
 *
 *  编码：UTF-8 无 BOM、LF。
 * ==========================================================================*/

/* 下面是"桩函数参数暂时用不到"的告警抑制：等你把函数实现了，把这三行删掉即可。 */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "statcalc.h"
#include <cmath>

namespace StatCalc {

/* 窗口长度：头文件里是 `extern const int kWindow;`，这里给出唯一定义 */
const int kWindow = 50;

/* TODO(阶段3/4)：取序列末尾 window 个样本（window<=0 时用 kWindow，超过实际长度就全给） */
QVector<double> tailValues(const QVector<double> &values, int window)
{
    return {};
}

/* TODO(阶段3/4)：对一段数值算 count/avg/min/max/range/std 填进 WindowStat（样本不足按约定处理） */
StatCalc::WindowStat compute(const QVector<double> &values, int window)
{
    return {};
}

/* TODO(阶段3/4)：同上，但输入是 QCustomPlot 用的 QPointF 数组，取其中前 count 个点的 y 值 */
StatCalc::WindowStat compute(const QPointF *points, int count, int window)
{
    return {};
}

}   /* namespace StatCalc */
