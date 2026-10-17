#include "dashboardcanvas.h"

#include "theme.h"

#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPolygon>
#include <QSet>
#include <QSizePolicy>
#include <QToolTip>
#include <QVariantList>

#include <climits>

namespace {
const int kMargin = 12;
const int kGap = 6;
}

DashboardCanvas::DashboardCanvas(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    /* 两板热力图并排要 2*224 + 56 + 左右边距 32 ≈ 536，留点余量 */
    setMinimumWidth(460);
    setMouseTracking(true);   /* 悬停提示需要，否则只在按住键时才有 move 事件 */
}

void DashboardCanvas::setTools(const QVariantMap &tools)
{
    m_tools = tools;
    update();
}

void DashboardCanvas::setNotice(const QString &text)
{
    m_notice = text;
    update();
}

void DashboardCanvas::setFailed(const QStringList &keys)
{
    m_failedKeys = keys;
    update();
}

void DashboardCanvas::setDelta(const QStringList &lines)
{
    m_delta = lines;
    update();
}

/* A2：只挂悬停提示、既不跳页也不查引脚的那种命中区（趋势折线数据点）。 */
int DashboardCanvas::tipHitCount() const
{
    int n = 0;
    for (const Hit &h : m_hits)
        if (h.key.isEmpty() && h.pin.isEmpty())
            ++n;
    return n;
}

/* 按当前主题给一整套配色。深色下用亮色文字 + 暗卡片，浅色反之。 */
DashboardCanvas::Palette DashboardCanvas::makePalette() const
{
    const bool dark = (Theme::mode() == Theme::Dark);
    Palette p;
    if (dark) {
        p.card = QColor("#26262c");
        p.border = QColor("#3a3a44");
        p.textPri = QColor("#e8e8ee");
        p.textSec = QColor("#9a9aa8");
        p.textTer = QColor("#6c6c78");
        p.ok = QColor("#4ec970");
        p.warn = QColor("#e3b341");
        p.err = QColor("#f85149");
        p.info = QColor("#5aa9ff");
        p.freeCell = QColor("#33333c");
        p.usedCell = QColor("#3d7eff");
        p.conflictCell = QColor("#f85149");
        p.declCell = QColor("#a371f7");
        p.track = QColor("#33333c");
    } else {
        p.card = QColor("#f4f4f7");
        p.border = QColor("#dcdce4");
        p.textPri = QColor("#1c1c22");
        p.textSec = QColor("#5c5c68");
        p.textTer = QColor("#8a8a96");
        p.ok = QColor("#1a7f37");
        p.warn = QColor("#9a6700");
        p.err = QColor("#cf222e");
        p.info = QColor("#2563eb");
        p.freeCell = QColor("#e6e6ec");
        p.usedCell = QColor("#4a8eff");
        p.conflictCell = QColor("#cf222e");
        p.declCell = QColor("#8250df");
        p.track = QColor("#e6e6ec");
    }
    return p;
}

void DashboardCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const Palette pal = makePalette();
    m_hits.clear();   /* 命中区随绘制重建 */

    if (m_tools.isEmpty()) {
        QFont f = p.font();
        f.setPointSize(11);
        p.setFont(f);
        p.setPen(pal.textSec);
        p.drawText(rect(), Qt::AlignCenter,
                   m_notice.isEmpty() ? QStringLiteral("尚无数据，点「刷新」取数。") : m_notice);
        return;
    }

    const int w = qMax(320, width() - kMargin * 2);
    int y = kMargin;

    y = drawHealth(p, kMargin, y, w, pal);
    y = drawDelta(p, kMargin, y, w, pal);
    y = drawPins(p, kMargin, y, w, pal);
    y = drawMemory(p, kMargin, y, w, pal);
    y = drawTasks(p, kMargin, y, w, pal);
    y = drawTrend(p, kMargin, y, w, pal);

    const int h = y + kMargin;
    if (m_contentHeight != h) {
        m_contentHeight = h;
        setMinimumHeight(h);
    }
}

int DashboardCanvas::sectionTitle(QPainter &p, int x, int y, int w,
                                  const QString &text, const Palette &pal)
{
    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(9);
    p.setFont(f);
    p.setPen(pal.textPri);
    p.drawText(QRect(x, y, w, 18), Qt::AlignLeft | Qt::AlignVCenter, text);
    f.setBold(false);
    p.setFont(f);
    return y + 24;
}

