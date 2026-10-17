#include "rtosgenpage.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShowEvent>
#include <QStringConverter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "lookupdata.h"   /* 流程卡清单 / 整段参考代码，都取自「单片机手册」dataset.json */

namespace {

struct KindMap { const char *key; const char *label; };

const KindMap TASK_KINDS[] = {
    {"周期", "周期"}, {"事件", "事件"}, {"一次性", "一次性"},
};
const KindMap RES_KINDS[] = {
    {"queue", "队列"}, {"mutex", "互斥量"}, {"recursive", "递归互斥量"},
    {"counting", "计数信号量"}, {"binary", "二值信号量"}, {"timer", "软定时器"},
};

QComboBox *makeKindCombo(const KindMap *map, int n, const QString &current, QWidget *parent)
{
    QComboBox *c = new QComboBox(parent);
    for (int i = 0; i < n; ++i)
        c->addItem(QString::fromUtf8(map[i].label), QString::fromUtf8(map[i].key));
    const int idx = c->findData(current);
    if (idx >= 0)
        c->setCurrentIndex(idx);
    return c;
}

QTableWidgetItem *textItem(const QString &s)
{
    QTableWidgetItem *it = new QTableWidgetItem(s);
    return it;
}

} // namespace

RtosGenPage::RtosGenPage(QWidget *parent)
    : QWidget(parent)
{
    /* 这一页内容最多（两张表 + 两块文本框 + 若干按钮行），直接堆在页面上时
       实测页面最小尺寸 1021×921，会把整个工具箱窗口顶到 1265×939、缩不小。
       套一层滚动区：窗口可以随意缩小，内容放不下时出滚动条。 */
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(9, 9, 9, 9);   /* F2：本页原来是 0/0，是全界面唯一"贴边"的页 */
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    QWidget *content = new QWidget(scroll);
    scroll->setWidget(content);

    QVBoxLayout *lay = new QVBoxLayout(content);

    QLabel *t = new QLabel(QStringLiteral("RTOS 骨架生成"), this);
    QFont tf = t->font();
    tf.setPointSize(tf.pointSize() + 3);
    tf.setBold(true);
    t->setFont(tf);
    lay->addWidget(t);

    QLabel *desc = new QLabel(QStringLiteral(
        "读移植好的工程里的 FreeRTOSConfig.h，按任务与资源清单生成 main.c 骨架。"
        "生成前会按配置做校验（优先级、栈、堆、需要的功能开关），命中项列在下方。"), this);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color:#555555;"));
    lay->addWidget(desc);

    /* ---- 配置来源 ---- */
    QHBoxLayout *cfgRow = new QHBoxLayout;
    cfgRow->addWidget(new QLabel(QStringLiteral("FreeRTOSConfig.h"), this));
    m_cfgEdit = new QLineEdit(this);
    m_cfgEdit->setPlaceholderText(QStringLiteral("你的 FreeRTOS 工程里 FreeRTOS/port/FreeRTOSConfig.h 的路径"));
    cfgRow->addWidget(m_cfgEdit, 1);
    QPushButton *bBrowse = new QPushButton(QStringLiteral("浏览"), this);
    QPushButton *bParse  = new QPushButton(QStringLiteral("解析"), this);
    QPushButton *bB1     = new QPushButton(QStringLiteral("用板1配置"), this);
    QPushButton *bB2     = new QPushButton(QStringLiteral("用板2配置"), this);
    cfgRow->addWidget(bBrowse);
    cfgRow->addWidget(bParse);
    cfgRow->addWidget(bB1);
    cfgRow->addWidget(bB2);
    lay->addLayout(cfgRow);

    m_cfgInfo = new QLabel(QStringLiteral("尚未解析配置。"), this);
    m_cfgInfo->setWordWrap(true);
    m_cfgInfo->setStyleSheet(QStringLiteral("color:#555555;"));
    lay->addWidget(m_cfgInfo);

    /* ---- 任务清单 ---- */
    QGroupBox *tbox = new QGroupBox(QStringLiteral("任务清单"), this);
    QVBoxLayout *tl = new QVBoxLayout(tbox);
    m_taskTable = new QTableWidget(0, 9, tbox);
    m_taskTable->setHorizontalHeaderLabels({QStringLiteral("任务名"), QStringLiteral("类型"),
                                            QStringLiteral("周期ms"), QStringLiteral("优先级"),
                                            QStringLiteral("栈(字)"), QStringLiteral("职责"),
                                            QStringLiteral("关联资源"),
                                            QStringLiteral("补全提示词"),
                                            QStringLiteral("WCET(µs)")});
    m_taskTable->horizontalHeader()->setStretchLastSection(true);
    m_taskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tl->addWidget(m_taskTable);
    QHBoxLayout *tbar = new QHBoxLayout;
    QPushButton *bAddT = new QPushButton(QStringLiteral("添加任务"), tbox);
    QPushButton *bDelT = new QPushButton(QStringLiteral("删除选中"), tbox);
    tbar->addWidget(new QLabel(QStringLiteral("场景模板"), tbox));
    m_scenario = new QComboBox(tbox);
    for (const RtosGen::ScenarioSpec &s : RtosGen::scenarios())
        m_scenario->addItem(s.name);
    QPushButton *bApply = new QPushButton(QStringLiteral("套用"), tbox);
    bApply->setToolTip(QStringLiteral("用该场景替换当前的任务与资源清单"));
    QPushButton *bImport = new QPushButton(QStringLiteral("从起手助手导入"), tbox);
    bImport->setToolTip(QStringLiteral("读起手助手「任务与中断划分」的任务表；"
                                       "约定：任务名 / 周期ms（事件填 0） / 优先级 / 栈(字) / 职责"));
    tbar->addWidget(m_scenario);
    tbar->addWidget(bApply);
    tbar->addWidget(bImport);
    tbar->addWidget(bAddT);
    tbar->addWidget(bDelT);
    tbar->addStretch(1);
    tl->addLayout(tbar);
    lay->addWidget(tbox);

    /* ---- 资源清单 ---- */
    QGroupBox *rbox = new QGroupBox(QStringLiteral("资源清单（队列 / 信号量 / 互斥量 / 定时器）"), this);
    QVBoxLayout *rl = new QVBoxLayout(rbox);
    m_resTable = new QTableWidget(0, 5, rbox);
    m_resTable->setHorizontalHeaderLabels({QStringLiteral("类型"), QStringLiteral("变量名"),
                                           QStringLiteral("数据项类型或回调"),
                                           QStringLiteral("参数"), QStringLiteral("备注")});
    m_resTable->horizontalHeader()->setStretchLastSection(true);
    m_resTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    rl->addWidget(m_resTable);
    QHBoxLayout *rbar = new QHBoxLayout;
    QPushButton *bAddR = new QPushButton(QStringLiteral("添加资源"), rbox);
    QPushButton *bDelR = new QPushButton(QStringLiteral("删除选中"), rbox);
    QPushButton *bChain = new QPushButton(QStringLiteral("按任务串联资源"), rbox);
    bChain->setToolTip(QStringLiteral("按任务顺序给相邻任务之间建队列，并双向写入关联资源"));
    rbar->addWidget(bAddR);
    rbar->addWidget(bDelR);
    rbar->addWidget(bChain);
    rbar->addStretch(1);
    rl->addLayout(rbar);
    lay->addWidget(rbox);

    /* ---- 参考代码段（勾选要并入的外设流程卡）----
       数据来自「单片机手册」的流程卡（引导教程板块），每张卡的正文就是一段可直接搬的
       初始化代码。首次显示本页时才去读那 4MB 数据（见 ensureFlowList）。 */
    QGroupBox *fbox = new QGroupBox(
        QStringLiteral("参考代码段（勾选要并入的外设，整段代码取自「单片机手册」流程卡）"), this);
    QVBoxLayout *fl = new QVBoxLayout(fbox);
    m_flowNote = new QLabel(QStringLiteral("首次打开本页时载入清单……"), fbox);
    m_flowNote->setWordWrap(true);
    m_flowNote->setStyleSheet(QStringLiteral("color:#555555;"));
    fl->addWidget(m_flowNote);
    m_flowList = new QListWidget(fbox);
    m_flowList->setFixedHeight(120);
    m_flowList->setToolTip(QStringLiteral(
        "勾选后，生成时会把该卡的可复用代码整段拼进 .c 的参考区（按卡号排序）。\n"
        "卡里的 #include 与 main() 会被去掉，初始化函数名会写进 main 的调用提示。"));
    fl->addWidget(m_flowList);
    lay->addWidget(fbox);

    /* ---- 选项 ---- */
    QHBoxLayout *opt = new QHBoxLayout;
    opt->addWidget(new QLabel(QStringLiteral("入口函数"), this));
    m_funcEdit = new QLineEdit(QStringLiteral("main"), this);
    m_funcEdit->setFixedWidth(90);
    opt->addWidget(m_funcEdit);
    m_until = new QCheckBox(QStringLiteral("周期任务用 vTaskDelayUntil"), this);
    m_until->setChecked(true);
    opt->addWidget(m_until);
    m_wdg = new QCheckBox(QStringLiteral("生成喂狗占位"), this);
    opt->addWidget(m_wdg);
    opt->addWidget(new QLabel(QStringLiteral("输出编码"), this));
    m_enc = new QComboBox(this);
    m_enc->addItem(QStringLiteral("GBK（main.c 口径）"));
    m_enc->addItem(QStringLiteral("UTF-8"));
    opt->addWidget(m_enc);
    opt->addWidget(new QLabel(QStringLiteral("文件基名"), this));
    m_baseEdit = new QLineEdit(QStringLiteral("app_tasks"), this);
    m_baseEdit->setFixedWidth(110);
    m_baseEdit->setToolTip(QStringLiteral("生成的 .h/.c 文件名基名，建议用 ASCII"));
    opt->addWidget(m_baseEdit);
    m_split = new QCheckBox(QStringLiteral("生成 .h"), this);
    m_split->setChecked(true);
    m_split->setToolTip(QStringLiteral("同时生成头文件（句柄 extern、任务原型）"));
    opt->addWidget(m_split);
    m_crlf = new QCheckBox(QStringLiteral("CRLF 换行"), this);
    opt->addWidget(m_crlf);
    opt->addStretch(1);
    lay->addLayout(opt);

    QHBoxLayout *opt2 = new QHBoxLayout;
    opt2->addWidget(new QLabel(QStringLiteral("额外 include（一行一个，可留空）"), this));
    m_includes = new QPlainTextEdit(this);
    m_includes->setPlaceholderText(QStringLiteral("sys_clock.h\nsys_usart.h"));
    m_includes->setFixedHeight(52);
    opt2->addWidget(m_includes, 1);
    lay->addLayout(opt2);

    /* ---- 动作 ---- */
    QHBoxLayout *bar = new QHBoxLayout;
    QPushButton *bVal  = new QPushButton(QStringLiteral("校验"), this);
    QPushButton *bGen  = new QPushButton(QStringLiteral("生成骨架"), this);
    QPushButton *bCopy = new QPushButton(QStringLiteral("复制代码"), this);
    QPushButton *bCopyCfg = new QPushButton(QStringLiteral("复制 FreeRTOSConfig.h"), this);
    QPushButton *bSave = new QPushButton(QStringLiteral("保存 main.c"), this);
    bCopyCfg->setToolTip(QStringLiteral("复制下方那份完整 FreeRTOSConfig.h（每个宏都带取值理由）"));
    bSave->setToolTip(QStringLiteral("保存 main.c（勾了「生成 .h」连 .h 一起存），"
                                     "同时把完整 FreeRTOSConfig.h 落到同一目录"));
    bar->addWidget(bVal);
    bar->addWidget(bGen);
    bar->addWidget(bCopy);
    bar->addWidget(bCopyCfg);
    bar->addWidget(bSave);
    bar->addStretch(1);
    lay->addLayout(bar);

    lay->addWidget(new QLabel(QStringLiteral("校验结果"), this));
    m_issues = new QPlainTextEdit(this);
    m_issues->setReadOnly(true);
    m_issues->setFixedHeight(90);
    lay->addWidget(m_issues);

    lay->addWidget(new QLabel(QStringLiteral("FreeRTOSConfig.h（完整生成物：每个宏都带取值理由；"
                                             "末尾附该放哪些 port 文件）"), this));
    m_config = new QPlainTextEdit(this);
    m_config->setReadOnly(true);
    m_config->setFixedHeight(220);
    QFont cfgMono(QStringLiteral("Consolas"));
    cfgMono.setStyleHint(QFont::Monospace);
    m_config->setFont(cfgMono);
    lay->addWidget(m_config);

    lay->addWidget(new QLabel(QStringLiteral("骨架预览"), this));
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_preview->setFont(mono);
    lay->addWidget(m_preview, 1);

    connect(bBrowse, &QPushButton::clicked, this, &RtosGenPage::browseConfig);
    connect(bParse,  &QPushButton::clicked, this, &RtosGenPage::parseConfig);
    connect(bB1,     &QPushButton::clicked, this, &RtosGenPage::useBoard1);
    connect(bB2,     &QPushButton::clicked, this, &RtosGenPage::useBoard2);
    connect(bAddT,   &QPushButton::clicked, this, &RtosGenPage::addTask);
    connect(bDelT,   &QPushButton::clicked, this, &RtosGenPage::delTask);
    connect(bApply,  &QPushButton::clicked, this, [this]() { applyScenario(m_scenario->currentIndex()); });
    connect(bImport, &QPushButton::clicked, this, &RtosGenPage::importFromKickstart);
    connect(bAddR,   &QPushButton::clicked, this, &RtosGenPage::addRes);
    connect(bDelR,   &QPushButton::clicked, this, &RtosGenPage::delRes);
    connect(bChain,  &QPushButton::clicked, this, [this]() { chainResources(); });
    connect(m_taskTable, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *) { doValidate(); });
    connect(m_resTable, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *) { doValidate(); });
    connect(bVal,    &QPushButton::clicked, this, &RtosGenPage::doValidate);
    connect(bGen,    &QPushButton::clicked, this, &RtosGenPage::doGenerate);
    connect(bCopy,   &QPushButton::clicked, this, &RtosGenPage::doCopy);
    connect(bCopyCfg, &QPushButton::clicked, this, &RtosGenPage::doCopyConfig);
    connect(bSave,   &QPushButton::clicked, this, &RtosGenPage::doSave);

    addTaskRow(RtosGen::TaskSpec{QStringLiteral("Sensor"), QStringLiteral("周期"), 2000, 3, 256,
                                 QStringLiteral("读传感器，写采集队列"), {}, {}, 0});
    addResRow(RtosGen::ResSpec{QStringLiteral("queue"), QStringLiteral("s_q_sensor"),
                               QStringLiteral("SensorMsg_t"), 4, QStringLiteral("采集到处理")});
}

