#ifndef __FWLIB_SYS_MPU6050_H
#define __FWLIB_SYS_MPU6050_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* sys_mpu6050.h: 板载 MPU6050(三轴加速度 / 三轴陀螺 / 温度)驱动
 * 接线: I2C1(PB8 = SCL / PB9 = SDA,与 24C02 同总线),器件地址 0x68(AD0 接地)
 * 约定: 加速度 g、角速度 °/s、温度 ℃;寄存器读写经 sys_i2c,寄存器映射与量程位域见手册
 * 姿态解算与滤波留在上层(见 sys_filter) */


/* 区块 1: 配置宏(换板只改此区) */
/* 器件 7 位地址(AD0 接地 = 0x68,接高改 0x69) */
#define SYS_MPU6050_ADDR        SYS_I2C_ADDR_MPU6050    /* 0x68 */

/* 陀螺量程: 0=±250 1=±500 2=±1000 3=±2000 (°/s)
 * 灵敏度: 131 / 65.5 / 32.8 / 16.4 LSB/(°/s) */
#define SYS_MPU6050_GYRO_FS     0

/* 加计量程: 0=±2g 1=±4g 2=±8g 3=±16g
 * 灵敏度: 16384 / 8192 / 4096 / 2048 LSB/g;静态倾角取 ±2g */
#define SYS_MPU6050_ACCEL_FS    0

/* 采样分频: 采样率 = 1kHz / (1 + 该值);7 → 125Hz */
#define SYS_MPU6050_SMPLRT_DIV  7


/* 区块 2: 基础功能 */
/* 原始 16 位读数,未换算;温度: ℃ = temp/340 + 36.53 */
typedef struct {
    int16_t ax, ay, az;     /* 加速度原始值 */
    int16_t temp;           /* 温度原始值: ℃ = temp/340 + 36.53 */
    int16_t gx, gy, gz;     /* 陀螺原始值 */
} SYS_MPU6050_Raw_t;

/* 初始化: 读 0x75 自检后唤醒,按区块 1 宏配置量程与采样率
 * 参数: bus = I2C 总线号(合法值见 sys_i2c.h)
 * 返回: 0 成功;1 = 无应答 / WHO_AM_I != 0x68 / 配置寄存器写失败 / 量程回读不符;2 = 总线号非法
 * 依据: 写 0x6B=0x01(PLL 时钟源,退出睡眠)、0x19、0x1A=0x03(DLPF 44Hz)、0x1B/0x1C(量程位域 4:3),
 *       末回读 0x1C 复核 */
uint8_t SYS_MPU6050_Init(SysI2cId_t bus);

/* 读一次原始数据(加速度 xyz + 温度 + 陀螺 xyz,共 7 个 int16)
 * 参数: out 非空,输出 14 字节数据区的解析结果
 * 返回: SYS_I2C_OK(0) 成功;负数 = I2C 错误码(见 sys_i2c.h)
 * 依据: 从 0x3B 起突发读 14 字节(地址自增),保证同一采样时刻 */
int SYS_MPU6050_ReadRaw(SysI2cId_t bus, SYS_MPU6050_Raw_t *out);


/* 区块 3: 扩展功能 */
/* 物理量数据(换算后) */
typedef struct {
    float ax, ay, az;       /* 加速度,单位 g(1g ≈ 9.8m/s²) */
    float gx, gy, gz;       /* 角速度,单位 °/s */
    float temp_c;           /* 温度,单位 ℃ */
} SYS_MPU6050_Data_t;

/* 读一次并换算为物理量(灵敏度按区块 1 量程宏选取)
 * 参数: out 非空,输出 g / °/s / ℃
 * 返回: 0 成功;负数 = I2C 错误码 */
int SYS_MPU6050_Read(SysI2cId_t bus, SYS_MPU6050_Data_t *out);

/* 静态倾角(加速度反算): pitch 俯仰角(绕 Y 轴), roll 横滚角(绕 X 轴),单位 度
 * 参数: pitch_deg / roll_deg 可为 NULL,允许只取其中一个
 * 返回: 0 成功;负数 = I2C 错误码
 * 依据: roll = atan2(ay, az), pitch = atan2(-ax, sqrt(ay²+az²));静止/慢速准确,
 *       受运动加速度干扰,动态角度需上层互补滤波(见 sys_filter) */
int SYS_MPU6050_ReadPitchRoll(SysI2cId_t bus, float *pitch_deg, float *roll_deg);

/* 读 WHO_AM_I(0x75,正常值 0x68),用于确认器件型号
 * 返回: 0~255;读失败或总线号非法返回 -1 */
int SYS_MPU6050_GetID(SysI2cId_t bus);

#endif /* __FWLIB_SYS_MPU6050_H */
