#include "lookupcli.h"

#include "apppaths.h"
#include "mainwindow.h"
#include "searchengine.h"

#include <QFile>
#include <QFileInfo>
#include <QStringConverter>
#include <QTextStream>

/* 从原 单片机手册独立程序（main.cpp）平移过来的命令行出口，逻辑一字未改，
   只把入口改成函数、把 --gui-smoke 摘出去（并入工具箱后由 Toolbox 的冒烟统一跑）。 */
int runLookupCli(const QStringList &args, MainWindow *w)
{
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);

    if (args.contains(QStringLiteral("--selftest")) || args.contains(QStringLiteral("--where"))) {
        QString err;
        const bool ok = w->loadData(&err);
        if (!ok) {
            out << QString::fromUtf8("装载失败：") << err << "\n";
            return 1;
        }
        const Dataset &d = w->dataset();
        out << QString::fromUtf8("数据文件 : ") << AppPaths::datasetPath() << "\n";
        out << QString::fromUtf8("条目总数 : ") << d.size() << "\n";

        if (args.contains(QStringLiteral("--where")))
            return 0;

        int fail = 0;
        auto chk = [&out, &fail](bool cond, const QString &what) {
            out << (cond ? QString::fromUtf8("PASS ") : QString::fromUtf8("FAIL ")) << what << "\n";
            if (!cond)
                ++fail;
        };
        chk(d.size() > 900, QString::fromUtf8("条目总数 > 900（实际 %1）").arg(d.size()));
        chk(d.countType(QStringLiteral("flow")) == 39,
            QString::fromUtf8("流程卡 = 39（实际 %1）").arg(d.countType(QStringLiteral("flow"))));
        /* 引导向导：要排除手册阶段卡（它们 _t 也是 guide，只是带 _b 工程标签） */
        int guideOnly = 0;
        for (const Card &c : d.cards())
            if (c.isGuide() && c.b.isEmpty())
                ++guideOnly;
        chk(guideOnly == 30,
            QString::fromUtf8("引导向导 = 30（实际 %1）").arg(guideOnly));
        /* 27 → 28：网页源给板2 补了一张「阶段 9｜双通道并行」（板2 由 0~8 变 0~9），
           dataset 重导后手册阶段卡 27（板1 10 + 板2 9 + Qt 8）→ 28（板1 10 + 板2 10 + Qt 8）。 */
        chk(d.countManu() == 28,
            QString::fromUtf8("手册阶段卡 = 28（实际 %1）").arg(d.countManu()));

        int noHay = 0;
        for (const Card &c : d.cards())
            if (c.hay.trimmed().isEmpty())
                ++noHay;
        chk(noHay == 0, QString::fromUtf8("每条都有检索语料（空 %1 条）").arg(noHay));

        chk(!d.search().isEmpty(), QString::fromUtf8("搜索用表已随数据导出"));
        chk(d.proj().size() == 3,
            QString::fromUtf8("项目引导工程数 = 3（实际 %1）").arg(d.proj().size()));

        const int refN   = d.indicesOfSection(int(Card::Ref)).size();
        const int guideN = d.indicesOfSection(int(Card::Guide)).size();
        const int buildN = d.indicesOfSection(int(Card::Build)).size();
        chk(refN + guideN + buildN == d.size(),
            QString::fromUtf8("三板块条目数相加 = 总数（%1 + %2 + %3）").arg(refN).arg(guideN).arg(buildN));
        chk(d.indexOfName(QStringLiteral("NVIC_Init")) >= 0, QString::fromUtf8("按名字反查条目可用"));

        /* 落地页结构自检（桌面版与网页/手机同一份清单：3 路径卡 + 10 主题 + 若干模块 chip）。
           这里只验"清单在不在、锚点格式对不对"；至于"什么时候显示落地页"由 refreshList 决定，
           靠 GUI 冒烟遍历覆盖。锚点刻意用纯 ASCII 下标（path:N/q:N/mod:N）——
           中文塞进 href 会被 QUrl 百分号编码，回来对不上。 */
        {
            const QString land = w->landingHtml();
            chk(land.contains(QString::fromUtf8("从这里开始")),
                QString::fromUtf8("落地页含标题「从这里开始」"));
            const int pathN = land.count(QStringLiteral("href='path:"));
            chk(pathN == 3, QString::fromUtf8("路径卡 = 3（实际 %1）").arg(pathN));
            const int qN = land.count(QStringLiteral("href='q:"));
            chk(qN == 10, QString::fromUtf8("热门主题 chip = 10（实际 %1）").arg(qN));
            const int modN = land.count(QStringLiteral("href='mod:"));
            chk(modN > 0 && modN <= 12,
                QString::fromUtf8("常用模块 chip = 1..12（实际 %1）").arg(modN));
            chk(land.contains(QString::fromUtf8("常用模块"))
                    && land.contains(QString::fromUtf8("热门主题")),
                QString::fromUtf8("落地页含「热门主题 / 常用模块」两个分组标题"));
        }

        out << QString::fromUtf8("\n失败项 ") << fail << "\n";
        /* 注意：这里**不要**提前 return —— 严格审查要求一次跑完，
           否则前面一项失败会把后面的搜索回归、基线比对整段吞掉（踩过）。 */

        /* 搜索回归：条数与首位要和网页版基线一致（基线取自 _build_regress.js 的固定用例）。
           任何差异都说明移植走偏了，必须逐条解释，不能默默放过。 */
        {
            struct Case { const char *q; int n; const char *first; };
            const Case cases[] = {
                { "打开DMA中断", 19, "DMA_GetITStatus" },
                { "配置串口中断", 13, "USART_ITConfig" },
                { "卡尔曼滤波",    8, "卡尔曼滤波一维实现流程（从抖动读数到可信值）" },
                { "看门狗喂狗",   17, "看门狗配置表（IWDG / WWDG 超时，含常用值）" },
                { "空闲中断",     92, "NVIC_Init" },
                { "DS18B20",       3, "测距与温度传感器速查（HC-SR04 / VL53L0X / DS18B20 / DHT11）" },
            };
            int bad = 0;
            if (!w->engine()) {
                out << QString::fromUtf8("FAIL 搜索引擎未就绪\n");
                ++fail;
            } else {
                for (const Case &c : cases) {
                    const SearchEngine::Result r =
                        w->engine()->run(QString::fromUtf8(c.q), int(Card::Ref), QString());
                    const QString first = r.order.isEmpty()
                        ? QString()
                        : w->dataset().at(r.order.first()).n;
                    const bool nOk = (r.order.size() == c.n);
                    const bool fOk = (first == QString::fromUtf8(c.first));
                    if (!nOk || !fOk)
                        ++bad;
                    out << ((nOk && fOk) ? QString::fromUtf8("PASS ") : QString::fromUtf8("FAIL "))
                        << QString::fromUtf8("搜索[%1] %2 条（期望 %3），首位 = %4%5\n")
                               .arg(QString::fromUtf8(c.q))
                               .arg(r.order.size()).arg(c.n).arg(first.isEmpty() ? QStringLiteral("无") : first,
                                    (nOk && fOk) ? QString() : QString::fromUtf8("  <== 与基线不符"));
                }
                fail += bad;
            }
            /* 搜索基线回归：与 data/search_baseline.txt 逐行比对。
           基线由 tools/export_json.js --oracle ... 生成（网页引擎当标准答案）。
           只比"条数 + 首位"是不够的——实测抓到过"表类卡片分数偏移"，条数与首位都对，只有尾部顺序错。
           所以这里比**整行**（含前五）。 */
        {
            const QString bp = QFileInfo(AppPaths::datasetPath())
                                   .absolutePath() + QStringLiteral("/search_baseline.txt");
            QFile bf(bp);
            if (!bf.open(QIODevice::ReadOnly)) {
                out << QString::fromUtf8("FAIL 读不到搜索基线：") << bp << "\n";
                ++fail;
            } else {
                int n = 0, bad = 0;
                while (!bf.atEnd()) {
                    const QString line = QString::fromUtf8(bf.readLine()).trimmed();
                    if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                        continue;
                    const int rb  = line.indexOf(QLatin1Char(']'));
                    const int bar = line.indexOf(QStringLiteral(" | "));
                    if (rb < 0 || bar < rb)
                        continue;
                    const QString sec = line.mid(1, rb - 1);
                    const QString q   = line.mid(rb + 1, bar - rb - 1).trimmed();
                    const int isec = (sec == QStringLiteral("guide")) ? int(Card::Guide)
                                   : (sec == QStringLiteral("build")) ? int(Card::Build)
                                                                     : int(Card::Ref);
                    const SearchEngine::Result r = w->engine()->run(q, isec, QString());
                    QStringList names;
                    for (int i : r.order)
                        names << w->dataset().at(i).n;
                    const QString mine = QStringLiteral("[%1] %2 | %3 | %4 | %5")
                        .arg(sec, q).arg(names.size())
                        .arg(names.isEmpty() ? QStringLiteral("无") : names.first(),
                             QStringList(names.mid(0, 5)).join(QStringLiteral(" / ")));
                    ++n;
                    if (mine != line) {
                        ++bad;
                        out << QString::fromUtf8("FAIL 基线不一致：") << q << "\n"
                            << QString::fromUtf8("     期望 ") << line << "\n"
                            << QString::fromUtf8("     实际 ") << mine << "\n";
                    }
                }
                out << ((bad == 0) ? QString::fromUtf8("PASS ") : QString::fromUtf8("FAIL "))
                    << QString::fromUtf8("搜索基线 %1 例逐行一致（差异 %2）\n").arg(n).arg(bad);
                fail += bad;
            }
        }

        out << QString::fromUtf8("\n失败项 ") << fail << "\n";
        return fail;
    }
    }

    /* ---- 搜索回归：与网页引擎（oracle）逐条比对 ----
       用法：--search [--sec ref|guide] [词...]
       输出格式与 tools/export_json.js --oracle 一致，便于直接 diff：
         [sec] 词 | 条数 | 首位 | 前五（用 / 分隔）                      */
    const int si = args.indexOf(QStringLiteral("--search"));
    if (si >= 0) {
        QString err;
        if (!w->loadData(&err)) {
            out << QString::fromUtf8("装载失败：") << err << "\n";
            return 1;
        }
        int sec = int(Card::Ref);
        QStringList qs;
        for (int i = si + 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--sec") && i + 1 < args.size()) {
                const QString s = args.at(++i).toLower();
                sec = (s == QStringLiteral("guide")) ? int(Card::Guide)
                    : (s == QStringLiteral("build")) ? int(Card::Build) : int(Card::Ref);
            } else {
                qs << args.at(i);
            }
        }
        if (qs.isEmpty())
            qs << QStringLiteral("打开DMA中断") << QStringLiteral("配置串口中断")
               << QStringLiteral("卡尔曼滤波") << QStringLiteral("看门狗喂狗")
               << QStringLiteral("空闲中断") << QStringLiteral("DS18B20");
        const QString secName = (sec == int(Card::Guide)) ? QStringLiteral("guide")
                              : (sec == int(Card::Build)) ? QStringLiteral("build")
                                                          : QStringLiteral("ref");
        for (const QString &q : qs) {
            const SearchEngine::Result r = w->engine()->run(q, sec, QString());
            QStringList names;
            for (int i : r.order)
                names << w->dataset().at(i).n;
            out << QStringLiteral("[%1] %2 | %3 | %4 | %5\n")
                       .arg(secName, q).arg(names.size())
                       .arg(names.isEmpty() ? QStringLiteral("无") : names.first())
                       .arg(QStringList(names.mid(0, 5)).join(QStringLiteral(" / ")));
        }
        return 0;
    }

    /* ---- 打分明细（排查与网页 oracle 的差异）----
       用法：--explain [--sec ref|guide] 词                              */
    const int xi = args.indexOf(QStringLiteral("--explain"));
    if (xi >= 0) {
        QString err;
        if (!w->loadData(&err)) {
            out << QString::fromUtf8("装载失败：") << err << "\n";
            return 1;
        }
        int sec = int(Card::Ref);
        QString q;
        for (int i = xi + 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--sec") && i + 1 < args.size()) {
                const QString s = args.at(++i).toLower();
                sec = (s == QStringLiteral("guide")) ? int(Card::Guide)
                    : (s == QStringLiteral("build")) ? int(Card::Build) : int(Card::Ref);
            } else {
                q = args.at(i);
            }
        }
        out << QString::fromUtf8("词=") << q << QString::fromUtf8("  板块=")
            << (sec == int(Card::Guide) ? QStringLiteral("guide")
               : sec == int(Card::Build) ? QStringLiteral("build") : QStringLiteral("ref")) << "\n";
        const QVector<SearchEngine::Explain> rows = w->engine()->explain(q, sec, QString());
        for (const SearchEngine::Explain &x : rows)
            out << QStringLiteral("%1 | tier=%2 firstPT=%3 cov=%4 req=%5 score=%6 ord=%7 | %8/%9\n")
                       .arg(w->dataset().at(x.index).n)
                       .arg(x.tier).arg(x.firstPT)
                       .arg(x.cov, 0, 'f', 3).arg(x.covReq, 0, 'f', 3)
                       .arg(x.score, 0, 'f', 1).arg(x.ord)
                       .arg(x.t, x.src);
        return 0;
    }

    /* ---- 主题归类逐条输出（与网页 modKeyOf 比对，校 TOPIC_RULES 正则移植）---- */
    if (args.contains(QStringLiteral("--topics"))) {
        QString err;
        if (!w->loadData(&err)) {
            out << QString::fromUtf8("装载失败：") << err << "\n";
            return 1;
        }
        for (const Card &c : w->dataset().cards())
            out << c.n << QLatin1Char('\t') << w->engine()->modKeyOf(c) << QLatin1Char('\n');
        return 0;
    }

    return -1;
}
