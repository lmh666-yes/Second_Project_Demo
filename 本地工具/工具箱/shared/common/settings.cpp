#include "settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QWidget>

/* INI 落在"工具箱/"根目录下（从程序位置向上找含「程序」「检查」「shared」的那一层），
   找不到就退回程序自身目录。命名固定 _toolbox.ini，方便整体拷贝。
   判据与 ToolboxWindow::toolsDirPath() 一致：「设计」目录在 2026-10 去子程序时已删除，
   再拿它当标记会让 INI 落到 exe 旁（build/release 下），各页状态记不住。 */
static QString resolveIniPath()
{
    /* 测试 / 量测出口的沙箱开关：设了 TOOLBOX_INI 就整份走它（见 程序\嵌入式工具箱\main.cpp）。
       冒烟会点界面开关，写的就是这份 ini，别动用户那份。 */
    const QByteArray box = qgetenv("TOOLBOX_INI");
    if (!box.isEmpty())
        return QString::fromLocal8Bit(box);

    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (d.exists(QStringLiteral("程序")) && d.exists(QStringLiteral("检查"))
            && d.exists(QStringLiteral("shared")))
            return d.filePath(QStringLiteral("_toolbox.ini"));
        if (!d.cdUp())
            break;
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("_toolbox.ini"));
}

Settings::Settings()
    : m_path(resolveIniPath())
{
}

QString Settings::filePath() const
{
    return m_path;
}

void Settings::setValue(const QString &key, const QVariant &v)
{
    QSettings s(m_path, QSettings::IniFormat);
    s.setValue(QStringLiteral("toolbox/") + key, v);
}

QVariant Settings::value(const QString &key, const QVariant &def) const
{
    QSettings s(m_path, QSettings::IniFormat);
    return s.value(QStringLiteral("toolbox/") + key, def);
}

void Settings::saveGeometry(const QString &key, const QWidget *w)
{
    if (!w)
        return;
    setValue(key + QStringLiteral("/geometry"), w->saveGeometry());
}

void Settings::restoreGeometry(const QString &key, QWidget *w)
{
    if (!w)
        return;
    const QVariant v = value(key + QStringLiteral("/geometry"));
    if (v.isValid())
        w->restoreGeometry(v.toByteArray());
}
