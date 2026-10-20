#include "toolboxwindow.h"

#include "kickstartwindow.h"
#include "mainwindow.h"
#include "cockpitpage.h"
#include "overviewpage.h"
#include "rtosgenpage.h"
#include "buildflashpage.h"
#include "scaffoldpage.h"
#include "settings.h"
#include "theme.h"
#include "workbenchwindow.h"
#include "toolrunpage.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include "pyfind.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

/* 工具箱根目录：从 exe 位置向上找同时含「程序」「检查」「shared」的目录。
   为什么不用「设计」当标记：2026-10 起工具箱内的独立子程序入口（协议工作台 /
   项目起手助手）已删除——它们本来就是同一个 Toolbox.exe 里的页面，留着只是重复入口。
   「设计」目录随之不存在了，所以标记改成「程序」（exe 自己就在 程序/嵌入式工具箱 下）。 */
QString ToolboxWindow::toolsDirPath()
{
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (d.exists(QStringLiteral("程序")) && d.exists(QStringLiteral("检查"))
            && d.exists(QStringLiteral("shared")))
            return d.absolutePath();
        if (!d.cdUp())
            break;
    }
    return QString();
}

static QString py(const QString &dir, const QString &rel)
{
    return QDir(dir).filePath(rel);
}

/* H1：页面表 —— 全工具箱**唯一**一份"有哪些页、在哪一组"的口径。
   加/删页面只改这里：侧栏、页面堆栈、--selftest、--ui-audit/--perf-probe 都跟着走。 */
/* D2 的插件约定（见 检查/示例插件/check.py）：
     check.py --manifest  → {"name":"...","group":"检查","desc":"...","report":"产物/x.md"}
   读 manifest 走子进程，不猜、不硬编：脚本自己说了算。 */
static QJsonObject readPluginManifest(const QString &script)
{
    const QStringList py = PyFind::detect();
    if (py.isEmpty() || script.isEmpty())
        return {};
    QProcess proc;
    proc.setWorkingDirectory(QFileInfo(script).absolutePath());
    QStringList args = py.mid(1);
    args << script << QStringLiteral("--manifest");
    proc.start(py.value(0), args);
    if (!proc.waitForFinished(8000))
        return {};
    return QJsonDocument::fromJson(proc.readAllStandardOutput()).object();
}

/* B1：插件 manifest 缓存 —— 发现阶段（discoverPlugins）已经把每个插件的元信息读回来了，
   建页阶段（buildPluginPage）**不该再起一次 python 进程**：那是冷启动的第二笔大开销
   （7 个插件 ≈ 1.4s，实测过）。这里按脚本绝对路径存，发现阶段填、建页阶段取；
   取不到才回落到"当场读一次"（保证单独用 buildPluginPage 时也对）。 */
static QHash<QString, QJsonObject> g_pluginManifests;

static QJsonObject pluginManifest(const QString &script)
{
    if (g_pluginManifests.contains(script))
        return g_pluginManifests.value(script);
    return readPluginManifest(script);
}

/* B1 性能修补：原来**逐个插件起一个 python 进程**读 manifest —— 插件从 1 个涨到 7 个后，
   冷启动多花 ~1.5-2s（实测 24 页 2574ms，超了 1500ms 那条线）。
   改成**一个** python 进程把 `检查/_manifest_all.py` 跑一遍，一次拿回全部元信息。
   约定没变（每个插件照样回答 `--manifest`），只是"问"的方式从挨个敲门变成一次问完。 */
static QJsonArray readAllPluginManifests(const QString &toolsDir)
{
    QJsonArray out;
    const QStringList py = PyFind::detect();
    if (py.isEmpty() || toolsDir.isEmpty())
        return out;
    const QString driver = QDir(toolsDir).filePath(QStringLiteral("检查/_manifest_all.py"));
    if (!QFileInfo::exists(driver))
        return out;                     /* 没有批读脚本 → 调用方回落到逐个读 */
    QProcess proc;
    proc.setWorkingDirectory(QFileInfo(driver).absolutePath());
    QStringList args = py.mid(1);
    args << driver;
    proc.start(py.value(0), args);
    if (!proc.waitForFinished(15000))
        return out;
    return QJsonDocument::fromJson(proc.readAllStandardOutput()).array();
}

QList<ToolboxWindow::PageDef> ToolboxWindow::discoverPlugins(const QString &toolsDir)
{
    QList<PageDef> out;
    if (toolsDir.isEmpty())
        return out;
    const QDir root(QDir(toolsDir).filePath(QStringLiteral("检查")));

    /* 快路径：一次进程拿全部 manifest（B1） */
    for (const QJsonValue &v : readAllPluginManifests(toolsDir)) {
        const QJsonObject m = v.toObject();
        const QString name = m.value(QStringLiteral("name")).toString().trimmed();
        const QString script = m.value(QStringLiteral("script")).toString();
        if (name.isEmpty() || script.isEmpty() || !QFileInfo::exists(script))
            continue;
        const QString group = m.value(QStringLiteral("group")).toString().trimmed();
        g_pluginManifests.insert(script, m);       /* B1：留给建页阶段复用，省掉第二次 python */
        out.append(PageDef{group.isEmpty() ? QStringLiteral("检查") : group,
                           name, nullptr, script});
    }
    if (!out.isEmpty())
        return out;

    /* 兜底：批读脚本缺失 / 跑不起来时，仍按"逐目录敲门"来一遍（不至于一个插件都没有） */
    for (const QFileInfo &fi : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString script = QDir(fi.absoluteFilePath()).filePath(QStringLiteral("check.py"));
        if (!QFileInfo::exists(script))
            continue;
        const QJsonObject m = readPluginManifest(script);
        const QString name = m.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
            continue;                   /* 没 manifest = 不是插件，跳过 */
        const QString group = m.value(QStringLiteral("group")).toString().trimmed();
        g_pluginManifests.insert(script, m);
        out.append(PageDef{group.isEmpty() ? QStringLiteral("检查") : group,
                           name, nullptr, script});
    }
    return out;
}

/* B3：闸门要的插件清单 —— (侧栏页名, 脚本绝对路径)，顺序 = 侧栏顺序。
   只是把 discoverPlugins 的结果转个形状，不另立口径。 */
QList<QPair<QString, QString>> ToolboxWindow::pluginShells(const QString &toolsDir)
{
    QList<QPair<QString, QString>> out;
    for (const PageDef &d : discoverPlugins(toolsDir))
        if (!d.plugin.isEmpty())
            out.append(qMakePair(d.name, d.plugin));
    return out;
}

const QList<ToolboxWindow::PageDef> &ToolboxWindow::pageDefs()
{
    static const QList<PageDef> defs = []() {
        QList<PageDef> v = {
            {QStringLiteral("查资料"), QStringLiteral("单片机手册"), &ToolboxWindow::buildLookupPage},
            {QStringLiteral("设计"), QStringLiteral("项目起手助手"), &ToolboxWindow::buildKickstartPage},
            {QStringLiteral("设计"), QStringLiteral("RTOS 骨架生成"), &ToolboxWindow::buildRtosGenPage},
            {QStringLiteral("设计"), QStringLiteral("协议工作台"), &ToolboxWindow::buildWorkbenchPage},
            {QStringLiteral("检查"), QStringLiteral("进度扫描"), &ToolboxWindow::buildProgressPage},
            {QStringLiteral("检查"), QStringLiteral("引脚规划"), &ToolboxWindow::buildPinPage},
            {QStringLiteral("检查"), QStringLiteral("RTOS 检查"), &ToolboxWindow::buildRtosPage},
            {QStringLiteral("检查"), QStringLiteral("编码守卫"), &ToolboxWindow::buildEncodingPage},
            {QStringLiteral("检查"), QStringLiteral("链路仿真"), &ToolboxWindow::buildLinkPage},
            {QStringLiteral("检查"), QStringLiteral("LoRa 检查"), &ToolboxWindow::buildLoraPage},
            {QStringLiteral("检查"), QStringLiteral("OTA 台架"), &ToolboxWindow::buildOtaPage},
            {QStringLiteral("检查"), QStringLiteral("断网缓存"), &ToolboxWindow::buildCachePage},
            {QStringLiteral("检查"), QStringLiteral("工程模板"), &ToolboxWindow::buildScaffoldPage},
            {QStringLiteral("检查"), QStringLiteral("编译烧录"), &ToolboxWindow::buildBuildFlashPage},
            {QStringLiteral("汇总"), QStringLiteral("项目驾驶舱"), &ToolboxWindow::buildCockpitPage},
            {QStringLiteral("汇总"), QStringLiteral("工程体检总览"), &ToolboxWindow::buildOverviewPage},
            {QStringLiteral("汇总"), QStringLiteral("全量检查"), &ToolboxWindow::buildFullCheckPage},
            {QStringLiteral("设置"), QStringLiteral("设置"), &ToolboxWindow::buildSettingsPage},
        };
        for (const PageDef &p : discoverPlugins(ToolboxWindow::toolsDirPath())) {
            /* 插到**同名分组的末尾**，而不是整表末尾 ——
               直接 append 会在侧栏造出第二个「检查」分组（第一次就跑出来了：
               18 页 / 6 组，末尾多出一组）。分组不存在才追加到表尾。 */
            int at = -1;
            for (int i = 0; i < v.size(); ++i)
                if (v.at(i).group == p.group)
                    at = i + 1;
            if (at < 0)
                v.append(p);
            else
                v.insert(at, p);
        }
        return v;
    }();
    return defs;
}