int DashboardCanvas::bar(QPainter &p, int x, int y, int w, const QString &label,
                         double used, double total, const Palette &pal, const QColor &fill)
{
    const int labelW = 46;
    const int barH = 10;
    const int barW = qMax(60, w - labelW - 160);

    QFont f = p.font();
    f.setPointSize(9);
    p.setFont(f);

    p.setPen(pal.textSec);
    p.drawText(QRect(x, y, labelW, barH + 4), Qt::AlignLeft | Qt::AlignVCenter, label);

    const QRect track(x + labelW, y + 2, barW, barH);
    p.setPen(Qt::NoPen);
    p.setBrush(pal.track);
    p.drawRoundedRect(track, 3, 3);

    double ratio = total > 0 ? used / total : 0.0;
    if (ratio < 0) ratio = 0;
    double shown = ratio > 1.0 ? 1.0 : ratio;
    QColor c = fill;
    if (ratio > 1.0) c = pal.err;
    else if (ratio > 0.8) c = pal.warn;
    p.setBrush(c);
    p.drawRoundedRect(QRect(track.x(), track.y(), int(track.width() * shown), track.height()), 3, 3);

    p.setPen(pal.textSec);
    const QString txt = QStringLiteral("%1 / %2 KB（%3%）")
                            .arg(used / 1024.0, 0, 'f', 1)
                            .arg(total / 1024.0, 0, 'f', 0)
                            .arg(ratio * 100.0, 0, 'f', 1);
    p.drawText(QRect(x + labelW + barW + 8, y, 150, barH + 4),
               Qt::AlignLeft | Qt::AlignVCenter, txt);

    return y + barH + 8;
}

