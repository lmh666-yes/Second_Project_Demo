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

    /* C2：起手助手「一键生成工程」用 —— 把答案转出来的外设/任务参数填进自定义两个框
       （填了就盖过预设），顺带设好输出目录。 */
    void prefillCustom(const QString &periphCsv, const QString &tasksCsv, const QString &outDir);
    /* C2：用当前参数立刻生成一次（由「一键生成工程」调）。 */
    void generateNow();

private slots:
    void onGenerate();
    void onBrowse();
    void onPresetChanged(int idx);

private:
    QString m_toolsDir;
    QLabel         *m_alert  = nullptr;
    QComboBox      *m_preset = nullptr;
    QComboBox      *m_style  = nullptr;
    QLineEdit      *m_periph = nullptr;   /* 自定义外设（逗号分隔；填了就盖过预设） */
    QLineEdit      *m_tasks  = nullptr;   /* 自定义任务（Name:prio:stack,...） */
    QLineEdit      *m_outDir = nullptr;
    QPushButton    *m_btnBrowse = nullptr;
    QPushButton    *m_btnGen = nullptr;
    QPlainTextEdit *m_log    = nullptr;
};

#endif // SCAFFOLDPAGE_H
