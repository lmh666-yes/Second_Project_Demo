#include "rtosgen.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringConverter>

#include <algorithm>
#include <cmath>
#include <functional>

#include "apppaths.h"     /* 与检查器共读同一份规则表：速查数据/rules/ */
#include "lookupdata.h"   /* 从「单片机手册」的 dataset.json 取补全素材 */

namespace RtosGen {

static QString readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QByteArray raw = f.readAll();
    f.close();
    for (const auto enc : {QStringConverter::Utf8, QStringConverter::System,
                           QStringConverter::Latin1}) {
        QStringDecoder dec(enc);
        const QString s = dec(raw);
        if (!dec.hasError())
            return s;
    }
    return QString::fromLatin1(raw);
}

/* 求简单整数宏：去类型转换、去 UL/L 后缀、代入已知宏。失败返回一个标记值。 */
static int evalInt(const QString &expr, const QHash<QString, int> &known, bool *ok)
{
    QString e = expr;
    e.replace(QRegularExpression(
                  QStringLiteral("\\(\\s*(?:unsigned\\s+short|unsigned\\s+int|unsigned\\s+long|"
                                 "size_t|int|uint\\d+_t|int\\d+_t|TickType_t|UBaseType_t|BaseType_t)"
                                 "\\s*\\)")),
              QString());
    for (auto it = known.constBegin(); it != known.constEnd(); ++it)
        e.replace(QRegularExpression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(it.key()))),
                  QString::number(it.value()));
    e.replace(QRegularExpression(QStringLiteral("\\b(\\d+)\\s*[uUlL]+\\b")), QStringLiteral("\\1"));
    e.replace(QLatin1Char('L'), QString());
    e = e.trimmed();
    static const QRegularExpression simple(QStringLiteral("^[\\d\\s+\\-*/()]+$"));
    if (!simple.match(e).hasMatch()) {
        if (ok)
            *ok = false;
        return 0;
    }
    /* 只含数字与四则运算符，按 C 整数语义手算（避免用 eval） */
    int pos = 0;
    auto skip = [&]() { while (pos < e.size() && e.at(pos).isSpace()) ++pos; };
    std::function<int()> parseExpr;
    auto parseAtom = [&]() -> int {
        skip();
        if (pos < e.size() && e.at(pos) == '(') {
            ++pos;
            const int v = parseExpr();
            skip();
            if (pos < e.size() && e.at(pos) == ')')
                ++pos;
            return v;
        }
        int v = 0;
        while (pos < e.size() && e.at(pos).isDigit()) {
            v = v * 10 + e.at(pos).digitValue();
            ++pos;
        }
        return v;
    };
    auto parseTerm = [&]() -> int {
        int v = parseAtom();
        for (;;) {
            skip();
            if (pos < e.size() && (e.at(pos) == '*' || e.at(pos) == '/')) {
                const QChar op = e.at(pos++);
                const int r = parseAtom();
                v = (op == '*') ? v * r : (r ? v / r : 0);
            } else {
                break;
            }
        }
        return v;
    };
    parseExpr = [&]() -> int {
        int v = parseTerm();
        for (;;) {
            skip();
            if (pos < e.size() && (e.at(pos) == '+' || e.at(pos) == '-')) {
                const QChar op = e.at(pos++);
                const int r = parseTerm();
                v = (op == '+') ? v + r : v - r;
            } else {
                break;
            }
        }
        return v;
    };
    const int result = parseExpr();
    if (ok)
        *ok = (pos >= e.size());
    return result;
}

FwConfig parseConfig(const QString &configHeaderPath)
{
    FwConfig cfg;
    cfg.path = configHeaderPath;
    const QString text = readText(configHeaderPath);
    if (text.isEmpty()) {
        cfg.notes << QStringLiteral("读不到文件：%1").arg(configHeaderPath);
        return cfg;
    }

    QHash<QString, QString> raw;
    const QString joined = QString(text).replace(QRegularExpression(QStringLiteral("\\\\\\s*\\n")),
                                                  QStringLiteral(" "));
    for (const QString &line : joined.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = QRegularExpression(
            QStringLiteral("^\\s*#\\s*define\\s+(\\w+)\\s+(.*?)(?:\\s*/\\*.*)?$")).match(line);
        if (m.hasMatch() && !raw.contains(m.captured(1)))
            raw.insert(m.captured(1), m.captured(2).trimmed());
    }
    cfg.hasAssert = QRegularExpression(QStringLiteral("#\\s*define\\s+configASSERT"))
                        .match(joined).hasMatch();

    const QStringList keys = {
        QStringLiteral("configPRIO_BITS"),
        QStringLiteral("configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY"),
        QStringLiteral("configTOTAL_HEAP_SIZE"),
        QStringLiteral("configMINIMAL_STACK_SIZE"),
        QStringLiteral("configMAX_PRIORITIES"),
        QStringLiteral("configTICK_RATE_HZ"),
        QStringLiteral("configUSE_MUTEXES"),
        QStringLiteral("configUSE_RECURSIVE_MUTEXES"),
        QStringLiteral("configUSE_COUNTING_SEMAPHORES"),
        QStringLiteral("configUSE_TIMERS"),
        QStringLiteral("configCHECK_FOR_STACK_OVERFLOW"),
    };
    for (const QString &k : keys) {
        if (!raw.contains(k))
            continue;
        bool ok = true;
        const int v = evalInt(raw.value(k), cfg.ints, &ok);
        if (ok)
            cfg.ints.insert(k, v);
        else
            cfg.notes << QStringLiteral("%1 的值无法解析：%2").arg(k, raw.value(k));
    }
    cfg.ok = cfg.ints.contains(QStringLiteral("configMAX_PRIORITIES"))
          && cfg.ints.contains(QStringLiteral("configMINIMAL_STACK_SIZE"));
    if (!cfg.ok)
        cfg.notes << QStringLiteral("未解析到 configMAX_PRIORITIES / configMINIMAL_STACK_SIZE，"
                                    "请确认选的是 FreeRTOSConfig.h");
    return cfg;
}

static QString kindText(const QString &kind)
{
    if (kind == QStringLiteral("queue"))
        return QStringLiteral("队列");
    if (kind == QStringLiteral("mutex"))
        return QStringLiteral("互斥量");
    if (kind == QStringLiteral("recursive"))
        return QStringLiteral("递归互斥量");
    if (kind == QStringLiteral("counting"))
        return QStringLiteral("计数信号量");
    if (kind == QStringLiteral("binary"))
        return QStringLiteral("二值信号量");
    if (kind == QStringLiteral("timer"))
        return QStringLiteral("软定时器");
    return kind;
}

/* ---------------- 跨流程卡冲突检测 ----------------
   勾选的每张流程卡都自带一段"可直接搬用"的初始化代码。两张卡若都往同一个中断向量里
   挂自己的 ISR、或都把某个脚配成自己的复用，同时并入就必然打架。这里只从卡片的可复用
   代码里**提取事实**（IRQn / 抢占值 / GPIO 脚位）后两两比对，不改任何代码、不内置手册表。 */

/* 剥掉注释与字符串（保留换行），避免注释里提到的 *_IRQn / GPIO_Pin_x 被当成占用。 */
static QString stripCodeComments(const QString &in)
{
    QString s = in;
    int i = 0;
    const int n = s.size();
    while (i < n) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('/') && i + 1 < n && s.at(i + 1) == QLatin1Char('/')) {
            while (i < n && s.at(i) != QLatin1Char('\n'))
                s[i++] = QLatin1Char(' ');
        } else if (c == QLatin1Char('/') && i + 1 < n && s.at(i + 1) == QLatin1Char('*')) {
            s[i] = s[i + 1] = QLatin1Char(' ');
            i += 2;
            while (i + 1 < n && !(s.at(i) == QLatin1Char('*') && s.at(i + 1) == QLatin1Char('/'))) {
                if (s.at(i) != QLatin1Char('\n'))
                    s[i] = QLatin1Char(' ');
                ++i;
            }
            if (i + 1 < n) {
                s[i] = s[i + 1] = QLatin1Char(' ');
                i += 2;
            } else {
                i = n;
            }
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar q = c;
            s[i++] = QLatin1Char(' ');
            while (i < n && s.at(i) != q) {
                if (s.at(i) == QLatin1Char('\\') && i + 1 < n) {
                    s[i] = s[i + 1] = QLatin1Char(' ');
                    i += 2;
                } else {
                    if (s.at(i) != QLatin1Char('\n'))
                        s[i] = QLatin1Char(' ');
                    ++i;
                }
            }
            if (i < n)
                s[i++] = QLatin1Char(' ');
        } else {
            ++i;
        }
    }
    return s;
}

