#ifndef SEARCHENGINE_H
#define SEARCHENGINE_H

/* 搜索引擎：逐函数对照网页版「标准库函数速查.html」重写，目标是与网页结果一致。
 *
 * 网页里的对应实现（行号指 标准库函数速查.html）：
 *   分词/词库     L1073-1228  ACTION/BROAD/SYN/ALIAS/CH_WORDS/SEG_DICT/segCN/tokenize/nameTokens
 *   命中判定      L1243-1267  hasTok / dist1
 *   加权与展开    L1270-1338  vocab/nearWords/dfOf/weightOf/conceptsOf/variantGroups/expandWords
 *   打分          L1340-1440  hitTier/bagNear/evalEntry/scoreOf
 *   主入口        L1442-1504  filtered()
 *   主题归类      L1525-1568  topicOf / modKeyOf
 *
 * 表全部来自 SearchTables（随数据导出），这里只放逻辑。 */

#include "dataset.h"
#include "searchtables.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class SearchEngine
{
public:
    SearchEngine(const QVector<Card> *cards, const SearchTables *tables);

    struct Result {
        QVector<int> order;      /* 结果条目下标，已排序 */
        QStringList  words;      /* 分词结果 */
        QStringList  expanded;   /* 概念展开出的英文词 */
        bool         relaxed = false;   /* 是否放宽了"必须命中" */
        bool         broad   = false;   /* 是否只命中宽泛词（结果很多） */
    };

    /* query 为空 → 按 _ord 列出该板块全部；
       section 见 Card::Section；module 为空表示不筛模块 */
    Result run(const QString &query, int section, const QString &module = QString()) const;

    /* 列表右侧的"命中原因"：返回命中的词（名字命中优先） */
    QStringList hitWords(const QString &query, int cardIndex, int section) const;

    /* 排查用：把一次搜索的结果连同打分明细一起给出（对照网页 oracle 定位规则差异） */
    struct Explain { int index = 0; int tier = 0; int firstPT = -1;
                     double cov = 0, covReq = 0, score = 0; int ord = 0;
                     QString t, src; };
    QVector<Explain> explain(const QString &query, int section,
                             const QString &module = QString()) const;

    /* 主题归类（引导/流程卡按主题筛模块；其余按 m 筛） */
    QString topicOf(const Card &c) const;
    QString modKeyOf(const Card &c) const;

    /* 为自检暴露的分词结果 */
    QStringList tokenize(const QString &raw) const;
    QStringList nameTokens(const QString &name) const;

private:
    /* ---- 基础 ---- */
    bool     hasTok(const QString &text, const QString &t) const;
    static bool dist1(const QString &a, const QString &b);
    QStringList segCN(const QString &run) const;
    QStringList conceptsOf(const QString &w) const;
    QStringList variantGroups(const QString &w) const;
    QStringList expandWords(const QStringList &words) const;
    QStringList nearWords(const QString &t) const;
    QString     fixTypo(const QString &w) const { return m_t->typo.value(w, w); }

    double dfOf(const QString &t) const;
    double weightOf(const QString &t) const;
    int    hitTier(const Card &c, const QStringList &group) const;
    int    bagNear(const QString &hay, const QStringList &words, int maxGap) const;

    struct Ev { double cov = 0, covReq = 0, sP = 0, sE = 0; int near = 0;
                int firstPT = -1, firstET = 0; };   /* 首个命中词的层级（网页 hits[0].pT 参与排序） */
    Ev evalEntry(const Card &c) const;
    double scoreOf(const Card &c, const QStringList &words, const Ev &e,
                   const QString &queryLower, int section) const;

    const QVector<Card> *m_cards = nullptr;
    const SearchTables  *m_t = nullptr;

    /* 派生缓存（构造时建一次） */
    QStringList          m_vocab;    /* 名字词表，保序（nearWords 取前 3 个，顺序重要） */
    QVector<QStringList> m_nameTok;  /* 每条目的名字分词 */
    QVector<QString>     m_nameLow;  /* 每条目名字小写 */
    mutable QHash<QString, double> m_df;      /* 懒算 */
    mutable QHash<QString, QStringList> m_near;/* 懒算 */
    mutable QHash<QString, QRegularExpression> m_bound;  /* hasTok 边界正则缓存 */
    mutable QHash<QString, QString> m_topic;  /* 条目名 → 主题 */

    /* filtered() 的临时状态（对照 JS 的 CUR_*） */
    struct Cur {
        QVector<QStringList> g;
        QVector<double>      w;
        QStringList          heads;
        QVector<bool>        req;
        double               tot = 1, reqTot = 1;
    };
    mutable Cur m_cur;
};

#endif // SEARCHENGINE_H
