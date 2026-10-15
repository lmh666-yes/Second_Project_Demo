/*
 *  sys_bh1750.h : BH1750FVI 数字环境光强度传感器模块
 *  器件 : BH1750FVI（ROHM），16 位串行输出，I2C 接口
 *  量程 : 1 lx ~ 65535 lx；H 分辨率模式下换算系数 1.2，
 *         65535 / 1.2 约 54612 lx 为读数上限
 *  供电 : 2.4V ~ 3.6V。GY-302 模块板载 3.3V LDO 与电平转换，接 5V 可用；
 *         裸芯片接 5V 会烧。
 *  地址 : 7 位，由 ADDR 脚电平决定，ADDR 接 GND 为 0x23，接 VCC 为 0x5C；
 *         本库其余器件地址为 0x50(24C02) / 0x68(MPU6050) / 0x38(AHT10) /
 *         0x44(SHT30) / 0x3C(SSD1306)，与 0x23 / 0x5C 均不冲突，可挂同一条 I2C。
 *  接线 : VCC 接 3.3V，GND 接 GND，SCL / SDA 接对应 I2C 的 PBx
 *  依赖 : sys_i2c.c 的 SYS_I2C_IsDeviceReady / WriteCmd / ReadRaw，delay.c 的 delay_ms
 *
 *  1) 器件是命令式，没有寄存器号：写进去的字节本身即命令。
 *     0x01 上电，0x00 掉电，0x07 复位，0x10 连续 H 分辨率（1 lx，约 120ms），
 *     0x20 单次 H 分辨率（测完自动回到掉电状态）。命令用 SYS_I2C_WriteCmd() 发，
 *     不能用 SYS_I2C_WriteBytes(bus, addr, cmd, NULL, 0)：该函数在 buf 为 0 或
 *     len 为 0 时直接返回 SYS_I2C_ERR_PARAM，实际什么都没发。
 *  2) 读数据也不能先发寄存器号，必须用 SYS_I2C_ReadRaw()。
 *     SYS_I2C_ReadBytes(..., 0x00, ...) 会先发一个 0x00，而 0x00 对 BH1750 是
 *     掉电命令，器件被关掉后返回旧值或全 0。
 *
 *  时序约束：测量时间由 MTreg 决定，出厂值 69 时 H 分辨率典型 120ms、最长约
 *  180ms，本驱动按最长值 SYS_BH1750_CONV_MS_H(180) 等待。MTreg 有效范围
 *  31 ~ 254；值大则积分时间长、更灵敏但更慢且更易饱和，值小则更快但暗处
 *  读数变粗。改完 MTreg 必须按新的测量时间等待再读，毫秒数查器件手册的
 *  测量时间表。ReadLuxOnce 每次调用阻塞约 180ms，不适合放进高频任务。
 *
 *  过饱和：读回 0xFFFF 表示光太强，读数无意义，ReadLux 返回
 *  SYS_BH1750_ERR_SATURATED。要测强光可换 L 分辨率模式（0x13，量程大 4 倍、
 *  分辨率 4 lx），或用 SYS_BH1750_SetMeasTime 缩短积分时间。
 *
 *  初始化顺序：先调用 SYS_I2C_Init()，再调用 SYS_BH1750_Init()。
 */

#ifndef __FWLIB_SYS_BH1750_H
#define __FWLIB_SYS_BH1750_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* ------------------------- 器件地址（7 位） ------------------------- */
#define SYS_BH1750_ADDR_LOW       0x23U   /* ADDR 接 GND（模块默认） */
#define SYS_BH1750_ADDR_HIGH      0x5CU   /* ADDR 接 VCC */
#define SYS_BH1750_ADDR_DEFAULT   SYS_BH1750_ADDR_LOW

