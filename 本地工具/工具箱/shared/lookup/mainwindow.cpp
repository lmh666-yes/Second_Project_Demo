#include "mainwindow.h"

#include "apppaths.h"
#include "cardhtml.h"
#include "searchengine.h"
#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString sectionName(int s)
{
    switch (s) {
    case int(Card::Guide): return QStringLiteral("引导教程");
    case int(Card::Build): return QStringLiteral("项目引导");
    default:               return QStringLiteral("资料查询");
    }
}

QString typeTag(const Card &c)
{
    if (c.isManu())   return QStringLiteral("手册");
    if (c.isFlow())   return QStringLiteral("流程");
    if (c.isGuide())  return QStringLiteral("向导");
    if (c.isTable())  return QStringLiteral("表");
    if (c.isStruct()) return QStringLiteral("结构体");
    if (c.isDs())     return QStringLiteral("手册页");
    if (c.isChip())   return QStringLiteral("芯片");
    return QStringLiteral("函数");
}

/* 落地页内容：与网页版 / 手机版同一份清单（标准库速查-手机版.html 的
   HOT_MODULES / HOT_TOPICS / 三条路径卡）。改一处要三处一起改。
   锚点一律用**纯 ASCII 下标**（q:0 / mod:0 / path:0）而不是把中文塞进 href ——
   href 里的非 ASCII 会被 QUrl 百分号编码，回来对不上，数值下标最稳。 */
const char *kHotModules[] = { "USART", "GPIO", "TIM", "ADC", "DMA", "I2C",
                              "SPI", "NVIC", "EXTI", "RCC", "FLASH", "RTC" };
const char *kHotTopics[] = { "串口 DMA + 空闲中断", "定时器 PWM", "数据帧组包 + CRC16",
                             "FreeRTOS 任务框架", "LoRa 点对点", "ESP8266 + MQTT",
                             "I2C 接传感器", "看门狗", "RTC 电子钟", "Flash 断网缓存" };

const int kHotModuleN = int(sizeof(kHotModules) / sizeof(kHotModules[0]));
const int kHotTopicN  = int(sizeof(kHotTopics)  / sizeof(kHotTopics[0]));

/* ---------- 项目引导「下一步」：读进度文档的阶段总表 ---------- */

struct StageRow { int no; QString name; bool done; };

/* 某个工程的进度文档绝对路径。
   路径来自板级配置 `速查数据/rules/board_envmonitor.json` 的 extra.progress_docs，
   与 dataset 的 proj 同序（板1 / 板2 / Qt，由 project_scaffold.py 保证）；
   配置里是相对仓库根的路径，仓库根 = 数据根的上三级。 */
QString progressDocForProject(const QString &tag)
{
    const QString tool = AppPaths::toolRoot();
    if (tool.isEmpty())
        return QString();
    QFile f(QDir(tool).filePath(QStringLiteral("rules/board_envmonitor.json")));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray docs = o.value(QStringLiteral("extra")).toObject()
                                .value(QStringLiteral("progress_docs")).toArray();
    int idx = -1;
    if (tag == QStringLiteral("板1"))      idx = 0;
    else if (tag == QStringLiteral("板2")) idx = 1;
    else if (tag == QStringLiteral("Qt"))  idx = 2;
    if (idx < 0 || idx >= docs.size())
        return QString();
    const QString repo = QDir::cleanPath(QDir(tool).filePath(QStringLiteral("../../..")));
    return QDir(repo).filePath(docs.at(idx).toString());
}

/* 解析进度文档「1. 阶段总表」那 5 列：| 阶段 | 阶段名 | 状态 | 证据 | 日期 |。
   只认第一列是整数的行；状态列出现 ☑ / ✅ / "完成" 即视为做完。
   表一结束（遇到非 | 开头且已收到行）就停，不会把后面的明细表当总表。 */
