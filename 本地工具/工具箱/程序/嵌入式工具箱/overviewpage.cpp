#include "overviewpage.h"

#include "dashboardcanvas.h"
#include "pyfind.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShowEvent>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

struct ReportDef {
    const char *title;
    const char *rel;      /* 相对工具箱目录 */
};

const ReportDef REPORTS[] = {
    {"进度扫描", "检查/进度跟踪/进度报告.md"},
    {"引脚规划", "检查/引脚规划/引脚规划报告.md"},
    {"RTOS 检查", "检查/RTOS检查/RTOS检查报告.md"},
    {"编码守卫", "检查/编码守卫/编码守卫报告.md"},
    {"LoRa 检查", "检查/LoRa检查/LoRa检查报告.md"},
    {"协议校验", "检查/协议校验/协议校验报告.md"},
    {"固件预算", "检查/固件预算/固件预算报告.md"},
    {"断网缓存", "检查/断网缓存/断网缓存报告.md"},
};

/* 可视化取数用的工具表：key = 传给画布的键，rel = 脚本相对工具箱目录。 */
struct ToolDef {
    const char *key;
    const char *rel;
};

const ToolDef TOOLS[] = {
    {"pin",      "检查/引脚规划/pin_planner.py"},
    {"rtos",     "检查/RTOS检查/rtos_check.py"},
    {"enc",      "检查/编码守卫/encoding_guard.py"},
    {"progress", "检查/进度跟踪/progress_scan.py"},
    {"lora",     "检查/LoRa检查/lora_check.py"},
    {"protocol", "检查/协议校验/protocol_check.py"},
    {"firmware", "检查/固件预算/firmware_budget.py"},
    {"cache",    "检查/断网缓存/cache_check.py"},
};

/* 取报告前若干行做摘要：这些报告的开头都是总览表。 */
QString digest(const QString &text, int maxLines)
{
    QStringList out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        out << line;
        if (out.size() >= maxLines)
            break;
    }
    return out.join(QLatin1Char('\n'));
}

/* 仓库根：向上找到含 EnvMonitor 或 .git 的那层（与 main.cpp 的 findRepoRoot 同判据）。
   D3 用它把报告里的相对路径（脚本按仓库根相对打印）还原成绝对路径。 */
QString repoRootOf(const QString &toolsDir)
{
    QDir d(toolsDir);
    for (int i = 0; i < 8; ++i) {
        if (d.exists(QStringLiteral("EnvMonitor")) || d.exists(QStringLiteral(".git")))
            return d.absolutePath();
        if (!d.cdUp())
            break;
    }
    return toolsDir;
}

/* 健康度卡片的 key → 工具页标题（点卡片 / 点历史画布卡片都走这里）。 */
QString pageOfTool(const QString &key)
{
    static const QHash<QString, QString> pageOf{
        {QStringLiteral("pin"),      QStringLiteral("引脚规划")},
        {QStringLiteral("rtos"),     QStringLiteral("RTOS 检查")},
        {QStringLiteral("enc"),      QStringLiteral("编码守卫")},
        {QStringLiteral("lora"),     QStringLiteral("LoRa 检查")},
        {QStringLiteral("protocol"), QStringLiteral("协议校验")},
        {QStringLiteral("firmware"), QStringLiteral("固件预算")},
        {QStringLiteral("cache"),    QStringLiteral("断网缓存")},
        {QStringLiteral("progress"), QStringLiteral("进度扫描")},
    };
    return pageOf.value(key);
}

} // namespace

