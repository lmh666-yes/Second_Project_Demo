#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "consistencytest.h"
#include "physrange.h"
#include "thresholddialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>

/* 6 个物理量的曲线/文字配色（顺序 = Series） */
static const char *kColors[int(Series::Count)] = {
    "#c0392b",   /* 温度 红 */
    "#2874a6",   /* 湿度 蓝 */
    "#1e8449",   /* 气压 绿 */
    "#b9770e",   /* 光照 橙 */
    "#7d3c98",   /* TVOC 紫 */
    "#707b7c"    /* MQ-135 灰 */
};

/* 各自 Y 轴的固定量程（量纲不同，必须一根轴一条曲线） */
static const double kYMin[int(Series::Count)] = { -10.0, 0.0, 950.0,   0.0,    0.0,    0.0 };
static const double kYMax[int(Series::Count)] = {  50.0, 100.0, 1050.0, 5000.0, 2000.0, 4095.0 };

/* 数据序号轴刻度步长候选。曲线横坐标 = 数据序号（第 1 帧是 1），
 * 按当前可视跨度挑一个整数步长：点少时步长 1，多了自动升到 2/5/10/20/50...
 * 刻度标签一律是整数，不出现小数。 */
static const double kTickSteps[] = { 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0,
                                     200.0, 500.0, 1000.0, 2000.0, 5000.0 };
static const int kTickStepCount = int(sizeof(kTickSteps) / sizeof(kTickSteps[0]));

/* ---------------------------------------------------------------------------
 *  数据序号轴刻度器：横坐标就是序号本身，标签按整数显示。
 *  没有任何数据时上层把 X 范围设成 (-1, 11)，刻度从 0 起，对应"没有数据
 *  就以 0 为起点"；有数据后第一帧落在序号 1 上。
 * -------------------------------------------------------------------------*/
class SampleIndexTicker : public QCPAxisTicker
{
protected:
    double getTickStep(const QCPRange &range) Q_DECL_OVERRIDE
    {
        const double span = range.size();
        for (int i = 0; i < kTickStepCount; ++i) {
            if ((span / kTickSteps[i]) <= 10.0) return kTickSteps[i];
        }
        return kTickSteps[kTickStepCount - 1];
    }

    int getSubTickCount(double tickStep) Q_DECL_OVERRIDE
    {
        return (tickStep >= 2.0) ? 1 : 0;       /* 步长 1 时不再画次刻度 */
    }

    QString getTickLabel(double tick, const QLocale &, QChar, int) Q_DECL_OVERRIDE
    {
        return QString::number(qint64(tick >= 0.0 ? tick + 0.5 : tick - 0.5));
    }
};

/* 数值卡片的小数位统一走 DataModel::decimals()：界面、CSV、对话框共用一套，
 * 避免卡片显示 2 位而 CSV 写 1 位这类对不上的情况 */

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    /* 小屏幕保底：三栏各自套一层滚动区，而不是在外面套一层。
     * 外层滚动区让 splitter 保留自己的 minSizeHint 宽度（左栏阈值表 515 px 起），
     * 窗口一窄就整体右移，右栏被推出可视区，看起来就是"右边被遮住"。每栏单独
     * 套滚动区后，栏的最小宽度不再由内容撑开，最小尺寸下三栏都完整在场，内容
     * 放不下时各栏自己出滚动条。 */
    /* 右栏最小宽度 300：告警表 5 列（时间/项目/类型/数值/阈值，均按内容定宽 +
     * 横向滚动条）在 300 px 下仍能显示完整列头，多余宽度全部让给中间曲线区。 */
    const int kPaneMinW[3] = { 292, 470, 300 };
    for (int i = 0; i < 3; ++i) {
        QWidget *pane = ui->splitter->widget(i);
        if (pane == nullptr) continue;

        QScrollArea *paneScroll = new QScrollArea(ui->splitter);
        paneScroll->setFrameShape(QFrame::NoFrame);
        paneScroll->setWidgetResizable(true);
        paneScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        paneScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        paneScroll->setMinimumWidth(kPaneMinW[i]);
        paneScroll->setWidget(pane);            /* setWidget 把 pane 重新挂到视口里 */
        ui->splitter->insertWidget(i, paneScroll);
    }

    /* 拉伸只给中间那栏：窗口变宽时曲线图变大，左右两栏宽度基本不变 */
    ui->splitter->setStretchFactor(0, 0);
    ui->splitter->setStretchFactor(1, 1);
    ui->splitter->setStretchFactor(2, 0);
    /* 实测三栏最小宽度合计（QE 的 minimumSizeHint）1090，窗口最小宽度取 1140：
     * 中间栏 470 是卡片三列与曲线区不再被裁的实测下限；左右两栏放不下时由
     * 各自滚动条兜住，不会再挤掉中间与右栏。 */
    setMinimumSize(1140, 640);
    /* 中间栏随窗口变宽（因子 1），左右两栏保持内容宽度（因子 0） */
    for (int i = 0; i < 3; ++i) {
        QWidget *paneScroll = ui->splitter->widget(i);
        if (paneScroll != nullptr) paneScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }

    /* buildHexLog() 必须排在 setupSerialUi() 前面：setupSerialUi() 末尾调
     *  onRefreshPorts() 扫串口，那里会 logLine() 写 m_hexLog。顺序反了就是往空
     *  指针 appendPlainText，表现为双击启动即闪退（0xC0000005 访问冲突）且不报错。 */
    buildHexLog();

    /* 设备控制通道（往下发命令）。先建对象再 buildControlPanel()，
     * 因为面板里的按钮 connect 就指向它。 */
    m_dev = new DevCtl(this);

    setupSerialUi();
    setupPlot();
    buildCards();
    buildCurveChecks();
    buildThresholdPanel();
    buildControlPanel();
    buildStatPanel();
    connectAll();

    /* 表格列：5 列按内容分配固定宽度，不再让最后一列独占剩余宽度。
     * 原来用 StretchLastSection，右栏一被压窄就先挤掉"阈值"列，看起来像被遮住。 */
    ui->tblAlarm->setColumnCount(5);
    ui->tblAlarm->setHorizontalHeaderLabels(QStringList()
        << "时间" << "项目" << "类型" << "数值" << "阈值");
    ui->tblAlarm->horizontalHeader()->setStretchLastSection(false);
    ui->tblAlarm->horizontalHeader()->setMinimumSectionSize(38);
    ui->tblAlarm->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->tblAlarm->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->tblAlarm->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->tblAlarm->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->tblAlarm->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    ui->tblAlarm->verticalHeader()->setVisible(false);
    ui->tblAlarm->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tblAlarm->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tblAlarm->setWordWrap(false);
    ui->tblAlarm->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    /* 右栏纵向配额：告警表给固定上限（自带滚动条），剩余高度全给统计区。
     * 只靠 setStretchFactor 不够：告警表默认 vertical Expanding 会把统计区挤到
     * 窗口下沿之外，看起来就是"统计区不见了"。 */
    ui->verticalLayoutRight->setStretch(0, 0);
    ui->verticalLayoutRight->setStretch(1, 1);
    /* 两个刷新定时器：50 ms 刷图/卡片，1 s 刷统计/告警/静默检测 */
    m_plotTimer = new QTimer(this);
    m_plotTimer->setInterval(50);
    connect(m_plotTimer, &QTimer::timeout, this, &MainWindow::onPlotTick);
    m_plotTimer->start();

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, &MainWindow::onStatsTick);
    m_statsTimer->start();

    ui->splitter->setSizes(QList<int>() << 330 << 660 << 330);   /* 真正的分配在 showEvent 里 */

    /* 阈值/开关/曲线勾选/串口参数；没有 ini 就用出厂默认值 */
    loadConfig();
    refreshThresholdPanel();

    refreshCards();
    refreshStats();
    refreshStatPanel();
    refreshPlot();          /* 没数据也要先把时间轴刻度范围摆好 */
    statusBar()->showMessage(QStringLiteral(
        "就绪。联调：板2 把 P6 跳线跳到 SP3232 侧 → USB-TTL/DB9 接电脑 → 波特率 115200 8N1。"));
}

/* 三栏宽度：等第一次真正显示、splitter 拿到真实宽度之后再分。构造里就 setSizes()
 * 时 splitter 还是默认尺寸，传入的 330/660/350 会先被按窄宽度缩放，右栏可能被挤没。 */
void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    if (m_splitterSized)
        return;
    m_splitterSized = true;

    const int total = ui->splitter->width() > 0 ? ui->splitter->width() : this->width();
    const int right = 310;                              /* 告警记录 + 统计：够用即可 */
    const int left  = (total >= 1200) ? 330 : 296;      /* 串口 / 记录 / 设备控制 / 阈值 */
    /* 中间栏给足：曲线勾选已挪到左栏，这里只需要卡片 3 列不再被裁的最小宽度。
     *  余下的宽度全给曲线区，右边的 Y 轴已经不占了。 */
    const int mid   = qMax(470, total - left - right);
    ui->splitter->setSizes(QList<int>() << left << mid << right);

    /* 布局定下来之后再把点条的左右边距对齐主图轴矩形：轴矩形宽度由 Y 轴标签与
     * 刻度宽度决定，只有布局真正算完才准。延后一次事件循环再对齐，避免此时
     * 轴矩形宽度还是旧值。 */
    syncXStripMargins();
    QTimer::singleShot(0, this, [this]() { syncXStripMargins(); rebuildXStrip(); });
}

/* 窗口尺寸变化：主图轴矩形宽度会变，点条边距跟着重新对齐 */
void MainWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
    syncXStripMargins();
    QTimer::singleShot(0, this, [this]() { syncXStripMargins(); rebuildXStrip(); });
}

MainWindow::~MainWindow()
{
    /* TODO(阶段5)：析构时要是还在记录 CSV，先停掉它（m_csv.isRecording() → m_csv.stop()），
     *            否则会留下一个没写完整的文件。 */
    delete ui;
}

/* ============================================================================
 *  界面搭建
 * ==========================================================================*/

void MainWindow::setupSerialUi()
{
    ui->comboBaud->addItems(QStringList() << "9600" << "19200" << "38400" << "57600"
                                          << "115200" << "230400" << "460800" << "921600");
    ui->comboBaud->setCurrentText("115200");     /* 板2 UART2 默认 115200 */

    ui->comboDataBits->addItem("8", int(QSerialPort::Data8));
    ui->comboDataBits->addItem("7", int(QSerialPort::Data7));
    ui->comboDataBits->addItem("6", int(QSerialPort::Data6));
    ui->comboDataBits->addItem("5", int(QSerialPort::Data5));
    ui->comboDataBits->setCurrentIndex(0);

    ui->comboParity->addItem(QStringLiteral("无校验"), int(QSerialPort::NoParity));
    ui->comboParity->addItem(QStringLiteral("偶校验"), int(QSerialPort::EvenParity));
    ui->comboParity->addItem(QStringLiteral("奇校验"), int(QSerialPort::OddParity));
    ui->comboParity->setCurrentIndex(0);

    ui->comboStopBits->addItem("1", int(QSerialPort::OneStop));
    ui->comboStopBits->addItem("1.5", int(QSerialPort::OneAndHalfStop));
    ui->comboStopBits->addItem("2", int(QSerialPort::TwoStop));
    ui->comboStopBits->setCurrentIndex(0);

    onRefreshPorts();
}

