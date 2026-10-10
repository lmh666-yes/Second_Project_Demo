/* ============================================================================
 *  sys_bh1750.h — BH1750FVI 数字环境光强度传感器驱动
 *  器件 : BH1750FVI（ROHM），16 位串行输出，I2C 接口
 *  量程 : H 分辨率 1 lx，raw / 1.2 = lx，65535 / 1.2 ≈ 54612 lx
 *  供电 : 2.4V ~ 3.6V。GY-302 模块板载 3.3V LDO 与电平转换，可接 5V；裸芯片接 5V 会损坏。
 *
 *  地址 : 7 位地址由 ADDR 脚决定，接 GND 为 0x23，接 VCC 为 0x5C；同总线其它器件
 *         0x50(24C02)/0x68(MPU6050)/0x38(AHT10)/0x44(SHT30)/0x3C(SSD1306) 与 0x23/0x5C 不冲突。
 *  接线 : VCC → 3.3V    GND → GND    SCL/SDA → 对应 I2C 的 PBx
 *  总线 : I2C，100kHz ~ 400kHz。
 *  依赖 : sys_i2c.c（SYS_I2C_IsDeviceReady / WriteCmd / ReadRaw）、delay.c（delay_ms）
 *
 *  协议 : 命令式器件，无寄存器号，写入的字节即命令，用 WriteCmd 发送。
 *         0x01 = 上电   0x00 = 掉电   0x07 = 复位
 *         0x10 = 连续 H 分辨率（1 lx，约 120ms）   0x20 = 单次 H 分辨率（测完自动掉电）
 *         SYS_I2C_WriteBytes(bus, addr, cmd, NULL, 0) 因 buf == 0 || len == 0 返回
 *         SYS_I2C_ERR_PARAM 且不发送数据；读数用 SYS_I2C_ReadRaw()，不发送寄存器号，
 *         SYS_I2C_ReadBytes(..., 0x00, ...) 先发送 0x00（即掉电命令），读数保持旧值或 0。
 *
 *  过饱和 : 读回 0xFFFF 时 ReadLux 返回 SYS_BH1750_ERR_SATURATED，读数无效；强光下可换
 *         L 分辨率（0x13，量程大 4 倍，分辨率 4 lx）或调小 MTreg 缩短积分时间。
 *
 *  MTreg : 出厂 69，有效范围 31 ~ 254。默认 MTreg 下 H 分辨率典型 120ms、最长 180ms，
 *         驱动按 SYS_BH1750_CONV_MS_H(180) 等待；改 MTreg 后按手册测量时间表延时再读。
 * ============================================================================ */

#ifndef __FWLIB_SYS_BH1750_H
#define __FWLIB_SYS_BH1750_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* ---------- 器件地址（7 位） ---------- */
#define SYS_BH1750_ADDR_LOW       0x23U   /* ADDR 接 GND（模块默认） */
#define SYS_BH1750_ADDR_HIGH      0x5CU   /* ADDR 接 VCC */
#define SYS_BH1750_ADDR_DEFAULT   SYS_BH1750_ADDR_LOW

/* ---------- 命令字（写进去就是命令） ---------- */
#define SYS_BH1750_CMD_POWER_DOWN  0x00U  /* 掉电（等待电流最小） */
#define SYS_BH1750_CMD_POWER_ON    0x01U  /* 上电（测量前必须先上电） */
#define SYS_BH1750_CMD_RESET       0x07U  /* 复位数据寄存器（上电状态不变） */

#define SYS_BH1750_CMD_CONT_H_RES  0x10U  /* 连续 H 分辨率，1 lx，约 120ms */
#define SYS_BH1750_CMD_CONT_H_RES2 0x11U  /* 连续 H 分辨率2，0.5 lx，约 120ms */
#define SYS_BH1750_CMD_CONT_L_RES  0x13U  /* 连续 L 分辨率，4 lx，约 16ms */

#define SYS_BH1750_CMD_ONE_H_RES   0x20U  /* 单次 H 分辨率，测完自动掉电 */
#define SYS_BH1750_CMD_ONE_H_RES2  0x21U  /* 单次 H 分辨率2 */
#define SYS_BH1750_CMD_ONE_L_RES   0x23U  /* 单次 L 分辨率 */

/* ---------- 时间参数 ---------- */
/* H 分辨率一次转换的等待时间（毫秒）。手册默认 MTreg：典型 120ms，最长 180ms；
 * 驱动取 180。 */
#define SYS_BH1750_CONV_MS_H       180U
/* L 分辨率一次转换（毫秒），手册典型 16ms，取 24 留余量。 */
#define SYS_BH1750_CONV_MS_L        24U

/* ---------- MTreg（measurement time） ---------- */
#define SYS_BH1750_MTREG_DEFAULT    69U   /* 出厂值 */
#define SYS_BH1750_MTREG_MIN        31U   /* 手册下限 */
#define SYS_BH1750_MTREG_MAX       254U   /* 手册上限 */

