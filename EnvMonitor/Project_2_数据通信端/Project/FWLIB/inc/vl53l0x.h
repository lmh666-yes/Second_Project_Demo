#ifndef __FWLIB_VL53L0X_H
#define __FWLIB_VL53L0X_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* VL53L0X 激光测距，ToF 飞行时间原理，量程 3cm ~ 200cm
 * 依赖 sys_i2c.h 提供寄存器读写，gpio_core.h 提供 DWT 毫秒计时
 * 接线：VIN 接 3.3V（器件为 2.8V 器件，接 5V 会损坏），GND 接 GND，
 *       SCL 接 PB8（板载 I2C1），SDA 接 PB9；XSHUT 模块内部已上拉，不用接，
 *       只有一条总线挂两颗时用它做分时上电改地址
 * 板载 I2C1 上已挂 24C02(0x50) 与 MPU6050(0x68)，VL53L0X 为 0x29，地址不冲突
 * 无效读数：接近 8190 或 8191 表示没测到，原因通常是目标太远、全黑或太斜
 * 读数约 20mm 且不随距离变化表示前方有玻璃或亚克力镜面反光
 * 大太阳下环境光会淹掉回波，量程明显缩水 */


/* 区块 1：定义与宏定义区 */
#define VL53L0X_I2C_ID          SYS_I2C_1   /* 挂在板载 I2C1（PB8=SCL / PB9=SDA） */
#define VL53L0X_I2C_ADDR        0x29U       /* 7 位地址，芯片固定 */
#define VL53L0X_I2C_SPEED       400000UL    /* 芯片支持 400kHz 快速模式 */

#define VL53L0X_TIMEOUT_MS      100U        /* 单次测量完成的等待超时，默认预算 33ms */

/* 出厂 Model ID（IDENTIFICATION_MODEL_ID = 0xC0），用于判断芯片是否应答 */
#define VL53L0X_MODEL_ID        0xEEU

/* 默认预算下的典型量程，长距模式另算 */
#define VL53L0X_RANGE_MIN_MM    30U         /* 近界，更近只能用短距模式 */
#define VL53L0X_RANGE_MAX_MM    2000U       /* 远界（白墙、室内正常光） */

/* 开机默认测量预算（微秒），数值越大越准越慢，下限 20000 */
#define VL53L0X_BUDGET_DEFAULT_US   33000UL

/* VCSEL 脉冲周期选择 */
#define VL53L0X_VCSEL_PRE           0U      /* 预量程段；合法值 12/14/16/18 */
#define VL53L0X_VCSEL_FINAL         1U      /* 最终量程段；合法值 8/10/12/14 */

/* 返回码（全库统一：0 成功） */
#define VL53L0X_OK              0U      /* 成功 */
#define VL53L0X_ERR_PARAM       1U      /* 空指针 / 参数非法 */
#define VL53L0X_ERR_NO_DEVICE   2U      /* 器件不在线（Model ID 读不到 0xEE） */
#define VL53L0X_ERR_I2C         3U      /* I2C 收发失败（查接线/上拉/供电） */
#define VL53L0X_ERR_TIMEOUT     4U      /* 等测量完成超时 */
#define VL53L0X_ERR_SPAD        5U      /* SPAD 信息读取失败（内部自检不过） */
#define VL53L0X_ERR_CALIB       6U      /* 参考校准失败 */
#define VL53L0X_ERR_NOT_INIT    7U      /* 未先调用 VL53L0X_Init */


/* 区块 2：基础功能 */

/* 初始化：上电调用一次，内含 ST 官方调参表与参考校准，耗时约 100ms
 * 前提：必须先调用 SYS_I2C_Init(VL53L0X_I2C_ID, VL53L0X_I2C_SPEED)
 * 返回：VL53L0X_OK / VL53L0X_ERR_*
 * 步骤顺序不可调换：读 0xC0 确认型号；DataInit 切 2V8 供电、取 stop_variable、放开信号率限制；
 * StaticInit 读 SPAD 信息、灌调参表、配中断；参考校准先 VHV(0x40) 再相位(0x00) */
uint8_t VL53L0X_Init(void);

/* 测一次距离：单次模式，测完自动停
 * 参数：mm 为出参，单位毫米
 * 返回：VL53L0X_OK 测到了；VL53L0X_ERR_TIMEOUT 超时（前方无目标或太远）；其它 VL53L0X_ERR_*
 * 耗时约 33ms（等于测量预算），阻塞式 */
uint8_t VL53L0X_ReadMm(uint16_t *mm);