void MainWindow::setupPlot()
{
    QCustomPlot *p = ui->plotEnv;

    /* 6 条曲线 + 高频刷新：关掉抗锯齿可减少绘图开销 */
    p->setNotAntialiasedElements(QCP::aeAll);
    /* 图例从曲线区右下角挪到图下面一行（可换行）：图例留在轴矩形里约占 150 px
     * 宽，窗口变窄时先被裁掉的是图例右侧文字；放到外面后曲线区整幅可见。 */
    p->legend->setVisible(false);
    p->axisRect()->setAutoMargins(QCP::msAll);
    p->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables |
                       QCP::iSelectItems);
    p->xAxis->setLabel(QStringLiteral("数据序号"));
    p->xAxis->setRange(-1.0, 11.0);          /* 没有数据时以 0 为起点 */
    p->xAxis->setTickLabelRotation(0);
    p->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new SampleIndexTicker));
    /* 拖动/缩放只作用在横轴上：纵轴由"当前查看量"的自动量程或固定量程决定，
     * 让用户拖歪了反而看不懂，所以把纵向的拖动与缩放显式关掉。 */
    p->axisRect()->setRangeDrag(Qt::Horizontal);
    p->axisRect()->setRangeZoom(Qt::Horizontal);

    /* 唯一一根 Y 轴放在最左边：标签、单位、颜色、量程都跟随"当前查看量" */
    m_viewAxis = p->yAxis;
    m_viewAxis->setVisible(true);
    m_viewAxis->setRange(kYMin[0], kYMax[0]);

    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);
        const QColor c(kColors[i]);

        /* 6 条曲线共用同一根左轴。同一时刻只画"当前查看量"那一条，
         * 其余曲线留在图元里但不可见，切换查看量时再显示对应那条。 */
        m_graph[i] = p->addGraph(p->xAxis, m_viewAxis);
        m_graph[i]->setName(QStringLiteral("%1 (%2)")
                                .arg(QString::fromUtf8(DataModel::name(s)),
                                     QString::fromUtf8(DataModel::unit(s))));
        m_graph[i]->setPen(QPen(c, 1.6));
        m_graph[i]->setVisible(false);
    }

    /* 选中点的竖线：让"选中了哪一个数据序号"在曲线区一眼可见 */
    m_selLine = new QCPItemStraightLine(p);
    m_selLine->setPen(QPen(QColor(192, 57, 43, 160), 1.0, Qt::DashLine));
    m_selLine->setSelectable(false);
    m_selLine->setVisible(false);

    /* X 轴可点击点条：与主图 X 范围同步的窄条，圆点贴着 X 轴放在这里，
     * 而不是压在曲线上。它自己不允许拖动/缩放，鼠标拖动与滚轮仍然由主图处理。
     * 左右边距由 syncXStripMargins() 逐像素跟随主图轴矩形，否则圆点会与主图
     * 上对应序号的位置横向错开（主图左侧要让出 Y 轴标签与刻度的宽度）。 */
    m_xStrip = new QCustomPlot(ui->centerPanel);
    m_xStrip->setNotAntialiasedElements(QCP::aeAll);
    m_xStrip->legend->setVisible(false);
    m_xStrip->axisRect()->setAutoMargins(QCP::msNone);
    m_xStrip->axisRect()->setMargins(QMargins(0, 0, 0, 10));
    m_xStrip->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new SampleIndexTicker));
    m_xStrip->xAxis->setRange(-1.0, 11.0);
    m_xStrip->xAxis->setTickLabels(false);
    m_xStrip->xAxis->setTicks(false);
    m_xStrip->xAxis->setBasePen(QPen(QColor(140, 140, 140), 1.0));
    m_xStrip->yAxis->setRange(0.0, 1.0);
    m_xStrip->yAxis->setVisible(false);
    m_xStrip->setInteractions(QCP::iNone);               /* 点条自身不拖动/不缩放 */
    m_xStrip->setMouseTracking(true);                    /* 悬停出 tooltip */
    m_xStrip->setMinimumHeight(kStripHeight);
    m_xStrip->setMaximumHeight(kStripHeight);
    m_xStrip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_xMarkerGraph = m_xStrip->addGraph(m_xStrip->xAxis, m_xStrip->yAxis);
    m_xMarkerGraph->setName(QStringLiteral("可点击数据点"));
    m_xMarkerGraph->setLineStyle(QCPGraph::lsNone);      /* 不画连线，只画点 */
    m_xMarkerGraph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, Qt::white, 8.0));
    m_xMarkerGraph->setSelectable(QCP::stNone);          /* 命中判定自己在 mousePress 里做 */
    m_xMarkerGraph->setPen(QPen(Qt::NoPen));

    /* 被选中的那一个点单独用一个图元画，才能画出"实心加圈"的强调效果 */
    m_selDotGraph = m_xStrip->addGraph(m_xStrip->xAxis, m_xStrip->yAxis);
    m_selDotGraph->setName(QStringLiteral("选中数据点"));
    m_selDotGraph->setLineStyle(QCPGraph::lsNone);
    m_selDotGraph->setPen(QPen(Qt::NoPen));
    m_selDotGraph->setSelectable(QCP::stNone);

    /* 整个点条都可点：按点击像素在最近 20 个点里取最近的一个，不再依赖
     * plottable 命中测试（小圆点必须点得准，实际用起来点不中）。 */
    connect(m_xStrip, &QCustomPlot::mousePress, this, &MainWindow::onXStripPressed);
    connect(m_xStrip, &QCustomPlot::mouseMove, this, &MainWindow::onXStripMoved);

    /* 主图与窄条的 X 范围互相同步：主图拖动/缩放后窄条跟着走，窄条上拖动
     * 也能对齐主图（窄条本身没有拖动交互，这里只做单向跟随加保护）。 */
    connect(p->xAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, [this](const QCPRange &r) {
                if (m_xStrip == nullptr || m_syncingStripRange) return;
                m_syncingStripRange = true;
                m_xStrip->xAxis->setRange(r);
                m_xStrip->replot(QCustomPlot::rpQueuedReplot);
                m_syncingStripRange = false;
            });

    /* 曲线颜色说明条：放在 X 轴条下面，与左栏勾选框用同一组颜色。
     * 说明条可点击：点某个颜色名就等于把"查看数据"切到那个物理量。 */
    m_lblLegend = new QLabel(ui->centerPanel);
    m_lblLegend->setObjectName(QStringLiteral("lblCurveLegend"));
    m_lblLegend->setTextFormat(Qt::RichText);
    m_lblLegend->setWordWrap(true);
    m_lblLegend->setCursor(Qt::PointingHandCursor);
    m_lblLegend->setToolTip(QStringLiteral("单击颜色名即可切换当前查看的物理量"));
    m_lblLegend->setStyleSheet(QStringLiteral("font-size:12px;color:#333;"));
    /* 说明条里点颜色名 = 切换查看量（带上左轴与曲线） */
    connect(m_lblLegend, &QLabel::linkActivated, this, [this](const QString &link) {
        if (!link.startsWith(QStringLiteral("view:"))) return;
        bool ok = false;
        const int idx = link.mid(5).toInt(&ok);
        if (!ok) return;
        if (ui->comboStatSeries != nullptr) ui->comboStatSeries->setCurrentIndex(idx);
        else onStatSeriesChanged(idx);
    });

    /* 把 X 轴条与说明条依次插到主图之后 */
    const int plotIdx = ui->verticalLayoutCenter->indexOf(ui->plotEnv);
    const int afterPlot = (plotIdx < 0) ? ui->verticalLayoutCenter->count() : plotIdx + 1;
    ui->verticalLayoutCenter->insertWidget(afterPlot, m_xStrip);
    ui->verticalLayoutCenter->insertWidget(afterPlot + 1, m_lblLegend);

    updateViewAxis();
    p->replot();
}

void MainWindow::buildCards()
{
    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);

        QFrame *card = new QFrame(ui->cardsHolder);
        card->setFrameShape(QFrame::StyledPanel);
        card->setFrameShadow(QFrame::Raised);
        card->setStyleSheet(QStringLiteral("QFrame{border-left:4px solid %1;background:#fbfbfb;}")
                                .arg(QLatin1String(kColors[i])));

        QVBoxLayout *lay = new QVBoxLayout(card);
        lay->setContentsMargins(8, 4, 8, 6);
        lay->setSpacing(0);

        m_cardName[i] = new QLabel(QStringLiteral("%1 (%2)")
                                       .arg(QString::fromUtf8(DataModel::name(s)),
                                            QString::fromUtf8(DataModel::unit(s))), card);
        m_cardName[i]->setStyleSheet(QStringLiteral("color:#666;font-size:12px;border:none;"));

        m_cardValue[i] = new QLabel(QStringLiteral("--"), card);
        QFont f = m_cardValue[i]->font();
        f.setPointSize(f.pointSize() + 10);
        f.setBold(true);
        m_cardValue[i]->setFont(f);
        m_cardValue[i]->setStyleSheet(QStringLiteral("color:#222;border:none;"));

        lay->addWidget(m_cardName[i]);
        lay->addWidget(m_cardValue[i]);

        ui->cardsGrid->addWidget(card, i / 3, i % 3);    /* 3 列 × 2 行 */
    }
    /* 三列等宽：卡片排布随中间栏宽度伸缩，窗口变窄时三列一起变窄而不是把
     *  第三列挤到可视区之外 */
    for (int c = 0; c < 3; ++c)
        ui->cardsGrid->setColumnStretch(c, 1);
}

void MainWindow::buildCurveChecks()
{
    /* 曲线勾选与两个开关放左栏的"曲线显示"分组，不占中间曲线区的宽度：
     * 6 条曲线勾选 + 两个开关排成一行需要约 740 px，放在中间栏会把右半部分
     * （MQ-135 勾选、量程校验）挤出可视区。左栏放不下时由左栏滚动条兜住。 */
    QGroupBox *box = new QGroupBox(QStringLiteral("曲线显示（勾选要画的物理量）"), ui->leftPanel);
    QGridLayout *grid = new QGridLayout(box);
    grid->setContentsMargins(8, 6, 8, 6);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);

    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);
        QCheckBox *chk = new QCheckBox(QStringLiteral("%1").arg(QString::fromUtf8(DataModel::name(s))),
                                       box);
        chk->setStyleSheet(QStringLiteral("color:%1;font-weight:600;").arg(QLatin1String(kColors[i])));
        chk->setChecked(i == 0);                         /* 默认查看量是温度，勾选与之一致 */
        grid->addWidget(chk, i / 2, i % 2);
        m_curveChk[i] = chk;

        connect(chk, &QCheckBox::toggled, this, [this, i](bool on) {
            /* 曲线不是"各画一条"：同一时刻只画当前查看量那一条，
             * 勾选框表示这个量能不能被选中查看（至少留一条）。 */
            if (!on) {
                int checked = 0;
                for (int k = 0; k < int(Series::Count); ++k) {
                    if (m_curveChk[k] != nullptr && m_curveChk[k]->isChecked()) ++checked;
                }
                if (checked == 0) {                     /* 不允许一条都不留 */
                    if (m_curveChk[i] != nullptr) {
                        QSignalBlocker b(m_curveChk[i]);
                        m_curveChk[i]->setChecked(true);
                    }
                    return;
                }
                if (m_statSeries == i) {                /* 当前查看量被取消，换到另一条 */
                    for (int k = 0; k < int(Series::Count); ++k) {
                        if (k != i && m_curveChk[k] != nullptr && m_curveChk[k]->isChecked()) {
                            if (ui->comboStatSeries != nullptr) ui->comboStatSeries->setCurrentIndex(k);
                            else onStatSeriesChanged(k);
                            break;
                        }
                    }
                }
            }
            refreshPlot();
            refreshStatPanel();
        });
    }

    /* 两个数据边界开关放在勾选下面两行（占满两列）：
     * Y 轴自动量程默认开：Y 轴下界固定 0、上界按当前查看量的数据自适应，
     * 这是需求指定的显示方式；关掉则用该物理量的固定量程（便于跨时间对比形状）。
     * 物理量量程校验默认开：超范围的值照常显示、照记 CSV，只是不参与告警判定；
     * 板1 气压/光照/TVOC/MQ-135 尚未接入（帧里是 0），不拦会刷出气压过低假告警。 */
    m_chkYAuto = new QCheckBox(QStringLiteral("Y 轴自动量程（下界 0，上界自适应）"), box);
    m_chkYAuto->setToolTip(QStringLiteral(
        "勾选：Y 轴下界固定 0，上界按当前查看量的数据自动缩放（默认）\n"
        "不勾：用当前查看量的固定量程"));
    m_chkYAuto->setChecked(true);
    grid->addWidget(m_chkYAuto, 3, 0, 1, 2);
    connect(m_chkYAuto, &QCheckBox::toggled, this, [this](bool) { refreshPlot(); });

    m_chkRange = new QCheckBox(QStringLiteral("量程校验"), box);
    m_chkRange->setToolTip(QStringLiteral(
        "超出物理合理范围的值标黄、计入统计，但不参与告警判定\n"
        "（板1 的气压/光照/TVOC/MQ-135 目前还没接，帧里恒为 0）"));
    m_chkRange->setChecked(true);
    grid->addWidget(m_chkRange, 4, 0, 1, 2);

    /* 插到"解析统计"之后、"告警阈值"之前 */
    int idx = ui->verticalLayoutLeft->indexOf(ui->thresholdBox);
    if (idx < 0) idx = ui->verticalLayoutLeft->count();
    ui->verticalLayoutLeft->insertWidget(idx, box);
}

/* 右侧统计区的初始化：填参数下拉框、接信号、把 .ui 里的控件指针存下来。
 * 下拉框默认选第一条可见曲线，与曲线勾选框保持同一个"当前参数"。 */
void MainWindow::buildStatPanel()
{
    m_lblStatAvg   = ui->lblStatAvg;
    m_lblStatMax   = ui->lblStatMax;
    m_lblStatMin   = ui->lblStatMin;
    m_lblStatCount = ui->lblStatCount;
    m_lblStatRange = ui->lblStatRange;
    m_lblSelPoint  = ui->lblSelPoint;

    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);
        ui->comboStatSeries->addItem(QStringLiteral("%1 (%2)")
                                         .arg(QString::fromUtf8(DataModel::name(s)),
                                              QString::fromUtf8(DataModel::unit(s))));
    }
    ui->comboStatSeries->setCurrentIndex(0);
    m_statSeries = 0;

    connect(ui->comboStatSeries, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onStatSeriesChanged);
    connect(ui->btnSelClear, &QPushButton::clicked, this, &MainWindow::onSelClear);

    for (QLabel *l : QList<QLabel *>() << m_lblStatAvg << m_lblStatMax << m_lblStatMin
                                      << m_lblStatCount << m_lblStatRange << m_lblSelPoint) {
        if (l != nullptr) l->setTextFormat(Qt::PlainText);   /* 数值里不出现富文本转义 */
    }

    refreshStatPanel();
}

void MainWindow::onStatSeriesChanged(int index)
{
    if (index < 0 || index >= int(Series::Count)) return;
    m_statSeries = index;

    /* 查看量换了：左侧 Y 轴与底部说明条跟着换，曲线也只画这一条。
     * 该量没勾选时一并勾上：下拉框与曲线勾选本来就是同一个"当前查看量"，
     * 统计区既然给出了它的数，曲线区就应该能点到它的点，否则用户只会看到
     * "有统计但一个点也点不到"。 */
    if (m_curveChk[index] != nullptr && !m_curveChk[index]->isChecked()) {
        QSignalBlocker b(m_curveChk[index]);            /* 避免与勾选槽互相触发 */
        m_curveChk[index]->setChecked(true);
    }

    updateViewAxis();
    refreshPlot();
    refreshStatPanel();
    ui->plotEnv->replot(QCustomPlot::rpQueuedReplot);
}

/* 行程自检入口：按 Series 下标指定查看量，等价于在"查看数据"下拉框里选一项。
 * 走 onStatSeriesChanged() 同一条落地路径，保证切换后左轴、曲线、统计一致。 */
