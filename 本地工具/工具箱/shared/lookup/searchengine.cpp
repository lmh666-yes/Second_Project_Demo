#include "searchengine.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace {

bool isCJK(QChar c)
{
    const ushort u = c.unicode();
    return u >= 0x4E00 && u <= 0x9FFF;
}
bool isAlnumAscii(QChar c)
{
    const ushort u = c.unicode();
    return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z');
}
bool isPunctBreak(QChar c)
{
    static const QString extra = QStringLiteral("，。、；：！？（）【】《》“”‘’·,.;:!?()[]{}<>\"'");
    return extra.contains(c);
}

} // namespace

SearchEngine::SearchEngine(const QVector<Card> *cards, const SearchTables *tables)
    : m_cards(cards), m_t(tables)
{
    const int n = m_cards ? m_cards->size() : 0;
    m_nameTok.reserve(n);
    m_nameLow.reserve(n);
    QSet<QString> seen;
    /* F1：这条正则原来是写在循环体里的 —— 每张卡片的每个词都重新编译一次
       （上千次编译），实测占掉整个索引构建 148ms 的绝大部分。规则是固定的，
       编译一次复用即可（QRegularExpression 支持隐式共享，const 复用是安全的）。 */
    static const QRegularExpression notAlnum(QStringLiteral("[^a-z0-9]"));
    for (int i = 0; i < n; ++i) {
        const Card &c = m_cards->at(i);
        const QStringList nt = nameTokens(c.n);
        m_nameTok.append(nt);
        m_nameLow.append(c.n.toLower());
        for (const QString &t : nt) {
            if (t.size() >= 3 && t.at(0) >= QLatin1Char('a') && t.at(0) <= QLatin1Char('z')
                && !t.contains(notAlnum)
                && !seen.contains(t)) {
                seen.insert(t);
                m_vocab.append(t);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  分词                                                                */
/* ------------------------------------------------------------------ */

QStringList SearchEngine::tokenize(const QString &raw) const
{
    QString s = raw.trimmed().toLower();
    if (s.isEmpty())
        return {};

    /* 全角 → 半角；全角空格/不换行空格 → 普通空格 */
    QString t;
    t.reserve(s.size() + 16);
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if (u >= 0xFF01 && u <= 0xFF5E)
            t.append(QChar(ushort(u - 0xFEE0)));
        else if (u == 0x3000 || u == 0x00A0)
            t.append(QLatin1Char(' '));
        else
            t.append(c);
    }
    /* 中英交界处插空格；标点一律当分隔符 */
    QString spaced;
    spaced.reserve(t.size() + 16);
    for (int i = 0; i < t.size(); ++i) {
        const QChar c = t.at(i);
        if (isPunctBreak(c)) {
            spaced.append(QLatin1Char(' '));
            continue;
        }
        if (i > 0) {
            const QChar p = t.at(i - 1);
            if ((isCJK(p) && isAlnumAscii(c)) || (isAlnumAscii(p) && isCJK(c)))
                spaced.append(QLatin1Char(' '));
        }
        spaced.append(c);
    }

    /* 按空白切分 → 每个片段做错别字纠正 / 中文分词 */
    QStringList out;
    const QStringList raw_toks = spaced.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts);
    for (const QString &tok : raw_toks) {
        const QString fix = fixTypo(tok);
        bool pureCJK = !fix.isEmpty();
        for (const QChar c : fix)
            if (!isCJK(c)) { pureCJK = false; break; }
        if (pureCJK) {
            if (m_t->segSet.contains(fix))
                out << fix;
            else
                out += segCN(fix);
        } else {
            out << fix;
        }
    }
    /* 去重（保序） */
    QStringList uniq;
    QSet<QString> seen;
    for (const QString &w : out) {
        if (w.isEmpty() || seen.contains(w))
            continue;
        seen.insert(w);
        uniq << w;
    }
    return uniq;
}

QStringList SearchEngine::segCN(const QString &run) const
{
    const QStringList &dict = m_t->segDict;
    QStringList out;
    int i = 0;
    while (i < run.size()) {
        QString hit;
        for (const QString &w : dict) {
            if (w.size() <= run.size() - i && run.mid(i, w.size()) == w) { hit = w; break; }
        }
        if (!hit.isEmpty()) {
            out << hit;
            i += hit.size();
            continue;
        }
        int j = i + 1;
        while (j < run.size()) {
            bool starts = false;
            for (const QString &w : dict)
                if (w.size() <= run.size() - j && run.mid(j, w.size()) == w) { starts = true; break; }
            if (starts)
                break;
            ++j;
        }
        out << run.mid(i, j - i);
        i = j;
    }
    return out;
}

QStringList SearchEngine::nameTokens(const QString &name) const
{
    /* F1：这 4 条正则原来都写在函数体里，于是**每调用一次就重新编译一遍**；而本函数是
       按卡片逐张调的（约 900 张 × 4 条 ≈ 3600 次编译），实测占了索引构建 124ms 的大头。
       模式是固定的，编译一次复用即可。 */
    static const QRegularExpression breakRe(
        QStringLiteral("[^0-9A-Za-z\\x{4e00}-\\x{9fff}]+"));
    static const QRegularExpression camelUpperRe(QStringLiteral("([A-Z]+)([A-Z][a-z])"));
    static const QRegularExpression camelLowerRe(QStringLiteral("([a-z0-9])([A-Z])"));
    static const QRegularExpression spaceRe(QStringLiteral("\\s+"));

    QStringList parts;
    const QStringList segs = name.split(breakRe, Qt::SkipEmptyParts);
    for (const QString &seg : segs) {
        bool hasCJK = false;
        for (const QChar c : seg)
            if (isCJK(c)) { hasCJK = true; break; }
        if (hasCJK) {
            parts << seg;
            continue;
        }
        QString s = seg;
        s.replace(camelUpperRe, QStringLiteral("\\1 \\2"));
        s.replace(camelLowerRe, QStringLiteral("\\1 \\2"));
        parts += s.split(spaceRe, Qt::SkipEmptyParts);
    }
    QStringList low;
    for (const QString &p : parts)
        low << p.toLower();
    return low;
}

/* ------------------------------------------------------------------ */
/*  命中判定                                                            */
/* ------------------------------------------------------------------ */

bool SearchEngine::hasTok(const QString &text, const QString &t) const
{
    if (t.isEmpty())
        return false;
    if (t.size() > 3) {
        for (const QChar c : t)
            if (!isAlnumAscii(c))
                return text.contains(t);
        return text.contains(t);
    }
    bool alnumOnly = true;
    for (const QChar c : t)
        if (!isAlnumAscii(c)) { alnumOnly = false; break; }
    if (!alnumOnly)
        return text.contains(t);
    /* 短词要走词边界，避免 Init 里的 it 被当成"中断" */
    QRegularExpression &re = m_bound[t];
    if (re.pattern().isEmpty())
        re = QRegularExpression(QStringLiteral("(^|[^a-z0-9])") + t
                                + QStringLiteral("([^a-z0-9]|$)"));
    return re.match(text).hasMatch();
}

bool SearchEngine::dist1(const QString &a, const QString &b)
{
    if (a == b)
        return false;
    if (a.size() == b.size()) {
        QVector<int> d;
        for (int i = 0; i < a.size(); ++i)
            if (a.at(i) != b.at(i))
                d << i;
        if (d.size() == 2 && d[1] == d[0] + 1 && a.at(d[0]) == b.at(d[1]) && a.at(d[1]) == b.at(d[0]))
            return true;
    }
    int i = 0, j = 0, diff = 0;
    while (i < a.size() && j < b.size()) {
        if (a.at(i) == b.at(j)) { ++i; ++j; continue; }
        if (++diff > 1)
            return false;
        if (a.size() > b.size()) ++i;
        else if (b.size() > a.size()) ++j;
        else { ++i; ++j; }
    }
    return diff + (a.size() - i) + (b.size() - j) <= 1;
}

QStringList SearchEngine::nearWords(const QString &t) const
{
    if (m_near.contains(t))
        return m_near.value(t);
    QStringList out;
    static const QRegularExpression shape(QStringLiteral("^[a-z][a-z0-9]{3,11}$"));
    if (shape.match(t).hasMatch()) {
        for (const QString &v : m_vocab) {
            if (qAbs(v.size() - t.size()) > 1)
                continue;
            if (dist1(t, v)) {
                out << v;
                if (out.size() >= 3)
                    break;
            }
        }
    }
    m_near.insert(t, out);
    return out;
}

double SearchEngine::dfOf(const QString &t) const
{
    if (m_df.contains(t))
        return m_df.value(t);
    int c = 0;
    const int n = m_cards ? m_cards->size() : 0;
    for (int i = 0; i < n; ++i)
        if (m_cards->at(i).hay.contains(t))
            ++c;
    m_df.insert(t, c);
    return c;
}

double SearchEngine::weightOf(const QString &t) const
{
    const double n = m_cards ? m_cards->size() : 1;
    const double raw = 1.0 + 0.25 * std::log((n + 1.0) / (1.0 + dfOf(t)));
    return qBound(0.4, raw, 2.0);
}

QStringList SearchEngine::conceptsOf(const QString &w) const
{
    QStringList out;
    const auto add = [&out](const QStringList &arr) {
        for (const QString &e : arr)
            if (!out.contains(e))
                out << e;
    };
    const QString fixed = fixTypo(w);
    if (m_t->conceptMap.contains(fixed))
        add(m_t->conceptMap.value(fixed));
    if (m_t->conceptMap.contains(w))
        add(m_t->conceptMap.value(w));
    if (fixed.size() >= 2) {
        QStringList keys;
        for (auto it = m_t->conceptMap.begin(); it != m_t->conceptMap.end(); ++it) {
            const QString k = it.key();
            if (k.size() >= 2 && k != fixed && fixed.contains(k))
                keys << k;
        }
        std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
            return a.size() > b.size();
        });
        for (const QString &k : keys)
            add(m_t->conceptMap.value(k));
    }
    return out;
}