int DashboardCanvas::drawHealth(QPainter &p, int x, int y, int w, const Palette &pal)
{
    struct Item { QString name; QString detail; QColor col; QString thr; };
    QVector<Item> items;
    /* 与下面 append 顺序一一对应，点卡片按此 key 跳工具页 */
    static const char *const kCardKeys[] = {"pin", "rtos", "enc", "lora", "protocol",
                                            "cache", "progress"};

    /* A2 阈值红黄线：数量类指标 1~4 处算「注意」（黄），≥5 处算「严重」（红）。
       阈值取自清单示例「引脚冲突 ≥ 5 处转红」，其余同口径 —— 免得每次自己盯数字。
       详情文字里的数字照旧保留，颜色只负责"要不要现在就去看"。 */
    auto level = [&pal](int n) -> QColor {
        if (n <= 0)
            return pal.ok;
        return n >= 5 ? pal.err : pal.warn;
    };

    {
        const QVariantMap m = m_tools.value(QStringLiteral("pin")).toMap();
        int conflicts = 0, dbg = 0;
        for (const QVariant &b : m.value(QStringLiteral("boards")).toList()) {
            const QVariantMap bm = b.toMap();
            conflicts += bm.value(QStringLiteral("conflicts")).toMap().size();
            dbg += bm.value(QStringLiteral("debug_used")).toList().size();
        }
        /* B1：调试口被业务宏占了算严重，卡片直接转红（报告里有专节讲怎么办）。
           字要短：8 张卡一行，长文本会被裁掉，明细看悬停提示。 */
        QString detail = conflicts ? QStringLiteral("%1 冲突").arg(conflicts)
                                   : QStringLiteral("无冲突");
        if (dbg)
            detail += QStringLiteral(" · 调试口%1").arg(dbg);
        items.append({QStringLiteral("引脚规划"), detail,
                      (dbg || conflicts >= 5) ? pal.err : (conflicts ? pal.warn : pal.ok),
                      QStringLiteral("1~4 处黄 · ≥5 处红 · 调试口被占即红")});
    }
    {
        const QVariantMap m = m_tools.value(QStringLiteral("rtos")).toMap();
        int severe = 0, warn = 0;
        for (const QVariant &b : m.value(QStringLiteral("boards")).toList()) {
            severe += b.toMap().value(QStringLiteral("severe")).toInt();
            warn += b.toMap().value(QStringLiteral("warn")).toInt();
        }
        items.append({QStringLiteral("RTOS 检查"),
                      severe ? QStringLiteral("%1 严重").arg(severe)
                             : (warn ? QStringLiteral("%1 注意").arg(warn) : QStringLiteral("正常")),
                      severe ? pal.err : (warn ? pal.warn : pal.ok),
                      QStringLiteral("有严重即红 · 仅注意则黄")});
    }
    {
        const QVariantMap m = m_tools.value(QStringLiteral("enc")).toMap();
        const int v = m.value(QStringLiteral("violations")).toInt();
        items.append({QStringLiteral("编码守卫"),
                      v ? QStringLiteral("%1 违规").arg(v) : QStringLiteral("无违规"),
                      level(v), QStringLiteral("1~4 处黄 · ≥5 处红")});
    }
    {
        const QVariantMap m = m_tools.value(QStringLiteral("lora")).toMap();
        int n = m.value(QStringLiteral("cross")).toList().size();
        for (const QVariant &b : m.value(QStringLiteral("boards")).toList())
            n += b.toMap().value(QStringLiteral("issues")).toList().size();
        items.append({QStringLiteral("LoRa 检查"),
                      n ? QStringLiteral("%1 问题").arg(n) : QStringLiteral("正常"),
                      level(n), QStringLiteral("1~2 处黄 · ≥3 处红")});
    }
    {
        const QVariantMap m = m_tools.value(QStringLiteral("protocol")).toMap();
        int n = 0;
        for (const QVariant &b : m.value(QStringLiteral("boards")).toList())
            n += b.toMap().value(QStringLiteral("issues")).toInt();
        const QList<QVariant> ff = m.value(QStringLiteral("findings")).toList();
        n += ff.size();
        items.append({QStringLiteral("协议校验"),
                      n ? QStringLiteral("%1 问题").arg(n) : QStringLiteral("通过"),
                      level(n), QStringLiteral("1~2 处黄 · ≥3 处红")});
    }
    {
        const QVariantMap m = m_tools.value(QStringLiteral("cache")).toMap();
        const int sv = m.value(QStringLiteral("severe")).toInt();
        const double holdSec = m.value(QStringLiteral("hold_seconds")).toDouble();
        const QString hold = m.value(QStringLiteral("hold_text")).toString();
        /* A1：除了"补传慢于写入 / 越界 / 记录塞不下"（severe）算红，再按"断网可扛时长"分级：
           < 10 分钟红、< 30 分钟黄。原来只显示 hold_text 一个绿字，"只能扛 3 分钟"也看不出来。
           注意 hold_seconds 为 0 表示宏没算出来 —— 那属于"未核算"，不替它染红（报告里另有严重项）。 */
        QColor col = pal.ok;
        if (sv || (holdSec > 0 && holdSec < 600))
            col = pal.err;
        else if (holdSec > 0 && holdSec < 1800)
            col = pal.warn;
        items.append({QStringLiteral("断网缓存"),
                      hold.isEmpty() ? QStringLiteral("未核算")
                                     : QStringLiteral("可扛 %1").arg(hold),
                      col,
                      QStringLiteral("补传慢于写入即红 · 可扛 <10 分钟红 · <30 分钟黄")});
    }
    {
        const QVariantMap m = m_tools.value(QStringLiteral("progress")).toMap();
        items.append({QStringLiteral("进度扫描"),
                      QStringLiteral("%1 待办 · %2 桩")
                          .arg(m.value(QStringLiteral("markers")).toInt())
                          .arg(m.value(QStringLiteral("stubs")).toInt()),
                      pal.info, QString()});
    }

    /* 本轮没取到数的工具：不能画成绿色"正常"——那是替失败的工具报平安。
       统一改成"未取到" + 注意色，明细见状态栏。 */
    for (int i = 0;
         i < items.size() && i < int(sizeof(kCardKeys) / sizeof(kCardKeys[0])); ++i) {
        if (m_failedKeys.contains(QString::fromLatin1(kCardKeys[i]))) {
            items[i].detail = QStringLiteral("未取到");
            items[i].col = pal.warn;
        }
    }

    const int n = items.size();
    if (n == 0)
        return y;
    const int cw = (w - kGap * (n - 1)) / n;
    const int ch = 50;

    for (int i = 0; i < n; ++i) {
        const int cx = x + i * (cw + kGap);
        p.setPen(QPen(pal.border, 1));
        p.setBrush(pal.card);
        p.drawRoundedRect(QRect(cx, y, cw, ch), 8, 8);

        p.setPen(Qt::NoPen);
        p.setBrush(items[i].col);
        p.drawEllipse(QPoint(cx + 14, y + 17), 4, 4);

        QFont f = p.font();
        f.setPointSize(9);
        f.setBold(true);
        p.setFont(f);
        p.setPen(pal.textPri);
        p.drawText(QRect(cx + 24, y + 8, cw - 30, 18), Qt::AlignLeft | Qt::AlignVCenter, items[i].name);

        f.setBold(false);
        p.setFont(f);
        p.setPen(items[i].col);
        p.drawText(QRect(cx + 12, y + 30, cw - 20, 22), Qt::AlignLeft | Qt::AlignVCenter, items[i].detail);

        Hit hit;
        hit.rect = QRect(cx, y, cw, ch);
        hit.key = (i < int(sizeof(kCardKeys) / sizeof(kCardKeys[0])))
                      ? QString::fromLatin1(kCardKeys[i]) : QString();
        hit.tip = QStringLiteral("%1：%2").arg(items[i].name, items[i].detail);
        if (!items[i].thr.isEmpty())
            hit.tip += QStringLiteral("\n阈值：%1").arg(items[i].thr);   /* A2：把红黄线写进提示，可查 */
        hit.tip += QStringLiteral("\n点击：看报告里这一节（想重跑 → 侧栏「检查」组）");
        m_hits.append(hit);
    }
    return y + ch + 14;
}

