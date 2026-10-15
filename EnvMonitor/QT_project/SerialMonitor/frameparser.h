#ifndef FRAMEPARSER_H
#define FRAMEPARSER_H

/* ============================================================================
 *  frameparser.h —— 环境帧/控制应答帧的解析（缓冲区 + 状态机 + CRC16-MODBUS）
 *  协议权威定义：Project_1_检测端 Project/FWLIB/inc/sys_frame.h；
 *  判据与 board1 main.c frame_build()、board2 main.c vTaskForward() 一致。
 *  环境帧 CMD=0x01，整帧 20 字节：
 *      [0][1]=AA 55  [2]=CMD  [3]=LEN=0x0C
 *      [4..15] 数据段，大端：TEMP×100 / HUMI×100 / PRESS×10 / LIGHT / TVOC / MQ135
 *      [16][17]=CRC16-MODBUS 低字节在前，范围 [2..15] 共 14 字节  [18][19]=55 AA
 *  必须"帧头 + 长度 + CRC"三重确认：帧尾与下一帧帧头在接缝处拼成 55 AA AA 55，
 *  只认 0xAA55 会把接缝当帧头（sys_frame.h 有同样说明）。
 *  校验失败只丢 1 字节再重找帧头：丢整帧长度会在上述接缝处连续丢帧。
 *  帧内无序号字段，丢帧数只能按到达间隔估算（>1.5×期望间隔时取 round(间隔/期望)-1）。
 *  协议细节与丢帧估算的取舍见《Qt上位机设计.md》。
 * ==========================================================================*/

#include <QByteArray>
#include <QVector>
#include <QString>
#include <QtGlobal>

/* 历史帧解析结果（定义在下方 EnvFrame 之后）。这里先声明：
 * unpackHistory 的声明只需要它的引用，因此不必把定义提前。 */
struct HistoryFrame;