QStringList SearchEngine::variantGroups(const QString &w) const
{
    QStringList g;
    const auto add = [&g](const QString &x) {
        if (!x.isEmpty() && !g.contains(x))
            g << x;
    };
    const QString fixed = fixTypo(w);
    add(w);
    add(fixed);
    if (m_t->syn.contains(w))
        for (const QString &x : m_t->syn.value(w)) add(x);
    if (m_t->syn.contains(fixed))
        for (const QString &x : m_t->syn.value(fixed)) add(x);
    for (const QString &x : conceptsOf(w)) add(x);
    if (m_t->alias.contains(w))
        for (const QString &x : m_t->alias.value(w)) add(x);
    if (m_t->alias.contains(fixed))
        for (const QString &x : m_t->alias.value(fixed)) add(x);
    for (const QString &x : nearWords(fixed)) add(x);
    const QStringList snapshot = g;      /* 网页是 g.slice().forEach(...)，先取快照 */
    for (const QString &t : snapshot)
        for (const QString &x : conceptsOf(t)) add(x);
    return g;
}

QStringList SearchEngine::expandWords(const QStringList &words) const
{
    QStringList out;
    for (const QString &w : words)
        for (const QString &e : conceptsOf(w))
            if (!out.contains(e))
                out << e;
    return out;
}