/* 「较上次」一行：把各指标的增减并排写出来，一眼看出这轮是变好还是变差。 */
int DashboardCanvas::drawDelta(QPainter &p, int x, int y, int w, const Palette &pal)
{
    if (m_delta.isEmpty())
        return y;
    QFont f = p.font();
    f.setPointSize(9);
    p.setFont(f);
    p.setPen(pal.textSec);
    const QString text = QStringLiteral("较上次：") + m_delta.join(QStringLiteral("　"));
    p.drawText(QRect(x, y, w, 18), Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(text, Qt::ElideRight, w));
    return y + 18;
}

int DashboardCanvas::drawPins(QPainter &p, int x, int y, int w, const Palette &pal)
{
    const QVariantMap m = m_tools.value(QStringLiteral("pin")).toMap();
    const QVariantList boards = m.value(QStringLiteral("boards")).toList();
    if (boards.isEmpty())
        return y;

    y = sectionTitle(p, x, y, w,
                     m_pinZoom ? QStringLiteral("引脚占用热力图（大格 · 双击还原）")
                               : QStringLiteral("引脚占用热力图（双击放大 · 单击看占用者）"), pal);

    if (!m_pinDetail.isEmpty()) {
        QFont df = p.font();
        df.setPointSize(9);
        p.setFont(df);
        p.setPen(pal.info);
        p.drawText(QRect(x, y, w, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(m_pinDetail, Qt::ElideRight, w));
        y += 20;
    }

    const int cell = m_pinZoom ? 15 : 10;
    const int gap  = m_pinZoom ? 2 : 1;
    const int labW = m_pinZoom ? 17 : 14;
    const int topH = m_pinZoom ? 14 : 12;
    const int cols = 16, rows = 7;
    const int gridW = labW + cols * (cell + gap);
    const int colGap = 56;
    const QString ports = QStringLiteral("ABCDEFG");

    QFont f = p.font();
    p.setFont(f);

    int maxBottom = y;
    for (int bi = 0; bi < boards.size() && bi < 2; ++bi) {
        const QVariantMap b = boards[bi].toMap();
        const int bx = x + bi * (gridW + colGap);
        int by = y;

        f.setPointSize(9);
        f.setBold(true);
        p.setFont(f);
        p.setPen(pal.textPri);
        p.drawText(QRect(bx, by, gridW, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   b.value(QStringLiteral("title")).toString());
        f.setBold(false);
        f.setPointSize(8);
        p.setFont(f);
        by += 20;

        const QVariantMap occ = b.value(QStringLiteral("occupied")).toMap();
        const QVariantMap con = b.value(QStringLiteral("conflicts")).toMap();
        /* A3：库里头文件声明了 _PORT/_PIN、但全工程没有任何代码引用的脚。
           单列一色，配合 D3 的候选脚建议 —— 这些脚才是"回收代价最低"的。 */
        QSet<QString> declUnused;
        for (const QVariant &v : b.value(QStringLiteral("decl_unused")).toList())
            declUnused.insert(v.toString());

        p.setPen(pal.textTer);
        for (int c = 0; c < cols; ++c) {
            p.drawText(QRect(bx + labW + c * (cell + gap), by, cell, topH),
                       Qt::AlignCenter, QString::number(c, 16).toUpper());
        }
        by += topH;

        for (int r = 0; r < rows; ++r) {
            p.setPen(pal.textTer);
            p.drawText(QRect(bx, by + r * (cell + gap), labW - 2, cell),
                       Qt::AlignRight | Qt::AlignVCenter, QString(ports.at(r)));
            for (int c = 0; c < cols; ++c) {
                const QString name = QStringLiteral("P%1%2").arg(ports.at(r))
                                         .arg(QString::number(c, 16).toUpper());
                QColor col = pal.freeCell;
                QStringList owners;
                bool declOnly = false;   /* 已声明但没人用（不是冲突） */
                if (con.contains(name)) {
                    col = pal.conflictCell;
                    for (const QVariant &o : con.value(name).toList())
                        owners << o.toString();
                } else if (declUnused.contains(name)) {
                    col = pal.declCell;
                    declOnly = true;
                    for (const QVariant &o : occ.value(name).toList())
                        owners << o.toString();
                } else if (occ.contains(name)) {
                    col = pal.usedCell;
                    for (const QVariant &o : occ.value(name).toList())
                        owners << o.toString();
                }
                const QRect cr(bx + labW + c * (cell + gap), by + r * (cell + gap), cell, cell);
                p.fillRect(cr, col);

                Hit hit;
                hit.rect = cr;
                if (owners.isEmpty())
                    hit.tip = QStringLiteral("%1：空闲").arg(name);
                else if (declOnly)
                    hit.tip = QStringLiteral("%1：%2（库内声明了，但工程里没有任何代码引用）")
                                  .arg(name, owners.join(QStringLiteral("、")));
                else
                    hit.tip = QStringLiteral("%1：%2").arg(name, owners.join(QStringLiteral("、")));
                hit.pin = name;
                hit.board = bi;
                m_hits.append(hit);
            }
        }
        maxBottom = qMax(maxBottom, by + rows * (cell + gap));
    }

    int ly = maxBottom + 10;
    int lx = x;
    struct Leg { QColor c; QString t; };
    const Leg leg[] = {
        {pal.freeCell, QStringLiteral("空闲")},
        {pal.usedCell, QStringLiteral("占用")},
        {pal.conflictCell, QStringLiteral("冲突")},
        {pal.declCell, QStringLiteral("声明未用")},
    };
    f.setPointSize(8);
    p.setFont(f);
    for (const Leg &g : leg) {
        p.setPen(Qt::NoPen);
        p.setBrush(g.c);
        p.drawRect(QRect(lx, ly + 3, 10, 10));
        p.setPen(pal.textSec);
        p.drawText(QRect(lx + 14, ly, 56, 16), Qt::AlignLeft | Qt::AlignVCenter, g.t);
        lx += 78;
    }
    return ly + 20;
}

int DashboardCanvas::drawMemory(QPainter &p, int x, int y, int w, const Palette &pal)
{
    const QVariantMap m = m_tools.value(QStringLiteral("enc")).toMap();
    const QVariantList mem = m.value(QStringLiteral("memory")).toList();
    if (mem.isEmpty())
        return y;

    y = sectionTitle(p, x, y, w, QStringLiteral("Flash / RAM 占用"), pal);

    for (const QVariant &v : mem) {
        const QVariantMap mm = v.toMap();
        QFont f = p.font();
        f.setPointSize(9);
        f.setBold(true);
        p.setFont(f);
        p.setPen(pal.textSec);
        p.drawText(QRect(x, y, w, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   mm.value(QStringLiteral("title")).toString());
        f.setBold(false);
        p.setFont(f);
        y += 15;

        y = bar(p, x, y, w, QStringLiteral("Flash"),
                mm.value(QStringLiteral("flash_used")).toDouble(),
                mm.value(QStringLiteral("flash_kb")).toDouble() * 1024.0, pal, pal.info);
        y = bar(p, x, y, w, QStringLiteral("RAM"),
                mm.value(QStringLiteral("ram_used")).toDouble(),
                mm.value(QStringLiteral("ram_kb")).toDouble() * 1024.0, pal, pal.ok);
        y += 6;
    }
    return y + 6;
}

int DashboardCanvas::drawTasks(QPainter &p, int x, int y, int w, const Palette &pal)
{
    const QVariantMap m = m_tools.value(QStringLiteral("rtos")).toMap();
    QVariantList all;
    for (const QVariant &b : m.value(QStringLiteral("boards")).toList())
        all += b.toMap().value(QStringLiteral("tasks")).toList();
    if (all.isEmpty())
        return y;

    y = sectionTitle(p, x, y, w, QStringLiteral("RTOS 任务"), pal);

    QFont f = p.font();
    f.setPointSize(9);
    p.setFont(f);
    p.setPen(pal.textTer);
    p.drawText(QRect(x, y, w, 16), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("任务名          优先级   栈(字)   出处"));
    y += 18;

    for (const QVariant &v : all) {
        const QVariantMap t = v.toMap();
        p.setPen(pal.textPri);
        const QString line = QStringLiteral("%1   prio %2   %3   %4:%5")
                                 .arg(t.value(QStringLiteral("name")).toString(), -14)
                                 .arg(t.value(QStringLiteral("prio")).toInt())
                                 .arg(t.value(QStringLiteral("stack")).toInt())
                                 .arg(t.value(QStringLiteral("file")).toString())
                                 .arg(t.value(QStringLiteral("line")).toInt());
        p.drawText(QRect(x, y, w, 16), Qt::AlignLeft | Qt::AlignVCenter, line);
        y += 16;
    }
    return y + 12;
}

int DashboardCanvas::drawTrend(QPainter &p, int x, int y, int w, const Palette &pal)
{
    const QVariantMap m = m_tools.value(QStringLiteral("progress")).toMap();
    const QVariantList hist = m.value(QStringLiteral("history")).toList();
    if (hist.size() < 2)
        return y;

    y = sectionTitle(p, x, y, w, QStringLiteral("进度趋势（待办 / 桩）"), pal);

    const int h = 50;
    const QRect area(x + 40, y, w - 48, h);

    /* Y 轴自适应：从 min 下方一点到 max 上方一点，避免曲线被压成一条平线。
       同时把上下界标出来，读者知道量纲（不是从 0 起）。 */
    int lo = INT_MAX, hi = 0;
    for (const QVariant &v : hist) {
        const int a = v.toMap().value(QStringLiteral("markers")).toInt();
        const int b = v.toMap().value(QStringLiteral("stubs")).toInt();
        lo = qMin(lo, qMin(a, b));
        hi = qMax(hi, qMax(a, b));
    }
    if (hi <= lo)
        hi = lo + 1;
    const int pad = qMax(1, (hi - lo) / 8);
    lo = qMax(0, lo - pad);
    hi = hi + pad;
    const int span = qMax(1, hi - lo);

    p.setPen(QPen(pal.border, 1));
    p.drawLine(area.left(), area.bottom(), area.right(), area.bottom());
    p.drawLine(area.left(), area.top(), area.left(), area.bottom());

    QFont yf = p.font();
    yf.setPointSize(8);
    p.setFont(yf);
    p.setPen(pal.textTer);
    p.drawText(QRect(x, area.top() - 8, 36, 16), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(hi));
    p.drawText(QRect(x, area.bottom() - 8, 36, 16), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(lo));

    const int n = hist.size();
    auto plot = [&](const QString &key, const QColor &col) {
        QPolygon poly;
        for (int i = 0; i < n; ++i) {
            const int val = hist.at(i).toMap().value(key).toInt();
            const int px = area.left() + (n <= 1 ? 0 : i * area.width() / (n - 1));
            int py = area.bottom() - (val - lo) * area.height() / span;
            py = qBound(area.top(), py, area.bottom());
            poly << QPoint(px, py);
        }
        p.setPen(QPen(col, 2));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(poly);
    };
    plot(QStringLiteral("markers"), pal.info);
    plot(QStringLiteral("stubs"), pal.warn);

    /* A2：给每个数据点登记一个命中区，悬停显示这一轮的日期与两个读数 ——
       只有折线形状时，看不出"这是哪天"、"当时是几个待办 / 几个桩"。 */
    const int bandW = n > 1 ? qMax(8, area.width() / (n - 1)) : area.width();
    for (int i = 0; i < n; ++i) {
        const QVariantMap it = hist.at(i).toMap();
        const int px = area.left() + (n <= 1 ? 0 : i * area.width() / (n - 1));
        Hit hit;
        hit.rect = QRect(px - bandW / 2, area.top(), bandW, area.height());
        hit.tip = QStringLiteral("%1\n待办标记：%2　待实现桩：%3")
                      .arg(it.value(QStringLiteral("time")).toString(),
                           QString::number(it.value(QStringLiteral("markers")).toInt()),
                           QString::number(it.value(QStringLiteral("stubs")).toInt()));
        m_hits.append(hit);
    }

    // 图例
    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);
    int lx = x + 6;
    const int ly = area.bottom() + 6;
    struct Leg { QColor c; QString t; };
    const Leg leg[] = {
        {pal.info, QStringLiteral("待办标记")},
        {pal.warn, QStringLiteral("待实现桩")},
    };
    for (const Leg &g : leg) {
        p.setPen(QPen(g.c, 2));
        p.drawLine(lx, ly + 7, lx + 14, ly + 7);
        p.setPen(pal.textSec);
        p.drawText(QRect(lx + 18, ly, 70, 16), Qt::AlignLeft | Qt::AlignVCenter, g.t);
        lx += 96;
    }
    return ly + 24;
}

/* ---------- 交互：悬停提示 / 点击跳转 ---------- */

void DashboardCanvas::mouseMoveEvent(QMouseEvent *e)
{
    for (const Hit &h : m_hits) {
        if (h.rect.contains(e->pos()) && !h.tip.isEmpty()) {
            QToolTip::showText(e->globalPosition().toPoint(), h.tip, this);
            return;
        }
    }
    QToolTip::hideText();
}

void DashboardCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    for (const Hit &h : m_hits) {
        if (!h.rect.contains(e->pos()))
            continue;
        if (!h.key.isEmpty()) {
            emit toolActivated(h.key);       /* 健康度卡片：跳工具页 */
            return;
        }
        if (!h.pin.isEmpty()) {
            showPinDetail(h.pin, h.board);   /* 引脚格子：看占用者 */
            return;
        }
    }
}

/* 双击热力图切大格：格子 12 → 18px，密集的板子看得清；同时放宽最小宽度让两板排得下。 */
void DashboardCanvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    for (const Hit &h : m_hits) {
        if (h.pin.isEmpty() || !h.rect.contains(e->pos()))
            continue;
        m_pinZoom = !m_pinZoom;
        setMinimumWidth(m_pinZoom ? 660 : 460);
        m_contentHeight = 0;      /* 让下次 paint 重算高度 */
        update();
        return;
    }
}

