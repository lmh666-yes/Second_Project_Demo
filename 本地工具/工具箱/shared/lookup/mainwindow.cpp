#include "mainwindow.h"

#include "apppaths.h"
#include "cardhtml.h"
#include "searchengine.h"
#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString sectionName(int s)
{
    switch (s) {
    case int(Card::Guide): return QStringLiteral("引导教程");
    case int(Card::Build): return QStringLiteral("项目引导");
    default:               return QStringLiteral("资料查询");
    }
}

QString typeTag(const Card &c)
{
    if (c.isManu())   return QStringLiteral("手册");
    if (c.isFlow())   return QStringLiteral("流程");
    if (c.isGuide())  return QStringLiteral("向导");
    if (c.isTable())  return QStringLiteral("表");
    if (c.isStruct()) return QStringLiteral("结构体");
    if (c.isDs())     return QStringLiteral("手册页");
    if (c.isChip())   return QStringLiteral("芯片");
    return QStringLiteral("函数");
}

/* 落地页内容：与网页版 / 手机版同一份清单（标准库速查-手机版.html 的
   HOT_MODULES / HOT_TOPICS / 三条路径卡）。改一处要三处一起改。
   锚点一律用**纯 ASCII 下标**（q:0 / mod:0 / path:0）而不是把中文塞进 href ——
   href 里的非 ASCII 会被 QUrl 百分号编码，回来对不上，数值下标最稳。 */
const char *kHotModules[] = { "USART", "GPIO", "TIM", "ADC", "DMA", "I2C",
                              "SPI", "NVIC", "EXTI", "RCC", "FLASH", "RTC" };
const char *kHotTopics[] = { "串口 DMA + 空闲中断", "定时器 PWM", "数据帧组包 + CRC16",
                             "FreeRTOS 任务框架", "LoRa 点对点", "ESP8266 + MQTT",
                             "I2C 接传感器", "看门狗", "RTC 电子钟", "Flash 断网缓存" };

const int kHotModuleN = int(sizeof(kHotModules) / sizeof(kHotModules[0]));
const int kHotTopicN  = int(sizeof(kHotTopics)  / sizeof(kHotTopics[0]));

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("单片机手册"));
    buildUi();

    /* 恢复上次板块 */
    const int sec = m_store.lastSection();
    if (sec >= 0 && sec <= 2 && sec != m_section)
        onSectionClicked(sec);

    /* 快捷键，与网页版对齐：/ 搜、Esc 清空、Ctrl+D 收藏、Ctrl+B 收藏夹、Ctrl+H 最近、Ctrl+J 主题
       （Qt6 去掉了 QShortcut 的"key + 槽"构造，得自己 connect）

       上下文必须限定在"本页及子控件"内：本页已并入工具箱，QShortcut 默认是
       Qt::WindowShortcut，作用域是整窗——不改的话在 RTOS 生成 / 协议工作台等页面上
       按 Ctrl+D、Ctrl+H、"/" 会误触速查的收藏、最近与搜索框聚焦。 */
    auto addSc = [this](const QKeySequence &k) {
        QShortcut *sc = new QShortcut(k, this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        return sc;
    };
    connect(addSc(QKeySequence(QStringLiteral("/"))), &QShortcut::activated, this, [this]() {
        m_search->setFocus();
        m_search->selectAll();
    });
    connect(addSc(QKeySequence(Qt::Key_Escape)), &QShortcut::activated, this, [this]() {
        m_search->clear();
        onSearchEdited();
    });
    const struct { const char *key; void (MainWindow::*slot)(); } scs[] = {
        { "Ctrl+D", &MainWindow::toggleCurrentFav },
        { "Ctrl+B", &MainWindow::onFavClicked },
        { "Ctrl+H", &MainWindow::onRecentClicked },
        { "Ctrl+J", &MainWindow::onThemeToggled },
    };
    for (const auto &s : scs)
        connect(addSc(QKeySequence(QString::fromLatin1(s.key))), &QShortcut::activated, this, s.slot);
}

