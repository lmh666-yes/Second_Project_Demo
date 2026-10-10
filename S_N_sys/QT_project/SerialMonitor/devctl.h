#ifndef DEVCTL_H
#define DEVCTL_H

/* ============================================================================
 *  devctl.h —— 上位机对板2 的下发控制通道（请求/应答 + 超时重发）
 *  单独成类：串口下发是"发出 → 等应答 → 超时重发 → 三次不回报错"的状态机，
 *  与界面无关，抽出后可被 --cmdselftest 直接跑（不开窗、不接板子）。
 *  协议（与板2 FWLIB/inc/sys_frame.h 控制帧区块一致）：
 *   请求 AA 55 <CMD> 01 <值> CRC 55 AA，CMD ∈ {0x10 SET_FWD, 0x11 SET_CACHE, 0x16 REPLAY}；
 *        AA 55 <CMD> 00 CRC 55 AA，CMD ∈ {0x12 QUERY, 0x13 CLR_CACHE, 0x14 REBOOT}；
 *   应答 AA 55 <CMD|0x80> <LEN> <状态> ... CRC 55 AA。
 *   板2 上行还有 0x15 历史帧（由 FrameParser 单独入队，不走本类）。
 *  只允许一条在途命令：应答里没有序号，两条同时在途分不清回的是哪条；要并发
 *  须给协议加 1 字节事务号。超时 600 ms × 3 次：串口 115200、板2 命令任务
 *  20 ms 轮询，正常往返 <50 ms，600 ms 约 10 倍余量，再长会让未接板子时点按钮
 *  等很久才报错。
 * ==========================================================================*/

#include <QObject>
#include <QByteArray>
#include <QQueue>
#include <QString>

#include "frameparser.h"

class QSerialPort;

/* 板2 状态快照（QUERY 应答解析结果；valid=false 表示还没查到过） */
struct DevStatus {
    bool    valid      = false;
    bool    fwd        = false;   /* 转发到本机开关 */
    bool    cache      = false;   /* 落 W25Q 缓存开关 */
    quint16 rxOk       = 0;       /* LoRa 收帧成功数 */
    quint16 rxBad      = 0;       /* LoRa 收帧失败数 */
    quint16 cacheCount = 0;       /* Flash 缓存条数 */
    quint16 lost       = 0;       /* 缓存溢出被丢弃条数 */
    quint32 uptimeS    = 0;       /* 板2 运行秒数 */
};

/* 补传状态快照（REPLAY 应答 0x96 解析结果；valid=false 表示还没查到过）
 * 剩余条数协议只给低字节，restCapped=true 表示实际剩余不少于 255 条 */
struct ReplayStatus {
    bool    valid      = false;
    quint8  state      = FrameProto::RP_IDLE;   /* RP_IDLE/RP_RUNNING/RP_DONE/RP_ERROR */
    quint16 sent       = 0;      /* 已补传条数 */
    int     rest       = 0;      /* 剩余条数（0..254，或 255 表示"255 以上"） */
    bool    restCapped = false;
    qint64  atMs       = 0;      /* 收到这帧应答的时刻 */
};

class DevCtl : public QObject
{
    Q_OBJECT

public:
    static const int TIMEOUT_MS = 600;   /* 单次等应答上限 */
    static const int MAX_TRIES  = 3;     /* 含首发，最多发 3 次 */

    explicit DevCtl(QObject *parent = nullptr);

    /* 串口打开/关闭时调用。attach 后才会真正写串口 */
    void attach(QSerialPort *port);
    void detach();
    bool attached() const { return m_port != nullptr; }

    /* ---- 下发接口（返回 false = 串口没开，命令没进队列）---- */
    bool setForward(bool on);
    bool setCache(bool on);
    bool query();
    bool clearCache();
    bool reboot();

    /* 开始/停止补传（0x16，载荷 1 字节：1 开始、0 停止）
     * 与其它下行命令共用同一套超时与重发：600 ms 无应答重发，三次不报失败 */
    bool startReplay()    { return setReplay(true); }
    bool stopReplay()     { return setReplay(false); }
    bool setReplay(bool on);

    /* 主窗口把 FrameParser 取出的应答转进来 */
    void onAck(const AckFrame &a);

    /* 周期调用（主窗口 50 ms 的曲线定时器里一起调）：超时重发/放弃 */
    void tick();

    /* 统计（左栏解析统计面板显示） */
    quint64 txCount()      const { return m_txCount; }
    quint64 rxCount()      const { return m_rxCount; }
    quint64 timeoutCount() const { return m_timeoutCount; }
    int     pendingCount() const { return m_queue.size(); }
    const DevStatus &lastStatus() const { return m_last; }
    const ReplayStatus &lastReplay() const { return m_replay; }

signals:
    void logText(const QString &line);            /* 往界面 hex 日志里写一行 */
    void statusChanged(const DevStatus &st);      /* QUERY 成功后 */
    void replayChanged(const ReplayStatus &st);   /* REPLAY 应答（0x96）成功后 */
    void ackReceived(const AckFrame &a);          /* 任何合法应答 */
    void commandFailed(quint8 reqCmd, const QString &reason);

private:
    struct Pending {
        quint8     cmd = 0;        /* 请求命令码（不含 ACK 标志） */
        QByteArray frame;          /* 完整请求帧（重发直接照抄，不重新组帧） */
        QString    label;          /* 中文名，日志用 */
        int        tries = 0;      /* 已发出次数 */
        qint64     sentMs = 0;     /* 最后一次发出的时刻 */
    };

    bool enqueue(quint8 cmd, const QByteArray &payload, const QString &label);
    void writeFrame(Pending &p);
    void popFront();
    void handleStatusAck(const AckFrame &a, const Pending *p);
    void handleReplayAck(const AckFrame &a);          /* 0x96：只更新补传进度快照 */
    static QString hex(const QByteArray &b);

    QSerialPort *m_port = nullptr;
    QQueue<Pending> m_queue;
    quint64 m_txCount = 0;
    quint64 m_rxCount = 0;
    quint64 m_timeoutCount = 0;
    DevStatus m_last;
    ReplayStatus m_replay;
};

#endif  /* DEVCTL_H */
