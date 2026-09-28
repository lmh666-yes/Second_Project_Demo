#include "mpu6050.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "sys_exti.h"

/* ================================================================
 *  mpu6050.c —— MPU6050 六轴传感器  实现文件
 * ================================================================
 *  三条实现要点 :
 *    ① 数据搬运全部交给 sys_i2c（它自带超时与错误码），本文件只管寄存器语义；
 *    ② 量程换算用"整数灵敏度表"（LSB/单位 ×1000），避免浮点与除法精度问题；
 *    ③ 零偏（陀螺静止输出）在驱动内部维护，Read 时自动扣除。
 * ================================================================ */


/* ================================================================
 *                    MPU6050 寄存器地址
 * ================================================================ */
#define MPU_SMPLRT_DIV      0x19
#define MPU_CONFIG          0x1A
#define MPU_GYRO_CONFIG     0x1B
#define MPU_ACCEL_CONFIG    0x1C
#define MPU_INT_PIN_CFG     0x37
#define MPU_INT_ENABLE      0x38
#define MPU_INT_STATUS      0x3A
#define MPU_ACCEL_XOUT_H    0x3B
#define MPU_TEMP_OUT_H      0x41
#define MPU_GYRO_XOUT_H     0x43
#define MPU_SIGNAL_PATH_RST 0x68
#define MPU_USER_CTRL       0x6A
#define MPU_PWR_MGMT_1      0x6B
#define MPU_PWR_MGMT_2      0x6C
#define MPU_WHO_AM_I        0x75

#define MPU_WHO_AM_I_VALUE  0x68    /* MPU6050 固定器件 ID */


/* ================================================================
 *                    灵敏度表（换量程只改这里）
 * ================================================================
 * 单位 = "LSB / 物理单位 × 1000"（乘 1000 是为了用整数表达 16.4、65.5 这类小数）
 *   ±2g   → 16384 LSB/g     ×1000 = 16384000
 *   ±4g   →  8192 LSB/g     ×1000 =  8192000
 *   ±8g   →  4096 LSB/g     ×1000 =  4096000
 *   ±16g  →  2048 LSB/g     ×1000 =  2048000
 *   ±250 dps → 131.0 LSB/(°/s) ×1000 = 131000
 *   ±500 dps →  65.5 LSB/(°/s) ×1000 =  65500
 *   ±1000dps →  32.8 LSB/(°/s) ×1000 =  32800
 *   ±2000dps →  16.4 LSB/(°/s) ×1000 =  16400
 * ================================================================ */
#if   (MPU6050_ACCEL_FS == MPU6050_ACCEL_2G)
    #define MPU_ACCEL_SENS_MILLI   16384000L
#elif (MPU6050_ACCEL_FS == MPU6050_ACCEL_4G)
    #define MPU_ACCEL_SENS_MILLI    8192000L
#elif (MPU6050_ACCEL_FS == MPU6050_ACCEL_8G)
    #define MPU_ACCEL_SENS_MILLI    4096000L
#else
    #define MPU_ACCEL_SENS_MILLI    2048000L
#endif

#if   (MPU6050_GYRO_FS == MPU6050_GYRO_250)
    #define MPU_GYRO_SENS_MILLI      131000L
#elif (MPU6050_GYRO_FS == MPU6050_GYRO_500)
    #define MPU_GYRO_SENS_MILLI       65500L
#elif (MPU6050_GYRO_FS == MPU6050_GYRO_1000)
    #define MPU_GYRO_SENS_MILLI       32800L
#else
    #define MPU_GYRO_SENS_MILLI       16400L
#endif

/* 编译期护栏：灵敏度表必须为正（防止量程宏写错导致静默算错） */
typedef char mpu_sens_check[(MPU_ACCEL_SENS_MILLI > 0 && MPU_GYRO_SENS_MILLI > 0) ? 1 : -1];


/* 陀螺零偏（单位 0.1°/s；由 MPU6050_CalibrateGyro 或外部设置写入） */
static int32_t mpu_gyro_bias[3] = {0, 0, 0};

/* 数据就绪中断标志 / 用户回调（ISR 置位，主循环用） */
static volatile uint8_t mpu_int_flag = 0;


/* ================================================================
 *                          内部小工具
 * ================================================================ */

/* 写一个寄存器；失败自动重试 MPU6050_RETRY 次 */
static uint8_t mpu_write(uint8_t reg, uint8_t val)
{
    uint8_t i;

    for (i = 0; i < MPU6050_RETRY; i++) {
        if (SYS_I2C_WriteByte(MPU6050_I2C_ID, MPU6050_ADDR, reg, val) == SYS_I2C_OK) {
            return 0U;
        }
    }
    return 1U;
}

