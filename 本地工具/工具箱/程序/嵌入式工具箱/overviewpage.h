#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

/* 工程体检总览页：把进度 / 引脚 / RTOS / 编码四份报告的关键结论聚到一屏。
   只读各工具生成的报告文件，不改任何东西；点「刷新」重新读取。 */

#include <QWidget>

class QLabel;
class QPushButton;
class QTextBrowser;

class OverviewPage : public QWidget {
    Q_OBJECT
public:
    explicit OverviewPage(const QString &toolsDir, QWidget *parent = nullptr);

private slots:
    void refresh();
    void openReportsDir();

private:
    QString m_toolsDir;
    QTextBrowser *m_view   = nullptr;
    QLabel       *m_status = nullptr;
};

#endif // OVERVIEWPAGE_H
