#include "sys_ccs811.h"
/* 器件说明与接线见同名 .h;本文件为实现层 */

#include "delay.h"
#include "sys_tick.h"


/*
 *  sys_ccs811.c : 空气质量传感器 CCS811(eCO2 + TVOC) 实现文件
 *  初始化顺序(手册要求,顺序错器件不出数且不报错):
 *      等 20ms → 读 0x20 HW_ID 自检 → 0xFF 写 4 字节软复位 → 等 20ms
 *      → 轮询 0x00 等 APP_VALID(bit4)= 1 → 查 ERROR(bit0)
 *      → 0xF4 只发命令(APP_START)→ 写 0x01 设测量模式
 *  APP_START 只发寄存器号、不带数据,必须用 SYS_I2C_WriteCmd();
 *  SYS_I2C_WriteBytes(..., NULL, 0) 在 len == 0 时返回 SYS_I2C_ERR_PARAM。
 *  数据就绪用时间判据,不用状态位,见 ccs811_meas_period_ms()。
 *  静态表按总线 SYS_I2C_1/SYS_I2C_2/SYS_I2C_3 各存一份。
 *
 *  寄存器速查(手册寄存器映射):
 *     0x00 STATUS         bit4 APP_VALID / bit3 FW_MODE(部分固件当 DRDY)
 *                         / bit0 ERROR
 *     0x01 MEAS_MODE      bit6:4 驱动模式(0 idle / 1 1s / 2 10s / 3 60s
 *                         / 4 250ms),bit3 中断使能
 *     0x02 ALG_RESULT_DATA 4 字节: eCO2 大端 u16 + TVOC 大端 u16
 *     0x05 ENV_DATA      4 字节: 湿度 大端 u16(/512 %RH)
 *                         + 温度 大端 u16(/512 ℃,偏移 +25)
 *     0x11 BASELINE      2 字节,断电保存 / 上电写回
 *     0x20 HW_ID         固定 0x81
 *     0x23 FW_APP_VERSION
 *     0xE0 ERROR_ID      错误码(0 = 无错)
 *     0xF4 APP_START     只发寄存器号,无数据
 *     0xFF SW_RESET      必须写 4 字节序列 0x11 0xE5 0x72 0x8A
 */


/* 内部定义 */
#define CCS811_REG_STATUS       0x00
#define CCS811_REG_MEAS_MODE    0x01
#define CCS811_REG_ALG_RESULT   0x02
#define CCS811_REG_ENV_DATA     0x05
#define CCS811_REG_BASELINE     0x11
#define CCS811_REG_HW_ID        0x20
#define CCS811_REG_ERROR_ID     0xE0
#define CCS811_REG_APP_START    0xF4
#define CCS811_REG_SW_RESET     0xFF

#define CCS811_STATUS_APP_VALID 0x10U   /* bit4: 片上应用固件已加载 */
#define CCS811_STATUS_ERROR     0x01U   /* bit0: 器件报错 */

/* 数据就绪只看时间
 * CCS811 没有可靠的 DRDY: 部分固件把 STATUS 的 bit3 当 DRDY,并非所有版本都支持。
 * 这里不读 bit3,改用"距上次成功读取的时间 ≥ 当前模式周期"作判据,见 ccs811_meas_period_ms()。 */

/* 按测量模式给出数据更新周期(ms)
 * MEAS_MODE 的 bit6:4 = 驱动模式,bit3 是中断使能,取值前先掩掉。
 * 模式 1/2/3/4 分别为 1s / 10s / 60s / 250ms。
 * 模式值非法(例如读到 0xFF)时返回 0,由调用方用兜底周期。 */
static uint32_t ccs811_meas_period_ms(uint8_t mode)
{
    switch (mode & 0x70U) {
        case 0x00U: return 0U;                                  /* idle: 不测量 */
        case 0x10U: return 1000U;                               /* mode 1: 1s  */
        case 0x20U: return 10000U;                              /* mode 2: 10s */
        case 0x30U: return 60000U;                              /* mode 3: 60s */
        case 0x40U: return 250U;                                /* mode 4: 250ms */
        default:    return 0U;                                  /* 非法: 走兜底 */
    }
}

/* 每条总线上"上次成功读取"的时间戳(ms),DataReady 时间判据的起点 */
static uint32_t s_last_read_ms[SYS_I2C_COUNT];

