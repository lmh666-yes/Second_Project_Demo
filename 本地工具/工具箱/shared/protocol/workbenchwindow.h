#ifndef WORKBENCHWINDOW_H
#define WORKBENCHWINDOW_H

#include <QMainWindow>

class QLineEdit;
class QPlainTextEdit;
class QTabWidget;

class WorkbenchWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit WorkbenchWindow(QWidget *parent = nullptr);

    /* 外部联动入口：把一段十六进制字节流填进解帧页并切过去。
       用于链路仿真等工具"发送产物到协议工作台"。 */
    void loadIntoParser(const QString &hex);

private:
    QWidget *buildBuildTab();
    QWidget *buildParseTab();
    QWidget *buildInjectTab();
    QWidget *buildTestTab();

    QTabWidget     *m_tabs       = nullptr;
    QLineEdit      *m_buildCmd  = nullptr;
    QPlainTextEdit *m_buildData = nullptr;
    QPlainTextEdit *m_buildOut  = nullptr;

    QPlainTextEdit *m_parseIn   = nullptr;
    QPlainTextEdit *m_parseOut  = nullptr;

    QPlainTextEdit *m_injectIn  = nullptr;
    QPlainTextEdit *m_injectOut = nullptr;

    QPlainTextEdit *m_testOut   = nullptr;
};

#endif // WORKBENCHWINDOW_H