QStringList ToolboxWindow::pageGroupNames()
{
    QStringList gs;
    for (const PageDef &d : pageDefs())
        if (gs.isEmpty() || gs.last() != d.group)
            gs << d.group;
    return gs;
}

QList<int> ToolboxWindow::pageGroupCounts()
{
    QList<int> cs;
    QString cur;
    for (const PageDef &d : pageDefs()) {
        const QString g = d.group;
        if (cs.isEmpty() || g != cur) {
            cs << 0;
            cur = g;
        }
        cs.last() += 1;
    }
    return cs;
}

QStringList ToolboxWindow::pageNames()
{
    QStringList ns;
    for (const PageDef &d : pageDefs())
        ns << d.name;
    return ns;
}

int ToolboxWindow::pageCount()
{
    return pageDefs().size();
}

ToolboxWindow::ToolboxWindow(QWidget *parent, bool suppressFirstRun)
    : QMainWindow(parent), m_suppressWelcome(suppressFirstRun)
{
    setWindowTitle(QStringLiteral("嵌入式工具箱"));
    m_toolsDir = toolsDirPath();

    QWidget *central = new QWidget(this);
    QHBoxLayout *root = new QHBoxLayout(central);

    /* 左栏：一段引导 + 分组列表。新用户打开就知道从哪开始。 */
    QWidget *side = new QWidget(central);
    side->setFixedWidth(220);
    QVBoxLayout *sidelay = new QVBoxLayout(side);
    sidelay->setContentsMargins(8, 8, 8, 8);
    sidelay->setSpacing(6);

    QLabel *guide = new QLabel(side);
    guide->setWordWrap(true);
    guide->setText(QStringLiteral(
        "<b>建议顺序</b><br>"
        "1. 起手助手：先想清要做什么<br>"
        "2. 写代码时随时查函数 / 点各项检查<br>"
        "3. 收尾前一键全量检查"));
    guide->setStyleSheet(QStringLiteral(
        "background:rgba(127,127,127,26);border:1px solid rgba(127,127,127,70);"
        "border-radius:8px;padding:8px;"));
    sidelay->addWidget(guide);

    /* 侧栏只留「建议顺序 + 页面列表 + 设置」。
       主题、默认选项、帮助这些个性化内容全部收进「设置」页——
       以前侧栏挂着「主题」「帮助 / 快捷键」两个常驻按钮，既占地方，
       也和"页面列表"混在一起，新用户不知道哪个是工具、哪个是偏好设置。 */
    QLabel *hint = new QLabel(side);
    hint->setWordWrap(true);
    hint->setText(QStringLiteral("主题 / 默认选项 / 帮助 都在最下面的「设置」里。"));
    hint->setStyleSheet(QStringLiteral("color:#8a8a8a;font-size:11px;padding:0 2px;"));
    sidelay->addWidget(hint);

    m_list = new QListWidget(side);
    m_list->setFrameShape(QFrame::NoFrame);

    /* 分组头：不可选、加粗，仅作分区提示 */
    auto addHeader = [this](const QString &text) {
        QListWidgetItem *it = new QListWidgetItem(text, m_list);
        it->setFlags(Qt::NoItemFlags);
        QFont f = it->font();
        f.setBold(true);
        it->setFont(f);
        it->setForeground(QColor(QStringLiteral("#8a8a8a")));
        it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    };

    m_pages = new QStackedWidget(central);

    /* H1：逐页按**唯一一份**页面表（pageDefs）建 —— 分组一变就插一次分组头。
       侧栏项名、页面堆栈、自检断言、量测都从这一份推，不再各自维护页数。 */
    {
        const QList<PageDef> &defs = pageDefs();
        QString curGroup;
        for (const PageDef &d : defs) {
            const QString g = d.group;
            if (g != curGroup) {
                addHeader(g);
                curGroup = g;
            }
            m_list->addItem(d.name);
            m_pages->addWidget(d.plugin.isEmpty() ? (this->*(d.build))()
                                                 : buildPluginPage(d.plugin));
        }
    }

    sidelay->addWidget(m_list, 1);
    root->addWidget(side);
    root->addWidget(m_pages, 1);
    setCentralWidget(central);

    /* C2：驾驶舱的「体检结论」接总览页的数据 —— 逐页建完、两个页都就位后才接得上
       （页面表里驾驶舱在总览页前面，建驾驶舱时 m_overview 还是空的）。 */
    if (m_cockpit && m_overview)
        m_cockpit->setOverview(m_overview);

    /* 记住协议工作台在堆栈里的页号，跨工具联动要切到它 */
    m_workbenchPage = m_pages->indexOf(m_workbench);
    m_lookupPage    = m_pages->indexOf(m_lookup);

    /* 分组头占位，映射"行号 → 页号"时要跳过它们 */
    m_rowToPage.clear();
    int page = 0;
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->flags() == Qt::NoItemFlags) {
            m_rowToPage.insert(row, -1);
        } else {
            m_rowToPage.insert(row, page);
            ++page;
        }
    }

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        const int p = m_rowToPage.value(row, -1);
        if (p >= 0) {
            m_pages->setCurrentIndex(p);
            /* 首次切到速查页才装载 4MB 数据（懒加载） */
            if (p == m_lookupPage)
                ensureLookupLoaded();
        }
    });

    /* C1：Ctrl+K 全局搜索 —— 一个框同时搜「页面名 / 检查项 / 手册条目」，回车直达。
       不必先搞清侧栏里各页在哪一栏。 */
    {
        QShortcut *sc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, &ToolboxWindow::showPalette);
    }

    /* D2/A1：首次运行向导 —— 第一次打开弹一次"三步上手"，之后记进 _toolbox.ini 不再弹。
       A1 起不再是"一段文字 + 知道了"，而是**三个真按钮**：点了直接进对应页。
       两个刻意的选择：
         · 用 singleShot 延到事件循环起来之后再弹，不在构造函数里阻塞；
         · 用**非模态**弹窗（而不是 exec）—— 免得 GUI 冒烟或任何"建完窗口就走"的探针
           被一个模态框挂住（冒烟的自动关框计时器是 700ms 后才装的，模态框会卡在前面）。 */
    if (!m_suppressWelcome && shouldShowWelcome()) {
        QTimer::singleShot(0, this, [this]() {
            Settings().setValue(QStringLiteral("ui/welcomeShown"), true);
            QDialog *dlg = new QDialog(this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->setWindowTitle(QStringLiteral("三步上手"));
            dlg->setModal(false);
            QVBoxLayout *wl = new QVBoxLayout(dlg);
            QLabel *wtxt = new QLabel(QStringLiteral(
                "<b>三步就能用起来，点下面的按钮直接去：</b><br>"
                "① 写代码时随手查函数 / 流程　② 开工就用骨架生成　"
                "③ 收尾跑一次体检<br><br>"
                "这条提示只弹这一次；入口随时可在「工程体检总览」页顶部找回。"), dlg);
            wtxt->setTextFormat(Qt::RichText);
            wtxt->setWordWrap(true);
            wl->addWidget(wtxt);
            QHBoxLayout *wbtn = new QHBoxLayout;
            QPushButton *b1 = new QPushButton(QStringLiteral("① 查单片机手册"), dlg);
            QPushButton *b2 = new QPushButton(QStringLiteral("② 生成工程骨架"), dlg);
            QPushButton *b3 = new QPushButton(QStringLiteral("③ 跑全量体检"), dlg);
            QPushButton *b4 = new QPushButton(QStringLiteral("知道了"), dlg);
            b1->setToolTip(QStringLiteral("跳到「单片机手册」，也可随时按 Ctrl+K 全局搜索"));
            b2->setToolTip(QStringLiteral("跳到「工程模板」，生成能直接编译的工程骨架"));
            b3->setToolTip(QStringLiteral("跳到「全量检查」，一次跑完 8 个检查器"));
            wbtn->addWidget(b1);
            wbtn->addWidget(b2);
            wbtn->addWidget(b3);
            wbtn->addStretch(1);
            wbtn->addWidget(b4);
            wl->addLayout(wbtn);
            connect(b1, &QPushButton::clicked, dlg, [this, dlg]() {
                dlg->close();
                jumpToPage(QStringLiteral("单片机手册"));
                ensureLookupLoaded();
            });
            connect(b2, &QPushButton::clicked, dlg, [this, dlg]() {
                dlg->close();
                jumpToPage(QStringLiteral("工程模板"));
            });
            connect(b3, &QPushButton::clicked, dlg, [this, dlg]() {
                dlg->close();
                jumpToPage(QStringLiteral("全量检查"));
            });
            connect(b4, &QPushButton::clicked, dlg, &QDialog::close);
            dlg->show();
        });
    }

    /* 目录解析失败：所有运行器页显式告警，而不是点了运行才报错 */
    if (m_toolsDir.isEmpty()) {
        for (QWidget *w : m_warnTargets) {
            if (auto *p = qobject_cast<ToolRunPage *>(w))
                p->warnMissingToolsDir();
        }
    }

    /* 默认尺寸取"够用"而不是"占满屏幕"（原 1240×820 在 1536×960 屏上占掉八成）。
       同时兜住两种情况：上次存了几何就恢复，但比屏幕大或跑到屏幕外时压回可视区内。 */
    Settings st;
    QScreen *scr = QGuiApplication::primaryScreen();
    const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1280, 800);
    const int maxW = qMax(760, avail.width()  - 40);
    const int maxH = qMax(520, avail.height() - 60);

    if (st.value(QStringLiteral("win/geometry")).isValid())
        st.restoreGeometry(QStringLiteral("win"), this);
    else
        resize(1120, 720);

    if (width() > maxW || height() > maxH)
        resize(qMin(width(), maxW), qMin(height(), maxH));

    move(qBound(avail.left(), x(), qMax(avail.left(), avail.right()  - width())),
         qBound(avail.top(),  y(), qMax(avail.top(),  avail.bottom() - height())));

    int wantPage = st.value(QStringLiteral("win/page"), 0).toInt();
    bool picked  = false;
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_rowToPage.value(row, -1) == wantPage) {
            m_list->setCurrentRow(row);
            picked = true;
            break;
        }
    }
    if (!picked) {
        /* 默认停在第一个可选项（跳过分组头） */
        for (int row = 0; row < m_list->count(); ++row) {
            if (m_rowToPage.value(row, -1) == 0) {
                m_list->setCurrentRow(row);
                break;
            }
        }
    }
}