QVector<StageRow> parseStageTable(const QString &path)
{
    QVector<StageRow> out;
    if (path.isEmpty())
        return out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    bool inTable = false;
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (!inTable) {
            if (line.startsWith(QStringLiteral("| 阶段")) || line.startsWith(QStringLiteral("| 步骤")))
                inTable = true;
            continue;
        }
        if (!line.startsWith(QLatin1Char('|'))) {
            if (!out.isEmpty())
                break;                      /* 总表结束 */
            continue;
        }
        if (line.contains(QStringLiteral("---")))
            continue;                       /* 分隔行 */
        const QStringList cells = line.split(QLatin1Char('|'));
        if (cells.size() < 4)
            continue;
        bool ok = false;
        const int no = cells.at(1).trimmed().toInt(&ok);
        if (!ok)
            continue;
        const QString name = cells.at(2).trimmed();
        const QString st   = cells.at(3);
        const bool done = st.contains(QStringLiteral("☑")) || st.contains(QStringLiteral("✅"))
                       || st.contains(QStringLiteral("完成"));
        out.append(StageRow{no, name, done});
    }
    return out;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("单片机手册"));
    buildUi();

    /* 恢复上次板块 */
    const int sec = m_store.lastSection();
    if (sec >= 0 && sec <= 2 && sec != m_section)
        onSectionClicked(sec);

    /* 快捷键，与网页版对齐：/ 搜、Esc 清空、Ctrl+D 收藏、Ctrl+B 收藏夹、Ctrl+H 最近、Ctrl+J 主题
       （Qt6 去掉了 QShortcut 的"key + 槽"构造，得自己 connect）

       上下文必须限定在"本页及子控件"内：本页已并入工具箱，QShortcut 默认是
       Qt::WindowShortcut，作用域是整窗——不改的话在 RTOS 生成 / 协议工作台等页面上
       按 Ctrl+D、Ctrl+H、"/" 会误触速查的收藏、最近与搜索框聚焦。 */
    auto addSc = [this](const QKeySequence &k) {
        QShortcut *sc = new QShortcut(k, this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        return sc;
    };
    connect(addSc(QKeySequence(QStringLiteral("/"))), &QShortcut::activated, this, [this]() {
        m_search->setFocus();
        m_search->selectAll();
    });
    connect(addSc(QKeySequence(Qt::Key_Escape)), &QShortcut::activated, this, [this]() {
        m_search->clear();
        onSearchEdited();
    });
    const struct { const char *key; void (MainWindow::*slot)(); } scs[] = {
        { "Ctrl+D", &MainWindow::toggleCurrentFav },
        { "Ctrl+B", &MainWindow::onFavClicked },
        { "Ctrl+H", &MainWindow::onRecentClicked },
        { "Ctrl+J", &MainWindow::onThemeToggled },
    };
    for (const auto &s : scs)
        connect(addSc(QKeySequence(QString::fromLatin1(s.key))), &QShortcut::activated, this, s.slot);
}

void MainWindow::buildUi()
{
    QWidget *central = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(6);

    /* ---- 第一行：板块 + 主题 ---- */
    QHBoxLayout *row1 = new QHBoxLayout;
    const char *names[3] = { "📖 资料查询", "📚 引导教程", "🧭 项目引导" };
    for (int i = 0; i < 3; ++i) {
        QPushButton *b = new QPushButton(QString::fromUtf8(names[i]), this);
        b->setCheckable(true);
        b->setChecked(i == m_section);
        connect(b, &QPushButton::clicked, this, [this, i]() { onSectionClicked(i); });
        row1->addWidget(b);
        m_secBtn[i] = b;
    }
    row1->addStretch(1);
    m_btnFav = new QPushButton(QStringLiteral("★ 收藏"), this);
    m_btnFav->setToolTip(QStringLiteral("收藏夹（Ctrl+B）"));
    connect(m_btnFav, &QPushButton::clicked, this, &MainWindow::onFavClicked);
    row1->addWidget(m_btnFav);
    m_btnRecent = new QPushButton(QStringLiteral("🕘 最近"), this);
    m_btnRecent->setToolTip(QStringLiteral("最近查看（Ctrl+H）"));
    connect(m_btnRecent, &QPushButton::clicked, this, &MainWindow::onRecentClicked);
    row1->addWidget(m_btnRecent);
    QPushButton *themeBtn = new QPushButton(QStringLiteral("🌗 主题"), this);
    connect(themeBtn, &QPushButton::clicked, this, &MainWindow::onThemeToggled);
    row1->addWidget(themeBtn);
    m_count = new QLabel(this);
    row1->addWidget(m_count);
    root->addLayout(row1);

    /* ---- 第二行：搜索 + 模块筛选 ---- */
    QHBoxLayout *row2 = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral(
        "中文 / 英文 / 混排都能搜（如 打开DMA中断、串口 中断、USART_Init）"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::returnPressed, this, &MainWindow::onSearchEdited);
    /* 边打字边出结果：网页版就是即时的，桌面版以前只在回车后才刷新，
       用户反馈"搜了没反应、得去切一下模块才出来"。这里做 120ms 防抖 ——
       连续打字期间不重排，停手立刻刷新，手感与网页版一致。 */
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(120);
    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::onSearchEdited);
    connect(m_search, &QLineEdit::textChanged, this, [this]() { m_searchTimer->start(); });
    m_module = new QComboBox(this);
    m_module->setMinimumWidth(190);
    connect(m_module, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModuleChanged);
    row2->addWidget(m_search, 1);
    row2->addWidget(new QLabel(QStringLiteral("模块"), this));
    row2->addWidget(m_module);
    root->addLayout(row2);

    /* ---- 项目引导的「下一步」入口 ----
       只在「项目引导 + 选中某个具体工程」时出现（见 updateNextStageHint）：
       读该工程进度文档的阶段总表，指出第一个还没勾的阶段，点一下直达那张卡。
       做成扁平按钮而不是 QLabel —— 它要能点。右边配一个「判定阶段」：
       跑一遍 stage_status.py 按代码现状判，并回写总表（A1）。 */
    {
        auto *hintRow = new QHBoxLayout();
        m_nextHint = new QPushButton(this);
        m_nextHint->setFlat(true);
        m_nextHint->setCursor(Qt::PointingHandCursor);
        m_nextHint->setStyleSheet(QStringLiteral(
            "QPushButton { text-align: left; padding: 6px 10px;"
            " border: 1px solid palette(mid); border-radius: 4px; }"
            "QPushButton:hover { background: palette(alternate-base); }"));
        m_nextHint->hide();
        connect(m_nextHint, &QPushButton::clicked, this, &MainWindow::jumpToNextStage);
        hintRow->addWidget(m_nextHint, 1);

        /* A3：把「下一步」从"跳过去"变成"跑起来" —— 直接调 build_flash.py --stage-run N。
           只在能真跑的工程（板1 / 板2）上出现；OTA / Qt 不在编译烧录的板表里，不显示。 */
        m_btnRunStage = new QPushButton(QStringLiteral("跑这一步"), this);
        m_btnRunStage->setToolTip(QStringLiteral(
            "一条命令走完这一步：判定 → 生成阶段自检 → 接入检查 → 编译 → 烧录 →\n"
            "串口收 STAGE<N>=PASS → 回写进度文档 → 归档快照（任一步失败即停并说清卡在哪）。\n"
            "需要先在 Keil 里接入 board_selftest 一次 —— 工具不替你改代码/工程。"));
        m_btnRunStage->hide();
        connect(m_btnRunStage, &QPushButton::clicked, this, &MainWindow::runNextStage);
        hintRow->addWidget(m_btnRunStage);

        m_btnJudge = new QPushButton(QStringLiteral("判定阶段"), this);
        m_btnJudge->setToolTip(QStringLiteral(
            "按代码现状判定各阶段（关键 API 有没有真调用），并回写进度文档的阶段总表"));
        m_btnJudge->hide();
        connect(m_btnJudge, &QPushButton::clicked, this, &MainWindow::runStageJudge);
        hintRow->addWidget(m_btnJudge);
        root->addLayout(hintRow);
    }

    /* ---- 主体：列表 + 详情 ---- */
    QSplitter *sp = new QSplitter(Qt::Horizontal, this);
    m_list = new QListWidget(sp);
    m_list->setMinimumWidth(260);
    connect(m_list, &QListWidget::currentRowChanged, this, &MainWindow::onCardPicked);
    m_detail = new QTextBrowser(sp);
    m_detail->setOpenLinks(false);
    /* 等宽字体等文档级样式要设在 QTextDocument 上（QTextBrowser 没有这个接口） */
    m_detail->document()->setDefaultStyleSheet(QStringLiteral(
        "pre { font-family: \"Cascadia Mono\", Consolas, monospace; }"
        "table { border-collapse: collapse; }"
        "a { text-decoration: none; }"));
    connect(m_detail, &QTextBrowser::anchorClicked, this, &MainWindow::onAnchor);
    sp->addWidget(m_list);
    sp->addWidget(m_detail);
    sp->setStretchFactor(0, 0);
    sp->setStretchFactor(1, 1);
    root->addWidget(sp, 1);

    setCentralWidget(central);
    resize(1120, 720);
}