CardRefs extractCardRefs(const QString &code)
{
    CardRefs r;
    const QString s = stripCodeComments(code);
    auto toNum = [](const QString &v) -> int {
        bool ok = false;
        const int n = v.toInt(&ok);
        return ok ? n : -1;   /* 值写成宏（SYS_USART_IRQ_PRE_PRIO）时判不出数，记 -1 */
    };
    auto addIrq = [&r](const QString &q) {
        if (!q.isEmpty() && !r.irqs.contains(q))
            r.irqs << q;
    };

    /* (a) 所有 *_IRQn 符号 */
    static const QRegularExpression irqRe(QStringLiteral("\\b([A-Za-z_]\\w*_IRQn)\\b"));
    {
        auto it = irqRe.globalMatch(s);
        while (it.hasNext())
            addIrq(it.next().captured(1));
    }

    /* (b) xxxNVIC_SetPriority(IRQn, 抢占值, ...) —— 覆盖 SYS_/HAL_/__ 前缀与裸名 */
    static const QRegularExpression setRe(QStringLiteral(
        "[A-Za-z_]*NVIC_SetPriority\\s*\\(\\s*([A-Za-z_]\\w*_IRQn)\\s*,\\s*(\\w+)"));
    {
        auto it = setRe.globalMatch(s);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            addIrq(m.captured(1));
            r.irqPreempt.insert(m.captured(1), toNum(m.captured(2)));
        }
    }

    /* (c) NVIC_InitStructure 写法：先 NVIC_IRQChannel = X_IRQn，随后 PreemptionPriority = N */
    static const QRegularExpression chRe(QStringLiteral("NVIC_IRQChannel\\s*=\\s*([A-Za-z_]\\w*)"));
    static const QRegularExpression preRe(
        QStringLiteral("NVIC_IRQChannelPreemptionPriority\\s*=\\s*(\\w+)"));
    {
        QString cur;
        for (const QString &line : s.split(QLatin1Char('\n'))) {
            const QRegularExpressionMatch mc = chRe.match(line);
            if (mc.hasMatch())
                cur = mc.captured(1);
            const QRegularExpressionMatch mp = preRe.match(line);
            if (mp.hasMatch()) {
                if (cur.isEmpty())
                    r.preemptNoChannel = true;
                else
                    r.irqPreempt.insert(cur, toNum(mp.captured(1)));
            }
        }
    }

    /* (d) 引脚：GPIO_PinAFConfig 是"端口+脚"写在同一个调用里，最可靠 */
    static const QRegularExpression afRe(QStringLiteral(
        "GPIO_PinAFConfig\\s*\\(\\s*GPIO([A-K])\\s*,\\s*GPIO_PinSource\\s*(\\d+)"));
    auto addPin = [&r](const QString &p) {
        if (!p.isEmpty() && !r.pins.contains(p))
            r.pins << p;
    };
    /* 脚位统一写成 PXn（PA9 / PB10），跨卡比对才好对上 */
    auto mkPin = [](const QString &port, const QString &num) {
        return QStringLiteral("P") + port + num;
    };
    {
        auto it = afRe.globalMatch(s);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            addPin(mkPin(m.captured(1), m.captured(2)));
        }
    }

    /* (e) 引脚：GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10; 这类赋值先记下来，
       等 GPIO_Init(GPIOx, &GPIO_InitStructure) 出现时再落到端口上（卡里普遍这么写）。
       同一行同时出现唯一端口 token 的，直接就地关联。 */
    static const QRegularExpression pinRe(QStringLiteral("GPIO_Pin_(\\d+)"));
    static const QRegularExpression portRe(QStringLiteral("\\bGPIO([A-K])\\b"));
    static const QRegularExpression initRe(QStringLiteral("GPIO_Init\\s*\\(\\s*GPIO([A-K])\\b"));
    {
        QStringList pending;
        for (const QString &line : s.split(QLatin1Char('\n'))) {
            QStringList ports;
            auto pit = portRe.globalMatch(line);
            while (pit.hasNext())
                ports << pit.next().captured(1);
            const QRegularExpressionMatch mi = initRe.match(line);
            if (mi.hasMatch()) {
                const QString port = mi.captured(1);
                for (const QString &p : pending)
                    addPin(mkPin(port, p));
                pending.clear();
                continue;
            }
            QStringList nums;
            auto nit = pinRe.globalMatch(line);
            while (nit.hasNext())
                nums << nit.next().captured(1);
            if (nums.isEmpty())
                continue;
            if (line.contains(QLatin1Char('='))) {
                /* 赋值给 InitStructure 的 Pin 字段 → 待定，等 GPIO_Init 落端口 */
                for (const QString &x : nums)
                    if (!pending.contains(x))
                        pending << x;
            } else if (ports.size() == 1) {
                for (const QString &x : nums)
                    addPin(mkPin(ports.first(), x));
            }
            /* 端口不只一个又没走 GPIO_Init → 关联不确定，宁可不报（避免误报） */
        }
    }
    return r;
}

QStringList flowConflictsFromRefs(const QStringList &names, const QVector<CardRefs> &refs)
{
    QStringList out;
    const int n = qMin(names.size(), int(refs.size()));
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            for (const QString &irq : refs.at(i).irqs) {
                if (!refs.at(j).irqs.contains(irq))
                    continue;
                out << QStringLiteral(
                           "[参考卡] 「%1」与「%2」都要用中断向量 %3：两者会往同一个 ISR 上挂自己的处理，"
                           "同时并入只能留一个，请确定由谁处理")
                           .arg(names.at(i), names.at(j), irq);
                const int a = refs.at(i).irqPreempt.value(irq, -2);
                const int b = refs.at(j).irqPreempt.value(irq, -2);
                if (a >= 0 && b >= 0 && a != b)
                    out << QStringLiteral(
                               "[参考卡] 「%1」与「%2」给 %3 配的抢占值不一致（%4 / %5）："
                               "同一中断只能有一个抢占值，后配的会盖掉先配的，需统一")
                               .arg(names.at(i), names.at(j), irq).arg(a).arg(b);
            }
            QStringList shared;
            for (const QString &p : refs.at(i).pins)
                if (refs.at(j).pins.contains(p) && !shared.contains(p))
                    shared << p;
            if (!shared.isEmpty())
                out << QStringLiteral(
                           "[参考卡] 「%1」与「%2」都占用了引脚 %3：同一脚位不能配两种复用，需确定归谁用")
                           .arg(names.at(i), names.at(j), shared.join(QStringLiteral("、")));
        }
    }
    QStringList uniq;
    for (const QString &m : out)
        if (!uniq.contains(m))
            uniq << m;
    return uniq;
}

/* ---------------- 规则表（数据模型）：与检查器共读同一份 ----------------
   中断上下文的 API 分类（哪些接口会阻塞 / 哪些内核临界区不能进）不是"芯片手册数据"，
   而是 FreeRTOS 自身的规则，已经外置在 工具箱/速查数据/rules/freertos_isr_rules.json：
   检查器 rtos_check.py 读它，这里也读它 —— 一份数据两处用，改表就两边同时生效。
   表读不到时本功能静默关闭（不猜、不内置第二份，避免两份规则漂移）。 */

namespace {

struct IsrRules {
    QStringList blocking;   /* 任务态接口：ISR 里调会阻塞 / 破坏内核 */
    QStringList critical;   /* 内核临界区：ISR 里要改用 *_FROM_ISR 版本 */
    QString     source;
    bool        loaded = false;
};

const IsrRules &isrRules()
{
    static const IsrRules r = [] {
        IsrRules x;
        const QString root = AppPaths::toolRoot();
        if (root.isEmpty())
            return x;
        const QString p = QDir(root).filePath(QStringLiteral("rules/freertos_isr_rules.json"));
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly))
            return x;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonArray b = o.value(QStringLiteral("blocking_api")).toArray();
        const QJsonArray c = o.value(QStringLiteral("isr_unsafe_critical")).toArray();
        for (const QJsonValue &v : b)
            x.blocking << v.toString();
        for (const QJsonValue &v : c)
            x.critical << v.toString();
        x.source = p;
        x.loaded = !x.blocking.isEmpty();
        return x;
    }();
    return r;
}

} // namespace

QStringList cardIsrApiIssues(const QString &cardName, const QString &code)
{
    QStringList out;
    const IsrRules &rules = isrRules();
    if (!rules.loaded || code.trimmed().isEmpty())
        return out;
    const QString s = stripCodeComments(code);

    static const QRegularExpression hdrRe(QStringLiteral(
        "\\b(\\w+_IRQHandler|\\w+_isr|\\w+_irq)\\s*\\([^;{)]*\\)\\s*\\{"));
    static const QRegularExpression callRe(QStringLiteral("([A-Za-z_]\\w*)\\s*\\("));
    auto it = hdrRe.globalMatch(s);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString fname = m.captured(1);
        /* 从 '{' 之后开始数括号，取出整段函数体 */
        const int bodyStart = m.capturedEnd();
        int depth = 1, i = bodyStart;
        while (i < s.size() && depth > 0) {
            const QChar ch = s.at(i);
            if (ch == QLatin1Char('{'))
                ++depth;
            else if (ch == QLatin1Char('}'))
                --depth;
            ++i;
        }
        const QString body = s.mid(bodyStart, qMax(0, i - bodyStart - 1));
        auto it2 = callRe.globalMatch(body);
        while (it2.hasNext()) {
            const QString api = it2.next().captured(1);
            if (rules.blocking.contains(api))
                out << QStringLiteral("[参考卡] 「%1」的中断服务函数 %2 里调用了 %3（任务态接口）："
                                      "中断上下文只能用 %3FromISR 系列，直接调会阻塞、破坏内核临界区")
                           .arg(cardName, fname, api);
            else if (rules.critical.contains(api))
                out << QStringLiteral("[参考卡] 「%1」的中断服务函数 %2 里调用了 %3（内核临界区）："
                                      "中断上下文要改用对应的 *_FROM_ISR 版本")
                           .arg(cardName, fname, api);
        }
    }
    QStringList uniq;
    for (const QString &msg : out)
        if (!uniq.contains(msg))
            uniq << msg;
    return uniq;
}

QStringList flowConflicts(const QStringList &flowPicks)
{
    QStringList out;
    if (flowPicks.isEmpty())
        return out;   /* 没勾卡就不碰速查数据 */
    QString err;
    if (!LookupData::load(&err)) {
        out << QStringLiteral("[参考卡] 勾了 %1 张卡，但读不到「单片机手册」数据，跨卡冲突检查做不了：%2")
                   .arg(flowPicks.size()).arg(err);
        return out;
    }
    QStringList names;
    QVector<CardRefs> refs;
    for (const QString &name : flowPicks) {
        const LookupData::FlowHit h = LookupData::flowByName(name);
        if (h.name.isEmpty()) {
            out << QStringLiteral("[参考卡] 勾选的卡「%1」在速查数据里找不到（名字要与速查里一字不差）")
                       .arg(name);
            continue;
        }
        QStringList fns, incs;
        names << name;
        refs << extractCardRefs(LookupData::extractReusable(h.code, &fns, &incs));
        /* 同一份规则表也用来拦"卡片里的 ISR 调了任务态接口"（检查器读的也是它） */
        out += cardIsrApiIssues(name, h.code);
    }
    out += flowConflictsFromRefs(names, refs);
    QStringList uniq;
    for (const QString &m : out)
        if (!uniq.contains(m))
            uniq << m;
    return uniq;
}

/* ---------------- 周期任务可调度性（确定性数学，只给建议不改任务） ---------------- */

