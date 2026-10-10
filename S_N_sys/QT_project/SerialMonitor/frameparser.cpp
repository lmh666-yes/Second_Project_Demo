#include "frameparser.h"

#include <QDateTime>

/* 协议实现。判据与板1 sys_frame.h / main.c 一致，帧格式见 frameparser.h 头部注释。 */

quint16 FrameProto::crc16Modbus(const quint8 *buf, int len)
{
    quint16 crc = 0xFFFFU;

    if (buf == nullptr || len <= 0) return crc;

    for (int i = 0; i < len; ++i) {
        crc ^= static_cast<quint16>(buf[i]);
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x0001U) crc = static_cast<quint16>((crc >> 1) ^ 0xA001U);
            else               crc = static_cast<quint16>(crc >> 1);
        }
    }
    return crc;
}

QByteArray FrameProto::buildEnv(qint16 tempX100, qint16 humiX100, qint16 pressX10,
                                quint16 lightLx, quint16 tvocPpb, quint16 mq135Raw)
{
    QByteArray f(ENV_TOTAL, '\0');

    f[0] = static_cast<char>(HEAD_HI);
    f[1] = static_cast<char>(HEAD_LO);
    f[2] = static_cast<char>(CMD_ENV);
    f[3] = static_cast<char>(ENV_LEN);

    const quint16 body[6] = {
        static_cast<quint16>(tempX100),  static_cast<quint16>(humiX100),
        static_cast<quint16>(pressX10),  lightLx, tvocPpb, mq135Raw
    };
    for (int i = 0; i < 6; ++i) {
        f[4 + 2 * i]     = static_cast<char>((body[i] >> 8) & 0xFFU);
        f[4 + 2 * i + 1] = static_cast<char>(body[i] & 0xFFU);
    }

    /* CRC 范围 = CMD + LEN + 数据段 = 14 字节，低字节在前 */
    const quint16 crc = crc16Modbus(reinterpret_cast<const quint8 *>(f.constData()) + 2,
                                    2 + ENV_LEN);
    f[16] = static_cast<char>(crc & 0xFFU);
    f[17] = static_cast<char>((crc >> 8) & 0xFFU);

    f[18] = static_cast<char>(TAIL_HI);
    f[19] = static_cast<char>(TAIL_LO);
    return f;
}

QByteArray FrameProto::buildHistory(quint32 tsRaw, const QByteArray &rawEnvFrame)
{
    QByteArray f(HISTORY_TOTAL, '\0');

    f[0] = static_cast<char>(HEAD_HI);
    f[1] = static_cast<char>(HEAD_LO);
    f[2] = static_cast<char>(CMD_HISTORY);
    f[3] = static_cast<char>(HISTORY_LEN);

    /* 时间戳 4 字节大端：板2 侧原样存的是 ts 字段，补传时按大端搬进载荷 */
    f[4] = static_cast<char>((tsRaw >> 24) & 0xFFU);
    f[5] = static_cast<char>((tsRaw >> 16) & 0xFFU);
    f[6] = static_cast<char>((tsRaw >> 8) & 0xFFU);
    f[7] = static_cast<char>(tsRaw & 0xFFU);

    /* 原始帧整体抄进载荷：不足 20 字节补 0，超出截断（正常情况正好 20 字节） */
    for (int i = 0; i < ENV_TOTAL; ++i) {
        f[4 + HISTORY_RAW_OFF + i] = (i < rawEnvFrame.size())
                                         ? rawEnvFrame.at(i) : static_cast<char>(0);
    }

    /* CRC 范围 = CMD + LEN + 数据段 = 26 字节，低字节在前；帧尾同实时帧 */
    const quint16 crc = crc16Modbus(reinterpret_cast<const quint8 *>(f.constData()) + 2,
                                    2 + HISTORY_LEN);
    f[4 + HISTORY_LEN] = static_cast<char>(crc & 0xFFU);
    f[5 + HISTORY_LEN] = static_cast<char>((crc >> 8) & 0xFFU);
    f[6 + HISTORY_LEN] = static_cast<char>(TAIL_HI);
    f[7 + HISTORY_LEN] = static_cast<char>(TAIL_LO);
    return f;
}

