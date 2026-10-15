#ifndef __FWLIB_SYS_BMP280_H
#define __FWLIB_SYS_BMP280_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* ================================================================
 *  sys_bmp280.h — 气压 + 温度传感器 BMP280 头文件
 * ================================================================
 *  功能 : BMP280(气压 + 温度,绝对气压精度 ±1 hPa)封装: chip id(0x58)自检 +
 *         读标定系数 + forced 单次转换 + Bosch 定点补偿 + 海拔换算。全部经
 *         sys_i2c 访问,本文件不直接操作寄存器。
 *  接线 : I2C1(PB8 = SCL / PB9 = SDA,与 24C02 同总线);器件 7 位地址 0x76
 *         (SDO 接地),SDO 接 VDDIO 时为 0x77。
 *  约束 : 气压随海拔与天气变化(同室晴天与台风天可差 30 hPa),只宜看趋势与
 *         相对变化;海拔按标准大气换算,需填当地当天海平面气压;气压对气流
 *         敏感;温度为芯片自身温度,受 MCU/稳压器加热时偏高 1~3 ℃。
 * 详见 : 检测数据端设计.md
 * ================================================================ */


/* 区块 1：定义与宏定义区（换板子只改这里） */
/* 器件 7 位地址(SDO 接地 = 0x76;SDO 接 VDDIO = 0x77) */
#define SYS_BMP280_ADDR_SDO_GND   0x76U
#define SYS_BMP280_ADDR_SDO_VDD   0x77U

/* 本板默认地址: SDO 接 GND,按实际接线修改 */
#define SYS_BMP280_ADDR           SYS_BMP280_ADDR_SDO_GND


/* 区块 2：量程 / 输出速率宏 */
/* 温度过采样 osrs_t: 0=跳过 1=×1 2=×2 3=×4 4=×8 5=×16
 * 过采样越高噪声越低,单次转换时间与功耗线性增加;×2 时温度噪声约 0.0058 ℃。 */
#define SYS_BMP280_OSRS_T         2U

/* 气压过采样 osrs_p: 同上,0=跳过 1=×1 2=×2 3=×4 4=×8 5=×16
 * 气压噪声比温度大,×4 起步才稳定(×1 时 RMS 噪声约 3.3 Pa,×16 约 0.2 Pa)。 */
#define SYS_BMP280_OSRS_P         3U

/* IIR 低通滤波系数(0xF5 的 filter[4:2]): 0=关 1=2 2=4 3=8 4=16
 * 硬件滤波会拖慢对突变的响应;测趋势取 2~4,测快速变化或作高度计时关掉,
 * 改由上层软件滤波。 */
#define SYS_BMP280_IIR_FILTER     2U

/* 是否使用 normal(连续)模式: 0 = forced 单次(默认) 1 = normal 连续转换
 * normal 模式连续转换会自加热,温度补偿(t_fine → 气压)随之偏高;forced 每次只
 * 转换几十 ms,自加热可忽略。置 1 时需保证散热并调大 SYS_BMP280_IIR_FILTER。 */
#define SYS_BMP280_USE_NORMAL_MODE   0

/* forced 模式下触发转换后固定等待的毫秒数
 * 手册单次转换最晚 43ms(×16 + ×16);本配置(×2 / ×4)约 10ms,取 15ms 余量后轮询 status。 */
#define SYS_BMP280_FORCED_DELAY_MS   15U

/* 等待 STATUS.measuring 变 0 的超时(ms),器件异常或总线被拉死时兜底 */
#define SYS_BMP280_MEAS_TIMEOUT_MS   100U


/* 编译期自检（配置非法时 #error） */
/* osrs_t / osrs_p 只允许 0(跳过)~ 5(×16),位域只有 3 位 */
#if ((SYS_BMP280_OSRS_T > 5U) || (SYS_BMP280_OSRS_P > 5U))
#  error "sys_bmp280: SYS_BMP280_OSRS_T / SYS_BMP280_OSRS_P 只允许 0~5(0=跳过,5=×16)"
#endif

/* osrs_t 与 osrs_p 不能同时为 0,否则器件不产生任何转换结果 */
#if ((SYS_BMP280_OSRS_T == 0U) && (SYS_BMP280_OSRS_P == 0U))
#  error "sys_bmp280: osrs_t 与 osrs_p 不能同时为 0,否则器件不产生任何转换结果"
#endif

/* IIR 滤波系数只允许 0(关)~ 4(×16) */
#if (SYS_BMP280_IIR_FILTER > 4U)
#  error "sys_bmp280: SYS_BMP280_IIR_FILTER 只允许 0~4(0=关,4=×16)"
#endif

/* SYS_BMP280_USE_NORMAL_MODE 只允许 0 / 1 */
#if ((SYS_BMP280_USE_NORMAL_MODE != 0) && (SYS_BMP280_USE_NORMAL_MODE != 1))
#  error "sys_bmp280: SYS_BMP280_USE_NORMAL_MODE 只允许 0(forced)或 1(normal)"
#endif


/* 区块 3：返回码（与 sys_i2c 错误码同风格）
 * 成功 = 0;负数 = sys_i2c 错误码(SYS_I2C_ERR_*,见 sys_i2c.h);正数 = 本模块错误码 */
#define SYS_BMP280_OK                0U    /* 成功 */
#define SYS_BMP280_ERR_PARAM         1U    /* 入参非法(bus 越界 / 输出指针为空) */
#define SYS_BMP280_ERR_CHIP_ID       2U    /* chip id 不是 0x58(0x60 = BME280,
                                            * 0x55 = BMP180;寄存器不同,本驱动不适用) */