OverviewPage::OverviewPage(const QString &toolsDir, QWidget *parent)
    : QWidget(parent), m_toolsDir(toolsDir)
{
    QVBoxLayout *lay = new QVBoxLayout(this);

    QLabel *t = new QLabel(QStringLiteral("工程体检总览"), this);
    QFont tf = t->font();
    tf.setPointSize(tf.pointSize() + 3);
    tf.setBold(true);
    t->setFont(tf);
    lay->addWidget(t);

    QLabel *desc = new QLabel(QStringLiteral(
        "一屏看清项目健康度：健康度卡片、引脚占用热力图、Flash/RAM 占用、"
        "RTOS 任务与进度趋势。数据来自各检查工具的 --json，点「刷新」重新取数。"), this);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color:rgba(127,127,127,255);"));
    lay->addWidget(desc);

    /* E1：上手三步 —— 不必先搞清侧栏那 16 个页面，从这三个入口就能开始 */
    QLabel *first = new QLabel(QStringLiteral(
        "上手三步：① 先跑一次全量体检 ② 有问题按卡片点进去看 ③ 要开工就去生成工程骨架。"), this);
    first->setWordWrap(true);
    lay->addWidget(first);
    QHBoxLayout *entry = new QHBoxLayout;
    QPushButton *bFull = new QPushButton(QStringLiteral("① 跑全量体检"), this);
    QPushButton *bScaf = new QPushButton(QStringLiteral("③ 生成工程骨架"), this);
    QPushButton *bDoc  = new QPushButton(QStringLiteral("查单片机手册"), this);
    bFull->setToolTip(QStringLiteral("跳到「全量检查」页，一次跑完 9 个检查器"));
    bScaf->setToolTip(QStringLiteral("跳到「工程模板」页，生成能直接编译的工程骨架"));
    bDoc->setToolTip(QStringLiteral("跳到「单片机手册」查函数 / 流程卡"));
    connect(bFull, &QPushButton::clicked, this,
            [this]() { emit requestPage(QStringLiteral("全量检查")); });
    connect(bScaf, &QPushButton::clicked, this,
            [this]() { emit requestPage(QStringLiteral("工程模板")); });
    connect(bDoc, &QPushButton::clicked, this,
            [this]() { emit requestPage(QStringLiteral("单片机手册")); });
    entry->addWidget(bFull);
    entry->addWidget(bScaf);
    entry->addWidget(bDoc);
    entry->addStretch(1);
    lay->addLayout(entry);

    QHBoxLayout *bar = new QHBoxLayout;
    QPushButton *bRefresh = new QPushButton(QStringLiteral("刷新"), this);
    QPushButton *bOpen    = new QPushButton(QStringLiteral("打开报告目录"), this);
    m_status = new QLabel(QStringLiteral("尚未取数。"), this);
    m_status->setStyleSheet(QStringLiteral("color:rgba(127,127,127,255);"));
    bar->addWidget(bRefresh);
    bar->addWidget(bOpen);
    bar->addSpacing(12);
    bar->addWidget(m_status);
    bar->addStretch(1);
    lay->addLayout(bar);

    m_tabs = new QTabWidget(this);

    m_canvas = new DashboardCanvas(this);
    m_canvas->setObjectName(QStringLiteral("mainDashboard"));   /* 供 --shot-overview 精确定位 */
    QScrollArea *sa = new QScrollArea(this);
    sa->setWidget(m_canvas);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);

    m_view = new QTextBrowser(this);
    m_view->setOpenExternalLinks(false);
    /* D3：报告原文里把「文件:行」转成了 src:N 链接，点了打开对应源文件。
       所以这里要拦住默认跳转，自己处理。 */
    m_view->setOpenLinks(false);
    connect(m_view, &QTextBrowser::anchorClicked, this, [this](const QUrl &u) {
        const QString s = u.toString();
        if (s.startsWith(QStringLiteral("doc:"))) {
            /* C1：点了报告里的条目名 → 让主窗口把「单片机手册」跳到该条目 */
            const int idx = s.mid(4).toInt();
            if (idx >= 0 && idx < m_docTargets.size())
                emit requestDoc(m_docTargets.at(idx));
            return;
        }
        if (!s.startsWith(QStringLiteral("src:"))) {
            if (u.scheme().startsWith(QStringLiteral("http")))
                QDesktopServices::openUrl(u);      /* 报告里若带外链，仍按外部打开 */
            return;
        }
        const int idx = s.mid(4).toInt();
        if (idx < 0 || idx >= m_srcTargets.size())
            return;
        const QStringList parts = m_srcTargets.at(idx).split(QLatin1Char('|'));
        const QString srcPath = parts.value(0);
        if (!QFileInfo::exists(srcPath)) {
            m_status->setText(QStringLiteral("找不到源文件：%1").arg(srcPath));
            return;
        }
        /* 用系统默认编辑器打开；行号写进状态栏（QDesktopServices 不带行号定位） */
        QDesktopServices::openUrl(QUrl::fromLocalFile(srcPath));
        m_status->setText(QStringLiteral("已打开 %1（第 %2 行）")
                              .arg(QFileInfo(srcPath).fileName(), parts.value(1)));
    });

    m_tabs->addTab(sa, QStringLiteral("可视化仪表盘"));
    m_tabs->addTab(m_view, QStringLiteral("报告原文"));

    /* A1 历史回看：从 _体检历史/ 里挑一轮快照重新渲染，而不是只能跟上一轮比。 */
    QWidget *hist = new QWidget(this);
    QVBoxLayout *hl = new QVBoxLayout(hist);
    hl->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout *hbar = new QHBoxLayout;
    m_histCombo = new QComboBox(hist);
    m_histCombo->setMinimumWidth(220);
    m_histStatus = new QLabel(QStringLiteral("还没有历史快照。"), hist);
    m_histStatus->setStyleSheet(QStringLiteral("color:rgba(127,127,127,255);"));
    hbar->addWidget(new QLabel(QStringLiteral("选一轮快照："), hist));
    hbar->addWidget(m_histCombo);
    hbar->addSpacing(12);
    hbar->addWidget(m_histStatus, 1);
    hl->addLayout(hbar);
    m_histCanvas = new DashboardCanvas(hist);
    m_histCanvas->setObjectName(QStringLiteral("histDashboard"));
    QScrollArea *hsa = new QScrollArea(hist);
    hsa->setWidget(m_histCanvas);
    hsa->setWidgetResizable(true);
    hsa->setFrameShape(QFrame::NoFrame);
    hl->addWidget(hsa, 1);
    m_tabs->addTab(hist, QStringLiteral("历史回看"));
    lay->addWidget(m_tabs, 1);

    connect(bRefresh, &QPushButton::clicked, this, &OverviewPage::refresh);
    connect(bOpen, &QPushButton::clicked, this, &OverviewPage::openReportsDir);
    connect(m_histCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &OverviewPage::showHistorySnapshot);

    /* 点健康度卡片 → 跳到对应工具页（由主窗口接住，见 ToolboxWindow::jumpToPage）。
       当前轮画布与历史回看画布同一套映射。 */
    connect(m_canvas, &DashboardCanvas::toolActivated, this, [this](const QString &key) {
        const QString t = pageOfTool(key);
        if (!t.isEmpty())
            emit requestPage(t);
    });
    connect(m_histCanvas, &DashboardCanvas::toolActivated, this, [this](const QString &key) {
        const QString t = pageOfTool(key);
        if (!t.isEmpty())
            emit requestPage(t);
    });

    /* 构造时不取数（要跑 6 个 Python，会拖慢工具箱启动）；先读已有的 md 报告，
       首次切到本页时再自动取一次结构化数据。 */
    m_canvas->setNotice(QStringLiteral("首次打开本页会自动取数，或点「刷新」。"));
    m_histCanvas->setNotice(QStringLiteral("还没有历史快照。每轮体检会自动归档一份。"));
    loadTextReports();
    reloadHistory();
}

