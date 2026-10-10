#include "sys_mpu6050.h"

#include <math.h>

/*  sys_mpu6050.c : 板载六轴传感器 MPU6050 实现
 *  初始化 = WHO_AM_I 自检 + 5 个配置寄存器(唤醒/采样率/滤波/量程);
 *  读数据 = 0x3B 起一次突发读 14 字节(地址自增),保证同一次采样。
 *  寄存器(手册寄存器映射):0x19 SMPLRT_DIV 采样分频;0x1A CONFIG DLPF;
 *  0x1B GYRO_CONFIG 陀螺量程(位 4:3);0x1C ACCEL_CONFIG 加计量程(位 4:3);
 *  0x3B~0x48 数据区 14 字节(ax/ay/az/temp/gx/gy/gz 大端);
 *  0x6B PWR_MGMT_1 电源管理(位 6 = 睡眠);0x75 WHO_AM_I 恒 0x68。
 *  寄存器含义与量程位域见寄存器映射手册;互补滤波公式见同名 .h。 */


/* 内部数据表 */
/* 灵敏度表(LSB 每物理单位) */
static const float mpu_gyro_lsb[4]  = { 131.0f, 65.5f, 32.8f, 16.4f };   /* LSB/(°/s) */
static const float mpu_accel_lsb[4] = { 16384.0f, 8192.0f, 4096.0f, 2048.0f }; /* LSB/g */

/* 寄存器地址 */
#define MPU_SMPLRT_DIV      0x19
#define MPU_CONFIG          0x1A
#define MPU_GYRO_CONFIG     0x1B
#define MPU_ACCEL_CONFIG    0x1C
#define MPU_ACCEL_XOUT_H    0x3B
#define MPU_PWR_MGMT_1      0x6B
#define MPU_WHO_AM_I        0x75
#define MPU_WHO_AM_I_VALUE  0x68


uint8_t SYS_MPU6050_Init(SysI2cId_t bus)
{
    uint8_t who = 0U;

    if (bus >= SYS_I2C_COUNT) return 2U;

    SYS_I2C_Init(bus, 400000);

    /* 自检:读得到 且 WHO_AM_I = 0x68 才算在线 */
    if (SYS_I2C_ReadByte(bus, SYS_MPU6050_ADDR, MPU_WHO_AM_I, &who) != SYS_I2C_OK) return 1U;
    if (who != MPU_WHO_AM_I_VALUE) return 1U;

    /* 唤醒 + 时钟源 = PLL(陀螺 X);采样分频;DLPF 44Hz;量程 <<3 对齐手册位域。
     * 五条写均检查返回值:任一条失败即 return 1,调用方据此不进入配置完成态。 */
    if (SYS_I2C_WriteByte(bus, SYS_MPU6050_ADDR, MPU_PWR_MGMT_1, 0x01U) != SYS_I2C_OK) return 1U;
    if (SYS_I2C_WriteByte(bus, SYS_MPU6050_ADDR, MPU_SMPLRT_DIV,
                          SYS_MPU6050_SMPLRT_DIV) != SYS_I2C_OK) return 1U;
    if (SYS_I2C_WriteByte(bus, SYS_MPU6050_ADDR, MPU_CONFIG, 0x03U) != SYS_I2C_OK) return 1U;
    if (SYS_I2C_WriteByte(bus, SYS_MPU6050_ADDR, MPU_GYRO_CONFIG,
                          (uint8_t)(SYS_MPU6050_GYRO_FS << 3)) != SYS_I2C_OK) return 1U;
    if (SYS_I2C_WriteByte(bus, SYS_MPU6050_ADDR, MPU_ACCEL_CONFIG,
                          (uint8_t)(SYS_MPU6050_ACCEL_FS << 3)) != SYS_I2C_OK) return 1U;

    /* 回读 ACCEL_CONFIG 确认量程写入生效 */
    {
        uint8_t chk = 0U;
        if (SYS_I2C_ReadByte(bus, SYS_MPU6050_ADDR, MPU_ACCEL_CONFIG, &chk) != SYS_I2C_OK) return 1U;
        if (chk != (uint8_t)(SYS_MPU6050_ACCEL_FS << 3)) return 1U;
    }
    return 0U;
}

