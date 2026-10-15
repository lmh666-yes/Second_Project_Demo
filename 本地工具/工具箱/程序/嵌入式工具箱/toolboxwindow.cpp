#include "toolboxwindow.h"

#include "kickstartwindow.h"
#include "mainwindow.h"
#include "overviewpage.h"
#include "rtosgenpage.h"
#include "settings.h"
#include "theme.h"
#include "workbenchwindow.h"
#include "toolrunpage.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QStackedWidget>
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

    m_themeBtn = new QPushButton(side);
    m_themeBtn->setText(QStringLiteral("主题：") + Theme::modeName());
    m_themeBtn->setToolTip(QStringLiteral("深色 / 浅色互切，选择记在 _toolbox.ini"));
    connect(m_themeBtn, &QPushButton::clicked, this, [this]() {
        Theme::toggle();
        m_themeBtn->setText(QStringLiteral("主题：") + Theme::modeName());
    });
    sidelay->addWidget(m_themeBtn);

    QPushButton *helpBtn = new QPushButton(QStringLiteral("帮助 / 快捷键"), side);
    connect(helpBtn, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(
            this, QStringLiteral("帮助"),
            QStringLiteral(
                "<b>建议顺序</b><br>"
                "0) STM32 速查：写代码时随手查函数 / 流程 / 引导（首次进页才装载数据）<br>"
                "1) 项目起手助手：先想清要做什么<br>"
                "2) RTOS 骨架生成：读配置 → 选场景 → 生成 .h/.c<br>"
                "3) 写代码时随时点各项检查<br>"
                "4) 全量检查：一键跑完五个命令行检查<br><br>"
                "<b>快捷键</b><br>"
                "Ctrl+R 运行当前工具　Ctrl+O 打开报告<br>"
                "输出区查找框回车跳到下一处<br><br>"
                "<b>主题</b><br>侧栏「主题」按钮切换深色 / 浅色，记在 _toolbox.ini。<br><br>"
                "<b>跨工具联动</b><br>链路仿真页生成后点「发送到协议工作台」，可直接进解帧页。"));
    });
    sidelay->addWidget(helpBtn);

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
        {"STM32 速查",   buildLookupPage()},
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
    };
    const Item wrap[] = {
        {"工程体检总览", buildOverviewPage()},
        {"全量检查",     buildFullCheckPage()},
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

/* STM32 速查（原独立程序，现并入本工具箱）。
   数据是「工具箱\速查数据\data\dataset.json」，由网页版那条链导出，这里只读不写。
   构造时**不装载数据**（4MB + 建索引，会让工具箱启动变慢），首次切到本页才装载。 */
QWidget *ToolboxWindow::buildLookupPage()
{
    m_lookup = new MainWindow(this);
    /* 本页的「🌗 主题」切的是全局主题，同步侧栏按钮文字 */
    connect(m_lookup, &MainWindow::themeToggled, this, [this]() {
        if (m_themeBtn)
            m_themeBtn->setText(QStringLiteral("主题：") + Theme::modeName());
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

QWidget *ToolboxWindow::buildOverviewPage()
{
    return new OverviewPage(m_toolsDir, this);
}

QWidget *ToolboxWindow::buildFullCheckPage()
{
    ToolRunPage *p = new ToolRunPage(
        QStringLiteral("全量检查"),
        QStringLiteral("依次运行五个命令行检查，输出合并显示。写完代码收尾前跑一遍。"), this);

    QVector<RunStep> steps;
    steps << RunStep{QStringLiteral("编码守卫"),
                     py(m_toolsDir, QStringLiteral("检查/编码守卫/encoding_guard.py")), nullptr};
    steps << RunStep{QStringLiteral("RTOS 检查"),
                     py(m_toolsDir, QStringLiteral("检查/RTOS检查/rtos_check.py")), nullptr};
    steps << RunStep{QStringLiteral("引脚规划"),
                     py(m_toolsDir, QStringLiteral("检查/引脚规划/pin_planner.py")), nullptr};
    steps << RunStep{QStringLiteral("进度扫描"),
                     py(m_toolsDir, QStringLiteral("检查/进度跟踪/progress_scan.py")),
                     []() { return QStringList{QStringLiteral("--quiet")}; }};
    steps << RunStep{QStringLiteral("链路仿真自检"),
                     py(m_toolsDir, QStringLiteral("检查/链路仿真/link_sim.py")),
                     []() { return QStringList{QStringLiteral("--selftest")}; }};
    p->setSteps(steps);
    m_warnTargets.append(p);
    return p;
}
