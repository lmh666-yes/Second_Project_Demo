#include "cockpitpage.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QVariant>

#include "dashboardcanvas.h"
#include "overviewpage.h"
#include "pyfind.h"
#include "toolrunpage.h"

namespace {

/* 卡片的顺序（与 .h 里的说明一致）：0 阶段 / 1 体检 / 2 体积 / 3 覆盖 / 4 待办桩。
   `data` 里给的键 → 卡下标；「体检结论」不在 data 里（它接总览页的数据）。 */
int cardIndex(const QString &key)
{
    if (key == QLatin1String("stage"))
        return 0;
    if (key == QLatin1String("size"))
        return 2;
    if (key == QLatin1String("cover"))
        return 3;
    if (key == QLatin1String("todo"))
        return 4;
    return -1;
}

/* 脚本输出的最后一行 JSON（各脚本都这么约定：人读的文本在前、JSON 收尾）。 */
QJsonObject lastJsonObject(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QString s = lines.at(i).trimmed();
        if (s.isEmpty())
            continue;
        QJsonParseError err{};
        const QJsonDocument d = QJsonDocument::fromJson(s.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && d.isObject())
            return d.object();
    }
    return {};
}

QFrame *makeCard(QLabel *&title, QLabel *&value, QLabel *&sub)
{
    QFrame *card = new QFrame;
    card->setObjectName(QStringLiteral("cockpitCard"));
    card->setFrameShape(QFrame::StyledPanel);
    QHBoxLayout *h = new QHBoxLayout(card);
    /* 边距/间距跟全界面统一，并把卡片压紧一点 —— 五张卡 + 运行器叠起来要能在
       1024×768 的小屏里放得下（--ui-audit 的"页最小尺寸 ≤740×654"那条线）。 */
    h->setContentsMargins(8, 4, 8, 4);
    h->setSpacing(10);

    title = new QLabel;
    title->setFixedWidth(96);
    QFont tf = title->font();
    tf.setBold(true);
    title->setFont(tf);

    value = new QLabel;
    value->setWordWrap(true);
    QFont vf = value->font();
    vf.setPointSize(vf.pointSize() + 1);
    vf.setBold(true);
    value->setFont(vf);

    sub = new QLabel;
    sub->setWordWrap(true);
    sub->setStyleSheet(QStringLiteral("color:#8a8a8a;"));

    QVBoxLayout *v = new QVBoxLayout;
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    v->addWidget(value);
    v->addWidget(sub);

    h->addWidget(title, 0, Qt::AlignTop);
    h->addLayout(v, 1);
    return card;
}

}   // namespace

