#ifndef DATAMODEL_H
#define DATAMODEL_H

/* ============================================================================
 *  datamodel.h —— 数据模型：最新值 + 历史环形缓冲 + 运行统计 + CSV 记录
 *  不继承 QObject：界面用 50 ms QTimer 主动取值，便于高频数据与批量重绘。
 *  6 条曲线共用时间轴：每帧给每条曲线各 append 一个点（湿度无效时 append NaN，
 *  QCustomPlot 自行断线），下标严格对齐，导出 CSV 时按同一下标取 6 个值。
 *  环形容量默认 1800 点 = 1 s 周期下 30 分钟，满一圈丢最老的点。
 * ==========================================================================*/

#include "frameparser.h"

#include <QDateTime>
#include <QFile>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QtGlobal>

/* 6 个物理量：顺序 = 界面卡片顺序 = CSV 列顺序，不要随意调换 */
enum class Series {
    Temp = 0,
    Humi,
    Press,
    Light,
    Tvoc,
    Mq135,
    Count
};

class DataModel
{
public:
    struct Stat {
        quint64 n = 0;
        double  min = 0.0;
        double  max = 0.0;
        double  sum = 0.0;
        double  avg() const { return n > 0 ? sum / double(n) : 0.0; }
        bool    valid() const { return n > 0; }
    };

    explicit DataModel(int capacity = 1800);

    void clear();
    void setCapacity(int cap);

    /* 收一帧（wallMs = 本机收到的毫秒时刻，用于时间轴与 CSV） */
    void addFrame(const EnvFrame &f, qint64 wallMs);

    /* 收一帧历史数据（板2 补传，CMD=0x15）。与 addFrame 的区别：
     *  1) 时间轴用帧里的原始时间戳（换算成毫秒），曲线上的位置是数据真实发生的
     *     时刻，而不是补传到达的时刻；
     *  2) 只更新曲线与统计，不产生任何告警相关动作，历史越限数据也不会改写实时告警
     *     状态（告警判定在上层，本层也不返回任何"该怎么处理"的信号）；
     *  3) 去重：以「原始时间戳 + 20 字节原始帧内容」为键，重复到达直接丢弃并计数。
     * 返回 true = 本次真的入库，false = 重复帧被丢弃（计数加一）。
     * tsRaw 为 0 或原始帧解析失败的数据不入库，返回 false 且计入拒绝计数。
     * wallMsOut 非空时回填本次实际使用的时间轴毫秒值，供上层写 CSV 时间列，
     * 保证导出文件里的时刻与曲线横坐标指向同一时刻。 */
    bool addHistory(const HistoryFrame &h, qint64 *wallMsOut = nullptr);

    /* 历史入库计数（供界面显示与自检查询） */
    quint64 historyInserted() const    { return m_histIn; }
    quint64 historyDuplicate() const   { return m_histDup; }
    quint64 historyRejected() const    { return m_histBad; }
    int     historyPendingDupKeys() const { return m_histKeys.size(); }

    /* 原始时间戳（hh*10000+mm*100+ss）换算成本机当天时刻的毫秒值。
     * ts 字段没有年月日，换算时按"当天"解释；若结果比 afterMs 早 12 小时以上，
     * 说明这批补传数据是前一晚跨零点采集的，按次日解释（afterMs 传已有的最后时刻，
     * 首次调用传 0）。ts 非法时返回 0，由调用方决定退回到达时刻还是丢弃。 */
    static qint64 resolveStampMs(quint32 tsRaw, qint64 afterMs);

    /* 最近一次入库的原始时间戳（0 = 还没有历史数据入库） */
    quint32 lastHistoryTs() const { return m_histLastTs; }

    /* 最新一帧 */
    const EnvFrame &latest() const { return m_latest; }
    bool    hasData() const     { return m_frames > 0; }
    quint64 framesTotal() const { return m_frames; }
    qint64  latestWallMs() const { return m_stampMs.isEmpty() ? 0 : m_stampMs.last(); }
    qint64  firstWallMs() const  { return m_t0WallMs; }
    int     capacity() const     { return m_capacity; }
    int     size() const         { return m_stampMs.size(); }

    /* 曲线数据：x = 相对第一帧的秒数，y = 该物理量 */
    const QVector<QPointF> &history(Series s) const { return m_hist[int(s)]; }
    const QVector<qint64>  &stamps() const           { return m_stampMs; }

    const Stat &stat(Series s) const { return m_stat[int(s)]; }

