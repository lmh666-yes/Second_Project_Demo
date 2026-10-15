#include "uploader.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

/* ------------------------------------------------------------------ */
/*  GitResult                                                          */
/* ------------------------------------------------------------------ */

QString GitResult::tail(int n) const
{
    if (out.isEmpty())
        return error;
    const QStringList ls = out.split(QRegularExpression(QStringLiteral("\r?\n")),
                                     Qt::SkipEmptyParts);
    const int from = qMax(0, ls.size() - n);
    QString s = ls.mid(from).join(QLatin1Char('\n')).trimmed();
    if (!error.isEmpty() && !s.contains(error))
        s = error + QStringLiteral("\n") + s;
    return s;
}

/* ------------------------------------------------------------------ */
/*  路径与解释器                                                        */
/* ------------------------------------------------------------------ */

QString Uploader::toolRoot()
{
    /* 程序根：从 exe 位置向上找含 src/git_backend.py 的那一层。
       本工具独立于工具箱，自带 src/，不再依赖工具箱目录结构。 */
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo::exists(d.filePath(QStringLiteral("src/git_backend.py"))))
            return d.absolutePath();
        if (!d.cdUp())
            break;
    }
    return QString();
}

QString Uploader::backendPath()
{
    const QString root = toolRoot();
    if (!root.isEmpty())
        return QDir(root).filePath(QStringLiteral("src/git_backend.py"));
    /* 兜底：开发期直接在源码目录里跑 */
    const QString alt = QDir(QCoreApplication::applicationDirPath())
                            .filePath(QStringLiteral("../../../src/git_backend.py"));
    return QFileInfo::exists(alt) ? QDir::cleanPath(alt) : QString();
}

QStringList Uploader::pythonCmd()
{
    /* 探测结果缓存在函数级 static 里：本函数在 call() 内部会被连着调用好几次，
       若每次都重新起进程试 3 个解释器，单次后端调用就会白起一堆进程。 */
    static bool        probed = false;
    static QString     prog;
    static QStringList pre;
    if (!probed) {
        const struct { const char *prog; QStringList pre; } cands[] = {
            { "python",  {} },
            { "py",      { QStringLiteral("-3") } },
            { "python3", {} },
        };
        for (const auto &c : cands) {
            QProcess probe;
            QStringList a = c.pre;
            a << QStringLiteral("-c") << QStringLiteral("print(1)");
            probe.start(QString::fromLatin1(c.prog), a);
            if (probe.waitForStarted(3000) && probe.waitForFinished(4000)
                && probe.exitCode() == 0) {
                prog = QString::fromLatin1(c.prog);
                pre  = c.pre;
                break;
            }
        }
        probed = true;
    }
    QStringList r;
    if (!prog.isEmpty()) {
        r << prog;
        r += pre;
    }
    return r;
}

bool Uploader::pythonAvailable()
{
    return !pythonCmd().isEmpty();
}

bool Uploader::gitAvailable(QString *version)
{
    /* git 是否可用由后端自己判断（后端会去找 git 并报版本） */
    Uploader u;
    const GitResult r = u.call(QStringList{QStringLiteral("gitver")}, 20000);
    if (!r.ok)
        return false;
    if (version)
        *version = r.out.trimmed();
    return true;
}

Uploader::Uploader(QObject *parent)
    : QObject(parent)
{
}

/* ------------------------------------------------------------------ */
/*  调用后端                                                            */
/* ------------------------------------------------------------------ */