bool MainWindow::loadData(QString *err)
{
    if (!m_ds.load(AppPaths::datasetPath())) {
        if (err)
            *err = m_ds.error();
        if (m_detail)
            m_detail->setHtml(QStringLiteral("<p style='color:#ffb454;'>%1</p>")
                                  .arg(CardHtml::esc(m_ds.error())));
        return false;
    }
    if (!m_tables.load(m_ds.search()) && err)
        *err = QStringLiteral("搜索用表缺失（dataset.json 的 search 段为空）");
    m_engine.reset(new SearchEngine(&m_ds.cards(), &m_tables));
    rebuildModuleBox();
    refreshList();
    return true;
}

void MainWindow::rebuildModuleBox()
{
    const QString keep = m_module->currentText();
    m_module->blockSignals(true);
    m_module->clear();
    /* 板块首项"全部XX"：项目引导（Build）列的是 3 个工程（板1 / 板2 / OTA），
       首项标签就得是"全部工程"，别再跟着资料查询叫"全部模块"。
       （三个板块分别是：资料查询=模块、引导教程=教程、项目引导=工程。） */
    QString allLabel = QStringLiteral("全部模块");
    if (m_section == int(Card::Guide))
        allLabel = QStringLiteral("全部教程");
    else if (m_section == int(Card::Build))
        allLabel = QStringLiteral("全部工程");
    m_module->addItem(allLabel);

    /* 模块名与网页一致：引导/流程卡按「主题」归类，其余按 m；项目引导按工程 */
    QStringList mods;
    for (int i : m_ds.indicesOfSection(m_section)) {
        const Card &c = m_ds.at(i);
        QString k;
        if (m_section == int(Card::Build))
            k = c.b;
        else if (m_engine)
            k = m_engine->modKeyOf(c);
        else
            k = c.m;
        if (!k.isEmpty() && !mods.contains(k))
            mods << k;
    }
    std::sort(mods.begin(), mods.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    m_module->addItems(mods);

    const int i = m_module->findText(keep);
    if (i >= 0)
        m_module->setCurrentIndex(i);
    m_module->blockSignals(false);
}

void MainWindow::onSectionClicked(int sec)
{
    m_section = sec;
    m_store.setLastSection(sec);
    for (int i = 0; i < 3; ++i)
        m_secBtn[i]->setChecked(i == sec);
    rebuildModuleBox();
    refreshList();
    m_detail->clear();
}

void MainWindow::onModuleChanged(int)
{
    refreshList();
}

void MainWindow::onSearchEdited()
{
    refreshList();
}

void MainWindow::refreshList()
{
    m_shown.clear();
    const QString mod = (m_module->currentIndex() > 0) ? m_module->currentText() : QString();

    if (m_section == int(Card::Build)) {
        /* 项目引导不走搜索引擎，直接列手册阶段卡（按工程筛） */
        for (int i : m_ds.indicesOfSection(m_section))
            if (mod.isEmpty() || m_ds.at(i).b == mod)
                m_shown << i;
    } else if (m_engine) {
        m_shown = m_engine->run(m_search->text(), m_section, mod).order;
    } else {
        m_shown = m_ds.indicesOfSection(m_section);
    }

    m_list->blockSignals(true);
    m_list->clear();
    for (int i : m_shown) {
        const Card &c = m_ds.at(i);
        /* 项目引导里列的全是手册阶段卡，每行再挂一个「⟨手册⟩」就是行行重复的噪声；
           那个板块直接只显示卡名（其余板块类型是混的，标签才有信息量）。 */
        if (m_section == int(Card::Build))
            m_list->addItem(c.n);
        else
            m_list->addItem(QStringLiteral("%1  ⟨%2⟩").arg(c.n, typeTag(c)));
    }
    m_list->blockSignals(false);

    /* 「下一步：阶段 N」入口 —— 列表一换就重算（板块 / 工程筛选都走这里） */
    updateNextStageHint();

    /* 落地页只在「资料查询 · 全部模块 · 没搜索」时出现，与网页/手机一致：
       一搜索或一选模块，列表就是正常结果，落地页立刻消失。
       这时右侧显示落地页而不是第一条卡；列表**不选中**任何行（选中会触发
       onCardPicked 把详情换成第 0 条卡，盖掉落地页）。 */
    const bool landing = (m_section == int(Card::Ref)) && mod.isEmpty()
                         && m_search->text().trimmed().isEmpty();
    if (landing) {
        m_list->setCurrentRow(-1);
        m_current = -1;
        m_detail->setHtml(landingHtml());
        m_detail->verticalScrollBar()->setValue(0);
        updateCounts();
        return;
    }

    updateCounts();
    if (!m_shown.isEmpty())
        m_list->setCurrentRow(0);
    else {
        m_detail->setHtml(QStringLiteral("<p style='color:#8b93a3;'>没有匹配的条目。</p>"));
        m_current = -1;
    }
}

/* 「下一步：阶段 N」—— 从进度文档的阶段总表读"做到哪了"。
   只在项目引导、且选了一个**具体工程**时显示：
     · 全部工程  → 不知道你说的是哪块板，不猜，隐藏；
     · 文档读不到 / 还没有总表 → 隐藏（宁可不显示，也不给一句假的"下一步"）。 */
void MainWindow::updateNextStageHint()
{
    if (!m_nextHint)
        return;
    m_nextHint->setText(QString());   /* 文字也清掉：nextStageHintFor() 靠"有没有字"判断显示没显示 */
    m_nextHint->hide();
    if (m_btnJudge)
        m_btnJudge->hide();
    m_nextHint->setProperty("stageNo", -1);
    if (m_btnRunStage)
        m_btnRunStage->hide();
    if (m_section != int(Card::Build))
        return;
    const QString tag = (m_module->currentIndex() > 0) ? m_module->currentText() : QString();
    if (tag.isEmpty())
        return;
    const QVector<StageRow> rows = parseStageTable(progressDocForProject(tag));
    if (rows.isEmpty())
        return;

    int next = -1;
    for (int i = 0; i < rows.size(); ++i) {
        if (!rows.at(i).done) {
            next = i;
            break;
        }
    }
    if (next < 0) {
        m_nextHint->setText(QStringLiteral("✅ %1：%2 个阶段全部完成（要重看某一段，直接在左边点）")
                                .arg(tag).arg(rows.size()));
    } else {
        const StageRow &r = rows.at(next);
        m_nextHint->setText(QStringLiteral("▶ 下一步：%1 · 阶段 %2｜%3　（点这里直达）")
                                .arg(tag).arg(r.no).arg(r.name));
        m_nextHint->setProperty("stageNo", r.no);
    }
    m_nextHint->show();
    if (m_btnJudge)
        m_btnJudge->show();
    /* A3：「跑这一步」只在有下一步、且这块板真能跑（板1 / 板2）时才给。 */
    if (m_btnRunStage) {
        const bool runnable = (next >= 0)
                              && (tag.startsWith(QStringLiteral("板1"))
                                  || tag.startsWith(QStringLiteral("板2")));
        m_btnRunStage->setEnabled(runnable);
        if (runnable)
            m_btnRunStage->show();
        else
            m_btnRunStage->hide();
    }
}

/* A1：按代码现状判阶段，并把结果回写进度文档（脚本自己只改「状态/证据」两列，
   判不了的阶段整行不碰）。跑完刷新横幅 —— 判完就知道下一步该干哪一段。 */
void MainWindow::runStageJudge()
{
    const QString tag = m_module->currentText();
    if (tag.isEmpty())
        return;
    const QString tool = AppPaths::toolRoot();
    if (tool.isEmpty())
        return;
    const QString script = QDir::cleanPath(
        QDir(tool).filePath(QStringLiteral("../检查/阶段判定/stage_status.py")));
    if (!QFileInfo::exists(script)) {
        QMessageBox::warning(this, QStringLiteral("判定阶段"),
                             QStringLiteral("找不到脚本：\n%1").arg(script));
        return;
    }

    m_btnJudge->setEnabled(false);
    m_btnJudge->setText(QStringLiteral("判定中…"));
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(QFileInfo(script).absolutePath());
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus) {
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        m_btnJudge->setEnabled(true);
        m_btnJudge->setText(QStringLiteral("判定阶段"));
        updateNextStageHint();          /* 判完横幅跟着走 */
        if (code != 0) {
            QMessageBox::warning(this, QStringLiteral("判定阶段失败"),
                                 err.isEmpty() ? QStringLiteral("退出码 %1").arg(code) : err);
        }
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        m_btnJudge->setEnabled(true);
        m_btnJudge->setText(QStringLiteral("判定阶段"));
        QMessageBox::warning(this, QStringLiteral("判定阶段"),
                             QStringLiteral("启动失败：没找到 Python。"));
        proc->deleteLater();
    });
    proc->start(QStringLiteral("python"),
                {script, QStringLiteral("--apply"), QStringLiteral("--board"), tag});
}

