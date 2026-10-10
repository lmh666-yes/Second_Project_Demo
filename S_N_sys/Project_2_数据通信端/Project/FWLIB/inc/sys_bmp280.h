#ifndef __FWLIB_SYS_BMP280_H
#define __FWLIB_SYS_BMP280_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* sys_bmp280.h: Bosch BMP280(气压 + 温度,绝对气压精度 ±1 hPa)驱动
 * 流程：chip id 自检(0x58)、读标定系数、forced 单次转换、读原始值、
 *       Bosch 官方定点补偿、海拔换算。
 * 接线：I2C1 的 PB8 = SCL、PB9 = SDA，与 24C02 同总线；
 *       器件地址 0x76(SDO 接地)或 0x77(SDO 接 VDDIO)。
 * 寄存器访问全部经 sys_i2c，不直接操作寄存器。
 *
 * 使用注意：
 * 1) 绝对气压随海拔与天气变化，同一房间晴天与台风天可差 30 hPa(约 250 m
 *    的假海拔)，应看短时间内的趋势与相对变化。
 * 2) Data_t::altitude_m 只有在 SYS_BMP280_SEA_LEVEL_HPA_DEFAULT 填当地当天
 *    实测海平面气压时才代表真海拔，填 1013.25 得到的是标准大气等效海拔。
 * 3) 气压对气流敏感，风扇、穿堂风、手靠近会使读数跳几 hPa；测趋势需加带小孔
 *    的缓冲腔，并配合 SYS_BMP280_IIR_FILTER 与上层滑动平均。
 * 4) 温度为芯片自身温度，被 MCU 或稳压器烤热时偏高 1~3 ℃；环境温度用独立
 *    温度传感器(如 SHT30)测量。
 * 5) 海拔换算用国际气压高度公式，前提是标准大气(海平面 15 ℃)；实际大气
 *    海拔越高偏差越大，对流层内约 1%~3%。 */


/* 器件 7 位地址(SDO 接地 = 0x76;SDO 接 VDDIO = 0x77) */
#define SYS_BMP280_ADDR_SDO_GND   0x76U
#define SYS_BMP280_ADDR_SDO_VDD   0x77U

/* 本板默认地址: SDO 接 GND,按实际接线修改 */
#define SYS_BMP280_ADDR           SYS_BMP280_ADDR_SDO_GND


/* 温度过采样 osrs_t: 0=跳过 1=×1 2=×2 3=×4 4=×8 5=×16
 * 取值越大噪声越低，单次转换时间与功耗线性增加。
 * ×2 时温度噪声约 0.0058 ℃。 */
#define SYS_BMP280_OSRS_T         2U

/* 气压过采样 osrs_p: 同上,0=跳过 1=×1 2=×2 3=×4 4=×8 5=×16
 * ×1 时气压 RMS 噪声约 3.3 Pa,×16 约 0.2 Pa。 */
#define SYS_BMP280_OSRS_P         3U

/* IIR 低通滤波系数(0xF5 的 filter[4:2]): 0=关 1=2 2=4 3=8 4=16
 * 系数越大对突变的响应越慢。测气压趋势用 2~4，测快速变化时关闭，改由上层滤波。 */
#define SYS_BMP280_IIR_FILTER     2U

/* 是否使用 normal(连续)模式: 0 = forced 单次(默认)
 *                            1 = normal 连续转换
 *
 * normal 模式持续转换，器件自加热使温度偏高几度；气压解算要用温度做补偿
 * (温度 → t_fine → 气压)，温度偏高会带偏气压输出，表现为气压缓慢漂移。
 * forced 模式每次只在需要时转换一次(几十 ms)，占空比低，自加热可忽略。
 * 置 1 时需保证器件散热，并把 SYS_BMP280_IIR_FILTER 调大。 */
#define SYS_BMP280_USE_NORMAL_MODE   0

/* forced 模式下"触发转换后固定等待"的毫秒数
 * 说明 : 手册单次转换最晚 43ms(最高过采样 ×16 + ×16);
 *        本配置(×2 / ×4)约 10ms 量级,给 15ms 余量后再轮询 status 兜底。 */
#define SYS_BMP280_FORCED_DELAY_MS   15U

/* 等待 STATUS.measuring 变 0 的超时(ms)
 * 说明 : 器件异常/总线被拉死时的兜底,绝不死等。 */
#define SYS_BMP280_MEAS_TIMEOUT_MS   100U


/* 编译期自检: 配置越界时直接报错 */
/* osrs_t / osrs_p 只允许 0(跳过)~ 5(×16),位域只有 3 位 */
#if ((SYS_BMP280_OSRS_T > 5U) || (SYS_BMP280_OSRS_P > 5U))
#  error "sys_bmp280: SYS_BMP280_OSRS_T / SYS_BMP280_OSRS_P 只允许 0~5(0=跳过,5=×16)"
#endif

/* 两个量都跳过时器件不产生转换,读回来永远是 0 */
#if ((SYS_BMP280_OSRS_T == 0U) && (SYS_BMP280_OSRS_P == 0U))
#  error "sys_bmp280: osrs_t 与 osrs_p 不能同时为 0,否则器件不产生任何转换结果"
#endif

/* IIR 滤波系数只允许 0(关)~ 4(×16) */
#if (SYS_BMP280_IIR_FILTER > 4U)
#  error "sys_bmp280: SYS_BMP280_IIR_FILTER 只允许 0~4(0=关,4=×16)"
#endif

