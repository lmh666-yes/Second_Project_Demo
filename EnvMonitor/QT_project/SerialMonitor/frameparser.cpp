/* ============================================================================
 *  frameparser.cpp —— 【本文件由用户自己实现】
 *
 *  这个文件是留给你自己的实现起点，不是参考实现：
 *  协议常量、结构体字段与全部函数签名都在 frameparser.h 里，
 *  本文件只保留每个函数的空桩，以及一行"这一步要做什么"的提示。
 *  实现清单 = frameparser.h 里的全部声明，逐条对照补完即可。
 *
 *  学习路线：标准库速查 → 项目引导 → Qt
 *  对应阶段：阶段 2「协议解析与数据模型」
 *           （帧结构 / CRC16-MODBUS / 缓冲区状态机 / 历史帧解析都在这一阶段）
 *
 *  参考实现备份（自己写完再对照，不要照抄一遍就交）：
 *      D:\GitHub_Project\_参考实现备份\20261012_195704\Qt\
 *
 *  编码：UTF-8 无 BOM、LF。
 * ==========================================================================*/

/* 下面是"桩函数参数暂时用不到"的告警抑制：等你把函数实现了，把这三行删掉即可。 */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "frameparser.h"
#include <QDateTime>

/* ===========================================================================
 *  FrameProto —— 协议工具函数（CRC / 组帧 / 整帧校验 / 历史帧 / 名字表）
 * =========================================================================*/

/* TODO(阶段2)：按 CRC16-MODBUS（初值 0xFFFF、多项式 0xA001、无最终异或）逐字节累加 */
quint16 FrameProto::crc16Modbus(const quint8 *buf, int len)
{
    return 0;
}

/* TODO(阶段2)：按环境帧 20 字节布局把 6 项数据按大端装进数据段，再补 CRC 与帧尾 */
QByteArray FrameProto::buildEnv(qint16 tempX100, qint16 humiX100, qint16 pressX10,
                                quint16 lightLx, quint16 tvocPpb, quint16 mq135Raw)
{
    return {};
}

/* TODO(阶段2)：对一整帧做"帧头 + 长度 + CRC + 帧尾"四重判定，返回 VerifyCode */
quint8 FrameProto::verify(const quint8 *buf, int len)
{
    return 0;
}

/* TODO(阶段2)：把时间戳与原始 20 字节整帧拼成 CMD=0x15 的历史帧，CRC 与帧尾同实时帧 */
QByteArray FrameProto::buildHistory(quint32 tsRaw, const QByteArray &rawEnvFrame)
{
    return {};
}

/* TODO(阶段2)：从历史帧数据段取出时间戳，并把后 20 字节交给 EnvFrame::unpack 解析 */
bool FrameProto::unpackHistory(const quint8 *payload, int len, HistoryFrame &out)
{
    return false;
}

/* TODO(阶段2)：按 AA 55 CMD LEN DATA… CRC 55 AA 组任意一帧控制帧（载荷超长要截断） */
QByteArray FrameProto::build(quint8 cmd, const QByteArray &payload)
{
    return {};
}

/* TODO(阶段2)：把命令码翻成中文名，表里没有的按要求输出 "0xNN" 形式 */
QString FrameProto::cmdName(quint8 cmd)
{
    return {};
}

/* TODO(阶段2)：把状态码 ST_* 翻成中文名，未知值同样输出 "0xNN" 形式 */
QString FrameProto::statusName(quint8 status)
{
    return {};
}

/* ===========================================================================
 *  EnvFrame / AckFrame —— 单个帧的解析结果
 * =========================================================================*/

/* TODO(阶段2)：从环境帧数据段按大端读出 6 项物理量，长度不对要拒绝 */
bool EnvFrame::unpack(const quint8 *payload, int len, EnvFrame &out)
{
    return false;
}

/* TODO(阶段2)：把 RP_IDLE / RP_RUNNING / RP_DONE / RP_ERROR 翻成界面与日志用的中文 */
QString AckFrame::replayStateName(quint8 state)
{
    return {};
}

/* ===========================================================================
 *  FrameParser —— 缓冲区 + 状态机：喂字节，取出校验通过的帧
 * =========================================================================*/

/* TODO(阶段2)：构造时给统计项赋初值（期望帧间隔 1 s 等），并清空缓冲与三个待取队列 */
FrameParser::FrameParser()
{
}

/* TODO(阶段2)：清空缓冲区、三个待取队列与全部统计计数，回到刚构造的状态 */
void FrameParser::reset()
{
}

/* TODO(阶段2)：把一段字节追加进缓冲区，然后尽量推进状态机取帧 */
void FrameParser::feed(const QByteArray &chunk)
{
}

/* TODO(阶段2)：单字节喂入（feed 的逐字节版本），内部同样走状态机 */
void FrameParser::feedByte(quint8 b)
{
}

/* TODO(阶段2)：从环境帧待取队列弹出一帧；队列为空返回 false */
bool FrameParser::takeFrame(EnvFrame &out)
{
    return false;
}

/* TODO(阶段2)：从控制应答待取队列弹出一帧；队列为空返回 false */
bool FrameParser::takeAck(AckFrame &out)
{
    return false;
}

/* TODO(阶段2)：从历史帧待取队列弹出一帧；队列为空返回 false */
bool FrameParser::takeHistory(HistoryFrame &out)
{
    return false;
}

/* TODO(阶段2)：把缓冲区里还没成帧的字节以十六进制串返回，调试用 */
QByteArray FrameParser::bufferedHex() const
{
    return {};
}

/* TODO(阶段2)：一次性解析整段字节流；outStats 非空时回填统计，lossEstimate 控制丢帧估算 */
QVector<EnvFrame> FrameParser::parseAll(const QByteArray &stream, Stats *outStats, bool lossEstimate)
{
    return {};
}

/* TODO(阶段2)：缓冲区里能推进多少就推进多少，逐帧做帧头/长度/CRC/帧尾判定并累加统计 */
void FrameParser::step()
{
}

/* TODO(阶段2)：从缓冲区头部丢掉 n 字节；countResync 决定是否计入 resyncBytes */
void FrameParser::dropFront(int n, bool countResync)
{
}