/* A3：跑这一步 —— 调 build_flash.py --stage-run N（一条命令走完一个阶段）。
   跑完（可能已把进度文档回写了）刷新横幅，并把过程末尾几行摆给用户看。 */
void MainWindow::runNextStage()
{
    if (!m_nextHint || !m_btnRunStage)
        return;
    const int no = m_nextHint->property("stageNo").toInt();
    const QString tag = m_module->currentText();
    if (no < 0 || tag.isEmpty())
        return;
    const QString tool = AppPaths::toolRoot();
    if (tool.isEmpty())
        return;
    const QString script = QDir::cleanPath(
        QDir(tool).filePath(QStringLiteral("../检查/构建烧录/build_flash.py")));
    if (!QFileInfo::exists(script)) {
        QMessageBox::warning(this, QStringLiteral("跑这一步"),
                             QStringLiteral("找不到脚本：\n%1").arg(script));
        return;
    }

    m_btnRunStage->setEnabled(false);
    m_btnRunStage->setText(QStringLiteral("跑着呢…"));
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(QFileInfo(script).absolutePath());
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, no, tag](int code, QProcess::ExitStatus) {
        QString body = (QString::fromUtf8(proc->readAllStandardOutput())
                        + QString::fromUtf8(proc->readAllStandardError())).trimmed();
        const QStringList ls = body.split(QLatin1Char('\n'));
        if (ls.size() > 16)
            body = QStringLiteral("…（只显示最后 16 行）\n") + ls.mid(ls.size() - 16).join(QLatin1Char('\n'));
        m_btnRunStage->setEnabled(true);
        m_btnRunStage->setText(QStringLiteral("跑这一步"));
        updateNextStageHint();          /* 跑完可能已回写进度文档 → 横幅跟着走 */
        const QString title = QStringLiteral("阶段验收：%1 · 阶段 %2").arg(tag).arg(no);
        if (code == 0)
            QMessageBox::information(this, title, body);
        else
            QMessageBox::warning(this, title,
                                 QStringLiteral("这一步没跑完（退出码 %1）。\n\n%2")
                                     .arg(code).arg(body));
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        m_btnRunStage->setEnabled(true);
        m_btnRunStage->setText(QStringLiteral("跑这一步"));
        QMessageBox::warning(this, QStringLiteral("跑这一步"),
                             QStringLiteral("启动失败：没找到 Python。"));
        proc->deleteLater();
    });
    proc->start(QStringLiteral("python"),
                {script, QStringLiteral("--stage-run"), QString::number(no),
                 QStringLiteral("--board"), tag});
}

