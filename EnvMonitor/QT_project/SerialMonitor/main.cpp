#include "mainwindow.h"

#include "consistencytest.h"
#include "devctl.h"
#include "physrange.h"
#include "qcustomplot.h"
#include "thresholddialog.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QScrollArea>
#include <QSplitter>
#include <QTextStream>
#include <QTimer>

#include <algorithm>

/* --selftest：不开窗口，只跑一致性自检，结果写 自检报告.txt。
 * 落文件而不是打印：Windows GUI 子系统（-subsystem,windows）没有控制台，
 * qDebug/printf 都看不见；写文件也便于附进验收文档。
 * 退出码：0 = 全部用例通过，1 = 有失败（脚本/CI 可直接判）。 */
static int runSelfTest()
{
    /* 帧解析用例 + 右侧统计区的窗口统计用例，一份报告里一起算通过率 */
    QVector<CaseResult> r = ConsistencyTest::runBuiltin();
    r += ConsistencyTest::runStatistics();
    const QString text = ConsistencyTest::toText(
        r, QStringLiteral("Qt 上位机 · 帧解析 + 统计自检（--selftest）"));

    QFile f(QDir::current().filePath(QStringLiteral("自检报告.txt")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        ts << QChar(0xFEFF) << text;
        f.close();
    }
    return ConsistencyTest::allPassed(r) ? 0 : 1;
}

/* --dumpmin：诊断窗口太窄时右侧被裁掉是哪个控件在撑宽度。
 * 窗口 show() 之后布局才有真实的最小尺寸，所以用 singleShot 延后取，
 * 把 minimumSizeHint 最宽的一批控件按宽度倒序写进 窗口尺寸诊断.txt。 */
static int runDumpMin(int feedFrames, int viewSeries)
{
    MainWindow w;
    w.show();
    /* 可选 --feed N / --view N：先灌 N 帧、切到查看量 N 再诊断，用于核对
     * "曲线区到底画了几条折线、折线里有几个点"。不带时数据为空，可见折线报 0。 */
    if (feedFrames > 0) w.feedDemoFrames(feedFrames);
    if (viewSeries >= 0) w.setViewSeries(viewSeries);

    QTimer::singleShot(300, &w, [&w]() {
        QFile f(QDir::current().filePath(QStringLiteral("窗口尺寸诊断.txt")));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qApp->quit();
            return;
        }
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        ts << QChar(0xFEFF);
        ts << QStringLiteral("窗口 minimumSizeHint = %1 x %2\n")
                  .arg(w.minimumSizeHint().width())
                  .arg(w.minimumSizeHint().height());
        ts << QStringLiteral("窗口 minimumSize     = %1 x %2\n")
                  .arg(w.minimumSize().width())
                  .arg(w.minimumSize().height());
        ts << QStringLiteral("窗口实际尺寸          = %1 x %2\n\n")
                  .arg(w.width())
                  .arg(w.height());

        /* 关键：布局最终到底给了各栏多少宽度 */
        if (QScrollArea *sa = w.findChild<QScrollArea *>()) {
            ts << QStringLiteral("滚动区 viewport = %1 x %2，widget = %3 x %4，最小提示 = %5\n")
                      .arg(sa->viewport()->width()).arg(sa->viewport()->height())
                      .arg(sa->widget() ? sa->widget()->width() : -1)
                      .arg(sa->widget() ? sa->widget()->height() : -1)
                      .arg(sa->widget() ? sa->widget()->minimumSizeHint().width() : -1);
        }
        if (QSplitter *sp = w.findChild<QSplitter *>()) {
            const QList<int> sz = sp->sizes();
            ts << QStringLiteral("splitter 实际 = %1 x %2，最小提示宽 = %3，sizes = ")
                      .arg(sp->width()).arg(sp->height())
                      .arg(sp->minimumSizeHint().width());
            for (int v : sz)
                ts << v << ' ';
            ts << '\n';
        }
        const char *names[] = { "leftPanel", "centerPanel", "rightPanel" };
        for (const char *n : names)
            if (QWidget *pw = w.findChild<QWidget *>(QString::fromLatin1(n)))
                ts << QStringLiteral("  %1 宽 = %2\n").arg(QString::fromLatin1(n)).arg(pw->width());

        /* 曲线区（QCustomPlot）与 X 轴条的轴矩形宽度：两者对齐、且给出曲线区
         * 可画折线的净宽度，便于核对"只留一根左轴后曲线区变宽了多少"。 */
        if (QCustomPlot *sp2 = w.findChild<QCustomPlot *>(QStringLiteral("plotEnv"))) {
            ts << QStringLiteral("  plotEnv 控件 = %1 x %2，轴矩形宽 = %3，轴矩形高 = %4\n")
                      .arg(sp2->width()).arg(sp2->height())
                      .arg(sp2->axisRect()->width()).arg(sp2->axisRect()->height());
            int drawn = 0, drawnPts = 0;
            for (int i = 0; i < sp2->plottableCount(); ++i) {
                if (QCPGraph *g = qobject_cast<QCPGraph *>(sp2->plottable(i))) {
                    if (!g->visible()) continue;
                    ++drawn;
                    drawnPts += g->dataCount();
                }
            }
            ts << QStringLiteral("  plotEnv 可见折线 = %1 条，合计 %2 点（期望 1 条）\n")
                      .arg(drawn).arg(drawnPts);
            if (QCPGraph *g = qobject_cast<QCPGraph *>(sp2->plottable(0))) {
                double k0 = 0.0, k1 = 0.0, v0 = 0.0, v1 = 0.0;
                if (g->dataCount() > 0) {
                    k0 = g->data()->at(0)->key;
                    k1 = g->data()->at(g->dataCount() - 1)->key;
                    v0 = g->data()->at(0)->value;
                    v1 = g->data()->at(g->dataCount() - 1)->value;
                }
                ts << QStringLiteral("  首条折线：可见=%1 点数=%2 首点=(%3,%4) 末点=(%5,%6) 颜色=%7\n")
                          .arg(g->visible() ? 1 : 0).arg(g->dataCount())
                          .arg(k0).arg(v0).arg(k1).arg(v1)
                          .arg(g->pen().color().name());
            }
            ts << QStringLiteral("  左轴：标签=%1 范围=%2 ~ %3\n")
                      .arg(sp2->yAxis->label())
                      .arg(sp2->yAxis->range().lower)
                      .arg(sp2->yAxis->range().upper);
            ts << QStringLiteral("  X 轴范围=%1 ~ %2\n")
                      .arg(sp2->xAxis->range().lower).arg(sp2->xAxis->range().upper);
        }
        for (QWidget *cw : w.findChildren<QWidget *>()) {
            if (qstrcmp(cw->metaObject()->className(), "QCustomPlot") != 0) continue;
            if (cw->objectName() == QStringLiteral("plotEnv")) continue;
            if (QCustomPlot *sx = qobject_cast<QCustomPlot *>(cw)) {
                ts << QStringLiteral("  X 轴条 = %1 x %2，轴矩形宽 = %3\n")
                          .arg(sx->width()).arg(sx->height()).arg(sx->axisRect()->width());
                int dots = 0, selDots = 0;
                for (int i = 0; i < sx->plottableCount(); ++i) {
                    QCPGraph *g = qobject_cast<QCPGraph *>(sx->plottable(i));
                    if (g == nullptr || !g->visible()) continue;
                    if (g->scatterStyle().shape() != QCPScatterStyle::ssDisc) continue;
                    if (g->dataCount() == 1 && g->scatterStyle().size() > 9.0)
                        selDots += g->dataCount();
                    else
                        dots += g->dataCount();
                }
                ts << QStringLiteral("  X 轴条圆点 = %1 个（最近 20 个样本优先保留，全集合按像素间距抽稀），选中高亮 = %2 个\n")
                          .arg(dots).arg(selDots);
            }
        }
        /* 点条与主图的逐像素对齐：两处轴矩形左右像素 + 序号 1/序号 N 的像素 x */
        QStringList align;
        w.dumpStripAlign(align);
        for (const QString &line : align)
            ts << QStringLiteral("  %1\n").arg(line);
        ts << '\n';

        struct Row { int w; QString name; QString cls; };
        QVector<Row> rows;
        const QList<QWidget *> ws = w.findChildren<QWidget *>();
        for (QWidget *c : ws) {
            const int mw = c->minimumSizeHint().width();
            if (mw < 120)
                continue;                       /* 只关心撑宽度的 */
            rows.push_back(Row{mw, c->objectName(),
                               QString::fromLatin1(c->metaObject()->className())});
        }
        std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.w > b.w; });
        ts << QStringLiteral("minimumSizeHint 最宽的控件（>120 px，前 25 个）：\n");
        for (int i = 0; i < rows.size() && i < 25; ++i)
            ts << QStringLiteral("  %1 px  %2  [%3]\n")
                      .arg(rows[i].w, 5)
                      .arg(rows[i].name.isEmpty() ? QStringLiteral("(无名)") : rows[i].name)
                      .arg(rows[i].cls);
        f.close();
        qApp->quit();
    });

    return QApplication::exec();
}

