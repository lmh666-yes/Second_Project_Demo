#include "consistencytest.h"

#include "datamodel.h"
#include "frameparser.h"

#include <QDateTime>
#include <QStringList>
#include <QtMath>

/* 独立算出的期望向量（Python：crc = 0xFFFF; for b in buf: crc ^= b;
 * for _ in range(8): crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1）
 * 这四帧的 CRC 分别为 0x2C98 / 0x23B3 / 0x28EF / 0x0CBA，与固件同算法。 */
static const char *VEC_NORMAL =
    "AA 55 01 0C 0A 00 17 8E 27 94 01 40 00 0F 03 2C 98 FA 55 AA";
static const char *VEC_NEGATIVE =
    "AA 55 01 0C FE 0C 11 94 27 10 00 00 00 00 00 00 B3 23 55 AA";
static const char *VEC_GLUED_2ND =
    "AA 55 01 0C 04 D2 16 2E 27 10 00 64 00 05 01 2C EF 28 55 AA";
static const char *VEC_GLUED_3RD =
    "AA 55 01 0C 00 00 00 00 00 00 00 00 00 00 00 00 BA 0C 55 AA";
/* 正常帧把数据段第 2 字节 0x00 改成 0x01 ⇒ CRC 必然不符（模拟比特翻转） */
static const char *VEC_BADCRC =
    "AA 55 01 0C 0A 01 17 8E 27 94 01 40 00 0F 03 2C 98 FA 55 AA";

/* 历史帧（0x15）：4 字节原始时间戳（大端，hh*10000+mm*100+ss）+ 20 字节原始环境帧。
 * 时间戳 123456 = 12:34:56 = 0x0001E240，原始帧用 VEC_NORMAL，
 * 期望字节同样由独立 Python 实现算出（与上面四个向量用同一段脚本）。 */
static const char *VEC_HISTORY =
    "AA 55 15 18 00 01 E2 40 "
    "AA 55 01 0C 0A 00 17 8E 27 94 01 40 00 0F 03 2C 98 FA 55 AA "
    "08 D8 55 AA";

namespace {

QByteArray hexOf(const char *s)
{
    return QByteArray::fromHex(QByteArray(s).replace(" ", ""));
}

bool near(double a, double b)
{
    return qAbs(a - b) < 1e-9;
}

QString frameStr(const EnvFrame &f)
{
    return QStringLiteral("%1℃ / %2%RH / %3hPa / %4lx / %5ppb / %6")
            .arg(f.tempC(), 0, 'f', 2)
            .arg(f.humiValid() ? f.humiRh() : -1.0, 0, 'f', 2)
            .arg(f.pressHpa(), 0, 'f', 1)
            .arg(f.lightLx)
            .arg(f.tvocPpb)
            .arg(f.mq135Raw);
}

/* 把一段字节流喂进 parser，返回取到的帧 */
QVector<EnvFrame> parseStream(const QByteArray &stream, const QVector<int> &chunks,
                              FrameParser::Stats *st, bool lossEstimate = false)
{
    FrameParser p;
    p.setLossEstimateEnabled(lossEstimate);

    int off = 0;
    EnvFrame f;
    QVector<EnvFrame> frames;

    if (chunks.isEmpty()) {
        p.feed(stream);
        while (p.takeFrame(f)) frames.append(f);
    } else {
        for (int c : chunks) {
            if (off >= stream.size()) break;
            const int n = qMin(c, stream.size() - off);
            p.feed(stream.mid(off, n));
            off += n;
            while (p.takeFrame(f)) frames.append(f);
        }
        if (off < stream.size()) {
            p.feed(stream.mid(off));
            while (p.takeFrame(f)) frames.append(f);
        }
    }
    if (st != nullptr) *st = p.stats();
    return frames;
}

CaseResult mk(const QString &name, bool pass, const QString &detail)
{
    CaseResult r;
    r.name = name;
    r.pass = pass;
    r.detail = detail;
    return r;
}

}   // namespace