/* 点「下一步」→ 在左侧列表里选中那张阶段卡（卡名形如「🏗 板1 · 阶段 3｜…」）。 */
void MainWindow::jumpToNextStage()
{
    if (!m_nextHint)
        return;
    const int no = m_nextHint->property("stageNo").toInt();
    if (no < 0)
        return;
    const QString tag = m_module->currentText();
    const QString needle = QStringLiteral("阶段 %1｜").arg(no);
    const int n = qMin(m_list->count(), m_shown.size());
    for (int r = 0; r < n; ++r) {
        const Card &c = m_ds.at(m_shown.at(r));
        if (c.b == tag && c.n.contains(needle)) {
            m_list->setCurrentRow(r);
            m_list->scrollToItem(m_list->item(r));
            return;
        }
    }
}

/* 自检入口：切到项目引导 + 选中该工程，把横幅文字取回来（空 = 没显示）。
   不走 isVisible()：命令行自检不 show 窗口，isVisible() 恒为 false，测不出东西。 */
QString MainWindow::nextStageHintFor(const QString &projectTag)
{
    onSectionClicked(int(Card::Build));
    const int idx = m_module->findText(projectTag);
    if (idx >= 0)
        m_module->setCurrentIndex(idx);      /* 触发 onModuleChanged → refreshList */
    return m_nextHint ? m_nextHint->text() : QString();
}

