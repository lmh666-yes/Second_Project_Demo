#include "scaffoldpage.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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
        "驱动库风格 = 复制现有驱动 .h/.c，main.c 里调用库函数。"), this);
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
    QString script = QDir(m_toolsDir).filePath(QStringLiteral("程序/工程模板/project_scaffold.py"));
    if (!QFileInfo::exists(script)) {
        m_log->appendPlainText(QStringLiteral("错误：生成器脚本不存在：%1").arg(script));
        return;
    }

    m_log->clear();
    m_log->appendPlainText(QStringLiteral("正在生成：preset=%1, style=%2, out=%3")
                               .arg(preset, m_style->currentData().toString(), out));
    m_btnGen->setEnabled(false);

    QProcess *proc = new QProcess(this);
    proc->setWorkingDirectory(QFileInfo(script).absolutePath());
    QStringList args;
    args << script << QStringLiteral("--preset") << preset
         << QStringLiteral("--style") << m_style->currentData().toString()
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
