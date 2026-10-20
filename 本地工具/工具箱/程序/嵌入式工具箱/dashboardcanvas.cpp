#include "dashboardcanvas.h"

#include "theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPolygon>
#include <QSet>
#include <QSizePolicy>
#include <QToolTip>
#include <QVariantList>

#include <algorithm>
#include <climits>

namespace {
const int kMargin = 12;
const int kGap = 6;

/* H12：红黄线阈值抽成**一份**口径（速查数据/rules/dashboard_thresholds.json），
   免得"面板 1~4 黄 / ≥5 红"这类数字散在代码里、改一处忘另一处。
   文件读不到就用下面这组内置默认值兜底（与 JSON 里那份一致）。
   测试可用环境变量 TOOLBOX_THRESHOLDS 指到临时配置，再调 resetThresholdsForTest() 重载。 */
struct Thresholds {
    int pinYellow = 1, pinRed = 5;
    int encYellow = 1, encRed = 5;
    int loraYellow = 1, loraRed = 3;
    int protoYellow = 1, protoRed = 3;
    int cacheRedSec = 600, cacheYellowSec = 1800;
    QString source;
};

QString defaultThresholdsPath()
{
    /* 从 exe 位置向上找含「速查数据」的那层（与其它工具定位仓库内文件的口径一致）。 */
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        const QString p = d.filePath(QStringLiteral("速查数据/rules/dashboard_thresholds.json"));
        if (QFileInfo::exists(p))
            return p;
        if (!d.cdUp())
            break;
    }
    return QString();
}

Thresholds loadThresholds()
{
    Thresholds t;
    const QByteArray box = qgetenv("TOOLBOX_THRESHOLDS");
    const QString path = box.isEmpty() ? defaultThresholdsPath()
                                       : QString::fromLocal8Bit(box);
    t.source = path;
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly))
        return t;                             /* 读不到：用内置默认值 */
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    auto grp = [&o](const char *k, const char *yk, int &yv, const char *rk, int &rv) {
        const QJsonObject g = o.value(QLatin1String(k)).toObject();
        if (g.contains(QLatin1String(yk)))
            yv = g.value(QLatin1String(yk)).toInt(yv);
        if (g.contains(QLatin1String(rk)))
            rv = g.value(QLatin1String(rk)).toInt(rv);
    };
    grp("pin_conflicts",  "yellow_min", t.pinYellow,  "red_min", t.pinRed);
    grp("enc_violations", "yellow_min", t.encYellow,  "red_min", t.encRed);
    grp("lora_issues",    "yellow_min", t.loraYellow, "red_min", t.loraRed);
    grp("protocol_issues","yellow_min", t.protoYellow,"red_min", t.protoRed);
    {
        const QJsonObject g = o.value(QStringLiteral("cache")).toObject();
        if (g.contains(QStringLiteral("red_below_seconds")))
            t.cacheRedSec = g.value(QStringLiteral("red_below_seconds")).toInt(t.cacheRedSec);
        if (g.contains(QStringLiteral("yellow_below_seconds")))
            t.cacheYellowSec = g.value(QStringLiteral("yellow_below_seconds"))
                                   .toInt(t.cacheYellowSec);
    }
    return t;
}

const Thresholds &thresholds()
{
    static Thresholds t = loadThresholds();
    return t;
}
}   // namespace

/* H12：给自检用的口径查询与缓存重置 —— 让 --thresholds-probe 能断言"配置真的在起作用"。 */
int DashboardCanvas::redMinFor(const QString &kind)
{
    const Thresholds &t = thresholds();
    if (kind == QLatin1String("pin"))      return t.pinRed;
    if (kind == QLatin1String("enc"))      return t.encRed;
    if (kind == QLatin1String("lora"))     return t.loraRed;
    if (kind == QLatin1String("protocol")) return t.protoRed;
    return 0;
}

int DashboardCanvas::yellowMinFor(const QString &kind)
{
    const Thresholds &t = thresholds();
    if (kind == QLatin1String("pin"))      return t.pinYellow;
    if (kind == QLatin1String("enc"))      return t.encYellow;
    if (kind == QLatin1String("lora"))     return t.loraYellow;
    if (kind == QLatin1String("protocol")) return t.protoYellow;
    return 0;
}

