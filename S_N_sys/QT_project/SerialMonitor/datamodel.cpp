#include "datamodel.h"

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

DataModel::DataModel(int capacity)
    : m_capacity(capacity > 0 ? capacity : 1)
{
    setCapacity(m_capacity);
}

void DataModel::setCapacity(int cap)
{
    m_capacity = cap > 0 ? cap : 1;
    for (int i = 0; i < int(Series::Count); ++i) m_hist[i].reserve(m_capacity);
    m_stampMs.reserve(m_capacity);
    m_histFlag.reserve(m_capacity);
    m_histKeys.reserve(m_capacity);
}

void DataModel::clear()
{
    for (int i = 0; i < int(Series::Count); ++i) {
        m_hist[i].clear();
        m_stat[i] = Stat();
    }
    m_stampMs.clear();
    m_histFlag.clear();
    m_histKeys.clear();
    m_histIn = 0;
    m_histDup = 0;
    m_histBad = 0;
    m_histLastTs = 0;
    m_t0WallMs = 0;
    m_frames = 0;
    m_latest = EnvFrame();
}

const char *DataModel::name(Series s)
{
    switch (s) {
    case Series::Temp:  return "温度";
    case Series::Humi:  return "湿度";
    case Series::Press: return "气压";
    case Series::Light: return "光照";
    case Series::Tvoc:  return "TVOC";
    case Series::Mq135: return "MQ-135";
    default:            return "?";
    }
}

const char *DataModel::unit(Series s)
{
    switch (s) {
    case Series::Temp:  return "℃";
    case Series::Humi:  return "%RH";
    case Series::Press: return "hPa";
    case Series::Light: return "lx";
    case Series::Tvoc:  return "ppb";
    case Series::Mq135: return "ADC";
    default:            return "";
    }
}

int DataModel::decimals(Series s)
{
    switch (s) {
    case Series::Temp:
    case Series::Humi:  return 2;
    case Series::Press: return 1;
    default:            return 0;        /* 光照/TVOC/MQ-135 是原始计数 */
    }
}

const char *DataModel::key(Series s)
{
    switch (s) {
    case Series::Temp:  return "Temp";
    case Series::Humi:  return "Humi";
    case Series::Press: return "Press";
    case Series::Light: return "Light";
    case Series::Tvoc:  return "Tvoc";
    case Series::Mq135: return "Mq135";
    default:            return "Unknown";
    }
}

double DataModel::value(const EnvFrame &f, Series s)
{
    switch (s) {
    case Series::Temp:  return f.tempC();
    case Series::Humi:  return f.humiValid() ? f.humiRh()
                                             : std::numeric_limits<double>::quiet_NaN();
    case Series::Press: return f.pressHpa();
    case Series::Light: return double(f.lightLx);
    case Series::Tvoc:  return double(f.tvocPpb);
    case Series::Mq135: return double(f.mq135Raw);
    default:            return std::numeric_limits<double>::quiet_NaN();
    }
}

void DataModel::push(Series s, double y, double x)
{
    QVector<QPointF> &v = m_hist[int(s)];
    v.append(QPointF(x, y));
    while (v.size() > m_capacity) v.removeFirst();
}

double DataModel::xOf(qint64 wallMs) const
{
    if (m_t0WallMs == 0) return 0.0;
    return double(wallMs - m_t0WallMs) / 1000.0;
}

void DataModel::recountStat(int seriesIdx)
{
    Stat st;                                   /* 先算在局部，最后一次性写回 */

    const QVector<QPointF> &v = m_hist[seriesIdx];
    for (int i = 0; i < v.size(); ++i) {
        const double y = v.at(i).y();
        if (std::isnan(y)) continue;
        if (st.n == 0) { st.min = y; st.max = y; }
        else {
            if (y < st.min) st.min = y;
            if (y > st.max) st.max = y;
        }
        st.sum += y;
        ++st.n;
    }
    m_stat[seriesIdx] = st;
}

void DataModel::trimKeyFront()
{
    if (m_stampMs.isEmpty()) {
        m_histKeys.clear();
        return;
    }

    /* 时间轴以时间升序排列，键表是它的子集：丢掉早于首点的历史键，
     * 然后把键表队首的时间坐标同步成时间轴首点，后续插入就落在正确位置 */
    const qint64 front = m_stampMs.first();
    int drop = 0;
    while (drop < m_histKeys.size() && m_histKeys.at(drop).wallMs < front) ++drop;
    if (drop > 0) m_histKeys.remove(0, drop);
}

