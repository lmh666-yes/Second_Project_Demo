#ifndef __FWLIB_SYS_CCS811_H
#define __FWLIB_SYS_CCS811_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* ================================================================
 *  sys_ccs811.h : 外接空气质量传感器 CCS811(eCO2 + TVOC)
 * ================================================================
 *  接线 : 外接模块挂 I2C1(PB8 = SCL / PB9 = SDA,与 24C02 同总线),
 *         7 位地址 0x5A(ADDR 接地)/ 0x5B(ADDR 接高),3.3V 供电,
 *         WAKE 引脚(若有)拉低才响应 I2C
 *  时序 : 等 20ms → 读 0x20 自检(0x81)→ 0xFF 写 4 字节软复位
 *         → 等 20ms → 轮询 0x00 等 APP_VALID(bit4)→ 查 ERROR(bit0)
 *         → 0xF4 只发命令(APP_START)→ 写 0x01 设测量模式 → 读 4 字节结果
 *  约束 : eCO2 / TVOC 为厂商算法给出的等效值,非 NDIR 实测,只作同一环境内
 *         的相对趋势判断;有效范围 eCO2 400~8192 ppm、TVOC 0~1187 ppb;
 *         MOX 气敏需老化,建议连续运行 48h 后再看绝对值;器件稳定后读
 *         0x11 基线并由调用方保存,下次上电初始化后写回;0x05 ENV_DATA 外部
 *         温湿度用于修正算法,手册建议提供;STATUS(0x00) 无跨版本可靠的
 *         DRDY 位,DataReady 用时间判据。调用示例见区块 5
 * ================================================================ */


/* ========== 区块 1：器件地址与 HW_ID 宏(换板子只改这里) ========== */
/* 7 位地址: ADDR 接地 = 0x5A,ADDR 接高 = 0x5B */
#define SYS_CCS811_ADDR_ADDR_GND   0x5AU
#define SYS_CCS811_ADDR_ADDR_HIGH  0x5BU

/* 本板默认地址: ADDR 接地 */
#define SYS_CCS811_ADDR            SYS_CCS811_ADDR_ADDR_GND

/* 硬件产品 ID(0x20 寄存器读出的固定值) */
#define SYS_CCS811_HW_ID_VALUE     0x81U


/* ========== 区块 2：初始化时序 / 测量模式宏 ========== */
/* 上电后首次通信前等待(ms): 手册要求 SW_RESET 后 ≥20ms 才可再通信;
 * 器件未就绪时事务被 NACK,表现为"器件不在线" */
#define SYS_CCS811_POWERON_DELAY_MS    20U

/* 软复位后等待(ms): 手册要求 SW_RESET 后 ≥20ms 才可再通信 */
#define SYS_CCS811_RESET_DELAY_MS      20U

/* 轮询 STATUS.APP_VALID(bit4) 变 1 的超时(ms): APP_VALID = 0 时片上应用
 * 固件未加载,手册禁止访问测量寄存器;超时返回 SYS_CCS811_ERR_APP_INVALID */
#define SYS_CCS811_APP_VALID_TIMEOUT_MS 1000U

/* 测量模式(0x01 MEAS_MODE)默认值: 0x10 = 恒供电 + 每秒测一次
 * 0x00 idle(空闲低功耗)/ 0x10 mode1 每秒 / 0x20 mode2 每 10 秒 /
 * 0x30 mode3 每 60 秒 / 0x40 mode4 每 250ms(功耗最高)
 * 以上任一值 | 0x08 = 允许 nINT 中断输出(本模块轮询,不使能)
 * 模式决定新数据周期,也决定 DataReady 的周期判据 */
#define SYS_CCS811_MEAS_MODE_DEFAULT   0x10U

/* 读不到 MEAS_MODE(总线异常)时数据就绪判据的兜底周期(ms) */
#define SYS_CCS811_DRDY_FALLBACK_MS    1000U


