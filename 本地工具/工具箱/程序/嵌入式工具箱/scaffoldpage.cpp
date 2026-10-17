#include "scaffoldpage.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

ScaffoldPage::ScaffoldPage(const QString &toolsDir, QWidget *parent)
    : QWidget(parent), m_toolsDir(toolsDir)
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    m_alert = new QLabel(this);
    m_alert->setStyleSheet(QStringLiteral("color:#e74c3c;"));
    m_alert->hide();
    layout->addWidget(m_alert);

    /* 说明 */
    auto *desc = new QLabel(QStringLiteral(
        "根据预设配置一键生成 STM32 工程骨架。"
        "寄存器风格 = 只输出 main.c，里面全是 RCC/GPIO/USART 寄存器操作，不依赖任何库；"
        "驱动库风格 = 复制现有驱动 .h/.c，main.c 里调用库函数。\n"
        "「自定义外设 / 自定义任务」填了就以它们为准，不再看预设 —— "
        "起手助手的「一键生成工程骨架」就是往这两个框里填。"), this);
    desc->setWordWrap(true);
    layout->addWidget(desc);

    /* 预设选择 */
    auto *presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel(QStringLiteral("预设："), this));
    m_preset = new QComboBox(this);
    m_preset->addItem(QStringLiteral("最小骨架（裸机：时钟+串口+LED）"), QStringLiteral("minimal"));
    m_preset->addItem(QStringLiteral("最小 FreeRTOS（1 个 LED 闪烁任务）"), QStringLiteral("freertos"));
    m_preset->addItem(QStringLiteral("EnvMonitor 检测端完整版"), QStringLiteral("envmonitor"));
    presetRow->addWidget(m_preset, 1);
    layout->addLayout(presetRow);

    /* 风格选择 */
    auto *styleRow = new QHBoxLayout();
    styleRow->addWidget(new QLabel(QStringLiteral("风格："), this));
    m_style = new QComboBox(this);
    m_style->addItem(QStringLiteral("寄存器操作（推荐，单文件即可编译）"), QStringLiteral("register"));
    m_style->addItem(QStringLiteral("驱动库调用（复制现有库文件）"), QStringLiteral("driver"));
    styleRow->addWidget(m_style, 1);
    layout->addLayout(styleRow);

    /* 自定义外设 / 任务（C2：起手助手一键生成时填这里） */
    auto *periphRow = new QHBoxLayout();
    periphRow->addWidget(new QLabel(QStringLiteral("自定义外设："), this));
    m_periph = new QLineEdit(this);
    m_periph->setPlaceholderText(QStringLiteral(
        "留空用上面的预设；填了就按它生成，如 usart,led,tim,adc,i2c,spi"));
    periphRow->addWidget(m_periph, 1);
    layout->addLayout(periphRow);

    auto *tasksRow = new QHBoxLayout();
    tasksRow->addWidget(new QLabel(QStringLiteral("自定义任务："), this));
    m_tasks = new QLineEdit(this);
    m_tasks->setPlaceholderText(QStringLiteral(
        "留空=无任务；填了就按它生成，格式 Name:prio:stack（如 Sensor:3:256,Comm:4:256）"));
    tasksRow->addWidget(m_tasks, 1);
    layout->addLayout(tasksRow);

    /* 输出目录 */
    auto *outRow = new QHBoxLayout();
    outRow->addWidget(new QLabel(QStringLiteral("输出目录："), this));
    m_outDir = new QLineEdit(this);
    m_outDir->setPlaceholderText(QStringLiteral("选择或输入输出目录…"));
    outRow->addWidget(m_outDir, 1);
    m_btnBrowse = new QPushButton(QStringLiteral("浏览…"), this);
    connect(m_btnBrowse, &QPushButton::clicked, this, &ScaffoldPage::onBrowse);
    outRow->addWidget(m_btnBrowse);
    layout->addLayout(outRow);

    /* 生成按钮 */
    m_btnGen = new QPushButton(QStringLiteral("生成工程"), this);
    connect(m_btnGen, &QPushButton::clicked, this, &ScaffoldPage::onGenerate);
    layout->addWidget(m_btnGen);

    /* 日志输出 */
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setPlaceholderText(QStringLiteral("生成日志将显示在这里…"));
    layout->addWidget(m_log, 1);

    if (m_toolsDir.isEmpty())
        warnMissingToolsDir();
}

void ScaffoldPage::warnMissingToolsDir()
{
    m_alert->setText(QStringLiteral("找不到工具箱目录，无法定位生成器脚本。"));
    m_alert->show();
    m_btnGen->setEnabled(false);
}

