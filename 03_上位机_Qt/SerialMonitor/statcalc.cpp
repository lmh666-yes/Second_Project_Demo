// ============================================================================
// statcalc.cpp —— 滑动窗口统计
//
// 口径见 statcalc.h：窗口 kWindow=50，剔除 1 个最大值 + 1 个最小值后再算平均
// （点数 < 3 时不剔除），NaN 视为无效点跳过。
//
// 编码：UTF-8 无 BOM、LF。
// ============================================================================

#include "statcalc.h"
#include <cmath>

/*====================函数=====================*/

namespace StatCalc {

// 窗口长度：头文件里是 `extern const int kWindow;`，这里给出唯一定义
const int kWindow = 50;

// 取序列末尾 window 个样本
// · window <= 0 用 kWindow；window 超过实际长度就全给
// · NaN 直接跳过（湿度无效帧）
// · 返回顺序与原序列一致
QVector<double> tailValues(const QVector<double> &values, int window)
{
    const int w = (window <= 0) ? kWindow : window;

    QVector<double> out;

    if (w == 0) {
        return out;
    }

    out.reserve(qMin(w, values.size()));

    // 从尾部往前扫，凑够 w 个有效点为止
    for (int i = values.size() - 1; (i >= 0) && (out.size() < w); --i) {
        const double v = values.at(i);

        if (std::isnan(v)) {
            continue;               // 无效帧不占用窗口
        }

        out.append(v);
    }

    // 反转成原序列顺序（从旧到新）
    for (int i = 0, j = out.size() - 1; i < j; ++i, --j) {
        const double t = out[i];
        out[i] = out[j];
        out[j] = t;
    }

    return out;
}

// 算 WindowStat：n / min / max / avg / trimmedHigh / trimmedLow
// · n == 0 时全部给 0
// · n >= 3：先从平均值里剔除 1 个最大值和 1 个最小值，trimmedHigh/Low 置 1
// · n <  3：不剔除，平均值就是算术平均
// · min/max 永远是未剔除的极值
StatCalc::WindowStat compute(const QVector<double> &values, int window)
{
    WindowStat st;

    const QVector<double> v = tailValues(values, window);

    st.n = v.size();

    if (st.n == 0) {
        return st;                  // 全部保持 0
    }

    double sum = 0.0;
    double mn  = v.at(0);
    double mx  = v.at(0);

    for (int i = 0; i < v.size(); ++i) {
        const double x = v.at(i);

        sum += x;

        if (x < mn) {
            mn = x;
        }
        if (x > mx) {
            mx = x;
        }
    }

    st.min = mn;
    st.max = mx;

    if (st.n >= 3) {
        // 只各剔一个：有多个相同极值时，剩下的照常参与平均
        st.avg         = (sum - mn - mx) / static_cast<double>(st.n - 2);
        st.trimmedHigh = 1;
        st.trimmedLow  = 1;
    } else {
        st.avg = sum / static_cast<double>(st.n);
    }

    return st;
}

// 输入换成 QCustomPlot 的 QPointF 数组：取前 count 个点的 y 值，再复用上面的 compute
StatCalc::WindowStat compute(const QPointF *points, int count, int window)
{
    QVector<double> v;

    if ((points != nullptr) && (count > 0)) {
        v.reserve(count);

        for (int i = 0; i < count; ++i) {
            v.append(points[i].y());
        }
    }

    return compute(v, window);
}

}   // namespace StatCalc
/*=================================================*/
