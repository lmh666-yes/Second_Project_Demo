/* BH1750FVI 数字环境光强度传感器驱动（实现）。器件为命令式：写入的字节即命令，没有寄存器号。
 * 读数据必须用 SYS_I2C_ReadRaw() 不发寄存器号：0x00 是掉电命令，误发后器件停止更新数据。
 * 发命令用 SYS_I2C_WriteCmd()；WriteBytes(..., NULL, 0) 判 buf==0||len==0 返回 SYS_I2C_ERR_PARAM。 */

#include "sys_bh1750.h"
#include "delay.h"

/* 每条总线记录的器件地址；0 = 该总线未 Init
 * 合法值只有 0x23 / 0x5C，故 0 可作未初始化标记 */
static uint8_t s_bh1750_addr[SYS_I2C_COUNT];

/* 内部小工具 */

/* 判断总线号是否合法 */
static uint8_t bh1750_bus_ok(SysI2cId_t bus)
{
    return (uint8_t)(((uint32_t)bus < (uint32_t)SYS_I2C_COUNT) ? 1U : 0U);
}

/* 判断七位地址是否为 BH1750 合法地址 */
static uint8_t bh1750_addr_ok(uint8_t addr7)
{
    return (uint8_t)(((addr7 == SYS_BH1750_ADDR_LOW) ||
                      (addr7 == SYS_BH1750_ADDR_HIGH)) ? 1U : 0U);
}

/* 取该总线已记录的地址；未 Init 则失败，不猜默认地址 */
static uint8_t bh1750_get_addr(SysI2cId_t bus, uint8_t *out)
{
    if (bh1750_bus_ok(bus) == 0U) return 0U;
    if (bh1750_addr_ok(s_bh1750_addr[bus]) == 0U) return 0U;   /* 未 Init */

    *out = s_bh1750_addr[bus];
    return 1U;
}

/* 初始化 */

uint8_t SYS_BH1750_InitAddr(SysI2cId_t bus, uint8_t addr7)
{
    if (bh1750_bus_ok(bus) == 0U) return SYS_BH1750_ERR_PARAM;
    if (bh1750_addr_ok(addr7) == 0U) return SYS_BH1750_ERR_PARAM;

    /* 先记地址再探测：探测失败也保留，便于调用方用 SendCmd 重试 */
    s_bh1750_addr[bus] = addr7;

    /* 探测器件是否应答；无应答常见原因：未供电、ADDR 脚电平错误、总线被拉死 */
    if (SYS_I2C_IsDeviceReady(bus, addr7) != SYS_I2C_OK) {
        return SYS_BH1750_ERR_NO_DEVICE;
    }

    /* 上电：掉电状态下测量命令会被忽略 */
    if (SYS_I2C_WriteCmd(bus, addr7, SYS_BH1750_CMD_POWER_ON) != SYS_I2C_OK) {
        return SYS_BH1750_ERR_NO_DEVICE;
    }
    delay_ms(1U);                       /* 手册：上电后等 1ms 才能发下一条命令 */

    /* 启动连续 H 分辨率测量并等首次转换完成，返回时读数有效 */
    if (SYS_I2C_WriteCmd(bus, addr7, SYS_BH1750_CMD_CONT_H_RES) != SYS_I2C_OK) {
        return SYS_BH1750_ERR_NO_DEVICE;
    }
    delay_ms(SYS_BH1750_CONV_MS_H);

    return SYS_BH1750_OK;
}

uint8_t SYS_BH1750_Init(SysI2cId_t bus)
{
    return SYS_BH1750_InitAddr(bus, SYS_BH1750_ADDR_DEFAULT);
}

uint8_t SYS_BH1750_GetAddr(SysI2cId_t bus)
{
    if (bh1750_bus_ok(bus) == 0U) return 0U;
    return s_bh1750_addr[bus];
}

/* 命令与读 */

