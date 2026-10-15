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

signals:
    /* 本页的「🌗 主题」按钮切的是工具箱全局主题（与侧栏那个是同一份 QSS），
       切完得让侧栏把自己的按钮文字刷新一下，否则两处显示不一致。 */
    void themeToggled();

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
    QLabel       *m_count  = nullptr;
    QListWidget  *m_list   = nullptr;
    QTextBrowser *m_detail = nullptr;
};

#endif // MAINWINDOW_H
