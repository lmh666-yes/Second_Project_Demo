#include "buildflashpage.h"

#include "pyfind.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

BuildFlashPage::BuildFlashPage(const QString &toolsDir, QWidget *parent)
    : QWidget(parent), m_toolsDir(toolsDir)
{
    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(8);

    m_alert = new QLabel(this);
    m_alert->setStyleSheet(QStringLiteral("color:#e74c3c;"));
    m_alert->hide();
    lay->addWidget(m_alert);

    auto *desc = new QLabel(QStringLiteral(
        "一条命令走完「编译 → 烧录 → 板载自检」：编译用 Keil 命令行（许可证不通会自动换下一个 UV4），"
        "烧录用 STM32_Programmer_CLI，自检靠串口里的启动横幅。每次编译都会记体积，可看趋势。"), this);
    desc->setWordWrap(true);
    lay->addWidget(desc);

    /* 板子 + 串口 */
    auto *row = new QHBoxLayout();
    row->addWidget(new QLabel(QStringLiteral("板子："), this));
    m_board = new QComboBox(this);
    m_board->addItem(QStringLiteral("板1 检测端"), QStringLiteral("板1"));
    m_board->addItem(QStringLiteral("板2 通信端"), QStringLiteral("板2"));
    m_board->addItem(QStringLiteral("OTA 工程"), QStringLiteral("OTA"));
    m_board->addItem(QStringLiteral("全部"), QStringLiteral("all"));
    row->addWidget(m_board, 1);

    row->addWidget(new QLabel(QStringLiteral("串口："), this));
    m_com = new QComboBox(this);
    m_com->setEditable(true);
    m_com->setMinimumWidth(150);
    m_com->addItem(QString());            /* 空 = 不指定 */
    row->addWidget(m_com);

    m_btnDetect = new QPushButton(QStringLiteral("探测"), this);
    m_btnDetect->setToolTip(QStringLiteral("探测 Keil / 烧录器 / 可用串口"));
    connect(m_btnDetect, &QPushButton::clicked, this, &BuildFlashPage::onDetect);
    row->addWidget(m_btnDetect);
    lay->addLayout(row);

    /* 动作按钮 */
    auto *acts = new QHBoxLayout();
    struct { const char *text; void (BuildFlashPage::*slot)(); const char *tip; } defs[] = {
        {"编译", &BuildFlashPage::onBuild, "Keil 命令行编译，抓错误行并记体积"},
        {"烧录", &BuildFlashPage::onFlash, "把 Objects/*.hex 烧进去"},
        {"串口自检", &BuildFlashPage::onSerial, "开串口收启动横幅，按 banner 判 PASS/FAIL"},
        {"全流程", &BuildFlashPage::onAll, "编译 + 烧录 + 串口自检"},
        {"体积趋势", &BuildFlashPage::onHistory, "看 Flash/RAM 变化与占用率"},
        {"状态快照", &BuildFlashPage::onSnapshot, "编译并把结果打包成 状态_时间/ 留档"},
        {"生成阶段自检", &BuildFlashPage::onGenSelftest,
         "按阶段生成 board_selftest.c（时钟/队列/组帧 CRC 回环），串口回 STAGE<N>=PASS"},
    };
    for (const auto &d : defs) {
        auto *b = new QPushButton(QString::fromUtf8(d.text), this);
        b->setToolTip(QString::fromUtf8(d.tip));
        connect(b, &QPushButton::clicked, this, d.slot);
        acts->addWidget(b);
        m_actions.append(b);
    }
    lay->addLayout(acts);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setPlaceholderText(QStringLiteral("输出显示在这里…（首次用先点「探测」）"));
    lay->addWidget(m_log, 1);

    if (m_toolsDir.isEmpty())
        warnMissingToolsDir();
}

void BuildFlashPage::warnMissingToolsDir()
{
    m_alert->setText(QStringLiteral("找不到工具箱目录，无法定位 build_flash.py。"));
    m_alert->show();
    for (auto *b : m_actions)
        b->setEnabled(false);
    m_btnDetect->setEnabled(false);
}

QString BuildFlashPage::scriptPath() const
{
    if (m_toolsDir.isEmpty())
        return QString();
    return QDir(m_toolsDir).filePath(QStringLiteral("检查/构建烧录/build_flash.py"));
}

void BuildFlashPage::runScript(const QStringList &extra, const QString &title)
{
    const QString script = scriptPath();
    if (script.isEmpty() || !QFileInfo::exists(script)) {
        m_log->appendPlainText(QStringLiteral("错误：找不到 %1").arg(script));
        return;
    }
    const QStringList py = PyFind::detect();
    if (py.isEmpty()) {
        m_log->appendPlainText(QStringLiteral("错误：没找到 Python"));
        return;
    }

    m_log->appendPlainText(QStringLiteral("\n===== %1 =====").arg(title));
    for (auto *b : m_actions)
        b->setEnabled(false);
    m_btnDetect->setEnabled(false);

    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(QFileInfo(script).absolutePath());
    QStringList args = py.mid(1);
    args << script << extra;
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        m_log->appendPlainText(QString::fromUtf8(proc->readAllStandardOutput()).trimmed());
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
        const QString s = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        if (!s.isEmpty())
            m_log->appendPlainText(s);
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus) {
        m_log->appendPlainText(QStringLiteral("[退出码 %1]").arg(code));
        for (auto *b : m_actions)
            b->setEnabled(true);
        m_btnDetect->setEnabled(true);
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            m_log->appendPlainText(QStringLiteral("启动失败：Python 不可用"));
        for (auto *b : m_actions)
            b->setEnabled(true);
        m_btnDetect->setEnabled(true);
        proc->deleteLater();
    });
    proc->start(py.value(0), args);
}