void RtosGenPage::setEnvMonitorRoot(const QString &root)
{
    m_envRoot = root;
}

void RtosGenPage::setKickstartProvider(std::function<QString(const QString &)> provider)
{
    m_kickProvider = provider;
}

void RtosGenPage::addTaskRow(const RtosGen::TaskSpec &t)
{
    const int r = m_taskTable->rowCount();
    m_taskTable->insertRow(r);
    m_taskTable->setItem(r, 0, textItem(t.name));
    m_taskTable->setCellWidget(r, 1, makeKindCombo(TASK_KINDS, 3, t.kind, m_taskTable));
    m_taskTable->setItem(r, 2, textItem(t.kind == QStringLiteral("周期") ? QString::number(t.periodMs)
                                                                          : QStringLiteral("0")));
    m_taskTable->setItem(r, 3, textItem(QString::number(t.prio)));
    m_taskTable->setItem(r, 4, textItem(QString::number(t.stackWords)));
    m_taskTable->setItem(r, 5, textItem(t.duty));
    m_taskTable->setItem(r, 6, textItem(t.links.join(QStringLiteral(", "))));
    m_taskTable->setItem(r, 7, textItem(t.apiHint));
    m_taskTable->setItem(r, 8, textItem(QString::number(t.wcetUs)));
}