/* 本模块在用的器件地址(按总线各存一份)
 * 读函数必须用初始化时的同一个地址: SYS_CCS811_InitAddr() 可改成 0x5B,
 * 若读函数写死 0x5A,初始化成功之后每次都会读不到。
 * 初值给默认地址,未 Init 直接调 GetHWID 也能用。 */
static uint8_t s_addr[SYS_I2C_COUNT] = {
    SYS_CCS811_ADDR, SYS_CCS811_ADDR, SYS_CCS811_ADDR
};


/* 内部辅助 */
/* 只发一个寄存器/命令字节(后面不带数据)
 * 专给 APP_START(0xF4) 这类命令式写用,底层是 SYS_I2C_WriteCmd():
 * START → 地址+W → cmd → STOP,即手册要求的事务形状。 */
static int ccs811_write_cmd(SysI2cId_t bus, uint8_t addr7, uint8_t reg)
{
    return SYS_I2C_WriteCmd(bus, addr7, reg);
}


/* 基础功能 */
uint8_t SYS_CCS811_Init(SysI2cId_t bus)
{
    return SYS_CCS811_InitAddr(bus, SYS_CCS811_ADDR);
}

int SYS_CCS811_GetHWID(SysI2cId_t bus)
{
    uint8_t id = 0U;

    if (bus >= SYS_I2C_COUNT) return SYS_I2C_ERR_PARAM;
    if (SYS_I2C_ReadByte(bus, s_addr[bus], CCS811_REG_HW_ID, &id) != SYS_I2C_OK) {
        return SYS_I2C_ERR_ADDR;
    }
    return (int)id;
}

int SYS_CCS811_GetStatus(SysI2cId_t bus)
{
    uint8_t st = 0U;

    if (bus >= SYS_I2C_COUNT) return SYS_I2C_ERR_PARAM;
    if (SYS_I2C_ReadByte(bus, s_addr[bus], CCS811_REG_STATUS, &st) != SYS_I2C_OK) {
        return SYS_I2C_ERR_DATA;
    }
    return (int)st;
}

int SYS_CCS811_GetErrorID(SysI2cId_t bus)
{
    uint8_t e = 0U;

    if (bus >= SYS_I2C_COUNT) return SYS_I2C_ERR_PARAM;
    if (SYS_I2C_ReadByte(bus, s_addr[bus], CCS811_REG_ERROR_ID, &e) != SYS_I2C_OK) {
        return SYS_I2C_ERR_DATA;
    }
    return (int)e;
}


