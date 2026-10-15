#include "searchtables.h"

#include <QJsonArray>
#include <QJsonValue>

namespace {

QStringList toList(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &x : v.toArray())
        out << x.toString();
    return out;
}

QHash<QString, QStringList> toMapList(const QJsonValue &v)
{
    QHash<QString, QStringList> m;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
        m.insert(it.key(), toList(it.value()));
    return m;
}

QHash<QString, QString> toMapStr(const QJsonValue &v)
{
    QHash<QString, QString> m;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
        m.insert(it.key(), it.value().toString());
    return m;
}

/* {__re, f} → 正则 */
QRegularExpression makeRe(const QJsonValue &v)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        const QString src = o.value(QStringLiteral("__re")).toString();
        const QString fl  = o.value(QStringLiteral("f")).toString();
        QRegularExpression::PatternOptions opt = QRegularExpression::NoPatternOption;
        if (fl.contains(QLatin1Char('i')))
            opt |= QRegularExpression::CaseInsensitiveOption;
        return QRegularExpression(src, opt);
    }
    return QRegularExpression(v.toString());
}

} // namespace

bool SearchTables::load(const QJsonObject &search)
{
    if (search.isEmpty())
        return false;

    conceptMap = toMapList(search.value(QStringLiteral("concept")));
    typo       = toMapStr (search.value(QStringLiteral("typo")));
    syn        = toMapList(search.value(QStringLiteral("syn")));
    alias      = toMapList(search.value(QStringLiteral("alias")));

    for (const QString &w : toList(search.value(QStringLiteral("action"))))
        action.insert(w);
    for (const QString &w : toList(search.value(QStringLiteral("broad"))))
        broad.insert(w);

    /* CH_WORDS 在网页里是 split(',') 的结果；但万一导出成字符串，这里兼容处理 */
    const QJsonValue cw = search.value(QStringLiteral("chWords"));
    QStringList chWords = cw.isArray() ? toList(cw)
                                       : cw.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);

    segDict = toList(search.value(QStringLiteral("segDict")));
    for (const QString &w : segDict)
        segSet.insert(w);

    genericDown = toList(search.value(QStringLiteral("genericDown")));

    genericName = QRegularExpression(search.value(QStringLiteral("genericName")).toString());
    bigTableRe  = QRegularExpression(search.value(QStringLiteral("bigTableRe")).toString());

    /* CONCEPT_BLOCK 在网页里是**对象**：{ '组帧': [/^SAI_/, /FrameInit$/, ...], ... }
       —— 注意不是"数组的[键,值]对"。早先按数组解析会拿到空表，导致
       "概念干扰屏蔽"（命中就 -90）这条规则整条失效，尾部排序会和网页不一致。 */
    const QJsonObject cb = search.value(QStringLiteral("conceptBlock")).toObject();
    for (auto it = cb.begin(); it != cb.end(); ++it) {
        QVector<QRegularExpression> res;
        for (const QJsonValue &r : it.value().toArray())
            res << makeRe(r);
        if (!res.isEmpty())
            conceptBlock.insert(it.key(), res);
    }

    for (const QJsonValue &v : search.value(QStringLiteral("topicRules")).toArray()) {
        const QJsonArray pair = v.toArray();
        if (pair.size() < 2)
            continue;
        topicRules.append(qMakePair(pair.at(0).toString(), makeRe(pair.at(1))));
    }

    (void)chWords;   /* 只用来拼 SEG_DICT；词典已经随 SEG_DICT 导出，这里不再单独用 */
    m_ready = !segDict.isEmpty();
    return m_ready;
}
