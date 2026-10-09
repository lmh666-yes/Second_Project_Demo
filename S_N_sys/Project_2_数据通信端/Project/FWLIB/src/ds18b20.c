#include "ds18b20.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_us 等）独立文件 */

/* ================================================================
 *  ds18b20.c —— DS18B20 单总线温度传感器  实现文件
 * ================================================================
 *  单总线时序全靠"拨引脚"，本文件把引脚操作收敛成两个内部函数：
 *      dq_low()  → 拉低（输出 0）
 *      dq_high() → 释放（开漏输出 1 = 高阻，靠外部上拉拉高）
 *  读电平直接读 IDR——开漏输出时 IDR 反映的是引脚真实电平，
 *  所以**不需要**在"输出/输入"之间来回切模式（这是本驱动比
 *  常见教程更简洁的地方）。
 *
 *  ⚠ 关键前提：DQ 必须有上拉电阻（本板 10K 已焊），否则释放后电平浮空。
 * ================================================================ */


/* 上一次成功读到的温度（0.1℃ 单位） */
static int16_t ds_last_c10 = 0;


/* ================================================================
 *                    底层：引脚操作
 * ================================================================ */
static void dq_low(void)
{
    GPIO_OutReset(DS18B20_PORT, DS18B20_PIN);
}

static void dq_high(void)
{
    GPIO_OutSet(DS18B20_PORT, DS18B20_PIN);   /* 开漏输出 1 = 释放总线 */
}

static uint8_t dq_read(void)
{
    return GPIO_InRead(DS18B20_PORT, DS18B20_PIN);
}


/* ================================================================
 *                    底层：单总线时序
 * ================================================================ */

/* 复位脉冲 + 等待存在脉冲
 * 返回 : 1 = 有器件应答（好）；0 = 无应答 */
uint8_t DS18B20_IsPresent(void)
{
    uint8_t present;

    dq_low();                                   /* ① 拉低 >480µs */
    delay_us(DS18B20_RESET_US);

    dq_high();                                  /* ② 释放，等 15~60µs */
    delay_us(DS18B20_RELEASE_US);

    present = dq_read();                        /* ③ 读：应为 0（器件拉低应答） */
    delay_us(DS18B20_PRESENCE_US);              /* ④ 等应答脉冲结束，总线回高 */

    return (present == 0U) ? 1U : 0U;
}

/* 写一个位（时隙 ≥60µs）
 *   写 0：拉低 60µs 左右；写 1：拉低 2µs 后立即释放（由外部上拉拉高） */
void DS18B20_WriteBit(uint8_t bit)
{
    if (bit) {
        dq_low();
        delay_us(2);
        dq_high();
        delay_us(60);
    } else {
        dq_low();
        delay_us(60);
        dq_high();
        delay_us(2);
    }
}

/* 读一个位：拉低 2µs 触发时隙 → 释放 → 15µs 内采样 */
uint8_t DS18B20_ReadBit(void)
{
    uint8_t bit;

    dq_low();
    delay_us(2);
    dq_high();
    delay_us(10);                               /* 采样点落在 15µs 窗口内 */
    bit = dq_read();
    delay_us(50);                               /* 补足时隙（≥60µs） */

    return bit;
}

/* 写一个字节（**低位在前**，这是单总线的规定，和 SPI 相反！） */
void DS18B20_WriteByte(uint8_t byte)
{
    for (uint8_t i = 0; i < 8U; i++) {
        DS18B20_WriteBit((uint8_t)(byte & 0x01U));
        byte = (uint8_t)(byte >> 1);
    }
}

