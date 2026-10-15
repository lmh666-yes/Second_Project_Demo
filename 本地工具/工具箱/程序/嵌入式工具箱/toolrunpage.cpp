#include "toolrunpage.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStandardPaths>
#include <QTextCursor>
#include <QTimer>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>

/* 依次尝试常见的 Python 启动方式。返回可用的程序名与固定前置参数。
   顺序：python（PATH 中的通用名）→ py -3（Windows 官方启动器）→ python3。 */
static QStringList detectPython()
{
    const struct { const char *prog; QStringList pre; } cands[] = {
        { "python",  {} },
        { "py",      { QStringLiteral("-3") } },
        { "python3", {} },
    };
    for (const auto &c : cands) {
        QProcess probe;
        QStringList a = c.pre;
        a << QStringLiteral("-c") << QStringLiteral("print(1)");
        probe.start(QString::fromLatin1(c.prog), a);
        if (probe.waitForStarted(3000) && probe.waitForFinished(4000)
            && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) {
            QStringList r;
            r << QString::fromLatin1(c.prog);
            r += c.pre;
            return r;
        }
    }
    return {};
}

ToolRunPage::ToolRunPage(const QString &title, const QString &desc, QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *lay = new QVBoxLayout(this);

    QLabel *t = new QLabel(title, this);
    QFont tf = t->font();
    tf.setPointSize(tf.pointSize() + 3);
    tf.setBold(true);
    t->setFont(tf);
    lay->addWidget(t);

    m_desc = new QLabel(desc, this);
    m_desc->setWordWrap(true);
    m_desc->setStyleSheet(QStringLiteral("color:%1;").arg(QStringLiteral("#8a8a8a")));
    lay->addWidget(m_desc);

    /* 目录缺失告警：默认隐藏，外部解析失败时显示 */
    m_alert = new QLabel(this);
    m_alert->setWordWrap(true);
    m_alert->setVisible(false);
    m_alert->setStyleSheet(QStringLiteral("color:#ffb4b4;background:#3a1c1c;"
                                          "border:1px solid #8a4a4a;border-radius:6px;padding:6px;"));
    lay->addWidget(m_alert);

    QHBoxLayout *opt = new QHBoxLayout;
    m_options = opt;
    opt->addStretch(1);
    lay->addLayout(opt);

    QHBoxLayout *bar = new QHBoxLayout;
    m_btnRun    = new QPushButton(QStringLiteral("运行"), this);
    m_btnStop   = new QPushButton(QStringLiteral("停止"), this);
    m_btnClear  = new QPushButton(QStringLiteral("清空输出"), this);
    m_btnCopy   = new QPushButton(QStringLiteral("复制全部"), this);
    m_btnSave   = new QPushButton(QStringLiteral("另存为…"), this);
    m_btnReport = new QPushButton(QStringLiteral("打开报告"), this);
    m_btnRun->setToolTip(QStringLiteral("运行（Ctrl+R）"));
    m_btnReport->setToolTip(QStringLiteral("打开报告（Ctrl+O）"));
    m_btnStop->setEnabled(false);
    m_btnReport->setEnabled(false);
    m_status = new QLabel(QStringLiteral("就绪"), this);
    m_status->setStyleSheet(QStringLiteral("color:%1;").arg(QStringLiteral("#8a8a8a")));
    bar->addWidget(m_btnRun);
    bar->addWidget(m_btnStop);
    bar->addWidget(m_btnClear);
    bar->addWidget(m_btnCopy);
    bar->addWidget(m_btnSave);
    bar->addWidget(m_btnReport);
    bar->addSpacing(12);
    bar->addWidget(m_status);
    bar->addStretch(1);

    /* 自定义动作按钮独立一行，外部按需挂（如链路仿真 → 协议工作台） */
    m_actionsLayout = new QHBoxLayout;
    lay->addLayout(bar);
    lay->addLayout(m_actionsLayout);

    /* 输出区顶部：搜索 + 跟随 */
    QHBoxLayout *sbar = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("在输出里查找…（回车跳到下一处）"));
    m_search->setClearButtonEnabled(true);
    m_find = new QLabel(this);
    m_follow = new QCheckBox(QStringLiteral("自动滚动到底"), this);
    m_follow->setChecked(true);
    sbar->addWidget(new QLabel(QStringLiteral("查找"), this));
    sbar->addWidget(m_search, 1);
    sbar->addWidget(m_find);
    sbar->addWidget(m_follow);
    lay->addLayout(sbar);

    m_out = new QPlainTextEdit(this);
    m_out->setReadOnly(true);
    m_out->setPlaceholderText(QStringLiteral("输出显示在这里"));
    m_out->setLineWrapMode(QPlainTextEdit::NoWrap);
    lay->addWidget(m_out);

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_btnRun, &QPushButton::clicked, this, &ToolRunPage::run);
    connect(m_btnStop, &QPushButton::clicked, this, &ToolRunPage::stop);
    connect(m_btnReport, &QPushButton::clicked, this, &ToolRunPage::openReport);
    connect(m_btnClear, &QPushButton::clicked, this, [this]() {
        m_out->clear();
        m_find->clear();
    });
    connect(m_btnCopy, &QPushButton::clicked, this, &ToolRunPage::copyAll);
    connect(m_btnSave, &QPushButton::clicked, this, &ToolRunPage::saveAs);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        append(decode(m_proc->readAllStandardOutput()));
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ToolRunPage::onFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            append(QStringLiteral("无法启动 Python。本页会自动尝试 python / py -3 / python3，"
                                  "都不行时请装一个 Python 并加入 PATH（勾选 Add to PATH）。\n"));
        }
    });

    /* 快捷键 */
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")), this, this, &ToolRunPage::run);
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+O")), this, this, &ToolRunPage::openReport);

    /* 单步超时看门狗：脚本卡住时自动终止，界面不会一直停在"运行中" */
    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    m_watchdog->setInterval(120000);
    connect(m_watchdog, &QTimer::timeout, this, [this]() {
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();
            append(QStringLiteral("\n[超时] 单步运行超过 %1 秒，已终止。"
                                  "长任务请改用该工具自己的 bat 入口。\n")
                       .arg(m_watchdog->interval() / 1000));
        }
        m_running = false;
        m_btnRun->setEnabled(true);
        m_btnStop->setEnabled(false);
        m_status->setText(QStringLiteral("已超时终止"));
        refreshReportButton();
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        if (!m_out->find(m_search->text()))
            m_find->setText(QStringLiteral("无匹配"));
        else
            m_find->clear();
    });
}