void ToolboxWindow::closeEvent(QCloseEvent *e)
{
    Settings st;
    st.saveGeometry(QStringLiteral("win"), this);
    st.setValue(QStringLiteral("win/page"), m_pages->currentIndex());
    QMainWindow::closeEvent(e);
}

QWidget *ToolboxWindow::buildKickstartPage()
{
    m_kickstart = new KickStartWindow(this);
    /* C2：起手助手填完答案 → 一键跳到「工程模板」把外设/任务填进去并生成骨架 */
    connect(m_kickstart, &KickStartWindow::scaffoldRequested, this,
            [this](const QString &periph, const QString &tasks, const QStringList &skipped) {
        ScaffoldPage *sp = m_pages->findChild<ScaffoldPage *>();
        if (!sp) {
            QMessageBox::warning(this, QStringLiteral("工程模板"),
                                 QStringLiteral("找不到「工程模板」页。"));
            return;
        }
        const QString out = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择工程骨架的输出目录"), QDir::homePath());
        jumpToPage(QStringLiteral("工程模板"));
        sp->prefillCustom(periph, tasks, out);
        if (!skipped.isEmpty()) {
            QMessageBox::information(
                this, QStringLiteral("有任务行没解析出来"),
                QStringLiteral("这些任务行缺「优先级」或「栈」，已跳过：\n\n%1\n\n"
                               "任务表四列（名称 / 周期或触发 / 优先级 / 栈）缺一不可，"
                               "补齐后可以再点一次。").arg(skipped.join(QStringLiteral("\n"))));
        }
        if (!out.isEmpty())
            sp->generateNow();
    });
    return m_kickstart;
}

QWidget *ToolboxWindow::buildRtosGenPage()
{
    RtosGenPage *p = new RtosGenPage(this);
    /* 仓库根：从工具箱目录上溯到含 EnvMonitor 的那一层，供"用板1/板2配置" */
    if (!m_toolsDir.isEmpty()) {
        QDir d(m_toolsDir);
        for (int i = 0; i < 8; ++i) {
            if (d.exists(QStringLiteral("EnvMonitor"))) {
                p->setEnvMonitorRoot(d.absolutePath());
                break;
            }
            if (!d.cdUp())
                break;
        }
    }
    /* 供「从起手助手导入」读任务表（起手助手先于本页构造，指针已就绪） */
    p->setKickstartProvider([this](const QString &id) {
        return m_kickstart ? m_kickstart->answer(id) : QString();
    });
    return p;
}

QWidget *ToolboxWindow::buildWorkbenchPage()
{
    m_workbench = new WorkbenchWindow(this);
    return m_workbench;
}

/* 单片机手册（原独立程序，现并入本工具箱）。
   数据是「工具箱\速查数据\data\dataset.json」，由网页版那条链导出，这里只读不写。
   构造时**不装载数据**（4MB + 建索引，会让工具箱启动变慢），首次切到本页才装载。 */
QWidget *ToolboxWindow::buildLookupPage()
{
    m_lookup = new MainWindow(this);
    /* 本页的「🌗 主题」切的是全局主题，把设置页那个下拉同步过来 */
    connect(m_lookup, &MainWindow::themeToggled, this, [this]() {
        if (m_setTheme) {
            QSignalBlocker b(m_setTheme);
            m_setTheme->setCurrentIndex(Theme::mode() == Theme::Dark ? 0 : 1);
        }
    });
    return m_lookup;
}

void ToolboxWindow::ensureLookupLoaded()
{
    if (m_lookupLoaded || !m_lookup)
        return;
    m_lookupLoaded = true;
    /* 装载失败时页面自己会把原因显示在详情区，这里不再重复处理 */
    m_lookup->loadData();
}

/* 联动：把 hex 填进协议工作台的解帧页，并把左栏选中项切到它。
   直接操作列表项而不是只切堆栈，左栏高亮才会跟着走。 */
void ToolboxWindow::jumpToWorkbench(const QString &hex)
{
    if (!m_workbench) {
        /* 构造顺序里协议工作台先于链路仿真，正常不会走到这里 */
        return;
    }
    m_workbench->loadIntoParser(hex);
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_rowToPage.value(row, -1) == m_workbenchPage) {
            m_list->setCurrentRow(row);
            break;
        }
    }
    m_pages->setCurrentIndex(m_workbenchPage);
}

/* 按页面标题跳到对应页：体检面板点健康度卡片时用（标题与侧栏列表项一致）。 */
void ToolboxWindow::jumpToPage(const QString &title)
{
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->text() == title) {
            m_list->setCurrentRow(row);
            return;
        }
    }
}

/* D2：首次运行向导该不该弹 —— 只看 _toolbox.ini 里 ui/welcomeShown 是否已置真。
   弹过一次就永远为假（"只弹一次"这条逻辑可被 --welcome-probe 断言）。 */
bool ToolboxWindow::shouldShowWelcome()
{
    return !Settings().value(QStringLiteral("ui/welcomeShown"), false).toBool();
}

/* D3：体检快照按"保留最近 keep 份"的口径（与 OverviewPage::writeSnapshot 的自动滚动一致），
   返回该删的那些（按文件名升序 = 旧→新，所以是列表最前面若干个）。 */
QStringList ToolboxWindow::overdueSnapshots(const QString &dir, int keep)
{
    QDir d(dir);
    const QStringList files = d.entryList(QStringList{QStringLiteral("*.json")},
                                          QDir::Files, QDir::Name);
    QStringList out;
    for (int i = 0; i < files.size() - keep; ++i)
        out << d.filePath(files.at(i));
    return out;
}

/* D3：各检查器的报告产物（跑一次检查就会重新生成，删了也能恢复）。 */
QStringList ToolboxWindow::reportFiles(const QString &toolsDir)
{
    QStringList dirs{
        QStringLiteral("检查/进度跟踪"), QStringLiteral("检查/引脚规划"),
        QStringLiteral("检查/RTOS检查"), QStringLiteral("检查/编码守卫"),
        QStringLiteral("检查/LoRa检查"), QStringLiteral("检查/协议校验"),
        QStringLiteral("检查/断网缓存"),
    };
    QStringList out;
    for (const QString &sub : dirs) {
        QDir d(QDir(toolsDir).filePath(sub));
        for (const QString &f : d.entryList(QStringList{QStringLiteral("*报告.md")}, QDir::Files))
            out << d.filePath(f);
    }
    return out;
}

