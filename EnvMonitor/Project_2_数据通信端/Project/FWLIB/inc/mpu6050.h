#ifndef __FWLIB_MPU6050_H
#define __FWLIB_MPU6050_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* MPU6050 六轴传感器（三轴加速度 + 三轴陀螺仪）。依赖 sys_i2c.h（I2C1）、
 * sys_exti.h（数据就绪中断）、gpio_core.h；数据搬运走 SYS_I2C_WriteBytes / ReadBytes。
 *
 * 天马 F407 开发板接线（普中-天马 F407 开发板原理图）：
 *   SCL = PB8  SDA = PB9，与板载 24C02 共用 I2C1，已有 4.7k 上拉
 *   AD0 = GND → 7 位地址 0x68
 *   INT = PC0，网络名 MPU_INT，推挽高电平有效，需自行用 sys_exti 接管
 *   PC0 在原理图上同时标了 MPU_INT 与 W_LED（WiFi 模块座 P3 的 LED 脚），
 *   插上 WiFi 模块时 PC0 被占用；用中断前先拔掉 WiFi 模块，或不用 INT 改轮询。
 * 换总线改 MPU6050_I2C_ID，换中断脚改 MPU6050_INT_PORT/PIN。 */


#define MPU6050_I2C_ID    SYS_I2C_1     /* 本板挂在 I2C1（PB8/PB9） */
/* AD0 接地 → 7 位地址 0x68；若 AD0 接 VCC 则改成 0x69 */
#define MPU6050_ADDR      0x68

#define MPU6050_INT_PORT   GPIOC
#define MPU6050_INT_PIN    GPIO_Pin_0

/* 加速度量程，括号内为对应灵敏度（LSB/g） */
#define MPU6050_ACCEL_2G    0x00    /* ±2g  ，16384 LSB/g */
#define MPU6050_ACCEL_4G    0x08    /* ±4g  ， 8192 LSB/g  */
#define MPU6050_ACCEL_8G    0x10    /* ±8g  ， 4096 LSB/g  */
#define MPU6050_ACCEL_16G   0x18    /* ±16g ， 2048 LSB/g  */

/* 陀螺仪量程，括号内为对应灵敏度（LSB/(°/s)） */
#define MPU6050_GYRO_250    0x00    /* ±250 °/s ，131.0 LSB/(°/s) */
#define MPU6050_GYRO_500    0x08    /* ±500 °/s ， 65.5 LSB/(°/s) */
#define MPU6050_GYRO_1000   0x10    /* ±1000°/s ， 32.8 LSB/(°/s) */
#define MPU6050_GYRO_2000   0x18    /* ±2000°/s ， 16.4 LSB/(°/s) */

/* 数字低通滤波 DLPF_CFG，逐行为加速度带宽 / 陀螺带宽 / 输出速率（1kHz 采样时） */
#define MPU6050_DLPF_260HZ  0x00    /* 260Hz / 256Hz / 8kHz */
#define MPU6050_DLPF_184HZ  0x01
#define MPU6050_DLPF_94HZ   0x02
#define MPU6050_DLPF_44HZ   0x03    /* 44Hz / 42Hz / 1kHz */
#define MPU6050_DLPF_21HZ   0x04
#define MPU6050_DLPF_10HZ   0x05
#define MPU6050_DLPF_5HZ    0x06    /* 5Hz / 5Hz / 1kHz */

/* 采样分频：采样率 = 1kHz / (1 + 分频)，0 → 1kHz */
#define MPU6050_SMPLRT_DIV  4       /* 200Hz 采样 */

/* 当前生效的量程，改上面三组量程宏即可，这里不用动 */
#define MPU6050_ACCEL_FS    MPU6050_ACCEL_8G
#define MPU6050_GYRO_FS     MPU6050_GYRO_2000
#define MPU6050_DLPF_CFG    MPU6050_DLPF_44HZ

/* 陀螺仪零偏校准采样次数，200 次 @200Hz 约 1 秒 */
#define MPU6050_CALIB_TIMES     200
#define MPU6050_RETRY           3

/* 编译期自检：量程宏必须是四个合法值之一 */
#if (MPU6050_ACCEL_FS != MPU6050_ACCEL_2G) && (MPU6050_ACCEL_FS != MPU6050_ACCEL_4G) && \
    (MPU6050_ACCEL_FS != MPU6050_ACCEL_8G) && (MPU6050_ACCEL_FS != MPU6050_ACCEL_16G)