QStringList scheduleAnalysis(const QVector<TaskSpec> &tasks)
{
    QStringList out;
    QVector<const TaskSpec *> per;   /* 填了 WCET 的周期任务才纳入分析 */
    int missing = 0;
    for (const TaskSpec &t : tasks) {
        if (t.kind != QStringLiteral("周期"))
            continue;
        if (t.wcetUs > 0 && t.periodMs > 0)
            per << &t;
        else
            ++missing;
    }
    if (per.isEmpty())
        return out;

    const int n = per.size();
    double U = 0.0;
    for (const TaskSpec *t : per)
        U += double(t->wcetUs) / (double(t->periodMs) * 1000.0);
    const double bound = n * (std::pow(2.0, 1.0 / n) - 1.0);

    out << QStringLiteral("[可调度] 周期任务利用率 U = %1（%2 个任务填了 WCET），"
                          "RMS 上界 n(2^(1/n)-1) = %3")
               .arg(U, 0, 'f', 3).arg(n).arg(bound, 0, 'f', 3);

    /* RMS 的前提是"周期越短优先级越高"；用户手填的优先级若没按这个来，
       上界结论就不适用，必须先讲清楚，别让人照着错的前提调参。 */
    bool monotonic = true;
    for (const TaskSpec *a : per)
        for (const TaskSpec *b : per)
            if (a->periodMs < b->periodMs && a->prio < b->prio)
                monotonic = false;
    if (!monotonic)
        out << QStringLiteral("[可调度] 优先级没有按「周期越短越高」分配，而 RMS 上界只在"
                              "该前提下成立——先把周期短的排到更高优先级，再按本结论调参");

    if (U > bound + 1e-9)
        out << QStringLiteral("[可调度] U 超过 RMS 上界：按固定优先级调度可能丢周期。"
                              "建议：加大任务周期 / 降低 WCET / 减少周期任务数（三者择一，改完重算）");
    else
        out << QStringLiteral("[可调度] U 未超上界，按固定优先级（RMS）调度可调度");

    /* 最坏响应时间：Ri = Ci + Σ_{j 优先级更高} ceil(Ri/Tj)·Cj，迭代到收敛 */
    for (const TaskSpec *t : per) {
        const double Ci = double(t->wcetUs);
        const double Ti = double(t->periodMs) * 1000.0;
        double R = Ci;
        bool blew = false;
        bool conv = false;
        for (int k = 0; k < 64 && !blew && !conv; ++k) {
            double next = Ci;
            for (const TaskSpec *j : per) {
                if (j == t || j->prio <= t->prio)
                    continue;
                next += std::ceil(R / (double(j->periodMs) * 1000.0)) * double(j->wcetUs);
            }
            if (next > Ti) {
                R = next;
                blew = true;
            } else if (std::fabs(next - R) < 0.5) {
                R = next;
                conv = true;
            } else {
                R = next;
            }
        }
        if (blew)
            out << QStringLiteral("[可调度] 任务 %1 的最坏响应时间 %2 ms 已超过其周期 %3 ms："
                                  "更低优先级任务的执行时间被它算漏了，实际会丢周期——"
                                  "应提高它的优先级，或压掉抢占它的高优先级任务的 WCET")
                       .arg(t->name)
                       .arg(R / 1000.0, 0, 'f', 1)
                       .arg(t->periodMs);
        else if (!conv)
            out << QStringLiteral("[可调度] 任务 %1 的响应时间迭代不收敛（用时太长），"
                                  "说明负载已过重，会丢周期").arg(t->name);
    }

    if (missing > 0)
        out << QStringLiteral("[可调度] 另有 %1 个周期任务未填 WCET(µs)，未纳入上面的分析"
                              "（填上最坏执行时间才算得准）").arg(missing);
    return out;
}

QStringList validate(const FwConfig &cfg, const QVector<TaskSpec> &tasks,
                     const QVector<ResSpec> &res, const QStringList &flowPicks)
{
    QStringList out;
    const int maxPrio = cfg.ints.value(QStringLiteral("configMAX_PRIORITIES"), 0);
    const int minStack = cfg.ints.value(QStringLiteral("configMINIMAL_STACK_SIZE"), 0);
    const int heap = cfg.ints.value(QStringLiteral("configTOTAL_HEAP_SIZE"), 0);
    const int useMutex = cfg.ints.value(QStringLiteral("configUSE_MUTEXES"), 0);
    const int useRec = cfg.ints.value(QStringLiteral("configUSE_RECURSIVE_MUTEXES"), 0);
    const int useCnt = cfg.ints.value(QStringLiteral("configUSE_COUNTING_SEMAPHORES"), 0);
    const int useTimer = cfg.ints.value(QStringLiteral("configUSE_TIMERS"), 0);

    int stackSum = 0;
    for (const TaskSpec &t : tasks) {
        if (maxPrio > 0 && (t.prio < 0 || t.prio >= maxPrio))
            out << QStringLiteral("[优先级] 任务 %1 优先级 %2 超出范围 0~%3（configMAX_PRIORITIES=%4）")
                   .arg(t.name).arg(t.prio).arg(maxPrio - 1).arg(maxPrio);
        if (minStack > 0 && t.stackWords < minStack)
            out << QStringLiteral("[栈] 任务 %1 栈 %2 字 < configMINIMAL_STACK_SIZE %3，易栈溢出")
                   .arg(t.name).arg(t.stackWords).arg(minStack);
        stackSum += t.stackWords;
        if (t.kind == QStringLiteral("周期") && t.periodMs <= 0)
            out << QStringLiteral("[周期] 任务 %1 的周期应大于 0").arg(t.name);
    }
    if (heap > 0 && stackSum * 4 > heap)
        out << QStringLiteral("[堆] 任务栈合计约 %1 字节，已超过 configTOTAL_HEAP_SIZE %2 字节")
               .arg(stackSum * 4).arg(heap);
    else if (heap > 0 && stackSum * 4 > heap * 7 / 10)
        out << QStringLiteral("[堆] 任务栈合计约 %1 字节，占 configTOTAL_HEAP_SIZE %2 字节的七成以上，"
                              "再加队列/信号量可能不足")
               .arg(stackSum * 4).arg(heap);

    bool needMutex = false, needRec = false, needCnt = false, needTimer = false;
    for (const ResSpec &r : res) {
        if (r.kind == QStringLiteral("mutex"))
            needMutex = true;
        else if (r.kind == QStringLiteral("recursive"))
            needRec = true;
        else if (r.kind == QStringLiteral("counting"))
            needCnt = true;
        else if (r.kind == QStringLiteral("timer"))
            needTimer = true;
    }
    if (needMutex && useMutex == 0)
        out << QStringLiteral("[配置] 用了互斥量，但 configUSE_MUTEXES = 0，需在 FreeRTOSConfig.h 置 1");
    if (needRec && useRec == 0)
        out << QStringLiteral("[配置] 用了递归互斥量，但 configUSE_RECURSIVE_MUTEXES = 0，需置 1");
    if (needCnt && useCnt == 0)
        out << QStringLiteral("[配置] 用了计数信号量，但 configUSE_COUNTING_SEMAPHORES = 0，需置 1");
    if (needTimer && useTimer == 0)
        out << QStringLiteral("[配置] 用了软定时器，但 configUSE_TIMERS = 0，需置 1");
    if (!cfg.hasAssert)
        out << QStringLiteral("[配置] FreeRTOSConfig.h 未定义 configASSERT，优先级违规不会被当场发现，"
                              "建议保留");

    /* 任务关联的资源必须在资源表里声明过，否则生成出来会引用不存在的名字 */
    QStringList resNames;
    for (const ResSpec &r : res)
        resNames << r.name;
    for (const TaskSpec &t : tasks)
        for (const QString &ln : t.links)
            if (!resNames.contains(ln))
                out << QStringLiteral("[关联] 任务 %1 关联的资源 %2 未在资源表中声明")
                       .arg(t.name, ln);

    /* 生成前拦截①：勾选的流程卡之间会不会打架（同一 IRQn / 同一脚位 / 抢占值不一致）。
       卡片的代码是可搬用的真代码，冲突必须在"生成之前"讲清楚，别等用户编完才发现。 */
    out += flowConflicts(flowPicks);

    /* 生成前拦截②：周期任务可调度性（只给带解释的建议，不改任务） */
    out += scheduleAnalysis(tasks);
    return out;
}

static QString taskFuncName(const QString &name)
{
    return QStringLiteral("vTask") + name;
}

static QString upperIdent(const QString &name)
{
    QString s = name;
    s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    return s.toUpper();
}

/* ---------------- 补全素材：都来自「单片机手册」的 dataset.json ---------------- */

/* 符号级补全：把速查里查到的函数原型与用途，做成一撮注释，贴在任务桩里。
   查不到就明说查不到（不要静默留白，否则用户以为功能没生效）。 */
static QStringList apiHintLines(const QString &hint, const QString &indent)
{
    QStringList L;
    QString err;
    if (!LookupData::load(&err)) {
        /* 数据读不到要明说，别报成"没查到"——那是两回事 */
        L << QStringLiteral("%1/* 速查提示[%2]：读不到「单片机手册」数据，未取原型").arg(indent, hint);
        L << QStringLiteral("%1   %2 */").arg(indent, err);
        return L;
    }
    const QVector<LookupData::FuncHit> hits = LookupData::findFuncs(hint, 6);
    L << QStringLiteral("%1/* 速查提示[%2]：单片机手册里可查到的相关函数").arg(indent, hint);
    if (hits.isEmpty()) {
        L << QStringLiteral("%1   —— 没查到（换个关键词，或在手册页里搜一下） */").arg(indent);
        return L;
    }
    for (const LookupData::FuncHit &h : hits) {
        L << QStringLiteral("%1   %2").arg(indent, h.proto);
        if (!h.desc.isEmpty())
            L << QStringLiteral("%1     %2").arg(indent, h.desc);
    }
    L << QStringLiteral("%1*/").arg(indent);
    return L;
}

/* 代码段级补全：把勾选的流程卡整段代码拼成一块"参考代码"。
   排序按卡片自带的圈码序号（数据作者已经按 时钟→引脚→外设→NVIC 排过），
   每块内部原样保留（它的注释就是「1. 开时钟 / 2. 引脚复用 / …」）。
   已去掉 #include 与其中的 main()（39 张流程卡里 16 张含 main）。 */
