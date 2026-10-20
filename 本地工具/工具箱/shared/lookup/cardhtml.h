#ifndef CARDHTML_H
#define CARDHTML_H

/* 详情渲染：把一条 Card 拼成 QTextBrowser 能显示的 HTML。
 *
 * Qt 富文本只支持 CSS 2.1 子集：不支持 flex / grid / border-radius / box-shadow，
 * 所以网页里靠 flex 的排版在这里一律摊平成块级元素，参数表用 <table>。
 * 代码块用 <pre>，等宽字体由 MainWindow 的 setDefaultStyleSheet 统一给。 */

#include "dataset.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

namespace CardHtml {

/* 渲染上下文：条目下标（做锚点用）、是否已收藏、已完成步骤集合 */
struct Ctx {
    int          index = -1;
    bool         isFav = false;
    QSet<QString> done;     /* 形如 "条目名#步骤序号" */

    /* D2：真实使用位置（uReal==true）渲染时，每行是个可点链接。
       这里收集链接目标（元素 = "绝对路径|行号"），href 里只带下标（纯 ASCII 最稳）；
       点击由 MainWindow::onAnchor 的 `src:` 分支处理。

       boardDirs 是"板标签 → 该板工程目录绝对路径"（板1/板2），链接目标要靠它把
       索引里的文件名（basename）拼成绝对路径。不传就退化成不可点（仅展示）。 */
    QStringList               *srcTargets = nullptr;
    const QHash<QString, QString> *boardDirs = nullptr;
};

/* dark/light 只影响局部配色（页面底色由 QSS 给） */
QString build(const Card &c, bool dark, const Ctx &ctx = Ctx());

/* 供「复制代码」用的纯文本（示例代码 + 引导步骤里的代码） */
QString copyText(const Card &c);

/* 搜索高亮用的转义（MainWindow 命中标黄时复用） */
QString esc(const QString &s);

} // namespace CardHtml

#endif // CARDHTML_H
