#ifndef DATASET_H
#define DATASET_H

/* 数据层：读 tools/export_json.js 生成的 data/dataset.json。
 *
 * 字段名沿用网页版的单字母（见「标准库速查」data_*.js 的维护说明），JSON 里怎么存就怎么读，
 * 不改名 —— 免得和网页端 schema 漂移。
 * 合并逻辑（去重、打类型标签、拼检索语料 _hay、挂使用位置/联动）已经在导出时由网页引擎跑过，
 * 这里只做装载，不重算。 */

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct Card {
    /* ---- 网页版原始字段 ---- */
    QString     m;      /* 模块分类 */
    QString     n;      /* 名称（函数名 / 卡片名） */
    QString     p;      /* 原型（结构体卡是用法提示） */
    QString     d;      /* 中文用途 */
    QString     r;      /* 返回值说明 */
    QString     e;      /* 示例代码（\n 已还原成换行） */
    QString     k;      /* 搜索关键词 */
    QString     t;      /* 类型：flow / table / struct / guide / datasheet / chip；空 = 普通函数条目 */
    QString     g;      /* 引导卡的目标/最终形态 */
    QString     b;      /* 手册工程标签（板1 / 板2 / Qt）；空 = 非手册条目 */
    QString     s;      /* 少量条目的补充说明 */
    QString     hay;    /* 检索语料（网页引擎拼好的，直接复用，不重算） */
    QString     src;    /* _src：core / auto / autostruct —— 打分时区分手写条目与自动收录 */
    QStringList h;      /* 查找表表头 */

    QVector<QVector<QString>> a;   /* 参数表 / 查找表数据行 / 流程核对表 */
    QVector<QVector<QString>> f;   /* 步骤：flow 为 [步骤, 做法]；guide/手册为 7 列 */

    int  i     = 0;     /* _i 原始下标 */
    int  ord   = 0;     /* _ord 排序权重 */
    bool hasDoc = false;
    int  docA   = 0;

    struct Use { QString tag, file, what; int line = 0; };
    QVector<Use> u;           /* 本项目使用位置 */
    /* D2：u 是不是来自**本仓库真实代码索引**（code_usage.json）。
       为 true 时手册页按"真实使用位置"渲染（有位置给可点链接、没有就明说没出现）；
       为 false 时沿用 dataset.json 里网页版自带的 u（兼容没跑索引的情形）。 */
    bool         uReal = false;
    QStringList  rel;         /* 相关与联动（条目名） */

    /* ---- 判定 ---- */
    /* 注意 t 是 dataset.cpp 从**合并后的 _t** 读来的：普通函数条目的 t 是 "func"，
       不是空串（网页引擎给所有条目都打了 _t）。所以判"是不是函数"必须认 "func"，
       早先写成 t.isEmpty() 会让 794 条函数条目全部判成"非函数"（踩过）。 */
    bool isFunc()   const { return t.isEmpty() || t == QStringLiteral("func"); }
    bool isFlow()   const { return t == QStringLiteral("flow"); }
    bool isGuide()  const { return t == QStringLiteral("guide"); }
    bool isTable()  const { return t == QStringLiteral("table"); }
    bool isStruct() const { return t == QStringLiteral("struct"); }
    bool isDs()     const { return t == QStringLiteral("datasheet"); }
    bool isChip()   const { return t == QStringLiteral("chip"); }
    bool isManu()   const { return !b.isEmpty(); }   /* 手册阶段卡 */

    /* 板块归属：资料查询 / 引导教程 / 项目引导 */
    enum Section { Ref = 0, Guide = 1, Build = 2 };
    Section section() const
    {
        if (isManu())                          return Build;
        if (isFlow() || isGuide())             return Guide;
        return Ref;
    }
};

class Dataset {
public:
    /* 装载；失败时 error() 给出原因 */
    bool load(const QString &path);

    /* D2：把本仓库的代码索引（code_usage.json）套进每张卡 —— 用它替换 Card.u，
       并把 uReal 置 true（文件在就说清"这些是真实索引"，空的条目好如实写"没出现"）。
       文件不存在 / 打不开 / 解析失败 → **原样不动**（uReal 保持 false），不报错、不清空。
       返回用到过位置的**条目数**。 */
    int applyUsage(const QString &path);

    bool           isLoaded() const { return m_loaded; }
    QString        error()    const { return m_error; }
    int            size()     const { return m_cards.size(); }
    const Card    &at(int i)  const { return m_cards.at(i); }
    const QVector<Card> &cards() const { return m_cards; }

    QJsonObject search() const { return m_search; }   /* 搜索用表（同义词/错别字/分词词典…） */
    QJsonArray  proj()   const { return m_proj; }      /* 项目引导的工程清单 */

    /* 某板块的条目（按 _ord / _i 预排好） */
    QVector<int> indicesOfSection(int section) const;
    /* 该板块里出现过的模块名（下拉筛选用），按出现顺序 */
    QStringList modulesOfSection(int section) const;
    /* 按名字找条目下标（相关与联动点击跳转用），找不到返回 -1 */
    int indexOfName(const QString &name) const;

    /* 统计（自检与状态栏用） */
    int countType(const QString &t) const;
    int countManu() const;

    QString sourcePath() const { return m_path; }

private:
    QVector<Card>  m_cards;
    QJsonObject    m_search;
    QJsonArray     m_proj;
    QString        m_path;
    QString        m_error;
    bool           m_loaded = false;
    QHash<QString, int> m_byName;
};

#endif // DATASET_H
