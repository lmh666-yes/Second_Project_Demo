#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "consistencytest.h"
#include "physrange.h"
#include "thresholddialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
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

/* 数值卡片的小数位统一走 DataModel::decimals()：界面、CSV、对话框共用一套，
 * 避免卡片显示 2 位而 CSV 写 1 位这类对不上的情况 */

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    /* 小屏幕保底：三栏外面套一层滚动区。三栏最小宽度合计约 1050 px、最小高度约
     *  930 px，比多数笔记本或远程桌面还大；不套滚动区时 Qt 会直接裁掉超出部分，
     *  表现为右边的阈值设置整块不见。套上以后窗口再小也只是出滚动条。 */
    QScrollArea *sa = new QScrollArea(this);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sa->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->horizontalLayoutMain->removeWidget(ui->splitter);
    sa->setWidget(ui->splitter);
    ui->horizontalLayoutMain->addWidget(sa);

    /* 拉伸只给中间那栏：窗口变宽时曲线图变大，左右两栏宽度基本不变 */
    ui->splitter->setStretchFactor(0, 0);
    ui->splitter->setStretchFactor(1, 1);
    ui->splitter->setStretchFactor(2, 0);
    setMinimumSize(960, 620);

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
    connectAll();

    /* 表格列 */
    ui->tblAlarm->setColumnCount(5);
    ui->tblAlarm->setHorizontalHeaderLabels(QStringList()
        << "时间" << "项目" << "类型" << "数值" << "阈值");
    ui->tblAlarm->horizontalHeader()->setStretchLastSection(true);
    ui->tblAlarm->verticalHeader()->setVisible(false);
    ui->tblAlarm->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tblAlarm->setSelectionBehavior(QAbstractItemView::SelectRows);

    /* 两个刷新定时器：50 ms 刷图/卡片，1 s 刷统计/告警/静默检测 */
    m_plotTimer = new QTimer(this);
    m_plotTimer->setInterval(50);
    connect(m_plotTimer, &QTimer::timeout, this, &MainWindow::onPlotTick);
    m_plotTimer->start();

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, &MainWindow::onStatsTick);
    m_statsTimer->start();

    ui->splitter->setSizes(QList<int>() << 330 << 660 << 350);   /* 真正的分配在 showEvent 里 */

    /* 阈值/开关/曲线勾选/串口参数；没有 ini 就用出厂默认值 */
    loadConfig();
    refreshThresholdPanel();

    refreshCards();
    refreshStats();
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
    const int right = 350;                              /* 阈值设置 + 告警记录 */
    const int left  = (total >= 1200) ? 330 : 300;      /* 串口 / 记录 / 统计 */
    const int mid   = qMax(380, total - left - right);  /* 其余全给曲线图 */
    ui->splitter->setSizes(QList<int>() << left << mid << right);
}

MainWindow::~MainWindow()
{
    if (m_csv.isRecording()) m_csv.stop();
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
    p->legend->setVisible(true);
    p->legend->setBrush(QColor(255, 255, 255, 210));
    p->axisRect()->setAutoMargins(QCP::msAll);
    p->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    p->xAxis->setLabel(QStringLiteral("时间 (s，相对第一帧)"));
    p->xAxis->setRange(0, 120);

    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);
        const QColor c(kColors[i]);

        if (i == 0) {
            m_axis[i] = p->yAxis;                       /* 左轴给温度 */
        } else {
            m_axis[i] = p->axisRect()->addAxis(QCPAxis::atRight);
            m_axis[i]->setVisible(false);
        }
        m_axis[i]->setLabel(QStringLiteral("%1 (%2)")
                                .arg(QString::fromUtf8(DataModel::name(s)),
                                     QString::fromUtf8(DataModel::unit(s))));
        m_axis[i]->setLabelColor(c);
        m_axis[i]->setTickLabelColor(c);
        m_axis[i]->setRange(kYMin[i], kYMax[i]);

        m_graph[i] = p->addGraph(p->xAxis, m_axis[i]);
        m_graph[i]->setName(QStringLiteral("%1 (%2)")
                                .arg(QString::fromUtf8(DataModel::name(s)),
                                     QString::fromUtf8(DataModel::unit(s))));
        m_graph[i]->setPen(QPen(c, 1.6));
        m_graph[i]->setVisible(false);
    }
    m_axis[0]->setVisible(false);                       /* 默认只显示温湿度 */
    m_graph[0]->setVisible(true);
    m_axis[0]->setVisible(true);
    m_graph[1]->setVisible(true);
    m_axis[1]->setVisible(true);

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
}