namespace FrameProto {

/* 帧头 / 帧尾（帧尾与帧头互为反序，协议规定） */
constexpr quint8 HEAD_HI = 0xAAU;
constexpr quint8 HEAD_LO = 0x55U;
constexpr quint8 TAIL_HI = 0x55U;
constexpr quint8 TAIL_LO = 0xAAU;

/* 固定开销 = 帧头 2 + CMD 1 + LEN 1 + CRC 2 + 帧尾 2 */
constexpr int OVERHEAD = 8;

constexpr quint8 CMD_ENV = 0x01U;
constexpr quint8 ENV_LEN = 0x0CU;
constexpr int ENV_TOTAL = ENV_LEN + OVERHEAD;   /* 20 */
constexpr int MAX_PAYLOAD = 64;                 /* 与 sys_frame.h 的 SYS_FRAME_MAX_PAYLOAD 一致 */

/* ---- 控制帧（上位机 → 板2；板2 → 上位机应答）--------------------------------
 *  与板2 FWLIB/inc/sys_frame.h 的控制帧区块逐字对应，改一处要同步改三处。
 *  应答帧 CMD = 请求 CMD | ACK_FLAG，数据段第 1 字节恒为状态码。 */
constexpr quint8 ACK_FLAG        = 0x80U;   /* 应答标志位 */
constexpr quint8 CMD_SET_FWD     = 0x10U;   /* 载荷 1 字节：板2 转发开关 */
constexpr quint8 CMD_SET_CACHE   = 0x11U;   /* 载荷 1 字节：板2 缓存开关 */
constexpr quint8 CMD_QUERY       = 0x12U;   /* 载荷 0 字节：查板2 状态 */
constexpr quint8 CMD_CLR_CACHE   = 0x13U;   /* 载荷 0 字节：清空板2 Flash 缓存 */
constexpr quint8 CMD_REBOOT      = 0x14U;   /* 载荷 0 字节：软复位板2 */
constexpr quint8 CMD_HISTORY     = 0x15U;   /* 载荷 24 字节：历史帧（板2 → 上位机） */
constexpr quint8 CMD_REPLAY      = 0x16U;   /* 载荷 1 字节：开始/停止补传（上位机 → 板2） */
constexpr quint8 CMD_REPLAY_ACK  = 0x96U;   /* 载荷 4 字节：补传进度应答（板2 → 上位机） */

/* 状态码（应答数据段第 1 字节） */
constexpr quint8 ST_OK           = 0U;
constexpr quint8 ST_PARAM        = 1U;      /* 载荷长度/取值非法 */
constexpr quint8 ST_UNSUPPORTED  = 2U;      /* 板2 不认识这个命令 */
constexpr quint8 ST_BUSY         = 3U;      /* 板2 正忙（例：清缓存失败） */

/* 历史帧（CMD=0x15）载荷布局，24 字节：
 *   [0..3]  原始时间戳，大端 32 位，格式沿用板2 的 hh*10000 + mm*100 + ss
 *   [4..23] 原始环境帧整帧 20 字节（含帧头/CRC/帧尾，与实时帧逐字节相同）
 * 时间戳放在数据段最前，原始帧整体在后，因此原始帧相对载荷首字节的偏移固定为 4。 */
constexpr int    HISTORY_LEN     = 24;      /* 历史帧数据段长度 */
constexpr int    HISTORY_TS_LEN  = 4;       /* 时间戳字段长度 */
constexpr int    HISTORY_RAW_OFF = 4;       /* 原始环境帧在数据段里的偏移 */
constexpr int    HISTORY_TOTAL   = HISTORY_LEN + 8;   /* 整帧 32 字节 */

/* 补传状态（CMD=0x16 命令的两种取值，也是 0x96 应答数据段第 1 字节的语义） */
constexpr quint8 REPLAY_STOP     = 0U;      /* 停止补传 */
constexpr quint8 REPLAY_START    = 1U;      /* 开始补传 */

constexpr quint8 RP_IDLE         = 0U;      /* 空闲 */
constexpr quint8 RP_RUNNING      = 1U;      /* 补传中 */
constexpr quint8 RP_DONE         = 2U;      /* 已完成 */
constexpr quint8 RP_ERROR        = 3U;      /* 出错 */

/* REPLAY 应答（0x96）数据段布局，4 字节 */
constexpr int    REPLAY_ACK_LEN  = 4;
constexpr int    RA_ST           = 0;       /* [0]      补传状态，取值 RP_* */
constexpr int    RA_SENT         = 1;       /* [1..2]   已补传条数，大端 16 位 */
constexpr int    RA_REST         = 3;       /* [3]      剩余条数低字节 */

/* QUERY 应答的数据段布局（16 字节，多字节一律大端，与环境帧同约定） */
constexpr int    QUERY_ACK_LEN   = 16;
constexpr int    QA_ST           = 0;    /* [0]      状态码 */
constexpr int    QA_FWD          = 1;    /* [1]      转发开关 0/1 */
constexpr int    QA_CACHE        = 2;    /* [2]      缓存开关 0/1 */
constexpr int    QA_RXOK         = 4;    /* [4..5]   LoRa 收到并校验通过的帧数 */
constexpr int    QA_RXBAD        = 6;    /* [6..7]   LoRa 校验失败的帧数 */
constexpr int    QA_CACHECNT     = 8;    /* [8..9]  当前缓存条数 */
constexpr int    QA_LOST         = 10;   /* [10..11] 因整扇区擦除被连带丢弃的条数 */
constexpr int    QA_UPTIME       = 12;   /* [12..15] 板2 已运行秒数 */

/* 变值/计数应答的载荷偏移 */
constexpr int    ACK_VAL_OFF     = 1;    /* SET_FWD/SET_CACHE 应答：[1] = 新值 */
constexpr int    ACK_CNT_OFF     = 1;    /* CLR_CACHE 应答：[1..2] = 清后条数 */

/* 整帧校验返回码（与 SYS_FRAME_* 一一对应，便于对照固件日志） */
enum VerifyCode {
    OK = 0, ERR_HEAD = 1, ERR_TAIL = 2, ERR_LEN = 3, ERR_CRC = 4, ERR_PARAM = 5
};

/* CRC16-MODBUS：初值 0xFFFF、多项式 0xA001（反射）、无最终异或
 * 与板1 SYS_MODBUS_Crc16 / 板2 MODBUS_CRC16 同算法 */
quint16 crc16Modbus(const quint8 *buf, int len);

/* 组一帧环境数据（与板1 frame_build() 等价；自检/回放文件生成用）
 * 返回整帧字节（ENV_TOTAL）；参数越界会被截断为 16 位，不做静默取整 */
QByteArray buildEnv(qint16 tempX100, qint16 humiX100, qint16 pressX10,
                    quint16 lightLx, quint16 tvocPpb, quint16 mq135Raw);

/* 校验"一整帧"（缓冲区里已是完整帧时用；与 SYS_FRAME_Verify 同判据） */
quint8 verify(const quint8 *buf, int len);

/* 历史帧的解析结果结构体定义见下方 EnvFrame 之后（unpackHistory 只用它的引用，
 * 函数声明不需要完整类型） */

/* 组一帧历史数据（整帧 HISTORY_TOTAL 字节）。参数 1 是原始时间戳（大端写入），
 * 参数 2 是原始环境帧整帧 20 字节（含帧头/CRC/帧尾，原样搬进载荷后 20 字节）；
 * CRC 与帧尾走与实时帧相同的规则。供板2 侧实现对照与上位机自检使用。 */
QByteArray buildHistory(quint32 tsRaw, const QByteArray &rawEnvFrame);

/* 解析历史帧数据段（长度必须是 HISTORY_LEN）。
 * 六项物理量复用环境帧解析：把数据段后 20 字节当成一帧原始帧，取其中的数据段
 * 再走 EnvFrame::unpack，实时帧与历史帧因此共用同一套字段解释，不存在两套。
 * 返回 false = 数据段长度不对或原始帧本身解析失败。 */
bool unpackHistory(const quint8 *payload, int len, HistoryFrame &out);

/* 组任意一帧（控制帧下发用）。payload 超 MAX_PAYLOAD 会被截断。
 * 与固件 SYS_FRAME_Build() 等价：返回整帧 = OVERHEAD + payload 字节 */
QByteArray build(quint8 cmd, const QByteArray &payload = QByteArray());

/* 命令名 / 状态码中文名（日志与界面用；未知值返回 "0xNN"） */
QString cmdName(quint8 cmd);
QString statusName(quint8 status);

/* 是不是应答帧（CMD 最高位 = ACK_FLAG） */
inline bool isAck(quint8 cmd) { return (cmd & ACK_FLAG) != 0U; }

}   // namespace FrameProto

