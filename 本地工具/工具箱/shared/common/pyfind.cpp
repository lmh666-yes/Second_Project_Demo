#include "pyfind.h"

#include <QProcess>
#include <QString>

namespace PyFind {

QStringList detect()
{
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
            QStringList r;
            r << QString::fromLatin1(c.prog);
            r += c.pre;
            return r;
        }
    }
    return {};
}

} // namespace PyFind