bool FrameProto::unpackHistory(const quint8 *payload, int len, HistoryFrame &out)
{
    if (payload == nullptr) return false;

    /* 只认协议定的 24 字节：长度不符时宁可不入库，也不按偏移硬猜 */
    if (len != HISTORY_LEN) return false;

    out.tsRaw = (static_cast<quint32>(payload[0]) << 24) |
                (static_cast<quint32>(payload[1]) << 16) |
                (static_cast<quint32>(payload[2]) << 8) |
                 static_cast<quint32>(payload[3]);

    out.rawFrame = QByteArray(reinterpret_cast<const char *>(payload + HISTORY_RAW_OFF),
                              ENV_TOTAL);

    /* 复用实时帧的解析：原始帧自身的 20 字节整帧交给 verify()，
     * 校验通过后取它数据段的 12 字节走 EnvFrame::unpack，字段解释与实时帧完全一致 */
    const quint8 *raw = reinterpret_cast<const quint8 *>(out.rawFrame.constData());
    if (verify(raw, ENV_TOTAL) != OK) {
        out.envOk = false;
        return false;
    }

    out.env.cmd = raw[2];
    out.env.len = raw[3];
    out.envOk = EnvFrame::unpack(raw + 4, ENV_LEN, out.env);
    return out.envOk;
}

quint8 FrameProto::verify(const quint8 *buf, int len)
{
    if (buf == nullptr || len < OVERHEAD) return ERR_PARAM;
    if (buf[0] != HEAD_HI || buf[1] != HEAD_LO) return ERR_HEAD;

    const int dataLen = buf[3];
    if (dataLen > MAX_PAYLOAD)  return ERR_LEN;
    if (dataLen + OVERHEAD != len) return ERR_LEN;      /* 缓冲区拼凑长度与 LEN 不一致 */

    const quint16 crc = crc16Modbus(buf + 2, 2 + dataLen);
    if (buf[4 + dataLen] != (crc & 0xFFU) ||
        buf[5 + dataLen] != ((crc >> 8) & 0xFFU)) return ERR_CRC;

    if (buf[6 + dataLen] != TAIL_HI ||
        buf[7 + dataLen] != TAIL_LO) return ERR_TAIL;

    return OK;
}

QByteArray FrameProto::build(quint8 cmd, const QByteArray &payload)
{
    int n = payload.size();
    if (n > MAX_PAYLOAD) n = MAX_PAYLOAD;               /* 截断到协议上限 */

    QByteArray f(OVERHEAD + n, '\0');
    f[0] = static_cast<char>(HEAD_HI);
    f[1] = static_cast<char>(HEAD_LO);
    f[2] = static_cast<char>(cmd);
    f[3] = static_cast<char>(static_cast<quint8>(n));
    for (int i = 0; i < n; ++i) f[4 + i] = payload.at(i);

    const quint16 crc = crc16Modbus(reinterpret_cast<const quint8 *>(f.constData()) + 2,
                                    2 + n);
    f[4 + n] = static_cast<char>(crc & 0xFFU);
    f[5 + n] = static_cast<char>((crc >> 8) & 0xFFU);
    f[6 + n] = static_cast<char>(TAIL_HI);
    f[7 + n] = static_cast<char>(TAIL_LO);
    return f;
}

QString FrameProto::cmdName(quint8 cmd)
{
    switch (cmd) {
    case CMD_ENV:       return QStringLiteral("环境数据上报");
    case CMD_SET_FWD:   return QStringLiteral("设置转发开关");
    case CMD_SET_CACHE: return QStringLiteral("设置缓存开关");
    case CMD_QUERY:     return QStringLiteral("查询板2 状态");
    case CMD_CLR_CACHE: return QStringLiteral("清空 Flash 缓存");
    case CMD_REBOOT:    return QStringLiteral("软复位板2");
    case CMD_HISTORY:   return QStringLiteral("历史帧（板2 补传）");
    case CMD_REPLAY:    return QStringLiteral("开始或停止补传");
    default: break;
    }
    if (isAck(cmd)) {
        const QString base = cmdName(static_cast<quint8>(cmd & ~ACK_FLAG));
        if (!base.startsWith(QStringLiteral("0x"))) return base + QStringLiteral("(应答)");
    }
    return QStringLiteral("0x%1").arg(cmd, 2, 16, QLatin1Char('0')).toUpper();
}

