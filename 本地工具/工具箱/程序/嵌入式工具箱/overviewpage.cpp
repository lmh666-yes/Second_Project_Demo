#include "overviewpage.h"

#include "dashboardcanvas.h"
#include "pyfind.h"

#include <QApplication>
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
    QScrollArea *sa = new QScrollArea(this);
    sa->setWidget(m_canvas);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);

    m_view = new QTextBrowser(this);
    m_view->setOpenExternalLinks(false);

    m_tabs->addTab(sa, QStringLiteral("可视化仪表盘"));
    m_tabs->addTab(m_view, QStringLiteral("报告原文"));
    lay->addWidget(m_tabs, 1);

    connect(bRefresh, &QPushButton::clicked, this, &OverviewPage::refresh);
    connect(bOpen, &QPushButton::clicked, this, &OverviewPage::openReportsDir);

    /* 点健康度卡片 → 跳到对应工具页（由主窗口接住，见 ToolboxWindow::jumpToPage）。 */
    connect(m_canvas, &DashboardCanvas::toolActivated, this, [this](const QString &key) {
        static const QHash<QString, QString> pageOf{
            {QStringLiteral("pin"),      QStringLiteral("引脚规划")},
            {QStringLiteral("rtos"),     QStringLiteral("RTOS 检查")},
            {QStringLiteral("enc"),      QStringLiteral("编码守卫")},
            {QStringLiteral("lora"),     QStringLiteral("LoRa 检查")},
            {QStringLiteral("protocol"), QStringLiteral("协议校验")},
            {QStringLiteral("firmware"), QStringLiteral("固件预算")},
            {QStringLiteral("progress"), QStringLiteral("进度扫描")},
        };
        const QString t = pageOf.value(key);
        if (!t.isEmpty())
            emit requestPage(t);
    });

    /* 构造时不取数（要跑 6 个 Python，会拖慢工具箱启动）；先读已有的 md 报告，
       首次切到本页时再自动取一次结构化数据。 */
    m_canvas->setNotice(QStringLiteral("首次打开本页会自动取数，或点「刷新」。"));
    loadTextReports();
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

QVariantMap OverviewPage::readPrevSnapshot() const
{
    QDir d(snapshotDir());
    const QStringList files = d.entryList(QStringList{QStringLiteral("*.json")},
                                          QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};
    QFile f(d.filePath(files.last()));   /* 名字按时间排，最后一个最新 */
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
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
        md << digest(text, 16);
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
