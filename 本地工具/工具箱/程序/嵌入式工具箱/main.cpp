#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLayout>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QString>
#include <QStringConverter>
#include <QStringList>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <functional>

#include "apppaths.h"
#include "dashboardcanvas.h"
#include "docbuilder.h"
#include "lookupcli.h"
#include "mainwindow.h"
#include "overviewpage.h"
#include "protocol.h"
#include "pyfind.h"
#include "rtosgen.h"
#include "settings.h"
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

/* H1：认出侧栏 —— 带分组头（不可选行）且可选行数正好等于**页面表页数**的那一个列表。
   原先按"条目数 ≥15"猜，页数一变（加/删页）这个门限就得跟着改；现在页数取自
   ToolboxWindow::pageDefs() 这一份单一来源，量测与审计都跟着它走。 */
static QListWidget *findSidebar(QWidget *w)
{
    for (QListWidget *l : w->findChildren<QListWidget *>()) {
        bool hasHeader = false;
        int rows = 0;
        for (int r = 0; r < l->count(); ++r) {
            if (l->item(r)->flags() == Qt::NoItemFlags)
                hasHeader = true;   /* 侧栏的分组头 */
            else
                ++rows;
        }
        if (hasHeader && rows == ToolboxWindow::pageCount())
            return l;
    }
    return nullptr;
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
    int skipped = 0;   /* 脚本因**本机缺外部工具链**（如没装 Keil）自己声明跳过（退出码 3）时计数 */

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

    /* C2：起手助手答案 → 工程模板参数。错例：认得出的外设/任务要齐；
       反例：认不出的文本不许硬塞、缺列的任务行必须跳过（而不是编一个默认值）。 */
    {
        const QString per = scaffoldPeriphArg(
            QStringLiteral("LoRa E22：USART3 / PB10-PB11 / 433 MHz\n"
                           "LED：PF9\nW25QXX：SPI1 / CS"),
            QStringLiteral("用 FreeRTOS。理由：采集与通信要并行"));
        const bool perOk = per.contains(QStringLiteral("usart"))
                        && per.contains(QStringLiteral("lora"))
                        && per.contains(QStringLiteral("spi"))
                        && per.contains(QStringLiteral("led"))
                        && per.contains(QStringLiteral("nvic"));
        const QString per2 = scaffoldPeriphArg(QStringLiteral("电源：3.3V LDO"),
                                              QStringLiteral("不用 RTOS"));
        const bool per2Ok = per2.contains(QStringLiteral("led"))
                         && !per2.contains(QStringLiteral("lora"))
                         && !per2.contains(QStringLiteral("nvic"));
        QStringList skipped;
        const QString tsk = scaffoldTasksArg(
            QStringLiteral("vTaskSensor：2 s 周期 / 优先级 3 / 栈 256 字\n"
                           "vTaskComm：队列触发 / 优先级 4 / 栈 256 字\n"
                           "vTaskBug：优先级 3 但没写栈"), &skipped);
        const bool tskOk = tsk == QStringLiteral("Sensor:3:256,Comm:4:256")
                        && skipped.size() == 1;
        const bool c2Ok = perOk && per2Ok && tskOk;
        out << QString::fromUtf8("起手助手→工程模板参数：")
            << (c2Ok ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL")) << "\n";
        if (!c2Ok)
            ++fail;
    }

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
        /* 抽查每个运行器页真正要用的脚本：漏一个就等于"该页点了运行才发现脚本不在"。
           新增工具时这里要同步补上（LoRa 检查 / 协议校验 / 断网缓存 就是这么补进来的）。 */
        dirOk = QFile::exists(QDir(tdir).filePath(QStringLiteral("检查/进度跟踪/progress_scan.py")))
             && QFile::exists(QDir(tdir).filePath(QStringLiteral("检查/链路仿真/link_sim.py")))
             && QFile::exists(QDir(tdir).filePath(QStringLiteral("检查/编码守卫/encoding_guard.py")))
             && QFile::exists(QDir(tdir).filePath(QStringLiteral("检查/RTOS检查/rtos_check.py")))
             && QFile::exists(QDir(tdir).filePath(QStringLiteral("检查/引脚规划/pin_planner.py")))
             && QFile::exists(QDir(tdir).filePath(QStringLiteral("检查/LoRa检查/lora_check.py")))
             && QFile::exists(QDir(tdir).filePath(QStringLiteral("检查/协议校验/protocol_check.py")))
             && QDir(tdir).exists(QStringLiteral("shared"));
    }
    out << QString::fromUtf8("工具箱目录解析：")
        << (dirOk ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL"))
        << QString::fromUtf8("（%1）")
               .arg(tdir.isEmpty() ? QString::fromUtf8("没解析出来") : tdir) << "\n";
    if (!dirOk)
        ++fail;

    /* H1：页面表口径 —— 页数/分组从 ToolboxWindow::pageDefs() 这一份推（侧栏、堆栈、
       量测同源）。这里断言结构自洽（分组数与组数一致、各组之和=总页数、页名不重），
       扫现行文档与注释挡住"再加一页忘了改说明"（审计时这类写死的页数散在 5 处、
       与页面表不符）。 */
    {
        const QStringList names  = ToolboxWindow::pageNames();
        const QStringList groups = ToolboxWindow::pageGroupNames();
        const QList<int>  counts = ToolboxWindow::pageGroupCounts();
        int sum = 0;
        for (int c : counts)
            sum += c;
        bool ok = names.size() == ToolboxWindow::pageCount()
               && groups.size() == counts.size()
               && sum == names.size()
               && QSet<QString>(names.begin(), names.end()).size() == names.size();
        /* 现行文档/注释里不许再写死页数 —— 写死的地方改了页面表却忘改，就是这个病根。
           数字与后缀拼起来写，免得本自检读到自己这份源码时误判"文档里还留着旧页数"。 */
        const QString oldNum = QStringLiteral("15");
        const QStringList staleTokens = {oldNum + QString::fromUtf8(" 个页面"),
                                         oldNum + QString::fromUtf8(" 页")};
        QStringList stale;
        const QStringList docs = {
            QStringLiteral("程序/嵌入式工具箱/说明.md"),
            QStringLiteral("程序/嵌入式工具箱/toolboxwindow.cpp"),
            QStringLiteral("程序/嵌入式工具箱/overviewpage.cpp"),
            QStringLiteral("程序/嵌入式工具箱/main.cpp"),
        };
        for (const QString &rel : docs) {
            QFile f(QDir(tdir).filePath(rel));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QString txt = QString::fromUtf8(f.readAll());
            for (const QString &bad : staleTokens)
                if (txt.contains(bad))
                    stale << QStringLiteral("%1:%2").arg(rel, bad);
        }
        ok = ok && stale.isEmpty();
        out << QString::fromUtf8("页面表口径：")
            << (ok ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL"))
            << QString::fromUtf8("（%1 页 / %2 组，各组页数 ").arg(names.size()).arg(groups.size());
        QStringList cnt;
        for (int c : counts)
            cnt << QString::number(c);
        out << cnt.join(QStringLiteral("·")) << QString::fromUtf8("）\n");
        if (!ok) {
            ++fail;
            if (!stale.isEmpty())
                out << QString::fromUtf8("  文档/注释仍写死旧页数：")
                    << stale.join(QStringLiteral("、")) << "\n";
        }
    }

    /* OTA 台架页引用的 ota_send.py 在仓库根的 OTA/tools/ 下（工具箱树之外）。
       判据是一路向上找到含 EnvMonitor/.git 的那层再拼 OTA/tools/ota_send.py；
       定位失败时那一页会整体不可用，所以单独自检。 */
    {
        const QString root = findRepoRoot();
        const bool otaOk = !root.isEmpty()
                           && QFile::exists(QDir(root).filePath(
                                  QStringLiteral("OTA/tools/ota_send.py")));
        out << QString::fromUtf8("OTA 台架脚本定位：")
            << (otaOk ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL")) << "\n";
        if (!otaOk)
            ++fail;
    }

    /* D1：把 8 个检查器自己的 --selftest 也挂进来跑。
       为什么必须挂：`检查/RTOS检查/rtos_check.py` 曾因改造后断言失效而自检红灯
       （27 项里 1 项 FAIL、退出码 1），但工具箱自检当时只查"脚本在不在"，
       红灯完全看不见 —— 于是"每一轮都能抓到"变成了"靠人想起来才跑"。
       这里逐个跑 --selftest，非 0 退出码直接计入失败项。 */
    {
        struct Checker { const char *name; const char *rel; };
        const Checker checkers[] = {
            {"进度扫描",     "检查/进度跟踪/progress_scan.py"},
            {"引脚规划",     "检查/引脚规划/pin_planner.py"},
            {"RTOS 检查",    "检查/RTOS检查/rtos_check.py"},
            {"编码守卫",     "检查/编码守卫/encoding_guard.py"},
            {"链路仿真",     "检查/链路仿真/link_sim.py"},
            {"LoRa 检查",    "检查/LoRa检查/lora_check.py"},
            {"协议校验",     "检查/协议校验/protocol_check.py"},
            {"断网缓存",     "检查/断网缓存/cache_check.py"},
            /* 2026-10-20：本轮新增的脚本也挂进这道闸门 —— 它们各自都有 --selftest，
               不挂进来就等于"新加的自检没人踩"，只在 coverage_board 汇总时才动一下。
               注意：链路仿真的「全链路时序」是 chain_sim.py（link_sim.py 仍管造帧）。 */
            {"代码索引",     "检查/代码索引/code_index.py"},
            {"协议对照",     "检查/协议对照/host_compare.py"},
            {"工程配置",     "检查/工程配置/config_audit.py"},
            {"阶段判定",     "检查/阶段判定/stage_status.py"},
            {"链路时序",     "检查/链路仿真/chain_sim.py"},
            {"构建烧录",     "检查/构建烧录/build_flash.py"},
            {"阶段自检生成", "检查/构建烧录/gen_selftest.py"},
            {"报告生成",     "检查/报告/report_gen.py"},
            {"覆盖率看板",   "检查/覆盖率/coverage_board.py"},
            {"示例插件",     "检查/示例插件/check.py"},
            /* 生成器也挂进来：它的"骨架阶段提示"曾出现"两种风格不一致 / 无任务时阶段3缺号"，
               靠自检钉住输出结构，别靠人想起来去比对。 */
            {"工程模板生成", "程序/工程模板/project_scaffold.py"},
        };
        out << QString::fromUtf8("\n── 各脚本自检（--selftest）：18 个脚本 + 工程模板生成器 ──\n");
        out.flush();
        const QStringList py = PyFind::detect();
        if (py.isEmpty()) {
            out << QString::fromUtf8("FAIL 未找到 Python，跑不了各检查器自检\n");
            ++fail;
        } else {
            for (const Checker &c : checkers) {
                const QString script = tdir.isEmpty()
                                           ? QString()
                                           : QDir(tdir).filePath(QString::fromUtf8(c.rel));
                QString note;
                int rc = -1;                        /* 脚本退出码；-1 = 没跑起来 */
                if (script.isEmpty() || !QFile::exists(script)) {
                    note = QString::fromUtf8("脚本不存在");
                } else {
                    QProcess p;
                    p.setWorkingDirectory(QFileInfo(script).absolutePath());
                    QStringList a = py.mid(1);
                    a << script << QStringLiteral("--selftest");
                    p.start(py.value(0), a);
                    if (!p.waitForStarted(5000) || !p.waitForFinished(120000)) {
                        p.kill();
                        note = QString::fromUtf8("启动失败或超时");
                    } else {
                        rc = (p.exitStatus() == QProcess::NormalExit) ? p.exitCode() : -1;
                        /* 约定：0=全过；1=有失败；**3=跳过**（本机缺外部工具链，如没装 Keil
                           时的 armcc 编译校验）。跳过**不算失败** —— 换台没工具的机器也能过闸门。 */
                        const bool skippedRun = (rc == 3);
                        if (skippedRun || rc != 0) {
                            for (const QString &ln :
                                 QString::fromUtf8(p.readAllStandardOutput())
                                     .split(QLatin1Char('\n'))) {
                                const QString t = ln.trimmed();
                                if (skippedRun ? t.startsWith(QStringLiteral("SKIP"))
                                               : t.contains(QStringLiteral("FAIL"))) {
                                    note = t;   /* 失败摘 FAIL 行；跳过摘 SKIP 行 */
                                    break;
                                }
                            }
                        }
                    }
                }
                if (rc == 3) {
                    out << QString::fromUtf8("SKIP ") << QString::fromUtf8(c.name)
                        << (note.isEmpty() ? QString()
                                           : QString::fromUtf8("（%1）").arg(note)) << "\n";
                    ++skipped;
                } else {
                    const bool ok = (rc == 0);
                    out << (ok ? QString::fromUtf8("PASS ") : QString::fromUtf8("FAIL "))
                        << QString::fromUtf8(c.name)
                        << (note.isEmpty() ? QString()
                                           : QString::fromUtf8("（%1）").arg(note)) << "\n";
                    if (!ok)
                        ++fail;
                }
            }
        }
    }

    /* 单片机手册（原独立程序，已并入本工具箱）：数据装载、条目计数、
       6 例搜索、28 例基线逐行回归——这些断言原样平移过来，作为回归闸门。
       数据在「工具箱/速查数据/data/dataset.json」，由网页版那条链导出；
       这里顺带测了 AppPaths 能不能按工具箱结构把这份数据定位出来（挪窝会直接 FAIL）。 */
    out << QString::fromUtf8("\n── 单片机手册（数据与搜索基线）──\n");
    out.flush();          /* 手册 CLI 用自己的 QTextStream(stdout)，不先冲会在输出里串行 */
    MainWindow lw;
    const int lfail = runLookupCli(args, &lw);
    if (lfail < 0) {
        out << QString::fromUtf8("FAIL 手册 CLI 未识别 --selftest\n");
        ++fail;
    } else {
        fail += lfail;
        /* C1：报告里点条目名 → 手册直达。用真实数据验跳转 API：
           错例（手册里有的词要定位得到）/ 反例（手册里没有的词要返回 0，不能乱指一个条目）。 */
        const int hitDog = lw.jumpToTerm(QStringLiteral("看门狗"));
        const int hitJunk = lw.jumpToTerm(QStringLiteral("这词手册里绝对没有_zzz"));
        const bool c1Ok = hitDog > 0 && hitJunk == 0;
        out << QString::fromUtf8("手册条目直达：")
            << (c1Ok ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL"))
            << QString::fromUtf8("（看门狗 %1 条 / 乱词 %2 条）\n").arg(hitDog).arg(hitJunk);
        if (!c1Ok)
            ++fail;

        /* 项目引导「下一步：阶段 N」入口 —— 读的是该工程进度文档的「阶段总表」。
           错例：选中板1 必须指出一个阶段（"下一步"或"全部完成"），且该阶段卡在列表里真能点中；
           反例：选「全部工程」时不知道说的是哪块板，必须不显示（不能瞎指一个阶段）。 */
        const QString hintAll = lw.nextStageHintFor(QStringLiteral("全部工程"));
        const QString hintB1  = lw.nextStageHintFor(QStringLiteral("板1"));
        /* 横幅里写的阶段号，左侧列表里得真有一张对应的卡（否则"点这里直达"是空话） */
        QString hintStage;
        {
            const int p = hintB1.indexOf(QStringLiteral("阶段 "));
            if (p >= 0) {
                const int q = hintB1.indexOf(QStringLiteral("｜"), p);
                if (q > p)
                    hintStage = hintB1.mid(p, q - p + 1);      /* 例："阶段 0｜" */
            }
        }
        bool targetOk = false;
        if (!hintStage.isEmpty()) {
            for (int i : lw.shown()) {
                const Card &c = lw.dataset().at(i);
                if (c.b == QStringLiteral("板1") && c.n.contains(hintStage)) {
                    targetOk = true;
                    break;
                }
            }
        }
        const bool hintOk = hintB1.contains(QStringLiteral("阶段"))
                         && (hintB1.contains(QStringLiteral("下一步"))
                             || hintB1.contains(QStringLiteral("全部完成")))
                         && targetOk
                         && hintAll.isEmpty();
        out << QString::fromUtf8("项目引导「下一步」入口：")
            << (hintOk ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL"))
            << QString::fromUtf8("（板1 → %1；点得中 %2；全部工程 → %3）\n")
                   .arg(hintB1.isEmpty() ? QString::fromUtf8("不显示(错)") : hintB1,
                        targetOk ? QString::fromUtf8("是") : QString::fromUtf8("否(错)"),
                        hintAll.isEmpty() ? QString::fromUtf8("不显示")
                                          : QString::fromUtf8("显示了(错)"));
        if (!hintOk)
            ++fail;

        /* A3：横幅旁边多了个「跑这一步」按钮（直接触发 build_flash.py --stage-run N）。
           错例：板1 有下一步 → 按钮必须显示；反例：「全部工程」不猜 → 必须不显示。 */
        const bool runB1  = lw.canRunNextStageFor(QStringLiteral("板1"));
        const bool runAll = lw.canRunNextStageFor(QStringLiteral("全部工程"));
        const bool runOk  = runB1 && !runAll;
        out << QString::fromUtf8("项目引导「跑这一步」按钮：")
            << (runOk ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL"))
            << QString::fromUtf8("（板1 → %1；全部工程 → %2）\n")
                   .arg(runB1 ? QString::fromUtf8("显示") : QString::fromUtf8("不显示(错)"),
                        runAll ? QString::fromUtf8("显示了(错)") : QString::fromUtf8("不显示"));
        if (!runOk)
            ++fail;

        /* H13：手册数据与网页版同源。Qt 版（内嵌页）读的是网页版那条链导出的
           dataset.json —— 断言它导出时写下的 meta.sourceHtml 正是网页版 HTML，
           且 meta.total 与 all 实际条数一致。这样"Qt 版看到的就是网页版同一份数据"
           这句结论有机器校验，而不是只写在文档里。 */
        const QString dsPath = AppPaths::datasetPath();
        bool h13Ok = false;
        QString h13Note;
        QFile df(dsPath);
        if (dsPath.isEmpty() || !df.open(QIODevice::ReadOnly)) {
            h13Note = QString::fromUtf8("dataset.json 读不到（%1）").arg(dsPath);
        } else {
            const QJsonObject root = QJsonDocument::fromJson(df.readAll()).object();
            const QJsonObject meta = root.value(QStringLiteral("meta")).toObject();
            const QString src = meta.value(QStringLiteral("sourceHtml")).toString();
            const int total = meta.value(QStringLiteral("total")).toInt(-1);
            const int real = root.value(QStringLiteral("all")).toArray().size();
            h13Ok = src == QStringLiteral("标准库函数速查.html") && total == real && real > 0;
            h13Note = QString::fromUtf8("sourceHtml=%1 total=%2 all=%3")
                          .arg(src.isEmpty() ? QString::fromUtf8("(缺)") : src)
                          .arg(total).arg(real);
        }
        out << QString::fromUtf8("手册数据与网页版同源（H13）：")
            << (h13Ok ? QString::fromUtf8("PASS") : QString::fromUtf8("FAIL"))
            << QString::fromUtf8("（%1）\n").arg(h13Note);
        if (!h13Ok)
            ++fail;
    }

    out << QString::fromUtf8("\n失败项 ") << fail
        << (skipped > 0 ? QString::fromUtf8("（另有 %1 项跳过：本机缺外部工具链）").arg(skipped)
                        : QString())
        << "\n";
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

    /* 带参数运行 = 测试 / 量测出口。这些出口会点界面开关、走遍所有页面，而它们写的是
       **用户自己的**两份本机状态：
         · _toolbox.ini（主题、行尾检查、窗口位置）
         · 速查数据\_stm32lookup.ini（速查页主题、当前板块、"最近浏览"历史）
       实测跑一轮 --gui-smoke 就把 enc/eol 从 false 改成 true、ui/theme 1→0，
       还把速查的"最近浏览"换成冒烟自己的遍历顺序 —— 等于替用户改了设置、丢了他的记录。
       所以这里把两份 ini 各拷一份到临时目录，再让本进程读写都落在副本上：
       读到的仍是用户那份的内容（主题/页号口径不变），写完只写副本，用户文件一个字节不动。
       （项目里对 ui/welcomeShown 早就提防过同一件事，见 toolboxwindow.h 的说明。） */
    if (!args.isEmpty()) {
        Settings st;
        const QString boxDir =
            QDir(QDir::tempPath()).filePath(QStringLiteral("toolbox_probe_ini"));
        QDir().mkpath(boxDir);
        struct { const char *env; QString src; } boxes[2] = {
            { "TOOLBOX_INI",        st.filePath()       },
            { "TOOLBOX_LOOKUP_INI", AppPaths::iniPath() },
        };
        for (const auto &b : boxes) {
            if (b.src.isEmpty())
                continue;
            const QString dst = QDir(boxDir).filePath(QFileInfo(b.src).fileName());
            QFile::remove(dst);
            if (QFileInfo::exists(b.src))
                QFile::copy(b.src, dst);      /* 先拷用户那份，读起来与平时一致 */
            qputenv(b.env, dst.toLocal8Bit());
        }

        /* A4：除了两份 ini，测试/量测出口还会写两份"历史"，同样别落到用户数据上：
             · 工具箱\_体检历史\*.json              —— 总览面板「历史回看」的快照（只留 20 份）
             · 检查\进度跟踪\_progress_history.json —— 进度趋势的读数（只留 30 份）
           实测每跑一轮 --shot-overview / --gui-smoke / --ui-audit 就往里塞一轮，
           于是登录到趋势图上的"待办 / 桩"变化里混着测试轮 —— 用户自己的真跑被稀释。
           做法与 ini 一致：把两份历史各拷进临时沙箱，读写都落副本（没有原件就从空开始）。
           子进程（各检查器 / 总览面板取数）继承本进程环境，所以 Python 那边读到的是同一份沙箱。 */
        const QString tdir = ToolboxWindow::toolsDirPath();
        if (!tdir.isEmpty()) {
            const QString histBox = QDir(boxDir).filePath(QStringLiteral("hist"));
            QDir().mkpath(histBox);

            /* 总览快照：整目录（都是平铺的 *.json）拷进沙箱，环境变量告诉面板改从这里读写 */
            const QString snapSrc = QDir(tdir).filePath(QStringLiteral("_体检历史"));
            const QString snapDst = QDir(histBox).filePath(QStringLiteral("_体检历史"));
            QDir(snapDst).removeRecursively();
            QDir().mkpath(snapDst);
            for (const QString &f : QDir(snapSrc).entryList(QStringList{QStringLiteral("*.json")},
                                                           QDir::Files))
                QFile::copy(QDir(snapSrc).filePath(f), QDir(snapDst).filePath(f));
            qputenv("TOOLBOX_HISTORY_DIR", snapDst.toLocal8Bit());

            /* 进度趋势：单个 json 文件 */
            const QString phSrc = QDir(tdir).filePath(
                QStringLiteral("检查/进度跟踪/_progress_history.json"));
            const QString phDst = QDir(histBox).filePath(QStringLiteral("_progress_history.json"));
            QFile::remove(phDst);
            if (QFileInfo::exists(phSrc))
                QFile::copy(phSrc, phDst);
            qputenv("TOOLBOX_PROGRESS_HISTORY", phDst.toLocal8Bit());
        }
    }

    /* GUI 崩溃冒烟：开窗口、遍历页面与控件、点一遍安全按钮（见 uismoke.h） */
    if (args.contains(QStringLiteral("--gui-smoke"))) {
        ToolboxWindow w(nullptr, true);   /* 测试路径：别弹首次向导、也别记"已弹过" */
        return uiSmoke::run(&w, args);
    }

    /* 体检面板截图：直接构造总览页、同步取数、渲染成 PNG。
       用途：验证可视化面板真的画出了东西（GUI 冒烟只证明"没崩"，看不到画没画）。
       用法：Toolbox.exe --shot-overview <输出.png> [--drop <工具键,...>] [--only-findings]
             --drop 用于断言"给定的工具没取到数时，面板标成未取到而不是绿色正常"。
             --only-findings 用于断言"报告原文的只看命中项开关真的把非命中项段落删掉了"。 */
    if (args.contains(QStringLiteral("--shot-overview"))) {
        QString path = QStringLiteral("overview.png");
        for (int i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == QStringLiteral("--shot-overview"))
                path = args[i + 1];
        }
        /* --drop <键,...>：故意让某几个工具"没取到"，断言面板把它标出来而不是画成绿色。
           用法：--shot-overview out.png --drop protocol,rtos */
        QStringList dropKeys;
        for (int i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == QStringLiteral("--drop"))
                dropKeys = args[i + 1].split(QLatin1Char(','), Qt::SkipEmptyParts);
        }
        OverviewPage page(ToolboxWindow::toolsDirPath());
        page.setDropKeys(dropKeys);
        page.resize(1180, 960);
        page.show();
        QMetaObject::invokeMethod(&page, "refresh", Qt::DirectConnection);

        /* refresh 是异步的（各检查器并行跑）：跑事件循环等它们回来，最多 25s，
           以画布命中区被填充作为完成判据。 */
        DashboardCanvas *canvas =
            page.findChild<DashboardCanvas *>(QStringLiteral("mainDashboard"));
        QElapsedTimer et;
        et.start();
        while (et.elapsed() < 25000) {
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            if (canvas && canvas->hitCount() > 0)
                break;
        }
        QApplication::processEvents();

        /* 报告原文里可点的「文件:行」链接数（D3）：为 0 说明没转成链接或报告里没有位置信息 */
        QTextStream(stdout) << QString::fromUtf8("报告可点链接: ") << page.srcLinkCount() << "\n";
        /* C1：报告原文里可点的「条目名」链接数（跳单片机手册）。
           报告属检查产物、不入库，全新克隆上可能一份都没有 —— 所以只在"明明有报告
           （有 file:行 链接）却一条条目名链接都没有"时才判失败，避免误伤。 */
        const int docN = page.docLinkCount();
        QTextStream(stdout) << QString::fromUtf8("手册可点条目: ") << docN << "\n";
        if (docN < 1 && page.srcLinkCount() > 0)
            return 2;
        /* A1：历史回看下拉框里应至少有一轮快照（本轮刷新刚写的那份） */
        const int histN = page.historyCount();
        QTextStream(stdout) << QString::fromUtf8("历史快照轮数: ") << histN << "\n";
        if (histN < 1)
            return 2;

        /* H4：带参数运行走沙箱 —— 刚归档的这份快照应带 `_source=test`（测试轮标记）。
           有了标记，将来万一测试轮混进真实历史，面板/工具能识别并剔除，趋势不再被稀释。 */
        {
            const QByteArray box = qgetenv("TOOLBOX_HISTORY_DIR");
            const QString hdir = box.isEmpty()
                                     ? QDir(ToolboxWindow::toolsDirPath())
                                           .filePath(QStringLiteral("_体检历史"))
                                     : QString::fromLocal8Bit(box);
            QDir hd(hdir);
            const QStringList snaps = hd.entryList(QStringList{QStringLiteral("*.json")},
                                                   QDir::Files, QDir::Name);
            bool marked = false;
            if (!snaps.isEmpty()) {
                QFile sf(hd.filePath(snaps.last()));
                if (sf.open(QIODevice::ReadOnly))
                    marked = QString::fromUtf8(sf.readAll())
                                 .contains(QStringLiteral("\"_source\":\"test\""));
            }
            QTextStream(stdout) << QString::fromUtf8("最近快照来源标记: ")
                                << (marked ? QStringLiteral("test（测试轮）")
                                           : QStringLiteral("缺失/非测试")) << "\n";
            if (!marked)
                return 2;
        }

        /* 断言交互区域已登记：健康度卡片 + 各板热力图格，取数成功时必然 > 6。
           命中区一旦变空，悬停与点击都会失灵，所以在这里兜住。 */
        if (canvas) {
            const int n = canvas->hitCount();
            QTextStream(stdout) << QString::fromUtf8("交互命中区: ") << n << "\n";
            if (n < 6)
                return 2;
            /* A2：进度历史有两轮以上就必须画出趋势折线，折线的数据点要登记悬停命中区。
               错例判据：有折线却一个趋势点命中区都没有（改没了悬停读数）→ 失败。
               反例：历史不足两轮时不画折线，也就不断言，免得误伤。 */
            const int rounds = page.progressHistoryRounds();
            const int tipN = canvas->tipHitCount();
            QTextStream(stdout) << QString::fromUtf8("趋势点命中区: ") << tipN
                                << QString::fromUtf8("（历史 ") << rounds
                                << QString::fromUtf8(" 轮）\n");
            if (rounds >= 2 && tipN < 1)
                return 2;
            /* B3：面板顶部"一句话结论"必须非空；有问题项时得写清严重/注意，
               没有问题时必须写"正常"（不能瞎报）。 */
            const QString concl = canvas->conclusionText();
            const int conclN = canvas->conclusionCount();
            QTextStream(stdout) << QString::fromUtf8("体检结论: ") << concl
                                << QString::fromUtf8("（问题项 ") << conclN << QString::fromUtf8("）\n");
            if (concl.isEmpty())
                return 2;
            if (conclN > 0
                && !concl.contains(QStringLiteral("严重"))
                && !concl.contains(QStringLiteral("注意")))
                return 2;
            if (conclN == 0 && !concl.contains(QStringLiteral("正常")))
                return 2;
            /* D4：--drop 指定的键必须被记成"未取到"（缺数不许画成绿色"正常"） */
            if (!dropKeys.isEmpty()) {
                const int dn = canvas->failedCount();
                QTextStream(stdout) << QString::fromUtf8("缺数标记: ") << dn
                                    << QString::fromUtf8(" / ") << dropKeys.size() << "\n";
                if (dn != dropKeys.size())
                    return 2;
            }
        }
        /* A4：--only-findings 打开"只看命中项"开关，断言过滤真的生效。
           错例：过滤后条目名链接没变少（等于没过滤）；反例：过滤后一条都不剩（删过头）。
           报告是检查产物、可能一份都没有 —— 所以只在"过滤前就有条目名链接"时才断言。 */
        if (args.contains(QStringLiteral("--only-findings"))) {
            const int fullDoc = page.docLinkCount();
            if (QCheckBox *box = page.findChild<QCheckBox *>(QStringLiteral("onlyFindingsBox"))) {
                box->setChecked(true);
                QApplication::processEvents();
            }
            const int fnDoc = page.docLinkCount();
            QTextStream(stdout) << QString::fromUtf8("只看命中项链接: ") << fullDoc
                                << QString::fromUtf8(" → ") << fnDoc << "\n";
            if (fullDoc > 0 && (fnDoc < 1 || fnDoc >= fullDoc))
                return 2;
        }
        return page.grab().save(path) ? 0 : 1;
    }

    /* C1 全局搜索（Ctrl+K）探针：直接调 paletteHits，断言三类候选都搜得到、乱词搜不到。
       错例：页面名 / 手册条目必须命中（搜不到 → 面板等于坏了）；报告在时「检查项」也要命中。
       反例：乱词必须 0 条；空查询不许把上千条手册条目倒出来。 */
    if (args.contains(QStringLiteral("--palette-probe"))) {
        ToolboxWindow w(nullptr, true);   /* 探针路径：别弹首次向导、也别记"已弹过" */
        const QStringList pageHit = w.paletteHits(QStringLiteral("引脚规划"));
        const QStringList findHit = w.paletteHits(QStringLiteral("冲突"));
        const QStringList manHit  = w.paletteHits(QStringLiteral("USART"));
        const QStringList bogus   = w.paletteHits(QStringLiteral("zzzqqq乱词xyz"));
        const QStringList blank   = w.paletteHits(QString());
        bool hasFinding = false;
        for (const QString &h : findHit)
            if (h.startsWith(QStringLiteral("检查项|")))
                hasFinding = true;
        const QString pinReport =
            QDir(ToolboxWindow::toolsDirPath()).filePath(QStringLiteral("检查/引脚规划/引脚规划报告.md"));
        const bool reportsExist = QFileInfo::exists(pinReport);

        QTextStream(stdout) << QString::fromUtf8("页面命中: ") << pageHit.size()
                            << QString::fromUtf8("　检查项命中: ") << findHit.size()
                            << QString::fromUtf8("　手册命中: ") << manHit.size()
                            << QString::fromUtf8("　乱词命中: ") << bogus.size()
                            << QString::fromUtf8("　报告在: ") << (reportsExist ? "是" : "否") << "\n";
        bool ok = pageHit.contains(QStringLiteral("页面|引脚规划"))
                  && !manHit.isEmpty()
                  && bogus.isEmpty()
                  && !blank.isEmpty();
        for (const QString &h : blank)               /* 反例：空查询不该列手册条目 */
            if (h.startsWith(QStringLiteral("手册|")))
                ok = false;
        if (reportsExist && !hasFinding)              /* 错例：有报告却没列出「检查项」 */
            ok = false;
        return ok ? 0 : 2;                            /* 探针跑完就退，别掉进后面的 GUI 事件循环 */
    }

    /* D2 首次运行向导探针：只验证"只弹一次"这条逻辑（不真弹窗）。
       错例：开关从未置真时 shouldShowWelcome 必须为真；反例：置真后必须为假。
       跑完把原值写回去，别改坏用户的 _toolbox.ini。 */
    if (args.contains(QStringLiteral("--welcome-probe"))) {
        Settings st;
        const bool saved = st.value(QStringLiteral("ui/welcomeShown"), false).toBool();
        st.setValue(QStringLiteral("ui/welcomeShown"), true);
        const bool afterShown = ToolboxWindow::shouldShowWelcome();
        st.setValue(QStringLiteral("ui/welcomeShown"), false);
        const bool afterReset = ToolboxWindow::shouldShowWelcome();
        st.setValue(QStringLiteral("ui/welcomeShown"), saved);   /* 还原 */
        QTextStream(stdout) << QString::fromUtf8("未弹过→应弹: ") << (afterReset ? "是" : "否")
                            << QString::fromUtf8("　已弹过→不再弹: ")
                            << (afterShown ? "否(错)" : "是") << "\n";
        return (!afterShown && afterReset) ? 0 : 2;
    }

    /* D3 清理口径探针：用临时目录造 25 份快照，断言只列出最早的 5 份该删（错例），
       并断言"不超过上限时不列任何东西"（反例）。不碰真实 _体检历史/。 */
    if (args.contains(QStringLiteral("--clean-probe"))) {
        const QString tmp = QDir(QDir::tempPath())
                                .filePath(QStringLiteral("toolbox_clean_probe"));
        QDir().mkpath(tmp);
        for (int i = 1; i <= 25; ++i)
            QFile(QDir(tmp).filePath(QStringLiteral("2026010%1_%02d0000.json")
                                         .arg(i / 10).arg(i % 100))).open(QIODevice::WriteOnly);
        const QStringList old = ToolboxWindow::overdueSnapshots(tmp, 20);
        const QStringList none = ToolboxWindow::overdueSnapshots(tmp, 25);
        const int reps = ToolboxWindow::reportFiles(ToolboxWindow::toolsDirPath()).size();
        QTextStream(stdout) << QString::fromUtf8("清理探针：25 份该删 ") << old.size()
                            << QString::fromUtf8("　上限=份数时该删 ") << none.size()
                            << QString::fromUtf8("　报告产物 ") << reps << "\n";
        QDir(tmp).removeRecursively();
        return (old.size() == 5 && none.isEmpty() && reps > 0) ? 0 : 2;
    }

    /* A2 探针：断言"每张健康度卡片指向的报告小节确实存在"（错例）+ "乱词找不到"（反例）。
       报告是检查产物、可能一份都没有 —— 那种情况下只断言关键词非空，不要求小节存在。 */
    if (args.contains(QStringLiteral("--finding-probe"))) {
        OverviewPage page(ToolboxWindow::toolsDirPath());
        /* H5：构造完**不该**已经载入报告原文（那是读 7 份 md、解析快照的重活，要推迟到首次进本页）；
           访问过计数之后才该载入。错例：又挪回构造函数 → 前半条FAIL；访问了还不载 → 后半条FAIL。 */
        const bool deferredOk = !page.reportsLoaded();
        const QStringList keys{QStringLiteral("pin"), QStringLiteral("rtos"),
                               QStringLiteral("enc"), QStringLiteral("lora"),
                               QStringLiteral("protocol"), QStringLiteral("cache"),
                               QStringLiteral("progress")};
        const bool anyReport = page.srcLinkCount() > 0 || page.docLinkCount() > 0;
        const bool onDemandOk = page.reportsLoaded();
        QTextStream(stdout) << QString::fromUtf8("报告延迟载入：构造时不载 ")
                            << (deferredOk ? QStringLiteral("是") : QStringLiteral("否"))
                            << QString::fromUtf8("　访问后已载 ")
                            << (onDemandOk ? QStringLiteral("是") : QStringLiteral("否")) << "\n";
        int empty = 0, missing = 0;
        QString detail;
        for (const QString &k : keys) {
            const QString sec = page.sectionKeywordOf(k);
            if (sec.isEmpty()) {
                ++empty;
                continue;
            }
            if (anyReport && !page.reportHasText(sec)) {
                ++missing;
                detail += k + QLatin1Char(' ') + sec + QStringLiteral("；");
            }
        }
        const bool bogus = page.reportHasText(QStringLiteral("zzz此节并不存在zzz"));
        QTextStream(stdout) << QString::fromUtf8("卡片→小节：关键词空 ") << empty
                            << QString::fromUtf8("　报告在却找不到 ") << missing
                            << QString::fromUtf8("　报告在: ") << (anyReport ? "是" : "否")
                            << QString::fromUtf8("　乱词命中: ") << (bogus ? "是(错)" : "否")
                            << (detail.isEmpty() ? QString() : QStringLiteral("　[") + detail + QLatin1Char(']'))
                            << "\n";
        return (empty == 0 && missing == 0 && !bogus && deferredOk && onDemandOk) ? 0 : 2;
    }

    /* H12 口径探针：红黄线阈值抽成一份口径（速查数据/rules/dashboard_thresholds.json）。
       断言四件事：① 默认配置在位且值就是文档那组；② 值自洽（黄下限 < 红下限、都 ≥1）；
       ③ 换成另一份配置（临时文件 + TOOLBOX_THRESHOLDS）后程序读到的确实跟着变（错例：配置真在起作用）；
       ④ 清掉环境变量后又回落到仓库默认配置（反例：不会一直粘着临时那份）。 */
    if (args.contains(QStringLiteral("--thresholds-probe"))) {
        QTextStream out(stdout);
        const QString tdir = ToolboxWindow::toolsDirPath();
        const QString defPath = QDir(tdir).filePath(
            QStringLiteral("速查数据/rules/dashboard_thresholds.json"));
        QFile df(defPath);
        const bool defOk = df.open(QIODevice::ReadOnly);
        if (defOk)
            df.close();

        qunsetenv("TOOLBOX_THRESHOLDS");          /* 先确保读的是仓库默认那份 */
        DashboardCanvas::resetThresholdsForTest();
        const int pRed = DashboardCanvas::redMinFor(QStringLiteral("pin"));
        const int lRed = DashboardCanvas::redMinFor(QStringLiteral("lora"));
        const int cRed = DashboardCanvas::cacheRedSeconds();
        const int cYel = DashboardCanvas::cacheYellowSeconds();
        const bool defaultsOk = (pRed == 5 && lRed == 3 && cRed == 600 && cYel == 1800);

        bool sane = true;
        for (const char *k : {"pin", "enc", "lora", "protocol"}) {
            const QString kk = QString::fromLatin1(k);
            sane = sane && DashboardCanvas::yellowMinFor(kk) >= 1
                        && DashboardCanvas::yellowMinFor(kk) < DashboardCanvas::redMinFor(kk);
        }

        const QString tmpDir = QDir(QDir::tempPath())
                                   .filePath(QStringLiteral("toolbox_thr_probe"));
        QDir().mkpath(tmpDir);
        const QString tmpCfg = QDir(tmpDir).filePath(QStringLiteral("thr.json"));
        {
            QFile w(tmpCfg);
            if (w.open(QIODevice::WriteOnly))
                w.write("{\"version\":1,"
                        "\"pin_conflicts\":{\"yellow_min\":1,\"red_min\":5},"
                        "\"enc_violations\":{\"yellow_min\":1,\"red_min\":5},"
                        "\"lora_issues\":{\"yellow_min\":1,\"red_min\":2},"
                        "\"protocol_issues\":{\"yellow_min\":1,\"red_min\":3},"
                        "\"cache\":{\"red_below_seconds\":600,\"yellow_below_seconds\":1800}}");
        }
        qputenv("TOOLBOX_THRESHOLDS", tmpCfg.toLocal8Bit());
        DashboardCanvas::resetThresholdsForTest();
        const bool overrideOk = (DashboardCanvas::redMinFor(QStringLiteral("lora")) == 2
                                 && DashboardCanvas::thresholdsSource() == tmpCfg);

        qunsetenv("TOOLBOX_THRESHOLDS");
        DashboardCanvas::resetThresholdsForTest();
        const bool fallbackOk = (DashboardCanvas::redMinFor(QStringLiteral("lora")) == 3);

        out << QString::fromUtf8("阈值口径：默认配置 ")
            << (defOk ? QStringLiteral("在") : QStringLiteral("缺"))
            << QString::fromUtf8("　默认值 ")
            << (defaultsOk ? QStringLiteral("对") : QStringLiteral("错"))
            << QString::fromUtf8("（pin红=%1 lora红=%2 缓存红=%3s 黄=%4s）").arg(pRed).arg(lRed).arg(cRed).arg(cYel)
            << QString::fromUtf8("　自洽 ") << (sane ? QStringLiteral("是") : QStringLiteral("否"))
            << QString::fromUtf8("　改配置后跟随 ") << (overrideOk ? QStringLiteral("是") : QStringLiteral("否"))
            << QString::fromUtf8("　清环境变量后回落 ") << (fallbackOk ? QStringLiteral("是") : QStringLiteral("否"))
            << "\n";
        out.flush();
        return (defOk && defaultsOk && sane && overrideOk && fallbackOk) ? 0 : 2;
    }

    /* F2 量测出口：把界面上"控件高度 / 字号 / 页边距与间距 / 滚动条宽度 / 文字是否被裁"
       按**真实像素**量一遍（逐页切过去量，隐藏页也算），把不一致的地方列出来。
       用途：① 驱动 F2 的统一整改；② 当作长期可复跑的"界面规范"检查（值一跑偏就能看见）。 */
    if (args.contains(QStringLiteral("--ui-audit"))) {
        ToolboxWindow w(nullptr, true);
        w.resize(1200, 900);
        w.show();
        QApplication::processEvents();

        QList<int> btnH, btnPt, tabH, sbW, listH, marginX;
        QList<int> pageMinW, pageMinH;    /* 各页 minimumSizeHint —— 决定主窗口能缩到多小 */
        QStringList clipList, pages, oversize;
        int clipped = 0;
        int sbHidden = 0;                 /* 隐藏的滚动条数（不算入"滚动条宽度"口径） */

        auto measure = [&](QWidget *pg) {
            if (!pg)
                return;
            for (QPushButton *b : pg->findChildren<QPushButton *>()) {
                if (b->text().isEmpty())
                    continue;
                btnH << b->height();
                btnPt << b->fontInfo().pointSize();
                const int need = b->fontMetrics().horizontalAdvance(b->text()) + 18;
                if (need > b->width()) {
                    ++clipped;
                    if (clipList.size() < 6)
                        clipList << QStringLiteral("「%1」需%2/实际%3").arg(b->text()).arg(need).arg(b->width());
                }
            }
            for (QTabWidget *t : pg->findChildren<QTabWidget *>())
                tabH << t->tabBar()->height();
            /* 滚动条宽度：只量**看得见**的（布局已参与、就是用户实际看到的那条）。
               隐藏的（内容没溢出）量出来是 QWidget 默认 100×30 或样式默认 14，都不是
               "界面上的滚动条"，算进来只会污染口径。 */
            for (QScrollArea *sa : pg->findChildren<QScrollArea *>()) {
                QScrollBar *sb = sa->verticalScrollBar();
                if (sb->isVisible())
                    sbW << sb->width();
                else
                    ++sbHidden;
            }
            for (QListWidget *l : pg->findChildren<QListWidget *>()) {
                QScrollBar *sb = l->verticalScrollBar();
                if (sb->isVisible())
                    sbW << sb->width();
                else
                    ++sbHidden;
                listH << l->height();
            }
            if (pg->layout()) {
                const QMargins m = pg->layout()->contentsMargins();
                marginX << m.left();
                if (pages.size() < 20)
                    pages << QStringLiteral("%1: m%2/%3 s%4")
                                 .arg(QString::fromLatin1(pg->metaObject()->className()))
                                 .arg(m.left()).arg(m.top()).arg(pg->layout()->spacing());
            }
        };
        auto dist = [](const QList<int> &v) {
            QMap<int, int> m;
            for (int x : v) ++m[x];
            QStringList parts;
            for (auto it = m.constBegin(); it != m.constEnd(); ++it)
                parts << QStringLiteral("%1px×%2").arg(it.key()).arg(it.value());
            return parts.isEmpty() ? QStringLiteral("（无）") : parts.join(QStringLiteral(" / "));
        };
        /* F2：沿布局树往下走（含子布局、标签页里的页），收"每个控件的最小尺寸"。
           用途：指出"是哪个控件把这一页的最小尺寸顶上去的"，好按图索骥地收。 */
        std::function<void(QLayout *, QList<QPair<int, QString>> &, int)> walk =
            [&](QLayout *l, QList<QPair<int, QString>> &acc, int depth) {
                if (!l || depth > 3)
                    return;
                for (int i = 0; i < l->count(); ++i) {
                    QLayoutItem *it = l->itemAt(i);
                    if (!it)
                        continue;
                    if (QWidget *cw = it->widget()) {
                        const QSize k = cw->minimumSizeHint();
                        acc << qMakePair(k.width(),
                                         QStringLiteral("%1(%2×%3)")
                                             .arg(QString::fromLatin1(cw->metaObject()->className()))
                                             .arg(k.width()).arg(k.height()));
                        walk(cw->layout(), acc, depth + 1);
                    } else if (QLayout *sl = it->layout()) {
                        walk(sl, acc, depth + 1);
                    }
                }
            };

        /* 切页走**侧栏**（与 --gui-smoke 同一条已验证安全的路径），不要直接 setCurrentIndex：
           实测直接切主堆栈会在退出阶段报堆损坏（0xC0000374），走侧栏则干净，
           而且侧栏切换还会触发"首次进手册页装载数据"的真实路径，量到的更接近实际使用。 */
        QListWidget *side = findSidebar(&w);
        if (side) {
            /* 找主堆栈：页数 == 侧栏里"可选行"数（分组头不算），比靠父子关系猜更稳。 */
            int rows = 0;
            for (int r = 0; r < side->count(); ++r)
                if (side->item(r)->flags() != Qt::NoItemFlags)
                    ++rows;
            QStackedWidget *pgStack = nullptr;
            for (QStackedWidget *s : w.findChildren<QStackedWidget *>())
                if (s->count() == rows) {
                    pgStack = s;
                    break;
                }
            for (int r = 0; r < side->count(); ++r) {
                if (side->item(r)->flags() == Qt::NoItemFlags)   /* 分组头，不可选 */
                    continue;
                /* 手册页也会量（它已不再导致崩溃——根因是"取数进程还在跑就退出"，已在
                   OverviewPage 析构里收干净）。保留开关只是为了对比诊断用。 */
                if (side->item(r)->text() == QStringLiteral("单片机手册")
                    && !args.contains(QStringLiteral("--with-lookup")))
                    continue;
                side->setCurrentRow(r);
                QApplication::processEvents();
                QWidget *pg = pgStack ? pgStack->currentWidget() : nullptr;
                measure(pg);
                /* F2「图形界面大小」：某页 minimumSizeHint 越大，主窗口就越缩不小。
                   门槛定在 740×654 —— 加上侧栏 220、边距/间距约 24，主窗口最小尺寸约
                   984×708，正好在 1024×768 这类小屏上放得下（高 912 那种就是这里翻车的）。
                   超标的页单独点名，并列出"是哪个控件把最小宽度顶上去的"。 */
                if (pg) {
                    const QSize mn = pg->minimumSizeHint();
                    pageMinW << mn.width();
                    pageMinH << mn.height();
                    if ((mn.width() > 740 || mn.height() > 654) && oversize.size() < 8) {
                        QList<QPair<int, QString>> found;
                        walk(pg->layout(), found, 0);
                        std::sort(found.begin(), found.end(),
                                  [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                                      return a.first > b.first;
                                  });
                        QStringList kids;
                        for (int i = 0; i < found.size() && kids.size() < 6; ++i)
                            kids << found.at(i).second;
                        oversize << QStringLiteral("%1 %2×%3 → 最宽的几个: %4")
                                        .arg(side->item(r)->text())
                                        .arg(mn.width()).arg(mn.height())
                                        .arg(kids.join(QStringLiteral(", ")));
                    }
                }
            }
            side->setCurrentRow(0);
        } else {
            measure(w.findChild<QStackedWidget *>() ? w.findChild<QStackedWidget *>()->currentWidget()
                                                   : nullptr);
        }

        QTextStream out(stdout);
        out << QString::fromUtf8("量到页面: ") << pages.size() << QString::fromUtf8(" 页\n");
        out << QString::fromUtf8("按钮高度: ") << dist(btnH) << "\n";
        out << QString::fromUtf8("按钮字号: ") << dist(btnPt) << "\n";
        out << QString::fromUtf8("标签栏高度: ") << dist(tabH) << "\n";
        out << QString::fromUtf8("滚动条宽度: ") << dist(sbW)
            << QString::fromUtf8("（另有 ") << sbHidden << QString::fromUtf8(" 条隐藏未计）\n");
        out << QString::fromUtf8("页左外边距: ") << dist(marginX) << "\n";
        out << QString::fromUtf8("窗口最小尺寸: ") << w.minimumSizeHint().width() << "×"
            << w.minimumSizeHint().height() << "\n";
        out << QString::fromUtf8("页最小尺寸: 宽 ") << dist(pageMinW)
            << QString::fromUtf8("　高 ") << dist(pageMinH) << "\n";
        out << QString::fromUtf8("文字可能被裁: ") << clipped
            << (clipList.isEmpty() ? QString() : QStringLiteral("　") + clipList.join(QStringLiteral("；")))
            << "\n";
        out << QString::fromUtf8("各页边距/间距: ") << pages.join(QStringLiteral("　")) << "\n";

        /* F2「UI 规范」硬判定：同一类尺寸必须收成一种，且不许有文字被裁。
           这是可反复跑的"界面标准"检查 —— 哪天谁改了 QSS 把按钮高度弄成两种，这里立刻红。 */
        auto kinds = [](const QList<int> &v) {
            QSet<int> s;
            for (int x : v) s.insert(x);
            return s.size();
        };
        const bool hOk    = kinds(btnH) == 1;      /* 按钮高度只许一种 */
        const bool ptOk   = kinds(btnPt) == 1;     /* 按钮字号只许一种 */
        const bool tabOk  = kinds(tabH) <= 1;      /* 标签栏高度只许一种（无标签栏时为空） */
        const bool sbOk   = kinds(sbW) == 1;       /* 滚动条宽度只许一种 */
        const bool mOk    = kinds(marginX) == 1;   /* 页左外边距只许一种 */
        const bool clipOk = clipped == 0;          /* 按钮文字不许被裁 */
        const bool sizeOk = oversize.isEmpty();    /* 页最小尺寸不许超过 740×654 */
        const bool ok = hOk && ptOk && tabOk && sbOk && mOk && clipOk && sizeOk;
        out << QString::fromUtf8("页最小尺寸偏大(宽>740 或 高>654 → 主窗口在 1024×768 上放不下): ")
            << oversize.size()
            << (oversize.isEmpty() ? QString() : QStringLiteral("　") + oversize.join(QStringLiteral("；")))
            << "\n";
        out << QString::fromUtf8("UI 规范: 按钮高度一种 ") << (hOk ? "是" : "否")
            << QString::fromUtf8("　按钮字号一种 ") << (ptOk ? "是" : "否")
            << QString::fromUtf8("　标签栏一种 ") << (tabOk ? "是" : "否")
            << QString::fromUtf8("　滚动条一种 ") << (sbOk ? "是" : "否")
            << QString::fromUtf8("　页左外边距一种 ") << (mOk ? "是" : "否")
            << QString::fromUtf8("　无裁字 ") << (clipOk ? "是" : "否")
            << QString::fromUtf8("　页最小尺寸达标 ") << (sizeOk ? "是" : "否")
            << QString::fromUtf8("　→ ") << (ok ? QStringLiteral("通过") : QStringLiteral("不达标")) << "\n";
        out.flush();
        /* 走事件循环退出（与 --gui-smoke 同一条干净路径）。实测：先 show 再直接 return，
           Qt 退出阶段会报堆损坏（0xC0000374）；从这里退出则干净。 */
        w.hide();
        QTimer::singleShot(0, qApp, []() { QApplication::quit(); });
        app.exec();                       /* 只为干净退出；退出码按上面的 UI 规范判定给 */
        return ok ? 0 : 2;
    }

    /* F1 量测出口：把"用起来要等多久"按毫秒量一遍 —— 冷启动（构造全部页面）、
       窗口显示 + 首帧、逐页首访（含首次进「单片机手册」要读 4MB 数据）。
       用途：① 驱动 F1 的性能优化（先量再改）；② 长期可复跑的"启动/切页不许卡"检查。 */
    if (args.contains(QStringLiteral("--perf-probe"))) {
        /* 先单独量一次解释器探测：首次是"真起一个 python 探一次"，第二次应命中缓存（≈0ms）。
           放在页面量测之前，是为了让后面的"逐页首访"体现**缓存生效后的常态**；
           真实用户第一次取数/第一次运行仍会付这一次（约几百毫秒），那是"取数"本身的一部分。 */
        QElapsedTimer dt;
        dt.start();
        const QStringList py1 = PyFind::detect();
        const qint64 msProbe1 = dt.elapsed();
        dt.restart();
        PyFind::detect();
        const qint64 msProbe2 = dt.elapsed();

        QElapsedTimer t;
        t.start();
        ToolboxWindow w(nullptr, true);
        const qint64 tCtor = t.elapsed();      /* 构造全部页面（含起手助手等）的耗时 */
        w.resize(1200, 900);
        w.show();
        QApplication::processEvents();
        QApplication::processEvents();          /* 一次布局、一次首帧 */
        const qint64 tShow = t.elapsed();

        QListWidget *side = findSidebar(&w);

        QList<QPair<qint64, QString>> pageMs;   /* (耗时ms, 页名) */
        qint64 total = 0;
        if (side) {
            for (int r = 0; r < side->count(); ++r) {
                if (side->item(r)->flags() == Qt::NoItemFlags)   /* 分组头 */
                    continue;
                QElapsedTimer pt;
                pt.start();
                side->setCurrentRow(r);            /* 首访：手册页会在这里读 4MB 数据 */
                QApplication::processEvents();
                const qint64 ms = pt.elapsed();
                total += ms;
                pageMs << qMakePair(ms, side->item(r)->text());
            }
            side->setCurrentRow(0);
        }
        std::sort(pageMs.begin(), pageMs.end(),
                  [](const QPair<qint64, QString> &a, const QPair<qint64, QString> &b) {
                      return a.first > b.first;
                  });

        /* F1 判定线：冷启动 > 1500ms、或单页首访 > 800ms 就算"卡"。
           这两条是"用户能明显感觉到停顿"的量级，先按这个卡住，后续优化再往下压。 */
        const qint64 worst = pageMs.isEmpty() ? 0 : pageMs.first().first;
        const bool ok = tCtor <= 1500 && worst <= 800;

        QTextStream out(stdout);
        out << QString::fromUtf8("解释器探测: 首次 ") << msProbe1 << QString::fromUtf8(" ms（")
            << (py1.isEmpty() ? QStringLiteral("没找到") : py1.value(0))
            << QString::fromUtf8("）　再次 ") << msProbe2 << QString::fromUtf8(" ms（缓存命中）\n");
        out << QString::fromUtf8("冷启动(构造全部页面): ") << tCtor << QString::fromUtf8(" ms\n");
        out << QString::fromUtf8("显示+首帧: ") << (tShow - tCtor) << QString::fromUtf8(" ms（累计 ")
            << tShow << QString::fromUtf8(" ms）\n");
        out << QString::fromUtf8("切遍 ") << pageMs.size() << QString::fromUtf8(" 页合计: ")
            << total << QString::fromUtf8(" ms\n");
        out << QString::fromUtf8("最慢的页: ");
        for (int i = 0; i < pageMs.size() && i < 6; ++i)
            out << QStringLiteral("%1 %2ms　").arg(pageMs.at(i).second).arg(pageMs.at(i).first);
        out << QString::fromUtf8("\n全部逐页: ");
        QList<QPair<qint64, QString>> all = pageMs;
        std::sort(all.begin(), all.end(),
                  [](const QPair<qint64, QString> &a, const QPair<qint64, QString> &b) {
                      return a.second < b.second;
                  });
        for (const auto &p : all)
            out << QStringLiteral("%1=%2ms　").arg(p.second).arg(p.first);
        out << "\n";
        out << QString::fromUtf8("F1 判定: 冷启动≤1500 ") << (tCtor <= 1500 ? "是" : "否")
            << QString::fromUtf8("　单页首访≤800 ") << (worst <= 800 ? "是" : "否")
            << QString::fromUtf8("　→ ") << (ok ? QStringLiteral("通过") : QStringLiteral("偏慢"))
            << "\n";
        out.flush();

        w.hide();
        QTimer::singleShot(0, qApp, []() { QApplication::quit(); });
        app.exec();
        return ok ? 0 : 2;
    }

    /* H7 变异自测：在**原脚本同目录**造一份临时副本，故意改坏一处判定，跑它自己的
       --selftest，断言"自检**必须变红**" —— 守住"自检真的在测那一条"。
       为什么放同目录：脚本要靠"自身位置 / 向上找仓库根 / 读同目录的数据文件"来工作，
       挪到 %TEMP% 那些判据就全变了（反例会假红）。副本文件名带 `_mut_`，跑完无论成败都删。
       每处都先跑一遍**没改坏**的原脚本作对照：对照不绿就说明是本环境的问题，不是"自检没测到"。 */
    if (args.contains(QStringLiteral("--mutation-selftest"))) {
        QTextStream out(stdout);
        const QString tdir = ToolboxWindow::toolsDirPath();
        const QStringList py = PyFind::detect();
        if (tdir.isEmpty() || py.isEmpty()) {
            out << QString::fromUtf8("FAIL 工具箱目录或 Python 不可用\n");
            return 2;
        }
        struct Mut { const char *rel; const char *find; const char *repl; const char *what; };
        const Mut muts[] = {
            {"检查/引脚规划/pin_planner.py",
             "owner.items() if len(mods) > 1",
             "owner.items() if len(mods) > 99",
             "DMA「同一条流被多模块共用」的判定"},
            {"检查/RTOS检查/rtos_check.py",
             "if api in ISR_UNSAFE_CRITICAL or api in BLOCKING_API:\n        return '严重'",
             "if api in ISR_UNSAFE_CRITICAL or api in BLOCKING_API:\n        return '注意'",
             "ISR 里调用阻塞接口的判级（严重→注意）"},
            {"检查/断网缓存/cache_check.py",
             "hold_sec = cap_slots * period_ms / 1000.0 if period_ms > 0 else 0",
             "hold_sec = cap_slots * period_ms / 1.0 if period_ms > 0 else 0",
             "断网「可扛时长」的换算（漏掉 /1000）"},
            {"检查/编码守卫/encoding_guard.py",
             "return 'gbk' if os.path.basename(path).lower() == 'main.c' else 'utf-8'",
             "return 'utf-8'",
             "main.c 的编码口径（把 GBK 当成 UTF-8）"},
        };
        auto runSelftest = [&](const QString &script) -> int {
            QProcess p;
            p.setWorkingDirectory(QFileInfo(script).absolutePath());
            QStringList a = py.mid(1);
            a << script << QStringLiteral("--selftest");
            p.start(py.value(0), a);
            if (!p.waitForStarted(5000))
                return -1;
            if (!p.waitForFinished(120000)) {
                p.kill();
                return -1;
            }
            return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
        };
        const int total = int(sizeof(muts) / sizeof(muts[0]));
        int bad = 0;
        for (int i = 0; i < total; ++i) {
            const Mut &m = muts[i];
            const QString src = QDir(tdir).filePath(QString::fromUtf8(m.rel));
            QFile f(src);
            if (!f.open(QIODevice::ReadOnly)) {
                out << QString::fromUtf8("FAIL 读不到 ") << QString::fromUtf8(m.rel) << "\n";
                ++bad;
                continue;
            }
            QString code = QString::fromUtf8(f.readAll());
            f.close();
            const QString find = QString::fromUtf8(m.find);
            if (!code.contains(find)) {
                out << QString::fromUtf8("FAIL 找不到要改坏的锚点（脚本改了？）：")
                    << QString::fromUtf8(m.what) << "\n";
                ++bad;
                continue;
            }
            /* 对照：没改坏的原脚本自检必须是绿的，否则说明是环境问题、不是"自检没测到" */
            const int base = runSelftest(src);
            if (base != 0) {
                out << QString::fromUtf8("FAIL 对照失败（原脚本自检就不是绿的，退出码 ")
                    << base << QString::fromUtf8("）：") << QString::fromUtf8(m.what) << "\n";
                ++bad;
                continue;
            }
            code.replace(find, QString::fromUtf8(m.repl));
            const QString mut = QFileInfo(src).absolutePath() + QStringLiteral("/_mut_")
                                + QFileInfo(src).completeBaseName() + QStringLiteral(".py");
            {
                QFile w(mut);
                if (!w.open(QIODevice::WriteOnly)) {
                    out << QString::fromUtf8("FAIL 写不出临时副本 ") << mut << "\n";
                    ++bad;
                    continue;
                }
                w.write(code.toUtf8());
            }
            const int ex = runSelftest(mut);
            QFile::remove(mut);
            const bool caught = ex > 0;          /* 自检必须变红（退出码非 0） */
            out << (caught ? QString::fromUtf8("PASS ") : QString::fromUtf8("FAIL "))
                << QString::fromUtf8("改坏「") << QString::fromUtf8(m.what)
                << QString::fromUtf8("」→ 自检变红：")
                << (caught ? QString::fromUtf8("是（自检真的在测这一条）")
                           : QString::fromUtf8("否（自检没测到这条判定！）"))
                << QString::fromUtf8("（退出码 ") << ex << QStringLiteral("）\n");
            if (!caught)
                ++bad;
        }
        out << QString::fromUtf8("变异自测：共 ") << total << QString::fromUtf8(" 处，未变红 ")
            << bad << QString::fromUtf8(" 处 → ")
            << (bad ? QStringLiteral("不通过") : QStringLiteral("通过")) << "\n";
        out.flush();
        return bad ? 2 : 0;
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
