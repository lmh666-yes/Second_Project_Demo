#ifndef UISMOKE_H
#define UISMOKE_H

/* GUI 崩溃冒烟出口：--gui-smoke
 *
 * 为什么需要：--selftest 只跑纯逻辑，覆盖不到 GUI。曾经出现过"四个自检全绿，
 * 但一点主题按钮就卡死"——窗口构造、样式重刷、控件交互这些路径只有真开窗口才走到。
 * 这个出口把主窗口建起来、遍历页面与控件、点一遍"不会弹对话框、不跑长任务"的按钮，
 * 用退出码报告结果，可自动跑、可复跑。
 *
 * 覆盖面提醒：正文列表若靠"可勾选的板块按钮"切换内容（速查页就是这样），
 * 只遍历当前板块会漏掉大部分条目。所以第 3b 步会把每个可勾选按钮各切一次、
 * 每切一次把列表重遍历一遍——否则资料板块那 900+ 张卡永远测不到（曾经就是盲区）。
 * 遍历按等间隔**抽样**（每块列表最多 40 行 + 补末行）：资料板块 900+ 行逐行点
 * 要跑 1~2 分钟，抽样后回到秒级，覆盖范围（行数 / 实点行数）照样打印出来。
 *
 * 另一处盲区：所有「运行」按钮都在屏蔽表里（怕弹框/长任务），于是"工具箱 -> Python
 * 子进程"这条链从来没被冒烟真正跑过。第 3c 步专门只跑一页（带硬超时），
 * 用输出框里有没有东西来证明这条链是通的。
 *
 * 用法（QApplication 之后、窗口对象已构造）：
 *     if (args.contains("--gui-smoke")) return uiSmoke::run(&w);
 *     if (args.contains("--gui-smoke")) return uiSmoke::run(&w, args);   // 想顺便截图时
 * 附加参数：--shot <图片路径>  跑完把窗口截一张图（排 UI 尺寸/布局问题用）
 * 退出码：0 = 未崩溃；卡死时本进程无法自救，外层请加超时并强杀。
 *
 * 说明：Git上传/src/ 下另有一份同样的拷贝（该工具独立于工具箱，不共用源码）。
 */

#include <QApplication>
#include <QCheckBox>
#include <QEventLoop>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStringConverter>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QWidget>