QVector<CaseResult> ConsistencyTest::runBuiltin()
{
    QVector<CaseResult> out;

    /* 用例 0：组帧回环，buildEnv 输出必须等于独立算出的向量 */
    {
        const QByteArray built = FrameProto::buildEnv(0x0A00, 0x178E, 0x2794,
                                                      0x0140, 0x000F, 0x032C);
        const QByteArray want = hexOf(VEC_NORMAL);
        out.append(mk(QStringLiteral("组帧回环（buildEnv == 独立 Python 向量）"),
                      built == want,
                      built == want ? QStringLiteral("20 字节逐字节一致，CRC=0x2C98 低位在前")
                                    : QStringLiteral("实测 %1，期望 %2")
                                      .arg(QString::fromLatin1(built.toHex(' ').toUpper()))
                                      .arg(QString::fromLatin1(want.toHex(' ').toUpper()))));
    }

    /* 用例 1：正常帧，6 个物理量换算与无符号 CRC */
    {
        FrameParser::Stats st;
        const QVector<EnvFrame> f = parseStream(hexOf(VEC_NORMAL), {}, &st);
        bool ok = (f.size() == 1) && st.errCrc == 0 && st.resyncBytes == 0 &&
                  near(f[0].tempC(), 25.60) && near(f[0].humiRh(), 60.30) &&
                  near(f[0].pressHpa(), 1013.2) && f[0].lightLx == 320 &&
                  f[0].tvocPpb == 15 && f[0].mq135Raw == 812 &&
                  f[0].cmd == FrameProto::CMD_ENV && f[0].len == FrameProto::ENV_LEN;
        out.append(mk(QStringLiteral("正常帧（25.60℃/60.30%/1013.2hPa/320lx/15ppb/812）"),
                      ok,
                      f.isEmpty() ? QStringLiteral("一帧都没解析出来，errCrc=%1 errLen=%2 errTail=%3")
                                    .arg(st.errCrc).arg(st.errLen).arg(st.errTail)
                                  : QStringLiteral("成帧 %1，%2，errCrc=%3 重同步字节=%4")
                                    .arg(f.size()).arg(frameStr(f[0]))
                                    .arg(st.errCrc).arg(st.resyncBytes)));
    }

    /* 用例 2：负温度，有符号 int16，-5.00℃ = 0xFE0C */
    {
        FrameParser::Stats st;
        const QVector<EnvFrame> f = parseStream(hexOf(VEC_NEGATIVE), {}, &st);
        const bool ok = (f.size() == 1) && near(f[0].tempC(), -5.00) &&
                        near(f[0].humiRh(), 45.00) && near(f[0].pressHpa(), 1000.0);
        out.append(mk(QStringLiteral("负温度（0xFE0C → -5.00℃，验证按 int16 而非 uint16 解析）"),
                      ok,
                      f.isEmpty() ? QStringLiteral("没解析出来（errCrc=%1）").arg(st.errCrc)
                                  : QStringLiteral("%1（若显示 650.36℃ 就是漏了符号扩展）").arg(frameStr(f[0]))));
    }

    /* 用例 3：CRC 错，必须丢这一帧，只重同步不猜 */
    {
        FrameParser::Stats st;
        const QVector<EnvFrame> f = parseStream(hexOf(VEC_BADCRC), {}, &st);
        const bool ok = f.isEmpty() && st.errCrc >= 1;
        out.append(mk(QStringLiteral("CRC 错帧必须被丢弃（数据段 0x00→0x01）"),
                      ok,
                      QStringLiteral("成帧 %1（期望 0），errCrc=%2，重同步字节=%3")
                        .arg(f.size()).arg(st.errCrc).arg(st.resyncBytes)));
    }

    /* 用例 4：三帧粘包，接缝 55 AA AA 55，必须一帧不丢 */
    {
        const QByteArray stream = hexOf(VEC_NORMAL) + hexOf(VEC_GLUED_2ND) +
                                  hexOf(VEC_GLUED_3RD);
        FrameParser::Stats st;
        const QVector<EnvFrame> f = parseStream(stream, {}, &st);
        bool ok = (f.size() == 3) && st.errCrc == 0 && st.errTail == 0;
        ok = ok && near(f[0].tempC(), 25.60) && near(f[1].tempC(), 12.34) &&
             near(f[1].humiRh(), 56.78) && f[1].lightLx == 100 && f[1].mq135Raw == 300 &&
             near(f[2].tempC(), 0.0) && f[2].mq135Raw == 0;
        out.append(mk(QStringLiteral("三帧粘包（接缝 55 AA AA 55，60 字节）"),
                      ok,
                      QStringLiteral("成帧 %1（期望 3）：%2 | %3 | %4")
                        .arg(f.size())
                        .arg(f.size() > 0 ? frameStr(f[0]) : QStringLiteral("-"))
                        .arg(f.size() > 1 ? frameStr(f[1]) : QStringLiteral("-"))
                        .arg(f.size() > 2 ? frameStr(f[2]) : QStringLiteral("-"))));
    }

    /* 用例 5：逐字节分包，模拟串口每次 readyRead 只给 1 字节 */
    {
        QVector<int> chunks;
        for (int i = 0; i < FrameProto::ENV_TOTAL; ++i) chunks.append(1);
        FrameParser::Stats st;
        const QVector<EnvFrame> f = parseStream(hexOf(VEC_NORMAL), chunks, &st);
        const bool ok = (f.size() == 1) && near(f[0].tempC(), 25.60) && st.resyncBytes == 0;
        out.append(mk(QStringLiteral("逐字节分包（20 次 feed，每次 1 字节）"),
                      ok,
                      QStringLiteral("成帧 %1，%2，重同步字节=%3（必须为 0）")
                        .arg(f.size())
                        .arg(f.isEmpty() ? QStringLiteral("-") : frameStr(f[0]))
                        .arg(st.resyncBytes)));
    }

    /* 用例 6：半帧残段接噪声后再来完整帧，验证重同步能力 */
    {
        const QByteArray half = hexOf(VEC_NORMAL).left(12);
        const QByteArray noise = hexOf("00 FF AA 00 AA");
        FrameParser::Stats st;
        const QVector<EnvFrame> f = parseStream(half + noise + hexOf(VEC_NORMAL), {}, &st);
        const bool ok = (f.size() == 1) && near(f[0].tempC(), 25.60) && st.resyncBytes > 0;
        out.append(mk(QStringLiteral("半帧残段 + 噪声 → 重同步后成 1 帧"),
                      ok,
                      QStringLiteral("成帧 %1，重同步字节=%2（必须 > 0），%3")
                        .arg(f.size()).arg(st.resyncBytes)
                        .arg(f.isEmpty() ? QStringLiteral("-") : frameStr(f[0]))));
    }

    /* 用例 7：湿度无效标记（负湿度；固件读失败时用它表示无数据） */
    {
        const QByteArray f = FrameProto::buildEnv(0x0A00, -1, 0x2794, 0x0140, 0x000F, 0x032C);
        FrameParser::Stats st;
        const QVector<EnvFrame> got = parseStream(f, {}, &st);
        const bool ok = (got.size() == 1) && !got[0].humiValid() && near(got[0].tempC(), 25.60);
        out.append(mk(QStringLiteral("湿度无效标记（humiX100 = -1 → 界面应显示 \"--\"）"),
                      ok,
                      got.isEmpty() ? QStringLiteral("没解析出来")
                                    : QStringLiteral("humiValid=%1，humiX100=%2（%3）")
                                      .arg(got[0].humiValid() ? "true" : "false")
                                      .arg(got[0].humiX100).arg(frameStr(got[0]))));
    }

    /* 用例 8：历史帧（补传）解析。时间戳要取出来，20 字节原始帧要原样保留，
     * 六项物理量要与同一帧走实时路径解出来的完全一致（复用同一段解帧逻辑）；
     * 同时它不能落进实时帧队列，否则会走告警判定。 */
    {
        const quint32 ts = 123456U;                      /* 12:34:56 */
        const QByteArray raw = hexOf(VEC_NORMAL);
        const QByteArray built = FrameProto::buildHistory(ts, raw);
        const QByteArray want = hexOf(VEC_HISTORY);

        FrameParser p;
        p.feed(built);
        HistoryFrame h;
        const bool got = p.takeHistory(h);
        EnvFrame live;
        const bool leaked = p.takeFrame(live);           /* 这里必须取不到 */

        const QVector<EnvFrame> ref = parseStream(raw, {}, nullptr);
        const bool envSame = !ref.isEmpty() && h.envOk &&
                             near(h.env.tempC(), ref[0].tempC()) &&
                             near(h.env.humiRh(), ref[0].humiRh()) &&
                             near(h.env.pressHpa(), ref[0].pressHpa()) &&
                             h.env.lightLx == ref[0].lightLx &&
                             h.env.tvocPpb == ref[0].tvocPpb &&
                             h.env.mq135Raw == ref[0].mq135Raw;

        const bool ok = (built == want) && got && h.valid() && h.tsRaw == ts &&
                        h.rawFrame == raw && envSame && !leaked &&
                        p.stats().framesHistory == 1 && p.stats().framesOk == 0;
        out.append(mk(QStringLiteral("历史帧解析（0x15：时间戳 + 20 字节原始帧，物理量与实时帧一致）"),
                      ok,
                      QStringLiteral("组帧%1 时间戳=%2 原始帧一致=%3 物理量一致=%4 "
                                     "落进实时队列=%5 framesHistory=%6 framesOk=%7")
                        .arg(built == want ? QStringLiteral("一致") : QStringLiteral("不一致"))
                        .arg(h.tsRaw)
                        .arg(h.rawFrame == raw ? QStringLiteral("是") : QStringLiteral("否"))
                        .arg(envSame ? QStringLiteral("是") : QStringLiteral("否"))
                        .arg(leaked ? QStringLiteral("是（不该发生）") : QStringLiteral("否"))
                        .arg(p.stats().framesHistory).arg(p.stats().framesOk)));
    }

    /* 用例 9：历史入库去重。同一时间戳 + 同一 20 字节帧连发两遍，只入库一条、
     * 去重计数为 1；同样内容的帧换成另一个时间戳则是两条数据，必须都入库，
     * 否则"同一读数在不同时刻重复出现"会被误判成重复，曲线会缺段。 */
    {
        DataModel m;
        const QByteArray raw = hexOf(VEC_NORMAL);
        const QByteArray once = FrameProto::buildHistory(123456U, raw);
        const QByteArray later = FrameProto::buildHistory(123457U, raw);

        FrameParser p;
        p.feed(once);
        p.feed(once);                                    /* 同一帧重复下发 */

        HistoryFrame h;
        int incoming = 0;
        while (p.takeHistory(h)) {
            ++incoming;
            m.addHistory(h);
        }

        const quint64 ins1 = m.historyInserted();
        const quint64 dup1 = m.historyDuplicate();
        const int     pts1 = m.size();
        const bool dupOk = (incoming == 2) && ins1 == 1 && dup1 == 1 && pts1 == 1;

        FrameParser p2;
        p2.feed(later);                                  /* 内容相同、时间戳不同 */
        while (p2.takeHistory(h)) m.addHistory(h);

        const bool diffOk = m.historyInserted() == 2 && m.historyDuplicate() == 1 &&
                            m.size() == 2;

        out.append(mk(QStringLiteral("历史入库去重（同时间戳同帧只入一次；同帧不同时间戳入两次）"),
                      dupOk && diffOk,
                      QStringLiteral("收到 %1 帧 → 入库 %2 重复丢弃 %3 曲线点 %4；"
                                     "换时间戳再收 1 帧 → 入库 %5 曲线点 %6（期望 1/1/1 与 2/2）")
                        .arg(incoming).arg(ins1).arg(dup1).arg(pts1)
                        .arg(m.historyInserted()).arg(m.size())));
    }

    /* 用例 10：不可用的历史帧必须被拒且不入库。时间戳为 0（板2 侧没带时间）
     * 与原始帧被破坏（改坏 1 位、解帧失败）两种，都只计数不落曲线。 */
    {
        DataModel m;

        HistoryFrame noTs;
        noTs.tsRaw = 0;                                  /* 时间戳字段缺失 */
        noTs.rawFrame = hexOf(VEC_NORMAL);
        noTs.envOk = true;
        const bool r1 = m.addHistory(noTs);

        /* 原始帧内容改坏 1 位，再包成 0x15 帧喂进解析器：它必须解不出历史帧 */
        QByteArray badRaw = hexOf(VEC_NORMAL);
        badRaw[4] = static_cast<char>(static_cast<quint8>(badRaw.at(4)) ^ 0x01U);
        FrameParser p;
        p.feed(FrameProto::buildHistory(123456U, badRaw));
        HistoryFrame parsed;
        const bool parsedOk = p.takeHistory(parsed);
        const bool r2 = parsedOk && m.addHistory(parsed);

        const bool ok = !r1 && !r2 && !parsedOk && p.stats().framesHistory == 0 &&
                        m.historyInserted() == 0 && m.historyDuplicate() == 0 &&
                        m.historyRejected() == 1 && m.size() == 0;
        out.append(mk(QStringLiteral("不可用历史帧被拒（时间戳为 0 / 原始帧解帧失败）"),
                      ok,
                      QStringLiteral("时间戳为 0 入库=%1；原始帧损坏可解析=%2（framesHistory=%3）"
                                     "；入库 %4 拒绝 %5 曲线点 %6（期望 0/否/0/0/1/0）")
                        .arg(r1).arg(parsedOk).arg(p.stats().framesHistory)
                        .arg(m.historyInserted()).arg(m.historyRejected()).arg(m.size())));
    }

    return out;
}

