#include "dataset.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

namespace {

/* 字符串数组 → 二维表 */
QVector<QVector<QString>> toGrid(const QJsonValue &v)
{
    QVector<QVector<QString>> out;
    for (const QJsonValue &row : v.toArray()) {
        QVector<QString> cells;
        for (const QJsonValue &c : row.toArray())
            cells << (c.isString() ? c.toString() : c.toVariant().toString());
        out << cells;
    }
    return out;
}

QStringList toList(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &x : v.toArray())
        out << (x.isString() ? x.toString() : x.toVariant().toString());
    return out;
}

} // namespace

bool Dataset::load(const QString &path)
{
    m_path = path;
    QFile f(path);
    if (!f.exists()) {
        m_error = QStringLiteral("找不到数据文件：%1\n（先在 tools/ 里跑 node export_json.js）").arg(path);
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("打不开数据文件：%1").arg(path);
        return false;
    }
    QJsonParseError pe {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close();
    if (doc.isNull() || !doc.isObject()) {
        m_error = QStringLiteral("数据文件不是合法 JSON（%1，偏移 %2）")
                      .arg(pe.errorString()).arg(pe.offset);
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonArray  all  = root.value(QStringLiteral("all")).toArray();
    if (all.isEmpty()) {
        m_error = QStringLiteral("数据文件里没有条目（all 为空）");
        return false;
    }

    m_cards.clear();
    m_byName.clear();
    m_cards.reserve(all.size());
    for (const QJsonValue &v : all) {
        const QJsonObject o = v.toObject();
        Card c;
        c.m = o.value(QStringLiteral("m")).toString();
        c.n = o.value(QStringLiteral("n")).toString();
        c.p = o.value(QStringLiteral("p")).toString();
        c.d = o.value(QStringLiteral("d")).toString();
        c.r = o.value(QStringLiteral("r")).toString();
        c.e = o.value(QStringLiteral("e")).toString();
        c.k = o.value(QStringLiteral("k")).toString();
        /* 类型要读**合并后的 _t**：手写卡片（流程/向导/结构体）在 data_*.js 里带字面量 t，
           而自动生成的"查找表 / 数据手册页"只有引擎给打的 _t。只读 t 会让这些卡片的类型丢失，
           继而错加 _src=core 的 +4/+3 加分、并整条跳过 table 专属规则（实测导致尾部排序偏移）。 */
        c.t = o.value(QStringLiteral("_t")).toString();
        if (c.t.isEmpty())
            c.t = o.value(QStringLiteral("t")).toString();
        c.g = o.value(QStringLiteral("g")).toString();
        c.b = o.value(QStringLiteral("_b")).toString();
        c.s = o.value(QStringLiteral("s")).toString();
        c.hay = o.value(QStringLiteral("_hay")).toString();
        c.src = o.value(QStringLiteral("_src")).toString();
        c.h = toList(o.value(QStringLiteral("h")));
        c.a = toGrid(o.value(QStringLiteral("a")));
        c.f = toGrid(o.value(QStringLiteral("f")));
        c.i   = o.value(QStringLiteral("_i")).toInt();
        c.ord = o.value(QStringLiteral("_ord")).toInt();
        c.hasDoc = o.value(QStringLiteral("_hasDoc")).toBool();
        c.docA   = o.value(QStringLiteral("_docA")).toInt();
        c.rel = toList(o.value(QStringLiteral("rel")));
        for (const QJsonValue &uv : o.value(QStringLiteral("u")).toArray()) {
            const QJsonObject uo = uv.toObject();
            Card::Use us;
            us.tag  = uo.value(QStringLiteral("tag")).toString();
            us.file = uo.value(QStringLiteral("f")).toString();
            us.line = uo.value(QStringLiteral("l")).toInt();
            us.what = uo.value(QStringLiteral("p")).toString();
            c.u << us;
        }
        if (!c.n.isEmpty() && !m_byName.contains(c.n))
            m_byName.insert(c.n, m_cards.size());
        m_cards << c;
    }

    m_search = root.value(QStringLiteral("search")).toObject();
    m_proj   = root.value(QStringLiteral("proj")).toArray();
    m_loaded = true;
    m_error.clear();
    return true;
}

int Dataset::applyUsage(const QString &path)
{
    if (path.isEmpty())
        return 0;
    QFile f(path);
    /* 文件不在 / 打不开 → 原样不动：手册页照旧用 dataset.json 自带的 u（uReal 保持 false）。 */
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return 0;
    QJsonParseError pe {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close();
    if (doc.isNull() || !doc.isObject())
        return 0;                       /* 索引文件坏了也原样不动，不静默清空 */

    const QJsonObject usage =
        doc.object().value(QStringLiteral("usage")).toObject();

    int n = 0;
    for (Card &c : m_cards) {
        c.u.clear();
        /* 索引文件在 → 每条都走"真实使用位置"渲染：这样没写进去的条目
           才能如实显示"没在你的代码里出现"，而不是留 dataset.json 里网页版自带的假位置。 */
        c.uReal = true;
        if (c.n.isEmpty())
            continue;
        const QJsonArray arr = usage.value(c.n).toArray();
        for (const QJsonValue &uv : arr) {
            const QJsonObject uo = uv.toObject();
            Card::Use us;
            us.tag  = uo.value(QStringLiteral("tag")).toString();
            us.file = uo.value(QStringLiteral("f")).toString();
            us.line = uo.value(QStringLiteral("l")).toInt();
            us.what = uo.value(QStringLiteral("p")).toString();
            c.u << us;
        }
        if (!c.u.isEmpty())
            ++n;
    }
    return n;
}

QVector<int> Dataset::indicesOfSection(int section) const
{
    QVector<int> idx;
    for (int i = 0; i < m_cards.size(); ++i)
        if (int(m_cards.at(i).section()) == section)
            idx << i;
    return idx;
}

QStringList Dataset::modulesOfSection(int section) const
{
    QStringList out;
    if (section == int(Card::Build)) {
        /* 项目引导按「工程」筛，不是模块 */
        for (const Card &c : m_cards)
            if (c.isManu() && !out.contains(c.b))
                out << c.b;
        return out;
    }
    for (const Card &c : m_cards) {
        if (int(c.section()) != section || c.m.isEmpty())
            continue;
        if (!out.contains(c.m))
            out << c.m;
    }
    return out;
}

int Dataset::indexOfName(const QString &name) const
{
    return m_byName.value(name, -1);
}

int Dataset::countType(const QString &t) const
{
    int n = 0;
    for (const Card &c : m_cards)
        if (c.t == t)
            ++n;
    return n;
}

int Dataset::countManu() const
{
    int n = 0;
    for (const Card &c : m_cards)
        if (c.isManu())
            ++n;
    return n;
}