bool MainWindow::setViewSeries(int index)
{
    if (index < 0 || index >= int(Series::Count)) return false;
    if (ui->comboStatSeries == nullptr) return false;
    ui->comboStatSeries->setCurrentIndex(index);   /* 触发 onStatSeriesChanged */
    return m_statSeries == index;
}

void MainWindow::onSelClear()
{
    m_hasSel = false;
    if (m_selLine != nullptr) m_selLine->setVisible(false);
    refreshStatPanel();
    ui->plotEnv->replot(QCustomPlot::rpQueuedReplot);
}

/* X 轴条单击：整条都可点。按点击像素在"最近 20 个可点击点"里取水平距离
 * 最近的一个，命中半径 kStripHitPx，不再依赖 plottable 命中测试。
 * 记下该点的横坐标（数据序号），供右侧显示数值。 */
void MainWindow::onXStripPressed(QMouseEvent *event)
{
    if (event == nullptr || event->button() != Qt::LeftButton) return;
    const double no = stripXToSampleNumber(int(event->position().x()));
    if (no <= 0.0) return;
    applySelectedSample(no);
}

/* X 轴条悬停：显示"序号 n 记录时刻 HH:MM:SS"，让序号和时间轴对得上 */
void MainWindow::onXStripMoved(QMouseEvent *event)
{
    if (event == nullptr || m_xStrip == nullptr) return;
    const double no = stripXToSampleNumber(int(event->position().x()));
    const int shown = int(no + 0.5);
    if (shown > 0 && shown != m_stripHoverNo) {
        m_stripHoverNo = shown;
        m_xStrip->setToolTip(QStringLiteral("序号 %1　记录时刻 %2")
                                 .arg(shown)
                                 .arg(sampleTimeText(shown)));
    } else if (shown <= 0 && m_stripHoverNo != 0) {
        m_stripHoverNo = 0;
        m_xStrip->setToolTip(QString());
    }
}

/* 点条像素 x → 最近的可点击序号（1 起）。可点击点 = 当前查看量末尾
 * kMarkerPoints 个有效点；水平距离超过 kStripHitPx 视为没点中，返回 0。 */
double MainWindow::stripXToSampleNumber(int px) const
{
    if (m_xStrip == nullptr) return 0.0;

    /* 候选 = 可视范围内所有有有效数据的样本（不做抽稀），
     * 与 tooltip 口径一致：落在两个绘制点之间也能选中最近的样本 */
    const QVector<double> numbers = stripHitNumbers();
    if (numbers.isEmpty()) return 0.0;

    double best = 0.0;
    double bestDist = double(kStripHitPx) + 1.0;
    for (double key : numbers) {
        const double cx = m_xStrip->xAxis->coordToPixel(key);
        const double d = qAbs(cx - double(px));
        if (d < bestDist) {
            bestDist = d;
            best = key;
        }
    }
    return (bestDist <= double(kStripHitPx)) ? best : 0.0;
}

/* 选中数据序号 sampleNo（1 起）对应的那一组。等价于用鼠标点 X 轴条上
 * 序号 sampleNo 的圆点，也是 --selsample 走的路（与鼠标同一落地路径）。 */
bool MainWindow::selectStatSampleByNumber(int sampleNo)
{
    if (sampleNo <= 0) return false;
    /* TODO(阶段3)：先确认数据模型里确实有这个序号（m_model.size() >= sampleNo），
     *            再到点条可命中的序号里找它、选中它。 */

    const QVector<double> numbers = stripHitNumbers();
    for (double key : numbers) {
        if (int(key + 0.5) == sampleNo) {
            applySelectedSample(double(sampleNo));
            return m_hasSel;
        }
    }
    return false;
}

/* 点条的左右边距跟随主图轴矩形：主图左边要给 Y 轴标签与刻度让出宽度，
 * 点条没有这些内容；不对齐时同一个序号在主图与点条上的像素 x 相差约 67 px，
 * 看起来就是"点条与时间轴（曲线）没对上"。右边距用"点条视口宽 - 主图轴矩形
 * 右边界"求，保证两侧轴矩形等宽，坐标换算逐像素一致。 */
void MainWindow::syncXStripMargins()
{
    if (m_xStrip == nullptr || ui->plotEnv == nullptr) return;
    QCPAxisRect *mainRect = ui->plotEnv->axisRect();
    const int left = mainRect->left();
    const int right = qMax(0, m_xStrip->viewport().width() - mainRect->right());
    m_xStrip->axisRect()->setMargins(QMargins(left, 0, right, 10));
}

/* 主图与点条的对齐诊断：两处轴矩形左右像素 + 序号 1/序号 N 的像素 x。
 * 两组数值相等（允许 1 px 取整误差）才算逐像素对齐。 */
void MainWindow::dumpStripAlign(QStringList &out) const
{
    if (m_xStrip == nullptr || ui->plotEnv == nullptr) return;
    QCPAxisRect *mr = ui->plotEnv->axisRect();
    QCPAxisRect *sr = m_xStrip->axisRect();
    out << QStringLiteral("主图轴矩形 left=%1 right=%2（视口宽 %3）")
               .arg(mr->left()).arg(mr->right()).arg(ui->plotEnv->viewport().width());
    out << QStringLiteral("点条轴矩形 left=%1 right=%2（视口宽 %3）")
               .arg(sr->left()).arg(sr->right()).arg(m_xStrip->viewport().width());
    out << QStringLiteral("点条高度=%1 px，命中半径=±%2 px（整条高度可点）")
               .arg(m_xStrip->height()).arg(kStripHitPx);

    /* 绘制口径 / 命中口径 / 最小相邻绘制间距（px）：核对密集数据下点不连成一片。
     * 整个绘制集合统一走 kStripMinGapPx 像素抽稀，因此最小间距应 >= 该值；
     * 末尾 kMarkerPoints 个只是遍历时先被考虑、优先保留，不再是无条件必画。 */
    const QVector<double> draw = stripDrawNumbers();
    const QVector<double> hit = stripHitNumbers();
    double minGap = -1.0;       /* 全部绘制点里相邻两点的最小像素间距 */
    for (int i = 1; i < draw.size(); ++i) {
        const double d = m_xStrip->xAxis->coordToPixel(draw.at(i))
                       - m_xStrip->xAxis->coordToPixel(draw.at(i - 1));
        if (minGap < 0.0 || d < minGap) minGap = d;
    }
    out << QStringLiteral("点条绘制点 = %1 个（最近 %2 个样本优先保留、"
                          "全集合按 >= %3 px 抽稀、总量上限 %4），"
                          "可视范围内可命中样本 = %5 个，最小相邻绘制间距 = %6 px")
               .arg(draw.size()).arg(kMarkerPoints)
               .arg(kStripMinGapPx).arg(kStripMaxDots).arg(hit.size())
               .arg(minGap, 0, 'f', 1);

    const double noFirst = 1.0;
    /* TODO(阶段3)：末序号取数据模型的帧数（m_model.size()），至少按 1 算。 */
    const double noLast = 1.0;      /* 占位：接入数据模型后改成 double(qMax(1, m_model.size())) */
    out << QStringLiteral("序号 1 像素 x：主图=%1 点条=%2")
               .arg(ui->plotEnv->xAxis->coordToPixel(noFirst), 0, 'f', 1)
               .arg(m_xStrip->xAxis->coordToPixel(noFirst), 0, 'f', 1);
    out << QStringLiteral("序号 %1 像素 x：主图=%2 点条=%3")
               .arg(int(noLast))
               .arg(ui->plotEnv->xAxis->coordToPixel(noLast), 0, 'f', 1)
               .arg(m_xStrip->xAxis->coordToPixel(noLast), 0, 'f', 1);
    out << QStringLiteral("主图 X 范围 = %1 ~ %2")
               .arg(ui->plotEnv->xAxis->range().lower, 0, 'f', 2)
               .arg(ui->plotEnv->xAxis->range().upper, 0, 'f', 2);
}

/* 主图单击（保留给图元点击路径）：只有点条上的可点击点会被选中。 */
void MainWindow::onPlotPointSelected(QCPAbstractPlottable *plottable, int dataIndex, QMouseEvent *event)
{
    Q_UNUSED(event);
    if (plottable == nullptr || plottable != m_xMarkerGraph) return;
    applySelectedIndex(dataIndex);
}

/* 选中第 dataIndex 个可点击点：记录数据序号与来源物理量，竖起竖线，刷新右侧数值。 */
void MainWindow::applySelectedIndex(int dataIndex)
{
    if (dataIndex < 0 || m_xMarkerGraph == nullptr) return;

    const QCPGraphDataContainer *data = m_xMarkerGraph->data().data();
    if (data == nullptr || dataIndex >= data->size()) return;

    applySelectedSample(data->at(dataIndex)->key);
}

/* 选中某个数据序号（1 起）：序号即横坐标，因此直接把 m_selX 设为它。
 * 鼠标点击（applySelectedIndex 传序号）与 --selsample 都走这里，
 * 选中状态只有这一条落地路径，不会出现两处显示不一致。 */
void MainWindow::applySelectedSample(double sampleNo)
{
    /* TODO(阶段3)：建立选中状态前先确认数据模型里有这一组数据（m_model.size() > 0）。 */
    if (sampleNo <= 0.0) return;

    m_selX = sampleNo;
    m_selSeries = m_statSeries;
    m_hasSel = true;

    if (m_selLine != nullptr) {
        m_selLine->setVisible(true);
        m_selLine->point1->setCoords(m_selX, 0.0);
        m_selLine->point2->setCoords(m_selX, 1.0);
    }
    rebuildXStrip();                 /* 让选中点变成实心加圈、其余点弱化 */
    refreshStatPanel();
    ui->plotEnv->replot(QCustomPlot::rpQueuedReplot);
}

/* 行内自检入口：按倒数第 fromEnd 个可点击点选中，返回是否真的选中了。 */
bool MainWindow::selectStatPointFromEnd(int fromEnd)
{
    if (m_xMarkerGraph == nullptr) return false;
    const QCPGraphDataContainer *data = m_xMarkerGraph->data().data();
    if (data == nullptr || data->isEmpty()) return false;

    const int idx = data->size() - 1 - qMax(0, fromEnd);
    if (idx < 0 || idx >= data->size()) return false;
    applySelectedIndex(idx);
    return m_hasSel;
}

/* 时间轴刻度文本：选中点的本机时刻 */
QString MainWindow::clockText(qint64 wallMs)
{
    return QDateTime::fromMSecsSinceEpoch(wallMs).toString(QStringLiteral("HH:mm:ss"));
}

/* 数据序号 → 该帧的记录时刻文本。序号从 1 起，等于时间轴下标 + 1；
 * 序号越界（例如数据被清空、或序号来自更早的一批数据）返回占位文本。 */
QString MainWindow::sampleTimeText(double sampleNo) const
{
    /* TODO(阶段3)：取数据模型的记录时刻表（m_model.stamps()），按下标（序号 - 1）取出
     *            该帧的时刻（序号 = 下标 + 1）；越界（数据已被清空、或序号来自更早的
     *            一批数据）返回占位文本。 */
    (void)sampleNo;
    return QStringLiteral("--:--:--");
}

/* 选中点的读数：数据序号 + 记录时刻 + 六个通道的值。
 * 序号就是横坐标本身，因此不必再"找最近的一帧"；六路数值从各曲线对齐的
 * 数组里按下标取值，与曲线、CSV 同源。 */
QString MainWindow::selectedPointText() const
{
    if (!m_hasSel) return QString();

    /* TODO(阶段3)：拼"选中数据 序号 N（记录时刻 …）"加六行"名称 数值 单位"：
     *            时刻取 m_model.stamps()，六路数值从各曲线的历史点
     *            （m_model.history(Series(i))）按下标（序号 - 1）取，数值按
     *            DataModel::decimals(i) 保留小数，y 不是有限值就写 "--"；
     *            序号越界（数据刚被清空）返回空串。 */
    return QString();
}

/* 右侧统计区：平均值 / 最大值 / 最小值 + 选中点数值。
 * 统计口径由 StatCalc::compute() 定义（最近 50 个有效点，点数不少于 3 时
 * 平均值剔除 1 个最大值与 1 个最小值）。 */
void MainWindow::refreshStatPanel()
{
    if (ui->comboStatSeries == nullptr || ui->lblStatAvg == nullptr) return;
    if (m_statSeries < 0 || m_statSeries >= int(Series::Count)) m_statSeries = 0;
    if (ui->comboStatSeries->currentIndex() != m_statSeries) {
        QSignalBlocker block(ui->comboStatSeries);
        ui->comboStatSeries->setCurrentIndex(m_statSeries);
    }

    const Series s = Series(m_statSeries);
    /* TODO(阶段3)：取当前查看量的窗口统计（m_model.windowStat(s)，口径见 StatCalc::compute）：
     *            有数据就把平均值/最大值/最小值（按 DataModel::decimals(s) 保留小数、
     *            带上单位）、窗口点数与窗口起止时刻（m_model.stamps() / m_model.history(s)）
     *            写进 lblStatAvg / lblStatMax / lblStatMin / lblStatCount / lblStatRange，
     *            窗口范围还要说明平均值有没有剔除极值；没数据时统一显示 "--"、"0" 与
     *            "窗口范围：无数据"。
     * TODO(阶段3)：把选中点读数（selectedPointText()）写进 m_lblSelPoint；未选中时显示
     *            "单击曲线区下方 X 轴上的圆点可读取该组数据的六路数值"的提示。 */
    (void)s;

    updateStatsWarn();
}

/* 当前查看量对应的曲线没勾选时，"选中数据点"区域给出明确提示。
 * 正常路径上选择查看量会自动勾上对应曲线，这里兜住"用户事后取消勾选"的情形。 */