/* ------------------------- 命令字（写进去就是命令） ------------------------- */
#define SYS_BH1750_CMD_POWER_DOWN  0x00U  /* 掉电（等待电流最小） */
#define SYS_BH1750_CMD_POWER_ON    0x01U  /* 上电（测量前必须先上电） */
#define SYS_BH1750_CMD_RESET       0x07U  /* 复位数据寄存器（上电状态不变） */

#define SYS_BH1750_CMD_CONT_H_RES  0x10U  /* 连续 H 分辨率，1 lx，约 120ms */
#define SYS_BH1750_CMD_CONT_H_RES2 0x11U  /* 连续 H 分辨率2，0.5 lx，约 120ms */
#define SYS_BH1750_CMD_CONT_L_RES  0x13U  /* 连续 L 分辨率，4 lx，约 16ms */

#define SYS_BH1750_CMD_ONE_H_RES   0x20U  /* 单次 H 分辨率，测完自动掉电 */
#define SYS_BH1750_CMD_ONE_H_RES2  0x21U  /* 单次 H 分辨率2 */
#define SYS_BH1750_CMD_ONE_L_RES   0x23U  /* 单次 L 分辨率 */

/* ------------------------- 时间参数 ------------------------- */
/* H 分辨率一次转换的等待毫秒数。手册：默认 MTreg 下典型 120ms、最长 180ms，
 * 本驱动取 180，按最长值等待，慢 60ms 换读数有效。 */
#define SYS_BH1750_CONV_MS_H       180U
/* L 分辨率一次转换（毫秒），手册典型 16ms，取 24 留余量。 */
#define SYS_BH1750_CONV_MS_L        24U

/* ------------------------- MTreg（measurement time） ------------------------- */
#define SYS_BH1750_MTREG_DEFAULT    69U   /* 出厂值 */
#define SYS_BH1750_MTREG_MIN        31U   /* 手册下限 */
#define SYS_BH1750_MTREG_MAX       254U   /* 手册上限 */

/* ------------------------- 返回码 ------------------------- */
/* 约定：0 = 成功；> 0 = 本驱动的语义码（见下）；< 0 = I2C 总线错误（SYS_I2C_ERR_*），
 *       总线错误交给 SYS_I2C_ErrStr() 转中文说明。 */
#define SYS_BH1750_OK              0     /* 成功 */
#define SYS_BH1750_ERR_PARAM       1     /* 参数非法 / 该总线还没 Init / 地址不是 0x23 或 0x5C */
#define SYS_BH1750_ERR_NO_DEVICE   2     /* 地址无应答：没接、没供电、ADDR 脚选错、总线被拉死 */
#define SYS_BH1750_ERR_SATURATED   3     /* 过饱和（读回 0xFFFF），光太强，读数无意义 */

/* 接口 */

/* 初始化（用默认地址 0x23）
 * 参数 : bus,总线编号 SYS_I2C_1 / SYS_I2C_2 / SYS_I2C_3
 * 返回 : SYS_BH1750_OK(0)         成功，器件已上电并进入连续 H 分辨率测量
 *        SYS_BH1750_ERR_PARAM(1)  bus 越界
 *        SYS_BH1750_ERR_NO_DEVICE(2) 0x23 无应答
 * 说明 : 依次探测器件、发 0x01 上电、发 0x10 连续 H 分辨率，
 *        并延时 SYS_BH1750_CONV_MS_H 等第一次转换完成，返回时读数已有效。
 *        探测失败时仍记录地址，后续可用 SendCmd 重试。
 * 注意 : 必须在 SYS_I2C_Init() 之后调用。 */
uint8_t SYS_BH1750_Init(SysI2cId_t bus);

/* 初始化（自定义地址，ADDR 接 VCC 时用 0x5C）
 * 参数 : addr7,SYS_BH1750_ADDR_LOW(0x23) 或 SYS_BH1750_ADDR_HIGH(0x5C)
 * 返回 : 同 SYS_BH1750_Init；地址不是上面两个之一时返回 SYS_BH1750_ERR_PARAM */
