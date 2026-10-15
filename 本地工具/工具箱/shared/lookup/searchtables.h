#ifndef SEARCHTABLES_H
#define SEARCHTABLES_H

/* 搜索引擎要用的"表"：全部随 dataset.json 的 search 段一起导出，
   C++ 侧不再手抄一份（手抄必然和网页端漂移）。
   JSON 里正则统一存成 {__re:"...", f:"flags"}，这里编译成 QRegularExpression。 */

#include <QHash>
#include <QJsonObject>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class SearchTables
{
public:
    bool load(const QJsonObject &search);

    bool ready() const { return m_ready; }

    /* 词表（concept 避开 C++20 关键字） */
    QHash<QString, QStringList> conceptMap;/* 中文概念 → 英文/别名列表 */
    QHash<QString, QString>    typo;       /* 错别字 → 正确写法 */
    QHash<QString, QStringList> syn;       /* 中文近义词 */
    QHash<QString, QStringList> alias;     /* 英文拼写变体 */
    QSet<QString>              action;     /* 动作词：不参与"必须命中" */
    QSet<QString>              broad;      /* 极宽泛词 */
    QStringList                segDict;    /* 分词词典：按长度降序 */
    QSet<QString>              segSet;
    QStringList                genericDown;/* 这些概念词下"一览表"类降权 */

    /* 规则 */
    QRegularExpression                          genericName;   /* 一览表 / 总表 / 映射表 */
    QRegularExpression                          bigTableRe;    /* 大查找表（按表名判断） */
    QHash<QString, QVector<QRegularExpression>> conceptBlock;  /* 概念 → 名字命中就扣分的模式 */
    QVector<QPair<QString, QRegularExpression>> topicRules;    /* 主题归类规则（有先后，先命中先用） */

private:
    bool m_ready = false;
};

#endif // SEARCHTABLES_H
