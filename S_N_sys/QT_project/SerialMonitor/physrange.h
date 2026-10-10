#ifndef PHYSRANGE_H
#define PHYSRANGE_H

/* ============================================================================
 *  physrange.h —— 6 个物理量的合理范围（阈值对话框、曲线自动量程、量程校验共用）
 *  单独成文件的原因：
 *    1) 阈值输入框要按物理量给出范围，否则 6 个框一律 ±10000，可以设出下限 9000、
 *       上限 10 这种自相矛盾的组合；
 *    2) 曲线 Y 轴自动量程需要兜底范围，否则一条恒 0 的线会把轴压成一条线；
 *    3) 板1 的气压/光照/TVOC/MQ-135 尚未接入（main.c 的 frame_build 直接传 0.0f/0U），
 *       0.0 hPa 会命中气压低于下限 980，联调时产生气压过低与光照过低两条假告警。
 *  因此超出合理范围的值照常显示、照常记 CSV，但不参与告警判定，只在统计里计数、
 *  在日志里各报一次：先让人看见，再决定要不要当真。
 *
 *  取值依据（均留有余量，不按手册标称量程死抠）：
 *    温度 -40 ~ 80 ℃     DHT11 只有 0~50℃，放宽给以后的 SHT30/BMP280
 *    湿度   0 ~ 100 %RH   DHT11 可靠量程 20~90%，但干燥环境真实值就 <20%RH
 *    气压 300 ~ 1100 hPa  BMP280 的量程是 300~1100 hPa
 *    光照   0 ~ 20000 lx  BH1750 满量程 54612 lx，>20000 基本是直射阳光/读数可疑
 *    TVOC   0 ~ 5000 ppb  CCS811 量程约 0~1187 ppb，>5000 判可疑
 *    MQ-135 0 ~ 4095      12 位 ADC 满量程（>4095 说明帧里字段本身不对）
 * ==========================================================================*/

#include "datamodel.h"

#include <QString>
#include <cmath>

namespace PhysRange {

inline double lo(Series s)
{
    switch (s) {
    case Series::Temp:  return -40.0;
    case Series::Humi:  return 0.0;
    case Series::Press: return 300.0;
    case Series::Light: return 0.0;
    case Series::Tvoc:  return 0.0;
    case Series::Mq135: return 0.0;
    default:            return 0.0;
    }
}

inline double hi(Series s)
{
    switch (s) {
    case Series::Temp:  return 80.0;
    case Series::Humi:  return 100.0;
    case Series::Press: return 1100.0;
    case Series::Light: return 20000.0;
    case Series::Tvoc:  return 5000.0;
    case Series::Mq135: return 4095.0;
    default:            return 10000.0;
    }
}

/* 超出合理范围返回 true。NaN / ±Inf 不在这里判，那属无效值，由调用方处理。 */
inline bool suspect(Series s, double v)
{
    if (!std::isfinite(v))
        return false;
    return (v < lo(s)) || (v > hi(s));
}

/* "−40 ~ 80" 这样的提示文本（阈值设置对话框里显示用） */
inline QString text(Series s)
{
    const int dec = DataModel::decimals(s);
    return QStringLiteral("%1 ~ %2")
            .arg(lo(s), 0, 'f', dec)
            .arg(hi(s), 0, 'f', dec);
}

}   /* namespace PhysRange */

#endif  /* PHYSRANGE_H */
