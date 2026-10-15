/* ============================================================================
 *  datamodel.cpp —— 【本文件由用户自己实现】
 *
 *  这个文件是留给你自己的实现起点，不是参考实现：
 *  缓存的成员、Series 枚举、Stat 结构与全部函数签名都在 datamodel.h 里，
 *  本文件只保留每个函数的空桩，以及一行"这一步要做什么"的提示。
 *  实现清单 = datamodel.h 里的全部声明，逐条对照补完即可。
 *
 *  学习路线：标准库速查 → 项目引导 → Qt
 *  对应阶段：阶段 2「协议解析与数据模型」
 *           （环形缓存、按时间戳有序插入、去重与重排号、Series 数值映射都在这一阶段）
 *           阶段 5「记录与导出」
 *           （本文件里的 CsvRecorder 与 saveHistoryCsv 属于这一阶段：
 *             表头、字段顺序、时间格式、增量写入与 flush）
 *
 *  参考实现备份（自己写完再对照，不要照抄一遍就交）：
 *      D:\GitHub_Project\_参考实现备份\20261012_195704\Qt\
 *
 *  编码：UTF-8 无 BOM、LF。
 * ==========================================================================*/

/* 下面是"桩函数参数暂时用不到"的告警抑制：等你把函数实现了，把这三行删掉即可。 */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

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

/* ===========================================================================
 *  DataModel —— 六条曲线的环形缓存与统计
 * =========================================================================*/

/* TODO(阶段2)：把容量存进 m_capacity（非法值要有兜底），并把全部曲线设为空状态 */
DataModel::DataModel(int capacity)
    : m_capacity(capacity > 0 ? capacity : 1)
{
}

/* TODO(阶段2)：改容量后按新容量裁剪已有数据，并同步重排号与统计 */
void DataModel::setCapacity(int cap)
{
}

/* TODO(阶段2)：清空全部曲线、时间戳、统计与历史计数，回到空数据状态 */
void DataModel::clear()
{
}

/* TODO(阶段2)：把 Series 枚举翻成界面显示用的中文名称（UTF-8 C 串） */
const char *DataModel::name(Series s)
{
    return "";
}

/* TODO(阶段2)：把 Series 枚举翻成单位字符串（℃ / %RH / hPa / lx / ppb / ADC） */
const char *DataModel::unit(Series s)
{
    return "";
}

/* TODO(阶段2)：给出每个量的小数位数，供界面格式化文本 */
int DataModel::decimals(Series s)
{
    return 0;
}

/* TODO(阶段2)：给出 ini 配置分组用的英文键名（thresholds/<key>），不能为空串 */
const char *DataModel::key(Series s)
{
    return "";
}

/* TODO(阶段2)：从 EnvFrame 里取出指定 Series 的物理量数值 */
double DataModel::value(const EnvFrame &f, Series s)
{
    return 0.0;
}

/* TODO(阶段2)：往指定曲线的环形缓存尾部压入一点 (x, y)，满了要挤掉最老的 */
void DataModel::push(Series s, double y, double x)
{
}

/* TODO(阶段2)：按 wallMs 把整帧六条曲线有序插入缓存，histFlag 区分实时帧与补传帧 */
void DataModel::insertOrdered(const EnvFrame &f, qint64 wallMs, bool histFlag)
{
}

/* TODO(阶段2)：把绝对毫秒时间换成曲线 X 轴用的相对秒数 */
double DataModel::xOf(qint64 wallMs) const
{
    return 0.0;
}

/* TODO(阶段2)：缓存被裁剪后重排 X 轴序号，让曲线连续 */
void DataModel::renumber()
{
}

/* TODO(阶段2)：重算第 seriesIdx 条曲线的窗口统计（交给 StatCalc 或按窗口累加） */
void DataModel::recountStat(int seriesIdx)
{
}

/* TODO(阶段2)：从各曲线的时间戳缓存头部裁掉超出容量的部分 */
void DataModel::trimKeyFront()
{
}

/* TODO(阶段2)：收到一帧实时数据后更新六条曲线、最新值与帧计数，并做去重/乱序处理 */
void DataModel::addFrame(const EnvFrame &f, qint64 wallMs)
{
}

/* TODO(阶段2)：把设备侧 32 位时间戳落成主机毫秒时间（考虑回绕与 afterMs 之后的候选） */
qint64 DataModel::resolveStampMs(quint32 tsRaw, qint64 afterMs)
{
    return 0;
}

/* TODO(阶段2)：把一帧历史数据并入缓存并返回其毫秒时间；重复/过旧帧返回 false（wallMsOut 可空） */
bool DataModel::addHistory(const HistoryFrame &h, qint64 *wallMsOut)
{
    return false;
}

/* TODO(阶段2)：取指定曲线的窗口统计（window<=0 用默认窗口长度） */
StatCalc::WindowStat DataModel::windowStat(Series s, int window) const
{
    return {};
}

/* TODO(阶段5)：把历史缓存写成 CSV 文件；失败时用 err 带回原因（err 可空） */
bool DataModel::saveHistoryCsv(const QString &path, QString *err) const
{
    return false;
}

/* ===========================================================================
 *  CsvRecorder —— 实时记录：按行追加并落盘
 * =========================================================================*/

/* TODO(阶段5)：析构时确保文件已关闭（仍在录制就先 stop） */
CsvRecorder::~CsvRecorder()
{
}

/* TODO(阶段5)：打开目标文件、写表头并置录制状态；打不开就把原因写进 err 并返回 false */
bool CsvRecorder::start(const QString &path, QString *err)
{
    return false;
}

/* TODO(阶段5)：停止录制，flush 并关闭文件，清空行计数 */
void CsvRecorder::stop()
{
}

/* TODO(阶段5)：按固定字段顺序把一帧（时间、六项数值、CRC 状态、可疑位、来源）写成一行 */
void CsvRecorder::append(const EnvFrame &f, const QDateTime &when, quint8 crcState,
                         quint8 suspectMask, Source source)
{
}

/* TODO(阶段5)：把来源枚举翻成 CSV 里写的文字（实时 / 历史补传） */
QString CsvRecorder::sourceText(Source s)
{
    return {};
}

/* TODO(阶段5)：返回 CSV 表头行，字段顺序要与 append 完全一致 */
QString CsvRecorder::header()
{
    return {};
}
