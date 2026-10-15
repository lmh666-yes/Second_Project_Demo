/* sys_bh1750.c — BH1750FVI 数字环境光强度传感器驱动（实现）
 * 命令式器件：无寄存器号，写入字节即命令；0x00 为掉电命令。
 * 读用 SYS_I2C_ReadRaw()，发命令用 SYS_I2C_WriteCmd()。详见 sys_bh1750.h。 */

#include "sys_bh1750.h"
#include "delay.h"

/* 每条总线的器件地址，合法值 0x23/0x5C；0 = 未 Init */
static uint8_t s_bh1750_addr[SYS_I2C_COUNT];

/* ---------------------------------------------------------------------------
 *  内部小工具
 * ------------------------------------------------------------------------- */

static uint8_t bh1750_bus_ok(SysI2cId_t bus)
{
    return (uint8_t)(((uint32_t)bus < (uint32_t)SYS_I2C_COUNT) ? 1U : 0U);
}

static uint8_t bh1750_addr_ok(uint8_t addr7)
{
    return (uint8_t)(((addr7 == SYS_BH1750_ADDR_LOW) ||
                      (addr7 == SYS_BH1750_ADDR_HIGH)) ? 1U : 0U);
}

/* 取已记录地址；未 Init 返回 0 */
static uint8_t bh1750_get_addr(SysI2cId_t bus, uint8_t *out)
{
    if (bh1750_bus_ok(bus) == 0U) return 0U;
    if (bh1750_addr_ok(s_bh1750_addr[bus]) == 0U) return 0U;

    *out = s_bh1750_addr[bus];
    return 1U;
}

/* ---------------------------------------------------------------------------
 *  初始化
 * ------------------------------------------------------------------------- */

uint8_t SYS_BH1750_InitAddr(SysI2cId_t bus, uint8_t addr7)
{
    if (bh1750_bus_ok(bus) == 0U) return SYS_BH1750_ERR_PARAM;
    if (bh1750_addr_ok(addr7) == 0U) return SYS_BH1750_ERR_PARAM;

    /* 先记地址后探测；探测失败也保留 */
    s_bh1750_addr[bus] = addr7;

    if (SYS_I2C_IsDeviceReady(bus, addr7) != SYS_I2C_OK) {
        return SYS_BH1750_ERR_NO_DEVICE;
    }

    /* 上电：掉电时测量命令被忽略 */
    if (SYS_I2C_WriteCmd(bus, addr7, SYS_BH1750_CMD_POWER_ON) != SYS_I2C_OK) {
        return SYS_BH1750_ERR_NO_DEVICE;
    }
    delay_ms(1U);                       /* 手册：上电后 1ms 才可发下一条命令 */

    /* 连续 H 分辨率测量；返回时首次转换已完成 */
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

/* ---------------------------------------------------------------------------
 *  命令与读
 * ------------------------------------------------------------------------- */

int SYS_BH1750_SendCmd(SysI2cId_t bus, uint8_t cmd)
{
    uint8_t addr;

    if (bh1750_get_addr(bus, &addr) == 0U) return SYS_BH1750_ERR_PARAM;

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

    /* 不发寄存器号，直接读 2 字节（高位在前）；0x00 为掉电命令，勿用 ReadBytes */
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

    /* 0xFFFF 为过饱和，raw/1.2 = 54612 lx 是下限非真值 */
    if (raw == 0xFFFFU) return SYS_BH1750_ERR_SATURATED;

    /* 手册：lx = raw / 1.2 */
    *lux = (float)raw / 1.2f;
    return SYS_BH1750_OK;
}

int SYS_BH1750_ReadLuxOnce(SysI2cId_t bus, float *lux)
{
    uint8_t addr;
    int err;

    if (lux == 0) return SYS_BH1750_ERR_PARAM;
    if (bh1750_get_addr(bus, &addr) == 0U) return SYS_BH1750_ERR_PARAM;

    /* 上电；单次模式测完自动掉电 */
    err = SYS_I2C_WriteCmd(bus, addr, SYS_BH1750_CMD_POWER_ON);
    if (err != SYS_I2C_OK) return err;

    err = SYS_I2C_WriteCmd(bus, addr, SYS_BH1750_CMD_ONE_H_RES);
    if (err != SYS_I2C_OK) return err;

    /* 等待转换完成，阻塞 180ms */
    delay_ms(SYS_BH1750_CONV_MS_H);

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

    /* 手册：分两条命令写，高 3 位 0100_0MT7MT6MT5，低 5 位 011_MT4..MT0 */
    err = SYS_I2C_WriteCmd(bus, addr, (uint8_t)(0x40U | (uint8_t)(mt >> 5)));
    if (err != SYS_I2C_OK) return err;

    err = SYS_I2C_WriteCmd(bus, addr, (uint8_t)(0x60U | (uint8_t)(mt & 0x1FU)));
    if (err != SYS_I2C_OK) return err;

    return SYS_BH1750_OK;
}