static QString flowCodeSection(const QStringList &picks, QStringList *initCalls,
                               QStringList *needIncludes)
{
    if (picks.isEmpty())
        return QString();
    QString err;
    if (!LookupData::load(&err)) {
        /* 勾了卡却取不到数据 → 明写一条，别让用户以为勾选没生效 */
        return QStringLiteral("/* 参考代码段：读不到「单片机手册」数据，勾选的卡未并入\n   %1 */")
            .arg(err);
    }

    /* 排序：按圈码，同码按名字，保证结果稳定 */
    QStringList sorted = picks;
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
        const int ra = LookupData::flowRank(a), rb = LookupData::flowRank(b);
        return ra != rb ? ra < rb : QString::localeAwareCompare(a, b) < 0;
    });

    QStringList L;
    L << QStringLiteral("/* =============== 外设初始化参考代码"
                        "（整段取自「单片机手册」流程卡） ===============");
    L << QStringLiteral("   每块都是流程卡里可直接搬用的代码，已去掉 #include 与其中的 main()；");
    L << QStringLiteral("   请按需裁剪，并在 main() 里调用其中的初始化函数。 */");

    int got = 0;
    for (const QString &name : sorted) {
        const LookupData::FlowHit h = LookupData::flowByName(name);
        if (h.code.trimmed().isEmpty())
            continue;
        QStringList fns, incs;
        const QString body = LookupData::extractReusable(h.code, &fns, &incs);
        if (body.trimmed().isEmpty())
            continue;
        ++got;
        L << QString();
        L << QStringLiteral("/* ---- %1 ---- */").arg(name);
        for (const QString &i : incs)
            if (needIncludes && !needIncludes->contains(i))
                needIncludes->append(i);
        L << body;
        if (initCalls) {
            for (const QString &f : fns) {
                /* 只挑初始化类函数作为"记得调用"的提示，避免把中断服务函数也列进来 */
                if ((f.contains(QStringLiteral("_Init")) || f.contains(QStringLiteral("_init"))
                     || f.endsWith(QStringLiteral("Init")) || f.endsWith(QStringLiteral("init")))
                    && !initCalls->contains(f))
                    initCalls->append(f);
            }
        }
    }
    if (got == 0)
        return QString();
    return L.join(QLatin1Char('\n'));
}

QString render(const FwConfig &cfg, const QVector<TaskSpec> &tasks,
               const QVector<ResSpec> &res, const GenOptions &opt,
               const QString &projectName)
{
    const QString proj = projectName.trimmed().isEmpty() ? QStringLiteral("未命名工程")
                                                         : projectName.trimmed();
    const QString base = opt.baseName.trimmed().isEmpty() ? QStringLiteral("app_tasks")
                                                          : opt.baseName.trimmed();
    /* 补全素材先算好：流程卡的整段代码要插在任务函数之前（main 会调用它们，
       定义必须在用之前），它带的头文件也要在 include 区提示出来。 */
    QStringList initCalls, refIncludes;
    const QString refSection = flowCodeSection(opt.flowPicks, &initCalls, &refIncludes);
    /* 需要哪些头 */
    bool needQueue = false, needSem = false, needTimer = false;
    for (const ResSpec &r : res) {
        if (r.kind == QStringLiteral("queue"))
            needQueue = true;
        else if (r.kind == QStringLiteral("timer"))
            needTimer = true;
        else
            needSem = true;
    }

    QStringList L;
    L << QStringLiteral("/* ================================================================");
    L << QStringLiteral(" *  %1 —— main.c 骨架").arg(proj);
    L << QStringLiteral(" *  由 嵌入式工具箱 · RTOS 骨架生成 生成，仅供参考的起点，不是成品。");
    L << QStringLiteral(" *  配置来源：%1").arg(cfg.path);
    L << QStringLiteral(" *  任务函数体、外设初始化处为 TODO，按注释补实现后再编译。");
    L << QStringLiteral(" * ================================================================ */");
    L << QString();
    L << QStringLiteral("#include \"FreeRTOS.h\"");
    L << QStringLiteral("#include \"task.h\"");
    if (needQueue)
        L << QStringLiteral("#include \"queue.h\"");
    if (needSem)
        L << QStringLiteral("#include \"semphr.h\"");
    if (needTimer)
        L << QStringLiteral("#include \"timers.h\"");
    if (!opt.includes.trimmed().isEmpty()) {
        for (const QString &s : opt.includes.split(QLatin1Char('\n'))) {
            const QString t = s.trimmed();
            if (t.isEmpty())
                continue;
            L << (t.startsWith(QLatin1Char('#')) ? t : QStringLiteral("#include \"%1\"").arg(t));
        }
    } else {
        L << QStringLiteral("/* TODO: 补板级头文件（时钟 / 串口 / 外设驱动等） */");
    }
    if (opt.splitHeader)
        L << QStringLiteral("#include \"%1.h\"").arg(base);
    if (!refIncludes.isEmpty())
        L << QStringLiteral("/* 下方「外设初始化参考代码」用到的头文件（按需补进上面的 include 区）：%1 */")
                 .arg(refIncludes.join(QStringLiteral(", ")));
    L << QString();
    L << QStringLiteral("/* USER CODE BEGIN Includes */");
    L << QStringLiteral("/* USER CODE END Includes */");
    L << QString();

    if (!opt.splitHeader) {
        L << QStringLiteral("/* ---------------- 任务参数 ---------------- */");
        for (const TaskSpec &t : tasks) {
            const QString u = upperIdent(t.name);
            L << QStringLiteral("#define APP_TASK_%1_PRIO      %2").arg(u).arg(t.prio);
            L << QStringLiteral("#define APP_TASK_%1_STACK     %2").arg(u).arg(t.stackWords);
            if (t.kind == QStringLiteral("周期"))
                L << QStringLiteral("#define APP_TASK_%1_PERIOD    %2U").arg(u).arg(t.periodMs);
            L << QString();
        }
        if (!res.isEmpty()) {
            L << QStringLiteral("/* ---------------- 句柄 ---------------- */");
            for (const ResSpec &r : res) {
                QString decl;
                if (r.kind == QStringLiteral("queue"))
                    decl = QStringLiteral("static QueueHandle_t %1 = NULL;").arg(r.name);
                else if (r.kind == QStringLiteral("timer"))
                    decl = QStringLiteral("static TimerHandle_t %1 = NULL;").arg(r.name);
                else
                    decl = QStringLiteral("static SemaphoreHandle_t %1 = NULL;").arg(r.name);
                if (!r.note.trimmed().isEmpty())
                    decl += QStringLiteral("   /* %1：%2 */").arg(kindText(r.kind), r.note.trimmed());
                L << decl;
            }
            L << QString();
        }
    } else if (!res.isEmpty()) {
        /* 分文件：句柄定义留在 .c，头文件里是 extern */
        L << QStringLiteral("/* ---------------- 句柄定义（头文件里已 extern） ---------------- */");
        for (const ResSpec &r : res) {
            QString decl;
            if (r.kind == QStringLiteral("queue"))
                decl = QStringLiteral("QueueHandle_t %1 = NULL;").arg(r.name);
            else if (r.kind == QStringLiteral("timer"))
                decl = QStringLiteral("TimerHandle_t %1 = NULL;").arg(r.name);
            else
                decl = QStringLiteral("SemaphoreHandle_t %1 = NULL;").arg(r.name);
            if (!r.note.trimmed().isEmpty())
                decl += QStringLiteral("   /* %1：%2 */").arg(kindText(r.kind), r.note.trimmed());
            L << decl;
        }
        L << QString();
    }

    if (!refSection.isEmpty()) {
        L << refSection;
        L << QString();
    }

    L << QStringLiteral("/* ---------------- 任务函数 ---------------- */");
    for (const TaskSpec &t : tasks) {
        const QString u = upperIdent(t.name);
        L << QStringLiteral("%1void %2(void *pvParameters)")
               .arg(opt.splitHeader ? QString() : QStringLiteral("static "), taskFuncName(t.name));
        L << QStringLiteral("{");
        L << QStringLiteral("    (void)pvParameters;");
        if (!t.duty.trimmed().isEmpty())
            L << QStringLiteral("    /* 职责：%1 */").arg(t.duty.trimmed());
        if (!t.links.isEmpty())
            L << QStringLiteral("    /* 关联资源：%1 */").arg(t.links.join(QStringLiteral("、")));
        if (!t.apiHint.trimmed().isEmpty()) {
            /* 符号级补全：把速查里查到的原型与用途贴在这里，省得来回切页面抄 */
            for (const QString &s : apiHintLines(t.apiHint.trimmed(), QStringLiteral("    ")))
                L << s;
        }
        if (t.kind == QStringLiteral("一次性")) {
            L << QStringLiteral("    /* TODO: 一次性工作 */");
            L << QStringLiteral("    /* USER CODE BEGIN %1 */").arg(u);
            L << QStringLiteral("    /* USER CODE END %1 */").arg(u);
            L << QStringLiteral("    vTaskDelete(NULL);");
        } else {
            if (t.kind == QStringLiteral("周期") && opt.useDelayUntil)
                L << QStringLiteral("    TickType_t xLastWake = xTaskGetTickCount();");
            L << QStringLiteral("    for (;;) {");
            L << QStringLiteral("        /* USER CODE BEGIN %1 */").arg(u);
            L << QStringLiteral("        /* USER CODE END %1 */").arg(u);
            if (t.kind == QStringLiteral("事件"))
                L << QStringLiteral("        /* TODO: 阻塞等待并处理（例如从 %1 取数据，"
                                    "portMAX_DELAY） */")
                       .arg(t.links.isEmpty() ? QStringLiteral("队列/信号量") : t.links.first());
            else
                L << QStringLiteral("        /* TODO: 处理一次 */");
            if (opt.feedWdg)
                L << QStringLiteral("        /* TODO: 喂狗报到位 SYS_WDG_Heartbeat(id) */");
            if (t.kind == QStringLiteral("周期")) {
                if (opt.useDelayUntil)
                    L << QStringLiteral("        vTaskDelayUntil(&xLastWake, "
                                        "pdMS_TO_TICKS(APP_TASK_%1_PERIOD));").arg(u);
                else
                    L << QStringLiteral("        vTaskDelay(pdMS_TO_TICKS(APP_TASK_%1_PERIOD));").arg(u);
            } else {
                L << QStringLiteral("        vTaskDelay(pdMS_TO_TICKS(10));   /* 事件任务的兜底节拍 */");
            }
            L << QStringLiteral("    }");
        }
        L << QStringLiteral("}");
        L << QString();
    }

    L << QStringLiteral("int %1(void)").arg(opt.funcName);
    L << QStringLiteral("{");
    L << QStringLiteral("    /* TODO: 时钟与板级外设初始化（时钟、串口、外设、看门狗等） */");
    if (!initCalls.isEmpty()) {
        L << QStringLiteral("    /* 下方「外设初始化参考代码」里的初始化函数请在此调用（按需裁剪）： */");
        for (const QString &f : initCalls)
            L << QStringLiteral("    /*   %1(); */").arg(f);
    }
    L << QStringLiteral("    /* USER CODE BEGIN Init */");
    L << QStringLiteral("    /* USER CODE END Init */");
    if (!res.isEmpty()) {
        L << QString();
        L << QStringLiteral("    /* 句柄先建后用：队列与信号量要在创建任务之前建立 */");
        for (const ResSpec &r : res) {
            if (r.kind == QStringLiteral("queue"))
                L << QStringLiteral("    %1 = xQueueCreate(%2, sizeof(%3));")
                       .arg(r.name).arg(r.param)
                       .arg(r.itemType.trimmed().isEmpty() ? QStringLiteral("uint8_t")
                                                           : r.itemType.trimmed());
            else if (r.kind == QStringLiteral("mutex"))
                L << QStringLiteral("    %1 = xSemaphoreCreateMutex();").arg(r.name);
            else if (r.kind == QStringLiteral("recursive"))
                L << QStringLiteral("    %1 = xSemaphoreCreateRecursiveMutex();").arg(r.name);
            else if (r.kind == QStringLiteral("counting"))
                L << QStringLiteral("    %1 = xSemaphoreCreateCounting(%2, 0);").arg(r.name).arg(r.param);
            else if (r.kind == QStringLiteral("binary"))
                L << QStringLiteral("    %1 = xSemaphoreCreateBinary();").arg(r.name);
            else if (r.kind == QStringLiteral("timer"))
                L << QStringLiteral("    %1 = xTimerCreate(\"%2\", pdMS_TO_TICKS(%3), pdTRUE, NULL, %4);")
                       .arg(r.name, r.name).arg(r.param)
                       .arg(r.itemType.trimmed().isEmpty() ? QStringLiteral("NULL")
                                                           : r.itemType.trimmed());
            L << QStringLiteral("    if (%1 == NULL) { /* TODO: 建失败，多半是 configTOTAL_HEAP_SIZE 不够 */ }")
                   .arg(r.name);
        }
    }
    L << QStringLiteral("    /* USER CODE BEGIN 2 */");
    L << QStringLiteral("    /* USER CODE END 2 */");
    L << QString();
    L << QStringLiteral("    /* 创建任务：函数 / 名字 / 栈深度(字) / 参数 / 优先级 / 句柄 */");
    for (const TaskSpec &t : tasks) {
        const QString u = upperIdent(t.name);
        L << QStringLiteral("    if (xTaskCreate(%1, \"%2\", APP_TASK_%3_STACK, NULL, "
                            "APP_TASK_%4_PRIO, NULL) != pdPASS) {")
               .arg(taskFuncName(t.name), t.name, u).arg(u);
        L << QStringLiteral("        /* TODO: 建任务失败，多半是堆不足 */");
        L << QStringLiteral("    }");
    }
    L << QString();
    L << QStringLiteral("    vTaskStartScheduler();");
    L << QString();
    L << QStringLiteral("    /* 走到这里说明堆不足，调度器没起来 */");
    L << QStringLiteral("    for (;;) { }");
    L << QStringLiteral("}");
    L << QString();

    return L.join(QLatin1Char('\n'));
}

