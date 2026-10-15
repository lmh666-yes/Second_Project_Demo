#ifndef MAINWINDOW_H
#define MAINWINDOW_H

/* ============================================================================
 *  mainwindow.h —— 上位机主界面
 *  数据链：板1（传感器）→ LoRa → 板2（网关，UART2 转发）→ CH340 → 本机串口
 *  本界面三件事：解析 20 字节二进制帧（FrameParser）；显示最新值 + 6 条曲线 +
 *  运行统计（DataModel + QCustomPlot）；判告警、记 CSV、跑一致性自检。
 *  刷新策略：readyRead 立即喂 FrameParser（只解析，不碰界面）；50 ms 定时器批量
 *  刷曲线与卡片；1000 ms 定时器刷统计/告警表/状态栏。不要改成来一帧刷一次。
 * ==========================================================================*/

#include <QMainWindow>
#include <QLabel>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QString>
#include <QTimer>

#include "alarmmanager.h"
#include "datamodel.h"
#include "devctl.h"
#include "frameparser.h"
#include "qcustomplot.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QCheckBox;
class QCloseEvent;
class QDoubleSpinBox;
class QGroupBox;
class QMouseEvent;
class QPlainTextEdit;
class QPushButton;
class QShowEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /* 造 count 个演示帧灌进数据链（间隔 1 s，时间轴与统计都能看出趋势）。
     * 菜单"灌入一帧演示数据"只灌 1 个；--shot 出图时灌一批，让时间轴刻度与
     * 最近 20 个点标记都有内容。 */
    void feedDemoFrames(int count);

    /* 行程自检用：直接按"倒数第 fromEnd 个点"选中 X 轴上的可点击数据点。
     * 与鼠标单击走同一条落地路径（applySelectedIndex），用于截图验证选中显示。 */
    bool selectStatPointFromEnd(int fromEnd = 0);

    /* 行程自检用：按数据序号（1 起）选中该组数据，等价于用鼠标点 X 轴条上
     * 序号 n 的圆点。用于验证"第一帧也完整可见、可点中"。 */
    bool selectStatSampleByNumber(int sampleNo);

    /* 诊断输出用：主图与 X 轴条的轴矩形左右像素、序号 1/序号 N 在两处的像素 x。
     * 用于核对"点条与主图逐像素对齐"。 */
    void dumpStripAlign(QStringList &out) const;

    /* 行程自检用：直接指定当前查看量（0..5 对应 Series 顺序），
     * 等价于用户在右侧"查看数据"下拉框里选一项，用于逐量出图核对左轴与曲线。 */
    bool setViewSeries(int index);
protected:
    /* 三栏宽度必须在第一次真正显示之后分配：构造里调 QSplitter::setSizes() 时
     * splitter 还是默认尺寸，传入的 330/660/350 会被按当时窄宽度缩掉，显示出
     * 真实尺寸后右栏（阈值设置/告警记录）可能被挤成 0 宽，表现为界面少一块。 */
    void showEvent(QShowEvent *e) override;

    /* 窗口尺寸变化后重新把点条的左右边距对齐到主图轴矩形 */
    void resizeEvent(QResizeEvent *e) override;

    /* 关窗前把阈值/勾选状态写进 上位机配置.ini，并停掉 CSV，避免留下半个文件 */
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onRefreshPorts();
    void onOpenPortClicked();
    void onSerialReadyRead();
    void onSerialError(QSerialPort::SerialPortError err);
    void onPlotTick();          /* 50 ms：批量刷曲线/卡片 */
    void onStatsTick();         /* 1 s：刷统计/告警/静默检测 */
    void onCsvToggled(bool on);
    void onStartCsvDialog();
    void onStopCsv();
    void onExportCsv();
    void onSelfTestClicked();
    void onClearData();
    void onClearAlarm();
    void onFeedDemo();
    void onProtocolDialog();
    void onAboutDialog();
    void onThresholdDialog();       /* 打开告警阈值设置对话框（含启用/回差） */
    void onThresholdDefaults();     /* 主界面上的恢复默认阈值 */

    /* 曲线区交互：单击最近 20 个时间点上的标记点 → 记录选中点；
     * 统计参数下拉框改变 → 换统计与标记的目标物理量 */
    void onPlotPointSelected(QCPAbstractPlottable *plottable, int dataIndex, QMouseEvent *event);
    void onXStripPressed(QMouseEvent *event);
    void onXStripMoved(QMouseEvent *event);
    void onStatSeriesChanged(int index);
    void onSelClear();

    /* 设备控制（左栏 3 · 设备控制）：三个按钮 + 两个开关 + 补传开关 */
    void onDevQuery();
    void onDevClearCache();
    void onDevReboot();
    void onDevFwdToggled(bool on);
    void onDevCacheToggled(bool on);
    void onDevStatusChanged(const DevStatus &st);
    void onDevLog(const QString &line);
    void onDevFailed(quint8 reqCmd, const QString &reason);
    void onDevReplayClicked();                  /* 开始/停止补传（0x16 下发） */
    void onDevReplayChanged(const ReplayStatus &st);

