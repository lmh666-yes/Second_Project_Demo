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
class QPlainTextEdit;
class QPushButton;
class QShowEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    /* 三栏宽度必须在第一次真正显示之后分配：构造里调 QSplitter::setSizes() 时
     * splitter 还是默认尺寸，传入的 330/660/350 会被按当时窄宽度缩掉，显示出
     * 真实尺寸后右栏（阈值设置/告警记录）可能被挤成 0 宽，表现为界面少一块。 */
    void showEvent(QShowEvent *e) override;

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

    /* 6 条曲线：勾选框 + 图元 + 各自的 Y 轴（量纲不同，必须各用一根轴） */
    QCheckBox *m_curveChk[int(Series::Count)] = { nullptr };
    QCPGraph  *m_graph[int(Series::Count)]    = { nullptr };
    QCPAxis   *m_axis[int(Series::Count)]     = { nullptr };

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
