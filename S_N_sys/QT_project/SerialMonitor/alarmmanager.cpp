#include "alarmmanager.h"

#include <QDateTime>
#include <cmath>

AlarmManager::AlarmManager()
{
    for (int i = 0; i < int(Series::Count); ++i) {
        m_th[i] = defaultThreshold(Series(i));
        m_state[i] = AlarmLevel::None;
    }
    m_records.reserve(256);
}

AlarmManager::Threshold AlarmManager::defaultThreshold(Series s)
{
    Threshold t;

    switch (s) {
    case Series::Temp:                     /* 实验室环境：5~35 ℃，回差 1 ℃ */
        t.low = 5.0;    t.high = 35.0;    t.hyst = 1.0;
        break;
    case Series::Humi:                     /* 20~80 %RH，回差 2 %RH */
        t.low = 20.0;   t.high = 80.0;    t.hyst = 2.0;
        break;
    case Series::Press:                    /* 980~1030 hPa，回差 1 hPa */
        t.low = 980.0;  t.high = 1030.0;  t.hyst = 1.0;
        break;
    case Series::Light:                    /* 只关心太暗：低于 50 lx 报，上限不用 */
        t.low = 50.0;   t.lowEnabled = true;
        t.high = 0.0;   t.highEnabled = false;
        t.hyst = 20.0;
        break;
    case Series::Tvoc:                     /* 只关心超标：>1000 ppb 报（CCS811 常见阈值） */
        t.low = 0.0;    t.lowEnabled = false;
        t.high = 1000.0; t.highEnabled = true;
        t.hyst = 50.0;
        break;
    case Series::Mq135:                    /* 只关心超标：ADC 原值 >2000 报 */
        t.low = 0.0;    t.lowEnabled = false;
        t.high = 2000.0; t.highEnabled = true;
        t.hyst = 100.0;
        break;
    default:
        break;
    }
    return t;
}

void AlarmManager::reset()
{
    for (int i = 0; i < int(Series::Count); ++i) m_state[i] = AlarmLevel::None;
    m_records.clear();
    m_enter = 0;
}

QString AlarmManager::levelText(AlarmLevel lv)
{
    switch (lv) {
    case AlarmLevel::Low:  return QStringLiteral("低于下限");
    case AlarmLevel::High: return QStringLiteral("超上限");
    default:               return QStringLiteral("正常");
    }
}

QString AlarmManager::recordLine(const Record &r)
{
    return QStringLiteral("%1 %2 %3%4（%5 %6%4）")
            .arg(QString::fromUtf8(DataModel::name(r.series)))
            .arg(r.recovery ? QStringLiteral("恢复") : levelText(r.level))
            .arg(r.value, 0, 'f', (r.series == Series::Mq135 || r.series == Series::Light ||
                                   r.series == Series::Tvoc) ? 0 : 2)
            .arg(QString::fromUtf8(DataModel::unit(r.series)))
            .arg(r.recovery ? QStringLiteral("刚离开")
                            : (r.level == AlarmLevel::High ? QStringLiteral("上限")
                                                           : QStringLiteral("下限")))
            .arg(r.limit, 0, 'f', (r.series == Series::Mq135 || r.series == Series::Light ||
                                   r.series == Series::Tvoc) ? 0 : 2);
}

QString AlarmManager::Record::text() const
{
    return recordLine(*this);
}

int AlarmManager::activeCount() const
{
    int n = 0;
    for (int i = 0; i < int(Series::Count); ++i)
        if (m_state[i] != AlarmLevel::None) ++n;
    return n;
}

QVector<AlarmManager::Record> AlarmManager::evaluate(const EnvFrame &f, qint64 wallMs, quint8 skipMask)
{
    QVector<Record> fresh;

    if (!m_enabled) {
        /* 关掉告警时清零状态，避免重新打开时带着旧状态乱报 */
        for (int i = 0; i < int(Series::Count); ++i) m_state[i] = AlarmLevel::None;
        return fresh;
    }

    for (int i = 0; i < int(Series::Count); ++i) {
        const Series s = Series(i);
        const Threshold &th = m_th[i];
        const double y = DataModel::value(f, s);
        if (std::isnan(y)) continue;               /* 无效值（如湿度读失败）不参与判定 */
        /* 超出物理合理范围（板1 气压/光照/TVOC/MQ-135 尚未接入、帧里恒 0）：
         * 本项跳过判定，状态保持原样，既不产生假告警，也不会把已成立的告警
         * 误报成恢复。界面把该值标黄，统计里单独计数。 */
        if (skipMask & (1u << i)) continue;

        const AlarmLevel cur = m_state[i];

        /* 1) 先按裸阈值算出候选等级 */
        AlarmLevel cand = AlarmLevel::None;
        if (th.highEnabled && y > th.high)      cand = AlarmLevel::High;
        else if (th.lowEnabled && y < th.low)   cand = AlarmLevel::Low;

        /* 2) 回差：已在告警中时，必须走出 阈值±回差 才允许恢复 */
        if (cand == AlarmLevel::None && cur != AlarmLevel::None) {
            const double h = th.hyst > 0.0 ? th.hyst : 0.0;
            if (cur == AlarmLevel::Low) {
                if (th.lowEnabled && y < th.low + h) cand = AlarmLevel::Low;
            } else if (cur == AlarmLevel::High) {
                if (th.highEnabled && y > th.high - h) cand = AlarmLevel::High;
            }
        }

        if (cand == cur) continue;                 /* 状态没变，不记录 */

        /* 3) 状态变化，记一条 */
        Record r;
        r.ms      = wallMs;
        r.series  = s;
        r.value   = y;
        r.level   = (cand == AlarmLevel::None) ? cur : cand;
        r.recovery = (cand == AlarmLevel::None);
        r.limit   = (r.level == AlarmLevel::High) ? th.high : th.low;

        if (!r.recovery) ++m_enter;
        m_state[i] = cand;

        m_records.append(r);
        while (m_records.size() > kMaxRecords) m_records.removeFirst();
        fresh.append(r);
    }

    return fresh;
}
