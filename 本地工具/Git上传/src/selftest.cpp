#include "uploader.h"
#include "profile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

/* 自检分两层：
   ① 纯 C++ 函数（不需要子进程）：mergeIgnore、explainFailure、defaultIgnoreRules、
      scanIgnoreCandidates、writeGitignore、Profile 结构 —— 这些一定跑得通。
   ② Python 后端链路：探测 python / 调 git_backend.py 的 gitver / status / 临时仓库
      init→add→commit→staged。后端子进程是可用的（Python subprocess 不受本机
      QProcess 限制影响），所以这一层能真跑。 */

static QStringList g_log;
static int g_fail = 0, g_pass = 0, g_skip = 0;

static void chk(bool ok, const QString &what)
{
    if (ok) { ++g_pass; g_log << QStringLiteral("PASS ") + what; }
    else    { ++g_fail; g_log << QStringLiteral("FAIL ") + what; }
}
static void skip(const QString &what) { ++g_skip; g_log << QStringLiteral("SKIP ") + what; }

static bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    f.close();
    return true;
}

int Uploader::selftest(QStringList &log)
{
    g_log.clear(); g_fail = 0; g_pass = 0; g_skip = 0;

    /* ---------- ① mergeIgnore ---------- */
    {
        const QString existing = QStringLiteral("# 注释\n*.o\nbuild/\n");
        const QStringList add = mergeIgnore(existing,
            QStringList{QStringLiteral("*.o"), QStringLiteral("build/"),
                        QStringLiteral("*.hex"), QStringLiteral(""),
                        QStringLiteral("*.hex")});
        chk(add == QStringList{QStringLiteral("*.hex")},
            QStringLiteral("mergeIgnore 只追加缺失规则（实际 %1 条）").arg(add.size()));
        chk(mergeIgnore(QString(), QStringList{QStringLiteral("a"), QStringLiteral("a")})
                == QStringList{QStringLiteral("a")},
            QStringLiteral("mergeIgnore 规则内部去重"));
        chk(mergeIgnore(QStringLiteral("noeol"), QStringList{QStringLiteral("x")})
                == QStringList{QStringLiteral("x")},
            QStringLiteral("mergeIgnore 兼容无尾换行"));
    }

    /* ---------- ② explainFailure ---------- */
    {
        chk(explainFailure(QStringLiteral("fatal: Authentication failed")).contains(QStringLiteral("登录")),
            QStringLiteral("explainFailure 识别认证失败"));
        chk(explainFailure(QStringLiteral("! [rejected] main -> main (fetch first)")).contains(QStringLiteral("拉取")),
            QStringLiteral("explainFailure 识别非快进"));
        chk(explainFailure(QStringLiteral("remote: Repository not found.")).contains(QStringLiteral("地址")),
            QStringLiteral("explainFailure 识别仓库不存在"));
        chk(explainFailure(QStringLiteral("nothing to commit, working tree clean")).contains(QStringLiteral("没有需要提交")),
            QStringLiteral("explainFailure 识别无变更"));
        chk(!explainFailure(QStringLiteral("怪错误")).isEmpty(),
            QStringLiteral("explainFailure 未知错误给兜底"));
    }

    /* ---------- ③ defaultIgnoreRules ---------- */
    {
        const QList<IgnoreRule> rs = defaultIgnoreRules();
        chk(rs.size() >= 20, QStringLiteral("默认忽略规则 %1 条（>=20）").arg(rs.size()));
        bool o = false, b = false, h = false, why = true;
        for (const IgnoreRule &r : rs) {
            if (r.pattern == QLatin1String("*.o"))     o = true;
            if (r.pattern == QLatin1String("build/"))  b = true;
            if (r.pattern == QLatin1String("*.hex"))   h = true;
            if (r.why.trimmed().isEmpty())             why = false;
        }
        chk(o && b && h, QStringLiteral("默认规则含 *.o / build/ / *.hex"));
        chk(why, QStringLiteral("每条默认规则都带说明"));
    }

    /* ---------- ④ writeGitignore 幂等 + 扫描 ---------- */
    {
        QTemporaryDir td;
        chk(td.isValid(), QStringLiteral("临时目录可用"));
        if (td.isValid()) {
            writeFile(td.path() + QStringLiteral("/main.c"), "int main(void){return 0;}\n");
            writeFile(td.path() + QStringLiteral("/a.o"), "x");
            writeFile(td.path() + QStringLiteral("/fw.hex"), "y");
            writeFile(td.path() + QStringLiteral("/Objects/b.o"), "z");

            Uploader up;
            const QList<IgnoreRule> cand = up.scanIgnoreCandidates(td.path());
            bool oHit = false, hexHit = false, objHit = false, elfHit = false;
            for (const IgnoreRule &r : cand) {
                if (r.pattern == QLatin1String("*.o")       && r.hits) oHit = true;
                if (r.pattern == QLatin1String("*.hex")     && r.hits) hexHit = true;
                if (r.pattern == QLatin1String("Objects/")  && r.hits) objHit = true;
                if (r.pattern == QLatin1String("*.elf")     && r.hits) elfHit = true;
            }
            chk(oHit,   QStringLiteral("扫描识别到 .o"));
            chk(hexHit, QStringLiteral("扫描识别到 .hex"));
            chk(objHit, QStringLiteral("扫描识别到 Objects/"));
            chk(!elfHit, QStringLiteral("扫描不误报不存在的 .elf"));

            GitResult r1 = up.writeGitignore(td.path(), QStringList{QStringLiteral("*.o"), QStringLiteral("*.hex")});
            chk(r1.ok && up.readGitignore(td.path()).contains(QStringLiteral("*.o")),
                QStringLiteral("writeGitignore 首次写入成功"));
            GitResult r2 = up.writeGitignore(td.path(), QStringList{QStringLiteral("*.o"), QStringLiteral("*.hex")});
            chk(r2.ok && r2.out.contains(QStringLiteral("无需改动")),
                QStringLiteral("writeGitignore 第二次幂等"));
            up.writeGitignore(td.path(), QStringList{QStringLiteral("*.axf")});
            chk(up.readGitignore(td.path()).count(QStringLiteral("*.axf")) == 1,
                QStringLiteral("writeGitignore 增量补充只写一次"));
        }
    }

    /* ---------- ⑤ Profile 结构 ---------- */
    {
        Profile p;
        chk(p.branch == QLatin1String("main"), QStringLiteral("Profile 默认分支 main"));
        chk(!p.isValid(), QStringLiteral("Profile 无名时 isValid=false"));
        p.name = QStringLiteral("测试档");
        chk(p.isValid(), QStringLiteral("Profile 有名时 isValid=true"));
    }

    /* ---------- ⑤b ProfileStore 往返（用临时 ini，不碰真实配置） ----------
       重点盯"档名里带逗号"：老实现用 QStringList，QSettings 写成逗号分隔，
       档名 "公司,备用" 会被读成两个档。现在存 JSON，必须能原样往返。 */
    {
        QTemporaryDir tmp;
        if (!tmp.isValid()) {
            skip(QStringLiteral("ProfileStore 往返（临时目录建不出来）"));
        } else {
            ProfileStore st(QDir(tmp.path()).filePath(QStringLiteral("_test.ini")));
            chk(st.names().isEmpty(), QStringLiteral("ProfileStore 空 ini 时没有档"));

            Profile a;
            a.name    = QStringLiteral("公司,备用");         /* 关键：含英文逗号 */
            a.account = QStringLiteral("someone");
            a.remote  = QStringLiteral("https://github.com/someone/repo.git");
            a.branch  = QStringLiteral("master");
            st.save(a);
            const QStringList n1 = st.names();
            chk(n1.size() == 1 && n1.first() == a.name,
                QStringLiteral("含逗号的档名存进去仍是一个档（实际 %1 个：%2）")
                    .arg(n1.size()).arg(n1.join(QStringLiteral(" / "))));
            chk(st.exists(a.name), QStringLiteral("含逗号的档名 exists 命中"));
            const Profile g = st.get(a.name);
            chk(g.remote == a.remote && g.branch == a.branch,
                QStringLiteral("含逗号的档名字段能原样读回"));
            chk(st.activeName() == a.name, QStringLiteral("首个档自动成为当前档"));

            Profile b;
            b.name   = QStringLiteral("个人:测试/2");        /* 带冒号与斜杠 */
            b.remote = QStringLiteral("https://github.com/me/two.git");
            st.save(b);
            chk(st.names().size() == 2, QStringLiteral("再加一个档 → 共 2 个"));
            chk(st.rename(a.name, QStringLiteral("公司,主用")),
                QStringLiteral("改名成另一个含逗号的档名"));
            chk(st.names().size() == 2 && !st.exists(a.name)
                    && st.exists(QStringLiteral("公司,主用")),
                QStringLiteral("改名后旧名消失、新名在列"));
            chk(st.activeName() == QStringLiteral("公司,主用"),
                QStringLiteral("改的是当前档 → active 跟着改名"));

            st.remove(QStringLiteral("个人:测试/2"));
            chk(st.names().size() == 1, QStringLiteral("删档后剩 1 个"));
            chk(st.get(QStringLiteral("个人:测试/2")).remote.isEmpty(),
                QStringLiteral("删掉的档读回是空（字段键已清）"));
        }
    }

    /* ---------- ⑥ Python 后端链路 ---------- */
    if (!pythonAvailable()) {
        skip(QStringLiteral("Python 后端链路（未检测到 python）"));
    } else {
        g_log << QStringLiteral("  解释器：") + pythonCmd().join(QLatin1Char(' '));
        const QString bp = backendPath();
        chk(!bp.isEmpty(), QStringLiteral("找到后端脚本 git_backend.py（%1）")
                               .arg(bp.isEmpty() ? QStringLiteral("未找到") : bp));
        if (!bp.isEmpty()) {
            Uploader up;

            /* gitver：顺带验证后端真能起 git */
            QString ver;
            const bool hasGit = gitAvailable(&ver);
            chk(hasGit, QStringLiteral("后端报告 git 可用（%1）").arg(ver));
            if (hasGit) {
                QTemporaryDir td;
                if (!td.isValid()) {
                    skip(QStringLiteral("临时仓库演算（临时目录不可用）"));
                } else {
                    const QString dir = td.path();
                    chk(!up.isRepo(dir), QStringLiteral("空目录初始 isRepo=false"));

                    GitResult ir = up.initRepo(dir, QStringLiteral("main"));
                    chk(ir.ok, QStringLiteral("后端 init 成功（%1）").arg(ir.tail(2)));
                    chk(up.isRepo(dir), QStringLiteral("init 后 isRepo=true"));

                    RepoState s0 = up.inspect(dir);
                    chk(s0.isRepo, QStringLiteral("inspect 认出仓库"));
                    chk(s0.branch == QLatin1String("main"),
                        QStringLiteral("分支 = main（实际 %1）").arg(s0.branch));

                    writeFile(dir + QStringLiteral("/a.c"), "int a;\n");
                    writeFile(dir + QStringLiteral("/b.c"), "int b;\n");
                    RepoState s1 = up.inspect(dir);
                    chk(s1.untracked == 2,
                        QStringLiteral("未跟踪 = 2（实际 %1）").arg(s1.untracked));
                    chk(!s1.hasRemote, QStringLiteral("未绑定时 hasRemote=false"));

                    GitResult ar = up.addAll(dir);
                    chk(ar.ok, QStringLiteral("后端 add 成功"));
                    const Uploader::StagedInfo si = up.staged(dir);
                    chk(si.files.size() == 2,
                        QStringLiteral("staged 列出 2 个（实际 %1）").arg(si.files.size()));
                    chk(si.stat.added == 2 && si.stat.modified == 0 && si.stat.deleted == 0,
                        QStringLiteral("变更统计 新增2/修改0/删除0（实际 %1/%2/%3）")
                            .arg(si.stat.added).arg(si.stat.modified).arg(si.stat.deleted));

                    /* 仓库作用域身份，不污染全局 */
                    up.setIdentity(dir, QStringLiteral("Selftest Bot"),
                                   QStringLiteral("selftest@example.invalid"), false);
                    RepoState sid = up.inspect(dir);
                    chk(sid.userName == QLatin1String("Selftest Bot"),
                        QStringLiteral("仓库作用域 user.name 生效"));
                    chk(sid.userEmail == QLatin1String("selftest@example.invalid"),
                        QStringLiteral("仓库作用域 user.email 生效"));

                    GitResult cr = up.commit(dir, QStringLiteral("自检提交"));
                    chk(cr.ok, QStringLiteral("后端 commit 成功（%1）").arg(cr.tail(2)));
                    RepoState s2 = up.inspect(dir);
                    chk(s2.untracked == 0 && s2.staged == 0,
                        QStringLiteral("提交后工作区干净（未跟踪 %1 / 暂存 %2）")
                            .arg(s2.untracked).arg(s2.staged));
                    chk(!s2.recent.isEmpty()
                            && s2.recent.first().contains(QStringLiteral("自检提交")),
                        QStringLiteral("recent 首条是刚提交的说明"));

                    /* 大文件：默认阈值 50/100MB，2MB 不该命中 */
                    writeFile(dir + QStringLiteral("/blob.bin"), QByteArray(2 * 1024 * 1024, 'z'));
                    const QList<Uploader::BigFile> bf = up.bigFiles(dir,
                        QStringList{QStringLiteral("blob.bin"), QStringLiteral("nope.bin")});
                    chk(bf.isEmpty(),
                        QStringLiteral("bigFiles 2MB 未达阈值时应为空（实际 %1）")
                            .arg(bf.size()));

                    /* 阈值由调用方传入，要能一路传到后端：压到 1MB 就该命中 */
                    const QList<Uploader::BigFile> bf2 = up.bigFiles(dir,
                        QStringList{QStringLiteral("blob.bin")}, 1, 1);
                    chk(bf2.size() == 1 && bf2.first().hard,
                        QStringLiteral("bigFiles 自定义阈值 1MB 命中并判硬（实际 %1）")
                            .arg(bf2.size()));

                    chk(up.readGitignore(dir).isEmpty(),
                        QStringLiteral("无 .gitignore 时读回空串"));
                }
            }
        }
    }

    log = g_log;
    log << QString();
    log << QStringLiteral("通过 %1 / 失败 %2 / 跳过 %3").arg(g_pass).arg(g_fail).arg(g_skip);
    return g_fail;
}
