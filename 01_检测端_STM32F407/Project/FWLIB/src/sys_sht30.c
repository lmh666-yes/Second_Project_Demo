#include "sys_sht30.h"
#include "delay.h"
/* 配套指引见同名 .h;本文件为实现层 */

/* 每条总线记录的器件地址，0 表示该总线还没 Init */
static uint8_t s_addr[SYS_I2C_COUNT];

/* 参数自检：总线号在范围内、地址是 0x44/0x45 */
static uint8_t sht30_arg_ok(SysI2cId_t bus, uint8_t addr7)
{
    if ((uint32_t)bus >= (uint32_t)SYS_I2C_COUNT) {
        return 0U;
    }
    if ((addr7 != SYS_SHT30_ADDR_LOW) && (addr7 != SYS_SHT30_ADDR_HIGH)) {
        return 0U;
    }
    return 1U;
}

/* CRC-8：多项式 0x31、初值 0xFF、无最终异或；SHT30 每次 2 字节数据后跟 1 字节 CRC */
uint8_t SYS_SHT30_Crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFFU;
    uint8_t i, b;

    if (data == 0) {
        return 0U;
    }
    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (b = 0U; b < 8U; b++) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((uint8_t)(crc << 1) ^ 0x31U);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint8_t SYS_SHT30_GetAddr(SysI2cId_t bus)
{
    if ((uint32_t)bus >= (uint32_t)SYS_I2C_COUNT) {
        return 0U;
    }
    return s_addr[bus];
}

int SYS_SHT30_SoftReset(SysI2cId_t bus)
{
    uint8_t cmd = SYS_SHT30_CMD_SOFT_RESET;   /* 0xA2，首字节 0x30 */
    uint8_t addr7 = SYS_SHT30_GetAddr(bus);

    if (addr7 == 0U) {
        return SYS_SHT30_ERR_PARAM;
    }
    return SYS_I2C_WriteBytes(bus, addr7, 0x30U, &cmd, 1U);
}

uint8_t SYS_SHT30_InitAddr(SysI2cId_t bus, uint8_t addr7)
{
    int r;

    if (sht30_arg_ok(bus, addr7) == 0U) {
        return SYS_SHT30_ERR_PARAM;
    }

    /* 先记录地址：即使探测失败，ReadRaw 仍可在接线修好后直接重试 */
    s_addr[bus] = addr7;

    r = SYS_I2C_IsDeviceReady(bus, addr7);
    if (r != SYS_I2C_OK) {
        return SYS_SHT30_ERR_NO_DEVICE;
    }

    /* 软复位把器件从"上次测量未读完"的中间态拉回来；上电到可通信最长 1.5ms */
    (void)SYS_SHT30_SoftReset(bus);
    delay_ms(SYS_SHT30_RESET_MS);

    r = SYS_I2C_IsDeviceReady(bus, addr7);
    if (r != SYS_I2C_OK) {
        return SYS_SHT30_ERR_NO_DEVICE;
    }
    return SYS_SHT30_OK;
}

uint8_t SYS_SHT30_Init(SysI2cId_t bus)
{
    return SYS_SHT30_InitAddr(bus, SYS_SHT30_ADDR_DEFAULT);
}

/* 候选测量命令表。
 * 很多 4 针模块丝印写 SHT30，实际贴的是 SHT40（0x44 同地址、数据格式相同、
 * 但命令字不同）。这里把两族命令都试一遍，用 CRC 校验判定谁成功。 */
typedef struct
{
    uint8_t cmd0;       /* 首字节 */
    uint8_t cmd1;       /* 次字节；单字节命令时填 0 */
    uint8_t conv_ms;    /* 转换等待时间 */
    uint8_t single;     /* 1 = 单字节命令（SHT4x 一族） */
} Sht30Cand_t;

static const Sht30Cand_t s_cand[] =
{
    { 0x24U, 0x06U, 15U, 0U },   /* SHT3x 高重复性、拉伸关闭 */
    { 0x2CU, 0x06U, 15U, 0U },   /* SHT3x 高重复性、拉伸使能 */
    { 0xFDU, 0x00U, 10U, 1U },   /* SHT4x 高精度 */
    { 0xF6U, 0x00U,  6U, 1U },   /* SHT4x 中精度 */
};