GitResult Uploader::call(const QStringList &args, int timeoutMs)
{
    GitResult r;

    const QString py = pythonCmd().isEmpty() ? QString() : pythonCmd().first();
    if (py.isEmpty()) {
        r.code = -1;
        r.error = QStringLiteral("未找到 Python。请确认已安装 Python 并加入 PATH。");
        emit logLine(r.error);
        return r;
    }
    QStringList pyPre = pythonCmd();
    pyPre.removeFirst();                       /* 去掉程序名，剩下前置参数 */

    const QString backend = backendPath();
    if (backend.isEmpty()) {
        r.code = -1;
        r.error = QStringLiteral("找不到后端脚本 git_backend.py（应在程序目录 src/ 下）。");
        emit logLine(r.error);
        return r;
    }

    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    p.setProcessEnvironment(env);
    QStringList full = QStringList{py};
    full += pyPre;
    full << backend;
    full += args;
    p.start(full.first(), full.mid(1));
    if (!p.waitForStarted(8000)) {
        r.code = -1;
        r.error = QStringLiteral("无法启动 Python 后端（%1）。").arg(py);
        emit logLine(r.error);
        return r;
    }
    if (!p.waitForFinished(timeoutMs)) {
#ifdef Q_OS_WIN
        /* 只 kill python 会把它拉起的 git 孙进程留下来（可能占着 index.lock），
           taskkill /T 连带整棵进程树一起收掉。 */
        const qint64 pid = p.processId();
        if (pid > 0)
            QProcess::execute(QStringLiteral("taskkill"),
                              QStringList{QStringLiteral("/PID"), QString::number(pid),
                                          QStringLiteral("/T"), QStringLiteral("/F")});
#endif
        p.kill();
        p.waitForFinished(2000);
        r.code = -2;
        r.error = QStringLiteral("后端命令超时（%1 ms）：%2").arg(timeoutMs).arg(args.join(' '));
        emit logLine(r.error);
        return r;
    }

    const QByteArray so = p.readAllStandardOutput();
    const QByteArray se = p.readAllStandardError();
    QString stdoutText = QString::fromUtf8(so).trimmed();
    if (stdoutText.isEmpty()) {
        r.code = p.exitCode();
        r.error = QString::fromUtf8(se).trimmed();
        if (r.error.isEmpty())
            r.error = QStringLiteral("后端没有返回内容（退出码 %1）。").arg(p.exitCode());
        emit logLine(r.error);
        return r;
    }

    /* 解析最后一行 JSON（后端只输出一行） */
    QString jsonLine = stdoutText;
    const int nl = jsonLine.lastIndexOf(QLatin1Char('\n'));
    if (nl >= 0)
        jsonLine = jsonLine.mid(nl + 1).trimmed();

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonLine.toUtf8(), &perr);
    if (doc.isNull() || !doc.isObject()) {
        r.code = -3;
        r.error = QStringLiteral("后端返回无法解析：%1").arg(jsonLine.left(200));
        emit logLine(r.error);
        return r;
    }
    const QJsonObject o = doc.object();
    r.ok    = o.value(QStringLiteral("ok")).toBool(false);
    r.code  = o.value(QStringLiteral("code")).toInt(-1);
    r.out   = o.value(QStringLiteral("out")).toString();
    r.error = o.value(QStringLiteral("error")).toString();
    r.data  = o.value(QStringLiteral("data")).toObject();

    /* 回显输出到日志 */
    if (!r.out.isEmpty()) {
        const QStringList ls = r.out.split(QRegularExpression(QStringLiteral("\r?\n")),
                                           Qt::SkipEmptyParts);
        for (const QString &l : ls)
            emit logLine(l);
    } else if (!r.error.isEmpty()) {
        emit logLine(r.error);
    }
    return r;
}

/* ------------------------------------------------------------------ */
/*  仓库                                                                */
/* ------------------------------------------------------------------ */

RepoState Uploader::inspect(const QString &folder)
{
    RepoState st;
    const GitResult r = call(QStringList{QStringLiteral("status"), folder});
    const QJsonObject s = r.data.value(QStringLiteral("state")).toObject();
    if (s.isEmpty() || !s.value(QStringLiteral("isRepo")).toBool())
        return st;
    st.isRepo    = true;
    st.toplevel  = s.value(QStringLiteral("toplevel")).toString();
    st.branch    = s.value(QStringLiteral("branch")).toString();
    st.remote    = s.value(QStringLiteral("remote")).toString();
    st.hasRemote = s.value(QStringLiteral("hasRemote")).toBool();
    st.staged    = s.value(QStringLiteral("staged")).toInt();
    st.unstaged  = s.value(QStringLiteral("unstaged")).toInt();
    st.untracked = s.value(QStringLiteral("untracked")).toInt();
    st.ahead     = s.value(QStringLiteral("ahead")).toInt();
    st.behind    = s.value(QStringLiteral("behind")).toInt();
    st.userName  = s.value(QStringLiteral("userName")).toString();
    st.userEmail = s.value(QStringLiteral("userEmail")).toString();
    for (const QJsonValue &v : s.value(QStringLiteral("recent")).toArray())
        st.recent << v.toString();
    return st;
}

