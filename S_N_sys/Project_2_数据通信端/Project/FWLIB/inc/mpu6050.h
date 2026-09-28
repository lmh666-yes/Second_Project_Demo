#ifndef __FWLIB_MPU6050_H
#define __FWLIB_MPU6050_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* ================================================================
 *  mpu6050.h —— 【板载】MPU6050 六轴传感器（三轴加速度 + 三轴陀螺仪）头文件
 * ================================================================
 *  设计定位 : 器件级驱动（薄封装）—— 依赖 sys_i2c 的总线原语，
 *             对外只暴露"读原始值 / 读物理量 / 中断 / 零偏校准"
 *  依赖     : sys_i2c.h（I2C1 总线）、sys_exti.h（数据就绪中断）、gpio_core.h
 *  标准库关键词 : 无——数据搬运全部走 SYS_I2C_WriteBytes / ReadBytes
 *                 （寄存器时序在 sys_i2c 内部；中断走 sys_exti 的回调机制）
 *
 *  【天马 F407 开发板接线（普中-天马 F407开发板原理图）】
 *      SCL = PB8   SDA = PB9   （与板载 24C02 共用 I2C1，已有 4.7k 上拉）
 *      AD0 = GND   → 7 位地址 = 0x68
 *      INT = PC0   （网络名 MPU_INT，推挽/高电平有效，需自行用 sys_exti 接管）
 *  ⚠️ PC0 在原理图上**同时**标了 MPU_INT 与 W_LED 两个网络名——
 *     W_LED 是 WiFi（ESP8266）模块座 P3 的 LED 脚。
 *     也就是说：**插上 WiFi 模块时 PC0 会被它占用**，两功能不能同时用；
 *     用 MPU6050 中断前先把 WiFi 模块拔了（或不用 INT，轮询读数据）。
 *
 *  使用方式 :
 *      SYS_I2C_Init(SYS_I2C_1, 400000);        // ① 先开总线
 *      if (MPU6050_Init() == 0) {              // ② 初始化（0 = 成功）
 *          MpuData_t d;
 *          MPU6050_Read(&d);                   // ③ 读加速度/角速度/温度
 *      }
 *
 *  移植指引：换总线改 MPU6050_I2C_ID；换中断脚改 MPU6050_INT_PORT/PIN。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* -------------------- 总线与地址 -------------------- */
#define MPU6050_I2C_ID    SYS_I2C_1     /* 本板挂在 I2C1（PB8/PB9） */
/* AD0 接地 → 7 位地址 0x68；若 AD0 接 VCC 则改成 0x69 */
#define MPU6050_ADDR      0x68

/* -------------------- 中断脚 -------------------- */
#define MPU6050_INT_PORT   GPIOC
#define MPU6050_INT_PIN    GPIO_Pin_0

/* -------------------- 量程（改动后驱动自动换算，无需改代码） -------------------- */
/* 加速度量程：越大量程 → 分辨率越低（振动大、要测冲击时选大档） */
#define MPU6050_ACCEL_2G    0x00    /* ±2g  ，16384 LSB/g  ，最灵敏 */
#define MPU6050_ACCEL_4G    0x08    /* ±4g  ， 8192 LSB/g  */
#define MPU6050_ACCEL_8G    0x10    /* ±8g  ， 4096 LSB/g  */
#define MPU6050_ACCEL_16G   0x18    /* ±16g ， 2048 LSB/g  */

/* 陀螺仪量程：越大越适合快速转动（无人机/云台常用 ±2000dps） */
#define MPU6050_GYRO_250    0x00    /* ±250 °/s ，131.0 LSB/(°/s) */
#define MPU6050_GYRO_500    0x08    /* ±500 °/s ， 65.5 LSB/(°/s) */
#define MPU6050_GYRO_1000   0x10    /* ±1000°/s ， 32.8 LSB/(°/s) */
#define MPU6050_GYRO_2000   0x18    /* ±2000°/s ， 16.4 LSB/(°/s) */

/* 数字低通滤波（DLPF_CFG）：越小越平滑但延迟越大
 *   加速度带宽 / 陀螺带宽 / 输出速率(1kHz 采样时) */