/* 首次显示本页才取数：避免工具箱一启动就跑一遍全部 Python 拖慢启动。 */
void OverviewPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (!m_firstShown) {
        m_firstShown = true;
        QTimer::singleShot(0, this, &OverviewPage::refresh);
    }
}

/* 异步取数：各检查器并行起进程，谁先回来谁先记账，全部到齐后统一渲染。
   原来是一个个同步 waitForFinished（点一次刷新卡 2~4 秒），改成并行 + 事件驱动。 */
void OverviewPage::refresh()
{
    if (m_toolsDir.isEmpty()) {
        m_canvas->setNotice(QStringLiteral("找不到工具箱目录，无法取数。"));
        m_status->setText(QStringLiteral("工具箱目录未解析"));
        return;
    }

    /* 清掉上一轮还在跑的进程（连点刷新时不叠罗汉）；先断连接，免得它们的
       finished 回调把新一轮的 m_pending 记账搅乱。 */
    for (QProcess *p : findChildren<QProcess *>()) {
        p->disconnect(this);
        p->kill();
        p->deleteLater();
    }

    m_tools.clear();
    m_failed.clear();
    m_pending = 0;

    const QStringList py = PyFind::detect();
    if (py.isEmpty()) {
        m_canvas->setNotice(QStringLiteral(
            "未找到 Python（已尝试 python / py -3 / python3）。装一个并加入 PATH 即可。"));
        m_status->setText(QStringLiteral("未找到 Python"));
        return;
    }

    for (const ToolDef &d : TOOLS) {
        const QString script = QDir(m_toolsDir).filePath(QString::fromUtf8(d.rel));
        const QString key = QString::fromUtf8(d.key);
        /* 测试钩子：--shot-overview --drop 指定的键直接当"没取到"，不真起进程 */
        if (m_dropKeys.contains(key)) {
            m_failed << key;
            continue;
        }
        if (!QFileInfo::exists(script)) {
            m_failed << key;
            continue;
        }

        QProcess *proc = new QProcess(this);
        proc->setWorkingDirectory(QFileInfo(script).absolutePath());
        QStringList a = py.mid(1);
        a << script << QStringLiteral("--json");

        /* 单个进程 30s 兜底超时，免得一个脚本卡住拖住整轮 */
        QTimer *tm = new QTimer(proc);
        tm->setSingleShot(true);
        tm->setInterval(30000);
        connect(tm, &QTimer::timeout, proc, &QProcess::kill);
        tm->start();

        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, key, proc](int, QProcess::ExitStatus) {
            onFetchFinished(key, proc);
        });

        proc->start(py.value(0), a);
        ++m_pending;
    }

    if (m_pending == 0) {
        finishFetch();
        return;
    }
    m_status->setText(QStringLiteral("正在取数…（并行跑 %1 个检查器）").arg(m_pending));
}