#error "MPU6050_ACCEL_FS must be one of MPU6050_ACCEL_2G/4G/8G/16G"
#endif
#if (MPU6050_GYRO_FS != MPU6050_GYRO_250) && (MPU6050_GYRO_FS != MPU6050_GYRO_500) && \
    (MPU6050_GYRO_FS != MPU6050_GYRO_1000) && (MPU6050_GYRO_FS != MPU6050_GYRO_2000)
#error "MPU6050_GYRO_FS must be one of MPU6050_GYRO_250/500/1000/2000"
#endif


/* 原始数据，单位 LSB */
typedef struct {
    int16_t ax;     /* 加速度 X（LSB） */
    int16_t ay;     /* 加速度 Y */
    int16_t az;     /* 加速度 Z */
    int16_t temp;   /* 温度原始值 */
    int16_t gx;     /* 角速度 X（LSB） */
    int16_t gy;
    int16_t gz;
} MpuRaw_t;

/* 换算后的物理量，整数表示，不依赖 %f */
typedef struct {
    int16_t ax_mg;      /* 加速度，单位 mg（1000mg = 1g） */
    int16_t ay_mg;
    int16_t az_mg;
    int32_t gx_dps10;   /* 角速度，单位 0.1°/s，123 = 12.3°/s */
    int32_t gy_dps10;
    int32_t gz_dps10;
    int16_t temp_c10;   /* 温度，单位 0.1℃，253 = 25.3℃ */
} MpuData_t;

/* 复位、唤醒、设量程/滤波/采样率、开数据就绪中断使能
 * 返回 : 0 = 成功；1 = 器件无应答
 * 前提 : 先 SYS_I2C_Init(SYS_I2C_1, 400000) 开好总线
 * 说明 : 内部含约 120ms 复位等待（阻塞）；重复调用安全
 */
uint8_t MPU6050_Init(void);

/* 探测器件是否在线（读 WHO_AM_I 应为 0x68）
 * 返回 : 1 = 在线；0 = 不在线 */
uint8_t MPU6050_IsOnline(void);

/* 读 WHO_AM_I 寄存器（正常为 0x68，可用它确认 I2C 通不通） */
uint8_t MPU6050_WhoAmI(void);

/* 读 14 字节原始数据（加速度 6 + 温度 2 + 角速度 6，一次 I2C 传输）
 * 返回 : 0 = 成功；非 0 = sys_i2c 错误码 */
uint8_t MPU6050_ReadRaw(MpuRaw_t *raw);

/* 读并换算成物理量（内部先 ReadRaw，再按当前量程换算并扣除零偏）
 * 返回 : 0 = 成功；1 = 参数空指针；2 = I2C 读失败 */
uint8_t MPU6050_Read(MpuData_t *out);

/* 只读温度（℃×10，整数） */
int16_t MPU6050_GetTempC10(void);


/* 进入/退出睡眠：1 = 睡眠省电，0 = 正常工作
 * 睡眠后量测停止，读到的数据不再更新；醒来后需等一个采样周期 */
void MPU6050_Sleep(uint8_t enable);

/* 软复位器件，复位后配置丢失，需重新 MPU6050_Init */
void MPU6050_Reset(void);

/* 陀螺仪零偏校准，要求传感器保持静止
 * 说明 : 连续采样 CALIB_TIMES 次求平均，结果存驱动内部，后续 MPU6050_Read 自动扣除
 * 阻塞 : 约 CALIB_TIMES / 采样率 秒，默认 200 次 @200Hz 约 1s
 * 返回 : 0 = 成功；非 0 = I2C 读失败
 */
uint8_t MPU6050_CalibrateGyro(void);

/* 手动设置/读取陀螺零偏（单位 0.1°/s） */
void    MPU6050_SetGyroBias(int32_t gx_dps10, int32_t gy_dps10, int32_t gz_dps10);
void    MPU6050_GetGyroBias(int32_t *gx_dps10, int32_t *gy_dps10, int32_t *gz_dps10);

/* 初始化中断引脚并注册回调，在 MPU6050_Init 之后调用
 * 回调在中断上下文执行，需保持短小；中断只置标志，主循环再读数据 */
void    MPU6050_IntInit(void (*callback)(void));

/* 关闭数据就绪中断 */
void    MPU6050_IntDisable(void);

/* 只用加速度算静态倾角，单位 0.1°，静止准确，运动时会抖
 * 参数 : pitch_x10 为绕 X 轴倾角（前后倾），roll_x10 为绕 Y 轴倾角（左右倾），可传 0
 * 说明 : 使用定点 atan 近似与整数开方，不依赖 math.h 和浮点
 */
void    MPU6050_GetTiltAngle(int16_t *pitch_x10, int16_t *roll_x10);

#endif /* __FWLIB_MPU6050_H */