QString FrameProto::statusName(quint8 status)
{
    switch (status) {
    case ST_OK:          return QStringLiteral("OK");
    case ST_PARAM:       return QStringLiteral("参数错");
    case ST_UNSUPPORTED: return QStringLiteral("板2 不支持该命令");
    case ST_BUSY:        return QStringLiteral("板2 忙");
    case 0xFFU:          return QStringLiteral("无应答");
    default: break;
    }
    return QStringLiteral("未知状态 0x%1").arg(status, 2, 16, QLatin1Char('0')).toUpper();
}

QString AckFrame::replayStateName(quint8 state)
{
    switch (state) {
    case FrameProto::RP_IDLE:    return QStringLiteral("空闲");
    case FrameProto::RP_RUNNING: return QStringLiteral("补传中");
    case FrameProto::RP_DONE:    return QStringLiteral("已完成");
    case FrameProto::RP_ERROR:   return QStringLiteral("出错");
    case 0xFFU:                  return QStringLiteral("未知（没有应答）");
    default: break;
    }
    return QStringLiteral("未知状态 0x%1").arg(state, 2, 16, QLatin1Char('0')).toUpper();
}

bool EnvFrame::unpack(const quint8 *payload, int len, EnvFrame &out)
{
    if (payload == nullptr || len < FrameProto::ENV_LEN) return false;

    auto rd16 = [payload](int off) -> quint16 {
        return static_cast<quint16>((static_cast<quint16>(payload[off]) << 8) |
                                    static_cast<quint16>(payload[off + 1]));
    };

    out.tempX100  = static_cast<qint16>(rd16(0));    /* 有符号 */
    out.humiX100  = static_cast<qint16>(rd16(2));    /* 有符号；负值 = 无效标记 */
    out.pressX10  = static_cast<qint16>(rd16(4));
    out.lightLx   = rd16(6);
    out.tvocPpb   = rd16(8);
    out.mq135Raw  = rd16(10);
    return true;
}

/* FrameParser */

FrameParser::FrameParser()
{
    m_buf.reserve(1024);
}

void FrameParser::reset()
{
    m_buf.clear();
    m_ready.clear();
    m_ack.clear();
    m_history.clear();
    m_st = Stats();
}

void FrameParser::feed(const QByteArray &chunk)
{
    if (chunk.isEmpty()) return;
    m_st.bytesIn += static_cast<quint64>(chunk.size());
    m_buf.append(chunk);
    step();
}

void FrameParser::feedByte(quint8 b)
{
    m_st.bytesIn += 1U;
    m_buf.append(static_cast<char>(b));
    step();
}

void FrameParser::dropFront(int n, bool countResync)
{
    if (n <= 0) return;
    if (n > m_buf.size()) n = m_buf.size();
    m_buf.remove(0, n);
    if (countResync) m_st.resyncBytes += static_cast<quint64>(n);
}

