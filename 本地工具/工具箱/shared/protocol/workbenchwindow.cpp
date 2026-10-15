#include "workbenchwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "protocol.h"

WorkbenchWindow::WorkbenchWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString::fromUtf8("协议工作台"));

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildBuildTab(),  QString::fromUtf8("组帧"));
    m_tabs->addTab(buildParseTab(),  QString::fromUtf8("解帧"));
    m_tabs->addTab(buildInjectTab(), QString::fromUtf8("坏帧注入"));
    m_tabs->addTab(buildTestTab(),   QString::fromUtf8("自检"));
    setCentralWidget(m_tabs);
}

void WorkbenchWindow::loadIntoParser(const QString &hex)
{
    if (!m_parseIn)
        return;
    m_parseIn->setPlainText(hex.trimmed());
    if (m_tabs)
        m_tabs->setCurrentIndex(1);         /* 切到解帧页 */
    if (m_parseIn->isVisible())
        m_parseIn->setFocus();
}

static QByteArray sampleEnvFrame()
{
    const QByteArray env = Proto::buildEnvData(23.45, 60.10, 1013.2, 1200, 150, 2048);
    return Proto::buildFrame(0x01, env);
}

QWidget *WorkbenchWindow::buildBuildTab()
{
    QWidget *w = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(w);

    QHBoxLayout *row = new QHBoxLayout;
    row->addWidget(new QLabel(QString::fromUtf8("命令字（hex）")));
    m_buildCmd = new QLineEdit(QStringLiteral("16"));
    m_buildCmd->setFixedWidth(90);
    row->addWidget(m_buildCmd);
    row->addStretch(1);
    lay->addLayout(row);

    lay->addWidget(new QLabel(QString::fromUtf8("载荷（hex，可带空格）")));
    m_buildData = new QPlainTextEdit;
    m_buildData->setPlaceholderText(QString::fromUtf8("例如：01（空载荷留空即可）"));
    m_buildData->setPlainText(QStringLiteral("01"));
    m_buildData->setFixedHeight(80);
    lay->addWidget(m_buildData);

    QHBoxLayout *bar = new QHBoxLayout;
    QPushButton *gen  = new QPushButton(QString::fromUtf8("生成帧"));
    QPushButton *copy = new QPushButton(QString::fromUtf8("复制结果"));
    bar->addWidget(gen);
    bar->addWidget(copy);
    bar->addStretch(1);
    lay->addLayout(bar);

    lay->addWidget(new QLabel(QString::fromUtf8("结果")));
    m_buildOut = new QPlainTextEdit;
    m_buildOut->setReadOnly(true);
    lay->addWidget(m_buildOut);

    connect(gen, &QPushButton::clicked, this, [this]() {
        bool ok = true;
        const QByteArray cmdB = Proto::parseHexInput(m_buildCmd->text(), &ok);
        if (!ok || cmdB.isEmpty()) {
            m_buildOut->setPlainText(QString::fromUtf8("命令字无效。"));
            return;
        }
        bool ok2 = true;
        const QByteArray data = Proto::parseHexInput(m_buildData->toPlainText(), &ok2);
        if (!ok2) {
            m_buildOut->setPlainText(QString::fromUtf8("载荷含非十六进制字符。"));
            return;
        }
        const QByteArray f = Proto::buildFrame((quint8)cmdB[0], data);
        m_buildOut->setPlainText(QString::fromUtf8("帧（%1 字节）：\n%2")
                                 .arg(f.size()).arg(Proto::hexSpaced(f)));
    });
    connect(copy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_buildOut->toPlainText());
    });
    return w;
}