void MainWindow::updateStatsWarn()
{
    if (m_lblSelPoint == nullptr || ui->comboStatSeries == nullptr) return;
    if (m_xMarkerGraph != nullptr && m_xMarkerGraph->visible()) return;

    const Series s = Series(qBound(0, m_statSeries, int(Series::Count) - 1));
    const QString nm = QString::fromUtf8(DataModel::name(s));
    m_lblSelPoint->setText(QStringLiteral("%1 曲线未显示，勾选左栏「曲线显示（勾选要画的物理量）」"
                                          "里的 %1 后可点击取点。\n"
                                          "统计值仍按采样数据计算，与曲线是否画出无关。")
                               .arg(nm));
}

void MainWindow::buildThresholdPanel()
{
    QGridLayout *g = new QGridLayout();
    g->setContentsMargins(0, 0, 0, 0);
    g->addWidget(new QLabel(QStringLiteral("项目")), 0, 0);
    g->addWidget(new QLabel(QStringLiteral("下限")), 0, 1);
    g->addWidget(new QLabel(QStringLiteral("上限")), 0, 2);
    g->addWidget(new QLabel(QStringLiteral("回差")), 0, 3);

    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);
        /* TODO(阶段4)：三个框的初始值取告警管理器里的阈值（m_alarm.threshold(s) 的
         *            low/high/hyst）；下面先给占位值。 */
        const double thLow  = 0.0;
        const double thHigh = 0.0;
        const double thHyst = 0.0;
        const int dec = DataModel::decimals(s);

        QLabel *name = new QLabel(QStringLiteral("%1").arg(QString::fromUtf8(DataModel::name(s))));
        name->setStyleSheet(QStringLiteral("color:%1;").arg(QLatin1String(kColors[i])));

        /* 三个框恒可编辑，不按该侧是否启用置灰：光照默认只启用下限，若把上限框
         * 置灰，用户想改上限时会改不了，也想不到去别处开。约定是改哪个框即启用
         * 哪一侧（见下面的 connect）；要彻底不判某一侧，去阈值设置对话框取消勾选。 */
        auto mkSpin = [this, i, dec](double val, const char *tip) {
            QDoubleSpinBox *sb = new QDoubleSpinBox(ui->thresholdBox);
            sb->setDecimals(dec);
            sb->setRange(-10000.0, 10000.0);
            sb->setSingleStep(dec == 0 ? 10.0 : 1.0);
            sb->setValue(val);
            sb->setToolTip(QString::fromUtf8(tip));
            return sb;
        };

        m_thLow[i]  = mkSpin(thLow,  "低于这个值就报过低（改这里即启用下限判定）");
        m_thHigh[i] = mkSpin(thHigh, "高于这个值就报过高（改这里即启用上限判定）");
        m_thHyst[i] = mkSpin(thHyst, "回差/迟滞：报警后要走出 阈值±回差 才恢复，避免在阈值附近反复报");

        g->addWidget(name,        i + 1, 0);
        g->addWidget(m_thLow[i],  i + 1, 1);
        g->addWidget(m_thHigh[i], i + 1, 2);
        g->addWidget(m_thHyst[i], i + 1, 3);

        connect(m_thLow[i], &QDoubleSpinBox::valueChanged,
                this, [this, i](double v) {
                    if (m_loading) return;
                    /* TODO(阶段4)：把下限 v 写进告警管理器（改值即启用下限判定），
                     *            再调 refreshThresholdPanel() 回显阈值表。 */
                    (void)v;
                    refreshThresholdPanel();
                });
        connect(m_thHigh[i], &QDoubleSpinBox::valueChanged,
                this, [this, i](double v) {
                    if (m_loading) return;
                    /* TODO(阶段4)：把上限 v 写进告警管理器（改值即启用上限判定），
                     *            再调 refreshThresholdPanel() 回显阈值表。 */
                    (void)v;
                    refreshThresholdPanel();
                });
        connect(m_thHyst[i], &QDoubleSpinBox::valueChanged,
                this, [this, i](double v) {
                    if (m_loading) return;
                    /* TODO(阶段4)：把回差 v 写进告警管理器（负值按 0 处理）。 */
                    (void)v;
                });
    }

    m_chkAlarmOn = new QCheckBox(QStringLiteral("启用告警判定（关掉则只显示数据）"));
    m_chkAlarmOn->setChecked(true);
    connect(m_chkAlarmOn, &QCheckBox::toggled, this, [this](bool on) {
        /* TODO(阶段4)：把"启用告警判定"总开关写进告警管理器，再回显阈值表。 */
        (void)on;
        refreshThresholdPanel();
    });

    g->addWidget(m_chkAlarmOn, int(Series::Count) + 1, 0, 1, 4);

    /* 两个按钮：全部阈值（含启用哪一侧和回差）在对话框里改；
     * 主界面这 4 列留着便于一边看曲线一边改。 */
    QPushButton *btnDlg = new QPushButton(QStringLiteral("阈值设置…（含启用/回差）"), ui->thresholdBox);
    connect(btnDlg, &QPushButton::clicked, this, &MainWindow::onThresholdDialog);

    QPushButton *btnDef = new QPushButton(QStringLiteral("恢复默认"), ui->thresholdBox);
    connect(btnDef, &QPushButton::clicked, this, &MainWindow::onThresholdDefaults);

    QHBoxLayout *h = new QHBoxLayout();
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(btnDlg);
    h->addWidget(btnDef);
    h->addStretch(1);
    g->addLayout(h, int(Series::Count) + 2, 0, 1, 4);

    ui->verticalLayoutThreshold->addLayout(g);
    ui->verticalLayoutThreshold->addStretch(1);
}

/* 把 AlarmManager 的当前阈值回显到主界面 4 列表格。
 *  · 对话框改完、恢复默认、加载 ini 之后都要调一次；
 *  · m_loading 用来挡住 valueChanged → 免得回显本身又被当成"用户改了值"
 *    而把没启用的那一侧意外打开。 */
void MainWindow::refreshThresholdPanel()
{
    if (!m_thLow[0])
        return;

    /* TODO(阶段4)：把告警管理器里的当前阈值回显到主界面 4 列表格 —— 逐项取
     *            m_alarm.threshold(Series(i)) 的 low/high/hyst 写进 m_thLow/m_thHigh/m_thHyst；
     *            未启用的一侧保留数值，但用灰字（color:#888;）并在后缀加" ·未判"；
     *            最后把总开关 m_chkAlarmOn 同步成 m_alarm.enabled()。
     *            回显期间要置 m_loading（挡住 valueChanged，免得回显本身被当成用户改了值，
     *            把没启用的那一侧意外打开）。 */
}

void MainWindow::onThresholdDialog()
{
    /* 对话框先改暂存、点确定才写回，所以进出各回显一次就够 */
    refreshThresholdPanel();
    /* TODO(阶段4)：弹出阈值设置对话框（ThresholdDialog::edit(&m_alarm, this)），
     *            点"确定"后阈值就写回了告警管理器；成功再回显阈值表、saveConfig()
     *            并记一条"阈值设置已更新并写入配置文件。"日志。 */
}

void MainWindow::onThresholdDefaults()
{
    /* TODO(阶段4)：把 6 项阈值恢复成出厂默认 —— 用一个默认构造的告警管理器
     *            （AlarmManager def）取出默认阈值写回 m_alarm，再把总开关打开。 */
    refreshThresholdPanel();
    saveConfig();
    logLine(QStringLiteral("告警阈值已恢复为默认值（并写入配置文件）。"));
}

/* ============================================================================
 *  设备控制面板（左栏 3 · 设备控制）
 *  数据往上走是监测，命令往下走才是控制：有了这一块，板2 的转发开关、缓存开关、
 *  清缓存、复位都不必重烧固件改常量。
 *  位置在记录与自检之下、解析统计之上：常用操作靠上，日志靠下；左栏编号因此
 *  为 1 串口 / 2 记录与自检 / 3 设备控制 / 4 解析统计 / 5 阈值。
 * ==========================================================================*/
void MainWindow::buildControlPanel()
{
    QGroupBox *box = new QGroupBox(QStringLiteral("3 · 设备控制（下发到板2）"), ui->leftPanel);

    QVBoxLayout *v = new QVBoxLayout(box);

    /* 两个开关：勾上即让板2 打开，取消即关闭；每次点击都会发一帧命令 */
    m_chkFwd = new QCheckBox(QStringLiteral("板2 转发数据到本机（USART2）"), box);
    m_chkFwd->setToolTip(QStringLiteral(
        "取消勾选后板2 不再往串口转发环境帧，曲线会停；板2 仍在收 LoRa、\n"
        "仍在落 Flash 缓存（缓存开关见下一项）。转发与缓存是两条独立的路。"));
    m_chkFwd->setChecked(true);
    connect(m_chkFwd, &QCheckBox::toggled, this, &MainWindow::onDevFwdToggled);

    m_chkCache = new QCheckBox(QStringLiteral("板2 把数据落 Flash 缓存"), box);
    m_chkCache->setToolTip(QStringLiteral(
        "断网缓存：板2 收到 LoRa 帧就写进 W25Q128（64 KB / 2048 条环形）。\n"
        "关掉后不再写缓存。网络恢复后用下面的“开始补传历史数据”让板2 从最老一条开始回传，\n"
        "进度显示在按钮右侧；也可以查询状态看缓存条数、用清空缓存清掉。"));
    m_chkCache->setChecked(true);
    connect(m_chkCache, &QCheckBox::toggled, this, &MainWindow::onDevCacheToggled);

    QPushButton *btnQ = new QPushButton(QStringLiteral("查询板2 状态"), box);
    btnQ->setToolTip(QStringLiteral("读回板2 的转发/缓存开关、收帧统计、缓存条数、运行时长"));
    connect(btnQ, &QPushButton::clicked, this, &MainWindow::onDevQuery);

    QPushButton *btnC = new QPushButton(QStringLiteral("清空板2 Flash 缓存"), box);
    btnC->setToolTip(QStringLiteral(
        "让板2 擦掉缓存里的全部记录（16 个 4 KB 扇区，约 0.5~1 s）。\n"
        "擦除期间板2 的 LoRa 收帧不会丢（有队列缓冲），但高频采数期间不宜操作。"));
    connect(btnC, &QPushButton::clicked, this, &MainWindow::onDevClearCache);

    QPushButton *btnR = new QPushButton(QStringLiteral("重启板2（软复位）"), box);
    btnR->setToolTip(QStringLiteral(
        "板2 先回应答、100 ms 后才复位，便于确认命令已到达。\n"
        "复位后串口不会断（CH340 独立供电），但会有一两秒没有数据，属正常。"));
    connect(btnR, &QPushButton::clicked, this, &MainWindow::onDevReboot);

    m_devStatus = new QLabel(QStringLiteral("板2 状态：未知（点查询板2 状态）"), box);
    m_devStatus->setWordWrap(true);
    m_devStatus->setStyleSheet(QStringLiteral("color:#555;"));

    /* 补传一块：断网期间板2 把帧写进 W25Q 缓存，恢复后由这里下令把历史补回来。
     * 按钮只管发 0x16；进度以板2 回的 0x96 应答为准，上位机不自己推算。 */
    QFrame *replayLine = new QFrame(box);
    replayLine->setFrameShape(QFrame::HLine);
    replayLine->setFrameShadow(QFrame::Sunken);

    QLabel *replayTitle = new QLabel(QStringLiteral("历史数据补传（板2 缓存回传）"), box);
    replayTitle->setStyleSheet(QStringLiteral("color:#555;"));

    m_btnReplay = new QPushButton(QStringLiteral("开始补传历史数据"), box);
    m_btnReplay->setToolTip(QStringLiteral(
        "让板2 把 W25Q 缓存里的历史帧按原时间戳回传（0x15），本机按原始时间补进曲线。\n"
        "补传不参与实时告警判定，历史越限数据不会触发告警。\n"
        "补传中可随时点停止；板2 走完缓存后会自己报已完成。"));
    connect(m_btnReplay, &QPushButton::clicked, this, &MainWindow::onDevReplayClicked);

    m_replayStatus = new QLabel(box);
    m_replayStatus->setWordWrap(true);
    m_replayStatus->setStyleSheet(QStringLiteral("color:#555;"));
    updateReplayLabel();

    m_btnQuery    = btnQ;
    m_btnClrCache = btnC;
    m_btnReboot   = btnR;

    v->addWidget(m_chkFwd);
    v->addWidget(m_chkCache);
    v->addWidget(btnQ);
    v->addWidget(btnC);
    v->addWidget(btnR);
    v->addWidget(m_devStatus);
    v->addWidget(replayLine);
    v->addWidget(replayTitle);
    v->addWidget(m_btnReplay);
    v->addWidget(m_replayStatus);

    /* 插到记录与自检之后：verticalLayoutLeft 的 insertWidget 下标 2，
     * 即 0=serialBox 1=recordBox 2=statsBox 3=thresholdBox，插在 statsBox 之前 */
    ui->verticalLayoutLeft->insertWidget(2, box);
}

/* ============================================================================
 *  设备控制槽
 * ==========================================================================*/

void MainWindow::onDevFwdToggled(bool on)
{
    if (m_loading) return;                  /* 回显设备真实状态时别把状态又发回去 */
    logLine(on ? QStringLiteral("请求：打开板2 转发") : QStringLiteral("请求：关闭板2 转发"));
    /* TODO(阶段6)：把转发开关下发给板2（DevCtl::setForward）。 */
}

void MainWindow::onDevCacheToggled(bool on)
{
    if (m_loading) return;
    logLine(on ? QStringLiteral("请求：打开板2 缓存") : QStringLiteral("请求：关闭板2 缓存"));
    /* TODO(阶段6)：把缓存开关下发给板2（DevCtl::setCache）。 */
}

void MainWindow::onDevQuery()
{
    /* TODO(阶段6)：向板2 下发"查询状态"命令（DevCtl::query）。 */
}