void OverviewPage::setDropKeys(const QStringList &keys)
{
    m_dropKeys = keys;
}

int OverviewPage::historyCount() const
{
    return m_histCombo ? m_histCombo->count() : 0;
}

void OverviewPage::onFetchFinished(const QString &key, QProcess *proc)
{
    const QByteArray out = proc->readAllStandardOutput();
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(out, &pe);
    if (pe.error == QJsonParseError::NoError && doc.isObject())
        m_tools.insert(key, doc.object().toVariantMap());
    else
        m_failed << key;

    proc->deleteLater();
    if (--m_pending <= 0)
        finishFetch();
}

void OverviewPage::finishFetch()
{
    /* 先把"没取到"的键告诉画布：缺数的卡片要显式标出，不能画成绿色"正常"。 */
    m_canvas->setFailed(m_failed);

    if (m_tools.isEmpty()) {
        m_canvas->setNotice(QStringLiteral(
            "%1 个检查器都没取到数据。请确认 Python 可用，或先跑一次「全量检查」。")
                                .arg(int(sizeof(TOOLS) / sizeof(TOOLS[0]))));
    } else {
        m_canvas->setTools(m_tools);
        /* 归档 + 对比：先读上次（写之前，最新那份就是上次），再写本次 */
        const QVariantMap prev = readPrevSnapshot();
        writeSnapshot(m_tools);
        m_canvas->setDelta(computeDelta(prev, m_tools));
        reloadHistory();   /* A1：多了一轮快照，历史回看的下拉列表跟着更新 */
    }

    QString st = QStringLiteral("已取数 %1 / %2")
                     .arg(m_tools.size())
                     .arg(int(sizeof(TOOLS) / sizeof(TOOLS[0])));
    if (!m_failed.isEmpty())
        st += QStringLiteral("，未取到：%1").arg(m_failed.join(QStringLiteral("、")));
    st += QStringLiteral("　·　%1")
              .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
    m_status->setText(st);

    loadTextReports();
}

