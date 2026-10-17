#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

/* 工程体检总览页：把各检查工具的结果聚到一屏。两个页签：
     · 可视化仪表盘 —— 健康度卡片 / 引脚热力图 / 内存占用 / 任务 / 趋势（DashboardCanvas 自绘）
     · 报告原文     —— 各工具生成的 md 报告摘要
   点「刷新」调各工具 --json 取结构化数据；首次切到本页也会自动取一次。
   只读，不改任何东西。 */

#include <QString>
#include <QVariantMap>
#include <QWidget>

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

protected:
    void showEvent(QShowEvent *e) override;

signals:
    /* 请求工具箱主窗口跳到某个工具页（按页面标题）。 */
    void requestPage(const QString &title);

private slots:
    void refresh();
    void openReportsDir();

private:
    /* 一个检查器的取数进程跑完：收 JSON、记账；全部到齐就渲染。 */
    void onFetchFinished(const QString &key, QProcess *proc);
    void finishFetch();
    void loadTextReports();

    /* 体检快照（D2 归档 + A3 对比）：每轮刷新存一份带时间戳的 JSON，只留最近 20 份。 */
    QString     snapshotDir() const;
    QVariantMap readPrevSnapshot() const;
    void        writeSnapshot(const QVariantMap &tools);
    QStringList computeDelta(const QVariantMap &prev, const QVariantMap &cur) const;

    QString m_toolsDir;
    QTabWidget      *m_tabs   = nullptr;
    DashboardCanvas *m_canvas = nullptr;
    QTextBrowser    *m_view   = nullptr;
    QLabel          *m_status = nullptr;
    bool m_firstShown = false;

    /* 异步取数状态：各检查器并行跑，各自回来时累加，到齐后统一渲染。 */
    int         m_pending = 0;
    QVariantMap m_tools;
    QStringList m_failed;
};

#endif // OVERVIEWPAGE_H