/* --shot 截图.png：把主界面自身渲染成 PNG。
 * 不用外部抓屏：Windows 缩放不是 100% 时外部抓屏受 DPI 虚拟化影响，只能抓到
 * 左上角一块；QWidget::grab() 取的是本进程的真实渲染结果，与屏幕显示一致。
 * 可选参数 --feed N：截图前先灌 N 帧演示数据（间隔 1 s），把曲线与统计区填满。
 * 可选参数 --view N：先切换"查看数据"（左轴与曲线），用于逐个物理量出图核对。
 * 可选参数 --selpoint N：按倒数第 N 个可点击点选中。
 * 可选参数 --selsample N：按数据序号 N（1 起）选中，用于验证"第一帧也能选中"。 */
static int runShot(const QString &path, int feedFrames, int selPoint, int viewSeries,
                   int selSample, bool minSize = false)
{
    MainWindow w;
    w.show();
    /* --minwin：先把窗口压到最小尺寸再截图，用于自查最小尺寸下右栏是否还被裁掉 */
    if (minSize) w.resize(w.minimumSize());
    if (feedFrames > 0) w.feedDemoFrames(feedFrames);
    /* --view N：先切查看量，再选点出图，用于逐个物理量核对左轴、曲线与 X 轴上的点 */
    if (viewSeries >= 0) w.setViewSeries(viewSeries);
    /* --selpoint N：按倒数第 N 个可点击点选中，出图时能验证"点击显示某组数值"
     * 与选中竖线确实画出来了。默认 -1 表示不选中。 */
    if (selPoint >= 0 && feedFrames > 0) w.selectStatPointFromEnd(selPoint);
    /* --selsample N：按数据序号选中（序号 1 = 第一帧），验证首点可见且可选中 */
    if (selSample > 0 && feedFrames > 0) w.selectStatSampleByNumber(selSample);

    QTimer::singleShot(800, &w, [&w, path]() {
        const QPixmap pm = w.grab();
        if (!pm.isNull())
            pm.save(path, "PNG");
        qApp->quit();
    });

    return QApplication::exec();
}

