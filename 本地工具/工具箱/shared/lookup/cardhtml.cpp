#include "cardhtml.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QHash>

namespace {

/* 深/浅色两套局部配色，跟 Theme 的调子一致 */
struct Col { QString key, dim, line, codeBg, codeFg, accent, chipBg; };
Col colors(bool dark)
{
    Col c;
    if (dark) {
        c.key    = QStringLiteral("#dbe7f3");
        c.dim    = QStringLiteral("#8b93a3");
        c.line   = QStringLiteral("#262c37");
        c.codeBg = QStringLiteral("#12161d");
        c.codeFg = QStringLiteral("#cfe3ff");
        c.accent = QStringLiteral("#22d3ee");
        c.chipBg = QStringLiteral("#1d222b");
    } else {
        c.key    = QStringLiteral("#1a1f2b");
        c.dim    = QStringLiteral("#5b667a");
        c.line   = QStringLiteral("#d5dbe5");
        c.codeBg = QStringLiteral("#f2f5fa");
        c.codeFg = QStringLiteral("#12304a");
        c.accent = QStringLiteral("#0891b2");
        c.chipBg = QStringLiteral("#eef1f6");
    }
    return c;
}

QString badge(const QString &text, const QString &bg, const QString &fg)
{
    return QStringLiteral("<span style='background:%1;color:%2;padding:1px 7px;"
                          "font-size:11px;'>%3</span>")
        .arg(bg, fg, text);
}

QString code(const QString &text, const Col &c)
{
    if (text.trimmed().isEmpty())
        return QString();
    return QStringLiteral("<pre style='background:%1;color:%2;padding:8px;'>%3</pre>")
        .arg(c.codeBg, c.codeFg, CardHtml::esc(text));
}

/* 两列表格：参数表 / 查找表 */
QString table2(const QVector<QVector<QString>> &rows, const QStringList &head, const Col &c)
{
    if (rows.isEmpty())
        return QString();
    QString h = QStringLiteral("<table width='100%' cellspacing='0' cellpadding='4' border='0'>");
    if (!head.isEmpty()) {
        h += QStringLiteral("<tr>");
        for (const QString &s : head)
            h += QStringLiteral("<td style='color:%1;font-weight:bold;border-bottom:1px solid %2;'>%3</td>")
                     .arg(c.dim, c.line, CardHtml::esc(s));
        h += QStringLiteral("</tr>");
    }
    for (const QVector<QString> &row : rows) {
        h += QStringLiteral("<tr>");
        for (int i = 0; i < row.size(); ++i) {
            const QString color = (i == 0) ? c.key : c.dim;
            h += QStringLiteral("<td style='color:%1;vertical-align:top;border-bottom:1px solid %2;'>%3</td>")
                     .arg(color, c.line, CardHtml::esc(row.at(i)));
        }
        h += QStringLiteral("</tr>");
    }
    h += QStringLiteral("</table>");
    return h;
}

QString h2(const QString &text, const Col &c)
{
    return QStringLiteral("<p style='color:%1;font-weight:bold;margin:12px 0 4px 0;'>%2</p>")
        .arg(c.accent, CardHtml::esc(text));
}

/* 「复制代码」：不把长文本塞进 href，只带条目下标，由窗口回调去剪贴板 */
QString copyLink(const Col &c, const CardHtml::Ctx &ctx)
{
    if (ctx.index < 0)
        return QString();
    return QStringLiteral("<p style='margin:0 0 2px 0;'><a href='copy:%1' "
                          "style='color:%2;text-decoration:none;'>📋 复制代码</a></p>")
        .arg(ctx.index).arg(c.accent);
}

/* guide / 手册的步骤：7 列（标题 / 为什么 / 做什么 / 代码 / 怎么验证 / 常见问题 / 相关）
   每步前面是可点的勾选框，进度存本机（锚点 step:<下标>:<步号>）。 */
QString guideSteps(const QVector<QVector<QString>> &steps, const Col &c,
                   const QString &cardName, const CardHtml::Ctx &ctx)
{
    if (steps.isEmpty())
        return QString();
    QString h;
    int done = 0;
    for (int i = 0; i < steps.size(); ++i)
        if (ctx.done.contains(QStringLiteral("%1#%2").arg(cardName).arg(i)))
            ++done;
    if (ctx.index >= 0)
        h += QStringLiteral("<p style='color:%1;margin:0 0 6px 0;'>进度 %2 / %3</p>")
                 .arg(c.dim).arg(done).arg(steps.size());

    int n = 0;
    for (const QVector<QString> &s : steps) {
        const int idx = n;
        ++n;
        const bool isDone = ctx.done.contains(QStringLiteral("%1#%2").arg(cardName).arg(idx));
        const auto at = [&s](int i) { return (i < s.size()) ? s.at(i) : QString(); };
        QString box;
        if (ctx.index >= 0)
            box = QStringLiteral("<a href='step:%1:%2' style='color:%3;text-decoration:none;'>%4</a> ")
                      .arg(ctx.index).arg(idx).arg(c.accent,
                          isDone ? QString::fromUtf8("☑") : QString::fromUtf8("☐"));
        h += QStringLiteral("<p style='color:%1;font-weight:bold;margin:10px 0 2px 0;'>"
                            "%2%3. %4</p>").arg(c.key, box).arg(n).arg(CardHtml::esc(at(0)));
        const struct { const char *label; int col; } parts[] = {
            { "为什么", 1 }, { "做什么", 2 }, { "怎么验证", 4 }, { "常见问题", 5 }, { "相关", 6 },
        };
        for (const auto &p : parts) {
            const QString v = at(p.col);
            if (v.trimmed().isEmpty())
                continue;
            h += QStringLiteral("<p style='color:%1;margin:2px 0;'><b>%2：</b>%3</p>")
                     .arg(c.dim, QString::fromUtf8(p.label), CardHtml::esc(v));
        }
        if (!at(3).trimmed().isEmpty()) {
            h += QStringLiteral("<p style='color:%1;margin:4px 0 0 0;'>代码</p>").arg(c.dim);
            h += code(at(3), c);
        }
    }
    return h;
}

/* flow 流程卡步骤：[步骤, 做法] */
QString flowSteps(const QVector<QVector<QString>> &steps, const Col &c)
{
    if (steps.isEmpty())
        return QString();
    QStringList rows;
    int n = 0;
    for (const QVector<QString> &s : steps) {
        ++n;
        rows << QStringLiteral("<tr><td style='color:%1;vertical-align:top;width:96px;"
                               "border-bottom:1px solid %2;'>%3</td>"
                               "<td style='color:%4;border-bottom:1px solid %2;'>%5</td></tr>")
                    .arg(c.key, c.line).arg(n)
                    .arg(c.dim, CardHtml::esc(s.size() > 1 ? s.at(1) : QString()));
        rows << QStringLiteral("<tr><td style='color:%1;border-bottom:1px solid %2;'>%3</td>"
                               "<td></td></tr>").arg(c.dim, c.line, CardHtml::esc(s.value(0)));
    }
    return QStringLiteral("<table width='100%' cellspacing='0' cellpadding='4'>%1</table>")
        .arg(rows.join(QString()));
}

} // namespace

