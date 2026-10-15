#include <QApplication>
#include <QStringList>
#include <QStringConverter>
#include <QTextStream>

#include "uploader.h"
#include "uploaderwindow.h"
#include "uismoke.h"

/* 命令行出口：不开界面也能用，便于脚本化与自检。
     --selftest                 跑内置逻辑自检（纯函数 + Python 后端链路）
     --status <folder>          打印目录的仓库状态
     --ignore-scan <folder>     列出该目录的忽略候选规则
     --ignore-write <folder> <规则,规则>   把规则追加进 .gitignore
     --explain <git输出片段>     解读一段 git 错误
     --git-version              打印 git 版本
     --where                    打印后端脚本与解释器位置（排障用） */
static int runCli(const QStringList &args)
{
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);

    if (args.contains(QStringLiteral("--selftest"))) {
        QStringList log;
        const int fail = Uploader::selftest(log);
        out << QString::fromUtf8("Git 上传自检：")
            << (fail == 0 ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL")) << "\n";
        for (const QString &s : log)
            out << s << "\n";
        out << QString::fromUtf8("\n失败项 ") << fail << "\n";
        return fail;
    }

    if (args.contains(QStringLiteral("--where"))) {
        out << QString::fromUtf8("后端脚本 : ") << Uploader::backendPath() << "\n";
        out << QString::fromUtf8("解释器   : ")
            << (Uploader::pythonCmd().isEmpty() ? QString::fromUtf8("未找到")
                                                : Uploader::pythonCmd().join(QLatin1Char(' ')))
            << "\n";
        return 0;
    }

    if (args.contains(QStringLiteral("--git-version"))) {
        QString v;
        if (!Uploader::gitAvailable(&v)) {
            out << QString::fromUtf8("未检测到 git\n");
            return 1;
        }
        out << v << "\n";
        return 0;
    }

    const int i = args.indexOf(QStringLiteral("--status"));
    if (i >= 0 && i + 1 < args.size()) {
        Uploader up;
        const RepoState st = up.inspect(args[i + 1]);
        if (!st.isRepo) {
            out << QString::fromUtf8("不是 git 仓库\n");
            return 1;
        }
        out << QString::fromUtf8("仓库根   : ") << st.toplevel << "\n";
        out << QString::fromUtf8("分支     : ") << st.branch << "\n";
        out << QString::fromUtf8("远程     : ")
            << (st.hasRemote ? st.remote : QString::fromUtf8("未绑定")) << "\n";
        out << QString::fromUtf8("领先/落后: ") << st.ahead << " / " << st.behind << "\n";
        out << QString::fromUtf8("待提交   : 已暂存 ") << st.staged
            << QString::fromUtf8(" / 未暂存 ") << st.unstaged
            << QString::fromUtf8(" / 未跟踪 ") << st.untracked << "\n";
        out << QString::fromUtf8("提交身份 : ") << st.userName << " <" << st.userEmail << ">\n";
        if (!st.recent.isEmpty()) {
            out << QString::fromUtf8("最近提交 :\n");
            for (const QString &l : st.recent)
                out << "  " << l << "\n";
        }
        return 0;
    }

    const int j = args.indexOf(QStringLiteral("--ignore-scan"));
    if (j >= 0 && j + 1 < args.size()) {
        Uploader up;
        const QList<IgnoreRule> rs = up.scanIgnoreCandidates(args[j + 1]);
        int hit = 0;
        for (const IgnoreRule &r : rs) {
            if (r.hits)
                ++hit;
            out << (r.hits ? QStringLiteral("[有] ") : QStringLiteral("[  ] "))
                << r.pattern << "    " << r.why << "\n";
        }
        out << QString::fromUtf8("\n共 %1 条，本目录命中 %2 条\n").arg(rs.size()).arg(hit);
        return 0;
    }

    const int k = args.indexOf(QStringLiteral("--ignore-write"));
    if (k >= 0 && k + 2 < args.size()) {
        Uploader up;
        const QStringList rules = args[k + 2].split(QLatin1Char(','), Qt::SkipEmptyParts);
        const GitResult r = up.writeGitignore(args[k + 1], rules);
        out << (r.ok ? r.out : r.error) << "\n";
        return r.ok ? 0 : 1;
    }

    const int m = args.indexOf(QStringLiteral("--explain"));
    if (m >= 0 && m + 1 < args.size()) {
        out << Uploader::explainFailure(args[m + 1]) << "\n";
        return 0;
    }

    return -1;
}

int main(int argc, char *argv[])
{
    /* 先建 QApplication：applicationDirPath() 要用它，CLI 与自检靠它定位后端脚本 */
    QApplication app(argc, argv);
    app.setApplicationName(QString::fromUtf8("Git 上传"));

    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    if (!args.isEmpty()) {
        const int r = runCli(args);
        if (r >= 0)
            return r;
    }

    /* GUI 崩溃冒烟：开窗口、遍历标签页与控件、点一遍安全按钮（见 uismoke.h） */
    if (args.contains(QStringLiteral("--gui-smoke"))) {
        UploaderWindow w;
        return uiSmoke::run(&w, args);
    }

    UploaderWindow w;
    w.resize(1040, 780);
    w.show();
    return app.exec();
}