/* ---------------------------------------------------------------------------
 *  解析结果：一帧环境数据
 * -------------------------------------------------------------------------*/
struct EnvFrame {
    quint8  cmd = 0;
    quint8  len = 0;
    qint16  tempX100 = 0;
    qint16  humiX100 = 0;     /* 可能为负 = 调用方的"无效标记" */
    qint16  pressX10 = 0;
    quint16 lightLx = 0;
    quint16 tvocPpb = 0;
    quint16 mq135Raw = 0;
    quint8  nodeId = 0x01;    /* 预留：多节点组网，当前固定 0x01 */

    double tempC()    const { return tempX100 / 100.0; }
    double humiRh()   const { return humiX100 / 100.0; }
    double pressHpa() const { return pressX10 / 10.0; }
    bool   humiValid() const { return humiX100 >= 0; }

    static bool unpack(const quint8 *payload, int len, EnvFrame &out);
};

/* ---------------------------------------------------------------------------
 *  解析结果：一帧历史数据（板2 补传，CMD=0x15）
 *  时间戳是数据在板1 采集时的原始时刻，不是到达上位机的时刻；
 *  原始帧是板2 缓存里存下来的那 20 字节整帧，与环境帧逐字节相同，
 *  所以物理量可以与实时帧走同一套解析（内部直接调用 EnvFrame::unpack）。
 * -------------------------------------------------------------------------*/
