#ifndef TOOLBOXWINDOW_H
#define TOOLBOXWINDOW_H

#include <QHash>
#include <QList>
#include <QMainWindow>

class QCloseEvent;
class QCheckBox;
class QComboBox;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QWidget;
class KickStartWindow;
class MainWindow;
class OverviewPage;
class ScaffoldPage;
class WorkbenchWindow;

class ToolboxWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ToolboxWindow(QWidget *parent = nullptr);

    /* 工具箱根目录（Python 脚本所在那层）。做成公开静态函数是为了让 --selftest
       也能断言它解析成功——路径判据一旦写错，运行器页会整体失效，属于必须测到的东西。 */
    static QString toolsDirPath();

    /* 按页面标题跳到对应页（列表项文本匹配）。供体检面板的健康度卡片联动。 */
    void jumpToPage(const QString &title);

    /* C1：全局搜索（Ctrl+K）的候选 —— 一次搜「页面名 / 检查项 / 手册条目」。
       返回 "类别|显示文本" 列表（类别 ∈ 页面 / 检查项 / 手册）；查询为空时列出页面名。
       手册条目来自速查数据集，首次调用会装载数据；检查项来自各报告的命中项小节。
       做成 public 是为了让 --palette-probe 能直接断言（错例：搜得到；反例：乱词搜不到）。 */
    QStringList paletteHits(const QString &query) const;

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
    QWidget *buildLoraPage();           /* LoRa 检查：配置一致性 / 引脚成对 / 波特率合法 */
    QWidget *buildOtaPage();            /* OTA 台架：包 OTA/tools/ota_send.py 串口直连模式 */
    QWidget *buildCachePage();          /* 断网缓存：W25QXX 环形日志容量与补传节奏核算 */
    QWidget *buildScaffoldPage();       /* 工程模板：一键生成 STM32 工程骨架 */
    QWidget *buildFullCheckPage();
    QWidget *buildOverviewPage();
    QWidget *buildSettingsPage();       /* 设置：集中放个性化选项（默认不变） */

    /* 速查页懒加载：4MB dataset.json + 建索引，别在工具箱启动时就付这个代价
       （用户第一诉求就是"打开要快"）。首次切到该页时才装载。 */
    void ensureLookupLoaded();

    /* 把设置里的个性化选项推回各页的控件（挡信号，避免回写死循环）。
       设置页改完立刻生效，不用重启。 */
    void applyPrefsToPages();

    void jumpToWorkbench(const QString &hex);   /* 联动：切页 + 填入字节流 */

    /* C1：Ctrl+K 打开全局搜索面板 */
    void showPalette();

    QString m_toolsDir;                 /* 工具箱目录，Python 脚本所在 */
    QListWidget    *m_list  = nullptr;
    QStackedWidget *m_pages = nullptr;
    QHash<int, int> m_rowToPage;        /* 列表行号 → 页面索引，分组头为 -1 */
    QList<QWidget *> m_warnTargets;     /* 目录缺失时需要告警的运行器页 */
    WorkbenchWindow *m_workbench = nullptr;   /* 供跨工具联动调用 */
    KickStartWindow *m_kickstart = nullptr;   /* 供 RTOS 生成页导入任务表 */
    MainWindow      *m_lookup    = nullptr;   /* 单片机手册（懒加载） */
    OverviewPage    *m_overview  = nullptr;   /* C1：全局搜索列「检查项」要从它取小节标题 */
    int m_workbenchPage = -1;                 /* 协议工作台在堆栈里的页号 */
    int m_lookupPage    = -1;                 /* 速查页在堆栈里的页号 */
    bool m_lookupLoaded = false;

    /* 各页的个性化选项控件：设置页改完要能立刻推回来，所以留指针。
       这些控件同时也是各自页面上可见的选项，两边读写同一份 _toolbox.ini 键。 */
    QComboBox *m_pinBoard  = nullptr;
    QComboBox *m_linkScen  = nullptr;
    QSpinBox  *m_linkCount = nullptr;
    QCheckBox *m_encEol    = nullptr;

    /* 设置页里的控件（改这些 = 改 _toolbox.ini，并同步到上面的页内控件） */
    QComboBox *m_setTheme    = nullptr;
    QComboBox *m_setPinBoard = nullptr;
    QComboBox *m_setLinkScen = nullptr;
    QSpinBox  *m_setLinkCnt  = nullptr;
    QCheckBox *m_setEncEol   = nullptr;

protected:
    void closeEvent(QCloseEvent *e) override;   /* 关窗时记忆几何与当前页 */
};

#endif // TOOLBOXWINDOW_H