void MainWindow::buildCurveChecks()
{
    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);
        QCheckBox *chk = new QCheckBox(QStringLiteral("%1").arg(QString::fromUtf8(DataModel::name(s))),
                                       ui->centerPanel);
        chk->setStyleSheet(QStringLiteral("color:%1;font-weight:600;").arg(QLatin1String(kColors[i])));
        chk->setChecked(i < 2);                          /* 默认勾温湿度 */
        ui->curveCheckLayout->addWidget(chk);
        m_curveChk[i] = chk;

        connect(chk, &QCheckBox::toggled, this, [this, i](bool on) {
            m_graph[i]->setVisible(on);
            m_axis[i]->setVisible(on);
            refreshPlot();
        });
    }

    /* 两个数据边界开关，放在曲线勾选框这一行，不占左栏宽度：
     * Y 轴自动量程默认关，固定量程便于跨时间对比曲线形状，但气压恒 0 时会被压在
     * 950~1050 轴底部看不见，所以留一个按现有数据自适应的选项。
     * 物理量量程校验默认开：超范围的值照常显示、照记 CSV，只是不参与告警判定；
     * 板1 气压/光照/TVOC/MQ-135 尚未接入（帧里是 0），不拦会刷出气压过低假告警。 */
    m_chkYAuto = new QCheckBox(QStringLiteral("Y 轴自动量程"), ui->centerPanel);
    m_chkYAuto->setToolTip(QStringLiteral("按当前可见曲线的数据自动缩放 Y 轴；关掉则用固定量程（默认）"));
    m_chkYAuto->setChecked(false);
    ui->curveCheckLayout->addWidget(m_chkYAuto);
    connect(m_chkYAuto, &QCheckBox::toggled, this, [this](bool) { refreshPlot(); });

    m_chkRange = new QCheckBox(QStringLiteral("量程校验"), ui->centerPanel);
    m_chkRange->setToolTip(QStringLiteral(
        "超出物理合理范围的值标黄、计入统计，但不参与告警判定\n"
        "（板1 的气压/光照/TVOC/MQ-135 目前还没接，帧里恒为 0）"));
    m_chkRange->setChecked(true);
    ui->curveCheckLayout->addWidget(m_chkRange);

    ui->curveCheckLayout->addStretch(1);
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
        const AlarmManager::Threshold &th = m_alarm.threshold(s);
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

        m_thLow[i]  = mkSpin(th.low,  "低于这个值就报过低（改这里即启用下限判定）");
        m_thHigh[i] = mkSpin(th.high, "高于这个值就报过高（改这里即启用上限判定）");
        m_thHyst[i] = mkSpin(th.hyst, "回差/迟滞：报警后要走出 阈值±回差 才恢复，避免在阈值附近反复报");

        g->addWidget(name,        i + 1, 0);
        g->addWidget(m_thLow[i],  i + 1, 1);
        g->addWidget(m_thHigh[i], i + 1, 2);
        g->addWidget(m_thHyst[i], i + 1, 3);

        connect(m_thLow[i], &QDoubleSpinBox::valueChanged,
                this, [this, i](double v) {
                    if (m_loading) return;
                    AlarmManager::Threshold t = m_alarm.threshold(Series(i));
                    t.low = v;
                    t.lowEnabled = true;            /* 改值即启用该侧 */
                    m_alarm.setThreshold(Series(i), t);
                    refreshThresholdPanel();
                });
        connect(m_thHigh[i], &QDoubleSpinBox::valueChanged,
                this, [this, i](double v) {
                    if (m_loading) return;
                    AlarmManager::Threshold t = m_alarm.threshold(Series(i));
                    t.high = v;
                    t.highEnabled = true;
                    m_alarm.setThreshold(Series(i), t);
                    refreshThresholdPanel();
                });
        connect(m_thHyst[i], &QDoubleSpinBox::valueChanged,
                this, [this, i](double v) {
                    if (m_loading) return;
                    AlarmManager::Threshold t = m_alarm.threshold(Series(i));
                    t.hyst = v > 0.0 ? v : 0.0;
                    m_alarm.setThreshold(Series(i), t);
                });
    }

    m_chkAlarmOn = new QCheckBox(QStringLiteral("启用告警判定（关掉则只显示数据）"));
    m_chkAlarmOn->setChecked(true);
    connect(m_chkAlarmOn, &QCheckBox::toggled, this, [this](bool on) {
        m_alarm.setEnabled(on);
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

    const bool wasLoading = m_loading;
    m_loading = true;
    for (int i = 0; i < int(Series::Count); ++i) {
        const AlarmManager::Threshold &t = m_alarm.threshold(Series(i));
        m_thLow[i]->setValue(t.low);
        m_thHigh[i]->setValue(t.high);
        m_thHyst[i]->setValue(t.hyst);

        /* 未启用的一侧：值保留，用灰字标出，表示该值不参与判定 */
        const QString off = QStringLiteral("color:#888;");
        m_thLow[i]->setStyleSheet(t.lowEnabled ? QString() : off);
        m_thHigh[i]->setStyleSheet(t.highEnabled ? QString() : off);
        m_thLow[i]->setSuffix(t.lowEnabled ? QString() : QStringLiteral(" ·未判"));
        m_thHigh[i]->setSuffix(t.highEnabled ? QString() : QStringLiteral(" ·未判"));
    }
    if (m_chkAlarmOn)
        m_chkAlarmOn->setChecked(m_alarm.enabled());
    m_loading = wasLoading;
}

void MainWindow::onThresholdDialog()
{
    /* 对话框先改暂存、点确定才写回，所以进出各回显一次就够 */
    refreshThresholdPanel();
    if (ThresholdDialog::edit(&m_alarm, this)) {
        refreshThresholdPanel();
        saveConfig();
        logLine(QStringLiteral("阈值设置已更新并写入配置文件。"));
    }
}

void MainWindow::onThresholdDefaults()
{
    AlarmManager def;
    for (int i = 0; i < int(Series::Count); ++i)
        m_alarm.setThreshold(Series(i), def.threshold(Series(i)));
    m_alarm.setEnabled(true);
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
    m_dev->setForward(on);
}

void MainWindow::onDevCacheToggled(bool on)
{
    if (m_loading) return;
    logLine(on ? QStringLiteral("请求：打开板2 缓存") : QStringLiteral("请求：关闭板2 缓存"));
    m_dev->setCache(on);
}

void MainWindow::onDevQuery()
{
    m_dev->query();
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
    m_dev->clearCache();
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
    m_dev->reboot();
}

/* 开始补传 / 停止补传：一个按钮两态。点了之后立刻切到"补传中"，
 * 板2 的应答（0x96）回来后再按实际状态覆盖，避免点了没反应的感觉。 */
void MainWindow::onDevReplayClicked()
{
    const bool wantStop = m_replayRunning;

    if (wantStop) {
        logLine(QStringLiteral("请求：停止补传历史数据"));
        m_dev->stopReplay();
    } else {
        logLine(QStringLiteral("请求：开始补传历史数据"));
        m_dev->startReplay();
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
        logLine(QStringLiteral("补传已完成：本次已补 %1 条，历史入库累计 %2 条（重复丢弃 %3 条）")
                    .arg(st.sent)
                    .arg(m_model.historyInserted())
                    .arg(m_model.historyDuplicate()));
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

    const ReplayStatus st = m_dev->lastReplay();
    QString stateText;
    if (!st.valid) {
        stateText = QStringLiteral("未知（还没收到板2 应答）");
    } else {
        stateText = AckFrame::replayStateName(st.state);
    }

    QString line = QStringLiteral("补传状态：%1\n已补传 %2 条 ｜ 剩余 %3 条")
                       .arg(stateText)
                       .arg(st.valid ? int(st.sent) : 0)
                       .arg(st.valid ? (st.restCapped
                                            ? QStringLiteral("255 以上（板2 未细分）")
                                            : QString::number(st.rest))
                                     : QStringLiteral("未知"));
    line += QStringLiteral("\n历史入库 %1 条（重复丢弃 %2，不可用 %3）")
                .arg(m_model.historyInserted())
                .arg(m_model.historyDuplicate())
                .arg(m_model.historyRejected());
    m_replayStatus->setText(line);
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
        m_dev->detach();                    /* 在途命令作废，应答不会再来 */
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

    m_dev->attach(m_serial);                /* 控制通道跟着串口一起上线 */

    m_parser.reset();                       /* 新会话：缓冲与统计清零，避免旧残包混进来 */
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

    const FrameParser::Stats before = m_parser.stats();
    m_parser.feed(chunk);

    EnvFrame f;
    while (m_parser.takeFrame(f))
        handleFrame(f, QDateTime::currentMSecsSinceEpoch());

    /* 补传回来的历史帧（CMD 0x15）走单独一条队列与单独一条处理路径：
     * 它只入库、回填曲线，不参与实时告警，也不刷新静默检测的收帧时刻。 */
    HistoryFrame h;
    while (m_parser.takeHistory(h))
        handleHistoryFrame(h);

    /* 控制应答帧（CMD 带 0x80）：交给 DevCtl 匹配在途命令 */
    AckFrame a;
    while (m_parser.takeAck(a))
        m_dev->onAck(a);

    const FrameParser::Stats &now = m_parser.stats();
    if (now.errCrc != before.errCrc)
        logLine(QStringLiteral("CRC 错：累计 %1（本次 %2 字节：%3）")
                    .arg(now.errCrc).arg(chunk.size())
                    .arg(QString::fromLatin1(chunk.toHex(' ').toUpper())));
    else if (now.errTail != before.errTail)
        logLine(QStringLiteral("帧尾错：累计 %1").arg(now.errTail));
    else if (now.errLen != before.errLen)
        logLine(QStringLiteral("长度字段非法：累计 %1").arg(now.errLen));
}

void MainWindow::onSerialError(QSerialPort::SerialPortError err)
{
    if (err == QSerialPort::NoError) return;

    logLine(QStringLiteral("串口错误：%1").arg(m_serial->errorString()));

    /* 拔线/驱动异常：ResourceError 之后端口句柄已不可用，必须关闭并放开界面 */
    if (err == QSerialPort::ResourceError || err == QSerialPort::DeviceNotFoundError) {
        if (m_serial->isOpen()) m_serial->close();
        m_dev->detach();
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
    m_model.addFrame(f, wallMs);
    m_lastFrameWallMs = wallMs;
    m_silenceWarned = false;

    /* 物理量量程校验：超出合理范围的值（板1 气压/光照/TVOC/MQ-135 尚未接入，
     * frame_build() 直接传 0）照常显示、照记 CSV，只是不参与告警判定：丢掉就分不清
     * 传感器没接与真实读数，不管则 0.0 hPa 会命中气压低于下限 980 而刷出假告警。
     * 日志只在进入/退出超范围时各记一条，避免每秒数条把日志冲爆。 */
    quint8 suspectMask = 0;
    const bool rangeCheck = (m_chkRange == nullptr) || m_chkRange->isChecked();
    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);
        const double v = DataModel::value(f, s);
        const bool bad = rangeCheck && PhysRange::suspect(s, v);
        if (bad) {
            suspectMask |= quint8(1u << i);
            ++m_rangeSuspect[i];
            ++m_rangeSuspectTotal;
        }
        if (bad != m_suspectState[i]) {
            m_suspectState[i] = bad;
            const QString val = std::isnan(v)
                                    ? QStringLiteral("--")
                                    : QString::number(v, 'f', DataModel::decimals(s))
                                          + QString::fromUtf8(DataModel::unit(s));
            logLine(QStringLiteral("%1 %2 = %3（合理范围 %4）：%5")
                        .arg(bad ? QStringLiteral("超范围") : QStringLiteral("已恢复"))
                        .arg(QString::fromUtf8(DataModel::name(s)))
                        .arg(val)
                        .arg(PhysRange::text(s))
                        .arg(bad ? QStringLiteral("已标黄，暂不参与告警判定（CSV 里记 +RANGE）")
                                 : QStringLiteral("已回到合理范围，恢复参与告警判定")));
        }
    }

    /* 告警判定：超范围的项已由 suspectMask 跳过，状态保持原样，不产生假告警或假恢复 */
    const QVector<AlarmManager::Record> fresh = m_alarm.evaluate(f, wallMs, suspectMask);
    for (const AlarmManager::Record &r : fresh) {
        appendAlarmRow(r);
        logLine((r.recovery ? QStringLiteral("恢复 ") : QStringLiteral("告警 ")) + r.text());
    }

    if (m_csv.isRecording()) {
        m_csv.append(f, QDateTime::fromMSecsSinceEpoch(wallMs), FrameProto::OK, suspectMask);
        ui->lblCsvInfo->setText(QStringLiteral("已记 %1 行\n%2")
                                    .arg(m_csv.rows()).arg(m_csv.path()));
    }
}

/* 补传回来的历史帧：只做三件事，入库、按需记 CSV、累计待显示条数。
 * 刻意不做的事：不调 AlarmManager::evaluate（两小时前的越限数据不该现在弹告警），
 * 不动 m_lastFrameWallMs 与 m_silenceWarned（历史帧不是实时链路的证据），
 * 不做量程校验计数（那是实时链路的质量指标）。 */
void MainWindow::handleHistoryFrame(const HistoryFrame &h)
{
    qint64 wallMs = 0;
    if (!m_model.addHistory(h, &wallMs)) return;   /* 重复或不可用：模型内部已计数 */

    ++m_histNewSinceTick;

    /* 时间列写原始采集时刻（由模型回填，与曲线横坐标同源），不写补传到达时刻，
     * 否则补传回来的历史数据在导出文件里会全部挤到导出前的几分钟。 */
    if (m_csv.isRecording()) {
        m_csv.append(h.env, QDateTime::fromMSecsSinceEpoch(wallMs), FrameProto::OK, 0,
                     CsvRecorder::SourceHistory);
        ui->lblCsvInfo->setText(QStringLiteral("已记 %1 行\n%2")
                                    .arg(m_csv.rows()).arg(m_csv.path()));
    }
}

/* ============================================================================
 *  刷新
 * ==========================================================================*/

void MainWindow::onPlotTick()
{
    refreshCards();
    refreshPlot();

    /* 下发通道的超时/重发挂在 50 ms 这个定时器上：DevCtl 里写的"600 ms 超时"
     * 才是真的 600 ms（挂在 1 s 的统计定时器上会变成 1 s 才判超时，参数与行为对不上）。 */
    if (m_dev != nullptr) m_dev->tick();
}

void MainWindow::onStatsTick()
{
    refreshStats();
    updateReplayLabel();

    /* 补传进度汇总：每秒一条，而不是每帧一条。2048 条缓存补回来时，
     * 逐帧记日志会把左侧日志与 hex 区全冲掉，反倒看不见链路异常。 */
    if (m_histNewSinceTick > 0) {
        logLine(QStringLiteral("补传入库 %1 条，累计 %2 条（重复丢弃 %3）")
                    .arg(m_histNewSinceTick)
                    .arg(m_model.historyInserted())
                    .arg(m_model.historyDuplicate()));
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
    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);

        if (!m_model.hasData()) {
            m_cardValue[i]->setText(QStringLiteral("--"));
            m_cardValue[i]->setStyleSheet(QStringLiteral("color:#999;border:none;"));
            continue;
        }

        const double v = DataModel::value(m_model.latest(), s);
        const AlarmLevel lv = m_alarm.state(s);

        if (std::isnan(v)) {
            m_cardValue[i]->setText(QStringLiteral("--"));      /* 无效值（如湿度读失败） */
        } else {
            m_cardValue[i]->setText(QString::number(v, 'f', DataModel::decimals(s)));
        }

        /* 三级配色：告警红 > 超范围琥珀 > 正常深灰。
         * 超范围用琥珀而不用红：它不是告警，只表示该值暂不可信。 */
        const bool suspect = ((m_chkRange == nullptr) || m_chkRange->isChecked())
                             && PhysRange::suspect(s, v);
        QString color = QStringLiteral("#222");
        if (suspect) color = QStringLiteral("#b8860b");
        if (lv != AlarmLevel::None) color = QStringLiteral("#c0392b");
        m_cardValue[i]->setStyleSheet(QStringLiteral("color:%1;border:none;").arg(color));
        m_cardValue[i]->setToolTip(suspect
            ? QStringLiteral("超出合理范围 %1，暂不参与告警判定").arg(PhysRange::text(s))
            : QString());
    }
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
    const int n = m_model.size();
    if (n <= 0) { p->replot(QCustomPlot::rpQueuedReplot); return; }

    const bool rebuild = (m_model.stamps().size() < int(m_shownFrames)) || (m_shownFrames == 0) ||
                         (m_model.history(Series::Temp).size() < int(m_shownFrames));

    for (int i = 0; i < int(Series::Count); ++i) {
        if (!m_graph[i]->visible()) continue;   /* QCPGraph 没有 isVisible()，是 QCPLayerable::visible() */
        const QVector<QPointF> &pts = m_model.history(Series(i));
        QVector<double> keys, vals;
        if (rebuild) {
            splitPoints(pts, keys, vals);
            m_graph[i]->setData(keys, vals, true);
        } else {
            const int add = n - int(m_shownFrames);
            if (add > 0) {
                const int from = qMax(0, pts.size() - add);
                splitPoints(pts.mid(from), keys, vals);
                if (!keys.isEmpty())
                    m_graph[i]->addData(keys, vals, true);
            }
        }
    }
    m_shownFrames = quint64(n);

    const double xLast = double(m_model.latestWallMs() - m_model.firstWallMs()) / 1000.0;
    double x0 = xLast - 120.0;                      /* 默认看最近 2 分钟 */
    if (x0 < 0.0) x0 = 0.0;
    p->xAxis->setRange(x0, xLast + 2.0);

    /* Y 轴默认固定量程，便于跨时间对比曲线形状；勾选自动量程后按当前可见曲线自适应。
     * 固定量程的副作用：气压轴是 950~1050 而板1 气压尚未接入、读数恒 0，那条线贴在
     * 轴外看不见，容易被误判成程序没收到数据。 */
    if (m_chkYAuto != nullptr && m_chkYAuto->isChecked()) {
        for (int i = 0; i < int(Series::Count); ++i) {
            if (!m_axis[i]->visible()) continue;
            const QVector<QPointF> &pts = m_model.history(Series(i));
            double lo = 0.0, hi = 0.0;
            bool any = false;
            for (const QPointF &pt : pts) {
                const double y = pt.y();
                if (!std::isfinite(y)) continue;        /* NaN/Inf（湿度无效）不参与定范围 */
                if (!any) { lo = hi = y; any = true; }
                else { lo = qMin(lo, y); hi = qMax(hi, y); }
            }
            if (!any) continue;                          /* 这条曲线没数据 → 维持原范围 */
            if (hi - lo < 1e-9) { lo -= 0.5; hi += 0.5; }/* 恒值：给点上下空间，别压成一条线 */
            const double pad = (hi - lo) * 0.10 + 1e-9;  /* 上下留 10% 余量 */
            m_axis[i]->setRange(lo - pad, hi + pad);
        }
    } else {
        for (int i = 0; i < int(Series::Count); ++i)
            m_axis[i]->setRange(kYMin[i], kYMax[i]);
    }

    p->replot(QCustomPlot::rpQueuedReplot);
}