QString renderHeader(const FwConfig &cfg, const QVector<TaskSpec> &tasks,
                     const QVector<ResSpec> &res, const GenOptions &opt,
                     const QString &projectName)
{
    const QString proj = projectName.trimmed().isEmpty() ? QStringLiteral("未命名工程")
                                                         : projectName.trimmed();
    const QString base = opt.baseName.trimmed().isEmpty() ? QStringLiteral("app_tasks")
                                                          : opt.baseName.trimmed();
    const QString guard = base.toUpper() + QStringLiteral("_H");

    bool needQueue = false, needSem = false, needTimer = false;
    for (const ResSpec &r : res) {
        if (r.kind == QStringLiteral("queue"))
            needQueue = true;
        else if (r.kind == QStringLiteral("timer"))
            needTimer = true;
        else
            needSem = true;
    }

    QStringList L;
    L << QStringLiteral("/* ================================================================");
    L << QStringLiteral(" *  %1 —— 任务与资源声明（%2.h）").arg(proj, base);
    L << QStringLiteral(" *  由 嵌入式工具箱 · RTOS 骨架生成 生成；配置来源：%1").arg(cfg.path);
    L << QStringLiteral(" * ================================================================ */");
    L << QString();
    L << QStringLiteral("#ifndef %1").arg(guard);
    L << QStringLiteral("#define %1").arg(guard);
    L << QString();
    L << QStringLiteral("#include \"FreeRTOS.h\"");
    L << QStringLiteral("#include \"task.h\"");
    if (needQueue)
        L << QStringLiteral("#include \"queue.h\"");
    if (needSem)
        L << QStringLiteral("#include \"semphr.h\"");
    if (needTimer)
        L << QStringLiteral("#include \"timers.h\"");
    L << QString();

    L << QStringLiteral("/* ---------------- 任务参数 ---------------- */");
    for (const TaskSpec &t : tasks) {
        const QString u = upperIdent(t.name);
        L << QStringLiteral("#define APP_TASK_%1_PRIO      %2").arg(u).arg(t.prio);
        L << QStringLiteral("#define APP_TASK_%1_STACK     %2").arg(u).arg(t.stackWords);
        if (t.kind == QStringLiteral("周期"))
            L << QStringLiteral("#define APP_TASK_%1_PERIOD    %2U").arg(u).arg(t.periodMs);
        L << QString();
    }

    if (!res.isEmpty()) {
        L << QStringLiteral("/* ---------------- 资源句柄（定义在 .c） ---------------- */");
        for (const ResSpec &r : res) {
            QString d;
            if (r.kind == QStringLiteral("queue"))
                d = QStringLiteral("extern QueueHandle_t %1;").arg(r.name);
            else if (r.kind == QStringLiteral("timer"))
                d = QStringLiteral("extern TimerHandle_t %1;").arg(r.name);
            else
                d = QStringLiteral("extern SemaphoreHandle_t %1;").arg(r.name);
            if (!r.note.trimmed().isEmpty())
                d += QStringLiteral("   /* %1：%2 */").arg(kindText(r.kind), r.note.trimmed());
            L << d;
        }
        L << QString();
    }

    L << QStringLiteral("/* ---------------- 任务函数原型 ---------------- */");
    for (const TaskSpec &t : tasks)
        L << QStringLiteral("void %1(void *pvParameters);").arg(taskFuncName(t.name));
    L << QString();
    L << QStringLiteral("#endif /* %1 */").arg(guard);
    L << QString();

    return L.join(QLatin1Char('\n'));
}

QString configSnippet(const FwConfig &cfg, const QVector<TaskSpec> &tasks,
                      const QVector<ResSpec> &res)
{
    const int maxPrio = cfg.ints.value(QStringLiteral("configMAX_PRIORITIES"), 0);
    const int minStack = cfg.ints.value(QStringLiteral("configMINIMAL_STACK_SIZE"), 0);
    const int heap = cfg.ints.value(QStringLiteral("configTOTAL_HEAP_SIZE"), 0);

    int usedPrio = -1;
    int minTaskStack = 0;
    bool firstStack = true;
    int stackSum = 0;
    for (const TaskSpec &t : tasks) {
        usedPrio = qMax(usedPrio, t.prio);
        if (firstStack || t.stackWords < minTaskStack) {
            minTaskStack = t.stackWords;
            firstStack = false;
        }
        stackSum += t.stackWords;
    }

    QStringList L;
    if (maxPrio > 0 && usedPrio >= 0 && usedPrio >= maxPrio)
        L << QStringLiteral("#define configMAX_PRIORITIES        (%1)   /* > 最高任务优先级 %2 */")
                 .arg(usedPrio + 1).arg(usedPrio);
    if (minStack > 0 && !firstStack && minTaskStack < minStack)
        L << QStringLiteral("#define configMINIMAL_STACK_SIZE    %1   /* 空闲任务栈(字)；不高于最小任务栈 %2 */")
                 .arg(minTaskStack).arg(minTaskStack);
    if (heap > 0 && stackSum * 4 > heap * 7 / 10)
        L << QStringLiteral("#define configTOTAL_HEAP_SIZE       ((size_t)(%1))   /* ≥ 任务栈合计 %2 字节 + 队列/信号量开销 */")
                 .arg(stackSum * 4 + 2048).arg(stackSum * 4);

    bool needMutex = false, needRec = false, needCnt = false, needTimer = false;
    for (const ResSpec &r : res) {
        if (r.kind == QStringLiteral("mutex"))
            needMutex = true;
        else if (r.kind == QStringLiteral("recursive"))
            needRec = true;
        else if (r.kind == QStringLiteral("counting"))
            needCnt = true;
        else if (r.kind == QStringLiteral("timer"))
            needTimer = true;
    }
    if (needMutex && cfg.ints.value(QStringLiteral("configUSE_MUTEXES"), 0) == 0)
        L << QStringLiteral("#define configUSE_MUTEXES            1");
    if (needRec && cfg.ints.value(QStringLiteral("configUSE_RECURSIVE_MUTEXES"), 0) == 0)
        L << QStringLiteral("#define configUSE_RECURSIVE_MUTEXES  1");
    if (needCnt && cfg.ints.value(QStringLiteral("configUSE_COUNTING_SEMAPHORES"), 0) == 0)
        L << QStringLiteral("#define configUSE_COUNTING_SEMAPHORES 1");
    if (needTimer && cfg.ints.value(QStringLiteral("configUSE_TIMERS"), 0) == 0)
        L << QStringLiteral("#define configUSE_TIMERS             1");
    if (!cfg.hasAssert)
        L << QStringLiteral("#define configASSERT(x)  if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;); }");

    if (L.isEmpty())
        return QString();

    QStringList out;
    out << QStringLiteral("/* FreeRTOSConfig.h 建议片段（按当前任务/资源清单得出，按需并入你的配置） */");
    out += L;
    out << QString();
    return out.join(QLatin1Char('\n'));
}

