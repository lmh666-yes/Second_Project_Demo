#ifndef TOOLBOXWINDOW_H
#define TOOLBOXWINDOW_H

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QStringList>

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
    /* suppressFirstRun：测试/探针路径传 true —— 既别弹首次运行向导，也别把
       "已弹过"记进 _toolbox.ini（否则冒烟会替用户把"第一次"吃掉，用户自己反而看不到）。 */
    explicit ToolboxWindow(QWidget *parent = nullptr, bool suppressFirstRun = false);

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

    /* D2：首次运行向导该不该弹（读 _toolbox.ini 的 ui/welcomeShown）。
       做成 public static 是为了让 --welcome-probe 能断言"只弹一次"这条逻辑。 */
    static bool shouldShowWelcome();

    /* D3：清理口径做成 static 纯函数，设置页按它删、--clean-probe 按它断言。
       overdueSnapshots(dir, keep)：按文件名升序（旧→新）列出该删的过期快照。
       reportFiles(toolsDir)：列出各检查器写的「*报告.md」。 */
    static QStringList overdueSnapshots(const QString &dir, int keep);
    static QStringList reportFiles(const QString &toolsDir);

    /* H1：页面表做成**单一来源** —— 侧栏、页面堆栈、自检、量测都从这一份推，
       文档与注释不再写死页数（原来"15 个页面"散在 5 处，实际 16 个，改一处漏一处）。
       四个查询按侧栏顺序：分组名 / 每组页数 / 页名 / 总页数。
       做成 public static 是为了让 --selftest 能直接断言（不必构造窗口）。 */
    static QStringList pageGroupNames();
    static QList<int>   pageGroupCounts();
    static QStringList pageNames();
    static int          pageCount();

    /* B3：闸门要的那份插件清单 —— (侧栏页名, 插件脚本绝对路径) 列表，顺序同侧栏。
       与 discoverPlugins 同源，免得"侧栏一份口径、闸门另一份口径"。
       做成 public static 是为了让 main.cpp 的 --selftest 能把插件壳**自动收进闸门**
       （之前是手工往表里补一行：B1 没做全的那半）。 */
    static QList<QPair<QString, QString>> pluginShells(const QString &toolsDir);

private:
    /* 页面表的载体：分组名 + 侧栏项名 + 该页的构造函数。构造函数在下面 pageDefs() 里
       一次列全，构造窗口时按它逐页建、逐组插分组头。 */
    /* plugin 非空 = 这一页由「检查器插件」提供（D2）：脚本自己带 --manifest，
       界面这边只负责给它一个 ToolRunPage，加检查器不用改 C++。 */
    struct PageDef { QString group; QString name;
                     QWidget *(ToolboxWindow::*build)();
                     QString plugin; };
    static const QList<PageDef> &pageDefs();
    /* D2：扫「检查/<名字>/check.py」并读 --manifest，返回要追加的页。
       插件坏了 / 没 python / 没 manifest 一律静默跳过（不能拖垮整个工具箱）。 */
    static QList<PageDef> discoverPlugins(const QString &toolsDir);
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
    QWidget *buildBuildFlashPage();     /* 编译烧录：编译→烧录→板载自检 + 体积趋势 + 状态快照 */
    QWidget *buildPluginPage(const QString &script);   /* D2：插件页（ToolRunPage 包一层） */
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
    bool m_suppressWelcome = false;   /* D2：测试路径不弹首次向导、也不记"已弹过" */

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