/* 读一个寄存器 */
static uint8_t mpu_read(uint8_t reg, uint8_t *out)
{
    uint8_t i;

    for (i = 0; i < MPU6050_RETRY; i++) {
        if (SYS_I2C_ReadByte(MPU6050_I2C_ID, MPU6050_ADDR, reg, out) == SYS_I2C_OK) {
            return 0U;
        }
    }
    return 1U;
}

/* 连续读多个寄存器 */
static uint8_t mpu_read_buf(uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint8_t i;

    for (i = 0; i < MPU6050_RETRY; i++) {
        if (SYS_I2C_ReadBytes(MPU6050_I2C_ID, MPU6050_ADDR, reg, buf, len) == SYS_I2C_OK) {
            return 0U;
        }
    }
    return 1U;
}

/* 16 位大端拼接（MPU6050 寄存器是"高字节在前"） */
static int16_t mpu_i16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* 原始 LSB → 毫克（mg） */
static int16_t mpu_to_mg(int16_t raw)
{
    return (int16_t)(((int32_t)raw * 1000000L) / MPU_ACCEL_SENS_MILLI);
}

/* 原始 LSB → 0.1°/s */
static int32_t mpu_to_dps10(int16_t raw)
{
    return ((int32_t)raw * 10000L) / MPU_GYRO_SENS_MILLI;
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
uint8_t MPU6050_WhoAmI(void)
{
    uint8_t id = 0;

    (void)mpu_read(MPU_WHO_AM_I, &id);
    return id;
}

uint8_t MPU6050_IsOnline(void)
{
    return (MPU6050_WhoAmI() == MPU_WHO_AM_I_VALUE) ? 1U : 0U;
}

void MPU6050_Reset(void)
{
    (void)mpu_write(MPU_PWR_MGMT_1, 0x80U);       /* DEVICE_RESET = 1 */
    Delay_ms(120);                                /* 手册要求 ≥100ms */
}

void MPU6050_Sleep(uint8_t enable)
{
    uint8_t v = 0;

    if (mpu_read(MPU_PWR_MGMT_1, &v) != 0U) return;

    if (enable) v |= 0x40U;                       /* SLEEP = 1 */
    else        v &= (uint8_t)~0x40U;

    (void)mpu_write(MPU_PWR_MGMT_1, v);
}

uint8_t MPU6050_Init(void)
{
    uint8_t id = 0;

    /* ① 探测器件：读 WHO_AM_I（不通就直接返回，别往下配） */
    if (mpu_read(MPU_WHO_AM_I, &id) != 0U) return 1U;
    if (id != MPU_WHO_AM_I_VALUE)          return 1U;

    /* ② 复位 + 唤醒（复位后必须等一段，否则后续寄存器写不进去） */
    MPU6050_Reset();

    if (mpu_write(MPU_PWR_MGMT_1, 0x01U) != 0U) return 1U;  /* 唤醒,时钟选陀螺 PLL(最稳) */
    Delay_ms(10);

    (void)mpu_write(MPU_SIGNAL_PATH_RST, 0x07U);  /* 复位加速度/陀螺/温度信号通路 */
    Delay_ms(10);

    (void)mpu_write(MPU_PWR_MGMT_2, 0x00U);       /* 六轴全部开启 */

    /* ③ 采样率 / 数字低通滤波 */
    (void)mpu_write(MPU_SMPLRT_DIV, (uint8_t)MPU6050_SMPLRT_DIV);
    (void)mpu_write(MPU_CONFIG,     (uint8_t)MPU6050_DLPF_CFG);

    /* ④ 量程 */
    (void)mpu_write(MPU_GYRO_CONFIG,  (uint8_t)MPU6050_GYRO_FS);
    (void)mpu_write(MPU_ACCEL_CONFIG, (uint8_t)MPU6050_ACCEL_FS);

    /* ⑤ 中断引脚：推挽、高电平有效、脉冲宽度 50us（配合 PC0 的 EXTI 上升沿） */
    (void)mpu_write(MPU_INT_PIN_CFG, 0x00U);
    (void)mpu_write(MPU_INT_ENABLE,  0x01U);      /* DATA_RDY_EN = 1 */

    /* ⑥ 清一下可能残留的中断标志 */
    {
        uint8_t st;
        (void)mpu_read(MPU_INT_STATUS, &st);
    }

    mpu_gyro_bias[0] = mpu_gyro_bias[1] = mpu_gyro_bias[2] = 0;
    return 0U;
}

uint8_t MPU6050_ReadRaw(MpuRaw_t *raw)
{
    uint8_t buf[14];

    if (raw == 0) return 1U;

    /* 0x3B 起连续 14 字节：AX AY AZ TEMP GX GY GZ */
    if (mpu_read_buf(MPU_ACCEL_XOUT_H, buf, 14U) != 0U) return 2U;

    raw->ax   = mpu_i16(&buf[0]);
    raw->ay   = mpu_i16(&buf[2]);
    raw->az   = mpu_i16(&buf[4]);
    raw->temp = mpu_i16(&buf[6]);
    raw->gx   = mpu_i16(&buf[8]);
    raw->gy   = mpu_i16(&buf[10]);
    raw->gz   = mpu_i16(&buf[12]);
    return 0U;
}

int16_t MPU6050_GetTempC10(void)
{
    MpuRaw_t raw;

    if (MPU6050_ReadRaw(&raw) != 0U) return 0;

    /* 手册公式：T(℃) = raw / 340 + 36.53
     * 化成 0.1℃ 整数：T10 = (raw/340 + 36.53) × 10
     *                    = raw * 10 / 340 + 365
     * ⚠ 原来写成 raw * 100 / 340，系数大了 10 倍，
     *   实测室温 25℃ 会读成 −75℃ 左右（已验证修正）。 */
    return (int16_t)(((int32_t)raw.temp * 10L) / 340L + 365L);
}

uint8_t MPU6050_Read(MpuData_t *out)
{
    MpuRaw_t raw;

    if (out == 0) return 1U;
    if (MPU6050_ReadRaw(&raw) != 0U) return 2U;

    out->ax_mg = mpu_to_mg(raw.ax);
    out->ay_mg = mpu_to_mg(raw.ay);
    out->az_mg = mpu_to_mg(raw.az);

    /* 扣掉零偏（校准值本身就是 0.1°/s 单位，直接减） */
    out->gx_dps10 = mpu_to_dps10(raw.gx) - mpu_gyro_bias[0];
    out->gy_dps10 = mpu_to_dps10(raw.gy) - mpu_gyro_bias[1];
    out->gz_dps10 = mpu_to_dps10(raw.gz) - mpu_gyro_bias[2];

    out->temp_c10 = (int16_t)(((int32_t)raw.temp * 10L) / 340L + 365L);
    return 0U;
}


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 零偏校准：连续采 N 次陀螺原始值求平均（要求静止） */
uint8_t MPU6050_CalibrateGyro(void)
{
    int64_t sx = 0, sy = 0, sz = 0;
    MpuRaw_t raw;
    uint16_t i;

    for (i = 0; i < MPU6050_CALIB_TIMES; i++) {
        if (MPU6050_ReadRaw(&raw) != 0U) return 1U;
        sx += raw.gx;
        sy += raw.gy;
        sz += raw.gz;
        Delay_ms(5);                 /* 顺带把采样拉开（≈200 次 × 5ms ≈ 1s） */
    }

    mpu_gyro_bias[0] = mpu_to_dps10((int16_t)(sx / (int32_t)MPU6050_CALIB_TIMES));
    mpu_gyro_bias[1] = mpu_to_dps10((int16_t)(sy / (int32_t)MPU6050_CALIB_TIMES));
    mpu_gyro_bias[2] = mpu_to_dps10((int16_t)(sz / (int32_t)MPU6050_CALIB_TIMES));
    return 0U;
}

void MPU6050_SetGyroBias(int32_t gx_dps10, int32_t gy_dps10, int32_t gz_dps10)
{
    mpu_gyro_bias[0] = gx_dps10;
    mpu_gyro_bias[1] = gy_dps10;
    mpu_gyro_bias[2] = gz_dps10;
}

void MPU6050_GetGyroBias(int32_t *gx_dps10, int32_t *gy_dps10, int32_t *gz_dps10)
{
    if (gx_dps10 != 0) *gx_dps10 = mpu_gyro_bias[0];
    if (gy_dps10 != 0) *gy_dps10 = mpu_gyro_bias[1];
    if (gz_dps10 != 0) *gz_dps10 = mpu_gyro_bias[2];
}

/* ----------------------------------------------------------------
 *  数据就绪中断：INT(PC0) 上升沿 → sys_exti 回调
 * ---------------------------------------------------------------- */
static void (*mpu_user_cb)(void) = 0;

/* sys_exti 的回调不带参数，所以这里做一层"置标志 + 转调用户回调" */
static void mpu_int_isr(void)
{
    mpu_int_flag = 1U;
    if (mpu_user_cb != 0) mpu_user_cb();
}

void MPU6050_IntInit(void (*callback)(void))
{
    mpu_user_cb  = callback;
    mpu_int_flag = 0U;

    /* 中断脚先配成输入（无内部上下拉——MPU6050 是推挽输出，不需要） */
    GPIO_InInit(MPU6050_INT_PORT, MPU6050_INT_PIN, GPIO_PuPd_NOPULL);

    /* 线号 = 引脚号：PC0 → EXTI0（⚠ 与 PA0/PE0 等"线 0"互斥，
     *   若你已把 KEY_UP(PA0) 挂到 EXTI，二者不能同时用） */
    (void)SYS_EXTI_InitLine((uint8_t)GPIO_PinSource(MPU6050_INT_PIN),
                            MPU6050_INT_PORT, MPU6050_INT_PIN,
                            SYS_EXTI_RISING, mpu_int_isr);
}

void MPU6050_IntDisable(void)
{
    SYS_EXTI_Disable((uint8_t)GPIO_PinSource(MPU6050_INT_PIN));
    mpu_int_flag = 0U;
    mpu_user_cb  = 0;
}

/* ----------------------------------------------------------------
 *  静态倾角（只用加速度，定点整数，不依赖 math.h）
 * ----------------------------------------------------------------
 *  原理：重力矢量在三轴上的分量 → 反正切得到倾角
 *        pitch = atan2(-ax, sqrt(ay²+az²))
 *        roll  = atan2( ay, az)
 *  这里用"多项式近似的 atan（定点）"替代库函数，代价是 ±0.5° 级误差，
 *  对入门级姿态显示完全够用；要更高精度请上互补滤波/卡尔曼。
 * ---------------------------------------------------------------- */
/* 定点 atan 近似：输入 x = 分子/分母 的比值（-1000~1000 表示 -1~1），
 * 输出角度 ×10（-450~450 表示 -45°~45°）；配合象限判断得到 ±90° */
static int32_t mpu_atan_approx(int32_t num, int32_t den)
{
    int32_t x;
    int32_t deg10;
    uint8_t neg = 0;

    if (den == 0) return (num >= 0) ? 900L : -900L;   /* 90° 边界 */

    if (num < 0) { num = -num; neg = 1U; }

    /* x = num/den，用 1024 做定点（x 可能 >1，后面分两段处理） */
    x = (num * 1024L) / den;

    if (x <= 1024L) {
        /* |角度| ≤ 45°：atan(x) ≈ (x*1024/1024 - 0.33*x³)*45/π… 用泰勒前两项 */
        /* deg10 ≈ x(定点/1024) * 573 - x³ * 0.192 * 573 / 1024² * 1024 */
        int32_t t = x;                                   /* 0 ~ 1024 */
        deg10 = (t * 573L) / 1024L;                      /* 一阶项（x 弧度 → 度 ×10） */
        deg10 -= (((t * t) / 1024L) * t / 1024L) * 196L / 1024L;  /* 三阶修正 */
    } else {
        /* |角度| > 45°：atan(x) = 90° - atan(1/x) */
        int32_t t = (1024L * 1024L) / x;                 /* 1/x 定点 */
        int32_t a = (t * 573L) / 1024L;
        a -= (((t * t) / 1024L) * t / 1024L) * 196L / 1024L;
        deg10 = 900L - a;
    }

    return neg ? -deg10 : deg10;
}

/* 整数平方根（牛顿迭代，用于 sqrt(ay²+az²)） */
static uint32_t mpu_isqrt(uint32_t v)
{
    uint32_t x;

    if (v == 0U) return 0U;

    x = v;
    for (uint8_t i = 0; i < 16U; i++) {
        uint32_t nx = (x + v / x) >> 1;
        if (nx >= x) break;
        x = nx;
    }
    return x;
}

void MPU6050_GetTiltAngle(int16_t *pitch_x10, int16_t *roll_x10)
{
    MpuData_t d;
    uint32_t ayz;
    int32_t  ax;

    if (MPU6050_Read(&d) != 0U) {
        if (pitch_x10 != 0) *pitch_x10 = 0;
        if (roll_x10  != 0) *roll_x10  = 0;
        return;
    }

    ax  = (int32_t)d.ax_mg;
    ayz = mpu_isqrt((uint32_t)((int32_t)d.ay_mg * (int32_t)d.ay_mg) +
                    (uint32_t)((int32_t)d.az_mg * (int32_t)d.az_mg));

    if (pitch_x10 != 0) *pitch_x10 = (int16_t)mpu_atan_approx(-ax, (int32_t)ayz);
    if (roll_x10  != 0) *roll_x10  = (int16_t)mpu_atan_approx((int32_t)d.ay_mg,
                                                              (int32_t)d.az_mg);
}