/* C1：全局搜索的候选。类别用 "类别|文本" 前缀区分，供面板分组显示与回车分流。
   页面名取侧栏所有可选项；检查项取各报告的命中项小节；手册条目查速查数据集。 */
QStringList ToolboxWindow::paletteHits(const QString &query) const
{
    const QString q = query.trimmed();
    QStringList out;

    /* ① 页面名（跳过分组头） */
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *it = m_list->item(row);
        if (!it || it->flags() == Qt::NoItemFlags)
            continue;
        const QString t = it->text();
        if (q.isEmpty() || t.contains(q, Qt::CaseInsensitive))
            out << QStringLiteral("页面|") + t;
    }

    /* ② 检查项：各报告的命中项小节标题（报告没生成时为空，不影响其余两类） */
    if (m_overview) {
        for (const QString &h : m_overview->findingHeadings())
            if (q.isEmpty() || h.contains(q, Qt::CaseInsensitive))
                out << QStringLiteral("检查项|") + h;
    }

    /* ③ 手册条目：查速查数据集（首次调用装载 4MB，之后复用索引）。
       查询为空时不搜 —— 否则会把上千条条目全列出来。 */
    if (!q.isEmpty() && m_lookup) {
        if (!m_lookup->dataset().isLoaded())
            m_lookup->loadData();
        if (SearchEngine *eng = m_lookup->engine()) {
            const Dataset &ds = m_lookup->dataset();
            QStringList seen;
            for (int sec = 0; sec <= int(Card::Build) && out.size() < 40; ++sec) {
                const SearchEngine::Result r = eng->run(q, sec);
                for (int k = 0; k < r.order.size() && out.size() < 40; ++k) {
                    const Card &c = ds.at(r.order.at(k));
                    if (seen.contains(c.n))
                        continue;
                    seen << c.n;
                    out << QStringLiteral("手册|") + c.n;
                }
            }
        }
    }
    return out;
}

/* C1：Ctrl+K 弹出的全局搜索面板。回车直达：页面名切页、检查项定位报告、
   手册条目跳单片机手册并定位该条目。 */
void ToolboxWindow::showPalette()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("全局搜索"));
    dlg.resize(580, 440);
    QVBoxLayout *lay = new QVBoxLayout(&dlg);

    QLineEdit *edit = new QLineEdit(&dlg);
    edit->setPlaceholderText(QStringLiteral("搜「页面名 / 检查项 / 手册条目」，回车直达…"));
    QListWidget *list = new QListWidget(&dlg);
    QLabel *hint = new QLabel(
        QStringLiteral("↑↓ 选择　回车直达　Esc 取消　·　页面=切页，检查项=定位报告，手册=查手册"), &dlg);
    hint->setStyleSheet(QStringLiteral("color:rgba(127,127,127,255);"));
    lay->addWidget(edit);
    lay->addWidget(list, 1);
    lay->addWidget(hint);

    QString chosen;
    auto refill = [&](const QString &q) {
        list->clear();
        for (const QString &hit : paletteHits(q)) {
            const int bar = hit.indexOf(QLatin1Char('|'));
            const QString kind = hit.left(bar);
            const QString text = hit.mid(bar + 1);
            const QString tag = kind == QStringLiteral("页面")   ? QStringLiteral("页面")
                                : kind == QStringLiteral("检查项") ? QStringLiteral("检查项")
                                                                 : QStringLiteral("手册");
            QListWidgetItem *it =
                new QListWidgetItem(QStringLiteral("【%1】%2").arg(tag, text), list);
            it->setData(Qt::UserRole, hit);
        }
        if (list->count() > 0)
            list->setCurrentRow(0);
    };
    auto accept = [&]() {
        if (QListWidgetItem *it = list->currentItem())
            chosen = it->data(Qt::UserRole).toString();
        dlg.accept();
    };
    connect(edit, &QLineEdit::textChanged, &dlg, refill);
    connect(edit, &QLineEdit::returnPressed, &dlg, accept);
    connect(list, &QListWidget::itemActivated, &dlg, [&](QListWidgetItem *) { accept(); });

    /* A4：首次用 Ctrl+K 时手册数据（4MB）还没装载，旧写法会在"边打字边搜"里同步装载、
       界面顿一下。改成：先把框显示出来（页面名 / 检查项立刻可用），再后台装载一次，
       装完按当前输入重列 —— 用户看到的是"正在装载手册数据…"，而不是卡住。 */
    const bool needLoad = m_lookup && !m_lookup->dataset().isLoaded();
    const QString hintDefault =
        QStringLiteral("↑↓ 选择　回车直达　Esc 取消　·　页面=切页，检查项=定位报告，手册=查手册");
    if (needLoad)
        hint->setText(QStringLiteral("正在装载手册数据…（页面名与检查项已可用，稍候手册条目补上）"));

    refill(QString());
    edit->setFocus();
    if (needLoad) {
        QTimer::singleShot(0, &dlg, [this, edit, refill, hint, hintDefault]() {
            ensureLookupLoaded();
            hint->setText(hintDefault);
            refill(edit->text());
        });
    }
    if (dlg.exec() != QDialog::Accepted || chosen.isEmpty())
        return;

    const int bar = chosen.indexOf(QLatin1Char('|'));
    const QString kind = chosen.left(bar);
    const QString text = chosen.mid(bar + 1);
    if (kind == QStringLiteral("页面")) {
        jumpToPage(text);
    } else if (kind == QStringLiteral("检查项")) {
        jumpToPage(QStringLiteral("工程体检总览"));
        if (m_overview)
            m_overview->focusFinding(text);
    } else if (kind == QStringLiteral("手册")) {
        jumpToPage(QStringLiteral("单片机手册"));
        ensureLookupLoaded();
        if (m_lookup)
            m_lookup->jumpToTerm(text);
    }
}

QWidget *ToolboxWindow::buildProgressPage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("进度扫描"),
        QStringLiteral("扫描工程源码里的待办标记与疑似桩函数，生成待实现清单。"
                       "报告写到 工具箱/检查/进度跟踪/进度报告.md。"), this);
    p->setSteps({{QStringLiteral("进度扫描"),
                  py(m_toolsDir, QStringLiteral("检查/进度跟踪/progress_scan.py")), nullptr}});
    p->setReportPath(py(m_toolsDir, QStringLiteral("检查/进度跟踪/进度报告.md")));
    m_warnTargets.append(p);
    return p;
}

QWidget *ToolboxWindow::buildPinPage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("引脚规划"),
        QStringLiteral("查看引脚占用、冲突与中断资源；可对选定的模块做冲突预检。"
                       "报告写到 工具箱/检查/引脚规划/引脚规划报告.md。"), this);

    QComboBox *board = new QComboBox(this);
    board->addItems({QStringLiteral("全部"), QStringLiteral("板1"), QStringLiteral("板2")});
    QLineEdit *use = new QLineEdit(this);
    use->setPlaceholderText(QStringLiteral("模块前缀，如 LORA_E22,W25QXX（留空则不预检）"));
    m_pinBoard = board;                 /* 设置页要能推值过来 */

    /* 选项记忆：下次打开还保持上次的选择 */
    Settings st;
    board->setCurrentIndex(st.value(QStringLiteral("pin/board"), 0).toInt());
    use->setText(st.value(QStringLiteral("pin/use")).toString());
    connect(board, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [board](int i) { Settings().setValue(QStringLiteral("pin/board"), i); });
    connect(use, &QLineEdit::textChanged, this,
            [use](const QString &t) { Settings().setValue(QStringLiteral("pin/use"), t); });

    QHBoxLayout *opt = p->optionsLayout();
    opt->insertWidget(0, new QLabel(QStringLiteral("板"), this));
    opt->insertWidget(1, board);
    opt->insertWidget(2, new QLabel(QStringLiteral("模块"), this));
    opt->insertWidget(3, use);

    p->setSteps({{QStringLiteral("引脚规划"),
                  py(m_toolsDir, QStringLiteral("检查/引脚规划/pin_planner.py")),
                  [board, use]() {
                      QStringList a;
                      if (board->currentIndex() == 1)
                          a << QStringLiteral("--board") << QStringLiteral("1");
                      else if (board->currentIndex() == 2)
                          a << QStringLiteral("--board") << QStringLiteral("2");
                      const QString u = use->text().trimmed();
                      if (!u.isEmpty())
                          a << QStringLiteral("--use") << u;
                      return a;
                  }}});
    p->setReportPath(py(m_toolsDir, QStringLiteral("检查/引脚规划/引脚规划报告.md")));
    m_warnTargets.append(p);
    return p;
}

QWidget *ToolboxWindow::buildRtosPage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("RTOS 检查"),
        QStringLiteral("检查中断优先级、NVIC 分组、ISR 误用、任务栈与堆预算。"
                       "报告写到 工具箱/检查/RTOS检查/RTOS检查报告.md。"), this);
    p->setSteps({{QStringLiteral("RTOS 检查"),
                  py(m_toolsDir, QStringLiteral("检查/RTOS检查/rtos_check.py")), nullptr}});
    p->setReportPath(py(m_toolsDir, QStringLiteral("检查/RTOS检查/RTOS检查报告.md")));
    m_warnTargets.append(p);
    return p;
}

