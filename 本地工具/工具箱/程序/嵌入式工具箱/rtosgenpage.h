#ifndef RTOSGENPAGE_H
#define RTOSGENPAGE_H

/* RTOS 骨架生成页：读 FreeRTOSConfig.h，按任务/资源清单生成 main.c 骨架。 */

#include <QWidget>

#include <functional>

#include "rtosgen.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QShowEvent;
class QTableWidget;

class RtosGenPage : public QWidget {
    Q_OBJECT
public:
    explicit RtosGenPage(QWidget *parent = nullptr);

    void setEnvMonitorRoot(const QString &root);   /* 用于"用板1/板2配置" */

    /* 提供起手助手某字段的取值，供「从起手助手导入」按钮使用。 */
    void setKickstartProvider(std::function<QString(const QString &)> provider);

private slots:
    void browseConfig();
    void parseConfig();
    void useBoard1();
    void useBoard2();
    void addTask();
    void delTask();
    void fillSample();
    void addRes();
    void delRes();
    void applyScenario(int idx);   /* 用场景模板替换任务与资源清单 */
    void importFromKickstart();    /* 从起手助手导入任务行 */
    void doValidate();
    void doGenerate();
    void doCopy();
    void doSave();

private:
    void addTaskRow(const RtosGen::TaskSpec &t);
    void addResRow(const RtosGen::ResSpec &r);
    void chainResources();      /* 按任务顺序自动建队列并双向关联 */
    QVector<RtosGen::TaskSpec> readTasks() const;
    QVector<RtosGen::ResSpec>  readResources() const;
    RtosGen::GenOptions        readOptions() const;
    bool    writeGenerated(const QString &path, const QString &text) const;
    void    showConfig();
    void    ensureFlowList();   /* 懒装载「STM32 速查」的流程卡清单（4MB，别在启动时读） */

protected:
    void showEvent(QShowEvent *e) override;   /* 首次显示本页时才去读速查数据 */

private:
    QString m_envRoot;
    QString m_cfgPath;

    QLineEdit      *m_cfgEdit  = nullptr;
    QLabel         *m_cfgInfo  = nullptr;
    QTableWidget   *m_taskTable = nullptr;
    QTableWidget   *m_resTable  = nullptr;
    QComboBox      *m_scenario  = nullptr;
    QLineEdit      *m_funcEdit  = nullptr;
    QCheckBox      *m_until     = nullptr;
    QCheckBox      *m_wdg       = nullptr;
    QComboBox      *m_enc       = nullptr;
    QLineEdit      *m_baseEdit  = nullptr;
    QCheckBox      *m_split     = nullptr;
    QCheckBox      *m_crlf      = nullptr;
    QPlainTextEdit *m_includes  = nullptr;
    QPlainTextEdit *m_config    = nullptr;
    QPlainTextEdit *m_issues    = nullptr;
    QPlainTextEdit *m_preview   = nullptr;
    QListWidget    *m_flowList  = nullptr;   /* 可勾选的流程卡清单 → flowPicks */
    QLabel         *m_flowNote  = nullptr;
    bool m_flowLoaded = false;

    RtosGen::FwConfig m_cfg;
    std::function<QString(const QString &)> m_kickProvider;
};

#endif // RTOSGENPAGE_H
