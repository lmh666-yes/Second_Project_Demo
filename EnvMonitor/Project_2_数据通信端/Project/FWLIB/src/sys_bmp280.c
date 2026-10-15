#include "sys_bmp280.h"
/* 接口说明与使用注记见同名 .h;本文件为实现层 */

#include <math.h>
#include "delay.h"
#include "sys_tick.h"


/* BMP280 气压 + 温度传感器驱动实现
 *
 * 实现流程:
 *   1) 初始化: 读 0xD0 自检(须 = 0x58) + 读 0x88~0x9F 共 24 字节标定系数
 *      + 写 0xF5/0xF4 配置;
 *   2) 读数据: forced 模式触发 0xF4,等转换结束,读 0xF7 起 6 字节,
 *      一次事务读完,6 字节取自同一采样时刻;
 *   3) 补偿: Bosch 数据手册定点参考实现
 *      (bmp280_compensate_T_int32 / bmp280_compensate_P_int64),
 *      气压算法中间量超出 32 位,需 int64_t;
 *   4) 海拔: 国际气压高度公式,海平面气压为可配宏。
 *
 * 寄存器地址(手册寄存器映射):
 *   0xD0 ID         芯片 ID(BMP280 = 0x58 / BME280 = 0x60 / BMP180 = 0x55)
 *   0xE0 RESET      写 0xB6 软复位,复位后需重读标定系数
 *   0xF3 STATUS     bit3 measuring(1 = 转换中) / bit0 im_update
 *   0xF4 CTRL_MEAS  osrs_t[7:5] | osrs_p[4:2] | mode[1:0]
 *                   mode: 00 = sleep,01/10 = forced,11 = normal
 *   0xF5 CONFIG     t_sb[7:5] | filter[4:2] | spi3w_en[0]
 *   0x88~0xA1       标定系数,本模块只用 0x88~0x9F 共 24 字节:
 *                   T1 u16 / T2 s16 / T3 s16,
 *                   P1 u16 / P2..P9 s16,全部小端
 *   0xF7~0xFC       数据 6 字节,气压 3 字节 + 温度 3 字节,大端 20 位 */


/* 内部数据表 */
/* 寄存器地址 */
#define BMP280_REG_ID           0xD0
#define BMP280_REG_RESET        0xE0
#define BMP280_REG_STATUS       0xF3
#define BMP280_REG_CTRL_MEAS    0xF4
#define BMP280_REG_CONFIG       0xF5
#define BMP280_REG_CALIB        0x88
#define BMP280_REG_DATA         0xF7

#define BMP280_ID_VALUE         0x58    /* BMP280 */
#define BMP280_ID_BME280        0x60    /* BME280,丝印常标为 BMP280 */
#define BMP280_ID_BMP180        0x55    /* BMP180,寄存器与标定系数均不同 */

#define BMP280_RESET_CMD        0xB6

#define BMP280_STATUS_MEASURING 0x08    /* bit3: 1 = 转换进行中 */

#define BMP280_MODE_SLEEP       0x00
#define BMP280_MODE_FORCED      0x01
#define BMP280_MODE_NORMAL      0x03

/* 标定系数(小端、有符号按类型解析;T1/P1 是无符号,其余有符号) */
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} Bmp280Calib_t;

/* 标定系数缓存,按总线各存一份
 * 器件软复位或掉电后系数丢失,Init 每次都重读 */
static Bmp280Calib_t s_calib[SYS_I2C_COUNT];

/* 本模块使用的器件地址,按总线各存一份
 * SYS_BMP280_InitAddr() 可改为 0x77,读数据须用同一地址,故此处保存
 * 初值为默认地址,未初始化直接调 GetID 也可用 */
static uint8_t s_addr[SYS_I2C_COUNT] = {
    SYS_BMP280_ADDR, SYS_BMP280_ADDR, SYS_BMP280_ADDR
};

/* 温度补偿中间量 t_fine,气压补偿的输入之一 */
static int32_t s_t_fine[SYS_I2C_COUNT];

/* 寄存器 0x88~0x9F 标定系数为小端,数据区 0xF7 起为大端 */
static int16_t bmp280_rd_s16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint16_t bmp280_rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}


/* Bosch BMP280 数据手册(BSH Rev 1.1 及之后)附录 Reference implementation
 * 的官方定点代码移植: 常量、位运算与运算顺序逐行对应,仅命名与注释本地化。
 *
 * 约束:
 *   1) 全部用整数运算,适配无 FPU 的 MCU;
 *   2) 中间量 p1/p2 超出 int32 范围,手册要求用 int64,缩为 int32 会算错;
 *   3) 改用 double 近似会引入与官方不一致的舍入偏差。 */