/* ---------- 返回码 ---------- */
/* 约定：0 = 成功；> 0 = 本驱动语义码；< 0 = SYS_I2C_ERR_*，可交 SYS_I2C_ErrStr() 取说明。 */
#define SYS_BH1750_OK              0     /* 成功 */
#define SYS_BH1750_ERR_PARAM       1     /* 参数非法 / 该总线还没 Init / 地址不是 0x23 或 0x5C */
#define SYS_BH1750_ERR_NO_DEVICE   2     /* 地址无应答：没接、没供电、ADDR 脚选错、总线被拉死 */
#define SYS_BH1750_ERR_SATURATED   3     /* 过饱和（读回 0xFFFF），光太强，读数无意义 */

/* ---------- 接口 ---------- */

/* 初始化（默认地址 0x23）
 * 参数 : bus 取 SYS_I2C_1 / SYS_I2C_2 / SYS_I2C_3
 * 返回 : 0 成功，已上电并进入连续 H 分辨率测量；1 bus 越界；2 0x23 无应答
 * 说明 : 探测器件、0x01 上电、0x10 连续 H 分辨率、延时 SYS_BH1750_CONV_MS_H；返回时首次
 *        转换已完成。须先调 SYS_I2C_Init()。探测失败也记录地址，可用 SendCmd 重试。 */
uint8_t SYS_BH1750_Init(SysI2cId_t bus);

/* 初始化（自定义地址，ADDR 接 VCC 时用 0x5C）
 * 参数 : addr7 取 SYS_BH1750_ADDR_LOW(0x23) 或 SYS_BH1750_ADDR_HIGH(0x5C)
 * 返回 : 同 SYS_BH1750_Init；其余取值返回 SYS_BH1750_ERR_PARAM */
uint8_t SYS_BH1750_InitAddr(SysI2cId_t bus, uint8_t addr7);

/* 取当前记录的总线地址（0 表示该总线还没 Init） */
uint8_t SYS_BH1750_GetAddr(SysI2cId_t bus);

/* 直接发命令字（换测量模式、复位、调 MTreg）
 * 参数 : cmd 取 SYS_BH1750_CMD_xxx，或 MTreg 命令 (0x40|(mt>>5)) / (0x60|(mt&0x1F))
 * 返回 : 0 成功；1 还没 Init；< 0 为 SYS_I2C_ERR_*
 * 注意 : 换测量模式后须自行延时（H 约 180ms、L 约 24ms）再读。 */
int SYS_BH1750_SendCmd(SysI2cId_t bus, uint8_t cmd);

/* 掉电（省电；下次读之前要重新上电或选一个测量模式）
 * 返回 : 0 成功 / 1 还没 Init / < 0 为 SYS_I2C_ERR_* */
int SYS_BH1750_PowerDown(SysI2cId_t bus);

/* 读原始 16 位光强值（不过饱和时 raw/1.2 = lx）
 * 参数 : raw 输出，高字节在前
 * 返回 : 0 成功 / 1 参数非法 / < 0 为 SYS_I2C_ERR_*
 * 说明 : 用 SYS_I2C_ReadRaw 读，不发送寄存器号；读到 0xFFFF 仍返回 0，饱和判断用
 *        SYS_BH1750_ReadLux。 */
int SYS_BH1750_ReadRaw(SysI2cId_t bus, uint16_t *raw);

/* 读光照强度（lx）
 * 参数 : lux 输出，单位 lx，可带小数
 * 返回 : 0 成功；1 参数非法 / 还没 Init / lux 为空指针；3 过饱和，*lux 未被修改；
 *        < 0 为 SYS_I2C_ERR_* 总线错误
 * 说明 : 连续测量模式下器件后台转换，读到最近一次结果（最长滞后约 180ms），调用节奏即
 *        采样率。 */
int SYS_BH1750_ReadLux(SysI2cId_t bus, float *lux);

/* 单次测量并读回（测完器件自动回到掉电状态）
 * 参数/返回 : 同 SYS_BH1750_ReadLux
 * 说明 : 序列为 0x01 上电、0x20 单次 H 分辨率、延时 SYS_BH1750_CONV_MS_H、读；每次调用
 *        阻塞约 180ms，不适合高频任务；不改器件长期状态。 */
int SYS_BH1750_ReadLuxOnce(SysI2cId_t bus, float *lux);

/* 设置 MTreg（measurement time，积分时间）
 * 参数 : mt 取 31 ~ 254，出厂 69
 * 返回 : 0 成功；1 mt 越界或还没 Init；< 0 为 SYS_I2C_ERR_*
 * 说明 : 器件分两条命令发（高 3 位 0x40|(mt>>5)、低 5 位 0x60|(mt&0x1F)），本函数一次发完；
 *        之后按手册测量时间表延时再读。值大更灵敏更慢易饱和，值小更快更粗更抗强光。 */
int SYS_BH1750_SetMeasTime(SysI2cId_t bus, uint8_t mt);

#endif /* __FWLIB_SYS_BH1750_H */