void MainWindow::onDevClearCache()
{
    /* 破坏性操作二次确认：缓存里可能是唯一的断网历史数据，
     * 误操作会丢掉最多 2048 条记录，且 Flash 擦除不可撤销。 */
    const QMessageBox::StandardButton r = QMessageBox::question(
        this, QStringLiteral("清空板2 Flash 缓存"),
        QStringLiteral("确定要清空板2 的 W25Q128 断网缓存吗？\n\n"
                       "· 会擦掉全部已缓存记录（最多 2048 条），不可恢复；\n"
                       "· 擦除需要约 0.5~1 秒，期间板2 仍在收 LoRa（有队列缓冲）；\n"
                       "· 只影响缓存，不影响正在转发的实时数据。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes) {
        logLine(QStringLiteral("已取消：清空板2 缓存"));
        return;
    }
    /* TODO(阶段6)：确认后下发"清空 Flash 缓存"命令（DevCtl::clearCache）。 */
}

void MainWindow::onDevReboot()
{
    const QMessageBox::StandardButton r = QMessageBox::question(
        this, QStringLiteral("软复位板2"),
        QStringLiteral("确定要软复位板2 吗？\n\n"
                       "· 板2 会先回应答、100 ms 后复位（命令确实到了才复位）；\n"
                       "· 复位期间约 1~2 秒没有数据，之后自动恢复转发；\n"
                       "· 串口连接不会断（CH340 是独立供电的）。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes) {
        logLine(QStringLiteral("已取消：重启板2"));
        return;
    }
    /* TODO(阶段6)：确认后下发"软复位"命令（DevCtl::reboot）。 */
}

/* 开始补传 / 停止补传：一个按钮两态。点了之后立刻切到"补传中"，
 * 板2 的应答（0x96）回来后再按实际状态覆盖，避免点了没反应的感觉。 */
void MainWindow::onDevReplayClicked()
{
    const bool wantStop = m_replayRunning;

    if (wantStop) {
        logLine(QStringLiteral("请求：停止补传历史数据"));
        /* TODO(阶段6)：让板2 停止补传（DevCtl::stopReplay）。 */
    } else {
        logLine(QStringLiteral("请求：开始补传历史数据"));
        /* TODO(阶段6)：让板2 从最老一条开始回传（DevCtl::startReplay）。 */
    }

    m_replayRunning = !wantStop;
    m_replayAckMs = QDateTime::currentMSecsSinceEpoch();
    m_replayWarned = false;
    updateReplayLabel();
}

void MainWindow::onDevReplayChanged(const ReplayStatus &st)
{
    if (!st.valid) return;

    /* 板2 说不在补传了（空闲/已完成/出错），按钮就该回到"开始补传" */
    m_replayRunning = (st.state == FrameProto::RP_RUNNING);
    m_replayAckMs = st.atMs;
    m_replayWarned = false;
    updateReplayLabel();

    if (st.state == FrameProto::RP_DONE) {
        /* TODO(阶段2/6)：补传完成日志再带上历史入库累计与重复丢弃数
         *            （m_model.historyInserted() / m_model.historyDuplicate()）。 */
        logLine(QStringLiteral("补传已完成：本次已补 %1 条").arg(st.sent));
    } else if (st.state == FrameProto::RP_ERROR) {
        logLine(QStringLiteral("板2 报告补传出错，可点停止后再重新开始"));
    }
}

/* 补传状态文案只在这里拼，按钮文字与状态行始终一致 */
void MainWindow::updateReplayLabel()
{
    if (m_btnReplay != nullptr) {
        m_btnReplay->setText(m_replayRunning ? QStringLiteral("停止补传")
                                             : QStringLiteral("开始补传历史数据"));
    }
    if (m_replayStatus == nullptr) return;

    /* TODO(阶段6)：从 DevCtl 取最近一次补传状态（m_dev->lastReplay()），把
     *            "补传状态：… 已补传 N 条 ｜ 剩余 M 条"与历史入库计数
     *            （m_model.historyInserted() / historyDuplicate() / historyRejected()）
     *            拼成文本写进 m_replayStatus；状态名可用 AckFrame::replayStateName()，
     *            没收到应答时显示"未知（还没收到板2 应答）"。 */
}

void MainWindow::onDevStatusChanged(const DevStatus &st)
{
    m_devStatusKnown = st.valid;

    /* 回显设备真实状态时用 QSignalBlocker 挡住 toggled：
     * 否则查询回来的开关状态会被当成用户又点了一下，立刻回发一帧命令，形成循环。 */
    {
        const QSignalBlocker b1(m_chkFwd);
        const QSignalBlocker b2(m_chkCache);
        if (st.valid) {
            m_chkFwd->setChecked(st.fwd);
            m_chkCache->setChecked(st.cache);
        }
    }

    if (!st.valid) {
        m_devStatus->setText(QStringLiteral("板2 状态：未知（点查询板2 状态）"));
        return;
    }

    const quint32 s = st.uptimeS;
    m_devStatus->setText(QStringLiteral(
            "板2 状态：转发 %1 ｜ 缓存 %2\n"
            "收帧：好 %3 / 坏 %4\n"
            "缓存 %5 条（溢出丢 %6）｜ 运行 %7:%8:%9")
        .arg(st.fwd   ? QStringLiteral("开") : QStringLiteral("关"),
             st.cache ? QStringLiteral("开") : QStringLiteral("关"))
        .arg(st.rxOk).arg(st.rxBad)
        .arg(st.cacheCount).arg(st.lost)
        .arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg((s % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0')));
    m_devStatus->setStyleSheet(QStringLiteral("color:#222;"));
}

void MainWindow::onDevLog(const QString &line)
{
    logLine(line);
}

void MainWindow::onDevFailed(quint8 reqCmd, const QString &reason)
{
    Q_UNUSED(reqCmd);
    statusBar()->showMessage(QStringLiteral("下发失败：%1").arg(reason), 5000);
}

void MainWindow::buildHexLog()
{
    m_hexLog = new QPlainTextEdit(ui->statsBox);
    m_hexLog->setReadOnly(true);
    m_hexLog->setMaximumBlockCount(300);          /* 只留最近 300 行，内存有界 */
    m_hexLog->setMaximumHeight(140);
    QFont f(QStringLiteral("Consolas"));
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(9);
    m_hexLog->setFont(f);
    ui->verticalLayoutStats->addWidget(m_hexLog);
}

void MainWindow::connectAll()
{
    connect(ui->btnRefreshPorts, &QPushButton::clicked, this, &MainWindow::onRefreshPorts);
    connect(ui->btnOpenPort,     &QPushButton::clicked, this, &MainWindow::onOpenPortClicked);
    connect(ui->btnSelfTest,     &QPushButton::clicked, this, &MainWindow::onSelfTestClicked);
    connect(ui->btnClearData,    &QPushButton::clicked, this, &MainWindow::onClearData);
    connect(ui->btnExportCsv,    &QPushButton::clicked, this, &MainWindow::onExportCsv);
    connect(ui->btnClearAlarm,   &QPushButton::clicked, this, &MainWindow::onClearAlarm);
    connect(ui->chkCsv,          &QCheckBox::toggled,   this, &MainWindow::onCsvToggled);

    connect(ui->actStartCsv, &QAction::triggered, this, &MainWindow::onStartCsvDialog);
    connect(ui->actStopCsv,  &QAction::triggered, this, &MainWindow::onStopCsv);
    connect(ui->actExportCsv, &QAction::triggered, this, &MainWindow::onExportCsv);
    connect(ui->actQuit,     &QAction::triggered, this, &QWidget::close);
    connect(ui->actSelfTest, &QAction::triggered, this, &MainWindow::onSelfTestClicked);
    connect(ui->actFeedDemo, &QAction::triggered, this, &MainWindow::onFeedDemo);
    connect(ui->actClear,    &QAction::triggered, this, &MainWindow::onClearData);
    connect(ui->actProtocol, &QAction::triggered, this, &MainWindow::onProtocolDialog);
    connect(ui->actAbout,    &QAction::triggered, this, &MainWindow::onAboutDialog);

    m_serial = new QSerialPort(this);
    connect(m_serial, &QSerialPort::readyRead, this, &MainWindow::onSerialReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &MainWindow::onSerialError);

    /* 设备控制通道：DevCtl 只管发与等，日志/状态回显都走主窗口 */
    connect(m_dev, &DevCtl::logText,        this, &MainWindow::onDevLog);
    connect(m_dev, &DevCtl::statusChanged,  this, &MainWindow::onDevStatusChanged);
    connect(m_dev, &DevCtl::replayChanged,  this, &MainWindow::onDevReplayChanged);
    connect(m_dev, &DevCtl::commandFailed,  this, &MainWindow::onDevFailed);

    /* X 轴条上的圆点单击：连接在 setupPlot() 里随 m_xStrip 一起建立。
     * 主图上的曲线不可选中，避免"点到折线"与"点到 X 轴上的点"混在一起。 */
}

/* ============================================================================
 *  串口
 * ==========================================================================*/

void MainWindow::onRefreshPorts()
{
    const QString keep = ui->comboPort->currentData().toString();
    ui->comboPort->clear();

    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        const QString desc = info.description().isEmpty()
                                 ? QStringLiteral("无描述") : info.description();
        ui->comboPort->addItem(QStringLiteral("%1 (%2)").arg(info.portName(), desc),
                               info.portName());
    }
    if (ui->comboPort->count() == 0)
        ui->comboPort->addItem(QStringLiteral("（未发现串口，检查 CH340 驱动）"), QString());

    if (!keep.isEmpty()) {
        const int idx = ui->comboPort->findData(keep);
        if (idx >= 0) ui->comboPort->setCurrentIndex(idx);
    }
    logLine(QStringLiteral("刷新串口列表：发现 %1 个").arg(ports.size()));
    statusBar()->showMessage(QStringLiteral("发现 %1 个串口").arg(ports.size()), 3000);
}

void MainWindow::onOpenPortClicked()
{
    if (m_serial->isOpen()) {
        m_serial->close();
        /* TODO(阶段6)：串口关闭时把设备控制通道摘下来（DevCtl::detach），在途命令作废。 */
        ui->btnOpenPort->setText(QStringLiteral("打开串口"));
        ui->lblSerialState->setText(QStringLiteral("状态：未连接"));
        ui->comboPort->setEnabled(true);
        ui->comboBaud->setEnabled(true);
        logLine(QStringLiteral("串口已关闭"));
        return;
    }

    const QString port = ui->comboPort->currentData().toString();
    if (port.isEmpty()) {
        ui->lblSerialState->setText(QStringLiteral("状态：没有可用串口"));
        return;
    }

    m_serial->setPortName(port);
    m_serial->setBaudRate(ui->comboBaud->currentText().toInt());
    m_serial->setDataBits(static_cast<QSerialPort::DataBits>(ui->comboDataBits->currentData().toInt()));
    m_serial->setParity(static_cast<QSerialPort::Parity>(ui->comboParity->currentData().toInt()));
    m_serial->setStopBits(static_cast<QSerialPort::StopBits>(ui->comboStopBits->currentData().toInt()));
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    /* 用 ReadWrite 而非 ReadOnly：控制命令要往同一个口写，只读打开时
     * m_serial->write() 会静默失败（返回 -1），表现为点按钮没反应、日志也没有。 */
    if (!m_serial->open(QIODevice::ReadWrite)) {
        ui->lblSerialState->setText(QStringLiteral("状态：打开失败（%1）").arg(m_serial->errorString()));
        logLine(QStringLiteral("打开 %1 失败：%2").arg(port, m_serial->errorString()));
        return;
    }

    /* TODO(阶段6)：控制通道跟着串口一起上线（DevCtl::attach(m_serial)）。 */

    /* TODO(阶段2)：新会话要把解析器的缓冲与统计清零（m_parser.reset()），
     *            避免上一轮的残包混进来。 */
    m_devStatusKnown = false;
    m_openWallMs = QDateTime::currentMSecsSinceEpoch();
    m_lastFrameWallMs = 0;
    m_silenceWarned = false;
    ui->btnOpenPort->setText(QStringLiteral("关闭串口"));
    ui->lblSerialState->setText(QStringLiteral("状态：已连接 %1 @ %2")
                                    .arg(port, ui->comboBaud->currentText()));
    ui->comboPort->setEnabled(false);
    ui->comboBaud->setEnabled(false);
    logLine(QStringLiteral("已打开 %1 @ %2 8N1（双向：收数据 + 下发控制）")
                .arg(port, ui->comboBaud->currentText()));
    statusBar()->showMessage(QStringLiteral("串口已打开，等待板2 转发的 20 字节帧…"));
}

void MainWindow::onSerialReadyRead()
{
    const QByteArray chunk = m_serial->readAll();
    if (chunk.isEmpty()) return;

    /* TODO(阶段2)：把这一批字节喂给解析器（m_parser.feed(chunk)），再把组好的环境帧
     *            逐条取出来（m_parser.takeFrame）交给 handleFrame(f, 当前毫秒)。
     *            补传回来的历史帧（CMD 0x15）走单独一条队列（m_parser.takeHistory），
     *            交给 handleHistoryFrame(h)：它只入库、回填曲线，不参与实时告警。
     * TODO(阶段6)：控制应答帧（CMD 带 0x80）交给 DevCtl 匹配在途命令
     *            （m_parser.takeAck → m_dev->onAck）。
     * TODO(阶段2)：比对喂入前后的解析统计（m_parser.stats()）：CRC 错、帧尾错、长度字段
     *            非法时各记一条日志（CRC 那条要带上本批字节的十六进制）。 */
}

void MainWindow::onSerialError(QSerialPort::SerialPortError err)
{
    if (err == QSerialPort::NoError) return;

    logLine(QStringLiteral("串口错误：%1").arg(m_serial->errorString()));

    /* 拔线/驱动异常：ResourceError 之后端口句柄已不可用，必须关闭并放开界面 */
    if (err == QSerialPort::ResourceError || err == QSerialPort::DeviceNotFoundError) {
        if (m_serial->isOpen()) m_serial->close();
        /* TODO(阶段6)：串口异常断开时把设备控制通道摘下来（DevCtl::detach）。 */
        ui->btnOpenPort->setText(QStringLiteral("打开串口"));
        ui->lblSerialState->setText(QStringLiteral("状态：串口异常已断开"));
        ui->comboPort->setEnabled(true);
        ui->comboBaud->setEnabled(true);
        statusBar()->showMessage(QStringLiteral("串口异常断开（%1），可点“刷新”后重连")
                                     .arg(m_serial->errorString()));
    }
}

/* ============================================================================
 *  收帧处理
 * ==========================================================================*/

void MainWindow::handleFrame(const EnvFrame &f, qint64 wallMs)
{
    /* TODO(阶段2)：把这一帧并进数据模型（m_model.addFrame(f, wallMs)），同时刷新
     *            "最近收帧时刻"（m_lastFrameWallMs）与静默标志（m_silenceWarned）。
     *
     * TODO(阶段4)：逐项做物理量量程校验（DataModel::value 取值 + PhysRange::suspect）：
     *            超范围项累计 m_rangeSuspect[i]/m_rangeSuspectTotal，只给这些项置
     *            suspectMask 位，并且只在"进入/退出超范围"时各记一条日志（带
     *            PhysRange::text(s) 的合理范围），免得每秒数条把日志冲爆。
     *
     * TODO(阶段4)：做告警判定（m_alarm.evaluate(f, wallMs, suspectMask)）：每条新记录用
     *            appendAlarmRow(r) 写进告警表，并记一条"告警/恢复 + r.text()"日志。
     *
     * TODO(阶段5)：正在记录 CSV 时把这一帧追加进文件（m_csv.append(f, 时刻,
     *            FrameProto::OK, suspectMask)），再把行数与路径写进 ui->lblCsvInfo。 */
    (void)f;
    (void)wallMs;
}

/* 补传回来的历史帧：只做三件事，入库、按需记 CSV、累计待显示条数。
 * 刻意不做的事：不调 AlarmManager::evaluate（两小时前的越限数据不该现在弹告警），
 * 不动 m_lastFrameWallMs 与 m_silenceWarned（历史帧不是实时链路的证据），
 * 不做量程校验计数（那是实时链路的质量指标）。 */
void MainWindow::handleHistoryFrame(const HistoryFrame &h)
{
    /* TODO(阶段2)：把历史帧并进数据模型（m_model.addHistory(h, &wallMs)）：重复或不可用
     *            就直接返回（模型内部已计数），成功就累计 m_histNewSinceTick。
     * TODO(阶段5)：正在记录 CSV 时追加这一帧（时间列写模型回填的原始采集时刻，
     *            来源标 CsvRecorder::SourceHistory），再把行数与路径写进 ui->lblCsvInfo。
     * 刻意不做的事：不调 AlarmManager::evaluate（两小时前的越限数据不该现在弹告警），
     * 不动 m_lastFrameWallMs 与 m_silenceWarned（历史帧不是实时链路的证据），
     * 不做量程校验计数（那是实时链路的质量指标）。 */
    (void)h;
}

/* ============================================================================
 *  刷新
 * ==========================================================================*/

void MainWindow::onPlotTick()
{
    refreshCards();
    refreshPlot();
    /* 统计区跟着曲线一起刷：它只扫最近 50 个点，代价远小于重绘；改由 1 s 定时器
     * 刷会让"窗口点数"看起来比曲线滞后一格，读起来像少了一个点。 */
    refreshStatPanel();

    /* 下发通道的超时/重发挂在 50 ms 这个定时器上：DevCtl 里写的"600 ms 超时"
     * 才是真的 600 ms（挂在 1 s 的统计定时器上会变成 1 s 才判超时，参数与行为对不上）。 */
    /* TODO(阶段6)：在这里驱动下发通道的超时/重发（m_dev->tick()）。 */
}

void MainWindow::onStatsTick()
{
    refreshStats();
    updateReplayLabel();

    /* 补传进度汇总：每秒一条，而不是每帧一条。2048 条缓存补回来时，
     * 逐帧记日志会把左侧日志与 hex 区全冲掉，反倒看不见链路异常。 */
    if (m_histNewSinceTick > 0) {
        /* TODO(阶段2/6)：补传进度汇总：记一条"补传入库 N 条，累计 X 条（重复丢弃 Y）"
         *            （m_model.historyInserted() / historyDuplicate()），记完把
         *            m_histNewSinceTick 清零。 */
        m_histNewSinceTick = 0;
        /* 曲线与卡片要重画：历史点是插进曲线中间位置的，不能靠增量 append */
        refreshPlot();
        refreshCards();
    }

    /* 补传中长时间收不到进度应答：提示链路问题，不自动重发（重发开始命令会让
     * 板2 从头再传一遍，重复数据虽然会被去重丢掉，但白占带宽与时间）。 */
    if (m_replayRunning && !m_replayWarned && m_replayAckMs > 0
        && QDateTime::currentMSecsSinceEpoch() - m_replayAckMs > 5000) {
        m_replayWarned = true;
        logLine(QStringLiteral("补传中已 5 s 未收到进度应答（0x96），"
                               "可查板2 缓存是否为空、串口是否在收数据"));
    }

    /* 静默检测：串口开着但 3 s 没帧，多半是板2 转发没开或波特率不对 */
    if (m_serial != nullptr && m_serial->isOpen() && !m_silenceWarned) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastFrameWallMs == 0) {
            if (now - m_openWallMs > 3000) {
                m_silenceWarned = true;
                logLine(QStringLiteral("串口已开 3 s 未收到任何帧："
                                       "依次查 ①板2 是否在跑（ForwardTask 有没有打日志）"
                                       " ②P6 跳线是否在 SP3232 侧 ③波特率/接线（TX-RX 要交叉）"));
                statusBar()->showMessage(QStringLiteral("3 s 未收到帧，见左侧日志"));
            }
        } else if (now - m_lastFrameWallMs > 3000) {
            m_silenceWarned = true;
            logLine(QStringLiteral("已 %1 ms 没有新帧，链路可能中断")
                        .arg(now - m_lastFrameWallMs));
            statusBar()->showMessage(QStringLiteral("超过 3 s 未收到帧"));
        }
    }
}

void MainWindow::refreshCards()
{
    /* TODO(阶段3)：从数据模型取最新一帧（m_model.hasData() / m_model.latest()），把每个
     *            物理量的值写进对应卡片 m_cardValue[i]：
     *            - 一条数据都没有时写 "--" 并置灰（color:#999;border:none;）；
     *            - 无效值（NaN，如湿度读失败）也写 "--"；
     *            - 否则按 DataModel::decimals(s) 的位数格式化数字；
     *            - 三级配色：告警红 #c0392b（m_alarm.state(s)）> 超范围琥珀 #b8860b
     *              （PhysRange::suspect）> 正常深灰 #222；超范围时用 PhysRange::text(s)
     *              给 setToolTip 写"超出合理范围…暂不参与告警判定"。
     *            卡片控件本身的创建与样式表在构建界面时已完成，不需要在这里重复。 */
}

/* QCustomPlot 2.x 的 setData 只接受两条 QVector<double>，没有 QVector<QPointF> 重载，
   所以曲线数据要在这里拆成 keys / values。y 为 NaN 的点会被断线跳过，湿度无效时正好
   靠它把曲线断开而不是画到 0。 */
static void splitPoints(const QVector<QPointF> &pts, QVector<double> &keys, QVector<double> &vals)
{
    keys.resize(pts.size());
    vals.resize(pts.size());
    for (int i = 0; i < pts.size(); ++i) {
        keys[i] = pts.at(i).x();
        vals[i] = pts.at(i).y();
    }
}

void MainWindow::refreshPlot()
{
    QCustomPlot *p = ui->plotEnv;
    /* TODO(阶段3)：帧数要从数据模型来（m_model.size()）。 */
    const int n = 0;
    if (n <= 0) {
        /* 一帧都没有：X 轴以 0 为起点，两端各留半个序号，刻度落在整数上 */
        p->xAxis->setRange(-0.5, 5.5);
        if (m_xStrip != nullptr) m_xStrip->xAxis->setRange(-0.5, 5.5);
        /* 没有数据时左轴也要跟着当前查看量，否则会停在初始的温度量程上，
         * 出现"标签写湿度、刻度却是 -10~50"这种对不上的显示。 */
        const int view0 = qBound(0, m_statSeries, int(Series::Count) - 1);
        m_viewAxis->setRange((kYMin[view0] < 0.0) ? 0.0 : kYMin[view0], kYMax[view0]);
        for (int i = 0; i < int(Series::Count); ++i) {
            if (m_graph[i] != nullptr) m_graph[i]->data()->clear();
        }
        m_shownFrames = 0;
        syncXStripMargins();
        rebuildXStrip();
        p->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    /* 只画当前查看量那一条：其余曲线数据不再刷新，省掉无用的数据搬运。 */
    const int view = qBound(0, m_statSeries, int(Series::Count) - 1);
    /* TODO(阶段3)：取当前查看量的历史点（m_model.history(Series(view))），交给下面的
     *            splitPoints 拆成 keys/values 再 setData。 */
    const QVector<QPointF> pts;
    QVector<double> keys, vals;
    /* 序号是"下标 + 1"，补传插队会让整段序号前移，所以补传后也必须整条重建。
     * 切换查看量换的是另一条曲线，它自己的点数是 0（或少于总帧数），
     * 因此这里用"该曲线自身的点数"判断，不能用全局的 m_shownFrames，
     * 否则切到别的量时 add = 0，曲线会一直空着。 */
    const bool rebuild = (m_graph[view]->dataCount() < pts.size()) ||
                         (pts.size() < int(m_shownFrames));
    if (rebuild) {
        splitPoints(pts, keys, vals);
        m_graph[view]->setData(keys, vals, true);
    } else {
        const int add = pts.size() - int(m_shownFrames);
        if (add > 0) {
            splitPoints(pts.mid(qMax(0, pts.size() - add)), keys, vals);
            if (!keys.isEmpty()) m_graph[view]->addData(keys, vals, true);
        }
    }
    /* TODO(阶段3)：已显示的点数按数据模型的总帧数记（m_model.stamps().size()）；
     *            这里先用本条曲线的点数占位，保证后面的增量 addData 判断不会错。 */
    m_shownFrames = quint64(pts.size());
    for (int i = 0; i < int(Series::Count); ++i) {
        if (m_graph[i] != nullptr) m_graph[i]->setVisible(i == view);
    }

    /* 默认看最近 120 组数据；两端各留 0.5 个序号余量，保证首点与末点都完整
     * 落在可视区内（原来范围是 1 ~ n+2，第一帧正好压在左侧轴线上，半个点被
     * 切掉，看起来就是"第一帧数据没显示"）。 */
    const double xLast = double(n);
    const double xFrom = (n <= 120) ? 1.0 : (xLast - 119.0);
    p->xAxis->setRange(xFrom - 0.5, xLast + 0.5);

    /* Y 轴下界固定为 0，上界按当前查看量的数据自适应；勾掉"Y 轴自动量程"
     * 时改用该量的固定量程（温度固定量程的下界是 -10，按需求抬到 0）。 */
    /* TODO(阶段3)：Y 轴自适应要用当前查看量的同一批点（这里与上面那条曲线同源）。 */
    const QVector<QPointF> &vpts = pts;
    if (m_chkYAuto != nullptr && m_chkYAuto->isChecked()) {
        double hi = 0.0;
        bool any = false;
        for (const QPointF &pt : vpts) {
            const double y = pt.y();
            if (!std::isfinite(y)) continue;             /* NaN/Inf（湿度无效）不参与定范围 */
            if (!any) { hi = y; any = true; }
            else if (y > hi) hi = y;
        }
        if (!any) hi = 1.0;
        if (hi < 1e-9) hi = 1.0;                         /* 恒 0：给一个可见的轴高 */
        m_viewAxis->setRange(0.0, hi * 1.10 + 1e-9);     /* 下界 0，上界留 10% 余量 */
    } else {
        const double lo = (kYMin[view] < 0.0) ? 0.0 : kYMin[view];
        m_viewAxis->setRange(lo, kYMax[view]);
    }

    /* 主图轴矩形可能刚因为 Y 轴标签/刻度宽度变化而变化，先同步点条边距，
     * 再画点条，保证两者像素对齐 */
    syncXStripMargins();
    rebuildXStrip();
    p->replot(QCustomPlot::rpQueuedReplot);
}

/* 点条上可以点中的序号：当前 X 轴可视范围内所有有有效数据的样本，不做抽稀。
 * 命中判定与 tooltip 都用这一组，因此"画出来的点少，但可视范围内到处都能点"，
 * 点击落在两个绘制点之间时会选中离点击处最近的那个样本。 */
QVector<double> MainWindow::stripHitNumbers() const
{
    QVector<double> out;
    /* TODO(阶段3)：数据模型里一帧都没有就直接返回（m_model.size()）。 */
    if (ui->plotEnv == nullptr) return out;

    const Series s = Series(qBound(0, m_statSeries, int(Series::Count) - 1));
    (void)s;      /* 占位：接入数据模型后这一行去掉，s 用来取该量的历史点 */
    /* TODO(阶段3)：取当前查看量（s）的历史点（m_model.history(s)）。 */
    const QVector<QPointF> pts;
    const double lo = ui->plotEnv->xAxis->range().lower;
    const double hi = ui->plotEnv->xAxis->range().upper;

    for (int i = 0; i < pts.size(); ++i) {
        const double x = pts.at(i).x();
        if (x >= lo && x <= hi && std::isfinite(pts.at(i).y())) out.append(x);
    }
    return out;
}

/* 点条上真正画出来的序号：整个绘制集合统一走 kStripMinGapPx 像素抽稀。
 * 遍历顺序是"从最新的样本往前走"：这样最近 kMarkerPoints 个样本先被考虑，
 * 在间距约束下优先保留，但它们不再是硬性必画——密集视图下（默认视图就是最近
 * 120 组数据，约 5 px/样本）末尾这一段同样会被抽稀，不会挤成一坨黑段。
 * 总数超过 kStripMaxDots 时只保留末尾 kStripMaxDots 个。
 * 被抽稀掉的样本仍然可以点中（命中口径见 stripHitNumbers()），因此不影响取数。 */
QVector<double> MainWindow::stripDrawNumbers() const
{
    QVector<double> out;
    /* TODO(阶段3)：数据模型里一帧都没有就直接返回（m_model.size()）。 */
    if (m_xStrip == nullptr || ui->plotEnv == nullptr) return out;

    const Series s = Series(qBound(0, m_statSeries, int(Series::Count) - 1));
    (void)s;      /* 占位：接入数据模型后这一行去掉，s 用来取该量的历史点 */
    /* TODO(阶段3)：取当前查看量（s）的历史点（m_model.history(s)）。 */
    const QVector<QPointF> pts;
    const int np = pts.size();
    if (np <= 0) return out;

    const double lo = ui->plotEnv->xAxis->range().lower;
    const double hi = ui->plotEnv->xAxis->range().upper;

    /* 一个 pass、从最新往旧：先判可视范围，再判与已绘制点的像素间距 */
    QVector<bool> keep(np, false);
    QVector<double> drawnPx;
    for (int i = np - 1; i >= 0; --i) {
        const double x = pts.at(i).x();
        if (x < lo || x > hi || !std::isfinite(pts.at(i).y())) continue;
        const double px = m_xStrip->xAxis->coordToPixel(x);
        bool far = true;
        for (double q : drawnPx) {
            if (qAbs(q - px) < double(kStripMinGapPx)) { far = false; break; }
        }
        if (!far) continue;
        keep[i] = true;
        drawnPx.append(px);
    }

    for (int i = 0; i < np; ++i)
        if (keep.at(i)) out.append(pts.at(i).x());

    if (out.size() > kStripMaxDots) out = out.mid(out.size() - kStripMaxDots);
    return out;
}

/* X 轴条上的可点击数据点：按"绘制口径"画点（末尾 20 个 + 间距足够的补画点）。
 * 圆点贴着 X 轴画在窄条 m_xStrip 上，不压曲线区、不挡主图的拖动与缩放；
 * 选中的那一个画成实心加圈（它是单独一个图元，因此即使不在绘制集合里也能画出来），
 * 其余画成空心弱化，便于一眼分辨。 */
void MainWindow::rebuildXStrip()
{
    if (m_xStrip == nullptr || m_xMarkerGraph == nullptr || m_viewAxis == nullptr) return;

    const Series s = Series(qBound(0, m_statSeries, int(Series::Count) - 1));
    const QColor c(kColors[int(s)]);
    const QVector<double> numbers = stripDrawNumbers();

    QVector<double> keys, vals, selKeys, selVals;
    bool selDrawn = false;
    for (double x : numbers) {
        if (m_hasSel && qAbs(x - m_selX) < 0.5) {
            selKeys.append(x);
            selVals.append(0.5);
            selDrawn = true;
        } else {
            keys.append(x);
            vals.append(0.5);
        }
    }
    /* 选中样本不在绘制集合里（被抽稀掉、或横轴放大到别处）时也把选中标记画出来，
     * 竖线与读数才对得上 */
    if (m_hasSel && !selDrawn) {
        selKeys.append(m_selX);
        selVals.append(0.5);
    }

    m_xMarkerGraph->setData(keys, vals, true);
    m_xMarkerGraph->setLineStyle(QCPGraph::lsNone);
    m_xMarkerGraph->setPen(QPen(Qt::NoPen));
    /* 普通点：直径约 8 px 的空心圆（弱化），整条高度都可点，不要求点得准 */
    m_xMarkerGraph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc,
                                                    QPen(c, 1.6), QBrush(Qt::white), 8.0));
    m_xMarkerGraph->setVisible(!keys.isEmpty() || !selKeys.isEmpty());

    m_selDotGraph->setData(selKeys, selVals, true);
    m_selDotGraph->setLineStyle(QCPGraph::lsNone);
    m_selDotGraph->setPen(QPen(Qt::NoPen));
    /* 选中点：直径约 12 px 的实心加外圈，截图上也一眼能看出来 */
    m_selDotGraph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc,
                                                   QPen(QColor(192, 57, 43), 2.6), QBrush(c), 12.0));
    m_selDotGraph->setVisible(!selKeys.isEmpty());

    /* 选中竖线跟着 X 轴条一起重画，保证与主图对齐 */
    if (m_selLine != nullptr) {
        /* TODO(阶段3)：数据模型里有数据时才显示选中虚线（m_model.size() > 0）。 */
    m_selLine->setVisible(m_hasSel);
        if (m_hasSel) {
            m_selLine->point1->setCoords(m_selX, 0.0);
            m_selLine->point2->setCoords(m_selX, 1.0);
        }
    }
    updateStatsWarn();
    m_xStrip->replot(QCustomPlot::rpQueuedReplot);
}

