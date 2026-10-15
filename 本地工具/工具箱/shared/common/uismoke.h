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
#include <QListWidget>
#include <QListWidgetItem>
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
            int lists = 0, rows = 0, maxRows = 0;
            for (QListWidget *l : w->findChildren<QListWidget *>()) {
                if (l->count() > 0) {
                    ++lists;
                    rows += l->count();
                    maxRows = qMax(maxRows, l->count());
                }
                for (int r = 0; r < l->count(); ++r) {
                    QListWidgetItem *it = l->item(r);
                    if (!it || it->flags() == Qt::NoItemFlags)
                        continue;
                    l->setCurrentRow(r);
                    QCoreApplication::processEvents();
                }
            }
            o << "LISTS " << label << "：列表 " << lists << " 个 / 共 " << rows
              << " 行 / 最长 " << maxRows << " 行\n";
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