void RtosGenPage::addResRow(const RtosGen::ResSpec &r)
{
    const int row = m_resTable->rowCount();
    m_resTable->insertRow(row);
    m_resTable->setCellWidget(row, 0, makeKindCombo(RES_KINDS, 6, r.kind, m_resTable));
    m_resTable->setItem(row, 1, textItem(r.name));
    m_resTable->setItem(row, 2, textItem(r.itemType));
    m_resTable->setItem(row, 3, textItem(QString::number(r.param)));
    m_resTable->setItem(row, 4, textItem(r.note));
}

QVector<RtosGen::TaskSpec> RtosGenPage::readTasks() const
{
    QVector<RtosGen::TaskSpec> v;
    for (int r = 0; r < m_taskTable->rowCount(); ++r) {
        RtosGen::TaskSpec t;
        if (auto *it = m_taskTable->item(r, 0))
            t.name = it->text().trimmed();
        if (auto *c = qobject_cast<QComboBox *>(m_taskTable->cellWidget(r, 1)))
            t.kind = c->currentData().toString();
        if (auto *it = m_taskTable->item(r, 2))
            t.periodMs = it->text().toInt();
        if (auto *it = m_taskTable->item(r, 3))
            t.prio = it->text().toInt();
        if (auto *it = m_taskTable->item(r, 4))
            t.stackWords = it->text().toInt();
        if (auto *it = m_taskTable->item(r, 5))
            t.duty = it->text().trimmed();
        if (auto *it = m_taskTable->item(r, 6)) {
            const QStringList parts = it->text().split(
                QRegularExpression(QStringLiteral("[,，、\\s]+")), Qt::SkipEmptyParts);
            for (const QString &p : parts)
                t.links << p.trimmed();
        }
        if (auto *it = m_taskTable->item(r, 7))
            t.apiHint = it->text().trimmed();
        if (auto *it = m_taskTable->item(r, 8))
            t.wcetUs = it->text().toInt();
        if (!t.name.isEmpty())
            v << t;
    }
    return v;
}