CockpitPage::CockpitPage(const QString &toolsDir, QWidget *parent)
    : QWidget(parent), m_toolsDir(toolsDir)
{
    QVBoxLayout *root = new QVBoxLayout(this);
    /* 9px 边距 = 全界面统一值（--ui-audit 会断言"页左外边距只许一种"）。 */
    root->setContentsMargins(9, 9, 9, 9);
    root->setSpacing(6);

    QLabel *head = new QLabel(QStringLiteral("项目驾驶舱"));
    QFont hf = head->font();
    hf.setPointSize(hf.pointSize() + 3);
    hf.setBold(true);
    head->setFont(hf);
    root->addWidget(head);

    QLabel *desc = new QLabel(
        QStringLiteral("一屏看「现在到哪、还差什么、下一步点哪」。五张卡的数字都是**现场取**"
                       "（阶段判定 / 体积历史 / 平台清单 / 进度扫描），体检结论与「工程体检总览」同源；"
                       "下面的按钮点了真跑，输出在本页底部，跑完卡片自动刷新。"));
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    root->addWidget(desc);

    /* 五张卡：顺序固定，值由取数结果填（体检结论那张由总览页数据填）。 */
    const QStringList titles = {QStringLiteral("阶段进度"), QStringLiteral("体检结论"),
                                QStringLiteral("体积趋势"), QStringLiteral("自检覆盖"),
                                QStringLiteral("待办与桩")};
    for (const QString &t : titles) {
        QLabel *tt = nullptr, *vv = nullptr, *ss = nullptr;
        root->addWidget(makeCard(tt, vv, ss));
        tt->setText(t);
        vv->setText(QStringLiteral("取数中…"));
        ss->setText(QString());
        m_cardTitle << tt;
        m_cardValue << vv;
        m_cardSub << ss;
    }

    /* 「下一步点哪」横幅 —— C2 要回答的正是这一句。 */
    m_next = new QLabel;
    m_next->setWordWrap(true);
    QFont nf = m_next->font();
    nf.setBold(true);
    m_next->setFont(nf);
    root->addWidget(m_next);

    /* 动作行：点了真跑。 */
    {
        QHBoxLayout *row = new QHBoxLayout;
        row->setSpacing(6);
        m_btnStage = new QPushButton(QStringLiteral("跑阶段验收"));
        m_btnStage->setToolTip(QStringLiteral("调 build_flash.py --stage-run N --board 板X"
                                              "（阶段号与板名来自上面的阶段进度卡）"));
        row->addWidget(m_btnStage);

        QPushButton *bHealth = new QPushButton(QStringLiteral("刷体检"));
        bHealth->setToolTip(QStringLiteral("让「工程体检总览」重跑一轮取数，本页结论跟着更新"));
        row->addWidget(bHealth);

        QPushButton *bReport = new QPushButton(QStringLiteral("出报告"));
        bReport->setToolTip(QStringLiteral("report_gen.py 出单文件 HTML 周报（写 检查/报告/产物/周报.html）"));
        row->addWidget(bReport);

        QPushButton *bVerify = new QPushButton(QStringLiteral("跑一键验收·快档"));
        bVerify->setToolTip(QStringLiteral("verify_all.py --no-build：跳构建（工具箱正开着时改不动自己的 exe）"));
        row->addWidget(bVerify);

        QPushButton *bCatalog = new QPushButton(QStringLiteral("去平台清单"));
        bCatalog->setToolTip(QStringLiteral("看逐项明细：入口 / 自检断言数 / 进闸门 / 变异覆盖"));
        row->addWidget(bCatalog);

        row->addStretch(1);
        root->addLayout(row);

        m_actions = {m_btnStage->text(), bHealth->text(), bReport->text(),
                     bVerify->text(), bCatalog->text()};

        connect(m_btnStage, &QPushButton::clicked, this, [this]() {
            if (m_stageArgv.size() < 2)
                return;
            startRun(QStringLiteral("阶段验收"), m_stageArgv.first(), m_stageArgv.mid(1));
        });
        connect(bHealth, &QPushButton::clicked, this, [this]() {
            if (!m_overview) {
                setStatus(QStringLiteral("本页没接上「工程体检总览」，刷不了体检 —— "
                                         "去「汇总 → 工程体检总览」点「刷新」也一样。"));
                return;
            }
            m_cardValue[1]->setText(QStringLiteral("体检取数中…"));
            QMetaObject::invokeMethod(m_overview, "refresh", Qt::QueuedConnection);
            setStatus(QStringLiteral("已让「工程体检总览」重跑一轮体检 —— 跑完本页结论自动更新。"));
        });
        connect(bReport, &QPushButton::clicked, this, [this]() {
            startRun(QStringLiteral("报告生成"),
                     QDir(m_toolsDir).filePath(QStringLiteral("检查/报告/report_gen.py")),
                     QStringList{QStringLiteral("--out"), QStringLiteral("产物/周报.html")});
        });
        connect(bVerify, &QPushButton::clicked, this, [this]() {
            startRun(QStringLiteral("一键验收 · 快档"),
                     QDir(m_toolsDir).filePath(QStringLiteral("检查/一键验收/verify_all.py")),
                     QStringList{QStringLiteral("--no-build")});
        });
        connect(bCatalog, &QPushButton::clicked, this,
                [this]() { emit requestPage(QStringLiteral("平台清单")); });
    }

    m_status = new QLabel;
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    /* 动作输出：内嵌一个运行器页（跑/停/复制/保存都是现成的）。 */
    m_run = new ToolRunPage(QStringLiteral("动作输出"),
                            QStringLiteral("驾驶舱上的按钮跑起来，输出都在这儿 —— "
                                           "跑完上面的卡片会自动刷新。"), this);
    m_run->setFinishedHook([this]() {
        refresh();
        return QStringLiteral("已重新取数并刷新上面的卡片。");
    });
    root->addWidget(m_run, 1);

    updateHealthCard();     /* 先把"没接/未取数"的实话摆上，别留空 */
}