#define MPU6050_DLPF_260HZ  0x00    /* 260Hz / 256Hz / 8kHz（几乎不滤） */
#define MPU6050_DLPF_184HZ  0x01
#define MPU6050_DLPF_94HZ   0x02
#define MPU6050_DLPF_44HZ   0x03    /* 44Hz / 42Hz / 1kHz ★推荐 */
#define MPU6050_DLPF_21HZ   0x04
#define MPU6050_DLPF_10HZ   0x05
#define MPU6050_DLPF_5HZ    0x06    /* 最平滑，适合慢速倾角测量 */

/* 采样分频：采样率 = 1kHz / (1 + 分频)，0 → 1kHz */
#define MPU6050_SMPLRT_DIV  4       /* 200Hz 采样，做倾角/姿态足够 */

/* -------------------- 当前生效的量程（改上面常量即可，这里不用动） ---------- */
#define MPU6050_ACCEL_FS    MPU6050_ACCEL_8G
#define MPU6050_GYRO_FS     MPU6050_GYRO_2000
#define MPU6050_DLPF_CFG    MPU6050_DLPF_44HZ

/* -------------------- 零偏校准 -------------------- */
/* 静止校准的采样次数（越多越准、越慢；200 次 ≈ 1 秒@200Hz） */
#define MPU6050_CALIB_TIMES     200
/* 校准/读取失败时重试次数 */
#define MPU6050_RETRY           3

/* -------------------- 编译期自检 -------------------- */
#if (MPU6050_ACCEL_FS != MPU6050_ACCEL_2G) && (MPU6050_ACCEL_FS != MPU6050_ACCEL_4G) && \
    (MPU6050_ACCEL_FS != MPU6050_ACCEL_8G) && (MPU6050_ACCEL_FS != MPU6050_ACCEL_16G)
#error "MPU6050_ACCEL_FS must be one of MPU6050_ACCEL_2G/4G/8G/16G"
#endif
#if (MPU6050_GYRO_FS != MPU6050_GYRO_250) && (MPU6050_GYRO_FS != MPU6050_GYRO_500) && \
    (MPU6050_GYRO_FS != MPU6050_GYRO_1000) && (MPU6050_GYRO_FS != MPU6050_GYRO_2000)
#error "MPU6050_GYRO_FS must be one of MPU6050_GYRO_250/500/1000/2000"
#endif


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 原始数据（未经换算，单位 = LSB） */
typedef struct {
    int16_t ax;     /* 加速度 X（LSB） */
    int16_t ay;     /* 加速度 Y */
    int16_t az;     /* 加速度 Z */
    int16_t temp;   /* 温度原始值 */
    int16_t gx;     /* 角速度 X（LSB） */
    int16_t gy;
    int16_t gz;
} MpuRaw_t;

/* 换算后的物理量（用整数表示，方便直接串口打印，不依赖 %f） */
typedef struct {
    int16_t ax_mg;      /* 加速度，单位 mg（1000mg = 1g） */
    int16_t ay_mg;
    int16_t az_mg;
    int32_t gx_dps10;   /* 角速度，单位 0.1°/s（如 123 = 12.3°/s） */
    int32_t gy_dps10;
    int32_t gz_dps10;
    int16_t temp_c10;   /* 温度，单位 0.1℃（如 253 = 25.3℃） */
} MpuData_t;

/* 初始化：复位 → 唤醒 → 设量程/滤波/采样率 → 开数据就绪中断使能
 * 返回 : 0 = 成功；1 = 器件无应答（查接线/供电/地址）
 * 前提 : 先 SYS_I2C_Init(SYS_I2C_1, 400000) 开好总线
 * 说明 : 内部含约 120ms 复位等待（阻塞）；重复调用安全
 * 标准库 : 全程走 sys_i2c 的寄存器读写，无额外标准库调用
 * 示例 : SYS_I2C_Init(SYS_I2C_1, 400000);
 *        if (MPU6050_Init() != 0) { 说明器件没接好，查接线 }
 *        -- 完整写法：if (MPU6050_Init() == 0) { 正常 } else { 查接线 } */
uint8_t MPU6050_Init(void);

/* 探测器件是否在线（读 WHO_AM_I 应为 0x68）
 * 返回 : 1 = 在线；0 = 不在线
 * 示例 : if (!MPU6050_IsOnline()) { printf("%s\r\n", SYS_I2C_ErrStr(SYS_I2C_ERR_ADDR)); } */
uint8_t MPU6050_IsOnline(void);

