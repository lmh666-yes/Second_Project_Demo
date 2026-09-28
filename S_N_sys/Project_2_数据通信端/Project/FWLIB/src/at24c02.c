#include "at24c02.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"

/* ================================================================
 *  at24c02.c —— AT24C02 EEPROM 驱动  实现文件
 * ================================================================
 *  24C02 的读写时序（sys_i2c 已经把底层搬好了，这里只说"数据格式"）：
 *      写 : START | 器件地址+W | ACK | 字节地址 | ACK | 数据... | ACK | STOP
 *      读 : START | 器件地址+W | ACK | 字节地址 | ACK |      ← 先"下指针"
 *           START | 器件地址+R | ACK | 数据... | NACK | STOP
 *  所以本质上 24C02 的"字节地址"在 sys_i2c 里就是一个 reg 参数 ——
 *  这也是为什么 SYS_I2C_WriteBytes/ReadBytes 能直接拿来用。
 *
 *  写周期等待（本文件的关键）：
 *      写入后芯片内部要 ~5ms 把数据搞进浮栅。这期间**它不应答**任何地址。
 *      教科书做法是死等 5ms；更好的做法是"发地址看有没有 ACK"，
 *      一有 ACK 就说明写完了 → 通常 2~3ms 就能返回，参数存得快一倍。
 * ================================================================ */


/* ================================================================
 *                      内部小工具
 * ================================================================ */

/* 等写周期结束：反复用"探测地址"看芯片是否恢复应答 */
static void at24c02_wait_ready(void)
{
    uint32_t guard = 0;

    /* 再加一层保险：万一芯片坏了永远不应答，也不能死在这里 */
    while (guard++ < 200U) {
        if (SYS_I2C_IsDeviceReady(AT24C02_I2C_ID, AT24C02_ADDR) == SYS_I2C_OK) {
            return;
        }
    }
    Delay_ms(AT24C02_WRITE_MS);     /* 兜底：直接等一个写周期 */
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
uint8_t AT24C02_Init(uint32_t speed)
{
    if (speed == 0U) speed = AT24C02_I2C_SPEED;

    SYS_I2C_Init(AT24C02_I2C_ID, speed);
    Delay_ms(10);                   /* 上电后 EEPROM 需要准备时间 */

    return AT24C02_IsOnline() ? 0U : 1U;
}

uint8_t AT24C02_IsOnline(void)
{
    return (SYS_I2C_IsDeviceReady(AT24C02_I2C_ID, AT24C02_ADDR) == SYS_I2C_OK) ? 1U : 0U;
}

uint8_t AT24C02_WriteByte(uint8_t addr, uint8_t data)
{
    int err;

    /* 24C02 的"寄存器地址"就是 EEPROM 内部字节地址，只有 8 位 */
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

    while (done < len) {
        /* 本次能写多少个：不能跨页（页 = 8 字节，页内地址低 3 位必须同页） */
        uint8_t  in_page = (uint8_t)(AT24C02_PAGE_SIZE - ((addr + done) & (AT24C02_PAGE_SIZE - 1U)));
        uint16_t n       = (uint16_t)(len - done);
        int      err;

        if (n > (uint16_t)in_page) n = (uint16_t)in_page;

        err = SYS_I2C_WriteBytes(AT24C02_I2C_ID, AT24C02_ADDR,
                                 (uint8_t)(addr + done), &buf[done], n);
        if (err != SYS_I2C_OK) return (uint8_t)(-err);

        at24c02_wait_ready();       /* 每次（页）写完都要等 */
        done = (uint16_t)(done + n);
    }
    return 0U;
}

uint8_t AT24C02_ReadBytes(uint8_t addr, uint8_t *buf, uint16_t len)
{
    int err;

    if (buf == 0 || len == 0U) return 1U;

    err = SYS_I2C_ReadBytes(AT24C02_I2C_ID, AT24C02_ADDR, addr, buf, len);
    return (err == SYS_I2C_OK) ? 0U : (uint8_t)(-err);
}


/* ================================================================
 *                    区块 3：按数据类型存取
 * ================================================================ */
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

    /* 小端序：低字节在前（和 STM32 内存里的排布一致，方便以后整块搬） */
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
    /* 用 union 按位搬运，不做任何数值转换（存进去和读到的一模一样） */
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

    str[max_len - 1U] = '\0';       /* 万一没存过（全 0xFF）也要保证可用 */
    return 0U;
}

uint8_t AT24C02_IsBlank(uint8_t addr, uint8_t magic)
{
    uint8_t v = 0;

    if (AT24C02_ReadByte(addr, &v) != 0U) return 1U;
    return (v == magic) ? 0U : 1U;
}

uint8_t AT24C02_Erase(void)
{
    uint8_t  buf[AT24C02_PAGE_SIZE];
    uint16_t i;
    uint16_t a;

    /* 出厂状态 = 全 0xFF（EEPROM 的"擦除"就是写 1） */
    for (i = 0; i < AT24C02_PAGE_SIZE; i++) buf[i] = 0xFFU;

    for (a = 0; a < AT24C02_TOTAL_SIZE; a = (uint16_t)(a + AT24C02_PAGE_SIZE)) {
        if (AT24C02_WriteBytes((uint8_t)a, buf, AT24C02_PAGE_SIZE) != 0U) return 1U;
    }
    return 0U;
}
