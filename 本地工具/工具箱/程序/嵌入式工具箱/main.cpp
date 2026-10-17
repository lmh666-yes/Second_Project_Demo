#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QString>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>

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
        };
        out << QString::fromUtf8("\n── 各检查器自检（--selftest）──\n");
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
                bool ok = false;
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
                        ok = (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0);
                        if (!ok) {
                            /* 失败时把带 FAIL 的那一行摘出来，省得再手跑一遍 */
                            for (const QString &ln :
                                 QString::fromUtf8(p.readAllStandardOutput())
                                     .split(QLatin1Char('\n'))) {
                                if (ln.contains(QStringLiteral("FAIL"))) {
                                    note = ln.trimmed();
                                    break;
                                }
                            }
                        }
                    }
                }
                out << (ok ? QString::fromUtf8("PASS ") : QString::fromUtf8("FAIL "))
                    << QString::fromUtf8(c.name)
                    << (note.isEmpty() ? QString()
                                       : QString::fromUtf8("（%1）").arg(note)) << "\n";
                if (!ok)
                    ++fail;
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

        /* 断言交互区域已登记：健康度卡片 + 两板热力图格，取数成功时必然 > 6。
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
        const QStringList keys{QStringLiteral("pin"), QStringLiteral("rtos"),
                               QStringLiteral("enc"), QStringLiteral("lora"),
                               QStringLiteral("protocol"), QStringLiteral("cache"),
                               QStringLiteral("progress")};
        const bool anyReport = page.srcLinkCount() > 0 || page.docLinkCount() > 0;
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
        return (empty == 0 && missing == 0 && !bogus) ? 0 : 2;
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
