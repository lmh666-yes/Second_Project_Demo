#include "store.h"

#include "apppaths.h"

#include <QSettings>

namespace {
const char *K_FAV    = "ui/fav";
const char *K_RECENT = "ui/recent";
const char *K_STEPS  = "ui/steps";
const char *K_SECTION= "ui/section";
const int   RECENT_MAX = 30;
}

Store::Store() = default;

QString Store::key(const QString &k) const
{
    return QStringLiteral("stm32lookup/") + k;
}

QStringList Store::list(const QString &k) const
{
    QSettings s(AppPaths::iniPath(), QSettings::IniFormat);
    return s.value(key(k)).toStringList();
}

void Store::setList(const QString &k, const QStringList &v)
{
    QSettings s(AppPaths::iniPath(), QSettings::IniFormat);
    s.setValue(key(k), v);
    s.sync();
}

QStringList Store::favorites() const { return list(QString::fromLatin1(K_FAV)); }

bool Store::isFavorite(const QString &name) const
{
    return favorites().contains(name);
}

bool Store::toggleFavorite(const QString &name)
{
    QStringList f = favorites();
    const bool was = f.contains(name);
    if (was)
        f.removeAll(name);
    else
        f.prepend(name);
    setList(QString::fromLatin1(K_FAV), f);
    return !was;
}

void Store::clearFavorites() { setList(QString::fromLatin1(K_FAV), QStringList()); }

QStringList Store::recent() const { return list(QString::fromLatin1(K_RECENT)); }

void Store::touchRecent(const QString &name)
{
    if (name.isEmpty())
        return;
    QStringList r = recent();
    r.removeAll(name);
    r.prepend(name);
    while (r.size() > RECENT_MAX)
        r.removeLast();
    setList(QString::fromLatin1(K_RECENT), r);
}

void Store::clearRecent() { setList(QString::fromLatin1(K_RECENT), QStringList()); }

QSet<QString> Store::doneSteps() const
{
    QSet<QString> out;
    for (const QString &s : list(QString::fromLatin1(K_STEPS)))
        out.insert(s);
    return out;
}

bool Store::isStepDone(const QString &card, int step) const
{
    return doneSteps().contains(QStringLiteral("%1#%2").arg(card).arg(step));
}

bool Store::toggleStep(const QString &card, int step)
{
    QStringList all = list(QString::fromLatin1(K_STEPS));
    const QString k = QStringLiteral("%1#%2").arg(card).arg(step);
    const bool was = all.contains(k);
    if (was)
        all.removeAll(k);
    else
        all << k;
    setList(QString::fromLatin1(K_STEPS), all);
    return !was;
}

void Store::clearSteps(const QString &card)
{
    QStringList all = list(QString::fromLatin1(K_STEPS));
    const QString prefix = card + QLatin1Char('#');
    QStringList keep;
    for (const QString &s : all)
        if (!s.startsWith(prefix))
            keep << s;
    setList(QString::fromLatin1(K_STEPS), keep);
}

int Store::lastSection() const
{
    QSettings s(AppPaths::iniPath(), QSettings::IniFormat);
    return s.value(key(QString::fromLatin1(K_SECTION)), 0).toInt();
}

void Store::setLastSection(int v)
{
    QSettings s(AppPaths::iniPath(), QSettings::IniFormat);
    s.setValue(key(QString::fromLatin1(K_SECTION)), v);
    s.sync();
}