/* 初始化(按手册顺序) */
uint8_t SYS_CCS811_InitAddr(SysI2cId_t bus, uint8_t addr7)
{
    static const uint8_t sw_reset_seq[4] = { 0x11U, 0xE5U, 0x72U, 0x8AU };
    uint8_t hw = 0U;
    uint8_t st = 0U;
    uint32_t t0;
    int err;

    if (bus >= SYS_I2C_COUNT) return SYS_CCS811_ERR_PARAM;

    /* 先把总线拉起来 */
    SYS_I2C_Init(bus, 400000);

    /* 保存本次使用的地址,后面所有读函数都用它(见 s_addr 说明) */
    s_addr[bus] = addr7;

    /* 1) 上电等待 20ms
     *    手册要求 SW_RESET 之后 ≥20ms 才可再次通信,上电时器件内部也在自复位。
     *    不等就发事务器件会 NACK,表现为"器件不在线"。 */
    delay_ms(SYS_CCS811_POWERON_DELAY_MS);

    /* 2) 自检 HW_ID(0x20) 必须 = 0x81
     *    不等于 0x81 说明接的不是 CCS811、ADDR 接反或器件未供电,
     *    返回专门错误码,不往下跑。 */
    err = SYS_I2C_ReadByte(bus, addr7, CCS811_REG_HW_ID, &hw);
    if (err != SYS_I2C_OK) return SYS_CCS811_ERR_HW_ID;
    if (hw != SYS_CCS811_HW_ID_VALUE) return SYS_CCS811_ERR_HW_ID;

    /* 3) 软复位 SW_RESET(0xFF ← 0x11 0xE5 0x72 0x8A)
     *    复位后回到确定的初始状态,避免上次运行留下的测量模式。
     *    这是写寄存器号 + 4 字节数据,用 WriteBytes。 */
    err = SYS_I2C_WriteBytes(bus, addr7, CCS811_REG_SW_RESET, sw_reset_seq, 4U);
    if (err != SYS_I2C_OK) return SYS_CCS811_ERR_HW_ID;

    /* 软复位后必须等 ≥20ms 才能再通信 */
    delay_ms(SYS_CCS811_RESET_DELAY_MS);

    /* 4) 轮询 STATUS(0x00) 等待 APP_VALID(bit4) = 1,带超时
     *    APP_VALID = 0 表示片上应用固件未加载完,此时手册禁止访问测量寄存器:
     *    提前读 0x02 不报错但会读到全 0 或脏数据,所以宁可超时报错。 */
    t0 = SYS_TICK_GetTick();
    for (;;) {
        if (SYS_I2C_ReadByte(bus, addr7, CCS811_REG_STATUS, &st) != SYS_I2C_OK) {
            return SYS_CCS811_ERR_APP_INVALID;
        }
        if ((st & CCS811_STATUS_APP_VALID) != 0U) {
            break;                          /* 固件就绪,可以继续 */
        }
        if (SYS_TICK_Elapsed(t0) >= SYS_CCS811_APP_VALID_TIMEOUT_MS) {
            return SYS_CCS811_ERR_APP_INVALID;
        }
    }

    /* 5) 查 STATUS.ERROR(bit0)
     *    器件报错时先读 ERROR_ID(0xE0) 取得具体错误码,再返回专门错误码,
     *    不把"器件报错"和"启动成功"混在一起。 */
    if ((st & CCS811_STATUS_ERROR) != 0U) {
        uint8_t e = 0U;
        (void)SYS_I2C_ReadByte(bus, addr7, CCS811_REG_ERROR_ID, &e);
        s_last_read_ms[bus] = 0U;           /* 没启动成功,时间判据归零 */
        return SYS_CCS811_ERR_DEV_ERROR;
    }

    /* 6) APP_START(0xF4): 只发寄存器号,后面不带数据
     *    必须用 SYS_I2C_WriteCmd();SYS_I2C_WriteBytes(..., NULL, 0)
     *    在 len == 0 时返回 SYS_I2C_ERR_PARAM,零长度等价于只发命令的写法不成立。 */
    err = ccs811_write_cmd(bus, addr7, CCS811_REG_APP_START);
    if (err != SYS_I2C_OK) return (uint8_t)SYS_CCS811_ERR_WRITE_CMD;

    /* 7) 设测量模式 MEAS_MODE(0x01)
     *    默认 0x10 = 恒供电 + 每 1 秒一次测量(mode 1),可选值见 .h。 */
    err = SYS_I2C_WriteByte(bus, addr7, CCS811_REG_MEAS_MODE,
                            (uint8_t)SYS_CCS811_MEAS_MODE_DEFAULT);
    if (err != SYS_I2C_OK) return (uint8_t)SYS_CCS811_ERR_PARAM;

    /* 启动成功: 把上次读取时间设为当前时刻,
     * 首次 DataReady 会等满一个测量周期再返回 1,避免刚启动就读到旧数据。 */
    s_last_read_ms[bus] = SYS_TICK_GetTick();

    return SYS_CCS811_OK;
}


/* 读取算法结果 */
int SYS_CCS811_Read(SysI2cId_t bus, SYS_CCS811_Data_t *out)
{
    uint8_t d[4];
    uint8_t st = 0U;
    uint8_t eid = 0U;
    int err;

    if (bus >= SYS_I2C_COUNT || out == 0) return SYS_CCS811_ERR_PARAM;

    /* 一次突发读 4 字节(0x02~0x05,寄存器地址自增):
     * eCO2 与 TVOC 取自同一次算法输出,分开读会得到不同时刻的值。 */
    err = SYS_I2C_ReadBytes(bus, s_addr[bus], CCS811_REG_ALG_RESULT, d, 4U);
    if (err != SYS_I2C_OK) return err;

    /* 大端: 高字节在前 */
    out->eco2_ppm = (uint16_t)(((uint16_t)d[0] << 8) | (uint16_t)d[1]);
    out->tvoc_ppb = (uint16_t)(((uint16_t)d[2] << 8) | (uint16_t)d[3]);

    /* 读取时的 STATUS / ERROR_ID 一并放进结构体,便于排错。
     * 读失败不算致命(数据已取得),对应字段置 0。 */
    if (SYS_I2C_ReadByte(bus, s_addr[bus], CCS811_REG_STATUS, &st) != SYS_I2C_OK) {
        st = 0U;
    }
    if (SYS_I2C_ReadByte(bus, s_addr[bus], CCS811_REG_ERROR_ID, &eid) != SYS_I2C_OK) {
        eid = 0U;
    }
    out->status   = st;
    out->error_id = eid;

    /* valid: APP_VALID 缺失或器件报错即为无效
     * (数据照填,调用方应据此丢弃) */
    out->valid = ((st & CCS811_STATUS_APP_VALID) != 0U &&
                  (st & CCS811_STATUS_ERROR) == 0U) ? 1U : 0U;

    /* 读数成功,刷新时间戳: DataReady 的时间判据以成功读取为起点 */
    s_last_read_ms[bus] = SYS_TICK_GetTick();

    return SYS_CCS811_OK;
}


