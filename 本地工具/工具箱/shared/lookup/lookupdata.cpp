#include "lookupdata.h"

#include "apppaths.h"
#include "dataset.h"
#include "searchengine.h"
#include "searchtables.h"

#include <QRegularExpression>
#include <QScopedPointer>

namespace {

/* 惰性装载的状态（函数级 static，避免额外全局对象） */
struct State {
    Dataset       ds;
    SearchTables  tables;
    QScopedPointer<SearchEngine> engine;
    bool          tried = false;
    bool          ok    = false;
    QString       err;
};

State &st()
{
    static State s;
    return s;
}

/* 取用途的第一句（数据集里 d 可能很长，塞进注释要短） */
QString firstSentence(const QString &s, int cap = 110)
{
    QString t = s.trimmed();
    t.replace(QLatin1Char('\n'), QLatin1Char(' '));
    for (int i = 0; i < t.size(); ++i) {
        const QChar c = t.at(i);
        if (c == QLatin1Char('。') || c == QLatin1Char('；') || c == QLatin1Char('.')) {
            t = t.left(i + 1);
            break;
        }
    }
    if (t.size() > cap)
        t = t.left(cap) + QStringLiteral("…");
    return t;
}

/* 圈码序号表（①..⑳ 在 U+2460 起连续 20 个；㉑..㉟、㊱..㊿ 是两段） */
const QString &numGlyphs()
{
    static const QString s = QString::fromUtf8(
        "①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮⑯⑰⑱⑲⑳"
        "㉑㉒㉓㉔㉕㉖㉗㉘㉙㉚㉛㉜㉝㉞㉟"
        "㊱㊲㊳㊴㊵㊶㊷㊸㊹㊺㊻㊼㊽㊾㊿");
    return s;
}

} // namespace

bool LookupData::load(QString *err)
{
    State &s = st();
    if (s.tried) {
        if (err && !s.ok)
            *err = s.err;
        return s.ok;
    }
    s.tried = true;

    const QString p = AppPaths::datasetPath();
    if (!s.ds.load(p)) {
        s.err = s.ds.error().isEmpty()
                    ? QStringLiteral("读不到速查数据：%1").arg(p)
                    : QStringLiteral("%1（%2）").arg(s.ds.error(), p);
        s.ok = false;
        if (err)
            *err = s.err;
        return false;
    }
    s.tables.load(s.ds.search());
    s.engine.reset(new SearchEngine(&s.ds.cards(), &s.tables));
    s.ok = true;
    return true;
}

bool    LookupData::loaded() { return st().ok; }
QString LookupData::path()   { return st().ok ? AppPaths::datasetPath() : QString(); }
int     LookupData::size()   { return st().ok ? st().ds.size() : 0; }

QVector<LookupData::FuncHit> LookupData::findFuncs(const QString &query, int limit)
{
    QVector<FuncHit> out;
    QString err;
    if (query.trimmed().isEmpty() || !load(&err))
        return out;

    State &s = st();
    const SearchEngine::Result r = s.engine->run(query, int(Card::Ref), QString());
    for (int i : r.order) {
        const Card &c = s.ds.at(i);
        if (!c.isFunc())
            continue;
        if (!c.p.contains(QLatin1Char('(')) || !c.p.contains(QLatin1Char(')')))
            continue;                        /* 没有原型的不算"符号级"提示（多为芯片/表卡） */
        FuncHit h;
        h.name   = c.n;
        h.proto  = c.p.trimmed();
        h.desc   = firstSentence(c.d.isEmpty() ? c.s : c.d);
        h.module = c.m;
        out << h;
        if (out.size() >= limit)
            break;
    }
    return out;
}