/* ========== 区块 3：返回码(与 sys_i2c 错误码同风格) ========== */
/* 0 = 成功;负数 = SYS_I2C_ERR_*(见 sys_i2c.h);正数 = 本模块错误码 */
#define SYS_CCS811_OK                 0U    /* 成功 */
#define SYS_CCS811_ERR_PARAM          1U    /* 入参非法(bus 越界 / 输出指针为空) */
#define SYS_CCS811_ERR_HW_ID          2U    /* HW_ID(0x20) 读出不是 0x81:
                                             * 接的不是 CCS811 或 ADDR 接反 */
#define SYS_CCS811_ERR_APP_INVALID    3U    /* 等 STATUS.APP_VALID 超时:
                                             * 片上固件未加载,不得访问测量寄存器 */
#define SYS_CCS811_ERR_DEV_ERROR      4U    /* STATUS.ERROR(bit0) = 1:
                                             * 器件报错,用 SYS_CCS811_GetErrorID()
                                             * 读 0xE0 看具体错误码 */
#define SYS_CCS811_ERR_WRITE_CMD      5U    /* sys_i2c 缺少 SYS_I2C_WriteCmd 原语:
                                             * 只发一字节、后无数据的写,
                                             * 需在 sys_i2c 里补上 */


/* ========== 区块 4：数据结构 ========== */
/* 一次空气质量读数 */
typedef struct {
    uint16_t eco2_ppm;   /* 等效二氧化碳,ppm(手册范围 400~8192) */
    uint16_t tvoc_ppb;   /* 等效总挥发性有机物,ppb(手册范围 0~1187) */
    uint8_t  status;     /* 读取时的 STATUS(0x00) 原始值(排错用) */
    uint8_t  error_id;   /* 读取时的 ERROR_ID(0xE0) 原始值(排错用,0 = 无错) */
    uint8_t  valid;      /* 1 = 本次读数有效;APP_VALID = 0 或 STATUS.ERROR = 1
                          * 时为 0,此时 eco2_ppm/tvoc_ppb 仍被填写但不采信 */
} SYS_CCS811_Data_t;


/* ========== 区块 5：基础功能 ========== */
/* 初始化(用区块 1 的默认地址)
 * 返回 : 0 = 成功;正数 = SYS_CCS811_ERR_*;负数 = SYS_I2C_ERR_*
 * 手册顺序(经 sys_i2c 调用,不直调标准库):
 *        等 20ms → 读 0x20 自检(= 0x81)→ 0xFF 写 4 字节软复位
 *        → 等 20ms → 轮询 0x00 等 APP_VALID(bit4)→ 查 ERROR(bit0)
 *        → 0xF4 只发命令(APP_START)→ 写 0x01 设测量模式
 * 示例 : if (SYS_CCS811_Init(SYS_I2C_1) != 0) { printf("CCS811 启动失败\r\n"); } */
uint8_t SYS_CCS811_Init(SysI2cId_t bus);

/* 初始化(指定 7 位地址)
 * 参数 : bus 总线(只调用 I2C_Init 完成该总线初始化);addr7 = 0x5A 或 0x5B
 * 返回 : 同 SYS_CCS811_Init
 * 说明 : addr7 按总线下标记录,后续 Read / DataReady / GetStatus / GetErrorID /
 *        GetHWID / SetEnvData / GetBaseline / SetBaseline 均用同一地址
 * 示例 : SYS_CCS811_InitAddr(SYS_I2C_1, SYS_CCS811_ADDR_ADDR_HIGH);  // ADDR 接高 */
uint8_t SYS_CCS811_InitAddr(SysI2cId_t bus, uint8_t addr7);

/* 读硬件产品 ID(0x20)
 * 返回 : 0x81 = CCS811;负数 = I2C 错误码
 * 说明 : 排查接的是不是 CCS811、ADDR 有没有接反的第一步 */