QString CardHtml::esc(const QString &s)
{
    QString t = s;
    t.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    t.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    t.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    t.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return t;
}

QString CardHtml::copyText(const Card &c)
{
    QStringList parts;
    if (!c.e.trimmed().isEmpty())
        parts << c.e;
    for (const QVector<QString> &s : c.f)
        if (s.size() > 3 && !s.at(3).trimmed().isEmpty())
            parts << s.at(3);
    return parts.join(QStringLiteral("\n\n"));
}

QString CardHtml::build(const Card &c, bool dark, const Ctx &ctx)
{
    const Col col = colors(dark);
    QString h;

    /* ---- 收藏 / 标题行 ---- */
    if (ctx.index >= 0)
        h += QStringLiteral("<p style='margin:0 0 4px 0;'><a href='fav:%1' "
                            "style='color:%2;text-decoration:none;'>%3</a></p>")
                 .arg(ctx.index).arg(col.accent,
                     ctx.isFav ? QString::fromUtf8("★ 已收藏（点一下取消）")
                               : QString::fromUtf8("☆ 收藏（Ctrl+D）"));
    h += QStringLiteral("<p style='font-size:17px;font-weight:bold;color:%1;margin:0 0 6px 0;'>%2</p>")
             .arg(col.key, esc(c.n));
    QStringList chips;
    if (!c.m.isEmpty())  chips << badge(esc(c.m), col.chipBg, col.dim);
    if (c.isManu())      chips << badge(esc(QStringLiteral("手册·") + c.b), col.chipBg, col.accent);
    if (c.isFlow())      chips << badge(QStringLiteral("流程"), col.chipBg, col.accent);
    else if (c.isGuide())chips << badge(QStringLiteral("向导"), col.chipBg, col.accent);
    else if (c.isTable())chips << badge(QStringLiteral("查找表"), col.chipBg, col.accent);
    else if (c.isStruct())chips << badge(QStringLiteral("结构体"), col.chipBg, col.accent);
    else if (c.isDs())   chips << badge(QStringLiteral("数据手册"), col.chipBg, col.accent);
    else if (c.isChip()) chips << badge(QStringLiteral("芯片对照"), col.chipBg, col.accent);
    else                 chips << badge(QStringLiteral("函数"), col.chipBg, col.accent);
    h += QStringLiteral("<p style='margin:0 0 10px 0;'>%1</p>").arg(chips.join(QStringLiteral(" ")));

    /* ---- 用途 ---- */
    if (!c.d.trimmed().isEmpty())
        h += QStringLiteral("<p style='color:%1;line-height:1.7;margin:0 0 8px 0;'>%2</p>")
                 .arg(col.key, esc(c.d));

    /* ---- 引导卡的目标 ---- */
    if (!c.g.trimmed().isEmpty()) {
        h += h2(QStringLiteral("最终形态"), col);
        h += QStringLiteral("<p style='color:%1;line-height:1.7;'>%2</p>").arg(col.dim, esc(c.g));
    }

    /* ---- 原型 ---- */
    if (!c.p.trimmed().isEmpty()) {
        h += h2(QStringLiteral("原型"), col);
        h += code(c.p, col);
    }

    /* ---- 参数表 / 查找表 ---- */
    if (c.isTable()) {
        if (!c.a.isEmpty()) {
            h += h2(QStringLiteral("内容"), col);
            h += table2(c.a, c.h, col);
        }
    } else if (!c.a.isEmpty()) {
        h += h2(QStringLiteral("参数"), col);
        h += table2(c.a, QStringList { QStringLiteral("参数"), QStringLiteral("可选值"),
                                       QStringLiteral("说明") }, col);
    }

    /* ---- 步骤 ---- */
    if (!c.f.isEmpty()) {
        h += h2(c.isFlow() ? QStringLiteral("流程步骤")
                           : QStringLiteral("动作清单"), col);
        if (!c.isFlow() && !CardHtml::copyText(c).isEmpty())
            h += copyLink(col, ctx);
        h += c.isFlow() ? flowSteps(c.f, col) : guideSteps(c.f, col, c.n, ctx);
    }

    /* ---- 返回值 ---- */
    if (!c.r.trimmed().isEmpty()) {
        h += h2(QStringLiteral("返回值"), col);
        h += QStringLiteral("<p style='color:%1;'>%2</p>").arg(col.dim, esc(c.r));
    }

    /* ---- 示例 ---- */
    if (!c.e.trimmed().isEmpty()) {
        h += h2(QStringLiteral("示例"), col);
        h += copyLink(col, ctx);
        h += code(c.e, col);
    }

    /* ---- 补充说明 ---- */
    if (!c.s.trimmed().isEmpty()) {
        h += h2(QStringLiteral("补充"), col);
        h += QStringLiteral("<p style='color:%1;'>%2</p>").arg(col.dim, esc(c.s));
    }

    /* ---- 本项目使用位置 ---- */
    if (c.uReal) {
        /* D2：来自本仓库**真实代码索引**（code_usage.json）。
           有位置 → 每行是个可点链接，跳到那块板的那一行（href 只带下标，点击归 onAnchor）；
           没有 → 如实写一句"没在你的代码里出现"，别让人误以为手册条目就是"你用了"。 */
        h += h2(QStringLiteral("本项目使用位置（按你的代码索引）"), col);
        if (c.u.isEmpty()) {
            h += QStringLiteral("<p style='color:%1;'>%2</p>")
                     .arg(col.dim, esc(QStringLiteral(
                         "没在你的代码里出现 —— 这条只有手册条目，你的工程里没用它")));
        } else {
            /* ① 分组：`板1`/`板2`（你自己写的 .c）排在 `板1·库`/`板2·库`（驱动源码
               FWLIB/src，不是你写的）之前 —— 混在一起看不出"这句到底是谁用的"。
               ② 每组**封顶**：有的 SPL 条目在库里出现上百次，全列出来这一页没法看；
               每组建最多 kPerTag 行，剩下的写「另有 N 处」。 */
            const int kPerTag = 5;
            QHash<QString, QVector<Card::Use>> byTag;
            QStringList order;                       /* 按首次出现定序，之后按分组重排 */
            for (const Card::Use &u : c.u) {
                if (!byTag.contains(u.tag))
                    order << u.tag;
                byTag[u.tag] << u;
            }
            QStringList mine, lib;
            for (const QString &t : order)
                (t.contains(QChar(0x00B7)) ? lib : mine) << t;      /* '·' = 库那组 */
            h += QStringLiteral("<p style='color:%1;font-size:12px;margin:0 0 4px 0;'>%2</p>")
                     .arg(col.dim, esc(QStringLiteral(
                         "板1/板2 = 你自己写的 .c；板1·库/板2·库 = 工程里的驱动源码(FWLIB/src)。"))
                     );
            QStringList rows;
            int more = 0;
            for (const QString &t : mine + lib) {
                const QVector<Card::Use> us = byTag.value(t);
                const bool isMine = !t.contains(QChar(0x00B7));
                const QString tagColor = isMine ? QStringLiteral("#34d399") : col.dim;
                for (int k = 0; k < us.size(); ++k) {
                    if (k >= kPerTag) {
                        more += us.size() - kPerTag;
                        break;
                    }
                    const Card::Use &u = us.at(k);
                    const QString shown = QStringLiteral("%1 %2:%3").arg(u.tag, u.file).arg(u.line);
                    QString cell = esc(shown);
                    QString abs;
                    if (ctx.boardDirs && ctx.srcTargets)
                        abs = ctx.boardDirs->value(t);              /* 板1 / 板1·库 → 目录 */
                    if (!abs.isEmpty())
                        abs = QDir(abs).filePath(u.file);           /* basename 拼成绝对路径 */
                    /* 只在文件**真的在**时才做成链接：拼不出来或文件不见的，
                       做成死的可点链接还不如老实显示文本（点了只会报"找不到源文件"）。 */
                    if (ctx.srcTargets && QFileInfo::exists(abs)) {
                        ctx.srcTargets->append(QStringLiteral("%1|%2").arg(abs).arg(u.line));
                        const int i = ctx.srcTargets->size() - 1;
                        cell = QStringLiteral("<a href='src:%1' style='color:%2;'>%3</a>")
                                   .arg(i).arg(col.accent, esc(shown));
                    }
                    rows << QStringLiteral("<tr><td style='color:%1;'>%2</td>"
                                           "<td style='color:%3;'>%4</td></tr>")
                                .arg(tagColor, cell).arg(col.dim, esc(u.what));
                }
            }
            h += QStringLiteral("<table width='100%' cellspacing='0' cellpadding='3'>%1</table>")
                     .arg(rows.join(QString()));
            if (more > 0)
                h += QStringLiteral("<p style='color:%1;font-size:12px;'>%2</p>")
                         .arg(col.dim, esc(QStringLiteral("另有 %1 处没列出来（同一处会重复出现，"
                                                          "只在工程里查代码时用得上）").arg(more)));
        }
    } else if (!c.u.isEmpty()) {
        /* 没有代码索引文件时的老渲染：沿用 dataset.json 里网页版自带的 u。 */
        h += h2(QStringLiteral("本项目使用位置"), col);
        QStringList rows;
        for (const Card::Use &u : c.u) {
            const bool mine = u.tag.startsWith(QStringLiteral("S"));
            const QString tagColor = mine ? QStringLiteral("#34d399") : col.dim;
            rows << QStringLiteral("<tr><td style='color:%1;'>%2</td><td style='color:%3;'>%4:%5</td>"
                                   "<td style='color:%6;'>%7</td></tr>")
                        .arg(tagColor, esc(u.tag), col.key)
                        .arg(esc(u.file)).arg(u.line)
                        .arg(col.dim, esc(u.what));
        }
        h += QStringLiteral("<table width='100%' cellspacing='0' cellpadding='3'>%1</table>")
                 .arg(rows.join(QString()));
    }

    /* ---- 相关与联动 ---- */
    if (!c.rel.isEmpty()) {
        h += h2(QStringLiteral("相关与联动"), col);
        QStringList links;
        for (const QString &r : c.rel)
            links << QStringLiteral("<a href='card:%1' style='color:%2;'>%3</a>")
                         .arg(esc(r), col.accent, esc(r));
        h += QStringLiteral("<p>%1</p>").arg(links.join(QStringLiteral("　")));
    }

    return h;
}