/* ---------- 体检快照：归档（D2） + 与上次对比（A3） ---------- */

QString OverviewPage::snapshotDir() const
{
    return QDir(m_toolsDir).filePath(QStringLiteral("_体检历史"));
}

/* 快照文件名是 yyyyMMdd_HHmmss.json，按名字升序即按时间升序（旧 → 新）。 */
QStringList OverviewPage::listSnapshots() const
{
    QDir d(snapshotDir());
    QStringList out;
    for (const QString &f : d.entryList(QStringList{QStringLiteral("*.json")},
                                        QDir::Files, QDir::Name))
        out << d.filePath(f);
    return out;
}

QVariantMap OverviewPage::readSnapshot(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
}

QVariantMap OverviewPage::readPrevSnapshot() const
{
    const QStringList files = listSnapshots();
    if (files.isEmpty())
        return {};
    return readSnapshot(files.last());   /* 名字按时间排，最后一个最新 */
}

void OverviewPage::writeSnapshot(const QVariantMap &tools)
{
    const QString dir = snapshotDir();
    QDir().mkpath(dir);

    QJsonObject obj;
    for (auto it = tools.constBegin(); it != tools.constEnd(); ++it)
        obj.insert(it.key(), QJsonValue::fromVariant(it.value()));

    QFile f(QDir(dir).filePath(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))
        + QStringLiteral(".json")));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));

    /* 只留最近 20 份，免得无限膨胀 */
    QDir d(dir);
    const QStringList files = d.entryList(QStringList{QStringLiteral("*.json")},
                                          QDir::Files, QDir::Name);
    for (int i = 0; i < files.size() - 20; ++i)
        QFile::remove(d.filePath(files.at(i)));
}

QStringList OverviewPage::computeDelta(const QVariantMap &prev, const QVariantMap &cur) const
{
    if (prev.isEmpty())
        return {QStringLiteral("首次体检，暂无对比基线")};

    auto pinConf = [](const QVariantMap &t) {
        int n = 0;
        for (const QVariant &b : t.value(QStringLiteral("pin")).toMap()
                                     .value(QStringLiteral("boards")).toList())
            n += b.toMap().value(QStringLiteral("conflicts")).toMap().size();
        return n;
    };
    auto rtosSev = [](const QVariantMap &t) {
        int n = 0;
        for (const QVariant &b : t.value(QStringLiteral("rtos")).toMap()
                                     .value(QStringLiteral("boards")).toList())
            n += b.toMap().value(QStringLiteral("severe")).toInt();
        return n;
    };
    auto encVio = [](const QVariantMap &t) {
        return t.value(QStringLiteral("enc")).toMap()
                .value(QStringLiteral("violations")).toInt();
    };
    auto loraIss = [](const QVariantMap &t) {
        const QVariantMap m = t.value(QStringLiteral("lora")).toMap();
        int n = m.value(QStringLiteral("cross")).toList().size();
        for (const QVariant &b : m.value(QStringLiteral("boards")).toList())
            n += b.toMap().value(QStringLiteral("issues")).toList().size();
        return n;
    };
    auto pv = [](const QVariantMap &t, const char *key) {
        return t.value(QStringLiteral("progress")).toMap()
                .value(QString::fromLatin1(key)).toInt();
    };
    auto fmt = [](const QString &name, int a, int b) {
        const int d = b - a;
        if (d == 0)
            return QStringLiteral("%1 持平").arg(name);
        return QStringLiteral("%1 %2%3")
            .arg(name, d > 0 ? QStringLiteral("↑") : QStringLiteral("↓"))
            .arg(qAbs(d));
    };

    return {
        fmt(QStringLiteral("引脚冲突"), pinConf(prev), pinConf(cur)),
        fmt(QStringLiteral("RTOS 严重"), rtosSev(prev), rtosSev(cur)),
        fmt(QStringLiteral("编码违规"), encVio(prev), encVio(cur)),
        fmt(QStringLiteral("LoRa 问题"), loraIss(prev), loraIss(cur)),
        fmt(QStringLiteral("待办标记"), pv(prev, "markers"), pv(cur, "markers")),
        fmt(QStringLiteral("待实现桩"), pv(prev, "stubs"), pv(cur, "stubs")),
    };
}

