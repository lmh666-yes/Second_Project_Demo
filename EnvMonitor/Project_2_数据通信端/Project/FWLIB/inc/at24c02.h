#ifndef __FWLIB_AT24C02_H
#define __FWLIB_AT24C02_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* at24c02.h : 板载 AT24C02 EEPROM 驱动，封装 sys_i2c 总线原语
 * 接线：SCL = PB8，SDA = PB9，与 MPU6050 共用 I2C1，板上已有 4.7k 上拉
 * A0/A1/A2 = GND，7 位器件地址 = 0x50
 * 容量 2Kbit = 256 字节，按字节改写，单字节可擦写 100 万次
 * 写周期约 5ms，驱动内部用 ACK 轮询等待完成；页写 8 字节不可跨页，写段接口已自动分页
 * 地址范围 0x00~0xFF，越界会绕回，参数地址需自行规划 */


/* 定义与宏定义区 */
#define AT24C02_I2C_ID      SYS_I2C_1       /* 本板挂 I2C1（PB8/PB9） */
#define AT24C02_ADDR        SYS_I2C_ADDR_24C02  /* 7 位地址 0x50 */
#define AT24C02_I2C_SPEED   100000UL        /* 100kHz，本工程不使用 400kHz */

#define AT24C02_TOTAL_SIZE  256U            /* 总容量（字节） */
#define AT24C02_PAGE_SIZE   8U              /* 一页 8 字节 */
#define AT24C02_WRITE_MS    5U              /* 写周期典型 5ms（最大 10ms） */

/* 地址规划：各段起始地址与用途，新增数据追加到对应段内 */
#define AT24C02_ADDR_MAGIC      0x00U       /* 0x00~0x03  魔数（判断是否首次上电） */
#define AT24C02_ADDR_PARAM      0x10U       /* 0x10~0x7F  用户参数区 */
#define AT24C02_ADDR_CALIB      0x80U       /* 0x80~0xEF  校准数据区 */
#define AT24C02_MAGIC_VALUE     0xA5U       /* 首次上电读到 0xFF 说明没存过 */

/* 编译期自检：地址规划不得越界，越界会绕回且不报错 */
#if (AT24C02_ADDR_CALIB >= AT24C02_TOTAL_SIZE)
#error "AT24C02 address plan exceeds 256 bytes"
#endif


/* 基础功能：按字节与按段存取 */
/* 初始化：开 I2C 总线并跑一次写周期
 * 参数 : speed 为总线速率 Hz，0 表示取 AT24C02_I2C_SPEED
 * 返回 : 0 = 器件应答正常；1 = 无应答（查上拉、地址、焊接）
 * 说明 : 内部含约 10ms 等待，上电后 EEPROM 需要准备时间 */
uint8_t AT24C02_Init(uint32_t speed);

/* 器件是否在线：0x50 有 ACK 即在
 * 返回 : 1 = 在线；0 = 不在；总线已由 SYS_I2C_Init 开好时可直接调用 */
uint8_t AT24C02_IsOnline(void);

/* 写一个字节，内部等待写周期完成，返回时数据已写入
 * 参数 : addr 为 0x00~0xFF；data 为要写的值
 * 返回 : 0 = 成功；非 0 = sys_i2c 错误码，可用 SYS_I2C_ErrStr 转文字 */
uint8_t AT24C02_WriteByte(uint8_t addr, uint8_t data);

/* 读一个字节
 * 返回 : 0 = 成功；非 0 = 错误码 */
uint8_t AT24C02_ReadByte(uint8_t addr, uint8_t *out);

/* 写一段，内部自动分页，跨页安全
 * 参数 : addr 为起始地址；buf/len 为数据与长度
 * 返回 : 0 = 成功；非 0 = 错误码
 * 说明 : 每写完一页等一次写周期，耗时约为 页数 x 5ms，不要放在中断里 */
uint8_t AT24C02_WriteBytes(uint8_t addr, const uint8_t *buf, uint16_t len);

/* 读一段（连续读，无分页限制）
 * 返回 : 0 = 成功；非 0 = 错误码 */
uint8_t AT24C02_ReadBytes(uint8_t addr, uint8_t *buf, uint16_t len);


/* 按数据类型存取 */
/* 存/取一个字节 */
uint8_t AT24C02_WriteU8 (uint8_t addr, uint8_t  val);
uint8_t AT24C02_ReadU8  (uint8_t addr, uint8_t  *out);

/* 存/取 16 位整数（小端序：低字节在前） */
uint8_t AT24C02_WriteU16(uint8_t addr, uint16_t val);
uint8_t AT24C02_ReadU16 (uint8_t addr, uint16_t *out);

/* 存/取 32 位整数（小端序） */
uint8_t AT24C02_WriteU32(uint8_t addr, uint32_t val);
uint8_t AT24C02_ReadU32 (uint8_t addr, uint32_t *out);

/* 存/取 32 位浮点（按 IEEE754 原样搬 4 字节，可用于存 PID 系数） */
uint8_t AT24C02_WriteFloat(uint8_t addr, float val);
uint8_t AT24C02_ReadFloat (uint8_t addr, float *out);

/* 存/取字符串（自动补 '\0'，最长 len-1 个字符）
 * 返回 : 0 = 成功；1 = 参数非法 */
uint8_t AT24C02_WriteString(uint8_t addr, const char *str, uint8_t max_len);
uint8_t AT24C02_ReadString (uint8_t addr, char *str, uint8_t max_len);

/* 用魔数判断是否首次上电
 * 返回 : 1 = 读到的值等于 magic，参数已存过；0 = 不等于 magic（空片）或 I2C 读失败
 * 说明 : 返回值按魔数是否匹配理解，误当作空白判断取反使用会每次上电重灌默认参数 */
uint8_t AT24C02_IsMagicSet(uint8_t addr, uint8_t magic);

/* 整片写 0xFF，恢复出厂用；256 字节约 0.2 秒 */
uint8_t AT24C02_Erase(void);

#endif /* __FWLIB_AT24C02_H */