/* 只认 0 / 1 */
#if ((SYS_BMP280_USE_NORMAL_MODE != 0) && (SYS_BMP280_USE_NORMAL_MODE != 1))
#  error "sys_bmp280: SYS_BMP280_USE_NORMAL_MODE 只允许 0(forced)或 1(normal)"
#endif


/* 返回码: 成功 = 0;负数 = sys_i2c 错误码(SYS_I2C_ERR_*,见 sys_i2c.h);
 *         正数 = 本模块错误码(含义见下)。 */
#define SYS_BMP280_OK                0U    /* 成功 */
#define SYS_BMP280_ERR_PARAM         1U    /* 入参非法(bus 越界 / 输出指针为空) */
#define SYS_BMP280_ERR_CHIP_ID       2U    /* chip id 不是 0x58,不是 BMP280
                                            * (读 0x60 = BME280,读 0x55 = BMP180;
                                            *  这两个器件寄存器不同,本驱动不适用) */
#define SYS_BMP280_ERR_CALIB         3U    /* 标定系数(0x88~0x9F)读取/写入失败 */
#define SYS_BMP280_ERR_TIMEOUT       4U    /* 等待转换完成超时(器件没接好/总线异常) */


/* 物理量数据(已换算) */
typedef struct {
    float temp_c;        /* 温度,单位 ℃(由 0.01 ℃ 定点值换算) */
    float press_pa;      /* 绝对气压,单位 Pa */
    float press_hpa;     /* 绝对气压,单位 hPa(= 100 Pa,气象常用) */
    float altitude_m;    /* 海拔,单位 m,仅在 SYS_BMP280_SEA_LEVEL_HPA_DEFAULT
                          *   填当地当天实测海平面气压时才代表真海拔,见文件头 2) */
} SYS_BMP280_Data_t;


/* 初始化(用默认地址 SYS_BMP280_ADDR)
 * 返回 : SYS_BMP280_OK(0) = 成功;正数 = SYS_BMP280_ERR_*;负数 = SYS_I2C_ERR_*
 * 流程 : 读 0xD0 自检,软复位(0xE0 ← 0xB6),突发读 0x88~0x9F 共 24 字节标定
 *        系数,再写 0xF5(CONFIG)/0xF4(CTRL_MEAS);不直调标准库外设函数 */
uint8_t SYS_BMP280_Init(SysI2cId_t bus);

/* 初始化(指定 7 位地址)
 * 参数 : bus   = 总线,只完成该总线的 I2C_Init
 *        addr7 = 器件 7 位地址(0x76 或 0x77)
 * 返回 : 同 SYS_BMP280_Init
 * 说明 : addr7 按总线下标记录,后续 ReadRaw / Read / Trigger / GetID 都用同一
 *        地址,不会出现初始化用 0x77、读数据仍访问 0x76 的情况。 */
uint8_t SYS_BMP280_InitAddr(SysI2cId_t bus, uint8_t addr7);

/* 读 chip id(0xD0),用于判断器件型号
 * 返回 : 0x58 = BMP280;0x60 = BME280;0x55 = BMP180;负数 = I2C 错误码 */
int SYS_BMP280_GetID(SysI2cId_t bus);

/* 读一次原始值并做官方定点补偿,输出温度 0.01 ℃ 与气压 Pa
 * 参数 : temp_c100 = 输出温度,单位 0.01 ℃(例 2356 = 23.56 ℃);可传 NULL
 *        press_pa  = 输出气压,单位 Pa;可传 NULL
 * 返回 : SYS_BMP280_OK(0) = 成功;正数 = 本模块错误码;负数 = I2C 错误码
 * 说明 : forced 模式下内部依次完成触发转换、等 status、突发读 6 字节,调用方
 *        不必先调 Trigger。6 字节一次读回可保证气压与温度属于同一采样时刻,
 *        分开读会引入采样时刻不一致的误差。 */
int SYS_BMP280_ReadRaw(SysI2cId_t bus, int32_t *temp_c100, uint32_t *press_pa);

/* 读一次并换算成物理量(温度/气压/海拔)
 * 返回 : SYS_BMP280_OK(0) = 成功;正数 = 本模块错误码;负数 = I2C 错误码 */
int SYS_BMP280_Read(SysI2cId_t bus, SYS_BMP280_Data_t *out);


/* 手动触发一次单次转换(forced 模式)
 * 返回 : SYS_BMP280_OK(0) = 成功;负数 = I2C 错误码
 * 说明 : 用于上层自行控制采样节奏,触发后由调用方决定何时读。
 *        使用 SYS_BMP280_Read / ReadRaw 时内部已含触发,不必再调本函数。 */
int SYS_BMP280_Trigger(SysI2cId_t bus);

/* 国际气压高度公式换算海拔(纯计算,不访问器件)
 * 公式 : h = 44330 × [1 − (P / P0)^(1/5.255)],标准大气模型
 * 参数 : press_hpa     = 当前绝对气压,hPa
 *        sea_level_hpa = 当地海平面气压,hPa(填 0 或负数时返回 0)
 * 返回 : 海拔,m(相对海平面)
 * 说明 : 绝对气压随天气变化,P0 必须填当地当天的值,否则算出的海拔会随天气
 *        浮动几十米;只比较相对高度变化时,两次用同一个 P0 即可。 */
float SYS_BMP280_AltitudeM(float press_hpa, float sea_level_hpa);

/* 当地海平面气压默认值(hPa),按当地当天实测值修改(见文件头 2))
 * 1013.25 是标准大气定义值,不是任何一天的实测值。 */
#define SYS_BMP280_SEA_LEVEL_HPA_DEFAULT   1013.25f

#endif /* __FWLIB_SYS_BMP280_H */