uint8_t SYS_BH1750_InitAddr(SysI2cId_t bus, uint8_t addr7);

/* 取当前记录的总线地址（0 表示该总线还没 Init） */
uint8_t SYS_BH1750_GetAddr(SysI2cId_t bus);

/* 直接发一个命令字（换测量模式、复位、调 MTreg 等）
 * 参数 : cmd,SYS_BH1750_CMD_xxx，或 (0x40|(mt>>5)) / (0x60|(mt&0x1F)) 两个 MTreg 命令
 * 返回 : 0 成功；SYS_BH1750_ERR_PARAM(1) 还没 Init；< 0 为 SYS_I2C_ERR_*
 * 注意 : 换测量模式后需自行延时（H 约 180ms、L 约 24ms）再读。 */
int SYS_BH1750_SendCmd(SysI2cId_t bus, uint8_t cmd);

/* 掉电（省电；下次读之前要重新上电或选一个测量模式）
 * 返回 : 0 成功 / 1 还没 Init / < 0 为 SYS_I2C_ERR_* */
int SYS_BH1750_PowerDown(SysI2cId_t bus);

/* 读原始 16 位光强值（不过饱和时 raw/1.2 = lx）
 * 参数 : raw,输出，高字节在前
 * 返回 : SYS_BH1750_OK(0) / SYS_BH1750_ERR_PARAM(1) / < 0 为 SYS_I2C_ERR_*
 * 说明 : 用 SYS_I2C_ReadRaw 读，不给器件发寄存器号。
 *        读到 0xFFFF 时本函数仍返回 OK（原始值就是 0xFFFF），
 *        判饱和用 SYS_BH1750_ReadLux 或自行比较 0xFFFF。 */
int SYS_BH1750_ReadRaw(SysI2cId_t bus, uint16_t *raw);

/* 读光照强度（lx）
 * 参数 : lux,输出，单位 lx，可带小数
 * 返回 : SYS_BH1750_OK(0)              成功
 *        SYS_BH1750_ERR_SATURATED(3)   过饱和，*lux 未被修改
 *        SYS_BH1750_ERR_PARAM(1)       参数非法 / 还没 Init / lux 为空指针
 *        < 0                           SYS_I2C_ERR_* 总线错误
 * 说明 : 连续测量模式下调用节奏就是采样率，器件一直在后台转换，
 *        读到的是最近一次转换结果，最长滞后约 180ms。 */
int SYS_BH1750_ReadLux(SysI2cId_t bus, float *lux);

/* 单次测量并读回（测完器件自动回到掉电状态，最省电）
 * 参数/返回 : 同 SYS_BH1750_ReadLux
 * 说明 : 内部序列为上电、0x20 单次 H 分辨率、延时 SYS_BH1750_CONV_MS_H、读。
 *        每次调用阻塞约 180ms，不适合高频任务，适合每秒采一次这类场合。
 *        不改变器件长期状态，不会把连续模式留在后台。 */
int SYS_BH1750_ReadLuxOnce(SysI2cId_t bus, float *lux);

/* 设置 MTreg（measurement time，即积分时间，一般不动）
 * 参数 : mt,31 ~ 254，出厂 69
 * 返回 : 0 成功 / SYS_BH1750_ERR_PARAM(1) mt 越界或还没 Init / < 0 为 SYS_I2C_ERR_*
 * 说明 : 器件要求分两条命令发（高 3 位 0x40|(mt>>5)，低 5 位 0x60|(mt&0x1F)），
 *        本函数一次发完。调完后必须按新的测量时间延时再读，毫秒数查手册测量时间表。
 *        值大则更灵敏更慢更易饱和，值小则更快更粗更抗强光。 */
int SYS_BH1750_SetMeasTime(SysI2cId_t bus, uint8_t mt);

#endif /* __FWLIB_SYS_BH1750_H */
