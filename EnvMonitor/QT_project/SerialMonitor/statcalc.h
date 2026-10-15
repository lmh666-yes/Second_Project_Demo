#ifndef STATCALC_H
#define STATCALC_H

/* ============================================================================
 *  statcalc.h —— 滑动窗口统计（上位机右侧统计区用）
 *  对最近 N 个采样点给出平均值 / 最大值 / 最小值。平均值剔除 1 个最大值与
 *  1 个最小值，只各剔一个；最小值或最大值出现多个相同值时，其余相同的值照常
 *  参与平均。N 默认 50，出处：上位机界面需求（最近 50 次数据的平均值统计）。
 *  纯函数，不依赖 Qt 界面，可被 --selftest 直接调用。
 * ==========================================================================*/

#include <QPointF>
#include <QVector>

namespace StatCalc {

/* 滑动窗口统计结果 */
struct WindowStat {
    int    n            = 0;      /* 窗口内的有效点数，最多 50 个（NaN 不计入） */
    int    trimmedHigh  = 0;      /* 从平均值里剔除的最大值个数，0 或 1 */
    int    trimmedLow   = 0;      /* 从平均值里剔除的最小值个数，0 或 1 */
    double min          = 0.0;    /* 窗口内最小值，与剔除无关 */
    double max          = 0.0;    /* 窗口内最大值，与剔除无关 */
    double avg          = 0.0;    /* 平均值；未剔除时是全部点的算术平均 */

    bool hasValue() const { return n > 0; }
    /* 平均值是否为剔除口径：点数不少于 3 时剔除 1 个最大值与 1 个最小值 */
    bool avgTrimmed() const { return trimmedHigh > 0 && trimmedLow > 0; }
};

/* 窗口长度：最近 50 个采样点 */
extern const int kWindow;

/* 取序列末尾最多 window 个值；NaN 视为无效点直接跳过（湿度无效帧）。
 * window <= 0 时按 kWindow 处理。 */
QVector<double> tailValues(const QVector<double> &values, int window = 0);

/* 计算统计量：点数不足 3 时不剔除，平均值取全部点的算术平均 */
WindowStat compute(const QVector<double> &values, int window = 0);

/* 从内存里已缓存的数据点中截取尾部窗口并统计。
 * 界面右侧统计区按当前采样点直接调用这个重载，不必先复制一份数值数组。 */
WindowStat compute(const QPointF *points, int count, int window = 0);

}   /* namespace StatCalc */

#endif  /* STATCALC_H */