int SearchEngine::hitTier(const Card &c, const QStringList &group) const
{
    const QStringList &nt = m_nameTok.at(c.i);
    const QString     &nl = m_nameLow.at(c.i);
    static const QRegularExpression shortRe(QStringLiteral("^[a-z0-9]{1,3}$"));
    int best = 0;
    for (const QString &t : group) {
        if (t.isEmpty())
            continue;
        const bool isShort = shortRe.match(t).hasMatch();
        if (nt.contains(t)) { best = 3; break; }
        bool prefix = false;
        if (t.size() >= 3)
            for (const QString &x : nt)
                if (x.startsWith(t)) { prefix = true; break; }
        if (prefix) { best = qMax(best, 2); continue; }
        if (!isShort && nl.contains(t)) { best = qMax(best, 2); continue; }
        if (best < 1 && hasTok(c.hay, t)) best = 1;
    }
    return best;
}

int SearchEngine::bagNear(const QString &hay, const QStringList &words, int maxGap) const
{
    QVector<int> pos;
    for (const QString &w : words) {
        const int i = hay.indexOf(w);
        if (i < 0)
            return 0;
        pos << i;
    }
    std::sort(pos.begin(), pos.end());
    int maxSpan = 0;
    for (int i = 1; i < pos.size(); ++i)
        maxSpan = qMax(maxSpan, pos[i] - pos[i - 1]);
    if (pos.size() > 1 && maxSpan > maxGap)
        return 0;
    return pos.size();
}