QString mergeUserCode(const QString &generated, const QString &existing)
{
    if (existing.trimmed().isEmpty())
        return generated;

    static const QRegularExpression re(QStringLiteral(
        "/\\* USER CODE BEGIN (\\w+) \\*/([\\s\\S]*?)/\\* USER CODE END \\1 \\*/"));

    QHash<QString, QVector<QString>> olds;
    {
        auto it = re.globalMatch(existing);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            olds[m.captured(1)] << m.captured(2);
        }
    }
    if (olds.isEmpty())
        return generated;

    QString out;
    qsizetype pos = 0;
    QHash<QString, int> seen;
    auto it = re.globalMatch(generated);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += generated.mid(pos, m.capturedStart() - pos);
        const QString name = m.captured(1);
        const int n = seen[name]++;
        QString body = m.captured(2);
        if (olds.contains(name) && n < olds.value(name).size())
            body = olds.value(name).at(n);
        out += QStringLiteral("/* USER CODE BEGIN %1 */").arg(name) + body
             + QStringLiteral("/* USER CODE END %1 */").arg(name);
        pos = m.capturedEnd();
    }
    out += generated.mid(pos);
    return out;
}

QString withLineEndings(const QString &text, bool crlf)
{
    if (!crlf)
        return text;
    QString t = text;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    t.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    return t;
}

QVector<ScenarioSpec> scenarios()
{
    QVector<ScenarioSpec> out;

    {
        ScenarioSpec s;
        s.name = QStringLiteral("采集端（3 任务）");
        s.desc = QStringLiteral("板1：采集 → 处理 → 上报（LoRa）");
        s.tasks = sampleTasks();
        s.res = sampleResources();
        out << s;
    }
    {
        ScenarioSpec s;
        s.name = QStringLiteral("网关端（5 任务）");
        s.desc = QStringLiteral("板2：收帧 → 转发 → 命令 → 补传 → 监控");
        s.tasks << TaskSpec{QStringLiteral("LoraRx"), QStringLiteral("周期"), 20, 3, 512,
                            QStringLiteral("收 LoRa 帧，校验后入队"), {QStringLiteral("s_q_frame")}, {}, 0};
        s.tasks << TaskSpec{QStringLiteral("Forward"), QStringLiteral("事件"), 0, 4, 512,
                            QStringLiteral("取帧写 Flash 缓存并经上位机串口转发"),
                            {QStringLiteral("s_q_frame"), QStringLiteral("s_qt_tx_mtx")}, {}, 0};
        s.tasks << TaskSpec{QStringLiteral("CmdRx"), QStringLiteral("周期"), 20, 3, 512,
                            QStringLiteral("收上位机命令，执行并应答"), {QStringLiteral("s_qt_tx_mtx")}, {}, 0};
        s.tasks << TaskSpec{QStringLiteral("Replay"), QStringLiteral("周期"), 50, 3, 256,
                            QStringLiteral("从最老记录补传历史帧，回报进度"), {}, {}, 0};
        s.tasks << TaskSpec{QStringLiteral("Monitor"), QStringLiteral("周期"), 1000, 5, 256,
                            QStringLiteral("打印统计、喂狗"), {}, {}, 0};
        s.res << ResSpec{QStringLiteral("queue"), QStringLiteral("s_q_frame"),
                         QStringLiteral("uint8_t"), 4, QStringLiteral("收帧到转发")};
        s.res << ResSpec{QStringLiteral("mutex"), QStringLiteral("s_qt_tx_mtx"),
                         QString(), 0, QStringLiteral("保护上位机串口发送")};
        out << s;
    }
    {
        ScenarioSpec s;
        s.name = QStringLiteral("最小入门（2 任务）");
        s.desc = QStringLiteral("一个周期任务 + 一个事件任务，练手用");
        s.tasks << TaskSpec{QStringLiteral("Sample"), QStringLiteral("周期"), 1000, 2, 256,
                            QStringLiteral("周期采样"), {QStringLiteral("s_q_data")}, {}, 0};
        s.tasks << TaskSpec{QStringLiteral("Handle"), QStringLiteral("事件"), 0, 2, 256,
                            QStringLiteral("阻塞取数据并处理"), {QStringLiteral("s_q_data")}, {}, 0};
        s.res << ResSpec{QStringLiteral("queue"), QStringLiteral("s_q_data"),
                         QStringLiteral("uint8_t"), 4, QStringLiteral("采样到处理")};
        out << s;
    }
    return out;
}

QVector<TaskSpec> parseTaskLines(const QString &text)
{
    QVector<TaskSpec> v;
    const QRegularExpression sep(QStringLiteral("[/|｜,，\\t]+"));
    const QRegularExpression lead(QStringLiteral("^\\s*\\d+\\s*[\\.、\\)]\\s*"));
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QStringLiteral("//")))
            continue;

        QStringList p = line.split(sep, Qt::SkipEmptyParts);
        if (p.isEmpty())
            continue;
        for (QString &s : p)
            s = s.trimmed();

        TaskSpec t;
        QString name = p[0];
        name.remove(lead);
        name.remove(QRegularExpression(QStringLiteral("^vTask")));
        t.name = name.trimmed();
        if (t.name.isEmpty())
            continue;

        const int period = (p.size() > 1) ? p[1].toInt() : 0;
        t.periodMs = (period > 0) ? period : 0;
        t.kind = (period > 0) ? QStringLiteral("周期") : QStringLiteral("事件");
        t.prio = (p.size() > 2) ? p[2].toInt() : 1;
        t.stackWords = (p.size() > 3) ? p[3].toInt() : 256;
        if (t.prio <= 0)
            t.prio = 1;
        if (t.stackWords <= 0)
            t.stackWords = 256;
        if (p.size() > 4) {
            QStringList duty;
            for (int i = 4; i < p.size(); ++i)
                duty << p[i];
            t.duty = duty.join(QStringLiteral(" "));
        }
        v << t;
    }
    return v;
}

QVector<TaskSpec> sampleTasks()
{
    QVector<TaskSpec> v;
    v << TaskSpec{QStringLiteral("Sensor"),  QStringLiteral("周期"), 2000, 3, 512,
                  QStringLiteral("读温湿度，写采集队列"), {QStringLiteral("s_q_sensor")}, {}, 0};
    v << TaskSpec{QStringLiteral("Process"), QStringLiteral("事件"), 0, 3, 512,
                  QStringLiteral("从采集队列取数据，组帧，写发送队列"),
                  {QStringLiteral("s_q_sensor"), QStringLiteral("s_q_frame")}, {}, 0};
    v << TaskSpec{QStringLiteral("Comm"),    QStringLiteral("事件"), 0, 4, 512,
                  QStringLiteral("从发送队列取帧，经 LoRa 发出"), {QStringLiteral("s_q_frame")}, {}, 0};
    return v;
}

QVector<ResSpec> sampleResources()
{
    QVector<ResSpec> v;
    v << ResSpec{QStringLiteral("queue"), QStringLiteral("s_q_sensor"),
                 QStringLiteral("SensorMsg_t"), 4, QStringLiteral("采集到处理")};
    v << ResSpec{QStringLiteral("queue"), QStringLiteral("s_q_frame"),
                 QStringLiteral("uint8_t"), 4, QStringLiteral("处理到发送（每项一帧）")};
    return v;
}

QVector<ResSpec> chainTasks(QVector<TaskSpec> &tasks)
{
    QVector<ResSpec> created;
    const int n = tasks.size();
    if (n < 2)
        return created;
    for (int i = 0; i + 1 < n; ++i) {
        const QString an = tasks.at(i).name.trimmed();
        const QString bn = tasks.at(i + 1).name.trimmed();
        if (an.isEmpty() || bn.isEmpty())
            continue;
        const QString qname = QStringLiteral("s_q_%1_to_%2").arg(an.toLower(), bn.toLower());
        ResSpec q;
        q.kind = QStringLiteral("queue");
        q.name = qname;
        q.itemType = QStringLiteral("uint8_t");   /* 占位，按实际数据项改 */
        q.param = 4;
        q.note = QStringLiteral("%1 → %2").arg(an, bn);
        created << q;
        if (!tasks[i].links.contains(qname))
            tasks[i].links << qname;
        if (!tasks[i + 1].links.contains(qname))
            tasks[i + 1].links << qname;
    }
    return created;
}