#define SYS_BMP280_ERR_CALIB         3U    /* 标定系数(0x88~0x9F)读取/写入失败 */
#define SYS_BMP280_ERR_TIMEOUT       4U    /* 等待转换完成超时(器件没接好/总线异常) */


/* 区块 4：数据结构：物理量数据(已换算) */
typedef struct {
    float temp_c;        /* 温度,单位 ℃(0.01 ℃ 定点换算而来) */
    float press_pa;      /* 绝对气压,单位 Pa */
    float press_hpa;     /* 绝对气压,单位 hPa(= 100 Pa,气象常用) */
    float altitude_m;    /* 海拔,单位 m;仅当 SYS_BMP280_SEA_LEVEL_HPA_DEFAULT
                          *   为当地真实海平面气压时有意义 */
} SYS_BMP280_Data_t;


/* 区块 5：基础功能 */
/* 初始化(用区块 1 的默认地址)
 * 返回 : SYS_BMP280_OK(0) = 成功;正数 = SYS_BMP280_ERR_*;负数 = SYS_I2C_ERR_*
 * 访问 : 读 0xD0 自检,软复位(0xE0 ← 0xB6),突发读 0x88~0x9F 共 24 字节标定系数,
 *        再写 0xF5(CONFIG) / 0xF4(CTRL_MEAS)
 * 示例 : if (SYS_BMP280_Init(SYS_I2C_1) != 0) { printf("BMP280 不在线\r\n"); } */
uint8_t SYS_BMP280_Init(SysI2cId_t bus);

/* 初始化(指定 7 位地址)
 * 参数 : bus   — 总线(内部只调用 I2C_Init 完成该总线初始化)
 *        addr7 — 器件 7 位地址(0x76 或 0x77)
 * 返回 : 同 SYS_BMP280_Init
 * 说明 : addr7 按总线下标各存一份,后续 ReadRaw / Read / Trigger / GetID 复用同一地址。
 * 示例 : SYS_BMP280_InitAddr(SYS_I2C_1, SYS_BMP280_ADDR_SDO_VDD);  // SDO 接 VDDIO */
uint8_t SYS_BMP280_InitAddr(SysI2cId_t bus, uint8_t addr7);

/* 读 chip id(0xD0),用于确认器件型号
 * 返回 : 0x58 = BMP280;0x60 = BME280;0x55 = BMP180;负数 = I2C 错误码
 * 示例 : int id = SYS_BMP280_GetID(SYS_I2C_1);   // 正常 = 88(0x58) */
int SYS_BMP280_GetID(SysI2cId_t bus);

/* 读一次原始值并做 Bosch 定点补偿,输出温度 0.01 ℃ 与气压 Pa
 * 参数 : temp_c100 — 输出温度,单位 0.01 ℃(例 2356 = 23.56 ℃);可传 NULL
 *        press_pa  — 输出气压,单位 Pa;可传 NULL
 * 返回 : SYS_BMP280_OK(0) = 成功;正数 = SYS_BMP280_ERR_*;负数 = SYS_I2C_ERR_*
 * 说明 : forced 模式下内部依次触发转换、等 status、突发读 6 字节,事务自包含,
 *        无需先调 Trigger;6 字节一次读回保证气压与温度同一采样时刻。
 * 示例 : int32_t t100; uint32_t pa; SYS_BMP280_ReadRaw(SYS_I2C_1, &t100, &pa); */
int SYS_BMP280_ReadRaw(SysI2cId_t bus, int32_t *temp_c100, uint32_t *press_pa);

/* 读一次并换算成物理量(温度/气压/海拔)
 * 返回 : SYS_BMP280_OK(0) = 成功;正数 = SYS_BMP280_ERR_*;负数 = SYS_I2C_ERR_*
 * 示例 : SYS_BMP280_Data_t d; SYS_BMP280_Read(SYS_I2C_1, &d); */
int SYS_BMP280_Read(SysI2cId_t bus, SYS_BMP280_Data_t *out);


/* 区块 6：扩展功能 */
/* 手动触发一次单次转换(forced 模式),由上层控制采样节奏
 * 返回 : SYS_BMP280_OK(0) = 成功;负数 = SYS_I2C_ERR_*
 * 说明 : 触发后由调用方决定何时读;SYS_BMP280_Read / ReadRaw 内部已含触发,
 *        不必再调本函数。
 * 示例 : SYS_BMP280_Trigger(SYS_I2C_1); delay_ms(20); SYS_BMP280_ReadRaw(...); */
int SYS_BMP280_Trigger(SysI2cId_t bus);

/* 国际气压高度公式换算海拔(纯计算,不访问器件)
 * 公式 : h = 44330 × [1 − (P / P0)^(1/5.255)],标准大气模型
 * 参数 : press_hpa      — 当前绝对气压,hPa
 *        sea_level_hpa  — 当地海平面气压,hPa(填 0 或负数时返回 0)
 * 返回 : 海拔,m(相对海平面)
 * 说明 : P0 必须填当地当天实测值,否则海拔随天气上下浮动几十米;
 *        只比较相对高度变化时两次使用同一 P0。
 * 示例 : float h = SYS_BMP280_AltitudeM(1000.0f, 1013.25f);   // ≈ 111 m */
float SYS_BMP280_AltitudeM(float press_hpa, float sea_level_hpa);

/* 当地海平面气压默认值(hPa),按实际需要修改
 * 1013.25 是标准大气定义值,不是任何一天的实测值。 */
#define SYS_BMP280_SEA_LEVEL_HPA_DEFAULT   1013.25f

#endif /* __FWLIB_SYS_BMP280_H */
