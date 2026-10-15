#include "at24c02.h"
/* 接口说明见 at24c02.h；本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_ms 等）独立文件 */

/* AT24C02 EEPROM 驱动实现。器件字节地址在 sys_i2c 中作为 reg 参数传入，
 * 故可直接使用 SYS_I2C_WriteBytes/ReadBytes。
 * 读写时序：
 *   写: START | 器件地址+W | ACK | 字节地址 | ACK | 数据... | ACK | STOP
 *   读: START | 器件地址+W | ACK | 字节地址 | ACK | START | 器件地址+R | ACK | 数据... | NACK | STOP
 * 写周期：写入后芯片内部约 5ms，其间不应答任何地址，故用探测地址 ACK 的方式等待 */


/* 等写周期结束：探测器件地址，直到芯片恢复应答 */
static void at24c02_wait_ready(void)
{
    uint32_t guard = 0;

    /* 探测次数上限：芯片异常无应答时不能在此死循环 */
    while (guard++ < 200U) {
        if (SYS_I2C_IsDeviceReady(AT24C02_I2C_ID, AT24C02_ADDR) == SYS_I2C_OK) {
            return;
        }
    }
    delay_ms(AT24C02_WRITE_MS);     /* 兜底：等一个写周期 */
}


uint8_t AT24C02_Init(uint32_t speed)
{
    if (speed == 0U) speed = AT24C02_I2C_SPEED;

    SYS_I2C_Init(AT24C02_I2C_ID, speed);
    delay_ms(10);                   /* 上电后 EEPROM 需要准备时间 */

    return AT24C02_IsOnline() ? 0U : 1U;
}

uint8_t AT24C02_IsOnline(void)
{
    return (SYS_I2C_IsDeviceReady(AT24C02_I2C_ID, AT24C02_ADDR) == SYS_I2C_OK) ? 1U : 0U;
}

uint8_t AT24C02_WriteByte(uint8_t addr, uint8_t data)
{
    int err;

    /* 24C02 的字节地址即 EEPROM 内部地址，只有 8 位 */
    err = SYS_I2C_WriteBytes(AT24C02_I2C_ID, AT24C02_ADDR, addr, &data, 1U);
    if (err != SYS_I2C_OK) return (uint8_t)(-err);

    at24c02_wait_ready();
    return 0U;
}

uint8_t AT24C02_ReadByte(uint8_t addr, uint8_t *out)
{
    int err;

    if (out == 0) return 1U;

    err = SYS_I2C_ReadBytes(AT24C02_I2C_ID, AT24C02_ADDR, addr, out, 1U);
    return (err == SYS_I2C_OK) ? 0U : (uint8_t)(-err);
}

uint8_t AT24C02_WriteBytes(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t done = 0;

    if (buf == 0 || len == 0U) return 1U;

    /* 越界必须先拦：24C02 地址只有 8 位，addr+len 超出总容量时数据会绕回低地址，
     * 静默覆盖头部 MAGIC 区。addr 为 uint8_t（0~255），只可能末址超过 255。
     * 越界返回 1 */
    if ((uint16_t)addr + len > (uint16_t)AT24C02_TOTAL_SIZE) return 1U;

    while (done < len) {
        /* 本次能写多少个：不能跨页（页 = 8 字节，页内地址低 3 位必须同页） */
        uint8_t  in_page = (uint8_t)(AT24C02_PAGE_SIZE - ((addr + done) & (AT24C02_PAGE_SIZE - 1U)));
        uint16_t n       = (uint16_t)(len - done);
        int      err;

        if (n > (uint16_t)in_page) n = (uint16_t)in_page;

        err = SYS_I2C_WriteBytes(AT24C02_I2C_ID, AT24C02_ADDR,
                                 (uint8_t)(addr + done), &buf[done], n);
        if (err != SYS_I2C_OK) return (uint8_t)(-err);

        at24c02_wait_ready();       /* 每页写完都要等写周期 */
        done = (uint16_t)(done + n);
    }
    return 0U;
}

uint8_t AT24C02_ReadBytes(uint8_t addr, uint8_t *buf, uint16_t len)
{
    int err;

    if (buf == 0 || len == 0U) return 1U;
    if ((uint16_t)addr + len > (uint16_t)AT24C02_TOTAL_SIZE) return 1U;   /* 越界返回 1 */

    err = SYS_I2C_ReadBytes(AT24C02_I2C_ID, AT24C02_ADDR, addr, buf, len);
    return (err == SYS_I2C_OK) ? 0U : (uint8_t)(-err);
}