bool Uploader::isRepo(const QString &folder)
{
    const GitResult r = call(QStringList{QStringLiteral("is-repo"), folder}, 30000);
    return r.ok && r.data.value(QStringLiteral("state")).toObject()
                       .value(QStringLiteral("isRepo")).toBool();
}

QString Uploader::remoteUrl(const QString &folder)
{
    const GitResult r = call(QStringList{QStringLiteral("remote"), folder}, 30000);
    return r.ok ? r.out.trimmed() : QString();
}

QString Uploader::currentBranch(const QString &folder)
{
    const RepoState st = inspect(folder);
    return st.isRepo && !st.branch.isEmpty() ? st.branch : QStringLiteral("main");
}

/* ------------------------------------------------------------------ */
/*  写操作                                                              */
/* ------------------------------------------------------------------ */

GitResult Uploader::initRepo(const QString &folder, const QString &branch)
{
    return call(QStringList{QStringLiteral("init"), folder,
                            branch.trimmed().isEmpty() ? QStringLiteral("main") : branch.trimmed()},
                60000);
}

GitResult Uploader::setRemote(const QString &folder, const QString &url)
{
    if (url.trimmed().isEmpty()) {
        GitResult r;
        r.code = 1;
        r.error = QStringLiteral("仓库地址为空。");
        return r;
    }
    return call(QStringList{QStringLiteral("remote"), folder, url.trimmed()}, 60000);
}

GitResult Uploader::setIdentity(const QString &folder, const QString &name,
                                const QString &email, bool global)
{
    QStringList a{QStringLiteral("identity"), folder, name, email};
    if (global)
        a << QStringLiteral("--global");
    return call(a, 60000);
}

GitResult Uploader::addAll(const QString &folder)
{
    return call(QStringList{QStringLiteral("add"), folder}, 120000);
}

GitResult Uploader::commit(const QString &folder, const QString &message)
{
    return call(QStringList{QStringLiteral("commit"), folder, message}, 120000);
}

GitResult Uploader::push(const QString &folder, const QString &branch)
{
    return call(QStringList{QStringLiteral("push"), folder,
                            branch.trimmed().isEmpty() ? QStringLiteral("main") : branch.trimmed()},
                360000);
}

GitResult Uploader::pull(const QString &folder, const QString &branch)
{
    return call(QStringList{QStringLiteral("pull"), folder, branch}, 360000);
}

/* ------------------------------------------------------------------ */
/*  变更预览 / 大文件                                                   */
/* ------------------------------------------------------------------ */

Uploader::StagedInfo Uploader::staged(const QString &folder)
{
    StagedInfo info;
    const GitResult r = call(QStringList{QStringLiteral("staged"), folder}, 60000);
    const QJsonObject s = r.data.value(QStringLiteral("staged")).toObject();
    for (const QJsonValue &v : s.value(QStringLiteral("files")).toArray())
        info.files << v.toString();
    info.stat.added    = s.value(QStringLiteral("added")).toInt();
    info.stat.modified = s.value(QStringLiteral("modified")).toInt();
    info.stat.deleted  = s.value(QStringLiteral("deleted")).toInt();
    return info;
}

QList<Uploader::BigFile> Uploader::bigFiles(const QString &folder, const QStringList &files,
                                            int warnMB, int stopMB)
{
    QList<BigFile> out;
    if (files.isEmpty())
        return out;
    QStringList a{QStringLiteral("bigfiles"), folder};
    /* 阈值由调用方给（默认 50/100MB），经 --warn/--stop 传给后端；
       非正数视为用后端默认值。 */
    if (warnMB > 0)
        a << QStringLiteral("--warn") << QString::number(warnMB);
    if (stopMB > 0)
        a << QStringLiteral("--stop") << QString::number(stopMB);
    a += files;
    const GitResult r = call(a, 60000);
    for (const QJsonValue &v : r.data.value(QStringLiteral("big")).toArray()) {
        const QJsonObject o = v.toObject();
        BigFile b;
        b.path  = o.value(QStringLiteral("path")).toString();
        b.bytes = (qint64)o.value(QStringLiteral("bytes")).toDouble();
        b.hard  = o.value(QStringLiteral("hard")).toBool();
        out << b;
    }
    return out;
}