QWidget *WorkbenchWindow::buildParseTab()
{
    QWidget *w = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(w);

    lay->addWidget(new QLabel(QString::fromUtf8(
        "粘贴十六进制字节流（可带空格）。按帧头配对，逐帧校验 CRC 与帧尾；"
        "不合格会重找帧头（重同步）。")));
    m_parseIn = new QPlainTextEdit;
    m_parseIn->setPlaceholderText(QString::fromUtf8("AA 55 01 0C ... 55 AA"));
    m_parseIn->setFixedHeight(110);
    lay->addWidget(m_parseIn);

    QHBoxLayout *bar = new QHBoxLayout;
    QPushButton *parse  = new QPushButton(QString::fromUtf8("解析"));
    QPushButton *sEnv   = new QPushButton(QString::fromUtf8("填入示例环境帧"));
    QPushButton *sHist  = new QPushButton(QString::fromUtf8("填入示例历史帧"));
    bar->addWidget(parse);
    bar->addWidget(sEnv);
    bar->addWidget(sHist);
    bar->addStretch(1);
    lay->addLayout(bar);

    lay->addWidget(new QLabel(QString::fromUtf8("解析结果")));
    m_parseOut = new QPlainTextEdit;
    m_parseOut->setReadOnly(true);
    lay->addWidget(m_parseOut);

    connect(sEnv, &QPushButton::clicked, this, [this]() {
        m_parseIn->setPlainText(Proto::hexSpaced(sampleEnvFrame()));
    });
    connect(sHist, &QPushButton::clicked, this, [this]() {
        QByteArray d;
        const quint32 ts = 0x0001E240;                 /* 123456 */
        d.append((char)((ts >> 24) & 0xFF));
        d.append((char)((ts >> 16) & 0xFF));
        d.append((char)((ts >> 8) & 0xFF));
        d.append((char)(ts & 0xFF));
        d += sampleEnvFrame();
        m_parseIn->setPlainText(Proto::hexSpaced(Proto::buildFrame(0x15, d)));
    });
    connect(parse, &QPushButton::clicked, this, [this]() {
        bool ok = true;
        const QByteArray s = Proto::parseHexInput(m_parseIn->toPlainText(), &ok);
        if (!ok) {
            m_parseOut->setPlainText(QString::fromUtf8("输入含非十六进制字符。"));
            return;
        }
        if (s.isEmpty()) {
            m_parseOut->setPlainText(QString::fromUtf8("输入为空。"));
            return;
        }
        QStringList notes;
        const QVector<Proto::Frame> frames = Proto::parseStream(s, notes);
        QStringList out;
        out << QString::fromUtf8("共 %1 字节，解出 %2 帧。").arg(s.size()).arg(frames.size());
        for (const Proto::Frame &f : frames) {
            out << QString();
            out << Proto::describeFrame(f);
        }
        for (const QString &n : notes)
            out << QString::fromUtf8("注：") + n;
        m_parseOut->setPlainText(out.join('\n'));
    });
    return w;
}

QWidget *WorkbenchWindow::buildInjectTab()
{
    QWidget *w = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(w);

    lay->addWidget(new QLabel(QString::fromUtf8(
        "粘贴一帧正常帧，生成一组坏帧变体，用于测试上位机与固件的解析容错。")));
    m_injectIn = new QPlainTextEdit;
    m_injectIn->setPlaceholderText(QString::fromUtf8("一帧的十六进制"));
    m_injectIn->setFixedHeight(90);
    lay->addWidget(m_injectIn);

    QHBoxLayout *bar = new QHBoxLayout;
    QPushButton *useSample = new QPushButton(QString::fromUtf8("填入示例环境帧"));
    QPushButton *gen       = new QPushButton(QString::fromUtf8("生成变体"));
    bar->addWidget(useSample);
    bar->addWidget(gen);
    bar->addStretch(1);
    lay->addLayout(bar);

    lay->addWidget(new QLabel(QString::fromUtf8("变体（每条含判定）")));
    m_injectOut = new QPlainTextEdit;
    m_injectOut->setReadOnly(true);
    lay->addWidget(m_injectOut);

    connect(useSample, &QPushButton::clicked, this, [this]() {
        m_injectIn->setPlainText(Proto::hexSpaced(sampleEnvFrame()));
    });
    connect(gen, &QPushButton::clicked, this, [this]() {
        bool ok = true;
        const QByteArray f = Proto::parseHexInput(m_injectIn->toPlainText(), &ok);
        if (!ok || f.size() < 8) {
            m_injectOut->setPlainText(QString::fromUtf8("输入不是一条完整帧（至少 8 字节）。"));
            return;
        }
        QStringList out;
        for (const Proto::Injection &inj : Proto::injections(f)) {
            QStringList notes;
            const QVector<Proto::Frame> r = Proto::parseStream(inj.bytes, notes);
            QString verdict;
            if (r.isEmpty())
                verdict = QString::fromUtf8("解析器未成帧（被丢弃）");
            else
                verdict = r.first().ok ? QString::fromUtf8("解析器判为通过")
                                       : QString::fromUtf8("解析器判为未通过");
            out << QString::fromUtf8("【%1】").arg(inj.desc);
            out << Proto::hexSpaced(inj.bytes);
            out << QString::fromUtf8("  → %1").arg(verdict);
            out << QString();
        }
        m_injectOut->setPlainText(out.join('\n'));
    });
    return w;
}

QWidget *WorkbenchWindow::buildTestTab()
{
    QWidget *w = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(w);

    lay->addWidget(new QLabel(QString::fromUtf8(
        "内置一致性自检：CRC 标准向量、文档里的两个协议向量、组帧与解帧往返、"
        "含噪声的多帧流、坏帧注入的拒绝行为。")));
    QPushButton *run = new QPushButton(QString::fromUtf8("运行自检"));
    lay->addWidget(run);

    m_testOut = new QPlainTextEdit;
    m_testOut->setReadOnly(true);
    lay->addWidget(m_testOut);

    connect(run, &QPushButton::clicked, this, [this]() {
        QStringList log;
        const int fail = Proto::selftest(log);
        QStringList out;
        out << (fail == 0 ? QString::fromUtf8("协议工作台自检：PASS")
                          : QString::fromUtf8("协议工作台自检：FAIL"));
        out << QString();
        out << log;
        out << QString();
        out << QString::fromUtf8("失败项 %1").arg(fail);
        m_testOut->setPlainText(out.join('\n'));
    });
    return w;
}
