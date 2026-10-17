#include "pyfind.h"

#include <QProcess>
#include <QString>

namespace PyFind {

/* F1：结果在本进程内缓存一次。
   探测是"起一个 python 探一次"的重活（同步等进程走完，实测 200~400ms），而一次使用里
   它会被反复调到：总览页每轮取数一次、每个工具页每跑一步一次、全量检查一次 ——
   不缓存就是反复付同样的钱（串行 4 步的工具页会白付 4 次）。
   解释器在一次运行期里不会变；万一用户在运行中装了 Python，原本的提示也正是
   "重开本程序即可"，所以缓存的口径与既有提示一致。

   为什么不"启动时后台预热"：那要么冻界面几百毫秒（与"启动要快"相悖），
   要么另起线程（多线程 + 退出时机又是另一类风险）。首次取数本来就有"正在取数…"提示，
   这一次探测付在那儿最自然。 */
QStringList detect()
{
    static bool probed = false;
    static QStringList cached;
    if (probed)
        return cached;
    probed = true;

    const struct { const char *prog; QStringList pre; } cands[] = {
        { "python",  {} },
        { "py",      { QStringLiteral("-3") } },
        { "python3", {} },
    };
    for (const auto &c : cands) {
        QProcess probe;
        QStringList a = c.pre;
        a << QStringLiteral("-c") << QStringLiteral("print(1)");
        probe.start(QString::fromLatin1(c.prog), a);
        if (probe.waitForStarted(3000) && probe.waitForFinished(4000)
            && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) {
            cached << QString::fromLatin1(c.prog);
            cached += c.pre;
            break;
        }
    }
    return cached;
}

} // namespace PyFind