QVector<RtosGen::ResSpec> RtosGenPage::readResources() const
{
    QVector<RtosGen::ResSpec> v;
    for (int r = 0; r < m_resTable->rowCount(); ++r) {
        RtosGen::ResSpec s;
        if (auto *c = qobject_cast<QComboBox *>(m_resTable->cellWidget(r, 0)))
            s.kind = c->currentData().toString();
        if (auto *it = m_resTable->item(r, 1))
            s.name = it->text().trimmed();
        if (auto *it = m_resTable->item(r, 2))
            s.itemType = it->text().trimmed();
        if (auto *it = m_resTable->item(r, 3))
            s.param = it->text().toInt();
        if (auto *it = m_resTable->item(r, 4))
            s.note = it->text().trimmed();
        if (!s.name.isEmpty())
            v << s;
    }
    return v;
}

RtosGen::GenOptions RtosGenPage::readOptions() const
{
    RtosGen::GenOptions o;
    o.funcName = m_funcEdit->text().trimmed().isEmpty() ? QStringLiteral("main")
                                                        : m_funcEdit->text().trimmed();
    o.useDelayUntil = m_until->isChecked();
    o.feedWdg = m_wdg->isChecked();
    o.includes = m_includes->toPlainText();
    o.splitHeader = m_split->isChecked();
    o.baseName = m_baseEdit->text().trimmed().isEmpty() ? QStringLiteral("app_tasks")
                                                        : m_baseEdit->text().trimmed();
    o.crlf = m_crlf->isChecked();
    /* 勾选的流程卡（名称）。顺序交给生成器按卡号重排，这里保持勾选顺序即可。 */
    if (m_flowList) {
        for (int i = 0; i < m_flowList->count(); ++i) {
            QListWidgetItem *it = m_flowList->item(i);
            if (it && it->checkState() == Qt::Checked)
                o.flowPicks << it->text();
        }
    }
    return o;
}

