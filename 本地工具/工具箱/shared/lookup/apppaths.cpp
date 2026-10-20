#include "apppaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

/* 从 exe 位置向上找同时含「程序」「检查」「shared」的目录 = 工具箱根。
   与 ToolboxWindow::toolsDirPath() 同一判据（那边判错会让所有运行器页失效，
   这边判错会让速查页读不到数据，所以两处必须一致）。 */
QString toolboxRoot()
{
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (d.exists(QStringLiteral("程序")) && d.exists(QStringLiteral("检查"))
            && d.exists(QStringLiteral("shared")))
            return d.absolutePath();
        if (!d.cdUp())
            break;
    }
    return QString();
}

bool hasDataset(const QString &dir)
{
    return !dir.isEmpty() && QFileInfo::exists(QDir(dir).filePath(QStringLiteral("data/dataset.json")));
}

} // namespace

QString AppPaths::toolRoot()
{
    const QString tb = toolboxRoot();
    if (!tb.isEmpty()) {
        /* 1) 主路径：工具箱内的速查数据目录 */
        const QString in = QDir(tb).filePath(QStringLiteral("速查数据"));
        if (hasDataset(in))
            return QDir(in).absolutePath();

        /* 2) 兜底：搬家前的旧位置——工具箱上一级的同级 STM32速查/（留着以防回退） */
        const QString sib = QDir(QDir(tb).absolutePath() + QStringLiteral("/.."))
                                 .filePath(QStringLiteral("STM32速查"));
        if (hasDataset(sib))
            return QDir(sib).absolutePath();
    }

    /* 3) 兜底：自己向上找含 data/dataset.json 的那一层（源码树里直接跑） */
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (hasDataset(d.absolutePath()))
            return d.absolutePath();
        if (!d.cdUp())
            break;
    }
    return QString();
}

QString AppPaths::datasetPath()
{
    const QString r = toolRoot();
    if (!r.isEmpty())
        return QDir(r).filePath(QStringLiteral("data/dataset.json"));
    /* 兜底：开发期直接在源码树里跑（build/release → 上三级） */
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("../../../data/dataset.json"));
}

QString AppPaths::usagePath()
{
    const QString r = toolRoot();
    if (!r.isEmpty())
        return QDir(r).filePath(QStringLiteral("data/code_usage.json"));
    /* 兜底：开发期直接在源码树里跑（与 datasetPath 同一口径） */
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("../../../data/code_usage.json"));
}

QHash<QString, QString> AppPaths::boardDirs()
{
    QHash<QString, QString> out;
    const QString r = toolRoot();
    if (r.isEmpty())
        return out;
    QFile f(QDir(r).filePath(QStringLiteral("rules/board_envmonitor.json")));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    /* 仓库根 = 数据根的上三级（速查数据 → 工具箱 → 本地工具 → 仓库根），
       与手册页定位进度文档的算法同源。 */
    const QString repo = QDir::cleanPath(QDir(r).filePath(QStringLiteral("../../..")));
    const QJsonArray boards = QJsonDocument::fromJson(f.readAll()).object()
                                  .value(QStringLiteral("boards")).toArray();
    for (const QJsonValue &v : boards) {
        const QJsonObject b = v.toObject();
        const QString name = b.value(QStringLiteral("name")).toString();
        const QString dir  = b.value(QStringLiteral("dir")).toString();
        QString tag;
        if (name.contains(QStringLiteral("板1")))
            tag = QStringLiteral("板1");
        else if (name.contains(QStringLiteral("板2")))
            tag = QStringLiteral("板2");
        if (tag.isEmpty() || dir.isEmpty())
            continue;
        const QString proj = QDir(repo).filePath(dir);
        out.insert(tag, proj);
        /* 驱动源码那一组（`板1·库`）在 <工程>/FWLIB/src：使用位置里这两组的文件
           都只记 basename，所以两组各配一个目录，拼绝对路径时才不会点空。 */
        out.insert(tag + QString::fromUtf8("·库"),
                   QDir(proj).filePath(QStringLiteral("FWLIB/src")));
    }
    return out;
}

QString AppPaths::iniPath()
{
    /* 测试 / 量测出口的沙箱开关：设了 TOOLBOX_LOOKUP_INI 就整份走它（见
       程序\嵌入式工具箱\main.cpp）。冒烟会走遍速查页，写的就是这份 ini
       （主题 / 当前板块 / "最近浏览"），别动用户那份。 */
    const QByteArray box = qgetenv("TOOLBOX_LOOKUP_INI");
    if (!box.isEmpty())
        return QString::fromLocal8Bit(box);

    const QString r = toolRoot();
    if (!r.isEmpty())
        return QDir(r).filePath(QStringLiteral("_stm32lookup.ini"));
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("_stm32lookup.ini"));
}