/* 测一次并取状态码：用于判断没测到的原因
 * 参数：mm 为出参距离，status 为出参状态字节，取值见 VL53L0X_StatusStr
 * 返回：VL53L0X_OK / VL53L0X_ERR_* */
uint8_t VL53L0X_ReadMmEx(uint16_t *mm, uint8_t *status);

/* 探测器件是否在线：发一次 I2C，能读到 Model ID 即在线
 * 返回：1 = 在线；0 = 不在 */
uint8_t VL53L0X_IsPresent(void);

/* 读 Model ID：正常为 0xEE，读回 0xFF 通常是器件没有应答 */
uint8_t VL53L0X_GetModelID(void);


/* 区块 3：扩展功能 */

/* 启动连续测量：传感器自己一直测，之后不用每次下命令
 * 参数：period_ms 为 0 时背靠背（测完立刻下一次，最快）；非 0 为定时模式，隔 period_ms 测一次
 * 返回：VL53L0X_OK / VL53L0X_ERR_* */
uint8_t VL53L0X_StartContinuous(uint32_t period_ms);

/* 读连续模式的最新一次结果：不启动测量，只等结果
 * 返回：VL53L0X_OK / VL53L0X_ERR_*
 * 未启动连续模式时调用会超时 */
uint8_t VL53L0X_ReadContinuousMm(uint16_t *mm);

/* 停止连续测量：回到单次模式 */
uint8_t VL53L0X_StopContinuous(void);

/* 改 I2C 地址：同一总线挂两颗 VL53L0X 时使用
 * 参数：new_addr7 为新的 7 位地址，合法范围 0x08~0x77，且不能与 24C02/MPU6050 冲突
 * 返回：VL53L0X_OK / VL53L0X_ERR_PARAM
 * 两颗都要接 XSHUT：先都拉低，放开第一颗并 Init 后 SetAddress(0x30)，
 * 再放开第二颗（此时它仍是 0x29）并 Init 后 SetAddress(0x31)
 * 地址掉电不保存，每次上电都要重新设置 */
uint8_t VL53L0X_SetAddress(uint8_t new_addr7);

/* 设测量预算：一次测量最多花多少微秒
 * 参数：budget_us 有效范围 20000 ~ 200000，默认 33000
 * 返回：VL53L0X_OK / VL53L0X_ERR_PARAM
 * 预算翻 N 倍，测距噪声降到 1/√N，代价是变慢 */
uint8_t VL53L0X_SetTimingBudget(uint32_t budget_us);

/* 读当前测量预算，单位微秒 */
uint32_t VL53L0X_GetTimingBudget(void);

/* 设回波信号率门限，单位 MCPS（兆计数/秒），默认 0.25
 * 参数：mcps 取值范围 0 ~ 511.99
 * 返回：VL53L0X_OK / VL53L0X_ERR_PARAM
 * 门限调低量程变大，但更容易被旁边墙的反光干扰；门限调高只认强回波，抗干扰好但量程缩水 */
uint8_t VL53L0X_SetSignalRateLimit(float mcps);

/* 改 VCSEL 脉冲周期：改完自动重算预算并重做相位校准
 * 参数：type 取 VL53L0X_VCSEL_PRE（合法 12/14/16/18，默认 14）
 *       或 VL53L0X_VCSEL_FINAL（合法 8/10/12/14，默认 10）
 *       period_pclks 为周期值，只能取偶数
 * 返回：VL53L0X_OK / VL53L0X_ERR_PARAM
 * 周期越长量程越大、精度略降；越短越准、量程越小 */
uint8_t VL53L0X_SetVcselPulsePeriod(uint8_t type, uint8_t period_pclks);

/* 读回当前 VCSEL 周期
 * 返回：周期值（PCLK 数）；参数非法返回 0 */
uint8_t VL53L0X_GetVcselPulsePeriod(uint8_t type);

/* 改超时，单位毫秒；0 表示不超时，会一直死等 */
void VL53L0X_SetTimeout(uint16_t ms);

/* 上次测量是否超时：读一次自动清零 */
uint8_t VL53L0X_TimeoutOccurred(void);

/* 状态码转中文说明，调试打印用
 * 状态码取自 RESULT_RANGE_STATUS(0x14) 的 bit6:3 */
const char *VL53L0X_StatusStr(uint8_t status);

/* 返回码转中文说明，调试打印用 */
const char *VL53L0X_ErrStr(uint8_t err);

#endif /* __FWLIB_VL53L0X_H */
