#include "profile.h"

#include "uploader.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSettings>

/* 配置档跟着程序走：程序根（含 src/git_backend.py 的那一层）下的 _git_upload.ini。
   本工具独立于工具箱，不再与综合程序共用 工具箱/_toolbox.ini。 */
static QString resolveIniPath()
{
    const QString root = Uploader::toolRoot();
    if (!root.isEmpty())
        return QDir(root).filePath(QStringLiteral("_git_upload.ini"));
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("_git_upload.ini"));
}

static QString pfx(const QString &k)
{
    return QStringLiteral("toolbox/upload/") + k;
}

static QString fieldKey(const QString &name, const QString &field)
{
    return QStringLiteral("toolbox/upload/p/%1/%2").arg(name, field);
}

namespace {

/* 档名清单存成 **JSON 字符串**，不用 QStringList：
   QSettings 把 QStringList 写成逗号分隔，档名里带逗号就会被拆成两条
   （原来的遗留缺陷：档名 "公司,备用" 存进去变成两个档）。JSON 不挑分隔符。 */
QStringList readNames(QSettings &s)
{
    const QString v = s.value(pfx(QStringLiteral("profiles"))).toString().trimmed();
    if (v.isEmpty())
        return QStringList();
    const QJsonDocument d = QJsonDocument::fromJson(v.toUtf8());
    if (d.isArray()) {
        QStringList out;
        for (const QJsonValue &x : d.array())
            out << x.toString();
        return out;
    }
    /* 读到的不是 JSON（手改过的 ini / 更早的逗号格式）：按逗号分一次兜底，
       宁可能读出旧档也不要静默判成"一个档都没有"。 */
    return v.split(QLatin1Char(','), Qt::SkipEmptyParts);
}

void writeNames(QSettings &s, const QStringList &names)
{
    QJsonArray a;
    for (const QString &n : names)
        a.append(n);
    s.setValue(pfx(QStringLiteral("profiles")),
               QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)));
}

/* 只写某个档的字段键，不碰列表和 active（save / rename 共用）。 */
void writeFields(QSettings &s, const QString &name, const Profile &p)
{
    s.setValue(fieldKey(name, QStringLiteral("account")),   p.account);
    s.setValue(fieldKey(name, QStringLiteral("remote")),    p.remote);
    s.setValue(fieldKey(name, QStringLiteral("branch")),    p.branch);
    s.setValue(fieldKey(name, QStringLiteral("userName")),  p.userName);
    s.setValue(fieldKey(name, QStringLiteral("userEmail")), p.userEmail);
    s.setValue(fieldKey(name, QStringLiteral("folder")),    p.folder);
    s.setValue(fieldKey(name, QStringLiteral("note")),      p.note);
}

} // namespace

Profile::Profile()
    : branch(QStringLiteral("main"))
{
}

ProfileStore::ProfileStore()
    : m_path(resolveIniPath())
{
    sanitize();
}

ProfileStore::ProfileStore(const QString &iniPath)
    : m_path(iniPath)
{
    sanitize();
}

/* 自愈：只做不会丢数据的修正 —— 列表里的空名与重复项去掉，
   active 指向已不存在的档时改指第一个（列表为空则清空）。
   真的有问题才写盘。 */
void ProfileStore::sanitize()
{
    const QStringList ns = names();
    QStringList keep;
    for (const QString &n : ns) {
        if (n.trimmed().isEmpty() || keep.contains(n))
            continue;
        keep << n;
    }
    const bool    listChanged = (keep != ns);
    const QString act         = raw(QStringLiteral("active")).toString();
    const bool    activeValid = act.isEmpty() ? keep.isEmpty() : keep.contains(act);
    if (!listChanged && activeValid)
        return;                            /* 没问题就不动文件 */

    QSettings s(m_path, QSettings::IniFormat);
    if (listChanged)
        writeNames(s, keep);
    if (!activeValid)
        s.setValue(pfx(QStringLiteral("active")), keep.isEmpty() ? QString() : keep.first());
    s.sync();
}

QString ProfileStore::filePath() const
{
    return m_path;
}