SearchEngine::Ev SearchEngine::evalEntry(const Card &c) const
{
    Ev e;
    double cov = 0, covReq = 0;
    for (int i = 0; i < m_cur.g.size(); ++i) {
        const QStringList &g = m_cur.g.at(i);
        const int pT = hitTier(c, QStringList { g.value(0) });
        const int eT = (g.size() > 1) ? hitTier(c, g.mid(1)) : 0;
        if (!pT && !eT)
            continue;
        const double w = m_cur.w.at(i);
        cov += w;
        if (m_cur.req.at(i) && (pT >= 1 || eT >= 2))
            covReq += w;
        e.sP += w * (pT >= 3 ? 70 : pT == 2 ? 40 : pT == 1 ? 18 : 0);
        e.sE += w * (eT >= 3 ? 28 : eT == 2 ? 16 : eT == 1 ? 6 : 0);
        if (e.firstPT < 0) {           /* 网页里 hits[0].pT 参与排序 */
            e.firstPT = pT;
            e.firstET = eT;
        }
    }
    e.near = (m_cur.heads.size() > 1 && bagNear(c.hay, m_cur.heads, 40)) ? 1 : 0;
    e.cov = (m_cur.tot > 0) ? qMin(1.0, cov / m_cur.tot) : 0.0;
    e.covReq = (m_cur.reqTot > 0) ? qMin(1.0, covReq / m_cur.reqTot) : 1.0;
    return e;
}