void ScaffoldPage::onBrowse()
{
    QString d = QFileDialog::getExistingDirectory(this,
        QStringLiteral("选择输出目录"), m_outDir->text().isEmpty() ? QDir::homePath() : m_outDir->text());
    if (!d.isEmpty())
        m_outDir->setText(d);
}

void ScaffoldPage::onPresetChanged(int /*idx*/)
{
    /* 可扩展：根据预设显示对应外设列表 */
}

void ScaffoldPage::prefillCustom(const QString &periphCsv, const QString &tasksCsv,
                                 const QString &outDir)
{
    m_periph->setText(periphCsv);
    m_tasks->setText(tasksCsv);
    if (!outDir.isEmpty())
        m_outDir->setText(outDir);
    m_log->clear();
    m_log->appendPlainText(
        QStringLiteral("已从起手助手填入：\n  外设 %1\n  任务 %2\n输出目录 %3\n"
                       "确认无误后点「生成工程」（外设/任务认不出来的一律不硬塞，"
                       "生成后请核对 README 里的外设清单）。")
            .arg(periphCsv.isEmpty() ? QStringLiteral("(无)") : periphCsv,
                 tasksCsv.isEmpty() ? QStringLiteral("(无)") : tasksCsv,
                 m_outDir->text().isEmpty() ? QStringLiteral("(未指定)") : m_outDir->text()));
}

void ScaffoldPage::generateNow()
{
    onGenerate();
}

void ScaffoldPage::onGenerate()
{
    if (m_toolsDir.isEmpty()) {
        m_log->appendPlainText(QStringLiteral("错误：工具箱目录未解析"));
        return;
    }
    QString out = m_outDir->text().trimmed();
    if (out.isEmpty()) {
        m_log->appendPlainText(QStringLiteral("错误：请指定输出目录"));
        return;
    }

    QString preset = m_preset->currentData().toString();
    const QString per = m_periph->text().trimmed();
    const QString tsk = m_tasks->text().trimmed();
    QString script = QDir(m_toolsDir).filePath(QStringLiteral("程序/工程模板/project_scaffold.py"));
    if (!QFileInfo::exists(script)) {
        m_log->appendPlainText(QStringLiteral("错误：生成器脚本不存在：%1").arg(script));
        return;
    }

    /* E1 防呆：输出目录已有东西就问一句，免得把同名的 main.c 直接盖掉 */
    const QDir od(out);
    if (od.exists() && !od.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
        const auto ans = QMessageBox::question(
            this, QStringLiteral("输出目录不是空的"),
            QStringLiteral("「%1」里已经有文件了。生成会**覆盖同名的** main.c / README.md / "
                           "FreeRTOSConfig.h，但不会删别的文件。\n\n继续吗？").arg(out),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes) {
            m_log->appendPlainText(QStringLiteral("已取消：请换一个空目录，或确认可覆盖后再点。"));
            return;
        }
    }

    m_log->clear();
    if (!per.isEmpty() || !tsk.isEmpty()) {
        m_log->appendPlainText(QStringLiteral("正在生成（自定义）：periph=%1, tasks=%2, style=%3, out=%4")
                                   .arg(per.isEmpty() ? QStringLiteral("(无)") : per,
                                        tsk.isEmpty() ? QStringLiteral("(无)") : tsk,
                                        m_style->currentData().toString(), out));
    } else {
        m_log->appendPlainText(QStringLiteral("正在生成：preset=%1, style=%2, out=%3")
                                   .arg(preset, m_style->currentData().toString(), out));
    }
    m_btnGen->setEnabled(false);

    QProcess *proc = new QProcess(this);
    proc->setWorkingDirectory(QFileInfo(script).absolutePath());
    QStringList args;
    args << script;
    if (!per.isEmpty() || !tsk.isEmpty()) {
        /* 填了自定义就按它生成，不再看预设（preset 只是"没填时的快捷方式"） */
        if (!per.isEmpty())
            args << QStringLiteral("--periph") << per;
        if (!tsk.isEmpty())
            args << QStringLiteral("--tasks") << tsk;
    } else {
        args << QStringLiteral("--preset") << preset;
    }
    args << QStringLiteral("--style") << m_style->currentData().toString()
         << QStringLiteral("--out") << out;
    proc->start(QStringLiteral("python"), args);

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus) {
        m_log->appendPlainText(QString::fromUtf8(proc->readAllStandardOutput()));
        m_log->appendPlainText(QString::fromUtf8(proc->readAllStandardError()));
        m_log->appendPlainText(QStringLiteral("退出码: %1").arg(code));
        m_btnGen->setEnabled(true);
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            m_log->appendPlainText(QStringLiteral("启动失败：未找到 python"));
        m_btnGen->setEnabled(true);
        proc->deleteLater();
    });
}
