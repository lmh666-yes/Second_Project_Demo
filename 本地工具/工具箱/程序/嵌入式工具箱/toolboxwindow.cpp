#include "toolboxwindow.h"

#include "kickstartwindow.h"
#include "mainwindow.h"
#include "overviewpage.h"
#include "rtosgenpage.h"
#include "scaffoldpage.h"
#include "settings.h"
#include "theme.h"
#include "workbenchwindow.h"
#include "toolrunpage.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
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
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
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

ToolboxWindow::ToolboxWindow(QWidget *parent)
    : QMainWindow(parent)
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

    struct Item { const char *name; QWidget *page; };
    const Item lookup[] = {
        {"单片机手册",   buildLookupPage()},
    };
    const Item design[] = {
        {"项目起手助手",  buildKickstartPage()},
        {"RTOS 骨架生成", buildRtosGenPage()},
        {"协议工作台",    buildWorkbenchPage()},
    };
    const Item check[] = {
        {"进度扫描",     buildProgressPage()},
        {"引脚规划",     buildPinPage()},
        {"RTOS 检查",    buildRtosPage()},
        {"编码守卫",     buildEncodingPage()},
        {"链路仿真",     buildLinkPage()},
        {"LoRa 检查",    buildLoraPage()},
        {"OTA 台架",     buildOtaPage()},
        {"固件预算",     buildFirmwareBudgetPage()},
        {"断网缓存",     buildCachePage()},
        {"工程模板",     buildScaffoldPage()},
    };
    const Item wrap[] = {
        {"工程体检总览", buildOverviewPage()},
        {"全量检查",     buildFullCheckPage()},
    };
    const Item prefs[] = {
        {"设置",         buildSettingsPage()},
    };

    addHeader(QStringLiteral("查资料"));
    for (const Item &it : lookup) {
        m_list->addItem(QString::fromUtf8(it.name));
        m_pages->addWidget(it.page);
    }
    addHeader(QStringLiteral("设计"));
    for (const Item &it : design) {
        m_list->addItem(QString::fromUtf8(it.name));
        m_pages->addWidget(it.page);
    }
    addHeader(QStringLiteral("检查"));
    for (const Item &it : check) {
        m_list->addItem(QString::fromUtf8(it.name));
        m_pages->addWidget(it.page);
    }
    addHeader(QStringLiteral("汇总"));
    for (const Item &it : wrap) {
        m_list->addItem(QString::fromUtf8(it.name));
        m_pages->addWidget(it.page);
    }
    addHeader(QStringLiteral("设置"));
    for (const Item &it : prefs) {
        m_list->addItem(QString::fromUtf8(it.name));
        m_pages->addWidget(it.page);
    }

    sidelay->addWidget(m_list, 1);
    root->addWidget(side);
    root->addWidget(m_pages, 1);
    setCentralWidget(central);

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
                       "或经虚拟串口喂给上位机。产物在 工具箱/检查/链路仿真/产物/。"), this);

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

/* OTA 台架：把 OTA/tools/ota_send.py 的串口直连模式包成界面。
   脚本在**仓库根**的 OTA/tools/ 下（在工具箱树之外），所以这里按"从工具箱目录上溯
   找到含 OTA/tools/ota_send.py 的那层"来定位，不把脚本挪进工具箱
   （挪进来要同步三处目录判据：settings.cpp::resolveIniPath、lookup/apppaths.cpp、
   ToolboxWindow::toolsDirPath，收益不抵风险）。
   TCP server 模式不包：那条路是"先起监听 → 再给板子上电让 ESP8266 反连"，
   顺序卡死在命令行里，做成按钮反而更绕，说明里留一行直接给命令。 */
