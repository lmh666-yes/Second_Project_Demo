#ifndef __FWLIB_SYS_CCS811_H
#define __FWLIB_SYS_CCS811_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* CCS811 空气质量传感器(eCO2 + TVOC)头文件
 * 本板接线 : 外接模块挂 I2C1(PB8 = SCL / PB9 = SDA,与 24C02 同总线),
 *            器件地址 0x5A(ADDR 接地),ADDR 接高为 0x5B
 *            CCS811 供 3.3V,WAKE 引脚(若有)拉低才响应 I2C
 * 初始化按手册顺序经 sys_i2c 完成 : HW_ID 自检 → 软复位 → 等 APP_VALID
 *            → APP_START → 设测量模式,再读 4 字节算法结果;本文件不直
 *            接操作寄存器,算法由厂商固件给出
 * 手册要点 : eCO2 是厂商算法按 TVOC 与历史趋势估算的等效值,TVOC 是等效
 *            总挥发性有机物,两者都不代表真实浓度,只适合同一环境内的
 *            相对变化与趋势
 *            MOX 气敏材料需老化,上电初期读数漂移大,连续运行 48 小时后
 *            绝对值才有参考意义
 *            基线在器件稳定时才可读;读出的基线由调用方保存(片内 Flash
 *            或 AT24C02),下次初始化后写回可省去重新老化;本模块不
 *            include 存储模块,频繁写 Flash 会伤寿命
 *            SetEnvData 的外部温湿度用于修正气敏算法,手册建议提供
 *            STATUS(0x00) 没有可靠的 DRDY 位,DataReady 用距上次成功读
 *            取的时间判据,见 .c;eCO2 有效范围 400~8192 ppm,TVOC
 *            0~1187 ppb,超范围值不代表真实浓度
 */


/* 定义与宏定义区 */
/* 器件 7 位地址(ADDR 接地 = 0x5A;ADDR 接高 = 0x5B) */
#define SYS_CCS811_ADDR_ADDR_GND   0x5AU
#define SYS_CCS811_ADDR_ADDR_HIGH  0x5BU

/* 本板默认地址: ADDR 接地, 唯一需要按实际接线修改的地方 */
#define SYS_CCS811_ADDR            SYS_CCS811_ADDR_ADDR_GND

/* 硬件产品 ID(0x20 寄存器读出的固定值) */
#define SYS_CCS811_HW_ID_VALUE     0x81U


/* 初始化时序 / 测量模式宏 */
/* 上电后第一次通信前的等待(ms)
 * 手册要求软复位(SW_RESET)之后 ≥20ms 才可再次通信;上电瞬间器件内部
 * 也在自复位,等这一段最省事,不等就发事务器件会 NACK */
#define SYS_CCS811_POWERON_DELAY_MS    20U

/* 软复位后等待(ms),手册要求 SW_RESET 后 ≥20ms 才可以再通信 */
#define SYS_CCS811_RESET_DELAY_MS      20U

/* 轮询 STATUS.APP_VALID(bit4) 变 1 的超时(ms)
 * APP_VALID = 0 表示片上应用固件还没加载好,此时手册禁止访问任何测量
 * 寄存器;超时返回 SYS_CCS811_ERR_APP_INVALID */
#define SYS_CCS811_APP_VALID_TIMEOUT_MS 1000U

/* 测量模式(0x01 MEAS_MODE)默认值,0x10 = 恒供电 + 每秒测一次
 * 可选值(bit3 = 中断使能,本模块默认不开) :
 *      0x00 = idle(空闲,不测量,进低功耗)
 *      0x10 = mode 1: 每秒测量一次,恒供电(最常用),默认
 *      0x20 = mode 2: 每 10 秒一次(低功耗)
 *      0x30 = mode 3: 每 60 秒一次(最低功耗)
 *      0x40 = mode 4: 每 250ms 一次(快速响应,功耗最高)
 *      以上任意值 | 0x08 = 允许 nINT 中断输出(本模块用轮询,不使能)
 * 模式决定多久能出一次新数据,也决定 DataReady 的周期判据 */