QVector<CaseResult> ConsistencyTest::replay(const QByteArray &stream)
{
    QVector<CaseResult> out;

    FrameParser::Stats st;
    const QVector<EnvFrame> frames = parseStream(stream, {}, &st, false);

    out.append(mk(QStringLiteral("回放字节数 / 成帧数"),
                  !frames.isEmpty(),
                  QStringLiteral("喂入 %1 字节 → 成帧 %2，重同步丢弃 %3 字节，"
                                 "非环境帧 %4，CRC 错 %5，长度错 %6，帧尾错 %7")
                    .arg(stream.size()).arg(frames.size()).arg(st.resyncBytes)
                    .arg(st.framesOtherCmd).arg(st.errCrc).arg(st.errLen).arg(st.errTail)));

    if (!frames.isEmpty()) {
        const EnvFrame &a = frames.first();
        const EnvFrame &b = frames.last();
        out.append(mk(QStringLiteral("首帧 / 末帧范围"),
                      true,
                      QStringLiteral("首帧 %1 ｜ 末帧 %2").arg(frameStr(a)).arg(frameStr(b))));
    }

    const quint64 bad = st.errCrc + st.errLen + st.errTail;
    const double good = double(st.framesOk);
    const double total = double(st.framesOk + bad);
    out.append(mk(QStringLiteral("帧解析正确率（本次回放）"),
                  bad == 0,
                  QStringLiteral("%1%%（成帧 %2 / 成帧+错帧 %3；错帧 CRC %4 + 长度 %5 + 帧尾 %6）")
                    .arg(total > 0 ? good * 100.0 / total : 0.0, 0, 'f', 2)
                    .arg(st.framesOk).arg(static_cast<quint64>(total))
                    .arg(st.errCrc).arg(st.errLen).arg(st.errTail)));

    return out;
}