void OverviewPage::loadTextReports()
{
    /* D3：报告里的「文件:行」转成可点链接 —— 点了直接用系统默认编辑器打开该源文件。
       为什么用下标锚点（src:N）而不是把路径塞进 URL：这里的路径是 Windows 反斜杠相对路径，
       直接当 URL 会被百分号编码搞得对不上。见 overviewpage.h 的 m_srcTargets。
       C1：同一次扫描里把「条目名」（全大写的下划线标识符，如 SYS_FRAME_BuildEnv）也转成
       可点链接（doc:N），点了跳「单片机手册」对应卡。两件事必须一趟做完 —— 分两趟的话
       第二趟会扫进第一趟生成的 [x](src:1)，把链接本身改坏。 */
    m_srcTargets.clear();
    m_docTargets.clear();
    const QString repoRoot = repoRootOf(m_toolsDir);
    static const QRegularExpression re(QStringLiteral(
        "`?([A-Za-z0-9_\\-./\\\\]+\\.(?:c|h|cpp|hpp|py|js|md|txt|s)):(\\d+)`?"
        "|\\b([A-Z][A-Z0-9]{1,}(?:_[A-Za-z0-9]+)+)\\b"));
    auto linkify = [this, &repoRoot](const QString &s) {
        QString out;
        int last = 0;
        auto it = re.globalMatch(s);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            out += s.mid(last, m.capturedStart() - last);
            if (!m.captured(1).isEmpty()) {
                const QString rel = m.captured(1);
                m_srcTargets << (QDir(repoRoot).filePath(rel) + QLatin1Char('|') + m.captured(2));
                out += QStringLiteral("[%1:%2](src:%3)")
                           .arg(rel, m.captured(2)).arg(m_srcTargets.size() - 1);
            } else {
                const QString term = m.captured(3);
                m_docTargets << term;
                out += QStringLiteral("[%1](doc:%2)")
                           .arg(term).arg(m_docTargets.size() - 1);
            }
            last = m.capturedEnd();
        }
        out += s.mid(last);
        return out;
    };

    QStringList md;
    md << QStringLiteral("# 工程体检总览") << QString();
    int found = 0;
    for (const ReportDef &r : REPORTS) {
        const QString path = QDir(m_toolsDir).filePath(QString::fromUtf8(r.rel));
        md << QStringLiteral("## %1").arg(QString::fromUtf8(r.title));
        if (!QFileInfo::exists(path)) {
            md << QStringLiteral("> 还没生成。先跑一次「全量检查」或对应工具。") << QString();
            continue;
        }
        ++found;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            md << QStringLiteral("> 打不开：`%1`").arg(path) << QString();
            continue;
        }
        const QString text = QString::fromUtf8(f.readAll());
        f.close();
        const QDateTime mt = QFileInfo(path).lastModified();
        md << QStringLiteral("> 文件：`%1`　生成时间：%2")
                  .arg(QDir(m_toolsDir).relativeFilePath(path),
                       mt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
           << QString();
        /* 摘要从 16 行放宽到 60 行：报告里的「命中项表」（含 文件:行）一般在 25 行往后，
           只取 16 行的话可点链接根本露不出来，D3 就成了摆设。 */
        md << linkify(digest(text, 60));
        md << QStringLiteral("---") << QString();
    }
    m_view->setMarkdown(md.join(QLatin1Char('\n')));
    if (m_status && found == 0 && !m_firstShown)
        m_status->setText(QStringLiteral("报告都还没生成。"));
}