void DataModel::insertOrdered(const EnvFrame &f, qint64 wallMs, bool histFlag)
{
    const double x = xOf(wallMs);

    /* 常见情形是追加：补传数据的时间戳都在实时帧之前，走到这里通常是历史段的延伸 */
    if (m_stampMs.isEmpty() || wallMs >= m_stampMs.last()) {
        m_latest = f;
        ++m_frames;
        m_stampMs.append(wallMs);
        m_histFlag.append(histFlag);
        for (int i = 0; i < int(Series::Count); ++i) push(Series(i), value(f, Series(i)), x);
    } else {
        /* 时间戳落在已有数据中间（或更早）：二分找位置后逐条对齐插入。
         * 上界查找使相同时间戳的新点排在已有点之后，不改变已有点的先后。 */
        const int at = int(std::upper_bound(m_stampMs.constBegin(), m_stampMs.constEnd(),
                                            wallMs) - m_stampMs.constBegin());
        m_stampMs.insert(at, wallMs);
        m_histFlag.insert(at, histFlag);
        for (int i = 0; i < int(Series::Count); ++i)
            m_hist[i].insert(at, QPointF(x, value(f, Series(i))));
    }

    /* 超容量时统一从最老的一端丢（时间轴、来源标记、6 条曲线下标保持一致） */
    while (m_stampMs.size() > m_capacity) {
        m_stampMs.removeFirst();
        if (!m_histFlag.isEmpty()) m_histFlag.removeFirst();
        for (int i = 0; i < int(Series::Count); ++i) {
            if (!m_hist[i].isEmpty()) m_hist[i].removeFirst();
        }
    }

    for (int i = 0; i < int(Series::Count); ++i) recountStat(i);
}

void DataModel::addFrame(const EnvFrame &f, qint64 wallMs)
{
    if (m_t0WallMs == 0) m_t0WallMs = wallMs;
    insertOrdered(f, wallMs, false);
}

qint64 DataModel::resolveStampMs(quint32 tsRaw, qint64 afterMs)
{
    const int hh = int(tsRaw / 10000U);
    const int mm = int((tsRaw / 100U) % 100U);
    const int ss = int(tsRaw % 100U);
    if (hh >= 24 || mm >= 60 || ss >= 60) return 0;      /* 字段非法 */

    const QDate d = QDate::currentDate();
    qint64 ms = QDateTime(d, QTime(hh, mm, ss)).toMSecsSinceEpoch();
    /* afterMs 是已有的最后时刻：换算结果比它早半天以上时按次日算（跨零点那批） */
    if (afterMs > 0 && ms < afterMs - 12LL * 3600 * 1000)
        ms = QDateTime(d.addDays(1), QTime(hh, mm, ss)).toMSecsSinceEpoch();
    return ms;
}