int selftest(QStringList &log, const QString &envMonitorRoot)
{
    int fail = 0;
    auto expect = [&](bool cond, const QString &what) {
        log << QStringLiteral("%1 %2").arg(cond ? QStringLiteral("PASS") : QStringLiteral("FAIL"), what);
        if (!cond)
            ++fail;
    };

    /* 找一块真实配置 */
    QString cfgPath;
    QStringList cands;
    if (!envMonitorRoot.isEmpty()) {
        cands << QDir(envMonitorRoot).filePath(
            QStringLiteral("EnvMonitor/Project_1_检测端/Project/FreeRTOS/port/FreeRTOSConfig.h"));
        cands << QDir(envMonitorRoot).filePath(
            QStringLiteral("EnvMonitor/Project_2_数据通信端/Project/FreeRTOS/port/FreeRTOSConfig.h"));
    }
    for (const QString &c : cands) {
        if (QFile::exists(c)) {
            cfgPath = c;
            break;
        }
    }
    if (cfgPath.isEmpty()) {
        log << QStringLiteral("SKIP 未找到真实 FreeRTOSConfig.h，跳过解析检查");
    } else {
        const FwConfig cfg = parseConfig(cfgPath);
        expect(cfg.ok, QStringLiteral("解析真实配置：得到 configMAX_PRIORITIES / configMINIMAL_STACK_SIZE"));
        expect(cfg.ints.value(QStringLiteral("configMAX_PRIORITIES")) == 8,
               QStringLiteral("configMAX_PRIORITIES 解析为 8"));
        expect(cfg.ints.value(QStringLiteral("configMINIMAL_STACK_SIZE")) == 128,
               QStringLiteral("configMINIMAL_STACK_SIZE 解析为 128"));
        expect(cfg.ints.value(QStringLiteral("configTOTAL_HEAP_SIZE")) == 30 * 1024,
               QStringLiteral("configTOTAL_HEAP_SIZE 解析为 30720"));
        expect(cfg.hasAssert, QStringLiteral("识别到 configASSERT"));
    }

    /* 校验能命中 */
    FwConfig c2;
    c2.ints.insert(QStringLiteral("configMAX_PRIORITIES"), 8);
    c2.ints.insert(QStringLiteral("configMINIMAL_STACK_SIZE"), 128);
    c2.ints.insert(QStringLiteral("configTOTAL_HEAP_SIZE"), 30720);
    c2.ints.insert(QStringLiteral("configUSE_MUTEXES"), 0);
    QVector<TaskSpec> bad;
    bad << TaskSpec{QStringLiteral("Bad"), QStringLiteral("周期"), 1000, 99, 32, QString(), {}, {}, 0};
    QVector<ResSpec> badres;
    badres << ResSpec{QStringLiteral("mutex"), QStringLiteral("m"), QString(), 0, QString()};
    const QStringList issues = validate(c2, bad, badres);
    expect(issues.filter(QStringLiteral("优先级")).size() > 0, QStringLiteral("校验命中：优先级越界"));
    expect(issues.filter(QStringLiteral("栈")).size() > 0, QStringLiteral("校验命中：栈过小"));
    expect(issues.filter(QStringLiteral("互斥量")).size() > 0, QStringLiteral("校验命中：用了互斥量但未开"));

    /* 渲染关键片段齐全（单文件模式：宏与句柄都落在 .c 里） */
    GenOptions single;
    single.splitHeader = false;
    const QString out = render(c2, sampleTasks(), sampleResources(), single,
                               QStringLiteral("自检工程"));
    expect(out.contains(QStringLiteral("xTaskCreate(vTaskSensor")), QStringLiteral("渲染含任务创建"));
    expect(out.contains(QStringLiteral("vTaskStartScheduler")), QStringLiteral("渲染含调度器启动"));
    expect(out.contains(QStringLiteral("xQueueCreate")), QStringLiteral("渲染含队列创建"));
    expect(out.contains(QStringLiteral("#define APP_TASK_SENSOR_PRIO")), QStringLiteral("渲染含任务参数宏"));
    expect(out.contains(QStringLiteral("关联资源：")), QStringLiteral("渲染含任务与资源的关联"));

    /* 串联资源：3 个任务应生成 2 条队列并双向写进关联 */
    QVector<TaskSpec> chained = sampleTasks();
    const QVector<ResSpec> made = chainTasks(chained);
    bool chainOk = (made.size() == 2) && chained[0].links.size() >= 2 && chained[2].links.size() >= 2;
    expect(chainOk, QStringLiteral("按任务串联：生成 2 条队列且首尾任务都关联上"));
    QVector<ResSpec> allRes = sampleResources();
    allRes += made;
    expect(validate(FwConfig{}, chained, allRes).filter(QStringLiteral("关联")).isEmpty(),
           QStringLiteral("串联后关联资源都有声明，校验无「关联」告警"));

    /* 分文件：头文件含 extern 与原型，.c 含 include 与建任务返回值检查 */
    GenOptions go;
    go.splitHeader = true;
    go.baseName = QStringLiteral("app_tasks");
    const QString hdr = renderHeader(FwConfig{}, sampleTasks(), sampleResources(), go,
                                     QStringLiteral("自检"));
    expect(hdr.contains(QStringLiteral("extern QueueHandle_t s_q_sensor;")),
           QStringLiteral("头文件含句柄 extern"));
    expect(hdr.contains(QStringLiteral("void vTaskSensor(void *pvParameters);")),
           QStringLiteral("头文件含任务原型"));
    expect(hdr.contains(QStringLiteral("#ifndef APP_TASKS_H")), QStringLiteral("头文件含 include guard"));
    const QString src = render(FwConfig{}, sampleTasks(), sampleResources(), go, QStringLiteral("自检"));
    expect(src.contains(QStringLiteral("#include \"app_tasks.h\"")), QStringLiteral(".c 含头文件 include"));
    expect(src.contains(QStringLiteral("if (xTaskCreate(vTaskSensor")),
           QStringLiteral(".c 建任务带返回值检查"));
    expect(src.contains(QStringLiteral("USER CODE BEGIN Init")), QStringLiteral(".c 含 USER CODE 保护区"));

    /* USER CODE 合并：旧文件里的用户内容搬进新生成文本 */
    const QString oldFile = QStringLiteral(
        "/* USER CODE BEGIN Init */\n    /* 我的初始化 */\n/* USER CODE END Init */");
    expect(mergeUserCode(src, oldFile).contains(QStringLiteral("我的初始化")),
           QStringLiteral("重生成保留 USER CODE 内容"));

    /* 行尾选项 */
    expect(withLineEndings(QStringLiteral("a\nb"), true).contains(QStringLiteral("\r\n")),
           QStringLiteral("CRLF 选项生效"));

    /* 配置建议：优先级越界与堆不足时给出改法 */
    FwConfig c3;
    c3.ints.insert(QStringLiteral("configMAX_PRIORITIES"), 4);
    c3.ints.insert(QStringLiteral("configTOTAL_HEAP_SIZE"), 1024);
    QVector<TaskSpec> big;
    big << TaskSpec{QStringLiteral("Big"), QStringLiteral("周期"), 1000, 9, 4096, QString(), {}, {}, 0};
    const QString sug = configSnippet(c3, big, {});
    expect(sug.contains(QStringLiteral("configMAX_PRIORITIES"))
               && sug.contains(QStringLiteral("configTOTAL_HEAP_SIZE")),
           QStringLiteral("配置建议含优先级与堆两条"));

    /* 解析任务行（起手助手导入用） */
    const QString taskText = QStringLiteral(
        "Sensor / 2000 / 3 / 512 / 读温湿度\n"
        "Process / 0 / 3 / 512 / 组帧发队列\n"
        "2、Comm / 0 / 4 / 256 / LoRa 发送\n");
    const QVector<TaskSpec> pt = parseTaskLines(taskText);
    expect(pt.size() == 3, QStringLiteral("解析任务行：得到 3 条"));
    expect(pt[0].kind == QStringLiteral("周期") && pt[0].periodMs == 2000
               && pt[0].prio == 3 && pt[0].stackWords == 512
               && pt[0].duty.contains(QStringLiteral("温湿度")),
           QStringLiteral("解析任务行：首条字段正确"));
    expect(pt[1].kind == QStringLiteral("事件") && pt[1].periodMs == 0,
           QStringLiteral("解析任务行：周期 0 判为事件"));
    expect(pt[2].name == QStringLiteral("Comm"), QStringLiteral("解析任务行：去掉序号前缀"));

    /* ---------------- 补全素材（取自「单片机手册」dataset.json） ----------------
       两条都要同时验"数据找得到"和"生成物真的含预期片段"：
       只断言数据装了，数据一挪窝功能就静默失效；只断言生成物含片段，又测不到数据来源。 */
    {
        QString lerr;
        const bool lok = LookupData::load(&lerr);
        expect(lok, QStringLiteral("速查数据可定位并装载（%1）")
                        .arg(lok ? LookupData::path() : lerr));
        expect(LookupData::size() > 900,
               QStringLiteral("手册条目数 > 900（实际 %1）").arg(LookupData::size()));

        /* 符号级：按前缀查 xQueueCreate，要回"带原型"的条目 */
        const QVector<LookupData::FuncHit> fh =
            LookupData::findFuncs(QStringLiteral("xQueueCreate"), 5);
        expect(!fh.isEmpty() && fh.first().name == QStringLiteral("xQueueCreate"),
               QStringLiteral("符号级：查 xQueueCreate 得到原型（%1）")
                   .arg(fh.isEmpty() ? QStringLiteral("没查到") : fh.first().proto));

        /* 界面那个可勾选清单走的就是这条路径：空查询 → 按 _ord 给出全部流程卡 */
        const QVector<LookupData::FlowHit> allFlows = LookupData::findFlows(QString(), 64);
        expect(allFlows.size() == 39,
               QStringLiteral("流程卡清单：空查询给出 %1 张（期望 39）").arg(allFlows.size()));

        /* 代码段级：① 串口卡原文含 main()，剥离后必须既没有 main、又保住初始化函数 */
        const LookupData::FlowHit c1 =
            LookupData::flowByName(QStringLiteral("① 串口 USART + DMA + 空闲中断接收（不定长帧）"));
        expect(!c1.code.isEmpty(), QStringLiteral("代码段级：流程卡可按名取到"));
        expect(c1.code.contains(QStringLiteral("int main(")),
               QStringLiteral("代码段级：该卡原文含 main（这正是要剥掉的东西）"));
        QStringList fns, incs;
        const QString body = LookupData::extractReusable(c1.code, &fns, &incs);
        expect(!body.contains(QStringLiteral("int main(")), QStringLiteral("代码段级：剥离后不含 main()"));
        expect(body.contains(QStringLiteral("_Init(")),
               QStringLiteral("代码段级：剥离后保住初始化函数"));
        expect(!incs.isEmpty(), QStringLiteral("代码段级：收集到 #include（%1）")
                                     .arg(incs.join(QStringLiteral(", "))));

        /* 拼进生成物：参考区在，且全程只有一个 main（拼接没把卡的 main 带进来） */
        GenOptions fo;
        fo.splitHeader = false;
        fo.flowPicks << QStringLiteral("① 串口 USART + DMA + 空闲中断接收（不定长帧）")
                     << QStringLiteral("⑤ 定时器输出 PWM（调光 / 调速 / 蜂鸣器）");
        const QString fout = render(FwConfig{}, sampleTasks(), sampleResources(), fo,
                                    QStringLiteral("自检"));
        const int mains = fout.count(QStringLiteral("int main("));
        expect(fout.contains(QStringLiteral("外设初始化参考代码")),
               QStringLiteral("生成物含外设初始化参考代码区"));
        expect(mains == 1, QStringLiteral("生成物只有一个 main（实际 %1 个）").arg(mains));
        expect(fout.contains(QStringLiteral("USART1_DMA_IDLE_Init")),
               QStringLiteral("生成物含选中的流程卡代码"));

        /* 符号级进生成物：任务带补全提示词时，桩里要出现提示与原型 */
        QVector<TaskSpec> ht = sampleTasks();
        ht[0].apiHint = QStringLiteral("xQueueCreate");
        GenOptions ho;
        ho.splitHeader = false;
        const QString hout = render(FwConfig{}, ht, sampleResources(), ho, QStringLiteral("自检"));
        expect(hout.contains(QStringLiteral("速查提示[xQueueCreate]"))
                   && hout.contains(QStringLiteral("xQueueCreate(")),
               QStringLiteral("符号级：任务桩里含速查提示与原型的片段"));
    }

    /* ---------------- 生成前拦截：跨流程卡冲突 ----------------
       真卡片之间未必冲突（也就测不到检测器），所以这里用合成"卡"验提取与比对本身：
       错例必须抓到，反例（不搭界的两张卡）必须不报。 */
    {
        const QString cardA = QStringLiteral(
            "GPIO_InitTypeDef GPIO_InitStructure;\n"
            "void USART1_IRQHandler(void) {\n"
            "    SYS_NVIC_SetPriority(USART1_IRQn, 5, 0);\n"
            "}\n"
            "GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;\n"
            "GPIO_Init(GPIOA, &GPIO_InitStructure);\n");
        const RtosGen::CardRefs ra = RtosGen::extractCardRefs(cardA);
        expect(ra.irqs.contains(QStringLiteral("USART1_IRQn")),
               QStringLiteral("跨卡提取：抓到中断向量"));
        expect(ra.irqPreempt.value(QStringLiteral("USART1_IRQn")) == 5,
               QStringLiteral("跨卡提取：抓到 SYS_NVIC_SetPriority 的抢占值字面量（5）"));
        expect(ra.pins.contains(QStringLiteral("PA9")) && ra.pins.contains(QStringLiteral("PA10")),
               QStringLiteral("跨卡提取：GPIO_Pin 赋值经 GPIO_Init 落到端口（PA9 / PA10）"));

        /* 反例：注释里的名字不算占用 */
        const RtosGen::CardRefs rc = RtosGen::extractCardRefs(QStringLiteral(
            "/* 注释里写着 TIM3_IRQn 与 GPIO_Pin_5，都不算占用 */\n"
            "/* NVIC_IRQChannelPreemptionPriority = 2 */\n"));
        expect(rc.irqs.isEmpty() && rc.pins.isEmpty(),
               QStringLiteral("跨卡提取反例：注释里的 IRQn / 脚位不当作占用"));

        /* 错例：两卡抢同一 IRQn（抢占值还不同）+ 同一脚位 → 三类都要报 */
        const RtosGen::CardRefs rb = RtosGen::extractCardRefs(QStringLiteral(
            "void USART1_IRQHandler(void) { SYS_NVIC_SetPriority(USART1_IRQn, 6, 0); }\n"
            "GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;\n"
            "GPIO_Init(GPIOA, &GPIO_InitStructure);\n"));
        QStringList cfNames;
        cfNames << QStringLiteral("卡A") << QStringLiteral("卡B");
        QVector<RtosGen::CardRefs> cfRefs;
        cfRefs << ra << rb;
        const QStringList cf = RtosGen::flowConflictsFromRefs(cfNames, cfRefs);
        const QString cfj = cf.join(QStringLiteral("\n"));
        expect(cfj.contains(QStringLiteral("USART1_IRQn")) && cfj.contains(QStringLiteral("卡A"))
                   && cfj.contains(QStringLiteral("卡B")),
               QStringLiteral("跨卡冲突：两卡抢同一中断向量要报出，并写明是哪两张卡"));
        expect(cfj.contains(QStringLiteral("抢占值不一致")),
               QStringLiteral("跨卡冲突：同一 IRQn 配了不同抢占值要报出（5 / 6）"));
        expect(cfj.contains(QStringLiteral("PA10")),
               QStringLiteral("跨卡冲突：两卡占同一脚位要报出（PA10）"));

        /* 反例：不搭界的两张卡不该报 */
        const RtosGen::CardRefs rd = RtosGen::extractCardRefs(QStringLiteral(
            "void TIM2_IRQHandler(void) { SYS_NVIC_SetPriority(TIM2_IRQn, 5, 0); }\n"
            "GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;\n"
            "GPIO_Init(GPIOB, &GPIO_InitStructure);\n"));
        QStringList okNames;
        okNames << QStringLiteral("卡A") << QStringLiteral("卡D");
        QVector<RtosGen::CardRefs> okRefs;
        okRefs << ra << rd;
        expect(RtosGen::flowConflictsFromRefs(okNames, okRefs).isEmpty(),
               QStringLiteral("跨卡冲突反例：不搭界的两张卡不报"));

        /* validate() 真的把勾选接上了（名字取自卡片清单，写错就查不到） */
        expect(RtosGen::validate(c2, sampleTasks(), sampleResources(),
                                 QStringList{QStringLiteral("速查里没有的卡")})
                   .filter(QStringLiteral("找不到")).size() > 0,
               QStringLiteral("生成前拦截：勾了速查里没有的卡名会报出"));
        expect(RtosGen::validate(c2, sampleTasks(), sampleResources(), QStringList())
                   .filter(QStringLiteral("参考卡")).isEmpty(),
               QStringLiteral("生成前拦截反例：没勾卡时不出现跨卡告警"));

        /* 共读的规则表（速查数据/rules）：卡片自己的 ISR 调任务态接口要拦下来 ——
           错例必抓、反例（ISR 里调 FromISR 版 / 任务态函数里调同名接口）必不报。 */
        const QStringList isrBad = RtosGen::cardIsrApiIssues(QStringLiteral("合成卡"), QStringLiteral(
            "void USART1_IRQHandler(void) {\n"
            "    xQueueSend(s_q, &x, 0);\n"
            "    taskENTER_CRITICAL();\n"
            "}\n"));
        expect(!isrBad.isEmpty() && isrBad.join(QStringLiteral("\n")).contains(QStringLiteral("USART1_IRQHandler"))
                   && isrBad.join(QStringLiteral("\n")).contains(QStringLiteral("xQueueSend")),
               QStringLiteral("规则表共读：卡片 ISR 里调任务态接口要拦下（并写明是哪个 ISR）"));
        expect(isrBad.join(QStringLiteral("\n")).contains(QStringLiteral("taskENTER_CRITICAL")),
               QStringLiteral("规则表共读：卡片 ISR 里进内核临界区要拦下"));
        expect(RtosGen::cardIsrApiIssues(QStringLiteral("合成卡"), QStringLiteral(
                   "void SPI1_IRQHandler(void) { xQueueSendFromISR(s_q, &x, &w); }\n"
                   "void App_Task(void) { xQueueSend(s_q, &x, 0); }\n"))
                   .isEmpty(),
               QStringLiteral("规则表共读反例：ISR 里调 FromISR / 任务态函数里调同名接口都不报"));
        /* 反例（真实数据）：速查里那张串口卡本身是正确写法，不该被拦 —— 拦了就是误报 */
        const LookupData::FlowHit realCard =
            LookupData::flowByName(QStringLiteral("① 串口 USART + DMA + 空闲中断接收（不定长帧）"));
        const QStringList realIsr =
            RtosGen::cardIsrApiIssues(realCard.name, realCard.code);
        expect(realIsr.isEmpty(),
               QStringLiteral("规则表共读反例：真实串口卡的 ISR 不误报（%1）")
                   .arg(realIsr.isEmpty() ? QStringLiteral("无告警")
                                          : realIsr.join(QStringLiteral(" / "))));
    }

    /* ---------------- 生成前拦截：周期任务可调度性 ---------------- */
    {
        /* 可调度：3 个周期任务，U≈0.35 < RMS 上界 0.78 */
        QVector<TaskSpec> good;
        good << TaskSpec{QStringLiteral("A"), QStringLiteral("周期"), 10, 3, 256, {}, {}, {}, 1000};
        good << TaskSpec{QStringLiteral("B"), QStringLiteral("周期"), 20, 2, 256, {}, {}, {}, 3000};
        good << TaskSpec{QStringLiteral("C"), QStringLiteral("周期"), 50, 1, 256, {}, {}, {}, 5000};
        const QString gj = RtosGen::scheduleAnalysis(good).join(QStringLiteral("\n"));
        expect(gj.contains(QStringLiteral("利用率")) && gj.contains(QStringLiteral("RMS 上界")),
               QStringLiteral("可调度性：给出利用率与 RMS 上界"));
        expect(!gj.contains(QStringLiteral("丢周期")),
               QStringLiteral("可调度性反例：U 未超上界不报丢周期"));
        expect(!gj.contains(QStringLiteral("优先级没有按")),
               QStringLiteral("可调度性：优先级按周期单调分配时不提前提告警"));

        /* 超载：U=1.2 超上界 0.83，且 Y 的响应时间超周期 → 要报丢周期 */
        QVector<TaskSpec> over;
        over << TaskSpec{QStringLiteral("X"), QStringLiteral("周期"), 10, 3, 256, {}, {}, {}, 8000};
        over << TaskSpec{QStringLiteral("Y"), QStringLiteral("周期"), 20, 2, 256, {}, {}, {}, 8000};
        const QString oj = RtosGen::scheduleAnalysis(over).join(QStringLiteral("\n"));
        expect(oj.contains(QStringLiteral("丢周期")),
               QStringLiteral("可调度性：U 超上界 / 响应时间超周期要报出丢周期"));

        /* 优先级没按周期排 → 要提醒 RMS 前提不成立 */
        QVector<TaskSpec> wrong;
        wrong << TaskSpec{QStringLiteral("Long"), QStringLiteral("周期"), 50, 3, 256, {}, {}, {}, 1000};
        wrong << TaskSpec{QStringLiteral("Short"), QStringLiteral("周期"), 10, 1, 256, {}, {}, {}, 1000};
        expect(RtosGen::scheduleAnalysis(wrong).join(QStringLiteral("\n"))
                   .contains(QStringLiteral("优先级没有按")),
               QStringLiteral("可调度性：优先级未按周期单调分配时提前提告警"));

        /* 只填了一部分 WCET → 要说明还有几个没纳入 */
        QVector<TaskSpec> part;
        part << TaskSpec{QStringLiteral("P"), QStringLiteral("周期"), 10, 2, 256, {}, {}, {}, 1000};
        part << TaskSpec{QStringLiteral("Q"), QStringLiteral("周期"), 20, 1, 256, {}, {}, {}, 0};
        expect(RtosGen::scheduleAnalysis(part).join(QStringLiteral("\n"))
                   .contains(QStringLiteral("未填 WCET")),
               QStringLiteral("可调度性：有周期任务没填 WCET 时给出提示"));

        /* 反例：一条 WCET 都没填 → 不输出（别在没数据时刷屏） */
        expect(RtosGen::scheduleAnalysis(sampleTasks()).isEmpty(),
               QStringLiteral("可调度性反例：没有填 WCET 的周期任务时不输出"));
    }

    return fail;
}

} // namespace RtosGen