int DashboardCanvas::cacheRedSeconds()    { return thresholds().cacheRedSec; }
int DashboardCanvas::cacheYellowSeconds() { return thresholds().cacheYellowSec; }
QString DashboardCanvas::thresholdsSource() { return thresholds().source; }

void DashboardCanvas::resetThresholdsForTest()
{
    /* 只有一个函数内的 static 缓存；用一个"重载"入口把它换掉（仅测试用）。 */
    static Thresholds *slot = nullptr;
    if (!slot) {
        /* 第一次调用：借 thresholds() 触发初始化，然后记录地址以便后续覆盖 */
        slot = const_cast<Thresholds *>(&thresholds());
        return;
    }
    *slot = loadThresholds();
}

DashboardCanvas::DashboardCanvas(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    /* 热力图每排放不下就换排：两板并排 ≥ 2*190 + 56 + 左右边距 ≈ 460，留点余量 */
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

    y = drawConclusion(p, kMargin, y, w, pal);   /* B3：先给一句话结论 + 最该先看的 3 件事 */
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

    /* H12 阈值红黄线：数量类指标到多少处算「注意」（黄）、多少处算「严重」（红）——
       数字统一来自 `速查数据/rules/dashboard_thresholds.json` 这一份口径（读不到用内置默认），
       免得改一处忘另一处。详情文字里的数字照旧保留，颜色只负责"要不要现在就去看"。 */
    const Thresholds &TH = thresholds();
    auto level = [&pal](int n, int redMin) -> QColor {
        if (n <= 0)
            return pal.ok;
        return n >= redMin ? pal.err : pal.warn;
    };
    auto thrText = [](int yellow, int red) {
        return QStringLiteral("%1~%2 处黄 · ≥%3 处红")
            .arg(yellow).arg(qMax(yellow, red - 1)).arg(red);
    };

    {
        const QVariantMap m = m_tools.value(QStringLiteral("pin")).toMap();
        int conflicts = 0, decls = 0, dbg = 0;
        for (const QVariant &b : m.value(QStringLiteral("boards")).toList()) {
            const QVariantMap bm = b.toMap();
            conflicts += bm.value(QStringLiteral("conflicts")).toMap().size();
            decls += bm.value(QStringLiteral("conflicts_decl")).toMap().size();
            dbg += bm.value(QStringLiteral("debug_used")).toList().size();
        }
        /* B1：只有"真会撞"（两个占用者工程里都在用）才算冲突 —— 库里成对声明、工程没启用的
           那些只是"潜在"，单列个数但不染红黄，否则满屏红字却不知道该改哪个。
           字要短：8 张卡一行，长文本会被裁掉，明细看悬停提示。 */
        QString detail = conflicts ? QStringLiteral("%1 真冲突").arg(conflicts)
                                   : QStringLiteral("无真冲突");
        if (decls)
            detail += QStringLiteral(" · %1 潜在").arg(decls);
        if (dbg)
            detail += QStringLiteral(" · 调试口%1").arg(dbg);
        items.append({QStringLiteral("引脚规划"), detail,
                      (dbg || conflicts >= TH.pinRed) ? pal.err
                                                      : (conflicts ? pal.warn : pal.ok),
                      QStringLiteral("真冲突 %1 · 调试口被真占即红"
                                     "（仅声明重叠的「潜在」不染色）")
                          .arg(thrText(TH.pinYellow, TH.pinRed))});
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
                      level(v, TH.encRed), thrText(TH.encYellow, TH.encRed)});
    }
    {
        const QVariantMap m = m_tools.value(QStringLiteral("lora")).toMap();
        int n = m.value(QStringLiteral("cross")).toList().size();
        for (const QVariant &b : m.value(QStringLiteral("boards")).toList())
            n += b.toMap().value(QStringLiteral("issues")).toList().size();
        items.append({QStringLiteral("LoRa 检查"),
                      n ? QStringLiteral("%1 问题").arg(n) : QStringLiteral("正常"),
                      level(n, TH.loraRed), thrText(TH.loraYellow, TH.loraRed)});
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
                      level(n, TH.protoRed), thrText(TH.protoYellow, TH.protoRed)});
    }
    {
        const QVariantMap m = m_tools.value(QStringLiteral("cache")).toMap();
        const int sv = m.value(QStringLiteral("severe")).toInt();
        const double holdSec = m.value(QStringLiteral("hold_seconds")).toDouble();
        const QString hold = m.value(QStringLiteral("hold_text")).toString();
        /* A1：除了"补传慢于写入 / 越界 / 记录塞不下"（severe）算红，再按"断网可扛时长"分级：
           < red_below_seconds 红、< yellow_below_seconds 黄（阈值来自 H12 那份口径）。
           注意 hold_seconds 为 0 表示宏没算出来 —— 那属于"未核算"，不替它染红（报告里另有严重项）。 */
        QColor col = pal.ok;
        if (sv || (holdSec > 0 && holdSec < TH.cacheRedSec))
            col = pal.err;
        else if (holdSec > 0 && holdSec < TH.cacheYellowSec)
            col = pal.warn;
        items.append({QStringLiteral("断网缓存"),
                      hold.isEmpty() ? QStringLiteral("未核算")
                                     : QStringLiteral("可扛 %1").arg(hold),
                      col,
                      QStringLiteral("补传慢于写入即红 · 可扛 <%1 分钟红 · <%2 分钟黄")
                          .arg(TH.cacheRedSec / 60).arg(TH.cacheYellowSec / 60)});
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

/* C2：结论 + 「最该先看的几件」抽成**纯函数**（不碰画笔）—— 驾驶舱页与总览页共用这一份口径。
   为什么要抽：这段判定原来只在 drawConclusion 里算，而**隐藏页不绘画** —— 用户没进过
   「工程体检总览」时那份结论压根没算出来，驾驶舱页就只能是"空着"或"另写一套判定"。
   判定口径仍是 B3/H12 那一套：引脚调试口=严重、数量类按阈值表、缓存按可扛时长。 */
QVector<DashboardCanvas::Finding> DashboardCanvas::findingsFor(const QVariantMap &tools,
                                                               const QStringList &failedKeys,
                                                               QString *head)
{
    QVector<Finding> bad;                     /* level: 0=严重 1=注意 */
    const Thresholds &TH = thresholds();       /* H12：与健康卡片同一份口径 */

    {
        const QVariantMap m = tools.value(QStringLiteral("pin")).toMap();
        int conflicts = 0, dbg = 0;
        for (const QVariant &b : m.value(QStringLiteral("boards")).toList()) {
            conflicts += b.toMap().value(QStringLiteral("conflicts")).toMap().size();
            dbg += b.toMap().value(QStringLiteral("debug_used")).toList().size();
        }
        /* B1：结论只收"真会撞/真危险"——仅声明重叠的不进"最该先看的 3 件"，
           否则清单里塞满还没启用的模块，等于把真正要动手的事埋了。 */
        if (dbg)
            bad.append({QStringLiteral("pin"), QStringLiteral("引脚规划"),
                        QStringLiteral("调试口被业务宏占了"), 0});
        else if (conflicts)
            bad.append({QStringLiteral("pin"), QStringLiteral("引脚规划"),
                        QStringLiteral("%1 处引脚冲突").arg(conflicts),
                        conflicts >= TH.pinRed ? 0 : 1});
    }
    {
        const QVariantMap m = tools.value(QStringLiteral("rtos")).toMap();
        int sev = 0, wr = 0;
        for (const QVariant &b : m.value(QStringLiteral("boards")).toList()) {
            sev += b.toMap().value(QStringLiteral("severe")).toInt();
            wr += b.toMap().value(QStringLiteral("warn")).toInt();
        }
        if (sev)
            bad.append({QStringLiteral("rtos"), QStringLiteral("RTOS 检查"),
                        QStringLiteral("%1 处严重（优先级 / ISR 用法）").arg(sev), 0});
        else if (wr)
            bad.append({QStringLiteral("rtos"), QStringLiteral("RTOS 检查"),
                        QStringLiteral("%1 处注意").arg(wr), 1});
    }
    {
        const QVariantMap m = tools.value(QStringLiteral("enc")).toMap();
        const int v = m.value(QStringLiteral("violations")).toInt();
        if (v)
            bad.append({QStringLiteral("enc"), QStringLiteral("编码守卫"),
                        QStringLiteral("%1 个文件违规").arg(v), v >= TH.encRed ? 0 : 1});
    }
    {
        const QVariantMap m = tools.value(QStringLiteral("lora")).toMap();
        int n = m.value(QStringLiteral("cross")).toList().size();
        for (const QVariant &b : m.value(QStringLiteral("boards")).toList())
            n += b.toMap().value(QStringLiteral("issues")).toList().size();
        if (n)
            bad.append({QStringLiteral("lora"), QStringLiteral("LoRa 检查"),
                        QStringLiteral("%1 处问题").arg(n), n >= TH.loraRed ? 0 : 1});
    }
    {
        const QVariantMap m = tools.value(QStringLiteral("protocol")).toMap();
        int n = 0;
        for (const QVariant &b : m.value(QStringLiteral("boards")).toList())
            n += b.toMap().value(QStringLiteral("issues")).toInt();
        n += m.value(QStringLiteral("findings")).toList().size();
        if (n)
            bad.append({QStringLiteral("protocol"), QStringLiteral("协议校验"),
                        QStringLiteral("%1 处问题").arg(n), n >= TH.protoRed ? 0 : 1});
    }
    {
        const QVariantMap m = tools.value(QStringLiteral("cache")).toMap();
        const int sv = m.value(QStringLiteral("severe")).toInt();
        const double hold = m.value(QStringLiteral("hold_seconds")).toDouble();
        const QString holdText = m.value(QStringLiteral("hold_text")).toString();
        if (sv)
            bad.append({QStringLiteral("cache"), QStringLiteral("断网缓存"),
                        QStringLiteral("补传跟不上写入或越界（%1）").arg(holdText), 0});
        else if (hold > 0 && hold < TH.cacheRedSec)
            bad.append({QStringLiteral("cache"), QStringLiteral("断网缓存"),
                        QStringLiteral("只能扛 %1（不足 %2 分钟）")
                            .arg(holdText).arg(TH.cacheRedSec / 60), 0});
        else if (hold > 0 && hold < TH.cacheYellowSec)
            bad.append({QStringLiteral("cache"), QStringLiteral("断网缓存"),
                        QStringLiteral("只能扛 %1（不足 %2 分钟）")
                            .arg(holdText).arg(TH.cacheYellowSec / 60), 1});
    }

    int sev = 0, wr = 0;
    for (const Finding &it : bad) {
        if (it.level == 0)
            ++sev;
        else
            ++wr;
    }
    std::stable_sort(bad.begin(), bad.end(),
                     [](const Finding &a, const Finding &b) { return a.level < b.level; });

    QString line;
    if (sev == 0 && wr == 0)
        line = tools.isEmpty() ? QStringLiteral("尚未取数 —— 点「刷新」跑一轮体检")
                               : QStringLiteral("本轮体检：全部正常，可以放心往下写");
    else
        line = QStringLiteral("本轮体检：%1 处严重、%2 处注意").arg(sev).arg(wr);
    if (!failedKeys.isEmpty())
        line += QStringLiteral("（另有 %1 个检查器没取到数）").arg(failedKeys.size());
    if (head)
        *head = line;
    return bad;
}

/* B3：面板顶部一句话结论 + 最该先看的 3 件事（每一项可点，直达报告里对应那一节）。
   B3 的目的：不用先看懂 7 张卡片与一堆数字，也知道"现在什么状态、先动哪一个"。
   判定口径与健康度卡片一致（引脚调试口=严重、数量类按 H12 那份阈值、缓存按可扛时长）——
   实际算的是 findingsFor()，驾驶舱页读的也是它。 */
int DashboardCanvas::drawConclusion(QPainter &p, int x, int y, int w, const Palette &pal)
{
    QString head;
    const QVector<Finding> bad = findingsFor(m_tools, m_failedKeys, &head);
    int sev = 0, wr = 0;
    for (const Finding &it : bad) {
        if (it.level == 0)
            ++sev;
        else
            ++wr;
    }
    /* 缓存给自检读（--shot-overview 断言"结论不能空、有问题必须写严重/注意"） */
    m_conclusionText = head;
    m_conclusionCount = bad.size();

    QFont f = p.font();
    f.setPointSize(10);
    f.setBold(true);
    p.setFont(f);
    p.setPen(sev ? pal.err : (wr ? pal.warn : pal.ok));
    p.drawText(QRect(x, y, w, 20), Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(head, Qt::ElideRight, w));
    y += 22;

    f.setBold(false);
    f.setPointSize(9);
    p.setFont(f);
    if (!bad.isEmpty()) {
        p.setPen(pal.textSec);
        p.drawText(QRect(x, y, 58, 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("建议先看："));
        int lx = x + 58;
        const int top = qMin(3, bad.size());
        for (int i = 0; i < top; ++i) {
            const QString label = QStringLiteral("%1 %2（%3）")
                                      .arg(i + 1).arg(bad.at(i).name, bad.at(i).why);
            const int tw = p.fontMetrics().horizontalAdvance(label) + 8;
            if (lx + tw > x + w)
                break;
            const QRect r(lx, y, tw, 18);
            p.setPen(bad.at(i).level == 0 ? pal.err : pal.warn);
            p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, label);
            Hit hit;
            hit.rect = r;
            hit.key = bad.at(i).key;
            hit.tip = QStringLiteral("%1：%2\n点击直达报告里那一节").arg(bad.at(i).name, bad.at(i).why);
            m_hits.append(hit);
            lx += tw + 4;
        }
        y += 20;
    }
    p.setPen(pal.textTer);
    p.drawText(QRect(x, y, w, 16), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("（点上面任一项、或点下面的健康度卡片，都能直达报告里对应的那一节）"));
    return y + 22;
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
    const int colGap = 56;      /* 同一排两板之间的横向间距 */
    const int rowGap = 26;      /* 放不下换排时的纵向间距 */
    /* 每排能放几块板：按可用宽度算，放不下就换排 —— 这样第三块（OTA）也会画出来，
       而不是像过去那样写死"只画两板"。 */
    const int perRow = qMax(1, (w + colGap) / (gridW + colGap));
    const int boardH = 20 + topH + rows * (cell + gap);   /* 一块板的高：标题 + 列头 + 网格 */
    const QString ports = QStringLiteral("ABCDEFG");

    QFont f = p.font();
    p.setFont(f);

    int maxBottom = y;
    /* A2：不再写死只画两板 —— 板表里有几块就画几块（第三块起换到下一排），
       否则 OTA 工程的引脚占用在面板上永远是空白，只能翻报告。 */
    for (int bi = 0; bi < boards.size(); ++bi) {
        const QVariantMap b = boards[bi].toMap();
        const int bx = x + (bi % perRow) * (gridW + colGap);
        int by = y + (bi / perRow) * (boardH + rowGap);

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
        /* B1：仅声明重叠 —— 多个宏声明了同一脚，但工程里只启用了其中一个（其余是还没启用的
           模块）。这不是"真会撞"，单列出来，免得跟真冲突混成一片红。 */
        const QVariantMap conDecl = b.value(QStringLiteral("conflicts_decl")).toMap();
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
                bool declOnly = false;      /* 声明未用（僵尸宏，B1 的第四态） */
                bool declOverlap = false;   /* 仅声明重叠（B1：潜在，不是真冲突） */
                if (con.contains(name)) {
                    col = pal.conflictCell;
                    for (const QVariant &o : con.value(name).toList())
                        owners << o.toString();
                } else if (conDecl.contains(name)) {
                    col = pal.declCell;
                    declOverlap = true;
                    for (const QVariant &o : conDecl.value(name).toList())
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
                else if (declOverlap)
                    hit.tip = QStringLiteral("%1：%2（多个宏声明了同一脚，但工程里只启用了其中一个"
                                             "—— 属潜在，不是真冲突）")
                                  .arg(name, owners.join(QStringLiteral("、")));
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
        {pal.conflictCell, QStringLiteral("真冲突")},
        {pal.declCell, QStringLiteral("声明未用/重叠")},
    };
    f.setPointSize(8);
    p.setFont(f);
    for (const Leg &g : leg) {
        p.setPen(Qt::NoPen);
        p.setBrush(g.c);
        p.drawRect(QRect(lx, ly + 3, 10, 10));
        p.setPen(pal.textSec);
        p.drawText(QRect(lx + 14, ly, 78, 16), Qt::AlignLeft | Qt::AlignVCenter, g.t);
        lx += 96;
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