/* 读一个字节（同样低位在前） */
uint8_t DS18B20_ReadByte(void)
{
    uint8_t byte = 0;

    for (uint8_t i = 0; i < 8U; i++) {
        byte = (uint8_t)(byte >> 1);
        if (DS18B20_ReadBit()) byte |= 0x80U;
    }
    return byte;
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
uint8_t DS18B20_Init(void)
{
    /* 开漏输出：写 1 = 释放总线（靠板上上拉拉高），写 0 = 拉低
     * GPIO_OutInitOD 内部自动使能端口时钟 */
    GPIO_OutInitOD(DS18B20_PORT, DS18B20_PIN);
    dq_high();

    return DS18B20_IsPresent();
}

/* 启动一次转换：SKIP ROM(0xCC) + CONVERT T(0x44)
 * SKIP ROM = 总线上只有一个器件时跳过 ROM 匹配（本板就一个，够用） */
uint8_t DS18B20_StartConvert(void)
{
    if (!DS18B20_IsPresent()) return 1U;

    DS18B20_WriteByte(0xCCU);
    DS18B20_WriteByte(0x44U);
    return 0U;
}

uint8_t DS18B20_ReadScratch(uint8_t sp[9])
{
    uint8_t i;
    uint8_t sum = 0;

    if (sp == 0) return 2U;

    if (!DS18B20_IsPresent()) return 1U;

    DS18B20_WriteByte(0xCCU);                   /* SKIP ROM */
    DS18B20_WriteByte(0xBEU);                   /* READ SCRATCHPAD */
    for (i = 0; i < 9U; i++) sp[i] = DS18B20_ReadByte();

    /* 前 8 字节求和的低 8 位 = 第 9 字节 */
    for (i = 0; i < 8U; i++) sum = (uint8_t)(sum + sp[i]);

    return (sum == sp[8]) ? 0U : 2U;
}

uint8_t DS18B20_ReadTempRaw(int16_t *raw16)
{
    uint8_t sp[9];
    uint8_t r;

    if (raw16 == 0) return 2U;

    r = DS18B20_ReadScratch(sp);
    if (r != 0U) return r;

    /* 温度是 16 位有符号数，LSB 在前（sp[0] 低字节、sp[1] 高字节） */
    *raw16 = (int16_t)(((uint16_t)sp[1] << 8) | (uint16_t)sp[0]);
    return 0U;
}

#if   (DS18B20_RESOLUTION == 9)
    #define DS18B20_CONV_MS   DS18B20_CONV_MS_9
#elif (DS18B20_RESOLUTION == 10)
    #define DS18B20_CONV_MS   DS18B20_CONV_MS_10
#elif (DS18B20_RESOLUTION == 11)
    #define DS18B20_CONV_MS   DS18B20_CONV_MS_11
#else
    #define DS18B20_CONV_MS   DS18B20_CONV_MS_12
#endif

uint8_t DS18B20_ReadTempC10(int16_t *t_c10)
{
    int16_t raw;
    int32_t t;
    uint8_t r;

    if (t_c10 == 0) return 2U;

    if (DS18B20_StartConvert() != 0U) return 1U;

    /* 等转换完成：不用 SYS_TICK（RTOS 下可能被占），用 DWT 毫秒延时最稳 */
    delay_ms_dwt(DS18B20_CONV_MS);

    r = DS18B20_ReadTempRaw(&raw);
    if (r != 0U) return r;

    /* 1 LSB = 1/16 ℃ → 0.1℃ 单位 = raw × 10 / 16，四舍五入 */
    t = ((int32_t)raw * 10L + (raw >= 0 ? 8L : -8L)) / 16L;
    *t_c10     = (int16_t)t;
    ds_last_c10 = (int16_t)t;
    return 0U;
}

int16_t DS18B20_GetLastTempC10(void)
{
    return ds_last_c10;
}


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
uint8_t DS18B20_SetResolution(uint8_t bits)
{
    uint8_t sp[9];
    uint8_t cfg;

    if (bits < 9U)  bits = 9U;
    if (bits > 12U) bits = 12U;

    if (DS18B20_ReadScratch(sp) != 0U) return 1U;

    /* 配置寄存器（sp[4]）bit6:5 = 分辨率（00=9 位 … 11=12 位） */
    cfg = sp[4];
    cfg = (uint8_t)(cfg & (uint8_t)~0x60U);
    cfg = (uint8_t)(cfg | (uint8_t)((uint8_t)(bits - 9U) << 5));

    if (!DS18B20_IsPresent()) return 1U;
    DS18B20_WriteByte(0xCCU);                   /* SKIP ROM */
    DS18B20_WriteByte(0x4EU);                   /* WRITE SCRATCHPAD */
    DS18B20_WriteByte(sp[2]);                   /* TH */
    DS18B20_WriteByte(sp[3]);                   /* TL */
    DS18B20_WriteByte(cfg);                     /* 配置寄存器 */

    return 0U;
}

uint8_t DS18B20_WriteScratch(uint8_t th, uint8_t tl)
{
    uint8_t sp[9];
    uint8_t cfg;

    /* 保留原有分辨率设置 */
    cfg = (DS18B20_ReadScratch(sp) == 0U) ? sp[4] : 0x1FU;

    if (!DS18B20_IsPresent()) return 1U;
    DS18B20_WriteByte(0xCCU);
    DS18B20_WriteByte(0x4EU);
    DS18B20_WriteByte(th);
    DS18B20_WriteByte(tl);
    DS18B20_WriteByte(cfg);

    return 0U;
}