/* 温度补偿,移植自官方 bmp280_compensate_T_int32()
 * 参数: c      标定系数 T1/T2/T3
 *       adc_T  0xF7 区读出的 20 位原始温度 ADC 值
 *       t_fine 输出,温度补偿中间量,气压补偿用其作输入
 * 返回: 温度,单位 0.01 ℃(2356 = 23.56 ℃) */
static int32_t bmp280_compensate_T_int32(const Bmp280Calib_t *c,
                                         int32_t adc_T, int32_t *t_fine)
{
    int32_t var1, var2, T;

    var1 = ((((adc_T >> 3) - ((int32_t)c->dig_T1 << 1))) * ((int32_t)c->dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)c->dig_T1)) *
              ((adc_T >> 4) - ((int32_t)c->dig_T1))) >> 12) *
            ((int32_t)c->dig_T3)) >> 14;

    *t_fine = var1 + var2;
    T = (*t_fine * 5 + 128) >> 8;       /* 0.01 ℃ */
    return T;
}

/* 气压补偿,移植自官方 bmp280_compensate_P_int64()
 * 参数: c      标定系数 P1..P9
 *       adc_P  0xF7 区读出的 20 位原始气压 ADC 值
 *       t_fine 温度补偿算出的中间量,须先算温度再算气压
 * 返回: 气压 Q24.8 定点,真实 Pa = 返回值 >> 8
 * 中间量 p1/p2 超出 int32,官方用 int64,不可改为 int32/double */
static uint32_t bmp280_compensate_P_int64(const Bmp280Calib_t *c,
                                          uint32_t adc_P, int32_t t_fine)
{
    int64_t var1, var2, p;

    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)c->dig_P6;
    var2 = var2 + ((var1 * (int64_t)c->dig_P5) << 17);
    var2 = var2 + (((int64_t)c->dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)c->dig_P3) >> 8) + ((var1 * (int64_t)c->dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)c->dig_P1) >> 33;

    if (var1 == 0) {
        return 0U;      /* var1 为 0 时直接返回,避免除零 */
    }

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)c->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)c->dig_P8) * p) >> 19;

    p = ((p + var1 + var2) >> 8) + (((int64_t)c->dig_P7) << 4);
    return (uint32_t)p;
}


/* 等待转换结束: 轮询 0xF3 的 bit3(measuring) 变 0
 * 返回: SYS_BMP280_OK / SYS_BMP280_ERR_TIMEOUT / 负数(I2C 错误)
 * 读 status 失败或超过 SYS_BMP280_MEAS_TIMEOUT_MS 返回 ERR_TIMEOUT */
static int bmp280_wait_ready(SysI2cId_t bus, uint8_t addr7)
{
    uint32_t t0 = SYS_TICK_GetTick();
    uint8_t  st = 0U;

    for (;;) {
        if (SYS_I2C_ReadByte(bus, addr7, BMP280_REG_STATUS, &st) != SYS_I2C_OK) {
            return SYS_BMP280_ERR_TIMEOUT;
        }
        if ((st & BMP280_STATUS_MEASURING) == 0U) {
            return SYS_BMP280_OK;
        }
        if (SYS_TICK_Elapsed(t0) >= SYS_BMP280_MEAS_TIMEOUT_MS) {
            return SYS_BMP280_ERR_TIMEOUT;
        }
    }
}

/* 写 0xF4: osrs_t[7:5] | osrs_p[4:2] | mode[1:0] */
static int bmp280_write_ctrl_meas(SysI2cId_t bus, uint8_t addr7, uint8_t mode)
{
    uint8_t v = (uint8_t)(((SYS_BMP280_OSRS_T & 0x07U) << 5) |
                          ((SYS_BMP280_OSRS_P & 0x07U) << 2) |
                          (mode & 0x03U));
    return SYS_I2C_WriteByte(bus, addr7, BMP280_REG_CTRL_MEAS, v);
}


/* 基础功能 */
uint8_t SYS_BMP280_Init(SysI2cId_t bus)
{
    return SYS_BMP280_InitAddr(bus, SYS_BMP280_ADDR);
}