#define SYS_CCS811_MEAS_MODE_DEFAULT   0x10U

/* 数据就绪判据的兜底周期(ms),读不到 MEAS_MODE(总线异常)时用 */
#define SYS_CCS811_DRDY_FALLBACK_MS    1000U


/* 返回码(与 sys_i2c 错误码同风格) */
/* 成功 = 0;负数 = sys_i2c 的错误码(SYS_I2C_ERR_*,见 sys_i2c.h);
 * 正数 = 本模块自己的错误码(含义见下) */
#define SYS_CCS811_OK                 0U    /* 成功 */
#define SYS_CCS811_ERR_PARAM          1U    /* 入参非法(bus 越界 / 输出指针为空) */
#define SYS_CCS811_ERR_HW_ID          2U    /* HW_ID(0x20) 读出的不是 0x81,
                                             * 即接的不是 CCS811 或 ADDR 接反 */
#define SYS_CCS811_ERR_APP_INVALID    3U    /* 等 STATUS.APP_VALID 超时,
                                             * 片上应用固件没加载好,
                                             * 此时不得访问任何测量寄存器 */
#define SYS_CCS811_ERR_DEV_ERROR      4U    /* STATUS.ERROR(bit0) = 1,
                                             * 用 SYS_CCS811_GetErrorID()
                                             * 读 0xE0 看具体错误码 */
#define SYS_CCS811_ERR_WRITE_CMD      5U    /* 发单字节写命令(后无数据)失败,
                                             * 由 sys_i2c 的写原语返回 */


/* 数据结构 */
/* 一次空气质量读数 */
typedef struct {
    uint16_t eco2_ppm;   /* 等效二氧化碳,单位 ppm(手册有效范围 400~8192) */
    uint16_t tvoc_ppb;   /* 等效总挥发性有机物,单位 ppb(手册有效范围 0~1187) */
    uint8_t  status;     /* 读取时的 STATUS(0x00) 原始值(排错用) */
    uint8_t  error_id;   /* 读取时的 ERROR_ID(0xE0) 原始值(排错用,0 = 无错) */
    uint8_t  valid;      /* 本次读数是否有意义: 1 = 有意义,0 = 无效
                          * (APP_VALID = 0 或 STATUS.ERROR = 1 时为 0;
                          *  此时 eco2_ppm/tvoc_ppb 仍被填写,但不要采信) */
} SYS_CCS811_Data_t;


/* 基础功能 */
/* 初始化(用默认地址)
 * 返回 : SYS_CCS811_OK(0) = 成功;
 *        正数 = 本模块错误码(SYS_CCS811_ERR_*);
 *        负数 = I2C 错误码(SYS_I2C_ERR_*)
 * 经 sys_i2c 按手册顺序执行 : 等 20ms → 读 0x20 自检(= 0x81)
 *        → 0xFF 写 4 字节软复位 → 等 20ms → 轮询 0x00 等 APP_VALID(bit4)
 *        → 查 ERROR(bit0) → 0xF4 只发命令(APP_START)→ 写 0x01 设测量模式 */
uint8_t SYS_CCS811_Init(SysI2cId_t bus);

/* 初始化(指定 7 位地址)
 * 参数 : bus   : 总线(只调用 I2C_Init 完成该总线的初始化)
 *        addr7 : 器件 7 位地址(0x5A 或 0x5B)
 * 返回 : 同 SYS_CCS811_Init
 * 本函数把 addr7 记在这条总线上,后续 Read / DataReady / GetStatus /
 * GetErrorID / GetHWID / SetEnvData / GetBaseline / SetBaseline 都自动用
 * 同一地址(按总线下标各存一份) */
uint8_t SYS_CCS811_InitAddr(SysI2cId_t bus, uint8_t addr7);

/* 读硬件产品 ID(0x20)
 * 返回 : 0x81 = CCS811;负数 = I2C 错误码
 * 用于确认接的是不是 CCS811、ADDR 有没有接反 */
int SYS_CCS811_GetHWID(SysI2cId_t bus);