int SYS_BH1750_SendCmd(SysI2cId_t bus, uint8_t cmd)
{
    uint8_t addr;

    if (bh1750_get_addr(bus, &addr) == 0U) return SYS_BH1750_ERR_PARAM;

    /* 只发一个字节，该字节对 BH1750 即命令本身 */
    return SYS_I2C_WriteCmd(bus, addr, cmd);
}

int SYS_BH1750_PowerDown(SysI2cId_t bus)
{
    return SYS_BH1750_SendCmd(bus, SYS_BH1750_CMD_POWER_DOWN);
}

int SYS_BH1750_ReadRaw(SysI2cId_t bus, uint16_t *raw)
{
    uint8_t addr;
    uint8_t b[2];
    int err;

    if (raw == 0) return SYS_BH1750_ERR_PARAM;
    if (bh1750_get_addr(bus, &addr) == 0U) return SYS_BH1750_ERR_PARAM;

    /* 直接读 2 字节，高字节在前；不发任何寄存器号
     * 换成 SYS_I2C_ReadBytes(bus, addr, 0x00, b, 2) 时，该 0x00 被当作掉电命令，器件不再更新数据 */
    err = SYS_I2C_ReadRaw(bus, addr, b, 2U);
    if (err != SYS_I2C_OK) return err;

    *raw = (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
    return SYS_BH1750_OK;
}

int SYS_BH1750_ReadLux(SysI2cId_t bus, float *lux)
{
    uint16_t raw;
    int err;

    if (lux == 0) return SYS_BH1750_ERR_PARAM;

    err = SYS_BH1750_ReadRaw(bus, &raw);
    if (err != SYS_BH1750_OK) return err;

    /* 0xFFFF = 过饱和：raw/1.2 = 54612 lx 只是下限，不是真值，返回错误码而不给假数 */
    if (raw == 0xFFFFU) return SYS_BH1750_ERR_SATURATED;

    /* 换算系数 1.2 出自数据手册：lx = raw / 1.2 */
    *lux = (float)raw / 1.2f;
    return SYS_BH1750_OK;
}

int SYS_BH1750_ReadLuxOnce(SysI2cId_t bus, float *lux)
{
    uint8_t addr;
    int err;

    if (lux == 0) return SYS_BH1750_ERR_PARAM;
    if (bh1750_get_addr(bus, &addr) == 0U) return SYS_BH1750_ERR_PARAM;

    /* 上电：单次模式测完自动回到掉电，每次都要先上电 */
    err = SYS_I2C_WriteCmd(bus, addr, SYS_BH1750_CMD_POWER_ON);
    if (err != SYS_I2C_OK) return err;

    /* 触发一次单次 H 分辨率测量 */
    err = SYS_I2C_WriteCmd(bus, addr, SYS_BH1750_CMD_ONE_H_RES);
    if (err != SYS_I2C_OK) return err;

    /* 等本次转换完成，阻塞约 180ms，不宜放入高频任务 */
    delay_ms(SYS_BH1750_CONV_MS_H);

    /* 直接读，读完器件已在掉电状态，不需再发 PowerDown */
    return SYS_BH1750_ReadLux(bus, lux);
}

int SYS_BH1750_SetMeasTime(SysI2cId_t bus, uint8_t mt)
{
    uint8_t addr;
    int err;

    if (mt < SYS_BH1750_MTREG_MIN || mt > SYS_BH1750_MTREG_MAX) {
        return SYS_BH1750_ERR_PARAM;
    }
    if (bh1750_get_addr(bus, &addr) == 0U) return SYS_BH1750_ERR_PARAM;

    /* 手册要求分两条命令写：高 3 位 0100_0MT7MT6MT5，低 5 位 011_MT4..MT0 */
    err = SYS_I2C_WriteCmd(bus, addr, (uint8_t)(0x40U | (uint8_t)(mt >> 5)));
    if (err != SYS_I2C_OK) return err;

    err = SYS_I2C_WriteCmd(bus, addr, (uint8_t)(0x60U | (uint8_t)(mt & 0x1FU)));
    if (err != SYS_I2C_OK) return err;

    return SYS_BH1750_OK;
}
