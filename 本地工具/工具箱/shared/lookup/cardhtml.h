#ifndef CARDHTML_H
#define CARDHTML_H

/* 详情渲染：把一条 Card 拼成 QTextBrowser 能显示的 HTML。
 *
 * Qt 富文本只支持 CSS 2.1 子集：不支持 flex / grid / border-radius / box-shadow，
 * 所以网页里靠 flex 的排版在这里一律摊平成块级元素，参数表用 <table>。
 * 代码块用 <pre>，等宽字体由 MainWindow 的 setDefaultStyleSheet 统一给。 */

#include "dataset.h"

#include <QSet>
#include <QString>

namespace CardHtml {

/* 渲染上下文：条目下标（做锚点用）、是否已收藏、已完成步骤集合 */
struct Ctx {
    int          index = -1;
    bool         isFav = false;
    QSet<QString> done;     /* 形如 "条目名#步骤序号" */
};

/* dark/light 只影响局部配色（页面底色由 QSS 给） */
QString build(const Card &c, bool dark, const Ctx &ctx = Ctx());

/* 供「复制代码」用的纯文本（示例代码 + 引导步骤里的代码） */
QString copyText(const Card &c);

/* 搜索高亮用的转义（MainWindow 命中标黄时复用） */
QString esc(const QString &s);

} // namespace CardHtml

#endif // CARDHTML_H