QWidget *ToolboxWindow::buildEncodingPage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("编码守卫"),
        QStringLiteral("检查编码（main.c 为 GBK、其余为 UTF-8）、BOM、乱码，"
                       "并给出 Keil 内存占用快照。报告写到 工具箱/检查/编码守卫/编码守卫报告.md。"), this);

    QCheckBox *eol = new QCheckBox(QStringLiteral("查换行（按 git 索引）"), this);
    eol->setChecked(Settings().value(QStringLiteral("enc/eol"), false).toBool());
    connect(eol, &QCheckBox::toggled, this,
            [](bool on) { Settings().setValue(QStringLiteral("enc/eol"), on); });
    m_encEol = eol;                     /* 设置页要能推值过来 */
    p->optionsLayout()->insertWidget(0, eol);

    p->setSteps({{QStringLiteral("编码守卫"),
                  py(m_toolsDir, QStringLiteral("检查/编码守卫/encoding_guard.py")),
                  [eol]() {
                      QStringList a;
                      if (eol->isChecked())
                          a << QStringLiteral("--eol");
                      return a;
                  }}});
    p->setReportPath(py(m_toolsDir, QStringLiteral("检查/编码守卫/编码守卫报告.md")));
    m_warnTargets.append(p);
    return p;
}

QWidget *ToolboxWindow::buildLinkPage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("链路仿真"),
        QStringLiteral("无硬件时生成与工程一致的字节流，可粘进协议工作台解帧页，"
                       "或经虚拟串口喂给上位机。产物在 工具箱/检查/链路仿真/产物/。\n"
                       "注：Qt 上位机 SerialMonitor 的帧解析 / CRC / 组帧仍是它自己的"
                       "「阶段2 待实现」骨架（源码里就是一堆 TODO，函数体返回空），"
                       "它的 --selftest 1/18、--cmdselftest 崩溃都是这么来的，"
                       "不是本工具箱的回归，也**不在本工具箱的维护范围**"
                       "（要联调请用「协议工作台」或本页的「发送到协议工作台」）。"), this);

    QComboBox *scen = new QComboBox(this);
    scen->addItems({QStringLiteral("normal"), QStringLiteral("lossy"),
                    QStringLiteral("noise"), QStringLiteral("replay")});
    QSpinBox *cnt = new QSpinBox(this);
    cnt->setRange(1, 1000);
    cnt->setValue(20);

    Settings lst;
    scen->setCurrentIndex(lst.value(QStringLiteral("link/scen"), 0).toInt());
    cnt->setValue(lst.value(QStringLiteral("link/count"), 20).toInt());
    m_linkScen = scen;                  /* 设置页要能推值过来 */
    m_linkCount = cnt;
    connect(scen, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [scen](int i) { Settings().setValue(QStringLiteral("link/scen"), i); });
    connect(cnt, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [cnt](int v) { Settings().setValue(QStringLiteral("link/count"), v); });

    QHBoxLayout *opt = p->optionsLayout();
    opt->insertWidget(0, new QLabel(QStringLiteral("场景"), this));
    opt->insertWidget(1, scen);
    opt->insertWidget(2, new QLabel(QStringLiteral("帧数"), this));
    opt->insertWidget(3, cnt);

    p->setSteps({{QStringLiteral("链路仿真"),
                  py(m_toolsDir, QStringLiteral("检查/链路仿真/link_sim.py")),
                  [scen, cnt]() {
                      return QStringList{QStringLiteral("--scenario"), scen->currentText(),
                                         QStringLiteral("--count"), QString::number(cnt->value())};
                  }},
                 /* B1/B2：全链路时序仿真 —— 六个剧本全跑一遍并断言，
                    同时出 SVG 时序图（板1→空中→板2→上位机→云 五条泳道）。
                    放在第二步：第一步出字节流给人看，第二步验链路行为对不对。 */
                 {QStringLiteral("全链路时序"),
                  py(m_toolsDir, QStringLiteral("检查/链路仿真/chain_sim.py")),
                  [this]() {
                      return QStringList{
                          QStringLiteral("--all"), QStringLiteral("--svg"),
                          py(m_toolsDir, QStringLiteral("检查/链路仿真/产物/链路时序.svg"))};
                  }}});
    /* 这一页只出 hex/log，没有 md 报告；按钮文字改成"打开产物目录"，
       路径也要指向真正的产物目录（原来指的是它的上一级 = 标签与行为不符）。 */
    p->setReportPath(py(m_toolsDir, QStringLiteral("检查/链路仿真/产物")));
    p->setReportLabel(QStringLiteral("打开产物目录"));

    /* 联动：把刚生成的 link_<场景>.hex 一键送进协议工作台的解帧页。
       产物与脚本默认输出规则一致（--out 未给时落在 链路仿真/产物/）。 */
    p->addAction(QStringLiteral("发送到协议工作台"), [this, p, scen]() -> QString {
        const QString dir = p->lastWorkingDir().isEmpty()
                                ? py(m_toolsDir, QStringLiteral("检查/链路仿真"))
                                : p->lastWorkingDir();
        const QString hex = QDir(QDir(dir).filePath(QStringLiteral("产物")))
                                .filePath(QStringLiteral("link_%1.hex").arg(scen->currentText()));
        if (!QFileInfo::exists(hex))
            return QStringLiteral("还没生成 link_%1.hex，先点一次「运行」。").arg(scen->currentText());
        QFile f(hex);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QStringLiteral("打不开 %1。").arg(hex);
        const QString text = QString::fromUtf8(f.readAll());
        f.close();
        if (text.trimmed().isEmpty())
            return QStringLiteral("%1 是空的，换个场景或调大帧数再试。").arg(hex);
        jumpToWorkbench(text);
        return QStringLiteral("已送入协议工作台：%1（%2 字符）").arg(hex).arg(text.size());
    });

    m_warnTargets.append(p);
    return p;
}

QWidget *ToolboxWindow::buildLoraPage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("LoRa 检查"),
        QStringLiteral("核查两板 lora_e22.h 的配置一致性、引脚宏成对性、波特率合法性、"
                       "帧间隔合理性。报告写到 工具箱/检查/LoRa检查/LoRa检查报告.md。"), this);
    p->setSteps({{QStringLiteral("LoRa 检查"),
                  py(m_toolsDir, QStringLiteral("检查/LoRa检查/lora_check.py")), nullptr}});
    p->setReportPath(py(m_toolsDir, QStringLiteral("检查/LoRa检查/LoRa检查报告.md")));
    m_warnTargets.append(p);
    return p;
}

/* OTA 台架：把 OTA/tools/ota_send.py 的两种下发模式都包成界面。
   脚本在**仓库根**的 OTA/tools/ 下（在工具箱树之外），所以这里按"从工具箱目录上溯
   找到含 OTA/tools/ota_send.py 的那层"来定位，不把脚本挪进工具箱
   （挪进来要同步三处目录判据：settings.cpp::resolveIniPath、lookup/apppaths.cpp、
   ToolboxWindow::toolsDirPath，收益不抵风险）。
   C2：除了原来的串口直连，再包 TCP server 模式（走 WiFi、板子反连 PC）——
   两个模式只是参数不同（--serial COMx / --tcp-server PORT），一个下拉框切换即可。 */