/* 首次显示本页时才去读速查数据（4MB + 建索引）。
   工具箱启动时要快，所以不在构造函数里读——那会让每次开工具箱都卡一下。 */
void RtosGenPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    ensureFlowList();
}

void RtosGenPage::ensureFlowList()
{
    if (m_flowLoaded || !m_flowList)
        return;
    m_flowLoaded = true;

    QString err;
    if (!LookupData::load(&err)) {
        m_flowNote->setText(QStringLiteral("读不到「单片机手册」的数据，本区不可用：%1").arg(err));
        m_flowNote->setStyleSheet(QStringLiteral("color:#b4552d;"));
        m_flowList->setEnabled(false);
        return;
    }
    const QVector<LookupData::FlowHit> flows = LookupData::findFlows(QString(), 64);
    for (const LookupData::FlowHit &h : flows) {
        QListWidgetItem *it = new QListWidgetItem(h.name, m_flowList);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Unchecked);
        it->setToolTip(h.desc);
    }
    m_flowNote->setText(QStringLiteral(
        "数据来自 %1（流程卡 %2 张）。勾选后生成时整段并入；卡里的 #include 与 main() 会被去掉。")
                            .arg(LookupData::path()).arg(flows.size()));
}

void RtosGenPage::browseConfig()
{
    const QString p = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 FreeRTOSConfig.h"), m_cfgEdit->text(),
        QStringLiteral("FreeRTOSConfig.h (*.h);;所有文件 (*)"));
    if (p.isEmpty())
        return;
    m_cfgEdit->setText(p);
    parseConfig();
}