/* 扩展功能 */
uint8_t SYS_CCS811_DataReady(SysI2cId_t bus)
{
    uint8_t mode = 0U;
    uint32_t period;

    if (bus >= SYS_I2C_COUNT) return 0U;

    /* 读当前测量模式,换算成数据更新周期
     * 读不到模式时按兜底周期算。 */
    if (SYS_I2C_ReadByte(bus, s_addr[bus], CCS811_REG_MEAS_MODE, &mode) != SYS_I2C_OK) {
        period = SYS_CCS811_DRDY_FALLBACK_MS;
    } else {
        period = ccs811_meas_period_ms(mode);
        if (period == 0U) {
            period = SYS_CCS811_DRDY_FALLBACK_MS;
        }
    }

    /* 时间判据: 距上次成功读取是否已过一个测量周期
     * 用无符号差值计算,计数器回绕(约 49.7 天)时结果依然正确。 */
    return (SYS_TICK_Elapsed(s_last_read_ms[bus]) >= period) ? 1U : 0U;
}

int SYS_CCS811_SetEnvData(SysI2cId_t bus, float temp_c, float humi_rh)
{
    uint8_t d[4];
    int32_t t, h;

    if (bus >= SYS_I2C_COUNT) return SYS_CCS811_ERR_PARAM;

    /* 钳位到手册允许范围: 湿度 0 ~ 100 %RH;温度 -25 ~ 50 ℃
     * (编码时温度加 25 偏移后为 0 ~ 75)。 */
    if (humi_rh > 100.0f) humi_rh = 100.0f;
    if (humi_rh < 0.0f)    humi_rh = 0.0f;
    if (temp_c > 50.0f)    temp_c = 50.0f;
    if (temp_c < -25.0f)   temp_c = -25.0f;

    /* 编码: 湿度 = RH × 512;温度 = (T + 25) × 512;各 2 字节大端。
     * 加 0.5f 作整数四舍五入。 */
    h = (int32_t)(humi_rh * 512.0f + 0.5f);
    t = (int32_t)((temp_c + 25.0f) * 512.0f + 0.5f);

    d[0] = (uint8_t)(((uint32_t)h >> 8) & 0xFFU);   /* 湿度高字节 */
    d[1] = (uint8_t)((uint32_t)h & 0xFFU);          /* 湿度低字节 */
    d[2] = (uint8_t)(((uint32_t)t >> 8) & 0xFFU);   /* 温度高字节 */
    d[3] = (uint8_t)((uint32_t)t & 0xFFU);          /* 温度低字节 */

    return SYS_I2C_WriteBytes(bus, s_addr[bus], CCS811_REG_ENV_DATA, d, 4U);
}

int SYS_CCS811_GetBaseline(SysI2cId_t bus, uint16_t *out)
{
    uint8_t d[2];
    int err;

    if (bus >= SYS_I2C_COUNT || out == 0) return SYS_CCS811_ERR_PARAM;

    err = SYS_I2C_ReadBytes(bus, s_addr[bus], CCS811_REG_BASELINE, d, 2U);
    if (err != SYS_I2C_OK) return err;

    *out = (uint16_t)(((uint16_t)d[0] << 8) | (uint16_t)d[1]);   /* 大端 */
    return SYS_CCS811_OK;
}

int SYS_CCS811_SetBaseline(SysI2cId_t bus, uint16_t baseline)
{
    uint8_t d[2];

    if (bus >= SYS_I2C_COUNT) return SYS_CCS811_ERR_PARAM;

    d[0] = (uint8_t)((baseline >> 8) & 0xFFU);   /* 大端,与 Get 对称 */
    d[1] = (uint8_t)(baseline & 0xFFU);

    return SYS_I2C_WriteBytes(bus, s_addr[bus], CCS811_REG_BASELINE, d, 2U);
}