/* 左侧唯一 Y 轴与底部颜色说明条一起跟随"当前查看量"：
 * 轴标签、单位、刻度颜色、量程都换成该物理量的；说明条里当前项加下划线加粗。 */
void MainWindow::updateViewAxis()
{
    if (m_viewAxis == nullptr) return;
    const int i = qBound(0, m_statSeries, int(Series::Count) - 1);
    const Series s = Series(i);
    const QColor c(kColors[i]);

    m_viewAxis->setLabel(QStringLiteral("%1 (%2)")
                             .arg(QString::fromUtf8(DataModel::name(s)),
                                  QString::fromUtf8(DataModel::unit(s))));
    m_viewAxis->setLabelColor(c);
    m_viewAxis->setTickLabelColor(c);
    m_viewAxis->setTickPen(QPen(c, 1.0));

    /* 顶部当前值卡片：当前查看量那一张加粗边框，其余降到灰字，
     * 一眼能看出"现在看的是哪一个量"（6 个当前值都还保留）。 */
    const bool cardsReady = (m_cardName[0] != nullptr);
    if (cardsReady) {
        QWidget *focused = m_cardName[i] != nullptr ? m_cardName[i]->parentWidget() : nullptr;
        for (int k = 0; k < int(Series::Count); ++k) {
            if (m_cardName[k] == nullptr) continue;
            QWidget *card = m_cardName[k]->parentWidget();
            if (card == nullptr) continue;
            const bool cur = (k == i);
            card->setStyleSheet(QStringLiteral("QFrame{border-left:4px solid %1;background:%2;%3}")
                                    .arg(QLatin1String(kColors[k]),
                                         cur ? QStringLiteral("#fbfbfb") : QStringLiteral("#f4f4f4"),
                                         cur ? QStringLiteral("border-top:2px solid #555;border-right:2px solid #555;border-bottom:2px solid #555;")
                                             : QString()));
            m_cardName[k]->setStyleSheet(QStringLiteral("color:%1;font-size:12px;border:none;")
                                             .arg(cur ? QStringLiteral("#333") : QStringLiteral("#8a8a8a")));
            m_cardValue[k]->setStyleSheet(QStringLiteral("color:%1;border:none;")
                                              .arg(cur ? QStringLiteral("#222") : QStringLiteral("#9a9a9a")));
        }
        Q_UNUSED(focused);
    }

    if (m_lblLegend != nullptr) {
        QStringList parts;
        for (int k = 0; k < int(Series::Count); ++k) {
            const bool cur = (k == i);
            parts << QStringLiteral("<a href='view:%1' style='color:%2;text-decoration:none;"
                                    "font-weight:%3;'>%4%5</a>")
                         .arg(k)
                         .arg(QLatin1String(kColors[k]),
                              cur ? QStringLiteral("700") : QStringLiteral("400"),
                              cur ? QStringLiteral("[ ") : QStringLiteral("&nbsp;&nbsp;-&nbsp;"),
                              QString::fromUtf8(DataModel::name(Series(k))));
        }
        m_lblLegend->setText(parts.join(QStringLiteral("&nbsp;&nbsp;&nbsp;")));
    }
}