void RtosGenPage::parseConfig()
{
    m_cfgPath = m_cfgEdit->text().trimmed();
    m_cfg = RtosGen::parseConfig(m_cfgPath);
    showConfig();
}

void RtosGenPage::useBoard1()
{
    if (m_envRoot.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("尚未定位到仓库根。"));
        return;
    }
    m_cfgEdit->setText(QDir(m_envRoot).filePath(
        QStringLiteral("EnvMonitor/Project_1_检测端/Project/FreeRTOS/port/FreeRTOSConfig.h")));
    parseConfig();
}

void RtosGenPage::useBoard2()
{
    if (m_envRoot.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("尚未定位到仓库根。"));
        return;
    }
    m_cfgEdit->setText(QDir(m_envRoot).filePath(
        QStringLiteral("EnvMonitor/Project_2_数据通信端/Project/FreeRTOS/port/FreeRTOSConfig.h")));
    parseConfig();
}

void RtosGenPage::showConfig()
{
    if (!m_cfg.ok) {
        m_cfgInfo->setText(QStringLiteral("配置未解析成功。%1")
                               .arg(m_cfg.notes.join(QStringLiteral("；"))));
        return;
    }
    const QHash<QString, int> &v = m_cfg.ints;
    QStringList parts;
    auto add = [&](const QString &k) {
        if (v.contains(k))
            parts << QStringLiteral("%1=%2").arg(k).arg(v.value(k));
    };
    add(QStringLiteral("configMAX_PRIORITIES"));
    add(QStringLiteral("configMINIMAL_STACK_SIZE"));
    add(QStringLiteral("configTOTAL_HEAP_SIZE"));
    add(QStringLiteral("configTICK_RATE_HZ"));
    add(QStringLiteral("configUSE_MUTEXES"));
    add(QStringLiteral("configUSE_RECURSIVE_MUTEXES"));
    add(QStringLiteral("configUSE_COUNTING_SEMAPHORES"));
    add(QStringLiteral("configUSE_TIMERS"));
    QString s = QStringLiteral("已解析：%1；configASSERT=%2。")
                    .arg(parts.join(QStringLiteral(", ")),
                         m_cfg.hasAssert ? QStringLiteral("有") : QStringLiteral("无"));
    if (!m_cfg.notes.isEmpty())
        s += QStringLiteral(" 提示：") + m_cfg.notes.join(QStringLiteral("；"));
    m_cfgInfo->setText(s);
}

void RtosGenPage::addTask()
{
    addTaskRow(RtosGen::TaskSpec{QStringLiteral("Task%1").arg(m_taskTable->rowCount() + 1),
                                 QStringLiteral("周期"), 1000, 1, 256, QString(), {}, {}, 0});
}

void RtosGenPage::delTask()
{
    const int r = m_taskTable->currentRow();
    if (r >= 0)
        m_taskTable->removeRow(r);
}

void RtosGenPage::fillSample()
{
    for (const RtosGen::TaskSpec &t : RtosGen::sampleTasks())
        addTaskRow(t);
    for (const RtosGen::ResSpec &r : RtosGen::sampleResources())
        addResRow(r);
}

void RtosGenPage::addRes()
{
    addResRow(RtosGen::ResSpec{QStringLiteral("queue"),
                               QStringLiteral("s_q_%1").arg(m_resTable->rowCount() + 1),
                               QStringLiteral("uint8_t"), 4, QString()});
}

void RtosGenPage::delRes()
{
    const int r = m_resTable->currentRow();
    if (r >= 0)
        m_resTable->removeRow(r);
}