void OverviewPage::openReportsDir()
{
    const QString dir = QDir(m_toolsDir).filePath(QStringLiteral("检查"));
    if (QFileInfo::exists(dir))
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

/* ---------- A1 历史回看：把某一轮快照重新渲染出来 ---------- */

/* 重列下拉框（最新在最前）；尽量保持原选中项，再渲染那一轮。 */
void OverviewPage::reloadHistory()
{
    if (!m_histCombo)
        return;
    const QString keep = m_histCombo->currentData().toString();
    const QStringList files = listSnapshots();

    m_histCombo->blockSignals(true);
    m_histCombo->clear();
    for (int i = files.size() - 1; i >= 0; --i) {   /* 最新的排最前 */
        const QString base = QFileInfo(files.at(i)).completeBaseName();
        const QDateTime dt = QDateTime::fromString(base, QStringLiteral("yyyyMMdd_HHmmss"));
        m_histCombo->addItem(dt.isValid()
                                 ? dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                 : base,
                             files.at(i));
    }
    int idx = m_histCombo->findData(keep);
    if (idx < 0)
        idx = 0;
    m_histCombo->setCurrentIndex(idx);
    m_histCombo->blockSignals(false);

    showHistorySnapshot();
}

void OverviewPage::showHistorySnapshot()
{
    if (m_histCombo)
        renderHistory(m_histCombo->currentData().toString());
}

void OverviewPage::renderHistory(const QString &path)
{
    if (!m_histCanvas)
        return;
    if (path.isEmpty()) {
        m_histCanvas->setNotice(QStringLiteral("还没有历史快照。每轮体检会自动归档一份。"));
        if (m_histStatus)
            m_histStatus->setText(QStringLiteral("共 0 轮"));
        return;
    }
    const QVariantMap tools = readSnapshot(path);
    if (tools.isEmpty()) {
        m_histCanvas->setNotice(QStringLiteral("这份快照读不出来（文件可能损坏）。"));
        if (m_histStatus)
            m_histStatus->setText(QStringLiteral("读取失败"));
        return;
    }

    /* 该轮没取到的工具：快照只存成功的，缺的键就是"那一轮没取到" */
    QStringList missing;
    for (const ToolDef &d : TOOLS) {
        const QString key = QString::fromUtf8(d.key);
        if (!tools.contains(key))
            missing << key;
    }
    m_histCanvas->setFailed(missing);   /* 缺数照样不许画成绿色"正常" */
    m_histCanvas->setTools(tools);

    /* 与上一轮（比它更旧的那份）对比：让"这一轮变好还是变差"有基线 */
    const QStringList files = listSnapshots();
    const int i = files.indexOf(path);
    m_histCanvas->setDelta(computeDelta(i > 0 ? readSnapshot(files.at(i - 1)) : QVariantMap(),
                                        tools));

    if (m_histStatus) {
        QString st = QStringLiteral("第 %1 / %2 轮　·　%3 个工具")
                         .arg(files.size() - i).arg(files.size()).arg(tools.size());
        if (!missing.isEmpty())
            st += QStringLiteral("，该轮未取到：%1").arg(missing.join(QStringLiteral("、")));
        m_histStatus->setText(st);
    }
}