void BuildFlashPage::onDetect()
{
    const QString script = scriptPath();
    if (script.isEmpty() || !QFileInfo::exists(script)) {
        m_log->appendPlainText(QStringLiteral("错误：找不到 %1").arg(script));
        return;
    }
    const QStringList py = PyFind::detect();
    if (py.isEmpty()) {
        m_log->appendPlainText(QStringLiteral("错误：没找到 Python"));
        return;
    }
    m_log->appendPlainText(QStringLiteral("\n===== 探测工具链 ====="));
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(QFileInfo(script).absolutePath());
    QStringList args = py.mid(1);
    args << script << QStringLiteral("--detect") << QStringLiteral("--json");
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int, QProcess::ExitStatus) {
        const QByteArray raw = proc->readAllStandardOutput();
        const QJsonObject o = QJsonDocument::fromJson(raw).object();
        const QJsonArray ports = o.value(QStringLiteral("ports")).toArray();
        const QString keep = m_com->currentText();
        m_com->blockSignals(true);
        m_com->clear();
        m_com->addItem(QString());
        for (const QJsonValue &v : ports) {
            const QJsonArray one = v.toArray();      /* [COM3, 描述] */
            if (one.size() >= 1) {
                const QString p = one.at(0).toString();
                const QString d = one.size() > 1 ? one.at(1).toString() : QString();
                m_com->addItem(d.isEmpty() ? p : QStringLiteral("%1（%2）").arg(p, d), p);
            }
        }
        m_com->setEditText(keep);
        m_com->blockSignals(false);

        m_log->appendPlainText(QStringLiteral("UV4："));
        for (const QJsonValue &v : o.value(QStringLiteral("uv4")).toArray())
            m_log->appendPlainText(QStringLiteral("  ") + v.toString());
        m_log->appendPlainText(QStringLiteral("烧录器："));
        for (const QJsonValue &v : o.value(QStringLiteral("programmer")).toArray())
            m_log->appendPlainText(QStringLiteral("  ") + v.toString());
        m_log->appendPlainText(QStringLiteral("串口：%1 个（已填进下拉框）").arg(ports.size()));
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        m_log->appendPlainText(QStringLiteral("探测失败：Python 不可用"));
        proc->deleteLater();
    });
    proc->start(py.value(0), args);
}

void BuildFlashPage::onBuild()
{
    runScript({QStringLiteral("--build"), QStringLiteral("--board"),
               m_board->currentData().toString()}, QStringLiteral("编译"));
}

void BuildFlashPage::onFlash()
{
    runScript({QStringLiteral("--flash"), QStringLiteral("--board"),
               m_board->currentData().toString()}, QStringLiteral("烧录"));
}

void BuildFlashPage::onSerial()
{
    QStringList a{QStringLiteral("--serial"), QStringLiteral("--board"),
                  m_board->currentData().toString()};
    const QString com = m_com->currentData().toString().isEmpty()
                            ? m_com->currentText().trimmed()
                            : m_com->currentData().toString();
    if (!com.isEmpty())
        a << QStringLiteral("--com") << com;
    runScript(a, QStringLiteral("串口自检"));
}

void BuildFlashPage::onAll()
{
    QStringList a{QStringLiteral("--all"), QStringLiteral("--board"),
                  m_board->currentData().toString()};
    const QString com = m_com->currentData().toString().isEmpty()
                            ? m_com->currentText().trimmed()
                            : m_com->currentData().toString();
    if (!com.isEmpty())
        a << QStringLiteral("--com") << com;
    runScript(a, QStringLiteral("全流程（编译 → 烧录 → 自检）"));
}

void BuildFlashPage::onHistory()
{
    runScript({QStringLiteral("--history"), QStringLiteral("--board"),
               m_board->currentData().toString()}, QStringLiteral("体积趋势"));
}

void BuildFlashPage::onSnapshot()
{
    runScript({QStringLiteral("--snapshot"), QStringLiteral("--board"),
               m_board->currentData().toString()}, QStringLiteral("状态快照"));
}

/* A4：生成阶段自检代码。走的是同一个目录下的 gen_selftest.py，
   生成完顺手做一次 armcc 编译校验（缺头文件脚本自己会补 -I）。 */
void BuildFlashPage::onGenSelftest()
{
    const QString script = m_toolsDir.isEmpty()
                               ? QString()
                               : QDir(m_toolsDir).filePath(
                                     QStringLiteral("检查/构建烧录/gen_selftest.py"));
    if (script.isEmpty() || !QFileInfo::exists(script)) {
        m_log->appendPlainText(QStringLiteral("错误：找不到 %1").arg(script));
        return;
    }
    const QStringList py = PyFind::detect();
    if (py.isEmpty()) {
        m_log->appendPlainText(QStringLiteral("错误：没找到 Python"));
        return;
    }
    m_log->appendPlainText(QStringLiteral("\n===== 生成阶段自检 ====="));
    for (auto *b : m_actions)
        b->setEnabled(false);
    m_btnDetect->setEnabled(false);

    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(QFileInfo(script).absolutePath());
    QStringList args = py.mid(1);
    args << script << QStringLiteral("--board") << m_board->currentData().toString()
         << QStringLiteral("--check");
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        m_log->appendPlainText(QString::fromUtf8(proc->readAllStandardOutput()).trimmed());
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus) {
        m_log->appendPlainText(QStringLiteral("[退出码 %1]").arg(code));
        for (auto *b : m_actions)
            b->setEnabled(true);
        m_btnDetect->setEnabled(true);
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        m_log->appendPlainText(QStringLiteral("启动失败：Python 不可用"));
        for (auto *b : m_actions)
            b->setEnabled(true);
        m_btnDetect->setEnabled(true);
        proc->deleteLater();
    });
    proc->start(py.value(0), args);
}