/* ------------------------------------------------------------------ */
/*  .gitignore 助手（C++ 侧完成，不需要起进程）                          */
/* ------------------------------------------------------------------ */

QList<IgnoreRule> Uploader::defaultIgnoreRules()
{
    QList<IgnoreRule> rs;
    auto add = [&rs](const QString &pat, const QString &why) {
        IgnoreRule r; r.pattern = pat; r.why = why; rs << r;
    };
    add(QStringLiteral("build/"),      QStringLiteral("编译输出目录"));
    add(QStringLiteral("Debug/"),      QStringLiteral("Keil/IDE 调试输出"));
    add(QStringLiteral("Release/"),    QStringLiteral("Keil/IDE 发布输出"));
    add(QStringLiteral("*.o"),         QStringLiteral("目标文件"));
    add(QStringLiteral("*.d"),         QStringLiteral("依赖文件"));
    add(QStringLiteral("*.crf"),       QStringLiteral("Keil 浏览信息"));
    add(QStringLiteral("*.axf"),       QStringLiteral("Keil 可执行文件"));
    add(QStringLiteral("*.hex"),       QStringLiteral("烧写文件"));
    add(QStringLiteral("*.bin"),       QStringLiteral("二进制固件"));
    add(QStringLiteral("*.elf"),       QStringLiteral("ELF 可执行"));
    add(QStringLiteral("*.map"),       QStringLiteral("链接映射表"));
    add(QStringLiteral("*.lst"),       QStringLiteral("列表文件"));
    add(QStringLiteral("*.lnp"),       QStringLiteral("Keil 链接输入"));
    add(QStringLiteral("*.htm"),       QStringLiteral("静态链接报告"));
    add(QStringLiteral("*.dep"),       QStringLiteral("Keil 依赖缓存"));
    add(QStringLiteral("*.uvguix.*"),  QStringLiteral("Keil 个人界面布局"));
    add(QStringLiteral("*.scvd"),      QStringLiteral("调试视图配置"));
    add(QStringLiteral("Objects/"),    QStringLiteral("MDK Objects 目录"));
    add(QStringLiteral("Listings/"),   QStringLiteral("MDK Listings 目录"));
    add(QStringLiteral("*.su"),        QStringLiteral("编译中间文件"));
    add(QStringLiteral("*.idb"),       QStringLiteral("编译中间文件"));
    add(QStringLiteral("*.pdb"),       QStringLiteral("调试符号"));
    add(QStringLiteral("*.gcda"),      QStringLiteral("覆盖率数据"));
    add(QStringLiteral("*.gcno"),      QStringLiteral("覆盖率数据"));
    add(QStringLiteral(".vscode/"),    QStringLiteral("编辑器本地配置"));
    add(QStringLiteral(".idea/"),      QStringLiteral("IDE 本地配置"));
    add(QStringLiteral("Thumbs.db"),   QStringLiteral("Windows 缩略图缓存"));
    add(QStringLiteral(".DS_Store"),   QStringLiteral("macOS 目录缓存"));
    return rs;
}

QList<IgnoreRule> Uploader::scanIgnoreCandidates(const QString &folder) const
{
    QList<IgnoreRule> rules = defaultIgnoreRules();
    QSet<QString> presentExt;
    QSet<QString> presentDir;

    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    int guard = 0;
    while (it.hasNext() && guard < 60000) {
        const QString p = it.next();
        ++guard;
        const QString norm = QDir::fromNativeSeparators(p);
        if (norm.contains(QStringLiteral("/.git/")))
            continue;
        const QFileInfo fi(p);
        presentExt.insert(QLatin1Char('.') + fi.suffix().toLower());
        presentDir.insert(fi.dir().dirName());
    }

    QList<IgnoreRule> out;
    for (IgnoreRule r : rules) {
        int hits = 0;
        const QString pat = r.pattern;
        if (pat.endsWith(QLatin1Char('/'))) {
            if (presentDir.contains(pat.left(pat.size() - 1)))
                hits = 1;
        } else if (pat.contains(QLatin1Char('*'))) {
            const QString suf = pat.mid(pat.lastIndexOf(QLatin1Char('*')) + 1).toLower();
            if (suf.startsWith(QLatin1Char('.')) && presentExt.contains(suf))
                hits = 1;
        }
        r.hits = hits;
        out << r;
    }
    return out;
}