/* 点击引脚格子：把该脚所有占用者的宏基名与出处拼成一行，画在热力图标题下方。 */
void DashboardCanvas::showPinDetail(const QString &pin, int board)
{
    const QVariantList boards = m_tools.value(QStringLiteral("pin")).toMap()
                                    .value(QStringLiteral("boards")).toList();
    if (board < 0 || board >= boards.size()) {
        m_pinDetail.clear();
        update();
        return;
    }
    const QVariantMap b = boards.at(board).toMap();
    const QVariantList det = b.value(QStringLiteral("pin_detail")).toMap()
                                 .value(pin).toList();

    QStringList parts;
    for (const QVariant &v : det) {
        const QVariantMap d = v.toMap();
        const QString loc = d.value(QStringLiteral("loc")).toString();
        const int colon = loc.lastIndexOf(QLatin1Char(':'));
        const QString file = QFileInfo(colon > 0 ? loc.left(colon) : loc).fileName();
        const QString line = colon > 0 ? loc.mid(colon + 1) : QString();
        parts << QStringLiteral("%1（%2:%3）")
                     .arg(d.value(QStringLiteral("base")).toString(), file, line);
    }

    m_pinDetail = parts.isEmpty()
                      ? QStringLiteral("%1 · %2 · 空闲")
                            .arg(pin, b.value(QStringLiteral("title")).toString())
                      : QStringLiteral("%1 · %2 · %3")
                            .arg(pin, b.value(QStringLiteral("title")).toString(),
                                 parts.join(QStringLiteral("；")));
    update();
}

void DashboardCanvas::leaveEvent(QEvent *)
{
    QToolTip::hideText();
}