void RtosGenPage::chainResources()
{
    QVector<RtosGen::TaskSpec> tasks = readTasks();
    if (tasks.size() < 2) {
        m_issues->setPlainText(QStringLiteral("任务少于两个，无需串联。"));
        return;
    }
    const QVector<RtosGen::ResSpec> made = RtosGen::chainTasks(tasks);

    /* 回写"关联资源"列：表中有名字的行按顺序对应 tasks 里的项 */
    int idx = 0;
    for (int r = 0; r < m_taskTable->rowCount() && idx < tasks.size(); ++r) {
        auto *it = m_taskTable->item(r, 0);
        if (!it || it->text().trimmed().isEmpty())
            continue;
        m_taskTable->setItem(r, 6, textItem(tasks[idx].links.join(QStringLiteral(", "))));
        ++idx;
    }

    /* 新建的队列补进资源表；已存在同名的不重复加 */
    QStringList existing;
    for (int r = 0; r < m_resTable->rowCount(); ++r)
        if (auto *it = m_resTable->item(r, 1))
            existing << it->text().trimmed();
    int added = 0;
    for (const RtosGen::ResSpec &q : made) {
        if (existing.contains(q.name))
            continue;
        addResRow(q);
        ++added;
    }

    m_issues->setPlainText(
        QStringLiteral("已按任务顺序串联：应建 %1 条队列，实际新增 %2 条，"
                       "并把各任务的关联资源填好。数据项类型默认 uint8_t，按实际改。")
            .arg(made.size()).arg(added));
}

void RtosGenPage::applyScenario(int idx)
{
    const QVector<RtosGen::ScenarioSpec> sc = RtosGen::scenarios();
    if (idx < 0 || idx >= sc.size())
        return;
    m_taskTable->setRowCount(0);
    m_resTable->setRowCount(0);
    for (const RtosGen::TaskSpec &t : sc.at(idx).tasks)
        addTaskRow(t);
    for (const RtosGen::ResSpec &r : sc.at(idx).res)
        addResRow(r);
    m_issues->setPlainText(QStringLiteral("已套用场景：%1 —— %2")
                               .arg(sc.at(idx).name, sc.at(idx).desc));
}

void RtosGenPage::importFromKickstart()
{
    if (!m_kickProvider) {
        m_issues->setPlainText(QStringLiteral("未连接起手助手（该入口只在综合程序里可用）。"));
        return;
    }
    const QString text = m_kickProvider(QStringLiteral("tasks"));
    if (text.trimmed().isEmpty()) {
        m_issues->setPlainText(QStringLiteral(
            "起手助手里还没填「任务与中断划分 → 任务表」。\n"
            "那里按：任务名 / 周期ms（事件填 0） / 优先级 / 栈(字) / 职责，一行一个任务。"));
        return;
    }
    const QVector<RtosGen::TaskSpec> ts = RtosGen::parseTaskLines(text);
    if (ts.isEmpty()) {
        m_issues->setPlainText(QStringLiteral(
            "按约定没解析出任务。检查字段顺序与分隔符（可用 / | , 制表符）。"));
        return;
    }
    m_taskTable->setRowCount(0);
    for (const RtosGen::TaskSpec &t : ts)
        addTaskRow(t);
    m_issues->setPlainText(QStringLiteral(
        "已从起手助手导入 %1 个任务（资源清单未动，可再点「按任务串联资源」）。").arg(ts.size()));
}

void RtosGenPage::doValidate()
{
    /* 把勾选的流程卡一并交给校验：跨卡冲突（同一 IRQn / 同一脚位 / 抢占值不一致）
       属于"生成前就该拦住"的问题，只校验任务/资源清单是查不出来的。 */
    const QStringList issues = RtosGen::validate(m_cfg, readTasks(), readResources(),
                                                 readOptions().flowPicks);
    QStringList out;
    if (!m_cfg.ok)
        out << QStringLiteral("注意：配置未解析，配置类校验已跳过。");
    out << (issues.isEmpty() ? QStringLiteral("未发现问题。") : issues.join(QStringLiteral("\n")));
    m_issues->setPlainText(out.join(QStringLiteral("\n")));
}

void RtosGenPage::doGenerate()
{
    doValidate();

    /* 工程名：从 FreeRTOSConfig.h 路径上溯，找到名为 Project 的目录，取其上一层 */
    const QString cfgPath = !m_cfgPath.isEmpty() ? m_cfgPath : m_cfgEdit->text().trimmed();
    QString proj;
    if (!cfgPath.isEmpty()) {
        QDir d = QFileInfo(cfgPath).dir();
        for (int i = 0; i < 8; ++i) {
            if (d.dirName() == QStringLiteral("Project")) {
                QDir up = d;
                if (up.cdUp())
                    proj = up.dirName();
                break;
            }
            if (!d.cdUp())
                break;
        }
    }
    if (proj.isEmpty())
        proj = m_funcEdit->text().trimmed();

    const QVector<RtosGen::TaskSpec> tk = readTasks();
    const QVector<RtosGen::ResSpec> rs = readResources();
    const RtosGen::GenOptions op = readOptions();
    m_preview->setPlainText(RtosGen::render(m_cfg, tk, rs, op, proj));
    /* 另一件生成物：完整 FreeRTOSConfig.h（逐宏给出取值理由 + port 文件清单）。
       不再只给"要改的那几条"（configSnippet 仍保留，供只关心增量时调用）。 */
    m_config->setPlainText(RtosGen::renderFreeRTOSConfig(m_cfg, tk, rs, proj));
}