void MainWindow::refreshStats()
{
    /* TODO(阶段2)：帧统计取自解析器（m_parser.stats()）：成帧/应答/喂入字节数、CRC 错、
     *            长度错、帧尾错、重同步丢字节、估算丢帧、缓冲里未成帧字节数；
     *            帧间隔按最小/最大间隔（st.minGapMs / st.maxGapMs / st.expectedIntervalMs）
     *            拼成"帧间隔：最小 x ms / 最大 y ms（期望 z ms）"。
     * TODO(阶段3)：静默时长 = 当前时刻 - m_lastFrameWallMs（没收到过帧时写"--"）。
     * TODO(阶段4)：量程可疑计数用 m_rangeSuspectTotal 与 m_rangeSuspect[]，只在有值时
     *            展开"物理量名 次数"明细（名字用 DataModel::name()）。
     * TODO(阶段4)：告警中项数与累计进入次数取 m_alarm.activeCount() / m_alarm.enterCount()。
     * TODO(阶段5)：CSV 行数取 m_csv.rows()。
     * TODO(阶段6)：下发/应答/超时计数取 m_dev->txCount() / rxCount() / timeoutCount()。
     *            以上拼成多行文本写进 ui->lblStats，静默时长单独一行附在最后。 */
}

void MainWindow::appendAlarmRow(const AlarmManager::Record &r)
{
    const int row = ui->tblAlarm->rowCount();
    ui->tblAlarm->insertRow(row);

    auto item = [](const QString &s, const QColor &c = QColor()) {
        QTableWidgetItem *it = new QTableWidgetItem(s);
        if (c.isValid()) it->setForeground(c);
        return it;
    };

    const QColor c = r.recovery ? QColor("#1e8449") : QColor("#c0392b");

    ui->tblAlarm->setItem(row, 0, item(QDateTime::fromMSecsSinceEpoch(r.ms)
                                           .toString("HH:mm:ss"), c));
    ui->tblAlarm->setItem(row, 1, item(QString::fromUtf8(DataModel::name(r.series)), c));
    ui->tblAlarm->setItem(row, 2, item(r.recovery ? QStringLiteral("恢复")
                                                  : AlarmManager::levelText(r.level), c));
    ui->tblAlarm->setItem(row, 3, item(QString::number(r.value, 'f', DataModel::decimals(r.series)), c));
    ui->tblAlarm->setItem(row, 4, item(QString::number(r.limit, 'f', DataModel::decimals(r.series)), c));

    ui->tblAlarm->scrollToBottom();
}

void MainWindow::logLine(const QString &s)
{
    if (m_hexLog == nullptr) return;   /* 构造函数里万一先来一条日志，宁可不显示也别闪退 */
    m_hexLog->appendPlainText(QStringLiteral("[%1] %2")
                                  .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), s));
    m_hexLog->moveCursor(QTextCursor::End);
}

/* ============================================================================
 *  功能按钮
 * ==========================================================================*/

void MainWindow::onCsvToggled(bool on)
{
    if (!on) { onStopCsv(); return; }

    /* TODO(阶段5)：勾上"记录 CSV"时开始记录：
     *            - 已经用"开始记录 CSV…"菜单指定过文件时（m_csv.isRecording()），只把
     *              "记录中（%1 行）+ 路径"同步到 ui->lblCsvInfo，不要再开第二个文件；
     *            - 否则在 data/ 目录下按"环境数据_yyyyMMdd_HHmmss.csv"生成路径，
     *              调 m_csv.start(path, &err)：失败就 QMessageBox::warning 并把
     *              ui->chkCsv 取消勾选，成功则把"记录中（0 行）+ 路径"写进
     *              ui->lblCsvInfo 并记一条"开始记录 CSV：%1"日志。 */
}

