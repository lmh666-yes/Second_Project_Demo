/* ============================================================================
 *  parsertest/main.cpp —— 无 GUI 的解析器自检入口
 *  用法：
 *      parsertest.exe                     只跑内置边界用例
 *      parsertest.exe 原始流文件           先跑内置用例，再回放该文件
 *                                          （二进制原始流或十六进制文本都认）
 *  退出码：0 = 全部通过，1 = 有失败（可脚本判定）
 *  结果同时打印到控制台并写入 UTF-8 文件 自检报告.txt：Windows 控制台是 GBK，
 *  中文看文件更稳。
 * ==========================================================================*/

#include "frameparser.h"
#include "consistencytest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <cctype>
#include <cstdio>

namespace {

/* 十六进制文本（可带空格/换行）→ 字节；不是纯十六进制就按二进制原样返回 */
QByteArray smartLoad(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    const QByteArray raw = f.readAll();
    f.close();

    QByteArray hexOnly;
    hexOnly.reserve(raw.size());
    for (char c : raw) {
        const char u = char(::toupper(static_cast<unsigned char>(c)));
        if ((u >= '0' && u <= '9') || (u >= 'A' && u <= 'F')) hexOnly.append(u);
        else if (u == ' ' || u == '\r' || u == '\n' || u == '\t' || u == ',') continue;
        else return raw;                       /* 出现非十六进制字符 → 当二进制 */
    }
    if (hexOnly.isEmpty() || (hexOnly.size() % 2) != 0) return raw;
    return QByteArray::fromHex(hexOnly);
}

void emitText(const QString &text, QTextStream &con, QTextStream &file)
{
    con << text;
    con.flush();
    file << text;
    file.flush();
}

}   // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QFile rep(QDir::current().filePath(QStringLiteral("自检报告.txt")));
    if (!rep.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "cannot write report file\n");
    }
    QTextStream con(stdout);
    QTextStream file(&rep);
    file.setEncoding(QStringConverter::Utf8);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    con.setCodec("UTF-8");
#endif
    file << QChar(0xFEFF);      /* UTF-8 BOM，记事本/Excel 打开不乱码 */

    /* 内置边界用例 */
    const QVector<CaseResult> builtin = ConsistencyTest::runBuiltin();
    emitText(ConsistencyTest::toText(builtin, QStringLiteral("Qt 上位机 · 帧解析自检（内置用例）")),
         con, file);

    bool allOk = ConsistencyTest::allPassed(builtin);

    /* 可选：回放真实抓包 */
    if (argc > 1) {
        const QString path = QString::fromLocal8Bit(argv[1]);
        const QByteArray stream = smartLoad(path);
        if (stream.isEmpty()) {
            emitText(QStringLiteral("\n[回放] 读不到文件或文件为空：%1\n").arg(path), con, file);
            allOk = false;
        } else {
            const QVector<CaseResult> rep2 = ConsistencyTest::replay(stream);
            emitText(QStringLiteral("\n"), con, file);
            emitText(ConsistencyTest::toText(rep2,
                     QStringLiteral("Qt 上位机 · 回放 %1（%2 字节）")
                        .arg(QFileInfo(path).fileName()).arg(stream.size())),
                 con, file);
            /* 回放里"帧解析正确率"这一条若 FAIL 只提示，不改变退出码；
               内置用例才是协议一致性的硬判据 */
        }
    }

    rep.close();
    return allOk ? 0 : 1;
}
