#ifndef SCAFFOLDPAGE_H
#define SCAFFOLDPAGE_H

/* 工程模板页：一键生成 STM32 工程骨架。
   调用 project_scaffold.py，支持预设选择和自定义外设。 */

#include <QWidget>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;

class ScaffoldPage : public QWidget {
    Q_OBJECT
public:
    explicit ScaffoldPage(const QString &toolsDir, QWidget *parent = nullptr);

    void warnMissingToolsDir();

private slots:
    void onGenerate();
    void onBrowse();
    void onPresetChanged(int idx);

private:
    QString m_toolsDir;
    QLabel         *m_alert  = nullptr;
    QComboBox      *m_preset = nullptr;
    QComboBox      *m_style  = nullptr;
    QLineEdit      *m_outDir = nullptr;
    QPushButton    *m_btnBrowse = nullptr;
    QPushButton    *m_btnGen = nullptr;
    QPlainTextEdit *m_log    = nullptr;
};

#endif // SCAFFOLDPAGE_H
