/* ============================================================================
 *  sys_sht30.h — SHT30 数字温湿度传感器驱动
 *  器件 : SHT30-DIS（Sensirion），16 位串行输出，I2C 接口
 *  量程 : 温度 -40 ~ +125 ℃（精度 ±0.2 ℃），湿度 0 ~ 100 %RH（精度 ±2 %RH）
 *         本驱动换算为 ℃ / %RH 浮点，以及 ×100 的 int16（0.01 单位），与 sys_frame 帧格式一致。
 *  供电 : 2.4V ~ 5.5V（SHT30-DIS 板级模块通常 3.3V）。裸芯片接 5V 前须按实物确认是否有 LDO。
 *
 *  地址 : 7 位地址由 ADDR 脚决定，接 GND 为 0x44，接 VCC 为 0x45。
 *         同总线其它器件 0x23(BH1750)/0x76 或 0x77(BMP280)/0x5A(CCS811) 与 0x44/0x45 不冲突。
 *  接线 : VCC → 3.3V    GND → GND    SCL/SDA → 对应 I2C 的 PBx
 *  总线 : I2C，100kHz ~ 1MHz（建议 100kHz；PBx 用内部上拉时 400kHz 容易无应答）
 *  依赖 : sys_i2c.c（SYS_I2C_IsDeviceReady / WriteBytes / ReadRaw）、delay.c（delay_ms）
 *
 *  协议 : 命令式器件，无寄存器号，写入的字节即命令，用 WriteBytes(bus, addr, cmd, buf, len)
 *         发送 1~2 字节命令。读数用 SYS_I2C_ReadRaw()（不发送寄存器号）：
 *         SYS_I2C_ReadBytes(..., 0x00, ...) 会先发送 0x00，对 SHT30 是非法命令。
 *
 *  测量命令（单次测量，测完自动休眠）：
 *         0x24 0x00  高重复性，**时钟拉伸使能** —— 本驱动不用：
 *                    传感器会拉低 SCL 直到转换完成，软件模拟/轮询 I2C 读不到数据，
 *                    表现为 SYS_I2C_ERR_ADDR，是最常见的"读不到 SHT30"原因。
 *         0x24 0x06  高重复性，**时钟拉伸关闭** —— 本驱动使用，发完命令延时 15ms 再读。
 *         0x24 0x0B  中重复性，不拉伸（约 6ms）    0x24 0x16  低重复性，不拉伸（约 4ms）
 *         0x30 0xA2  软复位（约 2ms）    0x30 0x93  复位后中断状态清除    0x30 0x6D  加热器（仅诊断用）
 *
 *  数据格式 : 每次读 6 字节 = 温度高/低/CRC + 湿度高/低/CRC，MSB 在前。
 *         温度 = -45 + 175 × raw / 65535 (℃)
 *         湿度 = 100 × raw / 65535 (%RH)
 *         CRC-8：多项式 0x31、初值 0xFF、不含最终异或，校验范围是前 2 个字节。
 *
 *  注意 : 1) 上电到可通信最长 1.5ms，本驱动 Init 先软复位再等 2ms。
 *         2) 测量命令发出后未读完数据就再次测量，会覆盖上一次结果。
 *         3) 加热器只用于诊断（除湿/自检），正常采集不要开，会让温度升高 1~2 ℃。
 * ============================================================================ */

#ifndef __FWLIB_SYS_SHT30_H
#define __FWLIB_SYS_SHT30_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* ---------- 器件地址（7 位） ---------- */
#define SYS_SHT30_ADDR_LOW        0x44U   /* ADDR 拉低/GND → 0x44（模块默认） */
#define SYS_SHT30_ADDR_HIGH       0x45U   /* ADDR 拉高/VCC → 0x45 */
#define SYS_SHT30_ADDR_DEFAULT    SYS_SHT30_ADDR_LOW

/* ---------- 命令字（前 2 字节，写进去就是命令） ---------- */
#define SYS_SHT30_CMD_MEAS_H_STRETCH   0x24U  /* 高重复性，时钟拉伸使能（本驱动不用） */
#define SYS_SHT30_CMD_MEAS_H_NOSTRETCH 0x06U  /* 高重复性 + 不拉伸：首字节 0x24，次字节 0x06 */
#define SYS_SHT30_CMD_MEAS_M_NOSTRETCH 0x0BU  /* 中重复性 + 不拉伸，次字节 */
#define SYS_SHT30_CMD_MEAS_L_NOSTRETCH 0x16U  /* 低重复性 + 不拉伸，次字节 */
#define SYS_SHT30_CMD_SOFT_RESET       0xA2U  /* 软复位：首字节 0x30，次字节 0xA2 */
#define SYS_SHT30_CMD_CLEAR_STATUS     0x93U  /* 清除中断/状态：首字节 0x30 */
#define SYS_SHT30_CMD_HEATER           0x6DU  /* 加热器（仅诊断）：首字节 0x30 */