int SYS_CCS811_GetHWID(SysI2cId_t bus);

/* 读状态寄存器(0x00)
 * 返回 : 原始字节(bit4 = APP_VALID,bit3 = 部分固件当 DRDY,bit0 = ERROR);
 *        负数 = I2C 错误码 */
int SYS_CCS811_GetStatus(SysI2cId_t bus);

/* 读错误码寄存器(0xE0)
 * 返回 : 原始字节(0 = 无错误);负数 = I2C 错误码
 * 说明 : 仅当 STATUS.ERROR(bit0) = 1 时读才有意义;手册错误码 0x01 写非法
 *        寄存器 / 0x02 读非法寄存器 / 0x04 测量模式非法 / 0x08 最大电阻检测
 *        错误 / 0x10 HEATER 故障 / 0x20 HEATER 供电故障 / 0x40 温湿度补偿无效 */
int SYS_CCS811_GetErrorID(SysI2cId_t bus);

/* 读一次算法结果(eCO2 + TVOC)
 * 参数 : out = eco2_ppm / tvoc_ppb / status / error_id / valid
 * 返回 : 0 = 事务成功;正数 = 本模块错误码;负数 = I2C 错误码
 * 说明 : 只负责读回并解析 4 字节;调用前先用 SYS_CCS811_DataReady() 判断,
 *        否则会读到重复的旧值;读成功后刷新"上次读取时间"供 DataReady 使用 */
int SYS_CCS811_Read(SysI2cId_t bus, SYS_CCS811_Data_t *out);


/* ========== 区块 6：扩展功能 ========== */
/* 数据是否该读了(非阻塞,不访问器件数据寄存器)
 * 返回 : 1 = 距上次成功读取已过一个测量周期,可以读新数据;0 = 还没到
 * 说明 : STATUS(0x00) 无跨版本可靠的 DRDY 位(部分固件把 bit3 当 DRDY),
 *        故改用时间判据: 记录每条总线上次成功读取的时刻,与该测量模式的
 *        周期比较(mode1 1s / mode2 10s / mode3 60s / mode4 250ms);未初始化
 *        或读不到模式时按 SYS_CCS811_DRDY_FALLBACK_MS 兜底;表见 .c */
uint8_t SYS_CCS811_DataReady(SysI2cId_t bus);

/* 写外部温湿度补偿(0x05 ENV_DATA)
 * 参数 : temp_c = 环境温度 ℃(有符号,约 -25 ~ 50);humi_rh = 相对湿度 %RH(0 ~ 100)
 * 返回 : 0 = 成功;正数 = 本模块错误码;负数 = I2C 错误码
 * 编码 : 湿度 = round(RH × 512),温度 = round((T + 25) × 512),各 2 字节大端;
 *        越界入参钳到边界;手册建议提供,不填也能运行但精度差 */
int SYS_CCS811_SetEnvData(SysI2cId_t bus, float temp_c, float humi_rh);

/* 读基线(0x11 BASELINE),用于断电保存、下次上电写回
 * 参数 : out = 基线值(2 字节大端解析)
 * 返回 : 0 = 成功;正数 = 本模块错误码;负数 = I2C 错误码
 * 说明 : 仅在器件已在干净空气中稳定运行足够久时读才有意义;保存位置
 *        (Flash / EEPROM)由调用方决定,本模块不 include 存储模块 */
int SYS_CCS811_GetBaseline(SysI2cId_t bus, uint16_t *out);

/* 写基线(0x11 BASELINE),初始化完成后写回上次保存的值
 * 返回 : 0 = 成功;正数 = 本模块错误码;负数 = I2C 错误码
 * 说明 : 写回后不必重新老化 48 小时;基线取自未稳定的器件会使读数不准,
 *        写入时机由调用方掌握 */
int SYS_CCS811_SetBaseline(SysI2cId_t bus, uint16_t baseline);

#endif /* __FWLIB_SYS_CCS811_H */
