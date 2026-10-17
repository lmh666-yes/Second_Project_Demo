#ifndef KICKSTARTWINDOW_H
#define KICKSTARTWINDOW_H

#include <QHash>
#include <QMainWindow>
#include <QVector>

#include "docbuilder.h"

class QListWidget;
class QPushButton;
class QStackedWidget;
class QTextBrowser;

class KickStartWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit KickStartWindow(QWidget *parent = nullptr);

    /* 取某一步某字段的填写内容（供跨工具导入，如 RTOS 骨架生成读"任务与中断划分"）。 */
    QString answer(const QString &id) const;

signals:
    /* C2：请求用起手助手的答案生成工程骨架。periph/tasks 已是生成器参数格式，
       skippedTasks 是任务表里解析不出「优先级/栈」而被跳过的原文（界面要提示）。 */
    void scaffoldRequested(const QString &periph, const QString &tasks,
                           const QStringList &skippedTasks);

private slots:
    void onStepChanged(int row);
    void onPrev();
    void onNext();
    void onExport();
    void onCopy();
    void onSave();
    void onLoad();

private:
    QWidget *buildPage(const PageSpec &page);
    void refreshPreview();
    QString docText() const;
    void setEditorText(const QString &id, const QString &text);

    QVector<PageSpec>          m_specs;
    QHash<QString, QString>    m_answers;
    QHash<QString, QWidget *>  m_editors;

    QListWidget    *m_steps   = nullptr;
    QStackedWidget *m_pages   = nullptr;
    QTextBrowser   *m_preview = nullptr;
    QPushButton    *m_btnPrev = nullptr;
    QPushButton    *m_btnNext = nullptr;
};

#endif // KICKSTARTWINDOW_H