void FrameParser::step()
{
    /* 主循环：能推进多少推进多少，凑不齐就留着等下一段字节 */
    for (;;) {
        if (m_buf.size() < 2) return;

        /* 1) 找帧头 0xAA 0x55 */
        int head = -1;
        for (int i = 0; i + 1 < m_buf.size(); ++i) {
            if (static_cast<quint8>(m_buf.at(i))     == FrameProto::HEAD_HI &&
                static_cast<quint8>(m_buf.at(i + 1)) == FrameProto::HEAD_LO) { head = i; break; }
        }
        if (head < 0) {
            /* 无帧头：留最后 1 字节（可能是 0xAA），其余按噪声丢掉 */
            dropFront(m_buf.size() - 1, true);
            return;
        }
        if (head > 0) dropFront(head, true);          /* 帧头之前的噪声丢掉 */

        if (m_buf.size() < 4) return;                 /* 还没读到 LEN */

        const int dataLen = static_cast<quint8>(m_buf.at(3));
        if (dataLen > FrameProto::MAX_PAYLOAD) {
            /* 长度超上限 → 这个帧头是假的，丢 1 字节重新找 */
            ++m_st.errLen;
            dropFront(1, true);
            continue;
        }

        const int total = dataLen + FrameProto::OVERHEAD;
        if (m_buf.size() < total) return;             /* 半包：等更多字节 */

        const quint8 *p = reinterpret_cast<const quint8 *>(m_buf.constData());

        /* 2) CRC16（范围 = CMD + LEN + 数据段；低字节在前） */
        const quint16 crc = FrameProto::crc16Modbus(p + 2, 2 + dataLen);
        if (p[4 + dataLen] != (crc & 0xFFU) ||
            p[5 + dataLen] != ((crc >> 8) & 0xFFU)) {
            ++m_st.errCrc;
            dropFront(1, true);                       /* 只丢 1 字节再重找帧头 */
            continue;
        }

        /* 3) 帧尾 0x55 0xAA */
        if (p[6 + dataLen] != FrameProto::TAIL_HI ||
            p[7 + dataLen] != FrameProto::TAIL_LO) {
            ++m_st.errTail;
            dropFront(1, true);
            continue;
        }

        /* 4) 帧头/CRC/帧尾都通过，按 CMD 分类入队 */
        const quint8 cmd = p[2];
        if (cmd == FrameProto::CMD_ENV && dataLen >= FrameProto::ENV_LEN) {
            EnvFrame f;
            f.cmd = cmd;
            f.len = static_cast<quint8>(dataLen);
            if (EnvFrame::unpack(p + 4, dataLen, f)) {
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (m_st.lastFrameMs > 0) {
                    const qint64 gap = now - m_st.lastFrameMs;
                    m_st.lastGapMs = gap;
                    if (m_st.minGapMs == 0 || gap < m_st.minGapMs) m_st.minGapMs = gap;
                    if (gap > m_st.maxGapMs) m_st.maxGapMs = gap;
                    if (m_lossEnabled && m_st.expectedIntervalMs > 0) {
                        const qint64 limit = (m_st.expectedIntervalMs * 3) / 2;   /* 1.5 倍 */
                        if (gap > limit) {
                            qint64 missed = (gap + m_st.expectedIntervalMs / 2) /
                                            m_st.expectedIntervalMs - 1;
                            if (missed > 0) m_st.lossEstimated += static_cast<quint64>(missed);
                        }
                    }
                }
                m_st.lastFrameMs = now;
                ++m_st.framesOk;
                m_ready.append(f);
            } else {
                ++m_st.framesOtherCmd;
            }
        } else if (cmd == FrameProto::CMD_HISTORY && dataLen == FrameProto::HISTORY_LEN) {
            /* 历史帧：单独入队。这里不更新 lastFrameMs 与丢帧估算，
             * 补传是成批来的，把它算进实时帧间隔会污染丢帧统计。 */
            HistoryFrame h;
            if (FrameProto::unpackHistory(p + 4, dataLen, h)) {
                ++m_st.framesHistory;
                m_history.append(h);
            } else {
                ++m_st.framesOtherCmd;            /* 帧本身合法，但原始帧解不出来 */
            }
        } else if (FrameProto::isAck(cmd)) {
            /* 控制应答帧：单独一个队列（既不是数据，也不算非法命令） */
            AckFrame a;
            a.cmd = cmd;
            a.payload = QByteArray(reinterpret_cast<const char *>(p + 4), dataLen);
            m_ack.append(a);
            ++m_st.framesAck;
        } else {
            ++m_st.framesOtherCmd;                    /* 合法帧但既非环境帧也非应答帧 */
        }

        dropFront(total, false);                      /* 整帧消费掉 */
    }
}

bool FrameParser::takeFrame(EnvFrame &out)
{
    if (m_ready.isEmpty()) return false;
    out = m_ready.takeFirst();
    return true;
}

bool FrameParser::takeAck(AckFrame &out)
{
    if (m_ack.isEmpty()) return false;
    out = m_ack.takeFirst();
    return true;
}

bool FrameParser::takeHistory(HistoryFrame &out)
{
    if (m_history.isEmpty()) return false;
    out = m_history.takeFirst();
    return true;
}

QByteArray FrameParser::bufferedHex() const
{
    return m_buf.toHex(' ').toUpper();
}

QVector<EnvFrame> FrameParser::parseAll(const QByteArray &stream, Stats *outStats,
                                        bool lossEstimate)
{
    FrameParser p;
    p.setLossEstimateEnabled(lossEstimate);
    p.feed(stream);

    QVector<EnvFrame> out;
    EnvFrame f;
    while (p.takeFrame(f)) out.append(f);
    if (outStats != nullptr) *outStats = p.stats();
    return out;
}
