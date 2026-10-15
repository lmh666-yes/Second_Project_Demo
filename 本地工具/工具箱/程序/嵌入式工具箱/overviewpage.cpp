#include "overviewpage.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
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
    {"固件预算", "检查/固件预算/固件预算报告.md"},
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
        "把各工具的报告关键结论聚到一屏。报告由「检查」组的工具生成；"
        "内容不是最新就先跑一次「全量检查」。"), this);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color:rgba(127,127,127,255);"));
    lay->addWidget(desc);

    QHBoxLayout *bar = new QHBoxLayout;
    QPushButton *bRefresh = new QPushButton(QStringLiteral("刷新"), this);
    QPushButton *bOpen    = new QPushButton(QStringLiteral("打开报告目录"), this);
    m_status = new QLabel(QStringLiteral("尚未读取。"), this);
    m_status->setStyleSheet(QStringLiteral("color:rgba(127,127,127,255);"));
    bar->addWidget(bRefresh);
    bar->addWidget(bOpen);
    bar->addSpacing(12);
    bar->addWidget(m_status);
    bar->addStretch(1);
    lay->addLayout(bar);

    m_view = new QTextBrowser(this);
    m_view->setOpenExternalLinks(false);
    lay->addWidget(m_view, 1);

    connect(bRefresh, &QPushButton::clicked, this, &OverviewPage::refresh);
    connect(bOpen, &QPushButton::clicked, this, &OverviewPage::openReportsDir);

    refresh();
}

void OverviewPage::refresh()
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
    const int total = int(sizeof(REPORTS) / sizeof(REPORTS[0]));
    m_status->setText(found == 0
                          ? QStringLiteral("%1 份报告都还没生成。").arg(total)
                          : QStringLiteral("已读取 %1 / %2 份报告。").arg(found).arg(total));
}

void OverviewPage::openReportsDir()
{
    const QString dir = QDir(m_toolsDir).filePath(QStringLiteral("检查"));
    if (QFileInfo::exists(dir))
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}