/* --rangecheck：量程校验（数据边界）的对照自检，不开窗口、写 量程校验自检.txt。
 * 板1 的 frame_build() 里气压/光照/TVOC/MQ-135 都是硬编码 0，0.0 hPa 会命中
 * 气压低于下限 980，一插串口就是假告警，所以把校验开/关两遍跑出来对照。
 * 报告里同时写明挡不住的部分：光照 0 lx 是物理合理值（关灯的房间），
 * 量程校验拦不住光照过低告警；只能去掉光照下限，或等 BH1750 接上。 */
static int runRangeCheck()
{
    /* 用真协议组帧再解析回来，与界面走同一条路径，不是直接塞结构体 */
    auto frameFrom = [](double tempC, double humiRh, double pressHpa,
                        quint16 light, quint16 tvoc, quint16 mq) {
        const QByteArray bytes = FrameProto::buildEnv(qint16(tempC * 100.0),
                                                      qint16(humiRh * 100.0),
                                                      qint16(pressHpa * 10.0),
                                                      light, tvoc, mq);
        FrameParser p;
        p.feed(bytes);
        EnvFrame f;
        p.takeFrame(f);
        return f;
    };

    /* 与 mainwindow.cpp 的 handleFrame() 同一套算法：先算 skipMask，再 evaluate */
    auto run = [](const EnvFrame &f, bool check, QString &detail) {
        quint8 mask = 0;
        if (check) {
            for (int i = 0; i < int(Series::Count); ++i) {
                if (PhysRange::suspect(Series(i), DataModel::value(f, Series(i))))
                    mask |= quint8(1u << i);
            }
        }
        AlarmManager m;
        const QVector<AlarmManager::Record> recs = m.evaluate(f, 0, mask);
        QStringList d;
        for (const AlarmManager::Record &r : recs) {
            if (r.recovery) continue;
            d << QString::fromUtf8(DataModel::name(r.series))
                     + (r.level == AlarmLevel::High ? QStringLiteral(" 超上限")
                                                    : QStringLiteral(" 低于下限"));
        }
        detail = d.isEmpty() ? QStringLiteral("（无）") : d.join(QStringLiteral("、"));
        return int(d.size());
    };

    struct Row {
        QString name;
        int     got;
        int     want;
        QString detail;
    };
    QVector<Row> rows;

    const EnvFrame board1Now = frameFrom(25.6, 60.3, 0.0, 0, 0, 0);   /* 板1 当前真实内容 */
    const EnvFrame hotTemp   = frameFrom(85.0, 60.3, 1013.2, 320, 15, 812);
    const EnvFrame humiBad   = frameFrom(25.6, -0.01, 1013.2, 320, 15, 812);  /* humiX100 = -1 */

    QString d;

    rows.push_back(Row{ QStringLiteral("① 板1 现状帧（气压/光照/TVOC/MQ-135 = 0），量程校验【关】"),
                        run(board1Now, false, d), 2, d });
    rows.push_back(Row{ QStringLiteral("② 板1 现状帧，量程校验【开】"),
                        run(board1Now, true,  d), 1, d });
    rows.push_back(Row{ QStringLiteral("③ 温度 85℃（超合理上限 80），量程校验【开】"),
                        run(hotTemp,   true,  d), 0, d });
    rows.push_back(Row{ QStringLiteral("④ 温度 85℃，量程校验【关】（对照）"),
                        run(hotTemp,   false, d), 1, d });
    rows.push_back(Row{ QStringLiteral("⑤ 湿度无效标记 humiX100 = -1"),
                        run(humiBad,   true,  d), 0, d });

    bool allOk = true;
    for (const Row &r : rows)
        if (r.got != r.want) allOk = false;

    QString text;
    QTextStream ts(&text);
    ts << QStringLiteral("上位机 · 数据边界（量程校验）自检（--rangecheck）\n");
    ts << QStringLiteral("========================================\n\n");
    ts << QStringLiteral("告警阈值（出厂默认）：温度 5~35℃ 回差1 ｜ 湿度 20~80%RH 回差2 ｜ "
                         "气压 980~1030hPa ｜ 光照 只启用下限 50lx ｜ TVOC 只启用上限 1000ppb ｜ "
                         "MQ-135 只启用上限 2000\n\n");
    ts << QStringLiteral("物理合理范围（physrange.h）：温度 -40~80℃ ｜ 湿度 0~100%RH ｜ "
                         "气压 300~1100hPa ｜ 光照 0~20000lx ｜ TVOC 0~5000ppb ｜ MQ-135 0~4095\n\n");
    int idx = 0;
    for (const Row &r : rows) {
        ts << QStringLiteral("[%1] %2\n").arg(r.got == r.want ? QStringLiteral("PASS")
                                                             : QStringLiteral("FAIL"))
                                          .arg(r.name);
        ts << QStringLiteral("      告警 %1 项（期望 %2）：%3\n")
                  .arg(r.got).arg(r.want).arg(r.detail);
        ++idx;
    }
    ts << QStringLiteral("\n----------------------------------------\n");
    ts << QStringLiteral("结论：%1\n")
              .arg(allOk ? QStringLiteral("边界处理符合预期")
                         : QStringLiteral("有不符合预期的用例"));
    ts << QStringLiteral(
        "\n说明：\n"
        "  · 量程校验只拦物理上不可能的值。0 hPa、85℃ 这种会被拦下（标黄 + 计入统计，\n"
        "    CSV 记 +RANGE），但不丢数据，否则无法区分传感器没接和真读数。\n"
        "  · 光照 0 lx 是合理值（关灯房间就是 0），因此拦不住光照低于下限 50lx 这条告警。\n"
        "    板1 现在的光照读数是硬编码 0，联调时若不想看到这条假告警，可去掉光照的下限启用\n"
        "    （阈值设置… 里取消勾选），或等 BH1750 接上、读数不再是 0。\n"
        "  · 温度合理上限 80℃ 高于告警上限 35℃：若确实有 80℃ 以上的场景要报警，\n"
        "    把量程校验关掉即可（那已在传感器量程之外，本就不该信）。\n");

    QFile f(QDir::current().filePath(QStringLiteral("量程校验自检.txt")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << QChar(0xFEFF) << text;
        f.close();
    }
    return allOk ? 0 : 1;
}

/* --shotth 截图.png：只截告警阈值设置对话框（6 项 × 下限/上限/启用/回差
 * 加一列合理范围，全都能改）。 */
static int runShotThreshold(const QString &path)
{
    AlarmManager mgr;                 /* 用出厂默认值显示，别受上一次改的 ini 影响 */
    ThresholdDialog d(&mgr);
    d.show();

    QTimer::singleShot(600, &d, [&d, path]() {
        const QPixmap pm = d.grab();
        if (!pm.isNull())
            pm.save(path, "PNG");
        qApp->quit();
    });

    return QApplication::exec();
}

/* --cmdselftest：下发控制通道（上位机 → 板2）的字节级自检，不开窗口、
 * 写 下发控制自检.txt，退出码 0 = 全通过。
 * 期望字节必须硬编码：若也用 FrameProto::build() 现场算，build() 本身写错
 * （CRC 范围少一字节、大端小端写反）时会自己证明自己正确。这里的期望字节由
 * 独立 Python 实现（tools/gen_ctrl_vectors.py，只照协议文字描述写）算出后抄入。
 * 不做端到端（真发真收）：本机没有第二个串口/虚拟串口对，真链路要等板2 接上。
 * 本自检覆盖组帧字节、应答解析与 DevCtl 状态机中的纯逻辑部分；接线与固件
 * 是否烧录只能在实机上验。 */
static int runCmdSelfTest()
{
    struct Case { QString name; bool pass; QString detail; };
    QVector<Case> cases;

    auto hex = [](const QByteArray &b) {
        return QString::fromLatin1(b.toHex(' ')).toUpper();
    };
    auto add = [&cases](const QString &name, bool pass, const QString &detail) {
        cases.push_back(Case{ name, pass, detail });
    };

    /* 组帧：请求帧字节与 Python 独立实现逐字节对照 */
    struct Vec { quint8 cmd; QByteArray payload; QString want; QString label; };
    const QVector<Vec> vecs = {
        { FrameProto::CMD_QUERY,     QByteArray(),                        QStringLiteral("AA 55 12 00 0D 10 55 AA"),       QStringLiteral("查询状态") },
        { FrameProto::CMD_SET_FWD,   QByteArray(1, '\x01'),               QStringLiteral("AA 55 10 01 01 B0 55 55 AA"),    QStringLiteral("转发=开") },
        { FrameProto::CMD_SET_FWD,   QByteArray(1, '\x00'),               QStringLiteral("AA 55 10 01 00 71 95 55 AA"),    QStringLiteral("转发=关") },
        { FrameProto::CMD_SET_CACHE, QByteArray(1, '\x01'),               QStringLiteral("AA 55 11 01 01 E1 95 55 AA"),    QStringLiteral("缓存=开") },
        { FrameProto::CMD_CLR_CACHE, QByteArray(),                        QStringLiteral("AA 55 13 00 0C 80 55 AA"),       QStringLiteral("清缓存") },
        { FrameProto::CMD_REBOOT,    QByteArray(),                        QStringLiteral("AA 55 14 00 0E B0 55 AA"),       QStringLiteral("软复位") },
        { FrameProto::CMD_REPLAY,    QByteArray(1, '\x01'),               QStringLiteral("AA 55 16 01 01 50 54 55 AA"),    QStringLiteral("开始补传") },
        { FrameProto::CMD_REPLAY,    QByteArray(1, '\x00'),               QStringLiteral("AA 55 16 01 00 91 94 55 AA"),    QStringLiteral("停止补传") },
    };
    for (const Vec &v : vecs) {
        const QByteArray got = FrameProto::build(v.cmd, v.payload);
        add(QStringLiteral("组帧 %1（0x%2）").arg(v.label)
                .arg(v.cmd, 2, 16, QLatin1Char('0')).toUpper(),
            hex(got) == v.want,
            QStringLiteral("得到 %1 / 期望 %2").arg(hex(got), v.want));
    }

    /* 解析：板2 应答帧 → AckFrame 各字段 */
    {
        const QByteArray f = FrameProto::build(0x92U,
            QByteArray::fromHex("000101000123000400C8000200001234"));
        FrameParser p;
        p.feed(f);
        AckFrame a;
        const bool got = p.takeAck(a);
        const bool ok = got && a.reqCmd() == FrameProto::CMD_QUERY &&
                        a.status() == FrameProto::ST_OK && a.fwdOn() && a.cacheOn() &&
                        a.rxOk() == 0x0123 && a.rxBad() == 0x0004 &&
                        a.cacheCount() == 200 && a.lostCount() == 2 && a.uptimeS() == 0x1234;
        add(QStringLiteral("解析 查询应答（16 字节，8 个字段）"), ok,
            QStringLiteral("req=0x%1 st=%2 fwd=%3 cache=%4 好=%5 坏=%6 缓存=%7 丢=%8 运行=%9s")
                .arg(a.reqCmd(), 2, 16, QLatin1Char('0')).toUpper()
                .arg(a.status()).arg(a.fwdOn()).arg(a.cacheOn())
                .arg(a.rxOk()).arg(a.rxBad()).arg(a.cacheCount())
                .arg(a.lostCount()).arg(a.uptimeS()));
        add(QStringLiteral("应答帧计入 framesAck（不是错误也不是环境帧）"),
            p.stats().framesAck == 1 && p.stats().framesOk == 0 &&
            p.stats().framesOtherCmd == 0,
            QStringLiteral("framesAck=%1 framesOk=%2 framesOtherCmd=%3 errCrc=%4")
                .arg(p.stats().framesAck).arg(p.stats().framesOk)
                .arg(p.stats().framesOtherCmd).arg(p.stats().errCrc));
    }

    {
        FrameParser p;
        p.feed(FrameProto::build(0x90U, QByteArray::fromHex("0001")));   /* SET_FWD OK，新值 1 */
        AckFrame a;
        const bool ok = p.takeAck(a) && a.status() == FrameProto::ST_OK &&
                        a.payload.size() == 2 && a.u8(FrameProto::ACK_VAL_OFF) == 1U;
        add(QStringLiteral("解析 转发开关应答（状态 + 新值）"), ok,
            QStringLiteral("st=%1 size=%2 新值=%3")
                .arg(a.status()).arg(a.payload.size()).arg(a.u8(FrameProto::ACK_VAL_OFF)));
    }

    {
        FrameParser p;
        p.feed(FrameProto::build(0x90U, QByteArray::fromHex("01")));     /* SET_FWD 参数错 */
        AckFrame a;
        const bool ok = p.takeAck(a) && a.status() == FrameProto::ST_PARAM && a.payload.size() == 1;
        add(QStringLiteral("解析 参数错应答（状态码 1）"), ok,
            QStringLiteral("st=%1（%2）").arg(a.status()).arg(FrameProto::statusName(a.status())));
    }

    {
        FrameParser p;
        p.feed(FrameProto::build(0x93U, QByteArray::fromHex("000000"))); /* CLR_CACHE OK，清后 0 条 */
        AckFrame a;
        const bool ok = p.takeAck(a) && a.status() == FrameProto::ST_OK &&
                        a.u16(FrameProto::ACK_CNT_OFF) == 0;
        add(QStringLiteral("解析 清缓存应答（状态 + 清后条数）"), ok,
            QStringLiteral("st=%1 清后条数=%2")
                .arg(a.status()).arg(a.u16(FrameProto::ACK_CNT_OFF)));
    }

    /* 补传应答 0x96：数据段第 1 字节是补传状态（不是通用状态码），
     * 后三字节为已补传条数（低、高）与剩余条数。期望字节同样由独立 Python 实现算出。 */
    {
        FrameParser p;
        p.feed(QByteArray::fromHex("AA55960402000507AFC755AA"));
        AckFrame a;
        const bool ok = p.takeAck(a) && a.cmd == FrameProto::CMD_REPLAY_ACK &&
                        a.reqCmd() == FrameProto::CMD_REPLAY &&
                        a.replayState() == FrameProto::RP_DONE && a.replayedNum() == 5 &&
                        a.replayRest() == 7 && !a.replayRestCapped() &&
                        a.payload.size() == FrameProto::REPLAY_ACK_LEN;
        add(QStringLiteral("解析 补传应答（已完成 / 已补 5 条 / 剩余 7 条）"), ok,
            QStringLiteral("req=0x%1 状态=%2（%3）已补=%4 剩余=%5 剩余被截断=%6")
                .arg(a.reqCmd(), 2, 16, QLatin1Char('0')).toUpper()
                .arg(a.replayState())
                .arg(AckFrame::replayStateName(a.replayState()))
                .arg(a.replayedNum()).arg(a.replayRest()).arg(a.replayRestCapped()));
    }

    {
        FrameParser p;
        p.feed(QByteArray::fromHex("AA5596040104D2FFB03055AA"));
        AckFrame a;
        const bool ok = p.takeAck(a) && a.cmd == FrameProto::CMD_REPLAY_ACK &&
                        a.replayState() == FrameProto::RP_RUNNING &&
                        a.replayedNum() == 1234 && a.replayRest() == 255 &&
                        a.replayRestCapped();
        add(QStringLiteral("解析 补传中应答（已补 1234 条 / 剩余 255 以上）"), ok,
            QStringLiteral("状态=%1 已补=%2 剩余=%3 剩余被截断=%4（板2 只用 1 字节表示剩余，"
                           "255 表示还有更多）")
                .arg(AckFrame::replayStateName(a.replayState()))
                .arg(a.replayedNum()).arg(a.replayRest()).arg(a.replayRestCapped()));
    }

    /* 0x96 与通用状态应答不能混：补传状态 2 = 已完成，不是"不支持"，
     * 因此它不能去更新 DevStatus，也不能被判成"板2 拒绝"。 */
    {
        DevCtl d;
        FrameParser p;
        p.feed(QByteArray::fromHex("AA55960402000507AFC755AA"));
        AckFrame a;
        p.takeAck(a);
        d.onAck(a);
        const ReplayStatus &r = d.lastReplay();
        const bool ok = r.valid && r.state == FrameProto::RP_DONE && r.sent == 5 &&
                        r.rest == 7 && a.cmd == FrameProto::CMD_REPLAY_ACK &&
                        !d.lastStatus().valid;
        add(QStringLiteral("DevCtl 收 0x96 更新补传进度（不写设备状态快照）"), ok,
            QStringLiteral("补传 valid=%1 状态=%2 已补=%3 剩余=%4；设备状态 valid=%5（期望 false）")
                .arg(r.valid).arg(AckFrame::replayStateName(r.state))
                .arg(r.sent).arg(r.rest).arg(d.lastStatus().valid));
    }

    /* 坏帧必须被拒：CRC 改 1 位 */
    {
        QByteArray bad = FrameProto::build(0x92U,
            QByteArray::fromHex("000101000123000400C8000200001234"));
        bad[6] = static_cast<char>(static_cast<quint8>(bad.at(6)) ^ 0x01U);   /* 翻 CRC 低字节 */
        FrameParser p;
        p.feed(bad);
        AckFrame a;
        const bool ok = !p.takeAck(a) && p.stats().errCrc >= 1;
        add(QStringLiteral("CRC 坏 1 位的应答被拒（不进 AckFrame）"), ok,
            QStringLiteral("errCrc=%1 取到应答=%2").arg(p.stats().errCrc).arg(p.takeAck(a)));
    }

    /* 环境帧与应答帧混流：两条数据 + 一条应答，互不干扰 */
    {
        QByteArray stream = FrameProto::buildEnv(2560, 6030, 10132, 320, 15, 812);
        stream += FrameProto::build(0x92U, QByteArray::fromHex("000101000123000400C8000200001234"));
        stream += FrameProto::buildEnv(2570, 6040, 10133, 330, 16, 820);

        FrameParser p;
        p.feed(stream);
        int env = 0;
        EnvFrame f;
        while (p.takeFrame(f)) ++env;
        AckFrame a;
        const bool ackOk = p.takeAck(a);
        add(QStringLiteral("环境帧与应答帧混流：2 数据 + 1 应答"),
            env == 2 && ackOk && p.stats().framesOk == 2 && p.stats().framesAck == 1,
            QStringLiteral("环境帧=%1 应答=%2 framesOk=%3 framesAck=%4 errCrc=%5")
                .arg(env).arg(ackOk).arg(p.stats().framesOk).arg(p.stats().framesAck)
                .arg(p.stats().errCrc));
    }

    /* DevCtl：串口没开时下发应被拒（返回 false，不崩） */
    {
        DevCtl d;
        const bool r1 = d.query();
        const bool r2 = d.setForward(true);
        add(QStringLiteral("串口未 attach：下发被拒且不崩（护栏）"),
            !r1 && !r2 && d.pendingCount() == 0 && d.txCount() == 0,
            QStringLiteral("query=%1 setFwd=%2 队列=%3 已发=%4")
                .arg(r1).arg(r2).arg(d.pendingCount()).arg(d.txCount()));
    }

    /* DevCtl：串口没开时开始/停止补传同样被拒，不进队列也不崩 */
    {
        DevCtl d;
        const bool r1 = d.startReplay();
        const bool r2 = d.stopReplay();
        add(QStringLiteral("串口未 attach：开始/停止补传被拒（护栏）"),
            !r1 && !r2 && d.pendingCount() == 0 && d.txCount() == 0,
            QStringLiteral("开始=%1 停止=%2 队列=%3 已发=%4")
                .arg(r1).arg(r2).arg(d.pendingCount()).arg(d.txCount()));
    }

    /* DevCtl：onAck 把应答解成状态快照，属不依赖串口的纯逻辑 */
    {
        DevCtl d;
        FrameParser p;
        p.feed(FrameProto::build(0x92U, QByteArray::fromHex("000101000123000400C8000200001234")));
        AckFrame a;
        p.takeAck(a);
        d.onAck(a);
        const DevStatus &s = d.lastStatus();
        const bool ok = s.valid && s.fwd && s.cache && s.rxOk == 0x0123 &&
                        s.rxBad == 4 && s.cacheCount == 200 && s.lost == 2 && s.uptimeS == 0x1234;
        add(QStringLiteral("DevCtl 解析状态快照（转发/缓存/统计/运行时长）"), ok,
            QStringLiteral("valid=%1 fwd=%2 cache=%3 好=%4 坏=%5 缓存=%6 丢=%7 运行=%8s")
                .arg(s.valid).arg(s.fwd).arg(s.cache).arg(s.rxOk).arg(s.rxBad)
                .arg(s.cacheCount).arg(s.lost).arg(s.uptimeS));
        add(QStringLiteral("DevCtl 收到应答计数 +1"), d.rxCount() == 1,
            QStringLiteral("rxCount=%1").arg(d.rxCount()));
    }

    bool allOk = true;
    int  passed = 0;
    for (const Case &c : cases) {
        if (c.pass) ++passed;
        else        allOk = false;
    }

    QString text;
    QTextStream ts(&text);
    ts << QStringLiteral("上位机 · 下发控制通道自检（--cmdselftest）\n");
    ts << QStringLiteral("========================================\n\n");
    ts << QStringLiteral("协议：AA 55 CMD LEN DATA… CRC低 CRC高 55 AA\n"
                         "请求 0x10 SET_FWD / 0x11 SET_CACHE / 0x12 QUERY / 0x13 CLR_CACHE / 0x14 REBOOT / 0x16 REPLAY\n"
                         "上行 0x01 环境帧 / 0x15 历史帧（板2 补传）\n"
                         "应答 CMD = 请求|0x80，数据段第 1 字节 = 状态（0=OK 1=参数错 2=不支持 3=忙）；\n"
                         "0x96 补传应答的数据段第 1 字节为补传状态（0=空闲 1=补传中 2=已完成 3=出错）\n\n");
    ts << QStringLiteral("期望字节来自独立 Python 实现（tools/gen_ctrl_vectors.py），"
                         "不是用被测代码自己算的。\n\n");
    for (const Case &c : cases) {
        ts << QStringLiteral("[%1] %2\n")
                  .arg(c.pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"), c.name);
        ts << QStringLiteral("      %1\n").arg(c.detail);
    }
    ts << QStringLiteral("\n----------------------------------------\n");
    ts << QStringLiteral("通过 %1 / %2\n").arg(passed).arg(cases.size());
    ts << QStringLiteral("结论：%1\n")
              .arg(allOk ? QStringLiteral("下发通道的字节与解析符合协议")
                         : QStringLiteral("有用例不符合协议"));
    ts << QStringLiteral(
        "\n说明（以下部分本自检未覆盖，不能视为已验证）：\n"
        "  · 真串口收发、板2 固件是否烧了新版本、USART2 RX 中断是否开对，要实机联调；\n"
        "  · DevCtl 的超时/重发（600 ms × 3）只在真链路上才有意义（本机没有第二个串口）；\n"
        "  · 补传端到端（板2 真回 0x15 历史帧、0x96 进度应答）要实机联调，"
        "本自检只覆盖组帧字节与应答解析；\n"
        "  · 板2 侧的对应实现见 Project_2_数据通信端/Project/main.c 的 vTaskCmdRx()。\n");

    QFile f(QDir::current().filePath(QStringLiteral("下发控制自检.txt")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << QChar(0xFEFF) << text;
        f.close();
    }
    return allOk ? 0 : 1;
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    const QStringList args = a.arguments();

    /* 与截图配套的灌数帧数 / 查看量：命令行里 --feed、--view 可以写在 --shot 后面，
     * 所以先整体解析一遍，再决定走哪个模式。 */
    int feedFrames = 0;
    int selPoint = -1;
    int viewSeries = -1;
    int selSample = 0;
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--feed"))
            feedFrames = qMax(0, args.at(i + 1).toInt());
        if (args.at(i) == QStringLiteral("--selpoint"))
            selPoint = args.at(i + 1).toInt();
        if (args.at(i) == QStringLiteral("--selsample"))
            selSample = qMax(0, args.at(i + 1).toInt());
        if (args.at(i) == QStringLiteral("--view"))
            viewSeries = args.at(i + 1).toInt();
    }

    if (args.contains(QStringLiteral("--selftest")))
        return runSelfTest();
    if (args.contains(QStringLiteral("--rangecheck")))
        return runRangeCheck();
    if (args.contains(QStringLiteral("--cmdselftest")))
        return runCmdSelfTest();
    if (args.contains(QStringLiteral("--dumpmin")))
        return runDumpMin(feedFrames, viewSeries);

    /* 再看是不是截图：--feed / --view / --selpoint / --selsample 已在上面解析完 */
    const bool minSize = args.contains(QStringLiteral("--minwin"));
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--shot"))
            return runShot(args.at(i + 1), feedFrames, selPoint, viewSeries, selSample, minSize);
        if (args.at(i) == QStringLiteral("--shotth"))
            return runShotThreshold(args.at(i + 1));
    }

    MainWindow w;
    w.show();
    return QApplication::exec();
}
