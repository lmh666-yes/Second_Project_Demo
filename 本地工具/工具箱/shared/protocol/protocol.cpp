#include "protocol.h"

#include <QtGlobal>
#include <QtMath>

namespace Proto {

quint16 crc16Modbus(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= (quint8)data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

QByteArray buildFrame(quint8 cmd, const QByteArray &data)
{
    QByteArray f;
    f.append((char)HEAD0);
    f.append((char)HEAD1);
    f.append((char)cmd);
    f.append((char)(quint8)data.size());
    f.append(data);
    const quint16 crc = crc16Modbus(f.mid(2));      /* CMD..DATA */
    f.append((char)(crc & 0xFF));
    f.append((char)((crc >> 8) & 0xFF));
    f.append((char)0x55);
    f.append((char)0xAA);
    return f;
}

static void put16(QByteArray &a, quint16 v)
{
    a.append((char)((v >> 8) & 0xFF));
    a.append((char)(v & 0xFF));
}

QByteArray buildEnvData(double tempC, double humiRh, double pressHpa,
                        int lightLx, int tvocPpb, int mq135Raw)
{
    QByteArray d;
    put16(d, (quint16)(qint16)qRound(tempC * 100.0));
    put16(d, (quint16)(qint16)qRound(humiRh * 100.0));
    put16(d, (quint16)(qint16)qRound(pressHpa * 10.0));
    put16(d, (quint16)lightLx);
    put16(d, (quint16)tvocPpb);
    put16(d, (quint16)mq135Raw);
    return d;
}

QString hexSpaced(const QByteArray &b)
{
    QString out;
    for (int i = 0; i < b.size(); ++i) {
        if (i)
            out += ' ';
        out += QString("%1").arg((quint8)b[i], 2, 16, QChar('0')).toUpper();
    }
    return out;
}

QByteArray parseHexInput(const QString &text, bool *errOk)
{
    QString t = text;
    t.remove(' ');
    t.remove('\t');
    t.remove('\n');
    t.remove('\r');
    t.remove(',');
    t.remove(QString::fromUtf8("，"));
    t.remove("0x");
    t.remove("0X");
    if (errOk)
        *errOk = true;
    if (t.isEmpty())
        return QByteArray();
    if (t.size() % 2 != 0) {
        if (errOk)
            *errOk = false;
        return QByteArray();
    }
    for (int i = 0; i < t.size(); ++i) {
        if (!QString("0123456789abcdefABCDEF").contains(t[i])) {
            if (errOk)
                *errOk = false;
            return QByteArray();
        }
    }
    return QByteArray::fromHex(t.toLatin1());
}

QString cmdName(quint8 cmd)
{
    switch (cmd) {
    case 0x01: return QString::fromUtf8("环境帧（板1 上行）");
    case 0x10: return QString::fromUtf8("设置转发开关");
    case 0x11: return QString::fromUtf8("设置缓存开关");
    case 0x12: return QString::fromUtf8("查询状态");
    case 0x13: return QString::fromUtf8("清空缓存");
    case 0x14: return QString::fromUtf8("重启板2");
    case 0x15: return QString::fromUtf8("历史帧（板2 上行）");
    case 0x16: return QString::fromUtf8("启停补传");
    case 0x90: return QString::fromUtf8("应答：设置转发");
    case 0x91: return QString::fromUtf8("应答：设置缓存");
    case 0x92: return QString::fromUtf8("应答：查询状态");
    case 0x93: return QString::fromUtf8("应答：清空缓存");
    case 0x94: return QString::fromUtf8("应答：重启");
    case 0x96: return QString::fromUtf8("应答：补传进度");
    default:
        if (cmd & 0x80)
            return QString("应答 0x%1").arg(cmd, 2, 16, QChar('0')).toUpper();
        return QString("其他 0x%1").arg(cmd, 2, 16, QChar('0')).toUpper();
    }
}

static QString decodeEnv(const QByteArray &d)
{
    if (d.size() < 12)
        return QString::fromUtf8("  载荷不足 12 字节，无法解析环境量。");
    auto getI16 = [&](int i) -> qint16 {
        return (qint16)((quint8)d[i] << 8 | (quint8)d[i + 1]);
    };
    auto getU16 = [&](int i) -> quint16 {
        return (quint16)((quint8)d[i] << 8 | (quint8)d[i + 1]);
    };
    QStringList L;
    L << QString::fromUtf8("  温度  %1 ℃").arg(getI16(0) / 100.0, 0, 'f', 2);
    L << QString::fromUtf8("  湿度  %1 %RH").arg(getI16(2) / 100.0, 0, 'f', 2);
    L << QString::fromUtf8("  气压  %1 hPa").arg(getI16(4) / 10.0, 0, 'f', 1);
    L << QString::fromUtf8("  光照  %1 lx").arg(getU16(6));
    L << QString::fromUtf8("  TVOC  %1 ppb").arg(getU16(8));
    L << QString::fromUtf8("  MQ135 %1（ADC 原始）").arg(getU16(10));
    return L.join('\n');
}

QString describeFrame(const Frame &f)
{
    const QString cmdHex = QString("%1").arg(f.cmd, 2, 16, QChar('0')).toUpper();
    const QString crcRx  = QString("%1").arg(f.crc, 4, 16, QChar('0')).toUpper();
    const QString crcCc  = QString("%1").arg(f.crcCalc, 4, 16, QChar('0')).toUpper();

    QStringList L;
    L << QString::fromUtf8("偏移 %1，共 %2 字节").arg(f.offset).arg(f.raw.size());
    L << QString::fromUtf8("  原始   %1").arg(hexSpaced(f.raw));
    L << QString::fromUtf8("  帧头   AA 55");
    L << QString::fromUtf8("  命令   0x%1（%2）").arg(cmdHex, cmdName(f.cmd));
    L << QString::fromUtf8("  长度   %1").arg(f.len);
    L << QString::fromUtf8("  载荷   %1")
            .arg(f.data.isEmpty() ? QString::fromUtf8("（空）") : hexSpaced(f.data));
    L << QString::fromUtf8("  CRC    收到 0x%1 / 算得 0x%2  %3")
            .arg(crcRx, crcCc,
                 f.crcOk ? QString::fromUtf8("相符") : QString::fromUtf8("不符"));
    L << QString::fromUtf8("  帧尾   55 AA  %1")
            .arg(f.tailOk ? QString::fromUtf8("正常") : QString::fromUtf8("异常"));
    L << QString::fromUtf8("  结论   %1%2")
            .arg(f.ok ? QString::fromUtf8("通过") : QString::fromUtf8("未通过"),
                 f.note.isEmpty() ? QString() : QString::fromUtf8("：") + f.note);

    if (f.ok && f.cmd == 0x01) {
        L << QString::fromUtf8("  环境量：");
        L << decodeEnv(f.data);
    } else if (f.ok && f.cmd == 0x15 && f.data.size() >= 24) {
        const quint32 ts = ((quint8)f.data[0] << 24) | ((quint8)f.data[1] << 16)
                         | ((quint8)f.data[2] << 8) | (quint8)f.data[3];
        L << QString::fromUtf8("  原始时间戳  0x%1（%2）")
                .arg(QString("%1").arg(ts, 8, 16, QChar('0')).toUpper()).arg(ts);
        L << QString::fromUtf8("  内嵌环境帧：");
        QStringList notes;
        const QVector<Frame> inner = parseStream(f.data.mid(4, 20), notes);
        if (!inner.isEmpty())
            L << decodeEnv(inner.first().data);
        else
            L << QString::fromUtf8("  （内嵌帧解析失败）");
    }
    return L.join('\n');
}

QVector<Frame> parseStream(const QByteArray &stream, QStringList &notes)
{
    QVector<Frame> out;
    const int n = stream.size();
    int i = 0;
    while (i + 1 < n) {
        if ((quint8)stream[i] != HEAD0 || (quint8)stream[i + 1] != HEAD1) {
            ++i;
            continue;
        }
        if (i + 3 >= n) {
            notes << QString::fromUtf8("偏移 %1：有帧头但后续字节不足").arg(i);
            break;
        }
        const quint8 cmd = (quint8)stream[i + 2];
        const int ln = (quint8)stream[i + 3];
        if (ln > MAX_LEN) {
            notes << QString::fromUtf8("偏移 %1：长度字段 %2 > 上限 %3，按噪声跳过")
                     .arg(i).arg(ln).arg(MAX_LEN);
            ++i;
            continue;
        }
        const int total = 8 + ln;
        if (i + total > n) {
            notes << QString::fromUtf8("偏移 %1：帧不完整，期望 %2 字节，只剩 %3 字节")
                     .arg(i).arg(total).arg(n - i);
            break;
        }
        Frame f;
        f.offset = i;
        f.raw = stream.mid(i, total);
        f.cmd = cmd;
        f.len = ln;
        f.data = f.raw.mid(4, ln);
        f.crcCalc = crc16Modbus(f.raw.mid(2, 2 + ln));
        f.crc = (quint16)((quint8)f.raw[4 + ln] | ((quint8)f.raw[5 + ln] << 8));
        f.tailOk = ((quint8)f.raw[total - 2] == 0x55 && (quint8)f.raw[total - 1] == 0xAA);
        f.crcOk = (f.crc == f.crcCalc);
        f.ok = f.tailOk && f.crcOk;
        if (!f.tailOk)
            f.note = QString::fromUtf8("帧尾不是 55 AA");
        else if (!f.crcOk)
            f.note = QString::fromUtf8("CRC 不符");
        out << f;
        i += total;
    }
    return out;
}

QVector<Injection> injections(const QByteArray &frame)
{
    QVector<Injection> v;
    if (frame.size() < 8)
        return v;

    QByteArray badCrc = frame;
    badCrc[badCrc.size() - 4] = (char)((quint8)badCrc[badCrc.size() - 4] ^ 0xFF);
    v << Injection{QString::fromUtf8("坏 CRC（翻转 CRC 低字节）"), badCrc};

    QByteArray badTail = frame;
    badTail[badTail.size() - 1] = (char)0x00;
    v << Injection{QString::fromUtf8("坏帧尾（尾字节改 00）"), badTail};

    QByteArray badLen = frame;
    badLen[3] = (char)((quint8)badLen[3] + 1);
    v << Injection{QString::fromUtf8("长度字段 +1（与实际不符）"), badLen};

    v << Injection{QString::fromUtf8("截断（去掉最后 1 字节）"), frame.left(frame.size() - 1)};

    QByteArray lead;
    lead.append((char)0x11);
    lead.append((char)0x22);
    lead += frame;
    v << Injection{QString::fromUtf8("前置噪声（前面加 2 字节）"), lead};

    QByteArray mid = frame;
    mid.insert(mid.size() - 2, (char)0x99);
    v << Injection{QString::fromUtf8("中间插 1 字节"), mid};

    v << Injection{QString::fromUtf8("连发两帧（粘包）"), frame + frame};
    return v;
}

static bool expect(bool cond, const QString &what, QStringList &log)
{
    log << QString("%1 %2").arg(cond ? "PASS" : "FAIL").arg(what);
    return cond;
}

int selftest(QStringList &log)
{
    int fail = 0;

    /* CRC 标准校验值 */
    if (!expect(crc16Modbus(QByteArray("123456789")) == 0x4B37,
                QString::fromUtf8("CRC16-MODBUS 标准向量 123456789 = 0x4B37"), log))
        ++fail;

    /* 文档里的两个协议向量 */
    if (!expect(hexSpaced(buildFrame(0x16, QByteArray(1, (char)0x01)))
                    == QString("AA 55 16 01 01 50 54 55 AA"),
                QString::fromUtf8("组帧 0x16 01 = AA 55 16 01 01 50 54 55 AA"), log))
        ++fail;
    if (!expect(hexSpaced(buildFrame(0x16, QByteArray(1, (char)0x00)))
                    == QString("AA 55 16 01 00 91 94 55 AA"),
                QString::fromUtf8("组帧 0x16 00 = AA 55 16 01 00 91 94 55 AA"), log))
        ++fail;

    /* 环境帧组帧后解析回来，六项数值一致 */
    const QByteArray env = buildEnvData(23.45, 60.10, 1013.2, 1200, 150, 2048);
    const QByteArray ef = buildFrame(0x01, env);
    QStringList notes;
    const QVector<Frame> fs = parseStream(ef, notes);
    bool envOk = (fs.size() == 1 && fs.first().ok && fs.first().data == env);
    if (!expect(envOk, QString::fromUtf8("环境帧组帧后解析：帧通过且载荷一致"), log))
        ++fail;

    /* 一帧 + 噪声 + 另一帧，应找出两帧 */
    QByteArray stream;
    stream.append((char)0xAB);
    stream += ef;
    stream += buildFrame(0x16, QByteArray(1, (char)0x01));
    notes.clear();
    const QVector<Frame> two = parseStream(stream, notes);
    if (!expect(two.size() == 2 && two[0].ok && two[1].ok,
                QString::fromUtf8("含前置噪声的两帧流：解出 2 帧且都通过"), log))
        ++fail;

    /* 坏帧注入：坏 CRC 与坏帧尾都应被拒 */
    bool rejCrc = false, rejTail = false;
    for (const Injection &inj : injections(ef)) {
        QStringList nt;
        const QVector<Frame> r = parseStream(inj.bytes, nt);
        if (inj.desc.contains(QString::fromUtf8("坏 CRC")) && !r.isEmpty() && !r.first().crcOk)
            rejCrc = true;
        if (inj.desc.contains(QString::fromUtf8("坏帧尾")) && !r.isEmpty() && !r.first().tailOk)
            rejTail = true;
    }
    if (!expect(rejCrc, QString::fromUtf8("坏 CRC 变体被判定 CRC 不符"), log))
        ++fail;
    if (!expect(rejTail, QString::fromUtf8("坏帧尾变体被判定帧尾异常"), log))
        ++fail;

    return fail;
}

} // namespace Proto