/* 按数据类型存取 */
uint8_t AT24C02_WriteU8(uint8_t addr, uint8_t val)
{
    return AT24C02_WriteByte(addr, val);
}

uint8_t AT24C02_ReadU8(uint8_t addr, uint8_t *out)
{
    return AT24C02_ReadByte(addr, out);
}

uint8_t AT24C02_WriteU16(uint8_t addr, uint16_t val)
{
    uint8_t tmp[2];

    /* 小端序：低字节在前，与 STM32 内存排布一致 */
    tmp[0] = (uint8_t)(val & 0xFFU);
    tmp[1] = (uint8_t)((val >> 8) & 0xFFU);
    return AT24C02_WriteBytes(addr, tmp, 2U);
}

uint8_t AT24C02_ReadU16(uint8_t addr, uint16_t *out)
{
    uint8_t tmp[2];

    if (out == 0) return 1U;
    if (AT24C02_ReadBytes(addr, tmp, 2U) != 0U) return 1U;

    *out = (uint16_t)(((uint16_t)tmp[1] << 8) | (uint16_t)tmp[0]);
    return 0U;
}

uint8_t AT24C02_WriteU32(uint8_t addr, uint32_t val)
{
    uint8_t tmp[4];

    tmp[0] = (uint8_t)(val & 0xFFU);
    tmp[1] = (uint8_t)((val >> 8) & 0xFFU);
    tmp[2] = (uint8_t)((val >> 16) & 0xFFU);
    tmp[3] = (uint8_t)((val >> 24) & 0xFFU);
    return AT24C02_WriteBytes(addr, tmp, 4U);
}

uint8_t AT24C02_ReadU32(uint8_t addr, uint32_t *out)
{
    uint8_t tmp[4];

    if (out == 0) return 1U;
    if (AT24C02_ReadBytes(addr, tmp, 4U) != 0U) return 1U;

    *out = ((uint32_t)tmp[3] << 24) | ((uint32_t)tmp[2] << 16) |
           ((uint32_t)tmp[1] << 8)  |  (uint32_t)tmp[0];
    return 0U;
}

uint8_t AT24C02_WriteFloat(uint8_t addr, float val)
{
    /* 按位搬运，不做数值转换 */
    union {
        float    f;
        uint32_t u;
    } conv;

    conv.f = val;
    return AT24C02_WriteU32(addr, conv.u);
}

uint8_t AT24C02_ReadFloat(uint8_t addr, float *out)
{
    union {
        float    f;
        uint32_t u;
    } conv;

    if (out == 0) return 1U;
    if (AT24C02_ReadU32(addr, &conv.u) != 0U) return 1U;

    *out = conv.f;
    return 0U;
}

uint8_t AT24C02_WriteString(uint8_t addr, const char *str, uint8_t max_len)
{
    uint8_t n = 0;

    if (str == 0 || max_len == 0U) return 1U;

    /* 连同结尾 '\0' 一起存，读的时候直接就是 C 字符串 */
    while (str[n] != '\0' && n < (uint8_t)(max_len - 1U)) n++;

    return AT24C02_WriteBytes(addr, (const uint8_t *)str, (uint16_t)(n + 1U));
}

uint8_t AT24C02_ReadString(uint8_t addr, char *str, uint8_t max_len)
{
    if (str == 0 || max_len == 0U) return 1U;

    if (AT24C02_ReadBytes(addr, (uint8_t *)str, max_len) != 0U) return 1U;

    str[max_len - 1U] = '\0';       /* 未存过时内容为全 0xFF，仍需保证可用 */
    return 0U;
}

/* 魔数判断参数是否已存过（首次上电判断）
 * 返回值：1 = 读到的值等于 magic；0 = 不相等或读失败 */
uint8_t AT24C02_IsMagicSet(uint8_t addr, uint8_t magic)
{
    uint8_t v = 0;

    if (AT24C02_ReadByte(addr, &v) != 0U) return 0U;   /* 读不出来 → 当"没存过" */
    return (v == magic) ? 1U : 0U;
}

uint8_t AT24C02_Erase(void)
{
    uint8_t  buf[AT24C02_PAGE_SIZE];
    uint16_t i;
    uint16_t a;

    /* 出厂状态为全 0xFF，EEPROM 的擦除即写 1 */
    for (i = 0; i < AT24C02_PAGE_SIZE; i++) buf[i] = 0xFFU;

    for (a = 0; a < AT24C02_TOTAL_SIZE; a = (uint16_t)(a + AT24C02_PAGE_SIZE)) {
        if (AT24C02_WriteBytes((uint8_t)a, buf, AT24C02_PAGE_SIZE) != 0U) return 1U;
    }
    return 0U;
}