void ProfileStore::setRaw(const QString &key, const QVariant &v)
{
    QSettings s(m_path, QSettings::IniFormat);
    s.setValue(pfx(key), v);
}

QVariant ProfileStore::raw(const QString &key, const QVariant &def) const
{
    QSettings s(m_path, QSettings::IniFormat);
    return s.value(pfx(key), def);
}

QStringList ProfileStore::names() const
{
    QSettings s(m_path, QSettings::IniFormat);
    return readNames(s);
}

bool ProfileStore::exists(const QString &name) const
{
    return names().contains(name);
}

Profile ProfileStore::get(const QString &name) const
{
    Profile p;
    if (name.trimmed().isEmpty())
        return p;
    QSettings s(m_path, QSettings::IniFormat);
    p.name      = name;
    p.account   = s.value(fieldKey(name, QStringLiteral("account")), QString()).toString();
    p.remote    = s.value(fieldKey(name, QStringLiteral("remote")), QString()).toString();
    p.branch    = s.value(fieldKey(name, QStringLiteral("branch")), QStringLiteral("main")).toString();
    p.userName  = s.value(fieldKey(name, QStringLiteral("userName")), QString()).toString();
    p.userEmail = s.value(fieldKey(name, QStringLiteral("userEmail")), QString()).toString();
    p.folder    = s.value(fieldKey(name, QStringLiteral("folder")), QString()).toString();
    p.note      = s.value(fieldKey(name, QStringLiteral("note")), QString()).toString();
    if (p.branch.trimmed().isEmpty())
        p.branch = QStringLiteral("main");
    return p;
}

void ProfileStore::save(const Profile &p)
{
    if (!p.isValid())
        return;
    QSettings s(m_path, QSettings::IniFormat);
    const QString n = p.name;
    writeFields(s, n, p);

    /* 列表与 active 都用同一个 QSettings 实例读写，
       免得另起一个实例同步时把刚写进去的字段冲掉 */
    QStringList ns = readNames(s);
    if (!ns.contains(n))
        ns << n;
    writeNames(s, ns);

    if (s.value(pfx(QStringLiteral("active"))).toString().isEmpty())
        s.setValue(pfx(QStringLiteral("active")), n);
    s.sync();
}

void ProfileStore::remove(const QString &name)
{
    if (name.trimmed().isEmpty())
        return;
    QSettings s(m_path, QSettings::IniFormat);
    s.remove(pfx(QStringLiteral("p/") + name));   /* 键 + 其下全部子键一起删 */

    QStringList ns = readNames(s);
    ns.removeAll(name);
    writeNames(s, ns);

    if (s.value(pfx(QStringLiteral("active"))).toString() == name)
        s.setValue(pfx(QStringLiteral("active")),
                   ns.isEmpty() ? QString() : ns.first());
    s.sync();
}

bool ProfileStore::rename(const QString &oldName, const QString &newName)
{
    if (oldName == newName || newName.trimmed().isEmpty())
        return false;
    QSettings s(m_path, QSettings::IniFormat);
    QStringList ns = readNames(s);
    if (!ns.contains(oldName) || ns.contains(newName))
        return false;                    /* 源档不存在 / 新名已被占用：不覆盖，直接拒 */

    const Profile p = get(oldName);
    const int     pos       = ns.indexOf(oldName);
    const bool    wasActive = (s.value(pfx(QStringLiteral("active"))).toString() == oldName);

    Profile np = p;
    np.name = newName;
    writeFields(s, newName, np);
    s.remove(pfx(QStringLiteral("p/") + oldName));

    ns.removeAll(oldName);               /* 重建列表：按原位置插回新名 */
    ns.insert(qBound(0, pos, ns.size()), newName);
    writeNames(s, ns);

    if (wasActive)                       /* 改的是当前档 → 当前档跟着改名 */
        s.setValue(pfx(QStringLiteral("active")), newName);
    s.sync();
    return true;
}

QString ProfileStore::activeName() const
{
    const QString a = raw(QStringLiteral("active")).toString();
    return names().contains(a) ? a : QString();
}

void ProfileStore::setActive(const QString &name)
{
    setRaw(QStringLiteral("active"), name);
}

Profile ProfileStore::activeProfile() const
{
    return get(activeName());
}