int SYS_MPU6050_ReadRaw(SysI2cId_t bus, SYS_MPU6050_Raw_t *out)
{
    uint8_t raw[14];
    int     err;

    if (bus >= SYS_I2C_COUNT || out == 0) return SYS_I2C_ERR_PARAM;

    /* 突发读(地址自增),14 字节来自同一次采样 */
    err = SYS_I2C_ReadBytes(bus, SYS_MPU6050_ADDR, MPU_ACCEL_XOUT_H, raw, 14U);
    if (err != SYS_I2C_OK) return err;

    out->ax   = (int16_t)(((uint16_t)raw[0]  << 8) | raw[1]);
    out->ay   = (int16_t)(((uint16_t)raw[2]  << 8) | raw[3]);
    out->az   = (int16_t)(((uint16_t)raw[4]  << 8) | raw[5]);
    out->temp = (int16_t)(((uint16_t)raw[6]  << 8) | raw[7]);
    out->gx   = (int16_t)(((uint16_t)raw[8]  << 8) | raw[9]);
    out->gy   = (int16_t)(((uint16_t)raw[10] << 8) | raw[11]);
    out->gz   = (int16_t)(((uint16_t)raw[12] << 8) | raw[13]);
    return SYS_I2C_OK;
}


int SYS_MPU6050_Read(SysI2cId_t bus, SYS_MPU6050_Data_t *out)
{
    SYS_MPU6050_Raw_t r;
    int err;

    if (bus >= SYS_I2C_COUNT || out == 0) return SYS_I2C_ERR_PARAM;

    err = SYS_MPU6050_ReadRaw(bus, &r);
    if (err != SYS_I2C_OK) return err;

    /* 换算:原始值 / 量程灵敏度(LSB 每单位);
     * 温度 = 原始值 / 340.0 + 36.53(手册温度换算) */
    out->ax = (float)r.ax / mpu_accel_lsb[SYS_MPU6050_ACCEL_FS];
    out->ay = (float)r.ay / mpu_accel_lsb[SYS_MPU6050_ACCEL_FS];
    out->az = (float)r.az / mpu_accel_lsb[SYS_MPU6050_ACCEL_FS];
    out->gx = (float)r.gx / mpu_gyro_lsb[SYS_MPU6050_GYRO_FS];
    out->gy = (float)r.gy / mpu_gyro_lsb[SYS_MPU6050_GYRO_FS];
    out->gz = (float)r.gz / mpu_gyro_lsb[SYS_MPU6050_GYRO_FS];
    out->temp_c = (float)r.temp / 340.0f + 36.53f;
    return SYS_I2C_OK;
}

int SYS_MPU6050_ReadPitchRoll(SysI2cId_t bus, float *pitch_deg, float *roll_deg)
{
    SYS_MPU6050_Raw_t r;
    float ax, ay, az;
    int   err;

    if (bus >= SYS_I2C_COUNT) return SYS_I2C_ERR_PARAM;

    err = SYS_MPU6050_ReadRaw(bus, &r);
    if (err != SYS_I2C_OK) return err;

    ax = (float)r.ax / mpu_accel_lsb[SYS_MPU6050_ACCEL_FS];
    ay = (float)r.ay / mpu_accel_lsb[SYS_MPU6050_ACCEL_FS];
    az = (float)r.az / mpu_accel_lsb[SYS_MPU6050_ACCEL_FS];

    /* 加速度倾角公式(度):roll = atan2(ay, az);
     * pitch = atan2(-ax, sqrt(ay^2 + az^2)) */
    if (roll_deg)  *roll_deg  = atan2f(ay, az) * 57.29578f;
    if (pitch_deg) *pitch_deg = atan2f(-ax, sqrtf(ay * ay + az * az)) * 57.29578f;
    return SYS_I2C_OK;
}

int SYS_MPU6050_GetID(SysI2cId_t bus)
{
    uint8_t who = 0U;

    if (bus >= SYS_I2C_COUNT) return -1;
    if (SYS_I2C_ReadByte(bus, SYS_MPU6050_ADDR, MPU_WHO_AM_I, &who) != SYS_I2C_OK) return -1;
    return (int)who;
}