void ToolRunPage::setSteps(const QVector<RunStep> &steps)
{
    m_steps = steps;
    refreshReportButton();
}

void ToolRunPage::setReportPath(const QString &path)
{
    m_report = path;
    refreshReportButton();
}

QHBoxLayout *ToolRunPage::optionsLayout()
{
    return m_options;
}

void ToolRunPage::warnMissingToolsDir()
{
    m_alert->setText(QStringLiteral(
        "找不到工具箱目录（需同时含「设计」「检查」「shared」三个分组目录）。"
        "请把本程序放回 工具箱\\程序\\嵌入式工具箱\\ 下再启动，否则各工具的脚本路径无法解析。"));
    m_alert->setVisible(true);
}

void ToolRunPage::addAction(const QString &text, std::function<QString()> onRun)
{
    QPushButton *b = new QPushButton(text, this);
    m_actionsLayout->addWidget(b);
    connect(b, &QPushButton::clicked, this, [this, onRun]() {
        if (onRun) {
            const QString msg = onRun();
            if (!msg.isEmpty())
                m_status->setText(msg);
        }
    });
}

/* 报告可用性：每次运行结束、切换步骤后重算，不再依赖构造时的一次性判断。 */
void ToolRunPage::refreshReportButton()
{
    m_btnReport->setEnabled(!m_report.isEmpty() && QFileInfo::exists(m_report));
}

/* 进程输出解码：优先 UTF-8，失败回退本地编码。脚本自己用 UTF-8 打印，中文不会乱码。 */
QString ToolRunPage::decode(const QByteArray &raw) const
{
    QString s = QString::fromUtf8(raw);
    if (s.contains(QChar(0xFFFD))) {
        const QString alt = QString::fromLocal8Bit(raw);
        if (!alt.contains(QChar(0xFFFD)))
            return alt;
    }
    return s;
}

void ToolRunPage::append(const QString &text)
{
    m_out->moveCursor(QTextCursor::End);
    m_out->insertPlainText(text);
    const bool follow = m_follow->isChecked();
    m_out->moveCursor(QTextCursor::End);
    if (follow)
        m_out->verticalScrollBar()->setValue(m_out->verticalScrollBar()->maximum());
}

void ToolRunPage::run()
{
    if (m_running || m_steps.isEmpty())
        return;
    m_out->clear();
    m_find->clear();
    m_stepCodes.clear();
    m_running = true;
    m_btnRun->setEnabled(false);
    m_btnStop->setEnabled(true);
    m_btnReport->setEnabled(false);
    startStep(0);
}

void ToolRunPage::stop()
{
    if (m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        append(QStringLiteral("\n[已停止]\n"));
    }
    m_watchdog->stop();
    m_running = false;
    m_btnRun->setEnabled(true);
    m_btnStop->setEnabled(false);
    m_status->setText(QStringLiteral("已停止"));
    refreshReportButton();
}