struct HistoryFrame {
    quint32      tsRaw = 0;      /* 原始时间戳，大端读出，格式 hh*10000+mm*100+ss */
    QByteArray   rawFrame;       /* 原始环境帧整帧 20 字节 */
    EnvFrame     env;            /* 原始帧解析出的六项物理量 */
    bool         envOk = false;  /* 原始帧本身是否解析成功 */

    bool valid() const { return rawFrame.size() == FrameProto::ENV_TOTAL && envOk; }
};

/* ---------------------------------------------------------------------------
 *  解析结果：一帧控制应答（板2 → 上位机）
 *  约定：应答的 CMD = 请求 CMD | 0x80，payload[0] 恒为状态码
 * -------------------------------------------------------------------------*/
struct AckFrame {
    quint8     cmd = 0;          /* 原始应答 CMD（含 ACK_FLAG） */
    QByteArray payload;          /* 数据段（不含 CMD/LEN/CRC/帧尾） */

    /* 对应的请求命令（去掉 ACK_FLAG）；例 0x92 → 0x12 QUERY */
    quint8 reqCmd() const { return static_cast<quint8>(cmd & ~FrameProto::ACK_FLAG); }
    bool   valid()  const { return !payload.isEmpty(); }
    quint8 status() const { return payload.isEmpty() ? 0xFFU
                                                     : static_cast<quint8>(payload.at(0)); }

    /* 大端读（越界返回 0；调用方先查 size() 或用 valid()） */
    quint8  u8(int off)  const { return (off >= 0 && off < payload.size())
                                        ? static_cast<quint8>(payload.at(off)) : 0U; }
    quint16 u16(int off) const {
        return static_cast<quint16>((static_cast<quint16>(u8(off)) << 8) |
                                     static_cast<quint16>(u8(off + 1)));
    }
    quint32 u32(int off) const {
        return (static_cast<quint32>(u8(off)) << 24) |
               (static_cast<quint32>(u8(off + 1)) << 16) |
               (static_cast<quint32>(u8(off + 2)) << 8) |
                static_cast<quint32>(u8(off + 3));
    }

    /* 语义化取值（QUERY 应答用；缺字节时返回 0/false） */
    bool    fwdOn()      const { return u8(FrameProto::QA_FWD) != 0U; }
    bool    cacheOn()    const { return u8(FrameProto::QA_CACHE) != 0U; }
    quint16 rxOk()       const { return u16(FrameProto::QA_RXOK); }
    quint16 rxBad()      const { return u16(FrameProto::QA_RXBAD); }
    quint16 cacheCount() const { return u16(FrameProto::QA_CACHECNT); }
    quint16 lostCount()  const { return u16(FrameProto::QA_LOST); }
    quint32 uptimeS()    const { return u32(FrameProto::QA_UPTIME); }

    /* 语义化取值（REPLAY 应答 0x96 用）
     * 剩余条数协议只给了低字节，超过 255 条时按 255 封顶，
     * 界面据此显示"剩余 255 条以上"而不是把 256 条显示成 0 条。 */
    quint8  replayState() const { return u8(FrameProto::RA_ST); }
    quint16 replayedNum() const { return u16(FrameProto::RA_SENT); }
    int     replayRest()  const {
        return (payload.size() >= FrameProto::REPLAY_ACK_LEN)
                   ? qMin(255, static_cast<int>(u8(FrameProto::RA_REST))) : 0;
    }
    bool    replayRestCapped() const {
        return payload.size() >= FrameProto::REPLAY_ACK_LEN &&
               u8(FrameProto::RA_REST) == 0xFFU;
    }