void MainWindow::buildUi()
{
    QWidget *central = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(6);

    /* ---- 第一行：板块 + 主题 ---- */
    QHBoxLayout *row1 = new QHBoxLayout;
    const char *names[3] = { "📖 资料查询", "📚 引导教程", "🧭 项目引导" };
    for (int i = 0; i < 3; ++i) {
        QPushButton *b = new QPushButton(QString::fromUtf8(names[i]), this);
        b->setCheckable(true);
        b->setChecked(i == m_section);
        connect(b, &QPushButton::clicked, this, [this, i]() { onSectionClicked(i); });
        row1->addWidget(b);
        m_secBtn[i] = b;
    }
    row1->addStretch(1);
    m_btnFav = new QPushButton(QStringLiteral("★ 收藏"), this);
    m_btnFav->setToolTip(QStringLiteral("收藏夹（Ctrl+B）"));
    connect(m_btnFav, &QPushButton::clicked, this, &MainWindow::onFavClicked);
    row1->addWidget(m_btnFav);
    m_btnRecent = new QPushButton(QStringLiteral("🕘 最近"), this);
    m_btnRecent->setToolTip(QStringLiteral("最近查看（Ctrl+H）"));
    connect(m_btnRecent, &QPushButton::clicked, this, &MainWindow::onRecentClicked);
    row1->addWidget(m_btnRecent);
    QPushButton *themeBtn = new QPushButton(QStringLiteral("🌗 主题"), this);
    connect(themeBtn, &QPushButton::clicked, this, &MainWindow::onThemeToggled);
    row1->addWidget(themeBtn);
    m_count = new QLabel(this);
    row1->addWidget(m_count);
    root->addLayout(row1);

    /* ---- 第二行：搜索 + 模块筛选 ---- */
    QHBoxLayout *row2 = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral(
        "中文 / 英文 / 混排都能搜（如 打开DMA中断、串口 中断、USART_Init）"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::returnPressed, this, &MainWindow::onSearchEdited);
    /* 边打字边出结果：网页版就是即时的，桌面版以前只在回车后才刷新，
       用户反馈"搜了没反应、得去切一下模块才出来"。这里做 120ms 防抖 ——
       连续打字期间不重排，停手立刻刷新，手感与网页版一致。 */
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(120);
    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::onSearchEdited);
    connect(m_search, &QLineEdit::textChanged, this, [this]() { m_searchTimer->start(); });
    m_module = new QComboBox(this);
    m_module->setMinimumWidth(190);
    connect(m_module, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModuleChanged);
    row2->addWidget(m_search, 1);
    row2->addWidget(new QLabel(QStringLiteral("模块"), this));
    row2->addWidget(m_module);
    root->addLayout(row2);

    /* ---- 主体：列表 + 详情 ---- */
    QSplitter *sp = new QSplitter(Qt::Horizontal, this);
    m_list = new QListWidget(sp);
    m_list->setMinimumWidth(260);
    connect(m_list, &QListWidget::currentRowChanged, this, &MainWindow::onCardPicked);
    m_detail = new QTextBrowser(sp);
    m_detail->setOpenLinks(false);
    /* 等宽字体等文档级样式要设在 QTextDocument 上（QTextBrowser 没有这个接口） */
    m_detail->document()->setDefaultStyleSheet(QStringLiteral(
        "pre { font-family: \"Cascadia Mono\", Consolas, monospace; }"
        "table { border-collapse: collapse; }"
        "a { text-decoration: none; }"));
    connect(m_detail, &QTextBrowser::anchorClicked, this, &MainWindow::onAnchor);
    sp->addWidget(m_list);
    sp->addWidget(m_detail);
    sp->setStretchFactor(0, 0);
    sp->setStretchFactor(1, 1);
    root->addWidget(sp, 1);

    setCentralWidget(central);
    resize(1120, 720);
}