void ToolRunPage::startStep(int index)
{
    if (index >= m_steps.size()) {
        m_running = false;
        m_watchdog->stop();
        m_btnRun->setEnabled(true);
        m_btnStop->setEnabled(false);
        printSummary();
        m_status->setText(QStringLiteral("完成"));
        refreshReportButton();
        return;
    }
    m_index = index;
    const RunStep &s = m_steps[index];
    append(QStringLiteral("==== %1 ====\n").arg(s.label));

    /* 每次运行都重新探测解释器：换了机器或改了 PATH 不用重启程序 */
    m_python = detectPython();
    if (m_python.isEmpty()) {
        append(QStringLiteral("未找到可用的 Python（已尝试 python、py -3、python3）。\n"
                              "请装一个 Python 并加入 PATH（安装时勾选 Add to PATH），重开本程序即可。\n"));
        m_running = false;
        m_btnRun->setEnabled(true);
        m_btnStop->setEnabled(false);
        m_status->setText(QStringLiteral("缺少 Python"));
        return;
    }

    QStringList a = m_python.mid(1);
    a << s.script;
    if (s.args)
        a += s.args();

    m_proc->setWorkingDirectory(QFileInfo(s.script).absolutePath());
    m_lastWorkDir = QFileInfo(s.script).absolutePath();
    m_proc->setProgram(m_python.value(0));
    m_proc->setArguments(a);
    m_status->setText(QStringLiteral("运行中：%1").arg(s.label));
    m_proc->start();
    m_watchdog->start();          /* 单步超时保护：卡住自动终止 */
    if (!m_proc->waitForStarted(4000)) {
        append(QStringLiteral("启动失败：%1\n").arg(m_proc->errorString()));
        m_watchdog->stop();       /* 否则 120s 后看门狗空转，把状态改成"已超时终止" */
        m_running = false;
        m_btnRun->setEnabled(true);
        m_btnStop->setEnabled(false);
        m_status->setText(QStringLiteral("启动失败"));
    }
}

void ToolRunPage::onFinished(int exitCode, int status)
{
    Q_UNUSED(status);
    m_stepCodes.append(exitCode);
    append(QStringLiteral("\n[退出码 %1]\n\n").arg(exitCode));
    if (m_running)
        startStep(m_index + 1);
}

/* 多步时在末尾打印一张汇总表，一眼看出哪项没过。单步不打印（没必要）。 */
void ToolRunPage::printSummary()
{
    if (m_steps.size() < 2 || m_stepCodes.isEmpty())
        return;
    QStringList out;
    out << QStringLiteral("──────── 汇总 ────────");
    int bad = 0;
    for (int i = 0; i < m_steps.size(); ++i) {
        const int code = (i < m_stepCodes.size()) ? m_stepCodes[i] : -1;
        const bool pass = (code == 0);
        if (!pass)
            ++bad;
        out << QStringLiteral("%1  %2（退出码 %3）")
                   .arg(pass ? QStringLiteral("[通过]") : QStringLiteral("[未通过]"),
                        m_steps[i].label)
                   .arg(code);
    }
    out << (bad == 0 ? QStringLiteral("全部通过。")
                     : QStringLiteral("共 %1 项未通过，见上方对应输出。").arg(bad));
    out << QString();
    append(out.join('\n'));
}

void ToolRunPage::copyAll()
{
    QApplication::clipboard()->setText(m_out->toPlainText());
    m_status->setText(QStringLiteral("已复制输出"));
}

void ToolRunPage::saveAs()
{
    const QString def = QDir::homePath() + QStringLiteral("/工具箱输出.txt");
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("另存输出"), def, QStringLiteral("文本 (*.txt);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        append(QStringLiteral("无法写入：%1\n").arg(path));
        return;
    }
    f.write(m_out->toPlainText().toUtf8());
    f.close();
    m_status->setText(QStringLiteral("已另存为 %1").arg(path));
}

void ToolRunPage::openReport()
{
    if (!m_report.isEmpty() && QFileInfo::exists(m_report))
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_report));
    else
        append(QStringLiteral("报告尚未生成，先运行一次。\n"));
}

/* 有的页只有产物目录（链路仿真出的是 hex/log，没有 md 报告），
   按钮就不能叫"打开报告"——按实际目标改文字，别让标签和行为对不上。 */
void ToolRunPage::setReportLabel(const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    if (m_btnReport) {
        m_btnReport->setText(text);
        m_btnReport->setToolTip(QStringLiteral("%1（Ctrl+O）").arg(text));
    }
}