void MainWindow::onStartCsvDialog()
{
    QDir().mkpath(QDir::current().filePath(QStringLiteral("data")));
    const QString def = QDir::current().filePath(
        QStringLiteral("data/环境数据_%1.csv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存为 CSV"),
                                                      def, QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;

    /* TODO(阶段5)：把这个选中的路径交给 CSV 记录器开始记录（m_csv.start(path, &err)）：
     *            失败弹 QMessageBox::warning；成功则勾上 ui->chkCsv（同步勾选框）并把
     *            "记录中（0 行）+ 路径"写进 ui->lblCsvInfo。 */
    (void)path;
}

void MainWindow::onStopCsv()
{
    /* TODO(阶段5)：正在记录时先记一条"停止记录 CSV（共 %1 行）：%2"日志，再
     *            m_csv.stop() 收尾（stop() 里会 close + flush 文件头，必须执行到）。 */
    if (ui->chkCsv->isChecked()) ui->chkCsv->setChecked(false);
    ui->lblCsvInfo->setText(QStringLiteral("未记录"));
}

void MainWindow::onExportCsv()
{
    /* TODO(阶段5)：一条数据都没有时提示"还没有数据。"并返回（m_model.hasData()）。 */
    const QString def = QDir::current().filePath(
        QStringLiteral("data/导出_%1.csv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出当前数据"),
                                                      def, QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;

    /* TODO(阶段5)：把数据模型里的历史导出到这个路径（m_model.saveHistoryCsv(path, &err)）：
     *            失败弹 QMessageBox::warning，成功记一条"已导出 %1 帧到 %2"日志
     *            （帧数取 m_model.size()）。 */
    (void)path;
}

void MainWindow::onSelfTestClicked()
{
    const QVector<CaseResult> r = ConsistencyTest::runBuiltin();
    const QString text = ConsistencyTest::toText(r, QStringLiteral("Qt 上位机 · 帧解析自检"));

    /* 同时落盘一份，方便附进验收文档 */
    QFile f(QDir::current().filePath(QStringLiteral("自检报告.txt")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        ts << QChar(0xFEFF) << text;
        f.close();
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("一致性自检（结果也写进 自检报告.txt）"));
    dlg.resize(760, 480);
    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    QPlainTextEdit *pe = new QPlainTextEdit(text, &dlg);
    pe->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    pe->setFont(mono);
    lay->addWidget(pe);
    dlg.exec();

    logLine(QStringLiteral("一致性自检：通过 %1/%2（帧解析正确率 %3%）")
                .arg(ConsistencyTest::passedCount(r)).arg(r.size())
                .arg(ConsistencyTest::passRate(r), 0, 'f', 1));
}

void MainWindow::onClearData()
{
    /* TODO(阶段2)：把数据模型的帧、解析器的缓冲/统计、告警管理器一起清零
     *            （m_model.clear() / m_parser.reset() / m_alarm.reset()）。 */
    m_shownFrames = 0;
    m_lastFrameWallMs = 0;
    m_silenceWarned = false;
    m_histNewSinceTick = 0;             /* 去重键表与历史计数随 m_model.clear() 一起清零 */
    m_rangeSuspectTotal = 0;
    for (int i = 0; i < int(Series::Count); ++i) {
        m_rangeSuspect[i] = 0;
        m_suspectState[i] = false;
    }

    for (int i = 0; i < int(Series::Count); ++i) {
        if (m_graph[i] != nullptr) m_graph[i]->data()->clear();
    }
    /* 选中状态与 X 轴上的点一起清掉：m_selX 还留着旧序号的话，清空后重画会在
     * 空图上继续画一条竖线和一个点。 */
    m_hasSel = false;
    if (m_selLine != nullptr) m_selLine->setVisible(false);
    if (m_xMarkerGraph != nullptr) m_xMarkerGraph->data()->clear();
    if (m_selDotGraph != nullptr) m_selDotGraph->data()->clear();
    rebuildXStrip();
    refreshStatPanel();
    updateViewAxis();
    ui->tblAlarm->setRowCount(0);
    ui->plotEnv->replot();
    refreshCards();
    refreshStats();
    updateReplayLabel();
    logLine(QStringLiteral("已清空数据、统计与告警记录"));
}

void MainWindow::onClearAlarm()
{
    /* TODO(阶段4)：清掉告警管理器里已记录的状态与计数（m_alarm.clearRecords()）。 */
    ui->tblAlarm->setRowCount(0);
    logLine(QStringLiteral("已清空告警记录表"));
}

/* 演示数据：按正弦造 count 帧，帧间隔 1 s（与板1 的 1 s 采样周期一致）。
 * 用途有两个：菜单里的"灌入一帧演示数据"（count = 1），以及 --shot 截图前
 * 灌满一屏（多帧）。数值是模拟的，只为界面自检。 */
void MainWindow::feedDemoFrames(int count)
{
    static int k = 0;
    /* TODO(阶段3)：基准时刻取模型里最后一帧的墙上时间（m_model.latestWallMs()），
     *            模型还是空的时候才用当前时刻。 */
    const qint64 base = QDateTime::currentMSecsSinceEpoch();

    for (int i = 0; i < count; ++i) {
        ++k;
        const double t = double(k) * 0.1;
        const qint16 temp  = qint16((22.0 + 6.0 * qSin(t)) * 100.0);
        const qint16 humi  = qint16((55.0 + 20.0 * qSin(t * 0.7 + 1.0)) * 100.0);
        const qint16 press = qint16((1013.0 + 3.0 * qSin(t * 0.3)) * 10.0);
        const quint16 lux  = quint16(400.0 + 300.0 * qSin(t * 1.3));
        const quint16 tvoc = quint16(300.0 + 900.0 * qSin(t * 0.5 + 2.0));
        const quint16 mq   = quint16(1200.0 + 900.0 * qSin(t * 0.9));

        EnvFrame f;
        f.cmd = FrameProto::CMD_ENV;
        f.len = FrameProto::ENV_LEN;
        f.tempX100 = temp;
        f.humiX100 = humi;
        f.pressX10 = press;
        f.lightLx = lux;
        f.tvocPpb = tvoc;
        f.mq135Raw = mq;

        /* 每帧只隔 1 s：文件里的时间轴才会有可见长度，点位也能分开点 */
        handleFrame(f, base + qint64(i + 1) * 1000);
    }

    /* 灌完立刻刷一次图：50 ms 定时器要等事件循环才轮到，命令行截图（--shot）
     * 不跑事件循环就截，不刷这一次标记点与曲线都是空的。 */
    refreshPlot();
    refreshCards();
    refreshStatPanel();
}

void MainWindow::onFeedDemo()
{
    feedDemoFrames(1);
    /* TODO(阶段3)：日志里要报出累计灌入的点数，接上数据模型后取 m_model.size()。 */
    logLine(QStringLiteral("演示帧（数值为模拟，仅供界面自检）"));
}

void MainWindow::onProtocolDialog()
{
    const QString t = QStringLiteral(
        "环境帧 CMD=0x01，整帧 20 字节（大端）：\n"
        "\n"
        " [0][1]   AA 55            帧头\n"
        " [2]      01               命令 = 环境数据上报\n"
        " [3]      0C               数据段长度 = 12\n"
        " [4..5]   TEMP   int16     ℃   ×100（负数补码）\n"
        " [6..7]   HUMI   int16     %RH ×100（负值 = 无效标记）\n"
        " [8..9]   PRESS  int16     hPa ×10（×100 会溢出 uint16）\n"
        " [10..11] LIGHT  uint16    lx\n"
        " [12..13] TVOC   uint16    ppb\n"
        " [14..15] MQ135  uint16    ADC 原始值\n"
        " [16][17] CRC16-MODBUS     低字节在前，范围 = [2..15] 共 14 字节\n"
        " [18][19] 55 AA            帧尾\n"
        "\n"
        "解帧判据：\n"
        " 1) 帧头 + 长度 + CRC + 帧尾 四重确认；\n"
        " 2) 校验失败只丢 1 个字节再重找帧头（接缝 55 AA AA 55 不会连续丢帧）；\n"
        " 3) 数据段没有序号字段，丢帧只能按到达间隔 >1.5×期望间隔 估算。\n"
        "\n"
        "与固件对应：sys_frame.h（SYS_FRAME_CMD_ENV / SYS_FRAME_ENV_LEN）\n"
        "            板1 main.c frame_build() · 板2 main.c vTaskForward()");
    QMessageBox::information(this, QStringLiteral("协议速查"), t);
}

void MainWindow::onAboutDialog()
{
    QMessageBox::about(this, QStringLiteral("关于"),
        QStringLiteral("<b>环境监护系统 · 上位机</b><br><br>"
                       "链路：板1（F407ZE 采集）→ LoRa E22 → 板2（F407ZG 网关）"
                       "→ 串口 → 本机<br>"
                       "本程序：二进制帧解析 + 6 项数值/曲线 + 迟滞告警 + CSV 记录 + 一致性自检<br><br>"
                       "协议与固件同源：<code>sys_frame.h</code>（20 字节帧 / CRC16-MODBUS）<br>"
                       "构建：Qt 6.11.2 MinGW 64-bit + QCustomPlot"));
}

/* ============================================================================
 *  配置落盘（上位机配置.ini，与 exe 同目录）
 *  6 项阈值（值 + 启用哪一侧 + 回差）、告警总开关、Y 轴自动量程、量程校验、
 *  6 条曲线的勾选、串口参数都写进 ini，启动时读回。用 QSettings::IniFormat
 *  而不是注册表：ini 位置可见、可手改、可随工程一起拷贝。
 * ==========================================================================*/

QString MainWindow::configPath() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/上位机配置.ini");
}

void MainWindow::loadConfig()
{
    const QString path = configPath();
    if (!QFile::exists(path)) {
        logLine(QStringLiteral("未找到配置文件，使用默认阈值：%1").arg(path));
        return;
    }

    QSettings ini(path, QSettings::IniFormat);
    /* Qt 6 的 ini 一律按 UTF-8 读写，QSettings::setIniCodec() 已移除，这里不再设编码。
     * 键名全用 ASCII：中文键在跨版本时容易出问题。 */

    m_loading = true;           /* 回填期间别让 valueChanged 反手把状态改掉 */

    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);
        /* TODO(阶段4)：从 [thresholds/<物理量键名>] 读回阈值 low/lowEnabled/high/
         *            highEnabled/hyst（默认值取告警管理器里当前那一条
         *            m_alarm.threshold(s)），读完用 m_alarm.setThreshold(s, t) 写回去。 */

        /* 曲线勾选 */
        if (m_curveChk[i] != nullptr) {
            ini.beginGroup(QStringLiteral("curves/") + QLatin1String(DataModel::key(s)));
            const bool on = ini.value(QStringLiteral("visible"), m_curveChk[i]->isChecked()).toBool();
            ini.endGroup();
            m_curveChk[i]->setChecked(on);      /* 槽里会同步 graph/axis 可见性 + refreshPlot */
        }
    }

    ini.beginGroup(QStringLiteral("alarm"));
    /* TODO(阶段4)：读回告警总开关 [alarm] enabled（默认取 m_alarm.enabled()），
     *            用 m_alarm.setEnabled() 写回去，并同步界面上的总开关勾选状态。 */
    ini.endGroup();

    ini.beginGroup(QStringLiteral("ui"));
    if (m_chkYAuto) m_chkYAuto->setChecked(ini.value(QStringLiteral("yAuto"), m_chkYAuto->isChecked()).toBool());
    if (m_chkRange) m_chkRange->setChecked(ini.value(QStringLiteral("rangeCheck"), m_chkRange->isChecked()).toBool());
    /* 当前查看量（左轴与曲线跟随它）。放在曲线勾选之后恢复：切换查看量会自动
     * 勾上对应曲线，顺序反了会把 ini 里存的勾选状态覆盖掉。 */
    if (ui->comboStatSeries != nullptr) {
        const int v = ini.value(QStringLiteral("viewSeries"), ui->comboStatSeries->currentIndex()).toInt();
        ui->comboStatSeries->setCurrentIndex(qBound(0, v, int(Series::Count) - 1));
    }
    ini.endGroup();

    ini.beginGroup(QStringLiteral("serial"));
    const QString port = ini.value(QStringLiteral("port")).toString();
    const QString baud = ini.value(QStringLiteral("baud")).toString();
    if (!baud.isEmpty()) {
        const int idx = ui->comboBaud->findText(baud);
        if (idx >= 0) ui->comboBaud->setCurrentIndex(idx);
    }
    if (!port.isEmpty()) {
        /* 存的是 "COM3"，combo 里是 "COM3 (描述…)" → 用前缀匹配 */
        const int idx = ui->comboPort->findText(port, Qt::MatchStartsWith);
        if (idx >= 0) ui->comboPort->setCurrentIndex(idx);
    }
    ini.endGroup();

    m_loading = false;
    logLine(QStringLiteral("已加载配置：%1").arg(path));
}

void MainWindow::saveConfig()
{
    QSettings ini(configPath(), QSettings::IniFormat);

    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);
        /* TODO(阶段4)：把该量的当前阈值写进 [thresholds/<物理量键名>]：
         *            low / lowEnabled / high / highEnabled / hyst（取自
         *            m_alarm.threshold(s)）。 */

        if (m_curveChk[i] != nullptr) {
            ini.beginGroup(QStringLiteral("curves/") + QLatin1String(DataModel::key(s)));
            ini.setValue(QStringLiteral("visible"), m_curveChk[i]->isChecked());
            ini.endGroup();
        }
    }

    ini.beginGroup(QStringLiteral("alarm"));
    /* TODO(阶段4)：把告警总开关写进 [alarm] enabled（取自 m_alarm.enabled()）。 */
    ini.endGroup();

    ini.beginGroup(QStringLiteral("ui"));
    ini.setValue(QStringLiteral("yAuto"),      m_chkYAuto ? m_chkYAuto->isChecked() : false);
    ini.setValue(QStringLiteral("rangeCheck"), m_chkRange ? m_chkRange->isChecked() : true);
    ini.setValue(QStringLiteral("viewSeries"), ui->comboStatSeries ? ui->comboStatSeries->currentIndex() : 0);
    ini.endGroup();

    ini.beginGroup(QStringLiteral("serial"));
    /* 只存 "COM3" 这个端口名，不存 comboBox 里的整串显示文本
     * （显示文本带着"（蓝牙链接上的标准串行）"这种随系统语言变动的描述，
     *   存下来换台机器/换语言就匹配不上了）。 */
    ini.setValue(QStringLiteral("port"), ui->comboPort->currentText().section(QLatin1Char(' '), 0, 0));
    ini.setValue(QStringLiteral("baud"), ui->comboBaud->currentText());
    ini.endGroup();

    ini.sync();
}

/* 关窗：先落盘，再停 CSV。
 *  CSV 的 stop() 会 close + flush 文件头，必须保证被执行到，所以放最后且不依赖界面状态。 */
void MainWindow::closeEvent(QCloseEvent *e)
{
    saveConfig();
    /* TODO(阶段5)：关窗前停掉 CSV 记录（m_csv.stop() 会 close + flush 文件头），
     *            放在最后且不依赖界面状态，保证一定被执行到。 */
    QMainWindow::closeEvent(e);
}