QString Uploader::readGitignore(const QString &folder) const
{
    QFile f(QDir(folder).filePath(QStringLiteral(".gitignore")));
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return QString();
    const QString s = QString::fromUtf8(f.readAll());
    f.close();
    return s;
}

QStringList Uploader::mergeIgnore(const QString &existing, const QStringList &rules)
{
    QSet<QString> have;
    for (const QString &l : existing.split(QRegularExpression(QStringLiteral("\r?\n")))) {
        const QString s = l.trimmed();
        if (s.isEmpty() || s.startsWith(QLatin1Char('#')))
            continue;
        have.insert(s);
    }
    QStringList add;
    for (const QString &r : rules) {
        const QString s = r.trimmed();
        if (s.isEmpty() || have.contains(s))
            continue;
        have.insert(s);
        add << s;
    }
    return add;
}

GitResult Uploader::writeGitignore(const QString &folder, const QStringList &rules)
{
    GitResult r;
    const QString path = QDir(folder).filePath(QStringLiteral(".gitignore"));
    QString old = readGitignore(folder);
    const QStringList add = mergeIgnore(old, rules);
    if (add.isEmpty()) {
        r.ok = true; r.code = 0;
        r.out = QStringLiteral(".gitignore 已包含全部建议规则，无需改动。");
        return r;
    }
    if (!old.isEmpty() && !old.endsWith(QLatin1Char('\n')))
        old += QLatin1Char('\n');
    if (!old.isEmpty())
        old += QStringLiteral("\n");
    old += QStringLiteral("# --- 由「Git 上传」工具的 .gitignore 助手补充 ---\n");
    for (const QString &a : add)
        old += a + QLatin1Char('\n');

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        r.code = 1;
        r.error = QStringLiteral("无法写入 .gitignore：%1").arg(f.errorString());
        return r;
    }
    f.write(old.toUtf8());
    f.close();
    r.ok = true; r.code = 0;
    r.out = QStringLiteral("已向 .gitignore 补充 %1 条规则。").arg(add.size());
    return r;
}

QString Uploader::explainFailure(const QString &out)
{
    static const QRegularExpression perm(QStringLiteral(
        "Authentication|could not read Username|terminal prompts disabled|"
        "403|Permission|Invalid username or password|"
        "unable to get password from user|could not read Password|"
        "support for password authentication was removed"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression nonff(QStringLiteral(
        "rejected|non-fast-forward|fetch first"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression nf(QStringLiteral(
        "not found|404|does not exist|Repository not found"), QRegularExpression::CaseInsensitiveOption);

    if (perm.match(out).hasMatch())
        return QStringLiteral("需要登录 GitHub。请先在任意终端执行一次 git push 完成登录"
                              "（或配置凭据管理器 / PAT），再回到这里重试。");
    if (nonff.match(out).hasMatch())
        return QStringLiteral("远程有本地没有的新提交。先点「拉取合并」把远程同步下来，再重新上传。");
    if (nf.match(out).hasMatch())
        return QStringLiteral("仓库地址不对或没有权限。请到 GitHub 确认仓库已建好、地址正确、账号有权限。");
    if (out.contains(QLatin1String("CONFLICT")))
        return QStringLiteral("合并冲突。请在编辑器里解决冲突文件后重新提交。");
    if (out.contains(QLatin1String("nothing to commit")))
        return QStringLiteral("没有需要提交的变更。");
    if (out.contains(QLatin1String("Please tell me who you are")))
        return QStringLiteral("git 提交身份未配置。请到「账户设置」填写 user.name / user.email 并保存。");
    return QStringLiteral("请查看下面的 git 原始输出定位问题。");
}
