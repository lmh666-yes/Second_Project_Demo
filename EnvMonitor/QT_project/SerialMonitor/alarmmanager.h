#ifndef ALARMMANAGER_H
#define ALARMMANAGER_H

/* ============================================================================
 *  alarmmanager.h —— 双阈值 + 回差（迟滞）告警
 *  双阈值 + 回差：温度在 35.0℃ 抖 0.1℃ 时简单比较会每秒报警又恢复，告警表刷屏、
 *  蜂鸣器反复响。进阈值立即报警，恢复须回到 阈值 ∓ 回差 之外；例：上限 35.0℃、
 *  回差 1.0℃ ⇒ 35.01℃ 报警，降到 33.99℃ 才恢复。
 *  三级分工（《项目整体评审与升级建议.md》§10）：板1 本地蜂鸣器 / 本文件（界面）/
 *  板2 云端微信 MQTT；告警由采集端与上位机按阈值判定，不是传感器自带。
 * ==========================================================================*/

#include "datamodel.h"
#include "frameparser.h"

#include <QString>
#include <QVector>
#include <QtGlobal>

enum class AlarmLevel { None = 0, Low = 1, High = 2 };

class AlarmManager
{
public:
    struct Threshold {
        bool   lowEnabled  = true;
        double low         = 0.0;
        bool   highEnabled = true;
        double high        = 0.0;
        double hyst        = 0.0;    /* 回差，> 0 */
    };

    struct Record {
        qint64     ms = 0;
        Series     series = Series::Temp;
        AlarmLevel level = AlarmLevel::None;   /* 恢复事件时为 None + recovery = true */
        double     value = 0.0;
        double     limit = 0.0;                /* 触发时对应的阈值（恢复时是刚离开的那个） */
        bool       recovery = false;

        QString text() const;                  /* 例："温度 超上限 36.20℃（上限 35.00℃）" */
    };

    AlarmManager();

    static Threshold defaultThreshold(Series s);
    void setThreshold(Series s, const Threshold &t) { m_th[int(s)] = t; }
    const Threshold &threshold(Series s) const { return m_th[int(s)]; }

    void setEnabled(bool on) { m_enabled = on; }
    bool enabled() const     { return m_enabled; }

    void reset();                              /* 清状态与记录（阈值保留） */

    /* 收一帧：返回本次新产生的事件（可能为空），界面拿去高亮/记表
     * skipMask：bit i = Series i，该位为 1 表示这一项本帧不参与判定
     * （超出物理合理范围、传感器没接时用，见 physrange.h）。
     * 跳过时该项告警状态保持原样，避免传感器临时读到 0 把已成立的告警刷成恢复事件。 */
    QVector<Record> evaluate(const EnvFrame &f, qint64 wallMs, quint8 skipMask = 0);

    const QVector<Record> &records() const { return m_records; }
    void clearRecords() { m_records.clear(); }

    AlarmLevel state(Series s) const { return m_state[int(s)]; }
    int        activeCount() const;            /* 当前处于告警的物理量个数 */
    quint64    enterCount() const { return m_enter; }   /* 进入告警次数（不含恢复） */

    static QString levelText(AlarmLevel lv);
    static QString recordLine(const Record &r);

private:
    static const int kMaxRecords = 5000;       /* 记录表内存上限，超出丢最老 */

    Threshold  m_th[int(Series::Count)];
    AlarmLevel m_state[int(Series::Count)];
    QVector<Record> m_records;
    bool    m_enabled = true;
    quint64 m_enter = 0;
};

#endif  /* ALARMMANAGER_H */