QVector<LookupData::FlowHit> LookupData::findFlows(const QString &query, int limit)
{
    QVector<FlowHit> out;
    QString err;
    if (!load(&err))
        return out;

    State &s = st();
    QVector<int> order;
    if (query.trimmed().isEmpty()) {
        /* 空查询：按数据集原有次序给全部流程卡 */
        for (int i = 0; i < s.ds.size(); ++i)
            if (s.ds.at(i).isFlow())
                order << i;
    } else {
        order = s.engine->run(query, int(Card::Guide), QString()).order;
    }
    for (int i : order) {
        const Card &c = s.ds.at(i);
        if (!c.isFlow())
            continue;
        FlowHit h;
        h.name = c.n;
        h.desc = firstSentence(c.d, 160);
        h.code = c.e;
        out << h;
        if (out.size() >= limit)
            break;
    }
    return out;
}

LookupData::FlowHit LookupData::flowByName(const QString &name)
{
    FlowHit h;
    QString err;
    if (!load(&err))
        return h;
    State &s = st();
    for (int i = 0; i < s.ds.size(); ++i) {
        const Card &c = s.ds.at(i);
        if (c.isFlow() && c.n == name) {
            h.name = c.n;
            h.desc = firstSentence(c.d, 160);
            h.code = c.e;
            break;
        }
    }
    return h;
}

int LookupData::flowRank(const QString &name)
{
    const QString t = name.trimmed();
    if (t.isEmpty())
        return 99;
    const int i = numGlyphs().indexOf(t.at(0));
    return i < 0 ? 99 : i;
}

QString LookupData::extractReusable(const QString &code, QStringList *funcsOut,
                                    QStringList *includesOut)
{
    QStringList lines = code.split(QLatin1Char('\n'));

    /* 1) 摘掉 #include（拼块时由生成物统一管头文件），记下来供提示 */
    QStringList incs;
    QStringList kept;
    const QRegularExpression reInc(QStringLiteral("^\\s*#\\s*include\\b"));
    for (const QString &l : lines) {
        if (reInc.match(l).hasMatch()) {
            /* "xxx.h" 先试引号形式；没有引号再试尖括号形式。
               注意不能串成一串 section（缺分隔符时 section 越界会直接给空串，踩过）。 */
            QString h = l.section(QLatin1Char('"'), 1, 1).trimmed();
            if (h.isEmpty() || h.contains(QLatin1Char('"')))
                h = l.section(QLatin1Char('<'), 1, 1).section(QLatin1Char('>'), 0, 0).trimmed();
            if (!h.isEmpty() && !incs.contains(h))
                incs << h;
            continue;
        }
        kept << l;
    }

    /* 2) 去掉 main() 整块（大括号配平）；顺手收集函数定义名 */
    const QRegularExpression reMain(QStringLiteral("^\\s*(?:int|void)\\s+main\\s*\\("));
    const QRegularExpression reFn(
        QStringLiteral("^\\s*(?:static\\s+)?(?:void|int|char|float|double|size_t|"
                       "u?int\\d+_t)\\s+([A-Za-z_]\\w*)\\s*\\("));
    QStringList fns;
    QStringList out;
    for (int i = 0; i < kept.size(); ++i) {
        const QString &l = kept.at(i);
        if (reMain.match(l).hasMatch()) {
            int depth = 0;
            bool started = false;
            for (; i < kept.size(); ++i) {
                const QString &cur = kept.at(i);
                for (const QChar ch : cur) {
                    if (ch == QLatin1Char('{')) { depth++; started = true; }
                    else if (ch == QLatin1Char('}')) depth--;
                }
                if (started && depth <= 0)
                    break;                       /* main 收尾 */
            }
            continue;
        }
        const QRegularExpressionMatch m = reFn.match(l);
        if (m.hasMatch()) {
            const QString fn = m.captured(1);
            if (!fns.contains(fn))
                fns << fn;
        }
        out << l;
    }

    /* 3) 规整空行：三行以上连续空行压成一行 */
    QStringList final;
    int blank = 0;
    for (const QString &l : out) {
        if (l.trimmed().isEmpty()) {
            if (++blank > 2)
                continue;
        } else {
            blank = 0;
        }
        final << l;
    }
    if (funcsOut)
        *funcsOut = fns;
    if (includesOut)
        *includesOut = incs;
    return final.join(QLatin1Char('\n')).trimmed();
}