QWidget *ToolboxWindow::buildOtaPage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("OTA 台架"),
        QStringLiteral(
            "把 OTA/tools/ota_send.py 包成界面，两种模式二选一：\n"
            "  · 【串口直连】USB-TTL 接板子 USART3 —— PB10(TX) 接 USB-TTL 的 RX，"
            "PB11(RX) 接 USB-TTL 的 TX，且【必须共地】；台架调试请先断开 ESP8266。\n"
            "  · 【TCP server】本机先起监听，再给板子上电让 ESP8266 反连（走 WiFi）；"
            "监听端口默认 8899，要与板子固件里配的一致。\n"
            "顺序：① 板子跑在 App（不是 Boot）；② 本页选模式、填固件 bin，点「运行」；"
            "③ 脚本先 QUERY 再 BEGIN，10 秒内没应答就退出并提示检查接线/波特率。\n"
            "发完 END 后板子要擦 64KB 下载扇区，【等约 8 秒别断电】，收到 DONE 才算成功；"
            "END 之后再上电重启，Bootloader 会把新固件搬到运行区。\n"
            "脚本位置：仓库根 OTA/tools/ota_send.py（在工具箱树之外，本页按相对仓库根引用）。"),
        this);

    QComboBox *mode = new QComboBox(this);
    mode->addItem(QStringLiteral("串口直连"), QStringLiteral("serial"));
    mode->addItem(QStringLiteral("TCP server（板子反连）"), QStringLiteral("tcp"));
    /* A5：COM 口改成"可选可输"的下拉 —— 常见口号直接点，特殊口号手输，且记住上次。
       （以前是空白输入框，每次都要自己回忆是 COM 几。） */
    QComboBox *com = new QComboBox(this);
    com->setEditable(true);
    com->addItem(QString());
    for (int i = 1; i <= 8; ++i)
        com->addItem(QStringLiteral("COM%1").arg(i));
    com->setToolTip(QStringLiteral("常见口号直接选；不在列表里就手输（设备管理器里能看到实际口号）"));
    QSpinBox *port = new QSpinBox(this);
    port->setRange(1, 65535);
    port->setValue(8899);
    QLineEdit *bin = new QLineEdit(this);
    bin->setPlaceholderText(QStringLiteral("固件 bin 路径，如 OTA/OTA1/Objects/app.bin"));
    QSpinBox *ver = new QSpinBox(this);
    ver->setRange(1, 65535);
    ver->setValue(1);

    Settings st;
    mode->setCurrentIndex(st.value(QStringLiteral("ota/mode")).toString() == QStringLiteral("tcp") ? 1 : 0);
    com->setCurrentText(st.value(QStringLiteral("ota/com")).toString());
    port->setValue(st.value(QStringLiteral("ota/port"), 8899).toInt());
    bin->setText(st.value(QStringLiteral("ota/bin")).toString());
    ver->setValue(st.value(QStringLiteral("ota/ver"), 1).toInt());
    connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [mode](int) {
        Settings().setValue(QStringLiteral("ota/mode"), mode->currentData().toString());
    });
    connect(com, &QComboBox::currentTextChanged, this,
            [](const QString &t) { Settings().setValue(QStringLiteral("ota/com"), t); });
    connect(port, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int v) { Settings().setValue(QStringLiteral("ota/port"), v); });
    connect(bin, &QLineEdit::textChanged, this,
            [bin](const QString &t) { Settings().setValue(QStringLiteral("ota/bin"), t); });
    connect(ver, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int v) { Settings().setValue(QStringLiteral("ota/ver"), v); });

    /* 模式切换时灰掉用不上的那一个输入框，免得填了 COM 却在 TCP 模式下被忽略 */
    auto syncMode = [mode, com, port]() {
        const bool tcp = mode->currentData().toString() == QStringLiteral("tcp");
        com->setEnabled(!tcp);
        port->setEnabled(tcp);
    };
    connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [syncMode](int) { syncMode(); });
    syncMode();

    /* F2「图形界面大小」：5 组"标签+控件"全塞一行，本页最小宽度 797px，主窗口在
       1024×768 屏上放不下。拆两行：第一行"连哪块板"（模式 / 串口 / 监听端口），
       第二行"发什么"（固件 bin / 版本）。 */
    QHBoxLayout *opt = p->optionsLayout();
    opt->insertWidget(0, new QLabel(QStringLiteral("模式"), this));
    opt->insertWidget(1, mode);
    opt->insertWidget(2, new QLabel(QStringLiteral("串口"), this));
    opt->insertWidget(3, com);
    opt->insertWidget(4, new QLabel(QStringLiteral("监听端口"), this));
    opt->insertWidget(5, port);
    QHBoxLayout *opt2 = p->optionsLayout2();
    opt2->addWidget(new QLabel(QStringLiteral("固件 bin"), this));
    opt2->addWidget(bin, 1);        /* 路径输入框吃掉这一行的剩余宽度 */
    opt2->addWidget(new QLabel(QStringLiteral("版本"), this));
    opt2->addWidget(ver);

    /* 从工具箱目录上溯找 OTA/tools/ota_send.py（repo 根那层） */
    QString script;
    QDir d(m_toolsDir);
    for (int i = 0; i < 8; ++i) {
        const QString cand = d.filePath(QStringLiteral("OTA/tools/ota_send.py"));
        if (QFileInfo::exists(cand)) { script = cand; break; }
        if (!d.cdUp()) break;
    }
    if (script.isEmpty())
        script = py(m_toolsDir, QStringLiteral("../OTA/tools/ota_send.py"));   /* 找不到时留个提示 */

    p->setSteps({{QStringLiteral("OTA 下发"), script,
                  [mode, com, port, bin, ver]() {
                      QStringList a;
                      if (mode->currentData().toString() == QStringLiteral("tcp"))
                          a << QStringLiteral("--tcp-server") << QString::number(port->value());
                      else
                          a << QStringLiteral("--serial") << com->currentText().trimmed();
                      a << QStringLiteral("--bin") << bin->text().trimmed()
                        << QStringLiteral("--ver") << QString::number(ver->value());
                      return a;
                  }}});
    p->setReportPath(QFileInfo(script).absolutePath());   /* 没报告，按钮用来开脚本所在目录 */
    p->setReportLabel(QStringLiteral("打开 OTA 目录"));
    m_warnTargets.append(p);
    return p;
}

QWidget *ToolboxWindow::buildCockpitPage()
{
    /* C2：项目驾驶舱 —— 一屏摆「现在到哪、还差什么、下一步点哪」，并且按钮真能触发。
       四张现场卡走 检查/驾驶舱/cockpit_data.py（一次取数）；「体检结论」接下面的总览页
       （用同一份 DashboardCanvas 判定口径，不另跑一套检查器）。 */
    CockpitPage *p = new CockpitPage(m_toolsDir, this);
    m_cockpit = p;
    connect(p, &CockpitPage::requestPage, this, &ToolboxWindow::jumpToPage);
    return p;
}

QWidget *ToolboxWindow::buildOverviewPage()
{
    OverviewPage *p = new OverviewPage(m_toolsDir, this);
    m_overview = p;      /* C1：全局搜索列的「检查项」取自本页（报告命中项小节） */
    /* 点面板上的健康度卡片 → 跳到对应工具页 */
    connect(p, &OverviewPage::requestPage, this, &ToolboxWindow::jumpToPage);
    /* C1：报告里点了「条目名」→ 跳到「单片机手册」并把那个词搜/定位出来 */
    connect(p, &OverviewPage::requestDoc, this, [this](const QString &term) {
        jumpToPage(QStringLiteral("单片机手册"));
        if (MainWindow *lw = m_pages->findChild<MainWindow *>())
            lw->jumpToTerm(term);
    });
    return p;
}

/* 断网缓存：把 检查/断网缓存/cache_check.py 包成界面。
   缓存几何（槽位 / 扇区 / 分区上界）自动读板2 的 w25qxx_log.h 与 w25qxx.h；
   上传周期 / 补传节奏 / 记录长度按工程实际填（默认值只是示例）。 */
QWidget *ToolboxWindow::buildCachePage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("断网缓存"),
        QStringLiteral(
            "核算板2 W25QXX 滚动日志的两件事：\n"
            "  · 断网能扛多久 —— 容量条数 × 上传周期；\n"
            "  · 补传会不会比写入慢 —— 慢了就会「边补边覆盖」，丢掉还没补上的数据\n"
            "    （就是 w25qxx_log.h 里 W25QXX_LogLost 非 0 的那种现象）。\n"
            "缓存几何（槽位大小 / 扇区数 / 分区上界）自动读板2 FWLIB/inc 的\n"
            "  w25qxx_log.h 与 w25qxx.h，不写死；宏算不出来时会报严重而不是静默通过。\n"
            "下面三个框是「工程事实」，请按实际填 —— 默认值只是示例，填错就白算。\n"
            "报告写到 工具箱/检查/断网缓存/断网缓存报告.md。"),
        this);

    QSpinBox *period = new QSpinBox(this);
    period->setRange(1, 3600000);
    period->setSuffix(QStringLiteral(" ms"));
    QSpinBox *replay = new QSpinBox(this);
    replay->setRange(1, 3600000);
    replay->setSuffix(QStringLiteral(" ms"));
    QSpinBox *rec = new QSpinBox(this);
    rec->setRange(1, 255);
    rec->setSuffix(QStringLiteral(" B"));

    Settings st;
    period->setValue(st.value(QStringLiteral("cache/period"), 1000).toInt());
    replay->setValue(st.value(QStringLiteral("cache/replay"), 50).toInt());
    rec->setValue(st.value(QStringLiteral("cache/record"), 20).toInt());
    connect(period, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int v) { Settings().setValue(QStringLiteral("cache/period"), v); });
    connect(replay, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int v) { Settings().setValue(QStringLiteral("cache/replay"), v); });
    connect(rec, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int v) { Settings().setValue(QStringLiteral("cache/record"), v); });

    QHBoxLayout *opt = p->optionsLayout();
    opt->insertWidget(0, new QLabel(QStringLiteral("上传周期"), this));
    opt->insertWidget(1, period);
    opt->insertWidget(2, new QLabel(QStringLiteral("补传每条"), this));
    opt->insertWidget(3, replay);
    opt->insertWidget(4, new QLabel(QStringLiteral("记录字节"), this));
    opt->insertWidget(5, rec);

    p->setSteps({{QStringLiteral("断网缓存"),
                  py(m_toolsDir, QStringLiteral("检查/断网缓存/cache_check.py")),
                  [period, replay, rec]() {
                      return QStringList{
                          QStringLiteral("--period-ms"), QString::number(period->value()),
                          QStringLiteral("--replay-ms"), QString::number(replay->value()),
                          QStringLiteral("--record-bytes"), QString::number(rec->value())};
                  }}});
    p->setReportPath(py(m_toolsDir, QStringLiteral("检查/断网缓存/断网缓存报告.md")));
    m_warnTargets.append(p);
    return p;
}