CockpitPage::~CockpitPage()
{
    /* 取数进程还在跑就收干净（与总览页同一条约定：不收会在退出时以堆损坏结束）。 */
    if (m_proc) {
        m_proc->disconnect();
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();
            m_proc->waitForFinished(2000);
        }
    }
}

void CockpitPage::setOverview(OverviewPage *ov)
{
    if (m_overview == ov)
        return;
    if (m_overview)
        disconnect(m_overview, nullptr, this, nullptr);
    m_overview = ov;
    if (m_overview)
        connect(m_overview, &OverviewPage::dataRefreshed, this, &CockpitPage::onOverviewData);
    updateHealthCard();
}

bool CockpitPage::ready() const
{
    /* 四张现场卡已渲染 + 「体检结论」是真结论（不是"取数中 / 尚未取数 / 没接上总览页"）。 */
    return m_loaded && m_cardValue.at(1)->text().startsWith(QStringLiteral("本轮体检"));
}

QStringList CockpitPage::cardTitles() const
{
    QStringList out;
    for (QLabel *l : m_cardTitle)
        out << l->text();
    return out;
}

QStringList CockpitPage::cardValues() const
{
    QStringList out;
    for (QLabel *l : m_cardValue)
        out << l->text();
    return out;
}

QStringList CockpitPage::cardSubs() const
{
    QStringList out;
    for (QLabel *l : m_cardSub)
        out << l->text();
    return out;
}

QStringList CockpitPage::cardLines() const
{
    QStringList out;
    for (int i = 0; i < m_cardTitle.size(); ++i)
        out << QStringLiteral("%1｜%2｜%3").arg(m_cardTitle.at(i)->text(),
                                                m_cardValue.at(i)->text(),
                                                m_cardSub.at(i)->text());
    return out;
}

QString CockpitPage::healthLine() const
{
    return m_cardValue.at(1)->text();
}

QString CockpitPage::healthFindings() const
{
    return m_cardSub.at(1)->text();
}

QStringList CockpitPage::actionLabels() const
{
    QStringList out;
    out << m_btnStage->text() << m_actions.mid(1);
    return out;
}

bool CockpitPage::stageRunEnabled() const
{
    return m_btnStage->isEnabled();
}

void CockpitPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    /* 懒加载：首次进本页才取数（构造全部页面是冷启动的大头，别在构造里起进程）。 */
    if (!m_started) {
        m_started = true;
        refresh();
    }
}

void CockpitPage::setStatus(const QString &text)
{
    m_status->setText(text);
}

void CockpitPage::refresh()
{
    startData();
    /* 体检结论接总览页的数据：总览页还没取过数就顺手让它跑一轮（用户不必先想到去点它）。 */
    if (m_overview && m_overview->toolData().isEmpty())
        QMetaObject::invokeMethod(m_overview, "refresh", Qt::QueuedConnection);
    updateHealthCard();
}

void CockpitPage::startData()
{
    const QStringList py = PyFind::detect();
    if (py.isEmpty()) {
        m_cardValue[0]->setText(QStringLiteral("没找到 Python —— 取不了数"));
        return;
    }
    const QString script =
        QDir(m_toolsDir).filePath(QStringLiteral("检查/驾驶舱/cockpit_data.py"));
    if (!QFileInfo::exists(script)) {
        m_cardValue[0]->setText(QStringLiteral("缺 检查/驾驶舱/cockpit_data.py —— 取不了数"));
        return;
    }
    if (m_proc) {
        m_proc->disconnect();
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();
            m_proc->waitForFinished(1000);
        }
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    ++m_gen;
    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(QFileInfo(script).absolutePath());
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) { onDataFinished(); });
    QStringList args = py.mid(1);
    args << script << QStringLiteral("--json");
    m_proc->start(py.value(0), args);
}

void CockpitPage::onDataFinished()
{
    if (!m_proc)
        return;
    const QString out = QString::fromUtf8(m_proc->readAll());
    const QJsonObject j = lastJsonObject(out);
    if (j.isEmpty()) {
        m_cardValue[0]->setText(QStringLiteral("取数失败：驾驶舱取数脚本没吐出 JSON"));
        m_cardSub[0]->setText(out.trimmed().right(120));
        setStatus(QStringLiteral("取数失败 —— 在命令行跑一次 检查/驾驶舱/cockpit_data.py 看原因。"));
        m_loaded = true;
        return;
    }
    renderCards(j);
    rebuildActions(j);
    m_loaded = true;
    const QStringList notes = j.value(QStringLiteral("notes")).toVariant().toStringList();
    setStatus(notes.isEmpty()
                  ? QStringLiteral("取数完成（四张卡都是刚跑出来的现场值）。")
                  : QStringLiteral("取数完成，但有取不到的路：") + notes.join(QStringLiteral("；")));
}

