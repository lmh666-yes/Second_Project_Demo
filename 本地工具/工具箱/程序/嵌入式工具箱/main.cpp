#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QString>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>

#include "docbuilder.h"
#include "lookupcli.h"
#include "mainwindow.h"
#include "protocol.h"
#include "rtosgen.h"
#include "theme.h"
#include "toolboxwindow.h"
#include "uismoke.h"

/* 仓库根：向上找含 EnvMonitor 或 .git 的目录，供 RTOS 骨架生成自检定位真实配置。 */
static QString findRepoRoot()
{
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 10; ++i) {
        if (d.exists(QStringLiteral("EnvMonitor")) || d.exists(QStringLiteral(".git")))
            return d.absolutePath();
        if (!d.cdUp())
            break;
    }
    return QString();
}

/* 命令行自检：验证内嵌的 Qt 工具逻辑（不开界面）。 */
static int runCli(const QStringList &args)
{
    /* 不是工具箱自检 → 把参数交给速查出口（--where / --search / --explain / --topics）。
       这些出口与工具箱自检无关，不能被 --selftest 这道门槛挡掉：
       挡掉的后果是"参数没被识别 → 落回开界面"，命令行静默变成开窗口（踩过）。 */
    if (!args.contains(QStringLiteral("--selftest"))) {
        MainWindow lw;
        return runLookupCli(args, &lw);   /* -1 = 不认识，交回 main 决定是否开界面 */
    }

    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);
    int fail = 0;

    QStringList plog;
    const int pfail = Proto::selftest(plog);
    out << QString::fromUtf8("协议工作台自检：")
        << (pfail == 0 ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL")) << "\n";
    for (const QString &s : plog)
        out << s << "\n";
    fail += pfail;

    QHash<QString, QString> a;
    a.insert(QStringLiteral("name"), QStringLiteral("自检项目"));
    a.insert(QStringLiteral("goal"), QStringLiteral("验证文档渲染"));
    const QString doc = renderMarkdown(a.value(QStringLiteral("name")), a);
    const bool docOk = doc.contains(QStringLiteral("# 自检项目"))
                    && doc.contains(QStringLiteral("待确认项"));
    out << QString::fromUtf8("起手助手渲染：")
        << (docOk ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL")) << "\n";
    if (!docOk)
        ++fail;

    QStringList rlog;
    const int rfail = RtosGen::selftest(rlog, findRepoRoot());
    out << QString::fromUtf8("RTOS 骨架生成自检：")
        << (rfail == 0 ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL")) << "\n";
    for (const QString &s : rlog)
        out << s << "\n";
    fail += rfail;

    /* 工具箱根目录解析：判据一旦写错（比如内部目录改名），五个运行器页会整体失效，
       而界面上只显示一行红色告警、很容易被忽略，所以这里必须自检到。
       不只判目录存在，还抽查运行器页真正要用的脚本文件。 */
    const QString tdir = ToolboxWindow::toolsDirPath();
    bool dirOk = !tdir.isEmpty();
    if (dirOk) {
        dirOk = QFile::exists(QDir(tdir).filePath(QStringLiteral("检查/进度跟踪/progress_scan.py")))
             && QFile::exists(QDir(tdir).filePath(QStringLiteral("检查/链路仿真/link_sim.py")))
             && QFile::exists(QDir(tdir).filePath(QStringLiteral("检查/编码守卫/encoding_guard.py")))
             && QDir(tdir).exists(QStringLiteral("shared"));
    }
    out << QString::fromUtf8("工具箱目录解析：")
        << (dirOk ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL"))
        << QString::fromUtf8("（%1）")
               .arg(tdir.isEmpty() ? QString::fromUtf8("没解析出来") : tdir) << "\n";
    if (!dirOk)
        ++fail;

    /* STM32 速查（原独立程序，已并入本工具箱）：数据装载、条目计数、
       6 例搜索、28 例基线逐行回归——这些断言原样平移过来，作为回归闸门。
       数据在「工具箱/速查数据/data/dataset.json」，由网页版那条链导出；
       这里顺带测了 AppPaths 能不能按工具箱结构把这份数据定位出来（挪窝会直接 FAIL）。 */
    out << QString::fromUtf8("\n── STM32 速查（数据与搜索基线）──\n");
    out.flush();          /* 速查 CLI 用自己的 QTextStream(stdout)，不先冲会在输出里串行 */
    MainWindow lw;
    const int lfail = runLookupCli(args, &lw);
    if (lfail < 0) {
        out << QString::fromUtf8("FAIL 速查 CLI 未识别 --selftest\n");
        ++fail;
    } else {
        fail += lfail;
    }

    out << QString::fromUtf8("\n失败项 ") << fail << "\n";
    return fail;
}

int main(int argc, char *argv[])
{
    /* 先建 QApplication：applicationDirPath() 要用它，自检靠它定位仓库根 */
    QApplication app(argc, argv);
    app.setApplicationName(QString::fromUtf8("嵌入式工具箱"));
    Theme::apply();                 /* 统一主题（深色/浅色） */

    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    /* GUI 崩溃冒烟：开窗口、遍历页面与控件、点一遍安全按钮（见 uismoke.h） */
    if (args.contains(QStringLiteral("--gui-smoke"))) {
        ToolboxWindow w;
        return uiSmoke::run(&w, args);
    }

    if (!args.isEmpty()) {
        const int r = runCli(args);
        if (r >= 0)
            return r;
    }

    ToolboxWindow w;
    w.show();
    return app.exec();
}