static int8_t s_cand_idx = -1;   /* -1 = 尚未确定；否则为命中的候选下标 */

/* 返回实际生效的器件族：0=未知 1=SHT3x 2=SHT4x */
uint8_t SYS_SHT30_Proto(SysI2cId_t bus)
{
    (void)bus;
    if (s_cand_idx < 0)
    {
        return 0U;
    }
    return (s_cand_idx < 2) ? 1U : 2U;
}

int SYS_SHT30_ReadRaw(SysI2cId_t bus, uint16_t *temp_raw, uint16_t *humi_raw)
{
    uint8_t addr7, b[6];
    uint8_t k, i, n;
    int r;

    if ((temp_raw == 0) || (humi_raw == 0))
    {
        return SYS_SHT30_ERR_PARAM;
    }
    addr7 = SYS_SHT30_GetAddr(bus);
    if (addr7 == 0U)
    {
        return SYS_SHT30_ERR_PARAM;
    }

    n = (uint8_t)(sizeof(s_cand) / sizeof(s_cand[0]));
    r = SYS_SHT30_ERR_NO_DEVICE;

    for (i = 0U; i < n; i++)
    {
        /* 已确认过协议就直接用它，失败再顺延尝试其它命令 */
        if (s_cand_idx < 0)
        {
            k = i;
        }
        else
        {
            k = (uint8_t)(((uint32_t)s_cand_idx + i) % n);
        }

        if (i != 0U)
        {
            SYS_I2C_BusReset(bus);
            delay_ms(3);
        }

        if (s_cand[k].single != 0U)
        {
            r = SYS_I2C_WriteCmd(bus, addr7, s_cand[k].cmd0);
        }
        else
        {
            r = SYS_I2C_WriteBytes(bus, addr7, s_cand[k].cmd0, &s_cand[k].cmd1, 1U);
        }
        if (r != SYS_I2C_OK)
        {
            continue;
        }

        delay_ms(s_cand[k].conv_ms);

        r = SYS_I2C_ReadRaw(bus, addr7, b, 6U);
        if (r != SYS_I2C_OK)
        {
            continue;
        }

        if ((SYS_SHT30_Crc8(&b[0], 2U) != b[2]) ||
            (SYS_SHT30_Crc8(&b[3], 2U) != b[5]))
        {
            r = SYS_SHT30_ERR_CRC;
            continue;
        }

        s_cand_idx = (int8_t)k;
        *temp_raw = (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
        *humi_raw = (uint16_t)(((uint16_t)b[3] << 8) | (uint16_t)b[4]);
        return SYS_SHT30_OK;
    }

    return r;
}

int SYS_SHT30_Read(SysI2cId_t bus, float *temp_c, float *humi_rh)
{
    uint16_t tr, hr;
    int r;

    if ((temp_c == 0) || (humi_rh == 0)) {
        return SYS_SHT30_ERR_PARAM;
    }

    r = SYS_SHT30_ReadRaw(bus, &tr, &hr);
    if (r != SYS_SHT30_OK) {
        return r;
    }

    *temp_c  = -45.0f + (175.0f * ((float)tr / 65535.0f));
    *humi_rh = 100.0f * ((float)hr / 65535.0f);
    return SYS_SHT30_OK;
}

int SYS_SHT30_ReadInt(SysI2cId_t bus, int16_t *temp_x100, int16_t *humi_x100)
{
    float t, h;
    float ts, hs;
    int r;

    if ((temp_x100 == 0) || (humi_x100 == 0)) {
        return SYS_SHT30_ERR_PARAM;
    }

    r = SYS_SHT30_Read(bus, &t, &h);
    if (r != SYS_SHT30_OK) {
        return r;
    }

    /* 四舍五入到 0.01 单位；负温度向下取整会差 1，按符号分别加减 0.5 */
    ts = (t * 100.0f);
    hs = (h * 100.0f);
    *temp_x100 = (int16_t)((ts >= 0.0f) ? (ts + 0.5f) : (ts - 0.5f));
    *humi_x100 = (int16_t)((hs >= 0.0f) ? (hs + 0.5f) : (hs - 0.5f));
    return SYS_SHT30_OK;
}
