#ifndef UPLOADER_H
#define UPLOADER_H

/* 上传逻辑层：对「Python 后端 git_backend.py」的一层 C++ 封装。
 *
 * 为什么要走 Python 而不是直接在 Qt 里起进程：
 *   本机环境下 Qt 的 QProcess 无法创建子进程（实测 started=0，给绝对路径也一样），
 *   而 Python 的 subprocess 正常。工具箱既有工具本来就是这个分工
 *   （Qt 界面 + Python 干活），这里保持一致，换机器也不受影响。
 *
 * 约定：后端每个子命令输出一行 JSON；本类负责调用、解析、发日志信号。 */

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

struct GitResult {
    bool        ok   = false;
    int         code = -1;
    QString     out;              /* 后端给的原始输出 */
    QString     error;            /* 后端给的可读原因（失败时） */
    QJsonObject data;             /* 结构化数据（status / staged / bigfiles 等） */

    GitResult() = default;
    QString tail(int n = 12) const;
};

/* 仓库当前状态快照，供「仓库状态」页展示。 */
struct RepoState {
    bool        isRepo     = false;
    bool        hasRemote  = false;
    QString     toplevel;
    QString     branch;
    QString     remote;
    int         staged     = 0;
    int         unstaged   = 0;
    int         untracked  = 0;
    int         ahead      = 0;
    int         behind     = 0;
    QStringList recent;
    QString     userName;
    QString     userEmail;
};

/* .gitignore 助手：一条建议规则。 */
struct IgnoreRule {
    QString pattern;
    QString why;
    int     hits = 0;
};

class Uploader : public QObject
{
    Q_OBJECT
public:
    explicit Uploader(QObject *parent = nullptr);

    /* 程序根目录（含 src/git_backend.py 的那一层）；找不到返回空。
       本工具独立运行，配置与后端脚本都跟着程序目录走。 */
    static QString toolRoot();
    /* 后端脚本路径（程序根/src/git_backend.py）。 */
    static QString backendPath();
    /* Python 解释器：依次探 python / py -3 / python3（与工具箱其他工具一致）。 */
    static QStringList pythonCmd();
    static bool    pythonAvailable();
    static bool    gitAvailable(QString *version = nullptr);

    /* ---- 调用后端 ---- */
    GitResult call(const QStringList &args, int timeoutMs = 180000);

    /* ---- 仓库 ---- */
    RepoState inspect(const QString &folder);
    bool      isRepo(const QString &folder);
    QString   currentBranch(const QString &folder);
    QString   remoteUrl(const QString &folder);

    /* ---- 写操作 ---- */
    GitResult initRepo(const QString &folder, const QString &branch);
    GitResult setRemote(const QString &folder, const QString &url);
    GitResult setIdentity(const QString &folder, const QString &name,
                          const QString &email, bool global);
    GitResult addAll(const QString &folder);
    GitResult commit(const QString &folder, const QString &message);
    GitResult push(const QString &folder, const QString &branch);
    GitResult pull(const QString &folder, const QString &branch);

    /* ---- 变更预览 ---- */
    struct ChangeStat { int added = 0; int modified = 0; int deleted = 0; };
    struct StagedInfo { QStringList files; ChangeStat stat; };
    StagedInfo staged(const QString &folder);

    /* ---- 大文件检查 ---- */
    struct BigFile { QString path; qint64 bytes = 0; bool hard = false; };
    QList<BigFile> bigFiles(const QString &folder, const QStringList &files,
                            int warnMB = 50, int stopMB = 100);

    /* ---- .gitignore 助手 ---- */
    static QList<IgnoreRule> defaultIgnoreRules();
    QList<IgnoreRule> scanIgnoreCandidates(const QString &folder) const;
    QString  readGitignore(const QString &folder) const;
    static QStringList mergeIgnore(const QString &existing, const QStringList &rules);
    /* 返回 (写了几条, 结果文本) */
    GitResult writeGitignore(const QString &folder, const QStringList &rules);

    static QString explainFailure(const QString &out);

    static int selftest(QStringList &log);

signals:
    void logLine(const QString &text);
};

#endif // UPLOADER_H