void RtosGenPage::doCopy()
{
    if (m_preview->toPlainText().isEmpty()) {
        doGenerate();
        if (m_preview->toPlainText().isEmpty())
            return;
    }
    QApplication::clipboard()->setText(m_preview->toPlainText());
}

void RtosGenPage::doCopyConfig()
{
    if (m_config->toPlainText().isEmpty()) {
        doGenerate();
        if (m_config->toPlainText().isEmpty())
            return;
    }
    QApplication::clipboard()->setText(m_config->toPlainText());
}

void RtosGenPage::doSave()
{
    if (m_preview->toPlainText().isEmpty())
        doGenerate();
    const QString code = m_preview->toPlainText();
    if (code.isEmpty())
        return;

    const RtosGen::GenOptions op = readOptions();
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存生成的源文件"),
        op.baseName + QStringLiteral(".c"), QStringLiteral("C 源文件 (*.c)"));
    if (path.isEmpty())
        return;

    QStringList written;
    if (writeGenerated(path, code))
        written << path;

    if (op.splitHeader) {
        QString stem = path;
        if (stem.endsWith(QStringLiteral(".c"), Qt::CaseInsensitive))
            stem.chop(2);
        const QString hpath = stem + QStringLiteral(".h");
        const QString hcode = RtosGen::renderHeader(m_cfg, readTasks(), readResources(), op, QString());
        if (writeGenerated(hpath, hcode))
            written << hpath;
    }

    /* 完整 FreeRTOSConfig.h 是另一件生成物，落到同一目录让"生成一次就齐了"。
       已存在就不覆盖 —— 那多半是你工程里真在用的配置，冲掉比不生成更糟。 */
    QString cfgNote;
    const QString cfgText = m_config->toPlainText();
    if (!cfgText.isEmpty()) {
        const QString cfgPath = QFileInfo(path).dir().filePath(QStringLiteral("FreeRTOSConfig.h"));
        if (QFile::exists(cfgPath))
            cfgNote = QStringLiteral("\n\n%1 已存在，未覆盖：它是你工程在用的配置，"
                                     "请按生成物里的宏值/理由自行并入。").arg(cfgPath);
        else if (writeGenerated(cfgPath, cfgText))
            written << cfgPath;
    }

    if (written.isEmpty())
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("没能写出文件。"));
    else
        QMessageBox::information(
            this, QStringLiteral("已保存"),
            written.join(QStringLiteral("\n"))
                + QStringLiteral("\n\n若目标文件已存在，其中 USER CODE 保护区的内容已保留。")
                + cfgNote);
}

/* 按所选编码写出；若目标已存在，先把 USER CODE 保护区的内容合并进来。
   返回是否写成功。 */
bool RtosGenPage::writeGenerated(const QString &path, const QString &text) const
{
    const QStringConverter::Encoding enc = (m_enc->currentIndex() == 1)
                                               ? QStringConverter::Utf8
                                               : QStringConverter::System;
    QString oldText;
    if (QFile::exists(path)) {
        QFile rf(path);
        if (rf.open(QIODevice::ReadOnly)) {
            const QByteArray raw = rf.readAll();
            rf.close();
            QStringDecoder dec(enc);
            const QString s = dec(raw);
            if (!dec.hasError())
                oldText = s;
        }
    }

    const QString merged = RtosGen::mergeUserCode(text, oldText);
    const QString finalText = RtosGen::withLineEndings(merged, readOptions().crlf);

    QStringEncoder encoder(enc);
    QByteArray bytes = encoder(finalText);
    if (encoder.hasError() || bytes.isEmpty())
        bytes = finalText.toUtf8();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const qint64 n = f.write(bytes);
    f.close();
    return n == bytes.size();
}
