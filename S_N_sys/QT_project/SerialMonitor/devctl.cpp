#include "devctl.h"

#include <QSerialPort>
#include <QDateTime>

/* DevCtl：下发命令 + 等应答 + 超时重发 */

DevCtl::DevCtl(QObject *parent) : QObject(parent)
{
}

void DevCtl::attach(QSerialPort *port)
{
    m_port = port;
    m_queue.clear();          /* 换串口或重开串口：在途命令作废，应答不可能再回来 */
}

void DevCtl::detach()
{
    m_port = nullptr;
    m_queue.clear();
}

QString DevCtl::hex(const QByteArray &b)
{
    return QString::fromLatin1(b.toHex(' ')).toUpper();
}

bool DevCtl::enqueue(quint8 cmd, const QByteArray &payload, const QString &label)
{
    if (m_port == nullptr) {
        emit logText(QStringLiteral("下发「%1」失败：串口没打开").arg(label));
        return false;
    }

    Pending p;
    p.cmd   = cmd;
    p.frame = FrameProto::build(cmd, payload);
    p.label = label;

    m_queue.enqueue(p);

    /* 只在队列里原本没有在途命令时才立刻发；
     * 否则等上一条超时或应答后由 popFront() 带出下一条（见头文件"一条在途"说明） */
    if (m_queue.size() == 1) writeFrame(m_queue.head());
    else emit logText(QStringLiteral("「%1」已排队（前面还有 %2 条）")
                              .arg(label).arg(m_queue.size() - 1));
    return true;
}

void DevCtl::writeFrame(Pending &p)
{
    if (m_port == nullptr) return;

    m_port->write(p.frame);
    p.sentMs = QDateTime::currentMSecsSinceEpoch();
    p.tries += 1;
    m_txCount += 1;

    emit logText(QStringLiteral("下发 %1 %2 (第 %3 次)  %4")
                     .arg(p.label,
                          QStringLiteral("%1").arg(p.cmd, 2, 16, QLatin1Char('0')).toUpper())
                     .arg(p.tries)
                     .arg(hex(p.frame)));
}

void DevCtl::popFront()
{
    if (m_queue.isEmpty()) return;
    m_queue.dequeue();
    if (!m_queue.isEmpty()) writeFrame(m_queue.head());   /* 带出下一条 */
}

bool DevCtl::setForward(bool on)
{
    return enqueue(FrameProto::CMD_SET_FWD, QByteArray(1, static_cast<char>(on ? 1 : 0)),
                   on ? QStringLiteral("打开转发") : QStringLiteral("关闭转发"));
}

bool DevCtl::setCache(bool on)
{
    return enqueue(FrameProto::CMD_SET_CACHE, QByteArray(1, static_cast<char>(on ? 1 : 0)),
                   on ? QStringLiteral("打开缓存") : QStringLiteral("关闭缓存"));
}

bool DevCtl::query()
{
    return enqueue(FrameProto::CMD_QUERY, QByteArray(), QStringLiteral("查询板2 状态"));
}

bool DevCtl::clearCache()
{
    return enqueue(FrameProto::CMD_CLR_CACHE, QByteArray(), QStringLiteral("清空 Flash 缓存"));
}

bool DevCtl::reboot()
{
    return enqueue(FrameProto::CMD_REBOOT, QByteArray(), QStringLiteral("软复位板2"));
}

bool DevCtl::setReplay(bool on)
{
    /* 0x16 载荷 1 字节：1 = 开始补传、0 = 停止补传 */
    return enqueue(FrameProto::CMD_REPLAY, QByteArray(1, static_cast<char>(on ? 1 : 0)),
                   on ? QStringLiteral("开始补传") : QStringLiteral("停止补传"));
}

void DevCtl::tick()
{
    if (m_port == nullptr || m_queue.isEmpty()) return;

    Pending &p = m_queue.head();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (p.sentMs <= 0 || now - p.sentMs < TIMEOUT_MS) return;

    if (p.tries < MAX_TRIES) {
        emit logText(QStringLiteral("「%1」超过 %2 ms 没应答，重发")
                         .arg(p.label).arg(TIMEOUT_MS));
        writeFrame(p);
        return;
    }

    /* 三次都不回：报错并放行下一条，避免队列永久堵住 */
    const quint8 cmd = p.cmd;
    const QString label = p.label;
    m_timeoutCount += 1;
    m_queue.dequeue();
    emit logText(QStringLiteral("「%1」%2 次无应答 —— 依次查：①串口线是否接在板2 的 USART2"
                                "（P6 跳线在 SP3232 侧）②板2 是否在跑（有没有打 CmdRx 统计）"
                                "③波特率 115200 8N1")
                     .arg(label).arg(MAX_TRIES));
    emit commandFailed(cmd, QStringLiteral("无应答"));
    if (!m_queue.isEmpty()) writeFrame(m_queue.head());
}