void MainWindow::refreshStats()
{
    const FrameParser::Stats &st = m_parser.stats();

    QString gap = QStringLiteral("帧间隔：--");
    if (st.maxGapMs > 0) {
        gap = QStringLiteral("帧间隔：最小 %1 ms / 最大 %2 ms（期望 %3 ms）")
                  .arg(st.minGapMs).arg(st.maxGapMs).arg(st.expectedIntervalMs);
    }

    QString sil = QStringLiteral("静默：--");
    if (m_lastFrameWallMs > 0)
        sil = QStringLiteral("距上一帧 %1 ms").arg(
                  QDateTime::currentMSecsSinceEpoch() - m_lastFrameWallMs);

    /* 量程校验明细：只列"真有可疑帧"的项，没问题的项不占地方 */
    QString suspect = QStringLiteral("量程可疑 %1 次").arg(m_rangeSuspectTotal);
    if (m_rangeSuspectTotal > 0) {
        QStringList detail;
        for (int i = 0; i < int(Series::Count); ++i) {
            if (m_rangeSuspect[i] == 0) continue;
            detail << QStringLiteral("%1 %2").arg(QString::fromUtf8(DataModel::name(Series(i))))
                                             .arg(m_rangeSuspect[i]);
        }
        suspect += QStringLiteral("（%1；超范围不参与告警）").arg(detail.join(QStringLiteral("、")));
    }

    const QString text = QStringLiteral(
        "成帧 %1 ｜ 应答 %2 ｜ 喂入 %3 字节\n"
        "CRC 错 %4 ｜ 长度错 %5 ｜ 帧尾错 %6 ｜ 重同步丢 %7 字节\n"
        "%8\n"
        "估算丢帧 %9（无序号字段，按间隔估算，仅参考）\n"
        "告警中 %10 项（累计进入 %11 次）｜ CSV %12 行\n"
        "%13\n"
        "下发 ↑%14 / 应答 ↓%15 / 超时 %16 ｜ 缓冲里未成帧 %17 字节")
        .arg(st.framesOk).arg(st.framesAck).arg(st.bytesIn)
        .arg(st.errCrc).arg(st.errLen).arg(st.errTail).arg(st.resyncBytes)
        .arg(gap)
        .arg(st.lossEstimated)
        .arg(m_alarm.activeCount()).arg(m_alarm.enterCount())
        .arg(m_csv.rows())
        .arg(suspect)
        .arg(m_dev != nullptr ? m_dev->txCount() : 0)
        .arg(m_dev != nullptr ? m_dev->rxCount() : 0)
        .arg(m_dev != nullptr ? m_dev->timeoutCount() : 0)
        .arg(m_parser.buffered());

    /* 再带上静默时长（单独一行，便于扫一眼） */
    ui->lblStats->setText(text + QStringLiteral("\n") + sil);
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

    /* 已经通过"开始记录 CSV…"菜单指定了文件：只同步勾选框，不要另开一个文件 */
    if (m_csv.isRecording()) {
        ui->lblCsvInfo->setText(QStringLiteral("记录中（%1 行）\n%2")
                                    .arg(m_csv.rows()).arg(m_csv.path()));
        return;
    }

    QDir().mkpath(QDir::current().filePath(QStringLiteral("data")));
    const QString path = QDir::current().filePath(
        QStringLiteral("data/环境数据_%1.csv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));

    QString err;
    if (!m_csv.start(path, &err)) {
        QMessageBox::warning(this, QStringLiteral("记录 CSV"),
                             QStringLiteral("无法写入 %1：%2").arg(path, err));
        ui->chkCsv->setChecked(false);
        return;
    }
    ui->lblCsvInfo->setText(QStringLiteral("记录中（0 行）\n%1").arg(path));
    logLine(QStringLiteral("开始记录 CSV：%1").arg(path));
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

    QString err;
    if (!m_csv.start(path, &err)) {
        QMessageBox::warning(this, QStringLiteral("记录 CSV"),
                             QStringLiteral("无法写入：%1").arg(err));
        return;
    }
    if (!ui->chkCsv->isChecked()) ui->chkCsv->setChecked(true);   /* 同步勾选框 */
    ui->lblCsvInfo->setText(QStringLiteral("记录中（0 行）\n%1").arg(path));
}

void MainWindow::onStopCsv()
{
    if (m_csv.isRecording()) {
        logLine(QStringLiteral("停止记录 CSV（共 %1 行）：%2").arg(m_csv.rows()).arg(m_csv.path()));
        m_csv.stop();
    }
    if (ui->chkCsv->isChecked()) ui->chkCsv->setChecked(false);
    ui->lblCsvInfo->setText(QStringLiteral("未记录"));
}

void MainWindow::onExportCsv()
{
    if (!m_model.hasData()) {
        QMessageBox::information(this, QStringLiteral("导出 CSV"), QStringLiteral("还没有数据。"));
        return;
    }
    const QString def = QDir::current().filePath(
        QStringLiteral("data/导出_%1.csv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出当前数据"),
                                                      def, QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;

    QString err;
    if (!m_model.saveHistoryCsv(path, &err))
        QMessageBox::warning(this, QStringLiteral("导出 CSV"), QStringLiteral("写入失败：%1").arg(err));
    else
        logLine(QStringLiteral("已导出 %1 帧到 %2").arg(m_model.size()).arg(path));
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
    m_model.clear();
    m_parser.reset();
    m_alarm.reset();
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
    ui->tblAlarm->setRowCount(0);
    ui->plotEnv->replot();
    refreshCards();
    refreshStats();
    updateReplayLabel();
    logLine(QStringLiteral("已清空数据、统计与告警记录"));
}

void MainWindow::onClearAlarm()
{
    m_alarm.clearRecords();
    ui->tblAlarm->setRowCount(0);
    logLine(QStringLiteral("已清空告警记录表"));
}

void MainWindow::onFeedDemo()
{
    /* 无硬件也能看界面/曲线/告警：按正弦 + 抖动造一帧（数值是假的，只为自检界面） */
    static int k = 0;
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

    handleFrame(f, QDateTime::currentMSecsSinceEpoch());
    logLine(QStringLiteral("演示帧 #%1（数值为模拟，仅供界面自检）").arg(k));
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
        AlarmManager::Threshold t = m_alarm.threshold(s);
        ini.beginGroup(QStringLiteral("thresholds/") + QLatin1String(DataModel::key(s)));
        t.low         = ini.value(QStringLiteral("low"),          t.low).toDouble();
        t.lowEnabled  = ini.value(QStringLiteral("lowEnabled"),   t.lowEnabled).toBool();
        t.high        = ini.value(QStringLiteral("high"),         t.high).toDouble();
        t.highEnabled = ini.value(QStringLiteral("highEnabled"),  t.highEnabled).toBool();
        t.hyst        = ini.value(QStringLiteral("hyst"),         t.hyst).toDouble();
        ini.endGroup();
        m_alarm.setThreshold(s, t);

        /* 曲线勾选 */
        if (m_curveChk[i] != nullptr) {
            ini.beginGroup(QStringLiteral("curves/") + QLatin1String(DataModel::key(s)));
            const bool on = ini.value(QStringLiteral("visible"), m_curveChk[i]->isChecked()).toBool();
            ini.endGroup();
            m_curveChk[i]->setChecked(on);      /* 槽里会同步 graph/axis 可见性 + refreshPlot */
        }
    }

    ini.beginGroup(QStringLiteral("alarm"));
    m_alarm.setEnabled(ini.value(QStringLiteral("enabled"), m_alarm.enabled()).toBool());
    ini.endGroup();

    ini.beginGroup(QStringLiteral("ui"));
    if (m_chkYAuto) m_chkYAuto->setChecked(ini.value(QStringLiteral("yAuto"), m_chkYAuto->isChecked()).toBool());
    if (m_chkRange) m_chkRange->setChecked(ini.value(QStringLiteral("rangeCheck"), m_chkRange->isChecked()).toBool());
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
        const AlarmManager::Threshold &t = m_alarm.threshold(s);
        ini.beginGroup(QStringLiteral("thresholds/") + QLatin1String(DataModel::key(s)));
        ini.setValue(QStringLiteral("low"),         t.low);
        ini.setValue(QStringLiteral("lowEnabled"),  t.lowEnabled);
        ini.setValue(QStringLiteral("high"),        t.high);
        ini.setValue(QStringLiteral("highEnabled"), t.highEnabled);
        ini.setValue(QStringLiteral("hyst"),        t.hyst);
        ini.endGroup();

        if (m_curveChk[i] != nullptr) {
            ini.beginGroup(QStringLiteral("curves/") + QLatin1String(DataModel::key(s)));
            ini.setValue(QStringLiteral("visible"), m_curveChk[i]->isChecked());
            ini.endGroup();
        }
    }

    ini.beginGroup(QStringLiteral("alarm"));
    ini.setValue(QStringLiteral("enabled"), m_alarm.enabled());
    ini.endGroup();

    ini.beginGroup(QStringLiteral("ui"));
    ini.setValue(QStringLiteral("yAuto"),      m_chkYAuto ? m_chkYAuto->isChecked() : false);
    ini.setValue(QStringLiteral("rangeCheck"), m_chkRange ? m_chkRange->isChecked() : true);
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
    if (m_csv.isRecording())
        m_csv.stop();
    QMainWindow::closeEvent(e);
}