private:
    /* 搭建（在构造里按顺序调用） */
    void setupSerialUi();
    void setupPlot();
    void buildCards();
    void buildCurveChecks();
    void buildStatPanel();          /* 右侧统计区：参数下拉框 + 数值标签 */
    void buildThresholdPanel();
    void buildControlPanel();
    void buildHexLog();
    void connectAll();

    /* 刷新 */
    void refreshCards();
    void refreshPlot();
    void refreshStats();
    void refreshThresholdPanel();   /* 把 AlarmManager 里的阈值回显到主界面 4 列表格 */
    void appendAlarmRow(const AlarmManager::Record &r);
    void logLine(const QString &s);

    /* X 轴条上的可点击数据点：画在与主图 X 范围同步的窄条 m_xStrip 上，
     * 不压曲线区、不挡拖动与缩放。 */
    void rebuildXStrip();
    /* 点条上真正画出来的数据序号（1 起，升序）：
     *  从最新的样本往前遍历，整个绘制集合统一按"与已绘制点的像素间距
     *  >= kStripMinGapPx"抽稀；最近 kMarkerPoints 个样本只是先被考虑、优先保留，
     *  不是无条件必画。总数不超过 kStripMaxDots。
     *  这样点在像素上不会连成一片，密集数据下也点得中。 */
    QVector<double> stripDrawNumbers() const;
    /* 点条上可命中的数据序号（1 起，升序）：当前可视范围内所有有有效数据的样本，
     *  不做抽稀。命中判定与 tooltip 都用这一组，因此绘制点之间也能点中最近的样本。 */
    QVector<double> stripHitNumbers() const;
    /* 把 X 轴条的左右边距对齐到主图轴矩形（主图左侧有 Y 轴标签与刻度，
     * 点条没有；不对齐时同一个序号在两处会横向错开）。 */
    void syncXStripMargins();
    /* 点条上的像素 x → 最近的一个可点击序号（1 起）；超出命中半径返回 0。
     * 命中判定用整条高度 + 水平 ±kStripHitPx，因此整条点条都可点。 */
    double stripXToSampleNumber(int px) const;
    /* 左侧唯一 Y 轴与底部颜色说明条一起跟随"当前查看量" */
    void updateViewAxis();
    /* 选中数据点列表里的第 dataIndex 个点：鼠标单击与行内自检都走这里 */
    void applySelectedIndex(int dataIndex);
    /* 选中某个数据序号（1 起）：序号即横坐标，选中状态只有这一条落地路径 */
    void applySelectedSample(double sampleNo);
    /* 把某条曲线的窗口统计刷进右侧统计区（平均值/最大值/最小值/窗口点数） */
    void refreshStatPanel();
    /* 查看量没勾选显示时，在"选中数据点"区域给出提示 */
    void updateStatsWarn();
    /* 选中点的读数文本：数据序号 + 记录时刻 + 6 个通道当前值 */
    QString selectedPointText() const;
    /* 数据序号 → 该帧记录时刻文本（序号从 1 起，越界返回占位文本） */
    QString sampleTimeText(double sampleNo) const;
    /* 最近一次采样的墙钟毫秒 → HH:mm:ss 文本 */
    static QString clockText(qint64 wallMs);

    /* 配置落盘：上位机配置.ini（阈值/告警开关/Y轴自动/曲线勾选/串口参数） */
    QString configPath() const;
    void    loadConfig();
    void    saveConfig();

    /* 取一帧后的统一处理（喂模型 + 判告警 + 记 CSV + 日志） */
    void handleFrame(const EnvFrame &f, qint64 wallMs);

    /* 取一帧历史数据后的处理（只入模型的历史通道 + 记 CSV + 必要的日志）。
     * 与 handleFrame 分开：这里不做告警判定、不刷静默检测的"最近收帧时刻"，
     * 历史帧不是实时数据，不能让它影响实时链路的判断。 */
    void handleHistoryFrame(const HistoryFrame &h);

    /* 把补传状态刷到界面（按钮文字 + 只读状态行），界面文案只在这一处生成 */
    void updateReplayLabel();

    Ui::MainWindow *ui = nullptr;

    QSerialPort  *m_serial = nullptr;
    FrameParser   m_parser;
    DataModel     m_model;
    AlarmManager  m_alarm;
    CsvRecorder   m_csv;
    QTimer       *m_plotTimer = nullptr;
    QTimer       *m_statsTimer = nullptr;

    /* 6 张数值卡片：值 label 数组（下标 = Series）+ 名字 label */
    QLabel *m_cardName[int(Series::Count)]  = { nullptr };
    QLabel *m_cardValue[int(Series::Count)] = { nullptr };

    /* 6 条曲线：勾选框 + 图元。所有曲线共用最左边的一根 Y 轴（m_viewAxis），
     * 同一时刻只显示"当前查看量"那一条，因此不再需要每量一根右侧 Y 轴。 */
    QCheckBox *m_curveChk[int(Series::Count)] = { nullptr };
    QCPGraph  *m_graph[int(Series::Count)]    = { nullptr };

    /* ---- 曲线区的数据序号轴与 X 轴可点击点 ----
     *  横坐标 = 数据序号（第 1 帧是 1、第 2 帧是 2），由 DataModel::renumber()
     *  写进 QPointF 的 x；真实时刻只在选中点读数、CSV 与告警记录里出现。
     *  m_selX 记的就是序号本身。 */
    static const int kMarkerPoints = 20;    /* 遍历绘制集合时优先保留的最近样本个数 */
    static const int kStripMaxDots = 60;    /* 点条上最多画多少个点：太多就连成一片 */
    static const int kStripMinGapPx = 8;    /* 绘制集合内任意相邻点的最小像素间距 */
    static const int kStripHeight = 32;     /* X 轴条高度（px）：够高才好点中 */
    static const int kStripHitPx = 12;      /* 命中半径（px）：离点 ±12 px 内都算点中它 */
    QCPAxis   *m_viewAxis = nullptr;               /* 最左边的唯一一根 Y 轴 */
    QCPGraph  *m_xMarkerGraph = nullptr;           /* X 轴条上那 20 个可点击圆点 */
    QCPGraph  *m_selDotGraph = nullptr;            /* X 轴条上被选中的那一个点 */
    QCustomPlot *m_xStrip = nullptr;               /* 与主图 X 范围同步的 X 轴窄条 */
    QCPItemStraightLine *m_selLine = nullptr;
    QLabel *m_lblLegend = nullptr;              /* 曲线区下方的颜色说明条（可点击切换查看量） */
    int    m_statSeries = 0;                /* 当前查看量/统计目标（= Series 下标） */
    bool   m_hasSel = false;
    double m_selX = 0.0;                    /* 选中点横坐标 = 数据序号 */
    int    m_selSeries = 0;
    bool   m_syncingStripRange = false;     /* 防止主图与窄条的 X 范围互相触发 */
    int    m_stripHoverNo = 0;              /* 鼠标悬停命中的序号（0 = 没命中） */

    /* 右侧统计区的控件（在 mainwindow.ui 里，这里只存指针便于刷新） */
    QLabel *m_lblStatAvg = nullptr;
    QLabel *m_lblStatMax = nullptr;
    QLabel *m_lblStatMin = nullptr;
    QLabel *m_lblStatCount = nullptr;
    QLabel *m_lblStatRange = nullptr;
    QLabel *m_lblSelPoint = nullptr;

    /* 阈值面板 */
    QDoubleSpinBox *m_thLow[int(Series::Count)]  = { nullptr };
    QDoubleSpinBox *m_thHigh[int(Series::Count)] = { nullptr };
    QDoubleSpinBox *m_thHyst[int(Series::Count)] = { nullptr };

    /* 三个开关（在曲线勾选框那一行里）：
     *   m_chkAlarmOn —— "启用告警判定"总开关
     *   m_chkYAuto   —— 曲线 Y 轴自动量程
     *   m_chkRange   —— 物理量量程校验（超范围只标黄 + 计数，不参与告警） */
    QCheckBox *m_chkAlarmOn = nullptr;
    QCheckBox *m_chkYAuto   = nullptr;
    QCheckBox *m_chkRange   = nullptr;

    /* 量程校验统计：各项可疑帧数 + 总计；m_suspectState 用于"进入/退出超范围"只报一次 */
    quint64 m_rangeSuspect[int(Series::Count)] = { 0 };
    quint64 m_rangeSuspectTotal = 0;
    bool    m_suspectState[int(Series::Count)] = { false };

    /* loadConfig() 回填控件期间置 true：此时控件信号只更新内存、不触发 saveConfig */
    bool    m_loading = false;

    /* ---- 设备控制（下行通道：板2 收、板2 做、板2 回）----
     *  数据往上走是监测，命令往下走才是控制。没有这一块，板2 的转发/缓存/复位
     *  只能靠重烧固件改常量。 */
    DevCtl     *m_dev = nullptr;
    QCheckBox  *m_chkFwd     = nullptr;   /* 板2 转发到本机（0x10） */
    QCheckBox  *m_chkCache   = nullptr;   /* 板2 落 W25Q 缓存（0x11） */
    QLabel     *m_devStatus  = nullptr;   /* 板2 状态回显（查询应答解析结果） */
    QPushButton *m_btnQuery  = nullptr;
    QPushButton *m_btnClrCache = nullptr;
    QPushButton *m_btnReboot = nullptr;
    bool        m_devStatusKnown = false;

    /* ---- 历史帧补传（左栏 3 · 设备控制里的补传一块）----
     *  按钮只负责发 0x16；进度全靠板2 回的 0x96 应答，上位机不自己估算。 */
    QPushButton *m_btnReplay   = nullptr;   /* 开始补传 / 停止补传 */
    QLabel      *m_replayStatus = nullptr;  /* 只读状态显示 */
    bool         m_replayRunning = false;   /* 已请求开始、还没收到"已完成/出错/空闲" */
    qint64       m_replayAckMs = 0;         /* 最近一次收到补传应答的时刻（进度静默检测） */
    bool         m_replayWarned = false;

    /* 本轮统计周期内新入库的历史帧数（补传期间每秒汇总记一条日志，
     * 而不是每帧一条：2048 条缓存补回来时日志会被刷爆） */
    quint64 m_histNewSinceTick = 0;

    QPlainTextEdit *m_hexLog = nullptr;

    qint64  m_openWallMs = 0;           /* 串口打开时刻（判断"开了多久还没收到帧"） */
    qint64  m_lastFrameWallMs = 0;      /* 最近一帧到达时刻（静默检测用） */
    quint64 m_shownFrames = 0;          /* 已画进曲线的帧数（增量 append） */
    bool    m_silenceWarned = false;
    bool    m_splitterSized = false;    /* 三栏宽度只分一次（showEvent 里） */
};

#endif  /* MAINWINDOW_H */
