#ifndef PROTOCOL_H
#define PROTOCOL_H

/* 帧协议逻辑层：CRC、组帧、解帧、坏帧注入。
   纯逻辑，不依赖界面，可被命令行自检直接调用。
   协议：AA 55 | CMD | LEN | DATA(LEN) | CRC_LO CRC_HI | 55 AA
   CRC16-MODBUS（初值 0xFFFF，多项式 0xA001），低字节在前，覆盖 CMD..DATA。 */

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Proto {

const quint8 HEAD0 = 0xAA;
const quint8 HEAD1 = 0x55;
const int    MAX_LEN = 64;      /* 单帧载荷上限 */

struct Frame {
    int        offset = 0;
    QByteArray raw;
    quint8     cmd = 0;
    int        len = 0;
    QByteArray data;
    quint16    crc = 0;         /* 帧里收到的 CRC */
    quint16    crcCalc = 0;     /* 重算的 CRC */
    bool       tailOk = false;
    bool       crcOk = false;
    bool       ok = false;
    QString    note;            /* 异常说明，正常为空 */
};

quint16 crc16Modbus(const QByteArray &data);

/* 组帧：cmd 为命令字，data 为载荷（自动补长度）。 */
QByteArray buildFrame(quint8 cmd, const QByteArray &data);

/* 按环境帧布局拼 12 字节载荷：温/湿 ×100、气压 ×10，其余原值，大端。 */
QByteArray buildEnvData(double tempC, double humiRh, double pressHpa,
                        int lightLx, int tvocPpb, int mq135Raw);

/* 解析整段字节流：按帧头配对、校验 CRC 与帧尾，失败即重找帧头（重同步）。
   notes 收集重同步与不完整片段。 */
QVector<Frame> parseStream(const QByteArray &stream, QStringList &notes);

/* 把一帧拆成可读的多行文本（含字段表与结果）。 */
QString describeFrame(const Frame &f);

QString cmdName(quint8 cmd);

struct Injection {
    QString    desc;
    QByteArray bytes;
};

/* 由一帧生成一组坏帧用于测试解析器：坏 CRC、坏帧尾、错长度、截断、噪声、插字节、粘包。 */
QVector<Injection> injections(const QByteArray &frame);

/* 十六进制文本：带空格（AA 55 ...）。 */
QString hexSpaced(const QByteArray &b);

/* 解析用户输入的十六进制文本（忽略空格与逗号）。失败返回空串并置 errOk=false。 */
QByteArray parseHexInput(const QString &text, bool *errOk = nullptr);

/* 内置自检。log 收集每项结果，返回失败项数。 */
int selftest(QStringList &log);

} // namespace Proto

#endif // PROTOCOL_H