double SearchEngine::scoreOf(const Card &c, const QStringList &words, const Ev &e,
                             const QString &queryLower, int section,
                             QVector<Term> *terms) const
{
    /* D3：每一项都**记下来**（人话 + 它对分数的贡献），零贡献的不记（记了只是噪音）。
       累加顺序与原来逐条 `+=` 完全一致 —— 这样 `sum(terms) == 返回的分数` 才能当断言用，
       "为什么这条排第一"才是算出来的、不是事后编的。 */
    const auto R = [terms](const QString &text, double add) {
        if (terms && add != 0)
            terms->append(Term { text, add });
        return add;
    };
    const QString &nl = m_nameLow.at(c.i);
    double s = 0;
    s += R(QStringLiteral("词命中（名字全命中 70 / 前缀 40 / 正文 18，按词权重加权）"), e.sP);
    s += R(QStringLiteral("概念展开词命中"), e.sE);
    s += R(QStringLiteral("覆盖度 %1 × 30").arg(e.cov, 0, 'f', 2), e.cov * 30);
    s += R(QStringLiteral("必中词覆盖度 %1 × 20").arg(e.covReq, 0, 'f', 2), e.covReq * 20);
    if (e.near)
        s += R(QStringLiteral("查询词在正文里挨得近（40 字内）"), 20);
    if (words.size() > 1 && nl.contains(words.join(QString())))
        s += R(QStringLiteral("名字连写命中（整串连着写）"), 60);

    /* 板块分离 */
    if (c.t == QStringLiteral("guide"))
        s += R(section == int(Card::Guide) ? QStringLiteral("板块=引导教程：向导卡加分")
                                           : QStringLiteral("板块≠引导教程：向导卡剔除"),
               (section == int(Card::Guide)) ? 400 : -5000);
    else if (c.t == QStringLiteral("flow"))
        s += R(section == int(Card::Guide) ? QStringLiteral("板块=引导教程：流程卡加分")
                                           : QStringLiteral("流程卡在资料区（弱加分）"),
               (section == int(Card::Guide)) ? 60 : 10);
    else if (c.t == QStringLiteral("datasheet"))
        s += R(QStringLiteral("数据手册卡"), 30);
    else if (c.src == QStringLiteral("core") && c.t != QStringLiteral("table"))
        s += R(QStringLiteral("手写条目（不是自动收录）"), 4);

    if (c.src == QStringLiteral("core") && !c.a.isEmpty()
        && c.t != QStringLiteral("table") && c.t != QStringLiteral("datasheet"))
        s += R(QStringLiteral("手写条目且带参数表"), 3);

    static const QRegularExpression typedefTail(QStringLiteral("typedef$"),
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression structAsk(
        QStringLiteral("结构体|成员|字段|typedef|定义|参数"));
    if (typedefTail.match(c.n).hasMatch() && !structAsk.match(queryLower).hasMatch())
        s += R(QStringLiteral("名字以 typedef 结尾且没问结构体（降权）"), -14);

    if (c.t == QStringLiteral("table") && m_t->bigTableRe.match(c.n).hasMatch()) {
        static const QRegularExpression seek(QStringLiteral("表|向量|宏|映射|分组|扇区|线"));
        bool tableSeeking = false;
        for (const QString &w : words)
            if (seek.match(w).hasMatch()) { tableSeeking = true; break; }
        s += R(tableSeeking ? QStringLiteral("大表且查询里有「表/向量/宏…」（不降权）")
                            : QStringLiteral("大表但查询里没有「表/向量/宏…」（降权）"),
               tableSeeking ? 0 : -60);
    }

    if (c.src == QStringLiteral("auto") && section == int(Card::Ref) && !words.isEmpty())
        s += R(QStringLiteral("自动收录条目（降权）"), -8);
    if (c.src == QStringLiteral("autostruct") && section == int(Card::Ref) && !words.isEmpty())
        s += R(QStringLiteral("自动收录的结构体（降权）"), -12);

    /* 概念干扰屏蔽 */
    if (!words.isEmpty()) {
        for (const QString &w : words) {
            QVector<QRegularExpression> bl;
            if (m_t->conceptBlock.contains(w))
                bl = m_t->conceptBlock.value(w);
            else {
                const QString f = fixTypo(w);
                if (m_t->conceptBlock.contains(f))
                    bl = m_t->conceptBlock.value(f);
            }
            bool blocked = false;
            for (const QRegularExpression &re : bl)
                if (re.match(c.n).hasMatch()) { blocked = true; break; }
            if (blocked) {
                s += R(QStringLiteral("命中概念屏蔽名单（名字是干扰项）"), -90);
                break;
            }
        }
    }

    /* 泛化卡片（一览表/映射表…）在搜具体概念时降权 */
    if (!words.isEmpty() && m_t->genericName.match(c.n).hasMatch()) {
        bool spec = false;
        for (const QString &w : words)
            if (m_t->genericDown.contains(fixTypo(w))) { spec = true; break; }
        if (spec)
            s += R(QStringLiteral("泛化卡（一览表/映射表…）遇到具体概念（降权）"), -120);
    }
    return s;
}

QStringList SearchEngine::Explain::reasonTexts() const
{
    QStringList out;
    for (const Term &tm : terms) {
        const QString n = QString::number(tm.add, 'g', 6);
        out << tm.text + QStringLiteral("  ") + (tm.add >= 0 ? QStringLiteral("+") + n : n);
    }
    return out;
}

/* ------------------------------------------------------------------ */
/*  D3：倒排索引（3 字窗口）与候选集收缩                                  */
/* ------------------------------------------------------------------ */

namespace {

/* 3 个小写字母数字打包成一个 key（只用这种窗口才进索引） */
inline quint64 gramKey(const QString &s, int i)
{
    return quint64(s.at(i).unicode())
         | (quint64(s.at(i + 1).unicode()) << 16)
         | (quint64(s.at(i + 2).unicode()) << 32);
}
inline bool isLowerAlnum(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9');
}
/* 这个词能不能靠 3 字窗口缩候选：纯小写字母数字、且 ≥3 字。
   短词（1~2 字，还有"中断"这种 2 字中文）命中面太宽，不进索引，改用逐条测。 */
inline bool grammable(const QString &t)
{
    if (t.size() < 3)
        return false;
    for (const QChar c : t)
        if (!isLowerAlnum(c))
            return false;
    return true;
}

/* 含 t 这个**子串**的条目（hay 或名字里都有索引）。
   为什么子串口径就够：对 len≥3 的纯小写字母数字 token，hitTier 的三种命中
   （名字全命中 / 名字前缀 / 名字含它 / 正文含它）**都是"某处出现 t 这个子串"**的子集，
   所以交集结果只会大不会小 —— 候选集只要不漏，最终结果就一个字都不差（多出来的会因
   cov=0 被 tight/loose 挡掉）。 */
QVector<int> gramCandidates(const QHash<quint64, QVector<int>> &g, const QString &t)
{
    const int m = t.size() - 2;
    QVector<const QVector<int> *> lists;
    lists.reserve(m);
    for (int p = 0; p < m; ++p) {
        const auto it = g.constFind(gramKey(t, p));
        if (it == g.constEnd())
            return QVector<int>();          /* 少一个窗口 → 不可能含 t */
        lists << &it.value();
    }
    std::sort(lists.begin(), lists.end(),
              [](const QVector<int> *a, const QVector<int> *b) { return a->size() < b->size(); });
    QVector<int> out = *lists.first();
    for (int li = 1; li < lists.size(); ++li) {
        const QVector<int> &cur = *lists.at(li);
        QVector<int> keep;
        keep.reserve(out.size());
        for (int v : out)
            if (std::binary_search(cur.begin(), cur.end(), v))
                keep << v;
        out = keep;
        if (out.isEmpty())
            break;
    }
    return out;
}

} // namespace

void SearchEngine::buildIndex() const
{
    if (m_gramBuilt)
        return;
    m_gramBuilt = true;                     /* 先置位：置位后即使中途出错也不反复重建 */
    if (!m_cards)
        return;
    const int n = m_cards->size();
    QSet<quint64> seen;                     /* 同一张卡里同一窗口只收一次 → postings 严格升序 */
    const auto addStr = [&seen, this](const QString &s, int i) {
        seen.clear();
        const int m = s.size() - 2;
        for (int p = 0; p < m; ++p) {
            if (!isLowerAlnum(s.at(p)) || !isLowerAlnum(s.at(p + 1)) || !isLowerAlnum(s.at(p + 2)))
                continue;
            const quint64 k = gramKey(s, p);
            if (seen.contains(k))
                continue;
            seen.insert(k);
            m_gram[k].append(i);
        }
    };
    for (int i = 0; i < n; ++i) {
        addStr(m_cards->at(i).hay, i);           /* hay 全小写（数据导出口径） */
        if (i < m_nameLow.size())
            addStr(m_nameLow.at(i), i);          /* 名字也要索引：tier2/3 靠名字命中 */
    }
}

QVector<int> SearchEngine::narrow(const QVector<int> &base, const QStringList &toks) const
{
    buildIndex();
    QSet<int> inBase;
    inBase.reserve(base.size() * 2);
    for (int i : base)
        inBase.insert(i);
    QVector<int> cand;
    QSet<int> seen;
    for (const QString &t : toks) {
        if (t.isEmpty())
            continue;
        if (grammable(t)) {
            const QVector<int> part = gramCandidates(m_gram, t);
            for (int i : part)
                if (inBase.contains(i) && !seen.contains(i)) {
                    seen.insert(i);
                    cand << i;
                }
        } else {
            /* 缩不动的词：老实扫一遍本板块，但**只测这一个词** ——
               比"每张卡都跑 evalEntry（含全部词 + 邻近词 + 正则）"便宜得多。 */
            for (int i : base) {
                if (seen.contains(i))
                    continue;
                if (hitTier(m_cards->at(i), QStringList { t }) != 0) {
                    seen.insert(i);
                    cand << i;
                }
            }
        }
    }
    std::sort(cand.begin(), cand.end());    /* 升序 → 与旧路径"按 base 顺序遍历"逐条一致 */
    return cand;
}

void SearchEngine::prepare(const QStringList &words) const
{
    m_cur = Cur();
    m_cur.g.reserve(words.size());
    for (const QString &w : words)
        m_cur.g.append(variantGroups(w));
    for (const QString &w : words)
        m_cur.w.append(weightOf(w));
    m_cur.heads = words;

    for (const QString &w : words) {
        const QString f = fixTypo(w);
        m_cur.req.append(!m_t->action.contains(w) && !m_t->action.contains(f));
    }
    if (!words.isEmpty()) {
        bool anyReq = false;
        for (bool b : m_cur.req)
            if (b) { anyReq = true; break; }
        if (!anyReq)
            m_cur.req.fill(true);
    }
    double tot = 0, reqTot = 0;
    for (int i = 0; i < m_cur.w.size(); ++i) {
        tot += m_cur.w.at(i);
        if (m_cur.req.at(i))
            reqTot += m_cur.w.at(i);
    }
    m_cur.tot = tot;
    m_cur.reqTot = reqTot;
}

QStringList SearchEngine::reasonsFor(const QString &query, int cardIndex, int section) const
{
    if (!m_cards || cardIndex < 0 || cardIndex >= m_cards->size())
        return QStringList();
    const QStringList words = tokenize(query);
    if (words.isEmpty())
        return QStringList();
    prepare(words);
    const Card &c = m_cards->at(cardIndex);
    const Ev e = evalEntry(c);
    QVector<Term> terms;
    scoreOf(c, words, e, query.toLower(), section, &terms);
    Explain x;
    x.terms = terms;
    return x.reasonTexts();
}

/* ------------------------------------------------------------------ */
/*  主入口                                                              */
/* ------------------------------------------------------------------ */

SearchEngine::Result SearchEngine::run(const QString &query, int section,
                                       const QString &module, bool useIndex) const
{
    Result res;
    res.words = tokenize(query);
    res.expanded = expandWords(res.words);

    prepare(res.words);                     /* D3：从 run() 里抽出来，explain/reasonsFor 共用 */

    /* 板块归属（对照网页 inSec；项目引导板块不走这里，由界面直接列手册卡） */
    const auto inSec = [section](const Card &c) {
        if (section == int(Card::Guide))
            return (c.t == QStringLiteral("guide") || c.t == QStringLiteral("flow")) && c.b.isEmpty();
        return c.t != QStringLiteral("guide") && c.t != QStringLiteral("flow") && c.b.isEmpty();
    };

    QVector<int> base;
    for (int i = 0; i < m_cards->size(); ++i) {
        const Card &c = m_cards->at(i);
        if (!inSec(c))
            continue;
        if (!module.isEmpty() && modKeyOf(c) != module)
            continue;
        base << i;
    }

    QVector<int> arr;
    if (!res.words.isEmpty()) {
        /* D3：先用倒排索引把候选集缩到"至少命中一个查询词"的条目 ——
           原来是**每张卡都跑 evalEntry**（含全部词、变体、邻近词和短词正则），
           条目一多就线性变慢。缩完只对候选跑 evalEntry；漏不漏由上面的论证 +
           自检里的"索引版 vs 朴素版逐条对照"两头钉住。 */
        QVector<int> cand;
        if (useIndex) {
            QStringList toks;
            for (const QStringList &g : m_cur.g)
                for (const QString &t : g)
                    if (!t.isEmpty() && !toks.contains(t))
                        toks << t;
            cand = narrow(base, toks);
            res.narrowed = true;
        } else {
            cand = base;                    /* 自检对照用：老路径 */
        }
        res.scanned = cand.size();

        QVector<int> tight, loose;
        QHash<int, Ev> ev;
        for (int i : cand) {
            const Ev e = evalEntry(m_cards->at(i));
            ev.insert(i, e);
            if (e.covReq >= 0.999)
                tight << i;
            else if (e.covReq >= 0.6)
                loose << i;
        }
        if (!tight.isEmpty()) {
            arr = tight + loose;
            res.relaxed = !loose.isEmpty();
        } else if (!loose.isEmpty()) {
            arr = loose;
            res.relaxed = true;
        }
        if (arr.isEmpty()) {
            for (int i : cand) {
                bool any = false;
                for (const QStringList &g : m_cur.g) {
                    for (const QString &t : g)
                        if (hasTok(m_cards->at(i).hay, t)) { any = true; break; }
                    if (any)
                        break;
                }
                if (any) {
                    arr << i;
                    ev.insert(i, evalEntry(m_cards->at(i)));
                }
            }
            res.relaxed = !arr.isEmpty();
        }

        /* 预打分（网页在比较器里现算，值一样，这里算一次） */
        QHash<int, double> sc;
        for (int i : arr)
            sc.insert(i, scoreOf(m_cards->at(i), res.words, ev.value(i), query.toLower(), section));

        std::stable_sort(arr.begin(), arr.end(), [&](int ai, int bi) {
            const Ev &ea = ev[ai];
            const Ev &eb = ev[bi];
            const int fa = (ea.covReq >= 0.999) ? 1 : 0;
            const int fb = (eb.covReq >= 0.999) ? 1 : 0;
            if (fb != fa)
                return fb < fa;
            const int na = (ea.firstPT >= 2) ? 1 : 0;
            const int nb = (eb.firstPT >= 2) ? 1 : 0;
            if (nb != na)
                return nb < na;
            const double d = sc.value(bi) - sc.value(ai);
            if (d != 0)
                return d < 0;
            return m_cards->at(ai).ord < m_cards->at(bi).ord;
        });
    } else {
        arr = base;
        std::stable_sort(arr.begin(), arr.end(), [this](int a, int b) {
            return m_cards->at(a).ord < m_cards->at(b).ord;
        });
    }

    /* 宽泛词判定 */
    if (!res.words.isEmpty()) {
        bool hasBroad = false, hasNarrow = false;
        for (const QString &w : res.words) {
            if (m_t->broad.contains(w))
                hasBroad = true;
            else if (!m_t->action.contains(w))
                hasNarrow = true;
        }
        res.broad = hasBroad && !hasNarrow && arr.size() > 60;
    }

    res.order = arr;
    return res;
}

QVector<SearchEngine::Explain> SearchEngine::explain(const QString &query, int section,
                                                      const QString &module) const
{
    const Result r = run(query, section, module);       /* run() 会把 m_cur 置为本次查询状态 */
    QVector<Explain> out;
    const QString ql = query.toLower();
    for (int i : r.order) {
        const Card &c = m_cards->at(i);
        const Ev e = evalEntry(c);
        Explain x;
        x.index   = i;
        x.tier    = (e.covReq >= 0.999) ? 1 : 0;
        x.firstPT = e.firstPT;
        x.cov     = e.cov;
        x.covReq  = e.covReq;
        x.score   = scoreOf(c, r.words, e, ql, section, &x.terms);   /* D3：顺手记下分数怎么来的 */
        x.ord     = c.ord;
        x.t       = c.t.isEmpty() ? QStringLiteral("(func)") : c.t;
        x.src     = c.src;
        out << x;
    }
    return out;
}

QStringList SearchEngine::hitWords(const QString &query, int cardIndex, int section) const
{
    QStringList out;
    const Card &c = m_cards->at(cardIndex);
    const QStringList words = tokenize(query);
    for (const QString &w : words) {
        const QStringList g = variantGroups(w);
        const int pT = hitTier(c, QStringList { g.value(0) });
        const int eT = (g.size() > 1) ? hitTier(c, g.mid(1)) : 0;
        if (!pT && !eT)
            continue;
        out << QStringLiteral("%1·%2").arg(pT >= 2 ? QStringLiteral("名称") : QStringLiteral("正文"), w);
    }
    (void)section;
    return out;
}

QString SearchEngine::topicOf(const Card &c) const
{
    if (m_topic.contains(c.n))
        return m_topic.value(c.n);
    const QString name = c.n.toLower();
    const QString head = name.split(QRegularExpression(QStringLiteral("[（(]"))).value(0);
    const QString all  = name + QLatin1Char(' ') + c.k.toLower() + QLatin1Char(' ') + c.d.toLower();
    QString hit;
    for (const auto &rule : m_t->topicRules)
        if (rule.second.match(head).hasMatch()) { hit = rule.first; break; }
    if (hit.isEmpty())
        for (const auto &rule : m_t->topicRules)
            if (rule.second.match(name).hasMatch()) { hit = rule.first; break; }
    if (hit.isEmpty())
        for (const auto &rule : m_t->topicRules)
            if (rule.second.match(all).hasMatch()) { hit = rule.first; break; }
    if (hit.isEmpty())
        hit = QStringLiteral("其他");
    m_topic.insert(c.n, hit);
    return hit;
}

QString SearchEngine::modKeyOf(const Card &c) const
{
    if (c.t == QStringLiteral("guide") || c.t == QStringLiteral("flow"))
        return topicOf(c);
    return c.m;
}
