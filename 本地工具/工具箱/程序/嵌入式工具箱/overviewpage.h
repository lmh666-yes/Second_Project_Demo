#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

/* 工程体检总览页：把各检查工具的结果聚到一屏。三个页签：
     · 可视化仪表盘 —— 健康度卡片 / 引脚热力图 / 内存占用 / 任务 / 趋势（DashboardCanvas 自绘）
     · 报告原文     —— 各工具生成的 md 报告摘要
     · 历史回看     —— 从 _体检历史/ 里挑某一轮快照重新渲染（不只跟上一轮比）
   点「刷新」调各工具 --json 取结构化数据；首次切到本页也会自动取一次。
   只读，不改任何东西。 */

#include <QString>
#include <QVariantMap>
#include <QWidget>

class QComboBox;
class QCheckBox;
class QLabel;
class QProcess;
class QPushButton;
class QShowEvent;
class QTabWidget;
class QTextBrowser;
class QTimer;
class DashboardCanvas;

class OverviewPage : public QWidget {
    Q_OBJECT
public:
    explicit OverviewPage(const QString &toolsDir, QWidget *parent = nullptr);

    /* 测试钩子：把这些工具键当作"没取到"（不真起进程）。
       供 `--shot-overview --drop <键>` 断言"缺数不许画成绿色"。 */
    void setDropKeys(const QStringList &keys);

    /* 「报告原文」里已转成可点链接的文件位置数（供 --shot-overview 打印核对）。 */
    int srcLinkCount() const { return m_srcTargets.size(); }

    /* C1：报告原文里已转成可点的「条目名」数（手册直达链接）。 */
    int docLinkCount() const { return m_docTargets.size(); }

    /* A1：历史回看下拉框里的快照轮数（供 --shot-overview 断言历史页真的建起来了）。 */
    int historyCount() const;

    /* A2：进度工具返回的历史轮数（≥2 才会有趋势折线；供 --shot-overview 决定是否断言趋势命中区）。 */
    int progressHistoryRounds() const;

    /* C1：全局搜索（Ctrl+K）列「检查项」用 —— 各报告的命中项小节标题（去重）。
       与 A4 的「只看命中项」同一套关键字，可视为"当前有哪些检查项"。 */
    QStringList findingHeadings() const;

    /* C1：全局搜索选中某条「检查项」时调用：切到「报告原文」页签、开「只看命中项」，
       并在报告原文里定位这段文字。 */
    void focusFinding(const QString &text);

    /* A2 自检用：健康度卡片 key → 报告里那一节的关键词；报告原文里是否真有这段文字。
       --finding-probe 据此断言"卡片指向的小节确实存在"（错例）+ "乱词找不到"（反例）。 */
    QString sectionKeywordOf(const QString &key) const;
    bool reportHasText(const QString &text) const;

protected:
    void showEvent(QShowEvent *e) override;

signals:
    /* 请求工具箱主窗口跳到某个工具页（按页面标题）。 */
    void requestPage(const QString &title);

    /* C1：请求主窗口把「单片机手册」跳到某个条目（报告里点了条目名）。 */
    void requestDoc(const QString &term);

private slots:
    void refresh();
    void openReportsDir();
    void showHistorySnapshot();

private:
    /* 一个检查器的取数进程跑完：收 JSON、记账；全部到齐就渲染。 */
    void onFetchFinished(const QString &key, QProcess *proc);
    void finishFetch();
    void loadTextReports();

    /* 体检快照（D2 归档 + A3 对比）：每轮刷新存一份带时间戳的 JSON，只留最近 20 份。 */
    QString     snapshotDir() const;
    QStringList listSnapshots() const;                 /* 升序（旧→新）的快照文件绝对路径 */
    QVariantMap readSnapshot(const QString &path) const;
    QVariantMap readPrevSnapshot() const;
    void        writeSnapshot(const QVariantMap &tools);
    QStringList computeDelta(const QVariantMap &prev, const QVariantMap &cur) const;

    /* A1 历史回看：重列下拉框（最新在前）、渲染选中的那一轮。 */
    void reloadHistory();
    void renderHistory(const QString &path);

    QString m_toolsDir;
    QTabWidget      *m_tabs   = nullptr;
    DashboardCanvas *m_canvas = nullptr;
    QTextBrowser    *m_view   = nullptr;
    QLabel          *m_status = nullptr;
    int              m_reportTab = -1;      /* C1：报告原文页签下标（focusFinding 切到它） */
    bool m_firstShown = false;
    /* 「报告原文」里被转成链接的文件位置：下标 → "绝对路径|行号"（用下标做锚点，
       避免把带反斜杠的 Windows 路径塞进 URL 被编码搅乱）。 */
    QStringList m_srcTargets;

    /* C1：报告原文里被转成链接的条目名（点它跳「单片机手册」对应卡）。 */
    QStringList m_docTargets;

    /* A1：历史回看页的下拉框与画布（与当前轮完全独立，互不干扰）。
       A3：再加一个「对比基准」下拉框，可任选两轮出增减，而不是只能跟上一轮比。 */
    QComboBox       *m_histCombo  = nullptr;
    QComboBox       *m_histBaseCombo = nullptr;
    DashboardCanvas *m_histCanvas = nullptr;
    QLabel          *m_histStatus = nullptr;

    /* A4：「报告原文」页的「只看命中项」开关（各报告的发现/冲突/违规小节抽出来单列）。 */
    QCheckBox       *m_onlyFind   = nullptr;

    /* 异步取数状态：各检查器并行跑，各自回来时累加，到齐后统一渲染。 */
    int         m_pending = 0;
    QVariantMap m_tools;
    QStringList m_failed;
    QStringList m_dropKeys;    /* 测试钩子：这些键一律当"没取到" */
};

#endif // OVERVIEWPAGE_H
