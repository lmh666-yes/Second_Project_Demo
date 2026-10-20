#ifndef BUILDFLASHPAGE_H
#define BUILDFLASHPAGE_H

/* 编译烧录页（A2/A3/A5）：把 build_flash.py 包成界面。
   一条命令走完 编译 → 烧录 → 板载自检；另有体积趋势与状态快照。
   探测到的串口直接填进下拉框（不再靠记忆敲 COM 号）。 */

#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class BuildFlashPage : public QWidget {
    Q_OBJECT
public:
    explicit BuildFlashPage(const QString &toolsDir, QWidget *parent = nullptr);
    void warnMissingToolsDir();

private slots:
    void onDetect();
    void onBuild();
    void onFlash();
    void onSerial();
    void onAll();
    void onHistory();
    void onSnapshot();
    void onGenSelftest();

private:
    void runScript(const QStringList &extra, const QString &title);
    QString scriptPath() const;

    QString         m_toolsDir;
    QLabel         *m_alert = nullptr;
    QComboBox      *m_board = nullptr;
    QComboBox      *m_com   = nullptr;
    QPushButton    *m_btnDetect = nullptr;
    QPlainTextEdit *m_log   = nullptr;
    QVector<QPushButton *> m_actions;
};

#endif // BUILDFLASHPAGE_H