/* A3：自检入口 —— 切到项目引导 + 选中该工程，看「跑这一步」按钮显示没显示。
   用 isHidden() 而不是 isVisible()：命令行自检不 show 窗口，isVisible() 恒为 false。 */
bool MainWindow::canRunNextStageFor(const QString &projectTag)
{
    onSectionClicked(int(Card::Build));
    const int idx = m_module->findText(projectTag);
    if (idx >= 0)
        m_module->setCurrentIndex(idx);
    return m_btnRunStage && !m_btnRunStage->isHidden();
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    /* 切回本页时重算「下一步」：你可能刚在编辑器里把某个阶段勾上了 */
    updateNextStageHint();
}

/* 落地页 HTML（「从这里开始」）。Qt 富文本不支持 flex/grid，
   所以路径卡与 chip 一律摊平成块级元素 + 空格分隔。 */
QString MainWindow::landingHtml() const
{
    const bool dark = (Theme::mode() == Theme::Dark);
    const QString dim    = dark ? QStringLiteral("#8b93a3") : QStringLiteral("#5b667a");
    const QString accent = dark ? QStringLiteral("#7fb3ff") : QStringLiteral("#1a6fd6");

    QString h = QStringLiteral(
        "<p style='font-size:15px;font-weight:700;margin:0 0 6px 0;'>👋 从这里开始</p>"
        "<p style='color:%1;font-size:12px;margin:0 0 12px 0;'>"
        "不确定用哪个函数？先看「流程卡」（按功能给完整可抄的初始化流程），"
        "或「引导教程」（从零一步步搭到能跑）。下面按你常做的事分组。</p>").arg(dim);

    const struct { const char *ic, *t, *d, *anchor; } paths[3] = {
        { "🧭", "我要自己搭一个外设",     "打开「引导教程」，从接线到验收一步步做", "path:0" },
        { "📋", "我知道要干嘛，只差代码", "搜功能词（如「打开DMA中断」），直接看流程卡", "path:1" },
        { "📖", "我要查某个函数的参数",   "搜函数名或宏名，看参数表 / 返回值 / 示例",   "path:2" },
    };
    for (const auto &p : paths)
        h += QStringLiteral("<p style='margin:0 0 8px 0;'>"
                            "<a href='%1' style='color:%5;font-weight:600;'>%2 %3</a>"
                            "<br><span style='color:%4;font-size:12px;'>%6</span></p>")
                 .arg(QString::fromLatin1(p.anchor), QString::fromUtf8(p.ic),
                      QString::fromUtf8(p.t), dim, accent, QString::fromUtf8(p.d));

    h += QStringLiteral("<p style='color:%1;font-size:12px;margin:14px 0 5px 0;'>"
                        "热门主题（点一下直接搜）</p>").arg(dim);
    for (int i = 0; i < kHotTopicN; ++i)
        h += QStringLiteral("<a href='q:%1'>%2</a>&nbsp;&nbsp;")
                 .arg(i).arg(CardHtml::esc(QString::fromUtf8(kHotTopics[i])));

    h += QStringLiteral("<p style='color:%1;font-size:12px;margin:14px 0 5px 0;'>"
                        "常用模块（点一下筛该模块）</p>").arg(dim);
    for (int i = 0; i < kHotModuleN; ++i) {
        const QString mod = QString::fromUtf8(kHotModules[i]);
        int n = 0;
        for (int ci : m_ds.indicesOfSection(int(Card::Ref)))
            if (m_ds.at(ci).m == mod) ++n;
        if (!n)
            continue;
        h += QStringLiteral("<a href='mod:%1'>%2</a>&nbsp;<span style='color:%4;"
                            "font-size:11px;'>(%3)</span>&nbsp;&nbsp;")
                 .arg(i).arg(CardHtml::esc(mod)).arg(n).arg(dim);
    }
    return h;
}

void MainWindow::updateCounts()
{
    const QString mod = (m_module->currentIndex() > 0) ? m_module->currentText() : QString();
    const bool landing = (m_section == int(Card::Ref)) && mod.isEmpty()
                         && m_search->text().trimmed().isEmpty();
    if (landing) {
        int flow = 0, guide = 0;
        for (const Card &c : m_ds.cards()) {
            if (c.isFlow())  ++flow;
            if (c.isGuide()) ++guide;
        }
        m_count->setText(QStringLiteral("共 %1 条 · %2 流程卡 + %3 向导可直接照着做")
                             .arg(m_ds.size()).arg(flow).arg(guide));
        return;
    }
    m_count->setText(QStringLiteral("%1 · 显示 %2 / 共 %3 条")
                         .arg(sectionName(m_section))
                         .arg(m_shown.size()).arg(m_ds.size()));
}