    static const char *name(Series s);
    static const char *unit(Series s);
    static int  decimals(Series s);                     /* 显示/CSV 该用几位小数 */
    static const char *key(Series s);                   /* 配置文件里的键名（ASCII） */
    static double value(const EnvFrame &f, Series s);   /* 无效湿度返回 NaN */

    /* 把当前历史缓冲导出成 CSV（UTF-8 BOM；与实时记录格式一致） */
    bool saveHistoryCsv(const QString &path, QString *err) const;

private:
    void push(Series s, double y, double x);

    /* 把一帧解析结果按给定墙钟时刻排进时间轴。
     * histFlag = 这条数据来自补传（CSV 的来源列据此写"补传"）。
     * 时刻落在已有数据之后就直接追加，落在中间或之前用二分找位置插入；
     * 插入要按时间顺序对齐 6 条曲线与时间戳数组，因此只有补传走插入分支。 */
    void insertOrdered(const EnvFrame &f, qint64 wallMs, bool histFlag);

    /* 曲线横向坐标：相对时间轴原点的秒数。返回值可能为负（补传的历史数据
     * 早于当前原点），上层按原值画，不做截断。 */
    double xOf(qint64 wallMs) const;

    /* 重算某条曲线的统计量（插入历史点后序号会平移，增量更新容易算错，
     * 补传不是高频路径，直接整条重算是可以的） */
    void recountStat(int seriesIdx);

    /* 时间轴裁剪后同步键表：把 wallMs 早于时间轴首点的历史键丢掉，
     * 保证键表与时间轴逐项对齐（键表是时间轴的子集，只多不少） */
    void trimKeyFront();

    int     m_capacity;
    qint64  m_t0WallMs = 0;
    quint64 m_frames = 0;
    EnvFrame m_latest;

    QVector<QPointF> m_hist[int(Series::Count)];
    QVector<qint64>  m_stampMs;

    /* 与 m_stampMs 逐项对齐的来源标记：true = 该点是补传的历史数据 */
    QVector<bool>    m_histFlag;

    /* 去重键表：与时间轴逐项对齐。只记通过 addHistory 入库的点，
     * 实时帧不占这里的位置，因此键表比时间轴短，用它自己的队首做对齐。
     * 键 = 原始时间戳（quint32）+ 20 字节原始帧内容（逐字节比较）。 */
    struct HistKey {
        qint64     wallMs = 0;   /* 栈内时间轴坐标（由 tsRaw 换算，含跨天展开） */
        QByteArray raw;          /* 20 字节原始环境帧内容 */
    };
    QVector<HistKey> m_histKeys;
    quint64 m_histIn = 0;        /* 历史帧成功入库次数 */
    quint64 m_histDup = 0;       /* 判定为重复而丢弃的次数 */
    quint64 m_histBad = 0;       /* 时间戳为 0 或原始帧解不出而不入库的次数 */
    quint32 m_histLastTs = 0;    /* 最近一次入库的原始时间戳 */

    Stat             m_stat[int(Series::Count)];
};

/* ---------------------------------------------------------------------------
 *  CsvRecorder —— 实时记录收到的每一帧
 *  表头与字段顺序见《Qt上位机设计.md》§6 定稿；改动会与文档及演示脚本对不上。
 *  来源列（"数据来源"）放在最后一列：既有的 9 列位置与含义都不动，
 *  按列序号解析的旧脚本读前 9 列仍然正确，只是多出一列会被读到或被忽略。
 * -------------------------------------------------------------------------*/
class CsvRecorder
{
public:
    /* 数据来源：实时帧 / 补传的历史帧。写进 CSV 最后一列 */
    enum Source { SourceLive = 0, SourceHistory = 1 };

    ~CsvRecorder();

    bool start(const QString &path, QString *err);
    void stop();
    bool isRecording() const { return m_file.isOpen(); }
    QString path() const     { return m_file.fileName(); }
    quint64 rows() const     { return m_rows; }

    /* crcState：本帧校验结果（正常运行时恒为 OK）
     * suspectMask：超出物理合理范围的项（bit i = Series i，见 physrange.h）。
     * 只在 CSV 里加 +RANGE 标记，数据照记，便于事后区分传感器没接与真实读数。
     * source：实时帧还是补传的历史帧，默认实时（既有调用点不需要改）。 */
    void append(const EnvFrame &f, const QDateTime &when, quint8 crcState,
                quint8 suspectMask = 0, Source source = SourceLive);

    /* 来源列的中文名（saveHistoryCsv 与实时记录共用，避免两处各写一份） */
    static QString sourceText(Source s);

    static QString header();

private:
    QFile   m_file;
    quint64 m_rows = 0;
};

#endif  /* DATAMODEL_H */