/* ---------- 时间参数（毫秒） ---------- */
/* 手册：高重复性转换最长时间 15ms；软复位后约 2ms 才能通信。 */
#define SYS_SHT30_CONV_MS_H        15U
#define SYS_SHT30_CONV_MS_M         6U
#define SYS_SHT30_CONV_MS_L         4U
#define SYS_SHT30_RESET_MS          2U

/* ---------- 返回码 ---------- */
/* 约定：0 = 成功；> 0 = 本驱动语义码；< 0 = SYS_I2C_ERR_*，可交 SYS_I2C_ErrStr() 取说明。 */
#define SYS_SHT30_OK              0     /* 成功 */
#define SYS_SHT30_ERR_PARAM       1     /* 参数非法 / 该总线还没 Init / 地址不是 0x44 或 0x45 */
#define SYS_SHT30_ERR_NO_DEVICE   2     /* 地址无应答：没接、没供电、ADDR 脚选错、总线被拉死 */
#define SYS_SHT30_ERR_CRC         3     /* CRC 校验失败：数据不可信（总线干扰/上拉不足/线太长） */

/* ---------- 接口 ---------- */

/* 初始化（默认地址 0x44）
 * 参数 : bus 取 SYS_I2C_1 / SYS_I2C_2 / SYS_I2C_3
 * 返回 : 0 成功，已软复位并完成一次探测；1 bus 越界；2 0x44 无应答
 * 说明 : 探测 → 软复位(0x30A2) → 延时 2ms → 再探测。须先调 SYS_I2C_Init()。
 *        探测失败也记录地址，可用 ReadRaw 重试。 */
uint8_t SYS_SHT30_Init(SysI2cId_t bus);

/* 初始化（自定义地址，ADDR 接 VCC 时用 0x45）
 * 返回 : 同 SYS_SHT30_Init；addr7 非 0x44/0x45 返回 SYS_SHT30_ERR_PARAM */
uint8_t SYS_SHT30_InitAddr(SysI2cId_t bus, uint8_t addr7);

/* 取当前记录的总线地址（0 表示该总线还没 Init） */
uint8_t SYS_SHT30_GetAddr(SysI2cId_t bus);

/* 软复位（0x30A2），耗时约 SYS_SHT30_RESET_MS
 * 返回 : 0 成功；1 还没 Init；< 0 为 SYS_I2C_ERR_* */
int SYS_SHT30_SoftReset(SysI2cId_t bus);

/* 读一次温湿度（浮点）
 * 参数 : bus；temp_c 输出摄氏度；humi_rh 输出 %RH（任一为空指针返回 SYS_SHT30_ERR_PARAM）
 * 返回 : 0 成功；1 参数/ 未 Init；2 无应答；3 CRC 错；< 0 为 SYS_I2C_ERR_*
 * 说明 : 内部发 0x24 0x06（不拉伸）→ delay_ms(15) → 读 6 字节 → 双 CRC 校验 → 换算。
 *        忙等 15ms，RTOS 下仍可用（不占 SysTick、不关中断），但会阻塞调用任务。 */
int SYS_SHT30_Read(SysI2cId_t bus, float *temp_c, float *humi_rh);

/* 读一次温湿度（原始码），用于自检/寄存器比对
 * 返回 : 同 SYS_SHT30_Read；temp_raw/humi_raw 为 16 位原始值 */
uint8_t SYS_SHT30_Proto(SysI2cId_t bus);
int SYS_SHT30_ReadRaw(SysI2cId_t bus, uint16_t *temp_raw, uint16_t *humi_raw);

/* 读一次温湿度（×100 整数，直接填帧）
 * 参数 : temp_x100 输出 0.01℃；humi_x100 输出 0.01%RH
 * 返回 : 同 SYS_SHT30_Read
 * 说明 : 先算浮点再 ×100 四舍五入，与 SYS_FRAME_BuildEnv 的单位约定一致。 */
int SYS_SHT30_ReadInt(SysI2cId_t bus, int16_t *temp_x100, int16_t *humi_x100);

/* CRC-8（多项式 0x31，初值 0xFF）
 * 参数 : data 数据首地址；len 字节数
 * 返回 : 计算出的 CRC 值
 * 说明 : 对外暴露，便于自检脚本用标准表向量验证驱动（0xBE 0xEF → 0x92）。 */
uint8_t SYS_SHT30_Crc8(const uint8_t *data, uint8_t len);

#endif /* __FWLIB_SYS_SHT30_H */