void MainWindow::onCardPicked(int row)
{
    if (row < 0 || row >= m_shown.size())
        return;
    showCard(m_shown.at(row));
}

void MainWindow::showCard(int cardIndex)
{
    if (cardIndex < 0 || cardIndex >= m_ds.size())
        return;
    m_current = cardIndex;
    const bool dark = (Theme::mode() == Theme::Dark);
    const Card &card = m_ds.at(cardIndex);

    CardHtml::Ctx ctx;
    ctx.index = cardIndex;
    ctx.isFav = m_store.isFavorite(card.n);
    ctx.done  = m_store.doneSteps();

    /* 命中原因：网页版会在条目旁标出「名称 / 正文」命中，这里放在详情顶部 */
    QString head;
    if (m_engine && !m_search->text().trimmed().isEmpty()) {
        const QStringList hits = m_engine->hitWords(m_search->text(), cardIndex, m_section);
        if (!hits.isEmpty())
            head = QStringLiteral("<p style='color:%1;font-size:12px;margin:0 0 6px 0;'>%2</p>")
                       .arg(dark ? QStringLiteral("#8b93a3") : QStringLiteral("#5b667a"),
                            CardHtml::esc(QString::fromUtf8("命中：") + hits.join(QString::fromUtf8("　"))));
    }
    m_detail->setHtml(head + CardHtml::build(card, dark, ctx));
    m_detail->verticalScrollBar()->setValue(0);
    m_store.touchRecent(card.n);
}

void MainWindow::onAnchor(const QUrl &url)
{
    const QString s = url.toString();
    if (s.startsWith(QStringLiteral("card:"))) {
        jumpToName(s.mid(5));
    } else if (s.startsWith(QStringLiteral("fav:"))) {
        toggleCurrentFav();
    } else if (s.startsWith(QStringLiteral("step:"))) {
        const QStringList parts = s.mid(5).split(QLatin1Char(':'));
        if (parts.size() == 2 && m_current >= 0) {
            m_store.toggleStep(m_ds.at(m_current).n, parts.at(1).toInt());
            showCard(m_current);
        }
    } else if (s.startsWith(QStringLiteral("copy:"))) {
        if (m_current >= 0)
            QApplication::clipboard()->setText(CardHtml::copyText(m_ds.at(m_current)));
        statusBar()->showMessage(QStringLiteral("代码已复制到剪贴板"), 2000);
    } else if (s.startsWith(QStringLiteral("q:"))) {
        /* 落地页「热门主题」chip：把词填进搜索框（屏蔽信号免得防抖定时器再刷一遍） */
        const int i = s.mid(2).toInt();
        if (i >= 0 && i < kHotTopicN) {
            m_searchTimer->stop();
            m_search->blockSignals(true);
            m_search->setText(QString::fromUtf8(kHotTopics[i]));
            m_search->blockSignals(false);
            refreshList();
        }
    } else if (s.startsWith(QStringLiteral("mod:"))) {
        /* 落地页「常用模块」chip：选中对应模块；setCurrentIndex 会触发 refreshList */
        const int i = s.mid(4).toInt();
        if (i >= 0 && i < kHotModuleN) {
            const int idx = m_module->findText(QString::fromUtf8(kHotModules[i]));
            if (idx > 0)
                m_module->setCurrentIndex(idx);
            else
                statusBar()->showMessage(QStringLiteral("该模块当前没有条目"), 2000);
        }
    } else if (s.startsWith(QStringLiteral("path:"))) {
        /* 落地页三条路径卡：0 切「引导教程」，1/2 直接塞搜索词 */
        const int i = s.mid(5).toInt();
        if (i == 0) {
            onSectionClicked(int(Card::Guide));
        } else if (i == 1 || i == 2) {
            m_searchTimer->stop();
            m_search->blockSignals(true);
            m_search->setText(i == 1 ? QStringLiteral("打开DMA中断")
                                     : QStringLiteral("USART_Init"));
            m_search->blockSignals(false);
            refreshList();
        }
    }
}

void MainWindow::toggleCurrentFav()
{
    if (m_current < 0)
        return;
    const bool now = m_store.toggleFavorite(m_ds.at(m_current).n);
    showCard(m_current);
    statusBar()->showMessage(now ? QStringLiteral("已加入收藏 ★")
                                 : QStringLiteral("已取消收藏"), 2000);
}