/* 读 WHO_AM_I 寄存器（正常应为 0x68；可用它确认 I2C 通不通） */
uint8_t MPU6050_WhoAmI(void);

/* 读 14 字节原始数据（加速度 6 + 温度 2 + 角速度 6，一次 I2C 传输搞定）
 * 返回 : 0 = 成功；非 0 = sys_i2c 错误码
 * 示例 : MpuRaw_t raw; if (MPU6050_ReadRaw(&raw) == 0) { ... } */
uint8_t MPU6050_ReadRaw(MpuRaw_t *raw);

/* 读并换算成物理量（内部先 ReadRaw，再按当前量程换算 + 扣零偏）
 * 返回 : 0 = 成功；1 = 参数空指针；2 = I2C 读失败
 * 示例 : MpuData_t d;
 *        if (MPU6050_Read(&d) == 0) {
 *            SYS_USART_Printf(SYS_USART_1, "ax=%d mg, temp=%d.%d C\r\n",
 *                             d.ax_mg, d.temp_c10 / 10, d.temp_c10 % 10);
 *        } */
uint8_t MPU6050_Read(MpuData_t *out);

/* 只读温度（℃×10，整数）
 * 示例 : int16_t t = MPU6050_GetTempC10();   // 253 = 25.3℃ */
int16_t MPU6050_GetTempC10(void);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* ---- 电源管理 ---- */
/* 进入/退出睡眠（1 = 睡眠省电，0 = 正常工作）
 * 说明 : 睡眠后量测停止，读到的数据不再更新；醒来后需等一个采样周期 */
void MPU6050_Sleep(uint8_t enable);

/* 软复位器件（复位后所有配置丢失，需重新 MPU6050_Init） */
void MPU6050_Reset(void);

/* ---- 零偏校准（静止校准，姿态解算前先做一次） ---- */
/* 陀螺仪零偏校准：要求传感器**保持静止**，内部连续采 CALIB_TIMES 次求平均
 * 说明 : 校准结果存在驱动内部，后续 MPU6050_Read 自动扣除
 * 阻塞 : 约 CALIB_TIMES / 采样率 秒（默认 200 次 @200Hz ≈ 1s）
 * 示例 : MPU6050_Init();
 *        Delay_ms(500);
 *        MPU6050_CalibrateGyro();     // 校准期间别碰板子 */
uint8_t MPU6050_CalibrateGyro(void);

/* 手动设置/读取陀螺零偏（单位 0.1°/s），可把校准值存 Flash 下次开机直接装载 */
void    MPU6050_SetGyroBias(int32_t gx_dps10, int32_t gy_dps10, int32_t gz_dps10);
void    MPU6050_GetGyroBias(int32_t *gx_dps10, int32_t *gy_dps10, int32_t *gz_dps10);

/* ---- 数据就绪中断（INT = PC0，走 sys_exti 回调） ---- */
/* 初始化中断引脚并注册回调（回调在中断上下文执行，保持短小）
 * 说明 : 在 MPU6050_Init 之后调用；中断只置标志，主循环再读数据最稳
 * 示例 : static volatile uint8_t g_ready = 0;
 *        static void on_mpu_int(void) { g_ready = 1; }
 *        MPU6050_IntInit(on_mpu_int);
 *        ...
 *        if (g_ready) { g_ready = 0; MPU6050_Read(&d); } */
void    MPU6050_IntInit(void (*callback)(void));

/* 关闭数据就绪中断 */
void    MPU6050_IntDisable(void);

/* ---- 姿态粗算（不做卡尔曼/互补滤波，仅倾斜角，够入门项目用） ---- */
/* 只用加速度算静态倾角（单位 0.1°，静止准确、运动时会抖）
 * 参数 : pitch_x10 —— 绕 X 轴倾角（前后倾），可传 0
 *        roll_x10  —— 绕 Y 轴倾角（左右倾），可传 0
 * 说明 : 使用定点 atan2 近似，不依赖 math.h / 浮点
 * 示例 : int16_t pitch, roll;
 *        MPU6050_GetTiltAngle(&pitch, &roll);
 *        LCD_ShowFixed(10, 90, pitch, 1, 3, LCD_COLOR_WHITE, LCD_COLOR_BLACK, 1); */
void    MPU6050_GetTiltAngle(int16_t *pitch_x10, int16_t *roll_x10);

#endif /* __FWLIB_MPU6050_H */
