#ifndef TOOLBOXWINDOW_H
#define TOOLBOXWINDOW_H

#include <QHash>
#include <QList>
#include <QMainWindow>

class QCloseEvent;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QWidget;
class KickStartWindow;
class MainWindow;
class WorkbenchWindow;

class ToolboxWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ToolboxWindow(QWidget *parent = nullptr);

    /* 工具箱根目录（Python 脚本所在那层）。做成公开静态函数是为了让 --selftest
       也能断言它解析成功——路径判据一旦写错，运行器页会整体失效，属于必须测到的东西。 */
    static QString toolsDirPath();

private:
    QWidget *buildKickstartPage();
    QWidget *buildRtosGenPage();
    QWidget *buildWorkbenchPage();
    QWidget *buildLookupPage();
    QWidget *buildProgressPage();
    QWidget *buildPinPage();
    QWidget *buildRtosPage();
    QWidget *buildEncodingPage();
    QWidget *buildLinkPage();
    QWidget *buildFullCheckPage();
    QWidget *buildOverviewPage();

    /* 速查页懒加载：4MB dataset.json + 建索引，别在工具箱启动时就付这个代价
       （用户第一诉求就是"打开要快"）。首次切到该页时才装载。 */
    void ensureLookupLoaded();

    void jumpToWorkbench(const QString &hex);   /* 联动：切页 + 填入字节流 */

    QString m_toolsDir;                 /* 工具箱目录，Python 脚本所在 */
    QListWidget    *m_list  = nullptr;
    QStackedWidget *m_pages = nullptr;
    QHash<int, int> m_rowToPage;        /* 列表行号 → 页面索引，分组头为 -1 */
    QList<QWidget *> m_warnTargets;     /* 目录缺失时需要告警的运行器页 */
    WorkbenchWindow *m_workbench = nullptr;   /* 供跨工具联动调用 */
    KickStartWindow *m_kickstart = nullptr;   /* 供 RTOS 生成页导入任务表 */
    MainWindow      *m_lookup    = nullptr;   /* STM32 速查（懒加载） */
    QPushButton     *m_themeBtn  = nullptr;   /* 侧栏主题按钮，速查页切主题时要同步文字 */
    int m_workbenchPage = -1;                 /* 协议工作台在堆栈里的页号 */
    int m_lookupPage    = -1;                 /* 速查页在堆栈里的页号 */
    bool m_lookupLoaded = false;

protected:
    void closeEvent(QCloseEvent *e) override;   /* 关窗时记忆几何与当前页 */
};

#endif // TOOLBOXWINDOW_H