bool MainWindow::loadData(QString *err)
{
    if (!m_ds.load(AppPaths::datasetPath())) {
        if (err)
            *err = m_ds.error();
        if (m_detail)
            m_detail->setHtml(QStringLiteral("<p style='color:#ffb454;'>%1</p>")
                                  .arg(CardHtml::esc(m_ds.error())));
        return false;
    }
    if (!m_tables.load(m_ds.search()) && err)
        *err = QStringLiteral("搜索用表缺失（dataset.json 的 search 段为空）");
    m_engine.reset(new SearchEngine(&m_ds.cards(), &m_tables));
    rebuildModuleBox();
    refreshList();
    return true;
}

void MainWindow::rebuildModuleBox()
{
    const QString keep = m_module->currentText();
    m_module->blockSignals(true);
    m_module->clear();
    m_module->addItem(m_section == int(Card::Guide) ? QStringLiteral("全部教程")
                                                    : QStringLiteral("全部模块"));

    /* 模块名与网页一致：引导/流程卡按「主题」归类，其余按 m；项目引导按工程 */
    QStringList mods;
    for (int i : m_ds.indicesOfSection(m_section)) {
        const Card &c = m_ds.at(i);
        QString k;
        if (m_section == int(Card::Build))
            k = c.b;
        else if (m_engine)
            k = m_engine->modKeyOf(c);
        else
            k = c.m;
        if (!k.isEmpty() && !mods.contains(k))
            mods << k;
    }
    std::sort(mods.begin(), mods.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    m_module->addItems(mods);

    const int i = m_module->findText(keep);
    if (i >= 0)
        m_module->setCurrentIndex(i);
    m_module->blockSignals(false);
}

void MainWindow::onSectionClicked(int sec)
{
    m_section = sec;
    m_store.setLastSection(sec);
    for (int i = 0; i < 3; ++i)
        m_secBtn[i]->setChecked(i == sec);
    rebuildModuleBox();
    refreshList();
    m_detail->clear();
}

void MainWindow::onModuleChanged(int)
{
    refreshList();
}

void MainWindow::onSearchEdited()
{
    refreshList();
}

void MainWindow::refreshList()
{
    m_shown.clear();
    const QString mod = (m_module->currentIndex() > 0) ? m_module->currentText() : QString();

    if (m_section == int(Card::Build)) {
        /* 项目引导不走搜索引擎，直接列手册阶段卡（按工程筛） */
        for (int i : m_ds.indicesOfSection(m_section))
            if (mod.isEmpty() || m_ds.at(i).b == mod)
                m_shown << i;
    } else if (m_engine) {
        m_shown = m_engine->run(m_search->text(), m_section, mod).order;
    } else {
        m_shown = m_ds.indicesOfSection(m_section);
    }

    m_list->blockSignals(true);
    m_list->clear();
    for (int i : m_shown) {
        const Card &c = m_ds.at(i);
        m_list->addItem(QStringLiteral("%1  ⟨%2⟩").arg(c.n, typeTag(c)));
    }
    m_list->blockSignals(false);

    /* 落地页只在「资料查询 · 全部模块 · 没搜索」时出现，与网页/手机一致：
       一搜索或一选模块，列表就是正常结果，落地页立刻消失。
       这时右侧显示落地页而不是第一条卡；列表**不选中**任何行（选中会触发
       onCardPicked 把详情换成第 0 条卡，盖掉落地页）。 */
    const bool landing = (m_section == int(Card::Ref)) && mod.isEmpty()
                         && m_search->text().trimmed().isEmpty();
    if (landing) {
        m_list->setCurrentRow(-1);
        m_current = -1;
        m_detail->setHtml(landingHtml());
        m_detail->verticalScrollBar()->setValue(0);
        updateCounts();
        return;
    }

    updateCounts();
    if (!m_shown.isEmpty())
        m_list->setCurrentRow(0);
    else {
        m_detail->setHtml(QStringLiteral("<p style='color:#8b93a3;'>没有匹配的条目。</p>"));
        m_current = -1;
    }
}

/* 落地页 HTML（「从这里开始」）。Qt 富文本不支持 flex/grid，
   所以路径卡与 chip 一律摊平成块级元素 + 空格分隔。 */
QString MainWindow::landingHtml() const
{
    const bool dark = (Theme::mode() == Theme::Dark);
    const QString dim    = dark ? QStringLiteral("#8b93a3") : QStringLiteral("#5b667a");
    const QString accent = dark ? QStringLiteral("#7fb3ff") : QStringLiteral("#1a6fd6");

    QString h = QStringLiteral(
        "<p style='font-size:15px;font-weight:700;margin:0 0 6px 0;'>👋 从这里开始</p>"
        "<p style='color:%1;font-size:12px;margin:0 0 12px 0;'>"
        "不确定用哪个函数？先看「流程卡」（按功能给完整可抄的初始化流程），"
        "或「引导教程」（从零一步步搭到能跑）。下面按你常做的事分组。</p>").arg(dim);

    const struct { const char *ic, *t, *d, *anchor; } paths[3] = {
        { "🧭", "我要自己搭一个外设",     "打开「引导教程」，从接线到验收一步步做", "path:0" },
        { "📋", "我知道要干嘛，只差代码", "搜功能词（如「打开DMA中断」），直接看流程卡", "path:1" },
        { "📖", "我要查某个函数的参数",   "搜函数名或宏名，看参数表 / 返回值 / 示例",   "path:2" },
    };
    for (const auto &p : paths)
        h += QStringLiteral("<p style='margin:0 0 8px 0;'>"
                            "<a href='%1' style='color:%5;font-weight:600;'>%2 %3</a>"
                            "<br><span style='color:%4;font-size:12px;'>%6</span></p>")
                 .arg(QString::fromLatin1(p.anchor), QString::fromUtf8(p.ic),
                      QString::fromUtf8(p.t), dim, accent, QString::fromUtf8(p.d));

    h += QStringLiteral("<p style='color:%1;font-size:12px;margin:14px 0 5px 0;'>"
                        "热门主题（点一下直接搜）</p>").arg(dim);
    for (int i = 0; i < kHotTopicN; ++i)
        h += QStringLiteral("<a href='q:%1'>%2</a>&nbsp;&nbsp;")
                 .arg(i).arg(CardHtml::esc(QString::fromUtf8(kHotTopics[i])));

    h += QStringLiteral("<p style='color:%1;font-size:12px;margin:14px 0 5px 0;'>"
                        "常用模块（点一下筛该模块）</p>").arg(dim);
    for (int i = 0; i < kHotModuleN; ++i) {
        const QString mod = QString::fromUtf8(kHotModules[i]);
        int n = 0;
        for (int ci : m_ds.indicesOfSection(int(Card::Ref)))
            if (m_ds.at(ci).m == mod) ++n;
        if (!n)
            continue;
        h += QStringLiteral("<a href='mod:%1'>%2</a>&nbsp;<span style='color:%4;"
                            "font-size:11px;'>(%3)</span>&nbsp;&nbsp;")
                 .arg(i).arg(CardHtml::esc(mod)).arg(n).arg(dim);
    }
    return h;
}

void MainWindow::updateCounts()
{
    const QString mod = (m_module->currentIndex() > 0) ? m_module->currentText() : QString();
    const bool landing = (m_section == int(Card::Ref)) && mod.isEmpty()
                         && m_search->text().trimmed().isEmpty();
    if (landing) {
        int flow = 0, guide = 0;
        for (const Card &c : m_ds.cards()) {
            if (c.isFlow())  ++flow;
            if (c.isGuide()) ++guide;
        }
        m_count->setText(QStringLiteral("共 %1 条 · %2 流程卡 + %3 向导可直接照着做")
                             .arg(m_ds.size()).arg(flow).arg(guide));
        return;
    }
    m_count->setText(QStringLiteral("%1 · 显示 %2 / 共 %3 条")
                         .arg(sectionName(m_section))
                         .arg(m_shown.size()).arg(m_ds.size()));
}

void MainWindow::onCardPicked(int row)
{
    if (row < 0 || row >= m_shown.size())
        return;
    showCard(m_shown.at(row));
}

void MainWindow::showCard(int cardIndex)
{
    if (cardIndex < 0 || cardIndex >= m_ds.size())
        return;
    m_current = cardIndex;
    const bool dark = (Theme::mode() == Theme::Dark);
    const Card &card = m_ds.at(cardIndex);

    CardHtml::Ctx ctx;
    ctx.index = cardIndex;
    ctx.isFav = m_store.isFavorite(card.n);
    ctx.done  = m_store.doneSteps();

    /* 命中原因：网页版会在条目旁标出「名称 / 正文」命中，这里放在详情顶部 */
    QString head;
    if (m_engine && !m_search->text().trimmed().isEmpty()) {
        const QStringList hits = m_engine->hitWords(m_search->text(), cardIndex, m_section);
        if (!hits.isEmpty())
            head = QStringLiteral("<p style='color:%1;font-size:12px;margin:0 0 6px 0;'>%2</p>")
                       .arg(dark ? QStringLiteral("#8b93a3") : QStringLiteral("#5b667a"),
                            CardHtml::esc(QString::fromUtf8("命中：") + hits.join(QString::fromUtf8("　"))));
    }
    m_detail->setHtml(head + CardHtml::build(card, dark, ctx));
    m_detail->verticalScrollBar()->setValue(0);
    m_store.touchRecent(card.n);
}

void MainWindow::onAnchor(const QUrl &url)
{
    const QString s = url.toString();
    if (s.startsWith(QStringLiteral("card:"))) {
        jumpToName(s.mid(5));
    } else if (s.startsWith(QStringLiteral("fav:"))) {
        toggleCurrentFav();
    } else if (s.startsWith(QStringLiteral("step:"))) {
        const QStringList parts = s.mid(5).split(QLatin1Char(':'));
        if (parts.size() == 2 && m_current >= 0) {
            m_store.toggleStep(m_ds.at(m_current).n, parts.at(1).toInt());
            showCard(m_current);
        }
    } else if (s.startsWith(QStringLiteral("copy:"))) {
        if (m_current >= 0)
            QApplication::clipboard()->setText(CardHtml::copyText(m_ds.at(m_current)));
        statusBar()->showMessage(QStringLiteral("代码已复制到剪贴板"), 2000);
    } else if (s.startsWith(QStringLiteral("q:"))) {
        /* 落地页「热门主题」chip：把词填进搜索框（屏蔽信号免得防抖定时器再刷一遍） */
        const int i = s.mid(2).toInt();
        if (i >= 0 && i < kHotTopicN) {
            m_searchTimer->stop();
            m_search->blockSignals(true);
            m_search->setText(QString::fromUtf8(kHotTopics[i]));
            m_search->blockSignals(false);
            refreshList();
        }
    } else if (s.startsWith(QStringLiteral("mod:"))) {
        /* 落地页「常用模块」chip：选中对应模块；setCurrentIndex 会触发 refreshList */
        const int i = s.mid(4).toInt();
        if (i >= 0 && i < kHotModuleN) {
            const int idx = m_module->findText(QString::fromUtf8(kHotModules[i]));
            if (idx > 0)
                m_module->setCurrentIndex(idx);
            else
                statusBar()->showMessage(QStringLiteral("该模块当前没有条目"), 2000);
        }
    } else if (s.startsWith(QStringLiteral("path:"))) {
        /* 落地页三条路径卡：0 切「引导教程」，1/2 直接塞搜索词 */
        const int i = s.mid(5).toInt();
        if (i == 0) {
            onSectionClicked(int(Card::Guide));
        } else if (i == 1 || i == 2) {
            m_searchTimer->stop();
            m_search->blockSignals(true);
            m_search->setText(i == 1 ? QStringLiteral("打开DMA中断")
                                     : QStringLiteral("USART_Init"));
            m_search->blockSignals(false);
            refreshList();
        }
    }
}

void MainWindow::toggleCurrentFav()
{
    if (m_current < 0)
        return;
    const bool now = m_store.toggleFavorite(m_ds.at(m_current).n);
    showCard(m_current);
    statusBar()->showMessage(now ? QStringLiteral("已加入收藏 ★")
                                 : QStringLiteral("已取消收藏"), 2000);
}

void MainWindow::onFavClicked()
{
    QMenu menu(this);
    const QStringList favs = m_store.favorites();
    if (favs.isEmpty()) {
        QAction *a = menu.addAction(QStringLiteral("还没有收藏：在详情页点 ☆ 或按 Ctrl+D"));
        a->setEnabled(false);
    }
    for (const QString &n : favs) {
        QAction *a = menu.addAction(n);
        connect(a, &QAction::triggered, this, [this, n]() { jumpToName(n); });
    }
    if (!favs.isEmpty()) {
        menu.addSeparator();
        QAction *c = menu.addAction(QStringLiteral("清空收藏"));
        connect(c, &QAction::triggered, this, [this]() {
            m_store.clearFavorites();
            statusBar()->showMessage(QStringLiteral("收藏已清空"), 2000);
        });
    }
    menu.exec(m_btnFav->mapToGlobal(QPoint(0, m_btnFav->height())));
}

void MainWindow::onRecentClicked()
{
    QMenu menu(this);
    const QStringList rec = m_store.recent();
    if (rec.isEmpty()) {
        QAction *a = menu.addAction(QStringLiteral("还没有浏览记录"));
        a->setEnabled(false);
    }
    for (const QString &n : rec) {
        QAction *a = menu.addAction(n);
        connect(a, &QAction::triggered, this, [this, n]() { jumpToName(n); });
    }
    if (!rec.isEmpty()) {
        menu.addSeparator();
        QAction *c = menu.addAction(QStringLiteral("清空最近"));
        connect(c, &QAction::triggered, this, [this]() {
            m_store.clearRecent();
            statusBar()->showMessage(QStringLiteral("最近查看已清空"), 2000);
        });
    }
    menu.exec(m_btnRecent->mapToGlobal(QPoint(0, m_btnRecent->height())));
}

void MainWindow::jumpToName(const QString &name)
{
    const int idx = m_ds.indexOfName(name);
    if (idx < 0) {
        statusBar()->showMessage(QStringLiteral("找不到条目：%1").arg(name), 3000);
        return;
    }
    const int sec = int(m_ds.at(idx).section());
    const bool crossSec = (sec != m_section);

    /* 跨板块跳转时，搜索框里的旧查询词还在，列表会继续被过滤 → 目标条目选不中。
       所以这里**一律先清干净搜索与模块筛选**，保证目标一定出现在列表里。 */
    if (crossSec)
        onSectionClicked(sec);
    if (!m_search->text().isEmpty()) {
        m_search->blockSignals(true);
        m_search->clear();
        m_search->blockSignals(false);
    }
    /* 模块筛选同样可能把它滤掉 */
    if (m_module->currentIndex() != 0) {
        m_module->blockSignals(true);
        m_module->setCurrentIndex(0);
        m_module->blockSignals(false);
    }
    refreshList();

    const int row = m_shown.indexOf(idx);
    if (row >= 0) {
        m_list->setCurrentRow(row);
        if (QListWidgetItem *it = m_list->item(row))
            m_list->scrollToItem(it);
    } else {
        showCard(idx);
    }
}

void MainWindow::onThemeToggled()
{
    Theme::toggle();
    if (m_current >= 0)
        showCard(m_current);        /* QTextDocument 不吃应用 QSS，必须重渲染 */
    emit themeToggled();            /* 通知工具箱侧栏同步按钮文字 */
}