void DevCtl::onAck(const AckFrame &a)
{
    m_rxCount += 1;
    emit ackReceived(a);

    /* 找对应的在途命令：协议没有事务号，只能按 CMD 去掉标志位匹配。
     * 队首不是它时（上一条刚超时放弃、这条应答才到）不影响解析：
     * 应答自带完整状态，照样显示，只是不计入这次成功。 */
    const bool match = (!m_queue.isEmpty() && m_queue.head().cmd == a.reqCmd());

    emit logText(QStringLiteral("应答 %1（%2）%3")
                     .arg(QStringLiteral("%1").arg(a.cmd, 2, 16, QLatin1Char('0')).toUpper(),
                          FrameProto::cmdName(a.cmd),
                          FrameProto::statusName(a.status()))
                     .arg(hex(a.payload)));

    handleStatusAck(a, match ? &m_queue.head() : nullptr);

    if (match) popFront();
}

void DevCtl::handleStatusAck(const AckFrame &a, const Pending *p)
{
    const quint8 st = a.status();

    switch (a.reqCmd()) {
    case FrameProto::CMD_QUERY:
        if (a.payload.size() < FrameProto::QUERY_ACK_LEN) {
            emit logText(QStringLiteral("查询应答只有 %1 字节（期望 %2），可能板2 固件版本旧")
                             .arg(a.payload.size()).arg(FrameProto::QUERY_ACK_LEN));
            return;
        }
        m_last.valid      = true;
        m_last.fwd        = a.fwdOn();
        m_last.cache      = a.cacheOn();
        m_last.rxOk       = a.rxOk();
        m_last.rxBad      = a.rxBad();
        m_last.cacheCount = a.cacheCount();
        m_last.lost       = a.lostCount();
        m_last.uptimeS    = a.uptimeS();
        emit statusChanged(m_last);
        break;

    case FrameProto::CMD_SET_FWD:
    case FrameProto::CMD_SET_CACHE:
        if (st == FrameProto::ST_OK && a.payload.size() > FrameProto::ACK_VAL_OFF) {
            const bool on = a.u8(FrameProto::ACK_VAL_OFF) != 0U;
            if (a.reqCmd() == FrameProto::CMD_SET_FWD) m_last.fwd = on;
            else                                       m_last.cache = on;
            emit statusChanged(m_last);
        }
        break;

    case FrameProto::CMD_CLR_CACHE:
        if (st == FrameProto::ST_OK) {
            m_last.cacheCount = a.u16(FrameProto::ACK_CNT_OFF);
            m_last.lost = 0;
            emit logText(QStringLiteral("板2 Flash 缓存已清空，清后条数 %1")
                             .arg(m_last.cacheCount));
            emit statusChanged(m_last);
        }
        break;

    case FrameProto::CMD_REBOOT:
        if (st == FrameProto::ST_OK) {
            emit logText(QStringLiteral("板2 已接受软复位（应答先回、100 ms 后才复位）"));
            m_last.valid = false;          /* 复位后状态未知，等下次查询 */
        }
        break;

    case FrameProto::CMD_REPLAY:
        /* 0x96 应答的数据段第 1 字节不是通用状态码而是补传状态（0 空闲/1 补传中/
         * 2 已完成/3 出错），所以不在这里按 statusName 解释，单独走一条处理 */
        handleReplayAck(a);
        return;                            /* 已处理完，跳过下面的通用"拒绝"判断 */

    default:
        break;
    }

    if (st != FrameProto::ST_OK && st != 0xFFU) {
        emit logText(QStringLiteral("板2 拒绝「%1」：%2")
                         .arg(p != nullptr ? p->label : FrameProto::cmdName(a.cmd),
                              FrameProto::statusName(st)));
    }
}

void DevCtl::handleReplayAck(const AckFrame &a)
{
    if (a.payload.size() < FrameProto::REPLAY_ACK_LEN) {
        emit logText(QStringLiteral("补传应答只有 %1 字节（期望 %2），可能板2 固件版本旧")
                         .arg(a.payload.size()).arg(FrameProto::REPLAY_ACK_LEN));
        return;
    }

    m_replay.valid      = true;
    m_replay.state      = a.replayState();
    m_replay.sent       = a.replayedNum();
    m_replay.rest       = a.replayRest();
    m_replay.restCapped = a.replayRestCapped();
    m_replay.atMs       = QDateTime::currentMSecsSinceEpoch();
    emit replayChanged(m_replay);
}
