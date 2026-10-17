#ifndef TOOLRUNPAGE_H
#define TOOLRUNPAGE_H

/* 运行器页：把命令行工具（.py）包成界面页。
   支持多步顺序执行；步骤参数在运行时取，可绑定到本页的选项控件。

   稳定性要点：
   - python 解释器在首次运行时逐个探测（python / py -3 / python3），并记住可用者；
   - 进程输出按 UTF-8 解码，失败时回退本地编码，避免中文乱码；
   - 报告可用性在每次运行结束后刷新，不是只在启动时判断一次；
   - 工具箱目录找不到时在页面顶部显式提示，不静默降级。 */

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QHBoxLayout;
class QProcess;
class QTimer;

struct RunStep {
    QString                       label;   /* 步骤名，打印在输出里 */
    QString                       script;  /* .py 绝对路径 */
    std::function<QStringList()>  args;    /* 运行时取附加参数，可为空 */
};

class ToolRunPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolRunPage(const QString &title, const QString &desc, QWidget *parent = nullptr);

    void setSteps(const QVector<RunStep> &steps);

    /* A3：整批跑完（并行模式收尾时）的钩子 —— 回调返回的文本会追加到输出末尾。
       用途：「全量检查」跑完让总览页刷新并归档一份体检快照，并在末尾提示去哪看健康度。 */
    void setFinishedHook(const std::function<QString()> &hook) { m_finishedHook = hook; }

    /* 并行模式：多步同时跑（「全量检查」用），输出按 `[步骤名]` 前缀区分。
       默认串行。并行时总耗时 ≈ 最慢的那一步，而不是各步相加。 */
    void setParallel(bool on) { m_parallel = on; }
    /* 报告产物的路径。有的是 .md 文件（进度/引脚/RTOS/编码），有的只有产物目录
       （链路仿真只出 hex/log，没有 md），所以允许每页改按钮文字。 */
    void setReportPath(const QString &path);
    void setReportLabel(const QString &text);      /* 供"打开报告"使用 */
    QHBoxLayout *optionsLayout();                 /* 外部往这一行加选项控件 */

    /* 工具箱目录未解析出来时由外部调用，页面上给出显式告警 */
    void warnMissingToolsDir();

    /* 加一个自定义动作按钮；回调返回要显示在状态栏的文本。
       用于跨工具联动（如「发送到协议工作台」）。 */
    void addAction(const QString &text, std::function<QString()> onRun);

    /* 取"最后一步运行的工作目录"，联动时用来找产物。 */
    QString lastWorkingDir() const { return m_lastWorkDir; }

private slots:
    void run();
    void stop();
    void openReport();

private:
    void startStep(int index);
    void startAllParallel();    /* 并行模式：一次把全部步骤拉起来 */
    void checkParallelDone();   /* 并行模式：全部回来才收尾（含启动失败的情形） */
    void onFinished(int exitCode, int status);
    void append(const QString &text);
    void refreshReportButton();
    QString decode(const QByteArray &raw) const;
    void copyAll();
    void saveAs();
    void printSummary();        /* 多步时打印逐项状态汇总表 */
    void renderParallelSummary(); /* 并行模式：把交错输出按步骤归位重排 */

    QString              m_report;
    QVector<RunStep>     m_steps;
    QStringList          m_python;      /* 探测到的解释器：程序名 + 前置参数 */
    QString              m_lastWorkDir; /* 最后一步运行的工作目录 */
    int                  m_index = 0;
    bool                 m_running = false;
    bool                 m_parallel = false;                 /* 并行模式（全量检查） */
    QVector<QProcess *>  m_parallelProcs;                    /* 并行模式下的各步进程 */
    QVector<int>         m_stepCodes;   /* 每步退出码，用于末尾汇总 */
    QVector<QString>     m_paraOut;     /* 并行模式：每步输出原文，收尾时按步骤归位重排 */
    std::function<QString()> m_finishedHook;   /* A3：整批跑完的钩子（见 setFinishedHook） */

    QLabel         *m_alert   = nullptr;
    QLabel         *m_desc    = nullptr;
    QLabel         *m_status  = nullptr;
    QLabel         *m_find    = nullptr;
    QHBoxLayout    *m_options = nullptr;
    QHBoxLayout    *m_actionsLayout = nullptr;   /* 自定义动作按钮，如「发送到协议工作台」 */
    QPlainTextEdit *m_out     = nullptr;
    QPushButton    *m_btnRun  = nullptr;
    QPushButton    *m_btnStop = nullptr;
    QPushButton    *m_btnReport = nullptr;
    QPushButton    *m_btnClear  = nullptr;
    QPushButton    *m_btnCopy   = nullptr;
    QPushButton    *m_btnSave   = nullptr;
    QLineEdit      *m_search  = nullptr;
    QCheckBox      *m_follow  = nullptr;
    QProcess       *m_proc    = nullptr;
    QTimer         *m_watchdog = nullptr;   /* 单步运行超时保护 */
};

#endif // TOOLRUNPAGE_H