int SYS_BMP280_GetID(SysI2cId_t bus)
{
    uint8_t id = 0U;

    if (bus >= SYS_I2C_COUNT) return SYS_I2C_ERR_PARAM;
    if (SYS_I2C_ReadByte(bus, s_addr[bus], BMP280_REG_ID, &id) != SYS_I2C_OK) {
        return SYS_I2C_ERR_ADDR;
    }
    return (int)id;
}

int SYS_BMP280_Trigger(SysI2cId_t bus)
{
    if (bus >= SYS_I2C_COUNT) return SYS_BMP280_ERR_PARAM;
    return bmp280_write_ctrl_meas(bus, s_addr[bus], BMP280_MODE_FORCED);
}

int SYS_BMP280_ReadRaw(SysI2cId_t bus, int32_t *temp_c100, uint32_t *press_pa)
{
    uint8_t d[6];
    uint32_t adc_P;
    int32_t adc_T;
    int     err;

    if (bus >= SYS_I2C_COUNT) return SYS_BMP280_ERR_PARAM;
    if (temp_c100 == 0 && press_pa == 0) return SYS_BMP280_ERR_PARAM;

#if (SYS_BMP280_USE_NORMAL_MODE != 0)
    /* normal 模式: 芯片按周期连续转换,直接读最新结果 */
    err = bmp280_wait_ready(bus, s_addr[bus]);
    if (err != SYS_BMP280_OK) return err;
#else
    /* forced 模式: 每次读之前触发一次单次转换,避免连续自加热 */
    err = bmp280_write_ctrl_meas(bus, s_addr[bus], BMP280_MODE_FORCED);
    if (err != SYS_I2C_OK) return err;

    /* 手册: 单次转换耗时随过采样上升,最高配置最坏约 43 ms
     * 先等固定余量,再轮询 status 确认 */
    delay_ms(SYS_BMP280_FORCED_DELAY_MS);
    err = bmp280_wait_ready(bus, s_addr[bus]);
    if (err != SYS_BMP280_OK) return err;
#endif

    /* 一次突发读 6 字节(0xF7~0xFC,寄存器地址自增),
     * 保证气压与温度取自同一采样时刻 */
    err = SYS_I2C_ReadBytes(bus, s_addr[bus], BMP280_REG_DATA, d, 6U);
    if (err != SYS_I2C_OK) return err;

    /* 20 位原始值: 气压在 d[0..2]、温度在 d[3..5],高位在前(大端)
     * 气压用 uint32_t 承接: 官方算法中有 (p << 31) 这类左移到符号位的运算,
     * 须走无符号,否则是未定义行为;温度侧 20 位不会溢出,故用有符号 */
    adc_P = (((uint32_t)d[0] << 16) | ((uint32_t)d[1] << 8) | d[2]) >> 4;
    adc_T = (int32_t)((((uint32_t)d[3] << 16) | ((uint32_t)d[4] << 8) | d[5]) >> 4);

    /* 温度先算得到 t_fine,气压补偿须用它作输入,顺序不能反 */
    if (temp_c100) {
        *temp_c100 = bmp280_compensate_T_int32(&s_calib[bus], adc_T, &s_t_fine[bus]);
    } else {
        (void)bmp280_compensate_T_int32(&s_calib[bus], adc_T, &s_t_fine[bus]);
    }

    if (press_pa) {
        /* 官方返回 Q24.8 定点,>> 8 得到 Pa */
        *press_pa = bmp280_compensate_P_int64(&s_calib[bus], adc_P, s_t_fine[bus]) >> 8;
    }
    return SYS_BMP280_OK;
}

int SYS_BMP280_Read(SysI2cId_t bus, SYS_BMP280_Data_t *out)
{
    int32_t  t100 = 0;
    uint32_t p_pa = 0U;
    int      err;

    if (bus >= SYS_I2C_COUNT || out == 0) return SYS_BMP280_ERR_PARAM;

    err = SYS_BMP280_ReadRaw(bus, &t100, &p_pa);
    if (err != SYS_BMP280_OK) return err;

    out->temp_c    = (float)t100 / 100.0f;
    out->press_pa  = (float)p_pa;
    out->press_hpa = (float)p_pa / 100.0f;
    out->altitude_m = SYS_BMP280_AltitudeM(out->press_hpa,
                                           SYS_BMP280_SEA_LEVEL_HPA_DEFAULT);
    return SYS_BMP280_OK;
}