QWidget *ToolboxWindow::buildOtaPage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("OTA 台架"),
        QStringLiteral(
            "把 OTA/tools/ota_send.py 的【串口直连模式】包成界面（不经 WiFi）。\n"
            "接线：USB-TTL 接板子 USART3 —— PB10(TX) 接 USB-TTL 的 RX，PB11(RX) 接 USB-TTL 的 TX，"
            "而且【必须共地】。台架调试请先断开 ESP8266，别让两个发送源打架。\n"
            "顺序：① 板子跑在 App（不是 Boot）；② 本页填 COM 口与固件 bin，点「运行」；"
            "③ 脚本先 QUERY 再 BEGIN，10 秒内没应答就退出并提示检查接线/波特率。\n"
            "发完 END 后板子要擦 64KB 下载扇区，【等约 8 秒别断电】，收到 DONE 才算成功；"
            "END 之后再上电重启，Bootloader 会把新固件搬到运行区。\n"
            "TCP server 模式（走 WiFi、板子反连 PC）本页包不住：请直接在仓库根跑 "
            "python OTA/tools/ota_send.py --tcp-server 8899 --bin <bin> --ver <n>。\n"
            "脚本位置：仓库根 OTA/tools/ota_send.py（在工具箱树之外，本页按相对仓库根引用）。"),
        this);

    QLineEdit *com = new QLineEdit(this);
    com->setPlaceholderText(QStringLiteral("COM 口，如 COM7"));
    QLineEdit *bin = new QLineEdit(this);
    bin->setPlaceholderText(QStringLiteral("固件 bin 路径，如 OTA/OTA1/Objects/app.bin"));
    QSpinBox *ver = new QSpinBox(this);
    ver->setRange(1, 65535);
    ver->setValue(1);

    Settings st;
    com->setText(st.value(QStringLiteral("ota/com")).toString());
    bin->setText(st.value(QStringLiteral("ota/bin")).toString());
    ver->setValue(st.value(QStringLiteral("ota/ver"), 1).toInt());
    connect(com, &QLineEdit::textChanged, this,
            [com](const QString &t) { Settings().setValue(QStringLiteral("ota/com"), t); });
    connect(bin, &QLineEdit::textChanged, this,
            [bin](const QString &t) { Settings().setValue(QStringLiteral("ota/bin"), t); });
    connect(ver, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int v) { Settings().setValue(QStringLiteral("ota/ver"), v); });

    QHBoxLayout *opt = p->optionsLayout();
    opt->insertWidget(0, new QLabel(QStringLiteral("串口"), this));
    opt->insertWidget(1, com);
    opt->insertWidget(2, new QLabel(QStringLiteral("固件 bin"), this));
    opt->insertWidget(3, bin);
    opt->insertWidget(4, new QLabel(QStringLiteral("版本"), this));
    opt->insertWidget(5, ver);

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

    p->setSteps({{QStringLiteral("OTA 下发（串口直连）"), script,
                  [com, bin, ver]() {
                      return QStringList{QStringLiteral("--serial"), com->text().trimmed(),
                                         QStringLiteral("--bin"), bin->text().trimmed(),
                                         QStringLiteral("--ver"), QString::number(ver->value())};
                  }}});
    p->setReportPath(QFileInfo(script).absolutePath());   /* 没报告，按钮用来开脚本所在目录 */
    p->setReportLabel(QStringLiteral("打开 OTA 目录"));
    m_warnTargets.append(p);
    return p;
}

QWidget *ToolboxWindow::buildOverviewPage()
{
    OverviewPage *p = new OverviewPage(m_toolsDir, this);
    /* 点面板上的健康度卡片 → 跳到对应工具页 */
    connect(p, &OverviewPage::requestPage, this, &ToolboxWindow::jumpToPage);
    return p;
}

/* 固件预算：把 检查/固件预算/firmware_budget.py 包成界面。
   一个文件输入框按扩展名自动选模式（.map/.bin/.hex），留空则跑"分区表自检"。
   与 OTA 台架的分工：台架负责"把固件推下去"，本页负责"推之前先算账/复核头"。 */
