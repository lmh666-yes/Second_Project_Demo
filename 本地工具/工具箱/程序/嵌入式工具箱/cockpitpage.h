#ifndef COCKPITPAGE_H
#define COCKPITPAGE_H

/* C2 项目驾驶舱页：一屏把「项目脉搏」摆出来 —— 现在到哪、还差什么、下一步点哪，并且**按钮真能触发**。

   五张卡（自上而下）：
     1. 阶段进度 —— 每块板 x/y + 「下一个要做的」+「机器能判的下一个」
     2. 体检结论 —— 一句话结论 + 最该先看的几件
     3. 体积趋势 —— 每块板最近一次 Flash/RAM + 记录轮数 + 较上次
     4. 自检覆盖 —— 闸门多少项 / 插件壳多少 / 变异覆盖多少
     5. 待办与桩 —— 标记 / 桩函数 / 空壳 + 桩最多的文件

   **数字不在这里算**，一处口径一处实现：
     · 1 / 3 / 4 / 5 由 `检查/驾驶舱/cockpit_data.py` **一次取数**给出（它自己会去跑
       阶段判定 / 体积历史 / 平台清单 / 进度扫描 四个兄弟脚本，并自带 --selftest 对账）；
     · 2 「体检结论」接「工程体检总览」的数据，用的还是 `DashboardCanvas::findingsFor()`
       那一条口径 —— 不在这一页重复实现一遍判定，也不另跑一套检查器。
       （为什么不直接读总览页画布上的结论文字：**隐藏页不绘画** —— 用户没去过总览页时
       画布从没 paintEvent 过，那份文字压根没算出来。所以结论要能脱离绘制算。）

   动作（点了真跑，输出在下方内嵌的运行器里；跑完自动刷新本页卡片）：
     · 跑阶段验收 —— `build_flash.py --stage-run N --board 板X`（N 与板名都来自取数结果，不写死）
     · 刷体检     —— 让「工程体检总览」重跑一轮取数（本页结论跟着更新）
     · 出报告     —— `report_gen.py`（单文件 HTML 周报，写 产物/周报.html）
     · 跑一键验收·快档 —— `verify_all.py --no-build`（**跳构建**：工具箱正开着时 exe 被自己占用）
     · 去平台清单 —— 跳到「平台清单」页看逐项明细

   C2 探针（`--cockpit-probe`）读的就是下面这几个访问器：把本页显示的现场值与
   `cockpit_data.py` 当场跑出来的 JSON 逐条对账，钉住"卡片不是写死的"。 */

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <QWidget>

class QLabel;
class QProcess;
class QPushButton;
class QShowEvent;
class QJsonObject;
class OverviewPage;
class ToolRunPage;

class CockpitPage : public QWidget {
    Q_OBJECT
public:
    explicit CockpitPage(const QString &toolsDir, QWidget *parent = nullptr);
    ~CockpitPage() override;

    /* 接「工程体检总览」页 —— 主窗口在所有页都建好后调；探针路径一样（自己造一个再灌进来）。
       「体检结论」卡与「刷体检」按钮都走它；没接上就如实写"没接总览页"。 */
    void setOverview(OverviewPage *ov);

    /* ---- C2 探针用的访问器（现场值） ---- */
    bool    loaded() const { return m_loaded; }          /* 四张现场卡已渲染 */
    bool    ready() const;                               /* 四张现场卡 + 体检结论都已就绪 */
    /* 卡的三样分开给（标题 / 值 / 小字）—— 值与小字里本身可能带"｜"，
       所以对账时**不要**把一行文本按分隔符切开拼回去。 */
    QStringList cardTitles() const;
    QStringList cardValues() const;
    QStringList cardSubs() const;
    QStringList cardLines() const;                       /* 一屏文本（人看日志用） */
    QString nextAction() const { return m_nextAction; }
    QStringList actionLabels() const;                    /* 动作按钮文字（含"不能点"的） */
    bool    stageRunEnabled() const;                     /* 「跑阶段验收」能不能点 */
    QStringList stageRunArgv() const { return m_stageArgv; }   /* 它将执行的命令（不含 python） */
    QString healthLine() const;                                 /* 体检结论卡的结论行 */
    QString healthFindings() const;                             /* 体检结论卡的小字 */

public slots:
    void refresh();                                      /* 重取数（首次进本页也会自动走一次） */

signals:
    void requestPage(const QString &title);               /* 动作里"去某页" */

protected:
    void showEvent(QShowEvent *e) override;

private slots:
    void onDataFinished();
    void onOverviewData();                                /* 总览页取数收尾 → 更新结论卡 */

private:
    void startData();
    void renderCards(const QJsonObject &j);
    void rebuildActions(const QJsonObject &j);
    void updateHealthCard();
    void startRun(const QString &label, const QString &script, const QStringList &args);
    void setStatus(const QString &text);

    QString m_toolsDir;
    OverviewPage *m_overview = nullptr;
    ToolRunPage  *m_run = nullptr;
    QProcess     *m_proc = nullptr;      /* cockpit_data.py 取数进程 */
    QLabel       *m_status = nullptr;
    QLabel       *m_next = nullptr;      /* 「下一步点哪」横幅 */
    QPushButton  *m_btnStage = nullptr;
    QVector<QLabel *> m_cardTitle;
    QVector<QLabel *> m_cardValue;
    QVector<QLabel *> m_cardSub;
    QStringList  m_actions;              /* 动作按钮文字（顺序与 m_btn* 一致） */
    QStringList  m_stageArgv;            /* 「跑阶段验收」的 argv */
    QString      m_nextAction;
    bool m_loaded = false;
    bool m_started = false;              /* 首次进页只自动取一次数 */
    int  m_gen = 0;                      /* 连点刷新时作废上一轮回调 */
};

#endif // COCKPITPAGE_H