bool DataModel::addHistory(const HistoryFrame &h, qint64 *wallMsOut)
{
    /* 先把不该入库的分出来，避免计数被无意义的帧污染 */
    if (h.tsRaw == 0U || !h.envOk || h.rawFrame.size() != FrameProto::ENV_TOTAL) {
        ++m_histBad;
        return false;
    }

    /* 原始时间戳只是 hh*10000+mm*100+ss，没有年月日：换算成本机当天的时刻，
     * 跨零点那一批按次日算。基准取栈内已有的最后时刻，因此栈内次序仍然单调。 */
    qint64 wallMs = resolveStampMs(h.tsRaw, m_stampMs.isEmpty() ? 0 : m_stampMs.last());
    if (wallMs == 0) {
        /* 时间戳字段非法（板2 未对时或字段错位）：退回用到达时刻排，数据内容照收 */
        wallMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (wallMsOut != nullptr) *wallMsOut = wallMs;

    /* 去重键 = 时间轴坐标（由原始时间戳换算）+ 20 字节原始帧内容。
     * 时间戳相同时逐字节比较原始帧：内容一致才算重复，内容不同按两条数据收下。 */
    for (int i = 0; i < m_histKeys.size(); ++i) {
        if (m_histKeys.at(i).wallMs == wallMs && m_histKeys.at(i).raw == h.rawFrame) {
            ++m_histDup;
            return false;
        }
    }

    /* 时间轴原点：第一条入库的数据（不论实时还是补传）决定它。
     * 若首批数据全是补传的历史帧，原点就是最早那批历史数据的时刻。 */
    if (m_t0WallMs == 0) m_t0WallMs = wallMs;
    insertOrdered(h.env, wallMs, true);
    HistKey k;
    k.wallMs = wallMs;
    k.raw = h.rawFrame;
    m_histKeys.append(k);

    ++m_histIn;
    m_histLastTs = h.tsRaw;
    m_latest = h.env;                 /* 历史数据也可能比实时数据新，界面按最新入库值显示 */

    trimKeyFront();
    return true;
}

bool DataModel::saveHistoryCsv(const QString &path, QString *err) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = file.errorString();
        return false;
    }

    QTextStream ts(&file);
    ts.setEncoding(QStringConverter::Utf8);
    ts << QChar(0xFEFF);                 /* UTF-8 BOM：Excel 直接双击不乱码 */
    ts << CsvRecorder::header() << "\r\n";

    /* 各系列下标严格对齐：第 i 个点就是第 i 帧 */
    const int n = m_stampMs.size();
    for (int i = 0; i < n; ++i) {
        const qint64 ms = m_stampMs.at(i);
        QStringList row;
        row << QDateTime::fromMSecsSinceEpoch(ms).toString("yyyy-MM-dd HH:mm:ss.zzz");
        for (int s = 0; s < int(Series::Count); ++s) {
            const QVector<QPointF> &v = m_hist[s];
            const int off = v.size() - n;                 /* 与时间轴对齐 */
            if (s == int(Series::Humi) && off + i >= 0 && off + i < v.size() &&
                std::isnan(v.at(off + i).y())) {
                row << "--";                              /* 无效湿度标记 */
            } else if (off + i >= 0 && off + i < v.size()) {
                row << QString::number(v.at(off + i).y(), 'f', 2);
            } else {
                row << "";
            }
        }
        row << "1";                                       /* 节点 ID：当前固定 1 */
        row << "OK";
        /* 来源列放最后：实时帧与补传的历史帧混在同一份导出里也能分开 */
        row << CsvRecorder::sourceText(m_histFlag.value(i, false) ? CsvRecorder::SourceHistory
                                                                 : CsvRecorder::SourceLive);
        ts << row.join(',') << "\r\n";
    }

    ts.flush();
    file.close();
    return true;
}

/* CsvRecorder */

CsvRecorder::~CsvRecorder()
{
    stop();
}

QString CsvRecorder::sourceText(Source s)
{
    return (s == SourceHistory) ? QStringLiteral("补传") : QStringLiteral("实时");
}

QString CsvRecorder::header()
{
    return QStringLiteral("时间,温度(℃),湿度(%RH),气压(hPa),光照(lx),TVOC(ppb),MQ-135,节点ID,CRC状态,数据来源");
}

bool CsvRecorder::start(const QString &path, QString *err)
{
    stop();

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = m_file.errorString();
        return false;
    }

    QTextStream ts(&m_file);
    ts.setEncoding(QStringConverter::Utf8);
    ts << QChar(0xFEFF);
    ts << header() << "\r\n";
    ts.flush();

    m_rows = 0;
    return true;
}

void CsvRecorder::stop()
{
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
}

void CsvRecorder::append(const EnvFrame &f, const QDateTime &when, quint8 crcState,
                         quint8 suspectMask, Source source)
{
    if (!m_file.isOpen()) return;

    QString status = (crcState == FrameProto::OK ? QStringLiteral("OK") : QStringLiteral("ERR"));
    if (suspectMask != 0U)
        status += QStringLiteral("+RANGE");   /* 本帧有量超出物理合理范围（值仍照记） */

    QStringList row;
    row << when.toString("yyyy-MM-dd HH:mm:ss.zzz");
    row << QString::number(f.tempC(), 'f', 2);
    row << (f.humiValid() ? QString::number(f.humiRh(), 'f', 2) : QStringLiteral("--"));
    row << QString::number(f.pressHpa(), 'f', 1);
    row << QString::number(f.lightLx);
    row << QString::number(f.tvocPpb);
    row << QString::number(f.mq135Raw);
    row << QString::number(f.nodeId);
    row << status;
    row << sourceText(source);            /* 来源列必须是最后一列，与表头一致 */

    QTextStream ts(&m_file);
    ts.setEncoding(QStringConverter::Utf8);
    ts << row.join(',') << "\r\n";
    ts.flush();                        /* 1 s 一行，每行都落盘，断电不丢已记录的 */

    ++m_rows;
}