QWidget *ToolboxWindow::buildScaffoldPage()
{
    ScaffoldPage *p = new ScaffoldPage(m_toolsDir, this);
    m_warnTargets.append(p);
    return p;
}

/* D2：插件页 = 一个 ToolRunPage 包住 check.py。标题/说明/报告路径都从 manifest 来，
   所以加检查器只写一个 py 文件 + 一个 --manifest，**不用改这里**。 */
QWidget *ToolboxWindow::buildPluginPage(const QString &script)
{
    const QJsonObject m = pluginManifest(script);   /* 发现阶段已读过 → 命中缓存，不再起 python */
    const QString name = m.value(QStringLiteral("name")).toString().trimmed();
    QString desc = m.value(QStringLiteral("desc")).toString();
    if (desc.isEmpty())
        desc = QStringLiteral("检查器插件（无 manifest 说明）");
    desc += QStringLiteral("\n\n插件脚本：") + script;

    ToolRunPage *p = new ToolRunPage(name, desc, this);
    p->setSteps({{name, script, []() { return QStringList(); }}});
    const QString rep = m.value(QStringLiteral("report")).toString();
    if (!rep.isEmpty())
        p->setReportPath(QDir(QFileInfo(script).absolutePath()).filePath(rep));
    return p;
}

QWidget *ToolboxWindow::buildBuildFlashPage()
{
    BuildFlashPage *p = new BuildFlashPage(m_toolsDir, this);
    m_warnTargets.append(p);
    return p;
}

QWidget *ToolboxWindow::buildFullCheckPage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("全量检查"),
        QStringLiteral("依次运行各命令行检查（编码守卫 / RTOS / 引脚规划 / LoRa / 协议校验 / "
                       "断网缓存 / 进度扫描 / 链路仿真自检），输出合并显示。"
                       "写完代码收尾前跑一遍。"), this);

    QVector<RunStep> steps;
    steps << RunStep{QStringLiteral("编码守卫"),
                     py(m_toolsDir, QStringLiteral("检查/编码守卫/encoding_guard.py")), nullptr};
    steps << RunStep{QStringLiteral("RTOS 检查"),
                     py(m_toolsDir, QStringLiteral("检查/RTOS检查/rtos_check.py")), nullptr};
    steps << RunStep{QStringLiteral("引脚规划"),
                     py(m_toolsDir, QStringLiteral("检查/引脚规划/pin_planner.py")), nullptr};
    steps << RunStep{QStringLiteral("LoRa 检查"),
                     py(m_toolsDir, QStringLiteral("检查/LoRa检查/lora_check.py")), nullptr};
    steps << RunStep{QStringLiteral("协议校验"),
                     py(m_toolsDir, QStringLiteral("检查/协议校验/protocol_check.py")), nullptr};
    /* 断网缓存不带参数 = 用默认的上传周期/补传节奏做核算（要按实测改就在页内填） */
    steps << RunStep{QStringLiteral("断网缓存"),
                     py(m_toolsDir, QStringLiteral("检查/断网缓存/cache_check.py")), nullptr};
    steps << RunStep{QStringLiteral("进度扫描"),
                     py(m_toolsDir, QStringLiteral("检查/进度跟踪/progress_scan.py")),
                     []() { return QStringList{QStringLiteral("--quiet")}; }};
    steps << RunStep{QStringLiteral("链路仿真自检"),
                     py(m_toolsDir, QStringLiteral("检查/链路仿真/link_sim.py")),
                     []() { return QStringList{QStringLiteral("--selftest")}; }};
    p->setSteps(steps);
    p->setParallel(true);   /* 八个检查器互不依赖：并行跑，总耗时 ≈ 最慢一个 */
    /* A3：跑完自动让总览页取数并归档一份体检快照（等同于点一次「刷新」），
       并在输出末尾告诉你去哪看 —— 免得跑完还要自己想起来去面板刷新。 */
    p->setFinishedHook([this]() {
        if (m_overview)
            QMetaObject::invokeMethod(m_overview, "refresh");
        return QStringLiteral("已取数并归档本轮体检快照 —— 去「汇总 → 工程体检总览」看健康度"
                              "（点健康度卡片可直达报告里对应那一节）。");
    });
    m_warnTargets.append(p);
    return p;
}

/* =====================================================================
 *  设置页
 *  ---------------------------------------------------------------------
 *  把原先散在各处的个性化选项收拢到一处（侧栏不再挂「主题」「帮助」两个常驻按钮）。
 *
 *  关键约定：这里的控件与各页自己的选项控件读写**同一份** `_toolbox.ini` 键，不做第二套配置。
 *    - 在设置里改 → 写键 + applyPrefsToPages() 立刻推给对应页，不用重启；
 *    - 在页面上改 → 打开设置页时从 Settings 重读，显示的就是新值。
 *  这样"默认值不变"是自然成立的：默认仍取自同一批键，只是多了个集中编辑的地方。
 * ===================================================================== */
