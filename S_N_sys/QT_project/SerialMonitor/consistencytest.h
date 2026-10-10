#ifndef CONSISTENCYTEST_H
#define CONSISTENCYTEST_H

/* ============================================================================
 *  consistencytest.h —— 三端一致性自检（回放 + 边界用例）
 *  不接硬件也能验证上位机这一端的解析与固件协议一致：用例的期望字节先用独立
 *  Python 脚本按 CRC16-MODBUS 算出（init 0xFFFF / poly 0xA001），不用本程序的
 *  buildEnv() 生成，避免自己验自己。出处《项目整体评审与升级建议.md》§0。
 *  两个入口：runBuiltin() 内置 8 个用例（正常 / 负温度 / CRC 错 / 三帧粘包 /
 *  逐字节分包 / 噪声重同步 / 负湿度无效标记 / 组帧回环）；replay(stream) 回放
 *  真实抓包，统计成帧数 / CRC 错 / 重同步字节。
 *  回放时所有帧在同一毫秒内到达，间隔统计无意义，必须关掉丢帧估算。
 * ==========================================================================*/

#include <QByteArray>
#include <QString>
#include <QVector>

struct CaseResult {
    QString name;
    bool    pass = false;
    QString detail;      /* 通过也写实测值，便于对照 */
};

class ConsistencyTest
{
public:
    /* 内置边界用例；每个用例都独立新建 parser，互不污染 */
    static QVector<CaseResult> runBuiltin();

    /* 回放一段字节流（超时/半帧也算：残包会记进 resyncBytes） */
    static QVector<CaseResult> replay(const QByteArray &stream);

    /* 汇总成人类可读报告（写 自检报告.txt / 显示在对话框里） */
    static QString toText(const QVector<CaseResult> &results, const QString &title);

    /* 通过率：passed / total（total = 0 时返回 0） */
    static double passRate(const QVector<CaseResult> &results);
    static bool   allPassed(const QVector<CaseResult> &results);
    static int    passedCount(const QVector<CaseResult> &results);
};

#endif  /* CONSISTENCYTEST_H */