    /* 补传状态的中文名（界面与日志用） */
    static QString replayStateName(quint8 state);
};

/* ---------------------------------------------------------------------------
 *  帧解析器：喂字节 → 取出校验通过的帧
 *  非 QObject（不需要 moc），自检程序可直接链接本文件
 * -------------------------------------------------------------------------*/
class FrameParser
{
public:
    struct Stats {
        quint64 bytesIn = 0;          /* 累计喂入字节 */
        quint64 framesOk = 0;         /* 校验通过的环境帧 */
        quint64 framesAck = 0;        /* 校验通过的控制应答帧（CMD 带 0x80 标志） */
        quint64 framesHistory = 0;    /* 校验通过的历史帧（CMD=0x15，板2 补传） */
        quint64 framesOtherCmd = 0;   /* 校验通过但既非环境帧、历史帧也非应答帧 */
        quint64 errCrc = 0;           /* CRC 错（比特错 / 丢字节） */
        quint64 errLen = 0;           /* 长度字段超上限 */
        quint64 errTail = 0;          /* 帧尾不对 */
        quint64 resyncBytes = 0;      /* 为重新同步丢掉的字节数（含噪声与残帧） */
        quint64 lossEstimated = 0;    /* 按到达间隔估算的丢帧数 */
        qint64  lastFrameMs = 0;      /* 上一帧到达时刻（ms） */
        qint64  lastGapMs = 0;        /* 上一帧与前一帧的间隔（ms） */
        qint64  minGapMs = 0;
        qint64  maxGapMs = 0;
        int     expectedIntervalMs = 1000;   /* 期望帧间隔（板1 采集周期 1 s） */
    };

    FrameParser();

    void reset();                                   /* 清空缓冲、待取队列与统计 */
    void feed(const QByteArray &chunk);             /* 喂入一段字节并推进状态机 */
    void feedByte(quint8 b);

    /* 取一帧环境数据（已校验）；返回 false = 暂时没有 */
    bool takeFrame(EnvFrame &out);
    int  pendingFrames() const { return m_ready.size(); }

    /* 取一帧控制应答（已校验）；返回 false = 暂时没有
     * 与环境帧分两个队列：环境帧是数据，应答帧是对下发命令的回话 */
    bool takeAck(AckFrame &out);
    int  pendingAcks() const { return m_ack.size(); }

    /* 取一帧历史数据（已校验）；返回 false = 暂时没有
     * 单独一个队列：历史帧要入的是数据模型的历史通道，不能混进实时帧队列，
     * 否则收帧端一个 while 循环就把历史数据当实时数据处理了。 */
    bool takeHistory(HistoryFrame &out);
    int  pendingHistory() const { return m_history.size(); }

    const Stats &stats() const { return m_st; }
    Stats       &statsRef()    { return m_st; }

    void setLossEstimateEnabled(bool on) { m_lossEnabled = on; }
    bool lossEstimateEnabled() const     { return m_lossEnabled; }

    /* 调试用：缓冲区里还没成帧的字节数 / 内容（十六进制） */
    int       buffered() const { return m_buf.size(); }
    QByteArray bufferedHex() const;

    /* 一次性解析整段字节流（自检 / 回放用） */
    static QVector<EnvFrame> parseAll(const QByteArray &stream,
                                      Stats *outStats = nullptr,
                                      bool lossEstimate = false);

private:
    void step();                    /* 缓冲区里能推进多少就推进多少 */
    void dropFront(int n, bool countResync);

    QByteArray           m_buf;
    QVector<EnvFrame>    m_ready;
    QVector<AckFrame>    m_ack;
    QVector<HistoryFrame> m_history;
    Stats                m_st;
    bool                 m_lossEnabled = true;
};

#endif  /* FRAMEPARSER_H */
