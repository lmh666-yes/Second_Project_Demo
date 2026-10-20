#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "dataset.h"
#include "searchtables.h"
#include "searchengine.h"
#include "store.h"

#include <QMainWindow>
#include <QScopedPointer>

class QLineEdit;
class QComboBox;
class QListWidget;
class QTextBrowser;
class QLabel;
class QPushButton;
class QTimer;
class QUrl;
class QShowEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    /* 供命令行出口复用：装载数据，失败原因写进 err */
    bool loadData(QString *err = nullptr);
    const Dataset &dataset() const { return m_ds; }

    /* 当前板块 / 当前显示的条目下标（截图与自检用） */
    int section() const { return m_section; }
    QVector<int> shown() const { return m_shown; }
    SearchEngine *engine() const { return m_engine.data(); }
    /* 落地页 HTML（供 --selftest 做结构自检；判"什么时候显示"在 refreshList 里） */
    QString landingHtml() const;

    /* C1：报告里的条目名直达 —— 外部（工程体检总览页）点条目名时调这个：
       把词填进搜索框、按当前板块跑一遍，命中就选中第一条。返回本次命中条数。 */
    int jumpToTerm(const QString &term);

    /* 供 --selftest 断言「下一步：阶段 N」入口：切到「项目引导」并选中该工程，
       返回横幅文字（空 = 没显示）。不依赖窗口是否 show，便于命令行自检。 */
    QString nextStageHintFor(const QString &projectTag);

signals:
    /* 本页的「🌗 主题」按钮切的是工具箱全局主题（与侧栏那个是同一份 QSS），
       切完得让侧栏把自己的按钮文字刷新一下，否则两处显示不一致。 */
    void themeToggled();

protected:
    /* 每次切回本页刷新「下一步」横幅：你可能刚在编辑器里把某个阶段勾上了，
       不刷的话横幅还停在上一个阶段（只有列表刷新才会重算）。 */
    void showEvent(QShowEvent *e) override;

private slots:
    void onSearchEdited();
    void onSectionClicked(int sec);
    void onModuleChanged(int idx);
    void onCardPicked(int row);
    void onAnchor(const QUrl &url);
    void onThemeToggled();
    void onFavClicked();
    void onRecentClicked();
    void toggleCurrentFav();

private:
    void buildUi();
    void rebuildModuleBox();
    void refreshList();
    void showCard(int cardIndex);
    void updateCounts();
    /* 项目引导「下一步」入口：读该工程进度文档的阶段总表，找第一个没勾的，
       在列表上方显示「▶ 下一步：板1 · 阶段 3｜xxx（点这里直达）」。
       只在「项目引导 + 选中某个具体工程」时出现；读不到文档就静默不显示。 */
    void updateNextStageHint();
    void jumpToNextStage();
    /* A1：跑一遍阶段判定并回写进度文档（脚本 stage_status.py --apply），
       跑完立刻刷新「下一步」横幅 —— 判完就知道该干哪一段了。 */
    void runStageJudge();
    /* 复位「模块」筛选（按名字跳 / 报告里点条目名之前都调，免得条目被筛掉） */
    void resetModuleFilter();
    /* 按条目名跳转（收藏 / 最近 / 相关与联动都走这里） */
    void jumpToName(const QString &name);

    Dataset       m_ds;
    SearchTables  m_tables;
    Store         m_store;
    QScopedPointer<SearchEngine> m_engine;
    QVector<int>  m_shown;      /* 当前列表里的条目下标 */
    int           m_section = int(Card::Ref);
    int           m_current = -1;

    QLineEdit    *m_search = nullptr;
    QComboBox    *m_module = nullptr;
    QTimer       *m_searchTimer = nullptr;   /* 边打字边出结果（防抖，避免每键都全量重排） */
    QPushButton  *m_secBtn[3] = { nullptr, nullptr, nullptr };
    QPushButton  *m_btnFav = nullptr;
    QPushButton  *m_btnRecent = nullptr;
    QPushButton  *m_nextHint = nullptr;      /* 项目引导的「下一步：阶段 N」入口 */
    QPushButton  *m_btnJudge = nullptr;      /* A1：判定阶段并回写进度文档 */
    QLabel       *m_count  = nullptr;
    QListWidget  *m_list   = nullptr;
    QTextBrowser *m_detail = nullptr;
};

#endif // MAINWINDOW_H