QString ConsistencyTest::toText(const QVector<CaseResult> &results, const QString &title)
{
    QString s;
    s += QStringLiteral("========================================\n");
    s += QStringLiteral(" %1\n").arg(title);
    s += QStringLiteral(" 时间：%1\n")
             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    s += QStringLiteral("========================================\n");

    int no = 0;
    for (const CaseResult &r : results) {
        s += QStringLiteral("[%1] %2  %3\n")
                 .arg(r.pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                 .arg(QString::number(++no).rightJustified(2, QLatin1Char('0')))
                 .arg(r.name);
        if (!r.detail.isEmpty())
            s += QStringLiteral("      %1\n").arg(r.detail);
    }

    const int passed = passedCount(results);
    s += QStringLiteral("----------------------------------------\n");
    s += QStringLiteral("通过 %1 / %2    帧解析正确率 = %3%\n")
             .arg(passed).arg(results.size())
             .arg(results.isEmpty() ? 0.0
                                    : double(passed) * 100.0 / double(results.size()), 0, 'f', 1);
    s += QStringLiteral("结论：%1\n")
             .arg(allPassed(results) ? QStringLiteral("三端协议一致")
                                     : QStringLiteral("有未通过用例，先别上板"));
    return s;
}

double ConsistencyTest::passRate(const QVector<CaseResult> &results)
{
    if (results.isEmpty()) return 0.0;
    return double(passedCount(results)) * 100.0 / double(results.size());
}

bool ConsistencyTest::allPassed(const QVector<CaseResult> &results)
{
    for (const CaseResult &r : results) if (!r.pass) return false;
    return !results.isEmpty();
}

int ConsistencyTest::passedCount(const QVector<CaseResult> &results)
{
    int n = 0;
    for (const CaseResult &r : results) if (r.pass) ++n;
    return n;
}
