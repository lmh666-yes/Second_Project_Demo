#ifndef DASHBOARDCANVAS_H
#define DASHBOARDCANVAS_H

/* 可视化体检面板的画布：把各检查工具的结构化结果画成图表。
   纯 QPainter 自绘，零新增依赖（不引 QtCharts，保持"拷走就能跑"）。
   数据由 OverviewPage 调各工具 --json 取回后灌进来。

   绘制内容（自上而下）：
     1) 健康度卡片行  —— 每个检查器各一张，状态点 + 关键数字（没取到的显式标出来）
     2) 引脚占用热力图 —— 每块板一张 A~G × 0~F 网格（空闲/占用/冲突/声明未用四态），
        放不下就换排；板表里有几块就画几块（含 OTA 工程）
     3) Flash / RAM 占用条
     4) RTOS 任务表    —— 有任务时才画
     5) 进度趋势迷你折线 —— 待办 / 桩 的历史变化 */

#include <QColor>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <QWidget>

class DashboardCanvas : public QWidget {
    Q_OBJECT
public:
    explicit DashboardCanvas(QWidget *parent = nullptr);

    /* tools 的键：pin / rtos / enc / lora / protocol / firmware / progress
       （见 OverviewPage 的 TOOLS 表）。值是各工具 --json 解析出的 QVariantMap，
       缺键表示该工具本次没取到。 */
    void setTools(const QVariantMap &tools);

    /* 无数据时的中央提示语。 */
    void setNotice(const QString &text);

    /* 本次没取到数的工具键（pin/rtos/...）：这些卡片不能画成绿色"正常"，
       否则面板会替一个失败的工具报平安。 */
    void setFailed(const QStringList &keys);

    /* 「较上次」增量行（如「引脚冲突 ↓2　待办标记 ↑6」），画在健康卡片下方。 */
    void setDelta(const QStringList &lines);

    /* 上次绘制登记的可交互区域数（卡片 + 热力图格）。
       供 --shot-overview 断言"交互已就绪"，避免命中区悄悄变空。 */
    int hitCount() const { return m_hits.size(); }

    /* 上一轮被标成"未取到"的工具数（供 --shot-overview 的缺数断言）。 */
    int failedCount() const { return m_failedKeys.size(); }

    /* A2：只做悬停提示、不响应点击的命中区数（趋势折线的数据点）。
       供 --shot-overview 断言"趋势点悬停读数已登记"，免得哪天被改没了没人发现。 */
    int tipHitCount() const;

    /* B3：上一次绘制算出的"一句话结论"与"有问题的项数"（供 --shot-overview 断言：
       结论文案不能空；有问题时必须写严重/注意，没问题时必须写正常）。
       注意：这两个值在 paintEvent 里更新，读之前得先让它画过一次。 */
    QString conclusionText() const { return m_conclusionText; }
    int conclusionCount() const { return m_conclusionCount; }

    /* H12：红黄线阈值抽成一份口径（速查数据/rules/dashboard_thresholds.json）。
       这四个 static 供 --thresholds-probe 断言"配置真的在起作用"，不参与界面绘制。 */
    static int redMinFor(const QString &kind);      /* kind: pin / enc / lora / protocol */
    static int yellowMinFor(const QString &kind);
    static int cacheRedSeconds();
    static int cacheYellowSeconds();
    static QString thresholdsSource();              /* 本次实际用的配置文件路径 */
    static void resetThresholdsForTest();           /* 改了 TOOLBOX_THRESHOLDS 后重载缓存 */

protected:
    void paintEvent(QPaintEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;   /* 双击热力图：切大格 */
    void leaveEvent(QEvent *e) override;

signals:
    /* 点了健康度卡片：请求跳到对应工具页（key 同 setTools 的键）。 */
    void toolActivated(const QString &key);

private:
    /* 一次绘制里登记的可交互区域：悬停显示 tip，点了发 key（key 为空则只悬停）。 */
    struct Hit {
        QRect rect;
        QString key;      /* 非空 = 点它跳工具页（健康度卡片） */
        QString tip;      /* 悬停提示 */
        QString pin;      /* 非空 = 引脚名，点击查详情 */
        int     board = -1;   /* 引脚所属板索引 */
    };

    /* 按主题取的一整套配色，避免在绘制代码里散落硬编码色。 */
    struct Palette {
        QColor card, border, textPri, textSec, textTer;
        QColor ok, warn, err, info;
        QColor freeCell, usedCell, conflictCell, declCell, track;
    };

    Palette makePalette() const;
    int drawConclusion(QPainter &p, int x, int y, int w, const Palette &pal);   /* B3：一句话结论 + 先看哪 3 件 */
    int drawHealth(QPainter &p, int x, int y, int w, const Palette &pal);
    int drawDelta(QPainter &p, int x, int y, int w, const Palette &pal);
    int drawPins(QPainter &p, int x, int y, int w, const Palette &pal);
    int drawMemory(QPainter &p, int x, int y, int w, const Palette &pal);
    int drawTasks(QPainter &p, int x, int y, int w, const Palette &pal);
    int drawTrend(QPainter &p, int x, int y, int w, const Palette &pal);
    int sectionTitle(QPainter &p, int x, int y, int w, const QString &text, const Palette &pal);
    int bar(QPainter &p, int x, int y, int w, const QString &label,
            double used, double total, const Palette &pal, const QColor &fill);
    void showPinDetail(const QString &pin, int board);   /* 点击引脚格子：拼出详情文本 */

    QVariantMap m_tools;
    QString m_notice;
    int m_contentHeight = 0;
    QVector<Hit> m_hits;      /* 每次 paint 重建，供悬停/点击命中测试 */
    QStringList m_failedKeys; /* 本轮没取到数的工具键 */

    bool    m_pinZoom = false;   /* 热力图大格模式（双击切换） */
    QString m_pinDetail;         /* 选中的引脚详情（画在热力图标题下方） */
    QStringList m_delta;         /* 「较上次」增量文本，空表示无基线 */
    QString m_conclusionText;    /* B3：上次绘制算出的一句话结论（供自检读取） */
    int     m_conclusionCount = 0;   /* B3：上次绘制算出的"有问题项数" */
};

#endif // DASHBOARDCANVAS_H