QWidget *ToolboxWindow::buildSettingsPage()
{
    /* F2「图形界面大小」：设置页是一长条分组表单，直接堆在页里会把主窗口的最小高度
       顶到 912px —— 1366×768 的笔记本上窗口都塞不进屏幕。改成"外层 9px 边距 + 里层滚动"，
       与「RTOS 骨架生成」页同一套做法：页边距/间距口径不变，最小高度交给滚动区。 */
    QWidget *w = new QWidget;
    QVBoxLayout *outer = new QVBoxLayout(w);
    outer->setContentsMargins(9, 9, 9, 9);   /* F2：与其余页统一（原 18/16，全界面唯一的例外） */
    outer->setSpacing(6);                    /* F2：与其余页统一（原 12） */

    QScrollArea *sa = new QScrollArea(w);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    QWidget *inner = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, 0, 0);     /* 边距已由外层给，这里别再叠一层 */
    lay->setSpacing(6);
    sa->setWidget(inner);
    outer->addWidget(sa);

    QLabel *title = new QLabel(QStringLiteral("设置"));
    QFont tf = title->font();
    tf.setPointSize(tf.pointSize() + 4);
    tf.setBold(true);
    title->setFont(tf);
    lay->addWidget(title);

    QLabel *sub = new QLabel(QStringLiteral(
        "这里就是各页里那几项，改完立刻生效，并记在 _toolbox.ini（换机器拷走工具箱目录会一起带走）。"));
    sub->setWordWrap(true);
    sub->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    lay->addWidget(sub);

    Settings st;

    /* ---- 外观 ---- */
    QGroupBox *g1 = new QGroupBox(QStringLiteral("外观"));
    QFormLayout *f1 = new QFormLayout(g1);
    m_setTheme = new QComboBox;
    m_setTheme->addItems({QStringLiteral("深色"), QStringLiteral("浅色")});
    m_setTheme->setCurrentIndex(Theme::mode() == Theme::Dark ? 0 : 1);
    connect(m_setTheme, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [](int i) {
        Theme::setMode(i == 0 ? Theme::Dark : Theme::Light);
    });
    f1->addRow(QStringLiteral("主题"), m_setTheme);
    lay->addWidget(g1);

    /* ---- 默认选项（与各页共享同一批键） ---- */
    QGroupBox *g2 = new QGroupBox(QStringLiteral("默认选项"));
    QFormLayout *f2 = new QFormLayout(g2);

    m_setPinBoard = new QComboBox;
    m_setPinBoard->addItems({QStringLiteral("全部"), QStringLiteral("板1"), QStringLiteral("板2")});
    m_setPinBoard->setCurrentIndex(st.value(QStringLiteral("pin/board"), 0).toInt());
    connect(m_setPinBoard, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        Settings().setValue(QStringLiteral("pin/board"), i);
        applyPrefsToPages();
    });
    f2->addRow(QStringLiteral("引脚规划 · 默认板卡"), m_setPinBoard);

    m_setLinkScen = new QComboBox;
    m_setLinkScen->addItems({QStringLiteral("normal"), QStringLiteral("lossy"),
                             QStringLiteral("noise"), QStringLiteral("replay")});
    m_setLinkScen->setCurrentIndex(st.value(QStringLiteral("link/scen"), 0).toInt());
    connect(m_setLinkScen, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        Settings().setValue(QStringLiteral("link/scen"), i);
        applyPrefsToPages();
    });
    f2->addRow(QStringLiteral("链路仿真 · 默认场景"), m_setLinkScen);

    m_setLinkCnt = new QSpinBox;
    m_setLinkCnt->setRange(1, 1000);
    m_setLinkCnt->setValue(st.value(QStringLiteral("link/count"), 20).toInt());
    connect(m_setLinkCnt, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        Settings().setValue(QStringLiteral("link/count"), v);
        applyPrefsToPages();
    });
    f2->addRow(QStringLiteral("链路仿真 · 默认帧数"), m_setLinkCnt);

    m_setEncEol = new QCheckBox(QStringLiteral("检查换行（按 git 索引）"));
    m_setEncEol->setChecked(st.value(QStringLiteral("enc/eol"), false).toBool());
    connect(m_setEncEol, &QCheckBox::toggled, this, [this](bool on) {
        Settings().setValue(QStringLiteral("enc/eol"), on);
        applyPrefsToPages();
    });
    f2->addRow(QStringLiteral("编码守卫 · 默认项"), m_setEncEol);

    lay->addWidget(g2);

    /* ---- 状态文件 ---- */
    QGroupBox *g3 = new QGroupBox(QStringLiteral("状态文件"));
    QVBoxLayout *v3 = new QVBoxLayout(g3);
    QLabel *pth = new QLabel(QStringLiteral("界面状态（主题、默认选项、窗口位置）记在这个文件里：<br><code>%1</code>")
                                 .arg(Settings().filePath().toHtmlEscaped()));
    pth->setWordWrap(true);
    pth->setTextFormat(Qt::RichText);
    v3->addWidget(pth);

    QHBoxLayout *b3 = new QHBoxLayout;
    QPushButton *openDir = new QPushButton(QStringLiteral("打开所在文件夹"));
    QPushButton *reset   = new QPushButton(QStringLiteral("恢复默认选项"));
    b3->addWidget(openDir);
    b3->addWidget(reset);
    b3->addStretch(1);
    v3->addLayout(b3);

    connect(openDir, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(Settings().filePath()).absolutePath()));
    });
    connect(reset, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("恢复默认"),
                QStringLiteral("把主题与各项默认选项恢复为初始值？\n（只动这几项，不影响窗口位置与速查的收藏/进度）"))
            != QMessageBox::Yes)
            return;
        Settings s;
        s.setValue(QStringLiteral("pin/board"), 0);
        s.setValue(QStringLiteral("link/scen"), 0);
        s.setValue(QStringLiteral("link/count"), 20);
        s.setValue(QStringLiteral("enc/eol"), false);
        Theme::setMode(Theme::Dark);
        /* 控件回到默认值 */
        QSignalBlocker b1(m_setTheme), b2(m_setPinBoard), b3(m_setLinkScen),
                      b4(m_setLinkCnt), b5(m_setEncEol);
        m_setTheme->setCurrentIndex(0);
        m_setPinBoard->setCurrentIndex(0);
        m_setLinkScen->setCurrentIndex(0);
        m_setLinkCnt->setValue(20);
        m_setEncEol->setChecked(false);
        applyPrefsToPages();
        QMessageBox::information(this, QStringLiteral("恢复默认"), QStringLiteral("已恢复。"));
    });
    lay->addWidget(g3);

    /* ---- D3 清理：过期体检快照 / 检查产物 ---- */
    QGroupBox *g5 = new QGroupBox(QStringLiteral("清理"));
    QVBoxLayout *v5 = new QVBoxLayout(g5);
    QLabel *clrHint = new QLabel(QStringLiteral(
        "<b>体检快照</b>：每轮「刷新」在 <code>_体检历史/</code> 存一份，"
        "自动只保留最近 <b>20</b> 份（每次刷新时滚动丢弃更旧的）。这里手动清一次同口径。<br>"
        "<b>检查产物</b>：各检查器写的 <code>*报告.md</code>（跑一次检查就会重新生成）——"
        "删掉后总览面板的「报告原文」会显示「还没生成」，重跑检查即可恢复。"));
    clrHint->setWordWrap(true);
    clrHint->setTextFormat(Qt::RichText);
    v5->addWidget(clrHint);
    QHBoxLayout *b5 = new QHBoxLayout;
    QPushButton *clrSnap = new QPushButton(QStringLiteral("清理过期体检快照（保留最近 20 份）"), g5);
    QPushButton *clrRep  = new QPushButton(QStringLiteral("清理检查产物（报告 .md）"), g5);
    b5->addWidget(clrSnap);
    b5->addWidget(clrRep);
    b5->addStretch(1);
    v5->addLayout(b5);
    lay->addWidget(g5);

    connect(clrSnap, &QPushButton::clicked, this, [this]() {
        const QString dir = QDir(m_toolsDir).filePath(QStringLiteral("_体检历史"));
        const QStringList old = overdueSnapshots(dir, 20);
        if (old.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("体检快照"),
                QStringLiteral("不超过保留上限 20 份，无需清理。"));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("清理过期体检快照"),
                QStringLiteral("删掉最早的 %1 份快照（保留最近 20 份）？\n目录：%2")
                    .arg(old.size()).arg(dir)) != QMessageBox::Yes)
            return;
        int done = 0;
        for (const QString &p : old)
            if (QFile::remove(p))
                ++done;
        QMessageBox::information(this, QStringLiteral("清理完成"),
            QStringLiteral("已删除 %1 份过期快照（保留最近 20 份）。").arg(done));
    });
    connect(clrRep, &QPushButton::clicked, this, [this]() {
        const QStringList reps = reportFiles(m_toolsDir);
        if (reps.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("检查产物"),
                QStringLiteral("没有找到报告 .md，无需清理。"));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("清理检查产物"),
                QStringLiteral("删掉 %1 份报告 .md？\n（跑一次检查就会重新生成；"
                               "删后总览「报告原文」会显示「还没生成」）").arg(reps.size()))
            != QMessageBox::Yes)
            return;
        int done = 0;
        for (const QString &p : reps)
            if (QFile::remove(p))
                ++done;
        QMessageBox::information(this, QStringLiteral("清理完成"),
            QStringLiteral("已删除 %1 份报告。").arg(done));
    });

    /* ---- 帮助 / 快捷键 ---- */
    QGroupBox *g4 = new QGroupBox(QStringLiteral("帮助 / 快捷键"));
    QVBoxLayout *v4 = new QVBoxLayout(g4);
    QLabel *help = new QLabel(QStringLiteral(
        "<b>建议顺序</b><br>"
        "0) 单片机手册：写代码时随手查函数 / 流程 / 引导（首次进页才装载数据）<br>"
        "1) 项目起手助手：先想清要做什么<br>"
        "2) RTOS 骨架生成：读配置 → 选场景 → 生成 .h/.c<br>"
        "3) 写代码时随时点各项检查<br>"
        "4) 全量检查：一键跑完八个命令行检查<br><br>"
        "<b>快捷键</b><br>"
        "Ctrl+K 全局搜索（页面名 / 检查项 / 手册条目，回车直达）<br>"
        "Ctrl+R 运行当前工具　Ctrl+O 打开报告<br>"
        "输出区查找框回车跳到下一处<br><br>"
        "<b>跨工具联动</b><br>"
        "链路仿真页生成后点「发送到协议工作台」，可直接进解帧页。"));
    help->setWordWrap(true);
    help->setTextFormat(Qt::RichText);
    v4->addWidget(help);
    lay->addWidget(g4);

    lay->addStretch(1);
    return w;
}

/* 设置页改完立刻推给各页。挡信号：这些控件自己也连着"写回 Settings"，
   不挡会形成 设置→页→Settings→… 的回环。 */
void ToolboxWindow::applyPrefsToPages()
{
    Settings st;
    if (m_pinBoard) {
        QSignalBlocker b(m_pinBoard);
        m_pinBoard->setCurrentIndex(st.value(QStringLiteral("pin/board"), 0).toInt());
    }
    if (m_linkScen) {
        QSignalBlocker b(m_linkScen);
        m_linkScen->setCurrentIndex(st.value(QStringLiteral("link/scen"), 0).toInt());
    }
    if (m_linkCount) {
        QSignalBlocker b(m_linkCount);
        m_linkCount->setValue(st.value(QStringLiteral("link/count"), 20).toInt());
    }
    if (m_encEol) {
        QSignalBlocker b(m_encEol);
        m_encEol->setChecked(st.value(QStringLiteral("enc/eol"), false).toBool());
    }
}
