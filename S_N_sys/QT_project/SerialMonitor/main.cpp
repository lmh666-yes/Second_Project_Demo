#include "mainwindow.h"

#include "consistencytest.h"
#include "devctl.h"
#include "physrange.h"
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
    const QVector<CaseResult> r = ConsistencyTest::runBuiltin();
    const QString text = ConsistencyTest::toText(
        r, QStringLiteral("Qt 上位机 · 帧解析自检（--selftest）"));

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
static int runDumpMin()
{
    MainWindow w;
    w.show();

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
 * 左上角一块；QWidget::grab() 取的是本进程的真实渲染结果，与屏幕显示一致。 */
static int runShot(const QString &path)
{
    MainWindow w;
    w.show();

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
    if (args.contains(QStringLiteral("--selftest")))
        return runSelfTest();
    if (args.contains(QStringLiteral("--rangecheck")))
        return runRangeCheck();
    if (args.contains(QStringLiteral("--cmdselftest")))
        return runCmdSelfTest();
    if (args.contains(QStringLiteral("--dumpmin")))
        return runDumpMin();
    for (int i = 1; i < args.size() - 1; ++i) {
        if (args.at(i) == QStringLiteral("--shot"))
            return runShot(args.at(i + 1));
        if (args.at(i) == QStringLiteral("--shotth"))
            return runShotThreshold(args.at(i + 1));
    }

    MainWindow w;
    w.show();
    return QApplication::exec();
}