/* 读状态寄存器(0x00)
 * 返回 : 原始字节(bit4 = APP_VALID,bit3 = 部分固件当 DRDY,bit0 = ERROR);
 *        负数 = I2C 错误码 */
int SYS_CCS811_GetStatus(SysI2cId_t bus);

/* 读错误码寄存器(0xE0)
 * 返回 : 原始字节(0 = 无错误);负数 = I2C 错误码
 * 只在 STATUS.ERROR(bit0) = 1 时读才有意义,手册列出的常见错误:
 *        0x01 写非法寄存器 / 0x02 读非法寄存器 / 0x04 测量模式非法 /
 *        0x08 最大电阻检测错误 / 0x10 HEATER 故障 / 0x20 HEATER 供电故障 /
 *        0x40 未收到有效温湿度补偿数据 */
int SYS_CCS811_GetErrorID(SysI2cId_t bus);

/* 读一次算法结果(eCO2 + TVOC)
 * 参数 : out : 输出(eco2_ppm / tvoc_ppb / status / error_id / valid)
 * 返回 : SYS_CCS811_OK(0) = 事务成功;正数 = 本模块错误码;负数 = I2C 错误码
 * 本函数只把 4 字节读回来并解析;是否到了该读的时刻要先用
 * SYS_CCS811_DataReady() 判断,否则会读到重复的旧值;读成功后内部
 * 刷新上次读取时间,供 DataReady 使用 */
int SYS_CCS811_Read(SysI2cId_t bus, SYS_CCS811_Data_t *out);


/* 扩展功能 */
/* 数据是否该读了(非阻塞,不访问器件数据寄存器)
 * 返回 : 1 = 距上次成功读取已经过一个测量周期,可以读新数据;0 = 还没到
 * CCS811 没有可靠的数据就绪位,部分固件版本把 STATUS 的 bit3 当 DRDY,
 * 并非所有版本都支持;本模块改用时间判据: 记录每条总线上一次成功读取
 * 的时刻,与当前测量模式对应的周期比较(mode1 = 1s / mode2 = 10s /
 * mode3 = 60s / mode4 = 250ms);未初始化或读不到模式时按
 * SYS_CCS811_DRDY_FALLBACK_MS 兜底,实现细节与 static 表见 .c */
uint8_t SYS_CCS811_DataReady(SysI2cId_t bus);

/* 写外部温湿度补偿(0x05 ENV_DATA)
 * 参数 : temp_c  : 环境温度,℃(有符号,范围约 -25 ~ 50)
 *        humi_rh : 环境相对湿度,%RH(范围约 0 ~ 100)
 * 返回 : SYS_CCS811_OK(0) = 成功;正数 = 本模块错误码;负数 = I2C 错误码
 * 手册建议提供,器件用它修正气敏算法;编码方式: 湿度 = round(RH × 512),
 * 温度 = round((T + 25) × 512),各占 2 字节大端;范围外的入参钳到边界
 * 后照常写入 */
int SYS_CCS811_SetEnvData(SysI2cId_t bus, float temp_c, float humi_rh);

/* 读基线(0x11 BASELINE),用于断电保存、下次上电写回
 * 参数 : out : 输出基线值(2 字节大端解析)
 * 返回 : SYS_CCS811_OK(0) = 成功;正数 = 本模块错误码;负数 = I2C 错误码
 * 只在器件已在干净空气里稳定运行足够久时读才有意义;保存位置
 * (Flash / EEPROM)由调用方决定,本模块不 include 存储模块 */
int SYS_CCS811_GetBaseline(SysI2cId_t bus, uint16_t *out);

/* 写基线(0x11 BASELINE),上电初始化完成后立刻写回上次保存的值
 * 返回 : SYS_CCS811_OK(0) = 成功;正数 = 本模块错误码;负数 = I2C 错误码
 * 写回后器件接着上次状态继续,不必重新老化 48 小时;若存基线时器件
 * 未稳定,写回的基线会让读数从一开始就不准,只能存稳定后读到的基线 */
int SYS_CCS811_SetBaseline(SysI2cId_t bus, uint16_t baseline);

#endif /* __FWLIB_SYS_CCS811_H */