void CockpitPage::renderCards(const QJsonObject &j)
{
    for (const QJsonValue &cv : j.value(QStringLiteral("cards")).toArray()) {
        const QJsonObject c = cv.toObject();
        const int i = cardIndex(c.value(QStringLiteral("key")).toString());
        if (i < 0)
            continue;
        m_cardValue[i]->setText(c.value(QStringLiteral("value")).toString());
        m_cardSub[i]->setText(c.value(QStringLiteral("sub")).toString());
    }
    m_nextAction = j.value(QStringLiteral("next_action")).toString();
    m_next->setText(QStringLiteral("下一步点哪：") + m_nextAction);
}

void CockpitPage::rebuildActions(const QJsonObject &j)
{
    const QJsonObject st = j.value(QStringLiteral("stage")).toObject();
    const bool runnable = st.value(QStringLiteral("runnable")).toBool();
    const int stage = st.value(QStringLiteral("next_stage")).toInt(-1);
    const QString board = st.value(QStringLiteral("next_board")).toString();

    m_stageArgv.clear();
    m_btnStage->setEnabled(runnable);
    if (runnable && stage >= 0 && !board.isEmpty()) {
        m_stageArgv << QDir(m_toolsDir).filePath(QStringLiteral("检查/构建烧录/build_flash.py"))
                    << QStringLiteral("--stage-run") << QString::number(stage)
                    << QStringLiteral("--board") << board;
        m_btnStage->setText(QStringLiteral("跑阶段验收（%1 阶段 %2）").arg(board).arg(stage));
    } else {
        /* 如实：没有能机器判的阶段就**不给点**，而不是给一个必然失败的按钮。 */
        m_btnStage->setText(QStringLiteral("跑阶段验收（暂时没有能机器判的阶段）"));
    }
}

void CockpitPage::updateHealthCard()
{
    if (!m_overview) {
        m_cardValue[1]->setText(QStringLiteral("本页没接上「工程体检总览」"));
        m_cardSub[1]->setText(QStringLiteral("（点「刷体检」也刷不动；请从工具箱主窗口进本页）"));
        return;
    }
    const QVariantMap tools = m_overview->toolData();
    if (tools.isEmpty()) {
        m_cardValue[1]->setText(QStringLiteral("尚未取数 —— 点「刷体检」跑一轮体检"));
        m_cardSub[1]->setText(QStringLiteral("（数据与「工程体检总览」同源，不另跑一套检查器）"));
        return;
    }
    QString head;
    const QVector<DashboardCanvas::Finding> fs =
        DashboardCanvas::findingsFor(tools, m_overview->failedKeys(), &head);
    m_cardValue[1]->setText(head);
    const QString rounds = QStringLiteral("快照 %1 轮（明细看「工程体检总览」页）")
                                .arg(m_overview->snapshotRounds());
    if (fs.isEmpty()) {
        m_cardSub[1]->setText(QStringLiteral("本轮没有该先看的问题项｜") + rounds);
    } else {
        QStringList top;
        for (int i = 0; i < fs.size() && i < 3; ++i)
            top << QStringLiteral("%1 %2（%3）").arg(i + 1).arg(fs.at(i).name, fs.at(i).why);
        m_cardSub[1]->setText(QStringLiteral("最该先看：") + top.join(QStringLiteral("｜"))
                              + QStringLiteral("｜") + rounds);
    }
}

void CockpitPage::onOverviewData()
{
    updateHealthCard();
}

void CockpitPage::startRun(const QString &label, const QString &script, const QStringList &args)
{
    if (!QFileInfo::exists(script)) {
        setStatus(QStringLiteral("找不到要跑的脚本：%1").arg(script));
        return;
    }
    m_run->setSteps({RunStep{label, script, [args]() { return args; }}});
    setStatus(QStringLiteral("已开始跑「%1」—— 输出在下面，跑完卡片会自动刷新。").arg(label));
    QMetaObject::invokeMethod(m_run, "run", Qt::QueuedConnection);
}