namespace uiSmoke {

inline QTextStream &log()
{
    static QTextStream s(stdout);
    static bool init = false;
    if (!init) {
        s.setEncoding(QStringConverter::Utf8);
        init = true;
    }
    return s;
}

/* 会弹对话框/弹出菜单、会跑长任务、会改仓库/网络的按钮一律不点，
   免得冒烟被对话框或 QMenu 的 exec 循环卡住，或误触发上传/拉取这类操作。 */
inline bool safeToClick(const QString &text)
{
    static const char *blocked[] = {
        "运行", "停止", "上传", "拉取", "生成", "保存", "应用", "绑定", "初始化",
        "导入", "导出", "打开", "另存", "浏览", "选择", "发送", "管理",
        "删除", "重命名", "新建",
        "收藏", "最近",   /* 这两类按钮点开会 exec() 一个弹出菜单，冒烟里点不了 */
    };
    const QString t = text.trimmed();
    if (t.isEmpty())
        return false;
    for (const char *b : blocked) {
        if (t.contains(QString::fromUtf8(b)))
            return false;
    }
    return true;
}

inline int run(QWidget *w, const QStringList &args = QStringList())
{
    w->show();
    QTimer::singleShot(700, [w, args]() {
        QTextStream &o = log();

        /* 先把窗口实际尺寸报出来：排 "UI 太大/太小" 这类问题时，先看数再看图 */
        {
            const qreal dpr = w->devicePixelRatioF();
            QScreen *scr = w->screen();
            const QRect av = scr ? scr->availableGeometry() : QRect();
            const QSize mn = w->minimumSizeHint();
            o << "WINDOW " << w->width() << "x" << w->height()
              << "  逻辑DPI比=" << dpr
              << "  屏幕可用区=" << av.width() << "x" << av.height()
              << "  最小尺寸=" << mn.width() << "x" << mn.height() << "\n";
            o.flush();
            /* 窗口缩不小，通常是某一页的 minimumSizeHint 太大把整摞顶住了：
               QStackedLayout 的最小尺寸取所有页的最大值（含隐藏页）。 */
            for (QStackedWidget *s : w->findChildren<QStackedWidget *>()) {
                for (int i = 0; i < s->count(); ++i) {
                    QWidget *pg = s->widget(i);
                    const QSize m = pg->minimumSizeHint();
                    if (m.width() >= 900 || m.height() >= 650)
                        o << "BIGPAGE " << i << " " << pg->metaObject()->className()
                          << " min=" << m.width() << "x" << m.height() << "\n";
                }
            }
            o.flush();
        }

        /* 模态对话框自动关掉，免得冒烟停在对话框上 */
        QTimer *closer = new QTimer(w);
        QObject::connect(closer, &QTimer::timeout, []() {
            if (QWidget *m = QApplication::activeModalWidget())
                m->close();
        });
        closer->start(200);

        /* 1) 逐个点"安全"按钮 */
        for (QPushButton *b : w->findChildren<QPushButton *>()) {
            if (!b->isEnabled())
                continue;
            if (!safeToClick(b->text())) {
                o << "SKIP  " << b->text() << "\n"; o.flush();
                continue;
            }
            o << "CLICK " << b->text() << "\n"; o.flush();
            b->click();
            QCoreApplication::processEvents();
        }

        /* 2) 遍历标签页 */
        for (QTabWidget *t : w->findChildren<QTabWidget *>()) {
            for (int i = 0; i < t->count(); ++i) {
                o << "TAB   " << t->tabText(i) << "\n"; o.flush();
                t->setCurrentIndex(i);
                QCoreApplication::processEvents();
            }
        }

        /* 3) 遍历列表（会切页面，覆盖各页构造后的重刷）。
              每轮把"遍历了几个列表、共多少行、最长那个多少行"打出来——
              这样"某个板块到底有没有被渲染到"是可查的，不用靠假设。 */
        auto traverseLists = [&o, w](const QString &label) {
            /* 抽样遍历：资料板块有 900+ 行，逐行 setCurrentRow 会让冒烟跑好几分钟
               （实测 3b 步要 1~2 分钟，外层超时得给到 300s）。
               这里每块列表最多点 kMaxProbe 行（等间隔抽样 + 补最后一行），
               既覆盖到"切行 → 渲染详情"这条路径，又把耗时压回秒级。
               行数照样打印出来，覆盖范围可查。 */
            const int kMaxProbe = 40;
            int lists = 0, rows = 0, maxRows = 0, probed = 0;
            for (QListWidget *l : w->findChildren<QListWidget *>()) {
                const int n = l->count();
                if (n > 0) {
                    ++lists;
                    rows += n;
                    maxRows = qMax(maxRows, n);
                }
                if (n == 0)
                    continue;
                const int step = (n > kMaxProbe) ? (n / kMaxProbe) : 1;
                for (int r = 0; r < n; r += step) {
                    QListWidgetItem *it = l->item(r);
                    if (!it || it->flags() == Qt::NoItemFlags)
                        continue;
                    l->setCurrentRow(r);
                    QCoreApplication::processEvents();
                    ++probed;
                }
                /* 抽样步长不整除时补上末行，别让"最后一张卡"永远测不到 */
                if ((n - 1) % step != 0) {
                    if (QListWidgetItem *it = l->item(n - 1)) {
                        if (it->flags() != Qt::NoItemFlags) {
                            l->setCurrentRow(n - 1);
                            QCoreApplication::processEvents();
                            ++probed;
                        }
                    }
                }
            }
            o << "LISTS " << label << "：列表 " << lists << " 个 / 共 " << rows
              << " 行 / 最长 " << maxRows << " 行 / 实点 " << probed << " 行\n";
            o.flush();
        };
        traverseLists(QStringLiteral("首轮"));

        /* 3b) 有些页面的正文列表由"可勾选的板块按钮"切换内容：
               只遍历当前板块会漏掉大部分条目——速查页资料板块那 900+ 张卡
               以前在冒烟里从来没被真正渲染过（盲区）。
               这里把每个可勾选按钮都切一次、并把它的页面切到前面（走真实"可见"路径），
               每切一次把列表整体重遍历一遍，于是每个板块都会被渲染到。
               按钮本身受 safeToClick 过滤，不会点开弹出菜单的按钮。 */
        QList<QPushButton *> toggles;
        for (QPushButton *b : w->findChildren<QPushButton *>()) {
            if (b->isCheckable() && b->isEnabled() && safeToClick(b->text()))
                toggles << b;
        }
        for (QPushButton *b : toggles) {
            /* 找按钮所在的"一页"：顺着父链往上，第一个 QStackedWidget 的直属子控件 */
            QWidget *pg = b;
            QStackedWidget *stk = nullptr;
            while (pg->parentWidget()) {
                if (auto *s = qobject_cast<QStackedWidget *>(pg->parentWidget())) {
                    stk = s;
                    break;
                }
                pg = pg->parentWidget();
            }
            o << "TOGGLE " << b->text() << (stk ? "（页已切到前面）" : "") << "\n";
            o.flush();
            if (stk)
                stk->setCurrentWidget(pg);
            b->click();
            QCoreApplication::processEvents();
            traverseLists(b->text());
        }

        /* 3c) 运行器页抽查：真跑一次"工具箱 -> Python"这条链。
               屏蔽表把所有「运行」按钮都挡掉了（怕弹框/长任务），所以这条链在冒烟里
               从来没通过；这里只挑一页跑，带硬超时，靠"输出框里有没有内容"判定。
               挂在步骤 4 之前：勾选框那步会翻 CRLF 等选项，先跑免得被改到。 */
        {
            QPushButton *runBtn = nullptr;
            QStackedWidget *runStk = nullptr;
            QWidget *runPage = nullptr;
            for (QPushButton *b : w->findChildren<QPushButton *>()) {
                if (!b->isEnabled() || b->text().trimmed() != QStringLiteral("运行"))
                    continue;                 /* 只要纯「运行」，不要「运行自检」等 */
                QWidget *pg = b;
                while (pg->parentWidget()) {
                    if (auto *s = qobject_cast<QStackedWidget *>(pg->parentWidget())) {
                        runStk = s;
                        runPage = pg;
                        break;
                    }
                    pg = pg->parentWidget();
                }
                if (runPage) { runBtn = b; break; }
            }
            QPlainTextEdit *outBox = runPage ? runPage->findChild<QPlainTextEdit *>() : nullptr;
            if (!runBtn || !runStk || !outBox) {
                o << "RUN   skipped（这一份程序里没有可运行的检查页）\n"; o.flush();
            } else {
                if (runStk->currentWidget() != runPage)
                    runStk->setCurrentWidget(runPage);
                QCoreApplication::processEvents();
                outBox->clear();
                o << "RUN   click 运行（超时 90s）\n"; o.flush();
                runBtn->click();
                /* 等输出出现，或超时：用嵌套事件循环，别用忙等（QProcess 靠事件循环收输出） */
                QEventLoop loop;
                QTimer guard;
                guard.setSingleShot(true);
                QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
                QObject::connect(outBox, &QPlainTextEdit::textChanged, &loop, [&]() {
                    if (!outBox->toPlainText().trimmed().isEmpty())
                        loop.quit();
                });
                guard.start(90000);
                loop.exec();
                const QString txt = outBox->toPlainText().trimmed();
                const QString head = txt.left(60).replace(QLatin1Char('\n'), QLatin1Char(' '));
                if (txt.isEmpty())
                    o << "RUN   无输出（进程没起来或没写回来）\n";
                else
                    o << "RUNOK 输出 " << txt.size() << " 字节：" << head << "\n";
                o.flush();
            }
        }

        /* 4) 勾选框翻一遍 */
        for (QCheckBox *c : w->findChildren<QCheckBox *>()) {
            c->toggle();
            QCoreApplication::processEvents();
        }

        /* 5) 可选截图（排 UI 尺寸 / 布局问题时用） */
        const int si = args.indexOf(QStringLiteral("--shot"));
        if (si >= 0 && si + 1 < args.size()) {
            const QString p = args.at(si + 1);
            if (w->grab().save(p))
                o << "SHOT  " << p << "\n";
            else
                o << "SHOT  失败 " << p << "\n";
            o.flush();
        }

        o << "GUI smoke 通过（未崩溃）\n"; o.flush();
        QCoreApplication::exit(0);
    });
    return QApplication::exec();
}

} // namespace uiSmoke

#endif // UISMOKE_H