QWidget *ToolboxWindow::buildFirmwareBudgetPage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("固件预算"),
        QStringLiteral(
            "核 OTA 分区与固件体积。填一个文件就按扩展名自动选模式：\n"
            "  · .map  Keil 链接 map：取 Code / RO / RW / ZI 与 Flash 合计，对照分区上限判越界；\n"
            "  · .bin  裸固件：算 CRC16 并按 ota_part.h 生成 16 字节固件头，输出可直接下发的包；\n"
            "  · .hex  Intel HEX：判地址范围是否落在预期分区（防刷过界、防盖掉 Boot）。\n"
            "留空则只做分区表自检（Boot/App/下载区/参数区不重叠、下载区 ≥ App、镜像上限 ≤ App）——"
            "「全量检查」跑的就是这一项。\n"
            "分区常量取自仓库根 OTA/OTA1/ota/ota_part.h（唯一事实来源），本工具不写死。\n"
            "文件可给绝对路径，也可给相对仓库根的路径（如 OTA/OTA1/Objects/标准模板.map）。\n"
            "报告写到 工具箱/检查/固件预算/固件预算报告.md。"),
        this);

    QLineEdit *file = new QLineEdit(this);
    file->setPlaceholderText(QStringLiteral("可选：.map / .bin / .hex；留空则只做分区表自检"));
    QComboBox *part = new QComboBox(this);
    part->addItems({QStringLiteral("App 运行区"), QStringLiteral("Boot 区"), QStringLiteral("下载区")});

    Settings st;
    file->setText(st.value(QStringLiteral("fwb/file")).toString());
    part->setCurrentIndex(st.value(QStringLiteral("fwb/part"), 0).toInt());
    connect(file, &QLineEdit::textChanged, this,
            [file](const QString &t) { Settings().setValue(QStringLiteral("fwb/file"), t); });
    connect(part, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [](int i) { Settings().setValue(QStringLiteral("fwb/part"), i); });

    QHBoxLayout *opt = p->optionsLayout();
    opt->insertWidget(0, new QLabel(QStringLiteral("文件"), this));
    opt->insertWidget(1, file);
    opt->insertWidget(2, new QLabel(QStringLiteral("分区"), this));
    opt->insertWidget(3, part);

    p->setSteps({{QStringLiteral("固件预算"),
                  py(m_toolsDir, QStringLiteral("检查/固件预算/firmware_budget.py")),
                  [file, part]() {
                      QStringList a;
                      const QString f = file->text().trimmed();
                      const QString low = f.toLower();
                      if (low.endsWith(QStringLiteral(".map")))
                          a << QStringLiteral("--map") << f;
                      else if (low.endsWith(QStringLiteral(".hex")))
                          a << QStringLiteral("--hex") << f;
                      else if (low.endsWith(QStringLiteral(".bin")))
                          a << QStringLiteral("--bin") << f;
                      if (part->currentIndex() == 1)
                          a << QStringLiteral("--part") << QStringLiteral("boot");
                      else if (part->currentIndex() == 2)
                          a << QStringLiteral("--part") << QStringLiteral("dl");
                      return a;
                  }}});
    p->setReportPath(py(m_toolsDir, QStringLiteral("检查/固件预算/固件预算报告.md")));
    m_warnTargets.append(p);
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

QWidget *ToolboxWindow::buildFullCheckPage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("全量检查"),
        QStringLiteral("依次运行各命令行检查（编码守卫 / RTOS / 引脚规划 / LoRa / 协议校验 / "
                       "固件预算 / 断网缓存 / 进度扫描 / 链路仿真自检），输出合并显示。"
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
    /* 固件预算不带参数 = 只做分区表自检（无输入也能跑，成果照样落报告） */
    steps << RunStep{QStringLiteral("固件预算"),
                     py(m_toolsDir, QStringLiteral("检查/固件预算/firmware_budget.py")), nullptr};
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
    QWidget *w = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(w);
    lay->setContentsMargins(18, 16, 18, 16);
    lay->setSpacing(12);

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

    /* ---- 帮助 / 快捷键 ---- */
    QGroupBox *g4 = new QGroupBox(QStringLiteral("帮助 / 快捷键"));
    QVBoxLayout *v4 = new QVBoxLayout(g4);
    QLabel *help = new QLabel(QStringLiteral(
        "<b>建议顺序</b><br>"
        "0) 单片机手册：写代码时随手查函数 / 流程 / 引导（首次进页才装载数据）<br>"
        "1) 项目起手助手：先想清要做什么<br>"
        "2) RTOS 骨架生成：读配置 → 选场景 → 生成 .h/.c<br>"
        "3) 写代码时随时点各项检查<br>"
        "4) 全量检查：一键跑完五个命令行检查<br><br>"
        "<b>快捷键</b><br>"
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