/* 扩展功能 */
float SYS_BMP280_AltitudeM(float press_hpa, float sea_level_hpa)
{
    /* 国际气压高度公式(标准大气):
     *   h = 44330 × [1 - (P / P0)^(1/5.255)]
     * 指数 1/5.255 = 0.1903,由气温直减率 6.5 K/km 得出 */
    if (press_hpa <= 0.0f || sea_level_hpa <= 0.0f) return 0.0f;
    return 44330.0f * (1.0f - powf(press_hpa / sea_level_hpa, 0.1903f));
}

uint8_t SYS_BMP280_InitAddr(SysI2cId_t bus, uint8_t addr7)
{
    uint8_t id  = 0U;
    uint8_t cal[24];
    uint8_t cfg;
    Bmp280Calib_t *c;
    int     err;

    if (bus >= SYS_I2C_COUNT) return SYS_BMP280_ERR_PARAM;

    /* 先初始化总线,调用方不用自己 Init I2C */
    SYS_I2C_Init(bus, 400000);

    /* 保存本次使用的地址,后续读函数沿用,
     * 这样 InitAddr(0x77) 之后 Read/ReadRaw 也跟着走 0x77 */
    s_addr[bus] = addr7;

    /* 1) 自检: 0xD0 须读到 0x58
     *    0x60 = BME280、0x55 = BMP180,两者寄存器与标定系数均不同,
     *    本驱动不适用,统一返回 SYS_BMP280_ERR_CHIP_ID */
    err = SYS_I2C_ReadByte(bus, addr7, BMP280_REG_ID, &id);
    if (err != SYS_I2C_OK) return SYS_BMP280_ERR_CHIP_ID;
    if (id != BMP280_ID_VALUE) {
        (void)BMP280_ID_BME280;
        (void)BMP280_ID_BMP180;
        return SYS_BMP280_ERR_CHIP_ID;
    }

    /* 2) 软复位: 写 0xB6,使器件回到已知初态
     *    手册要求复位后延时再读标定系数,复位期间器件不响应 */
    if (SYS_I2C_WriteByte(bus, addr7, BMP280_REG_RESET, BMP280_RESET_CMD) == SYS_I2C_OK) {
        delay_ms(5);
    }

    /* 3) 读 24 字节标定系数(0x88~0x9F,突发读,小端解析)
     *    系数每片不同,须从器件读取,不能写死常数 */
    err = SYS_I2C_ReadBytes(bus, addr7, BMP280_REG_CALIB, cal, 24U);
    if (err != SYS_I2C_OK) return SYS_BMP280_ERR_CALIB;

    c = &s_calib[bus];
    c->dig_T1 = bmp280_rd_u16(&cal[0]);
    c->dig_T2 = bmp280_rd_s16(&cal[2]);
    c->dig_T3 = bmp280_rd_s16(&cal[4]);
    c->dig_P1 = bmp280_rd_u16(&cal[6]);
    c->dig_P2 = bmp280_rd_s16(&cal[8]);
    c->dig_P3 = bmp280_rd_s16(&cal[10]);
    c->dig_P4 = bmp280_rd_s16(&cal[12]);
    c->dig_P5 = bmp280_rd_s16(&cal[14]);
    c->dig_P6 = bmp280_rd_s16(&cal[16]);
    c->dig_P7 = bmp280_rd_s16(&cal[18]);
    c->dig_P8 = bmp280_rd_s16(&cal[20]);
    c->dig_P9 = bmp280_rd_s16(&cal[22]);

    /* 4) 写 0xF5 CONFIG: 只填 IIR filter[4:2],t_sb/spi3w 保持 0
     *    t_sb 只在 normal 模式下有效,forced 模式用不到 */
    cfg = (uint8_t)((SYS_BMP280_IIR_FILTER & 0x07U) << 2);
    if (SYS_I2C_WriteByte(bus, addr7, BMP280_REG_CONFIG, cfg) != SYS_I2C_OK) {
        return SYS_BMP280_ERR_CALIB;
    }

    /* 5) 写 0xF4 CTRL_MEAS:
     *    forced 模式先写 sleep 清 mode,每次读之前再由 Trigger 触发;
     *    normal 模式在此直接进连续转换 */
#if (SYS_BMP280_USE_NORMAL_MODE != 0)
    err = bmp280_write_ctrl_meas(bus, addr7, BMP280_MODE_NORMAL);
#else
    err = bmp280_write_ctrl_meas(bus, addr7, BMP280_MODE_SLEEP);
#endif
    if (err != SYS_I2C_OK) return SYS_BMP280_ERR_CALIB;

    return SYS_BMP280_OK;
}
