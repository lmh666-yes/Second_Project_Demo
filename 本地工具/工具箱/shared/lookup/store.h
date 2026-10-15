#ifndef STORE_H
#define STORE_H

/* 本机状态：收藏 / 最近查看 / 向导进度 / 上次板块。
   （主题在 theme.cpp 里，也落在同一个 ini：_stm32lookup.ini）
   键名与网页版 localstorage 的语义对齐：收藏与最近按"条目名"存，
   进度按"条目名#步骤序号"存 —— 用名字而不是下标，重建数据后不会错位。 */

#include <QSet>
#include <QString>
#include <QStringList>

class Store
{
public:
    Store();

    /* ---- 收藏 ---- */
    QStringList favorites() const;
    bool isFavorite(const QString &name) const;
    bool toggleFavorite(const QString &name);   /* 返回切换后是否已收藏 */
    void clearFavorites();

    /* ---- 最近查看（最多 30 条，最新在前） ---- */
    QStringList recent() const;
    void touchRecent(const QString &name);
    void clearRecent();

    /* ---- 向导 / 手册的步骤进度 ---- */
    QSet<QString> doneSteps() const;
    bool isStepDone(const QString &card, int step) const;
    bool toggleStep(const QString &card, int step);
    void clearSteps(const QString &card);

    /* ---- 界面状态 ---- */
    int  lastSection() const;
    void setLastSection(int s);

private:
    QString key(const QString &k) const;
    QStringList list(const QString &k) const;
    void setList(const QString &k, const QStringList &v);
};

#endif // STORE_H