void MainWindow::onFavClicked()
{
    QMenu menu(this);
    const QStringList favs = m_store.favorites();
    if (favs.isEmpty()) {
        QAction *a = menu.addAction(QStringLiteral("还没有收藏：在详情页点 ☆ 或按 Ctrl+D"));
        a->setEnabled(false);
    }
    for (const QString &n : favs) {
        QAction *a = menu.addAction(n);
        connect(a, &QAction::triggered, this, [this, n]() { jumpToName(n); });
    }
    if (!favs.isEmpty()) {
        menu.addSeparator();
        QAction *c = menu.addAction(QStringLiteral("清空收藏"));
        connect(c, &QAction::triggered, this, [this]() {
            m_store.clearFavorites();
            statusBar()->showMessage(QStringLiteral("收藏已清空"), 2000);
        });
    }
    menu.exec(m_btnFav->mapToGlobal(QPoint(0, m_btnFav->height())));
}

void MainWindow::onRecentClicked()
{
    QMenu menu(this);
    const QStringList rec = m_store.recent();
    if (rec.isEmpty()) {
        QAction *a = menu.addAction(QStringLiteral("还没有浏览记录"));
        a->setEnabled(false);
    }
    for (const QString &n : rec) {
        QAction *a = menu.addAction(n);
        connect(a, &QAction::triggered, this, [this, n]() { jumpToName(n); });
    }
    if (!rec.isEmpty()) {
        menu.addSeparator();
        QAction *c = menu.addAction(QStringLiteral("清空最近"));
        connect(c, &QAction::triggered, this, [this]() {
            m_store.clearRecent();
            statusBar()->showMessage(QStringLiteral("最近查看已清空"), 2000);
        });
    }
    menu.exec(m_btnRecent->mapToGlobal(QPoint(0, m_btnRecent->height())));
}

void MainWindow::resetModuleFilter()
{
    /* 模块筛选会把不在该模块里的条目滤掉，导致"明明有却搜不到"。
       按名字跳 / 报告里点条目名时都要先把它复位。 */
    if (m_module->currentIndex() != 0) {
        m_module->blockSignals(true);
        m_module->setCurrentIndex(0);
        m_module->blockSignals(false);
    }
}

void MainWindow::jumpToName(const QString &name)
{
    const int idx = m_ds.indexOfName(name);
    if (idx < 0) {
        statusBar()->showMessage(QStringLiteral("找不到条目：%1").arg(name), 3000);
        return;
    }
    const int sec = int(m_ds.at(idx).section());
    const bool crossSec = (sec != m_section);

    /* 跨板块跳转时，搜索框里的旧查询词还在，列表会继续被过滤 → 目标条目选不中。
       所以这里**一律先清干净搜索与模块筛选**，保证目标一定出现在列表里。 */
    if (crossSec)
        onSectionClicked(sec);
    if (!m_search->text().isEmpty()) {
        m_search->blockSignals(true);
        m_search->clear();
        m_search->blockSignals(false);
    }
    /* 模块筛选同样可能把它滤掉 */
    resetModuleFilter();
    refreshList();

    const int row = m_shown.indexOf(idx);
    if (row >= 0) {
        m_list->setCurrentRow(row);
        if (QListWidgetItem *it = m_list->item(row))
            m_list->scrollToItem(it);
    } else {
        showCard(idx);
    }
}

int MainWindow::jumpToTerm(const QString &term)
{
    const QString t = term.trimmed();
    if (t.isEmpty())
        return 0;
    if (!m_engine)                       /* 数据懒加载：还没装就先装一次 */
        loadData();
    /* 先按条目名精确找（报告里的 SYS_FRAME_Build 这类多半就是条目名） */
    if (m_ds.indexOfName(t) >= 0) {
        jumpToName(t);
        return 1;
    }
    /* 没有同名条目就退回搜索：填进搜索框、按当前板块跑一遍，命中就定位第一条。
       「项目引导」板块列的是手册阶段卡、不走搜索，所以在那里搜不到时要切回「资料查询」
       再搜一遍 —— 报告里点一个函数名，就该在资料里找到它，不该因为当时停在引导页而落空。 */
    m_searchTimer->stop();
    m_search->blockSignals(true);
    m_search->setText(t);
    m_search->blockSignals(false);
    resetModuleFilter();      /* 模块筛选不做的话，不在该模块的条目会被滤掉 → 误报"没有" */
    if (m_section == int(Card::Build))
        onSectionClicked(int(Card::Ref));
    refreshList();
    if (m_shown.isEmpty() && m_section != int(Card::Ref)) {
        onSectionClicked(int(Card::Ref));      /* onSectionClicked 里会 refreshList */
    }
    if (!m_shown.isEmpty()) {
        m_list->setCurrentRow(0);
        if (QListWidgetItem *it = m_list->item(0))
            m_list->scrollToItem(it);
        statusBar()->showMessage(QStringLiteral("「%1」命中 %2 条，已定位第一条")
                                     .arg(t).arg(m_shown.size()), 3000);
    } else {
        statusBar()->showMessage(QStringLiteral("手册里没有「%1」").arg(t), 3000);
    }
    return m_shown.size();
}


void MainWindow::onThemeToggled()
{
    Theme::toggle();
    if (m_current >= 0)
        showCard(m_current);        /* QTextDocument 不吃应用 QSS，必须重渲染 */
    emit themeToggled();            /* 通知工具箱侧栏同步按钮文字 */
}
