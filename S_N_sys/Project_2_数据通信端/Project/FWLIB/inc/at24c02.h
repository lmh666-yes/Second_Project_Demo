#ifndef __FWLIB_AT24C02_H
#define __FWLIB_AT24C02_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* ================================================================
 *  at24c02.h —— 【板载】AT24C02 EEPROM 驱动  头文件
 * ================================================================
 *  设计定位 : 器件级驱动（薄封装）—— 依赖 sys_i2c 的总线原语，
 *             给上层提供"按字节/按类型存数据"的接口
 *  依赖     : sys_i2c.h（本板 24C02 挂在 I2C1：SCL=PB8 / SDA=PB9）
 *  标准库关键词 : 无——全部走 SYS_I2C_WriteBytes / ReadBytes
 *
 *  【这块芯片解决什么问题（面试点）】
 *    单 片 机 掉 电 后 RAM 全丢，但"上次设的 PID 参数 / 阈值 / 校准值"
 *    要记住 → 就存到 EEPROM。24C02 = 2Kbit = **256 字节**，
 *    按字节改写、单字节可擦写 100 万次，正好适合存"参数表"。
 *
 *  【接线（普中-天马 F407开发板）】
 *      SCL = PB8   SDA = PB9   （与 MPU6050 共用 I2C1，板上已有 4.7k 上拉）
 *      A0/A1/A2 = GND  → 7 位器件地址 = 0x50
 *
 *  【使用方式（存一组参数）】
 *      AT24C02_Init(100000);                       // ① 开总线并探测器件
 *      AT24C02_WriteU16(0x00, 1234);               // ② 把 1234 存到 0x00
 *      uint16_t v; AT24C02_ReadU16(0x00, &v);      // ③ 读回来
 *
 *  【三个必须知道的坑】
 *    ① **写进去要等约 5ms** 才能读（芯片内部在写），本驱动每写一次都会
 *       用"ACK 轮询"等到写完成，你不用自己 Delay；
 *    ② 一次页写不能**跨页**（一页 8 字节），跨了会绕回本页开头覆盖数据。
 *       本驱动的 AT24C02_WriteBytes 已自动分页，放心整段写；
 *    ③ 地址范围只有 0x00~0xFF（256 字节），越界会**绕回**，写参数请自己
 *       规划好地址表（建议在 .h 里定义 PARAM_ADDR_xxx 常量）。
 *
 *  移植指引 : 换总线改 AT24C02_I2C_ID；换器件地址改 AT24C02_ADDR。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
#define AT24C02_I2C_ID      SYS_I2C_1       /* 本板挂 I2C1（PB8/PB9） */
#define AT24C02_ADDR        SYS_I2C_ADDR_24C02  /* 7 位地址 0x50 */
#define AT24C02_I2C_SPEED   100000UL        /* 100kHz（EEPROM 别跑 400k） */

#define AT24C02_TOTAL_SIZE  256U            /* 总容量（字节） */
#define AT24C02_PAGE_SIZE   8U              /* 一页 8 字节 */
#define AT24C02_WRITE_MS    5U              /* 写周期典型 5ms（最大 10ms） */

/* ---- 建议的地址规划（你自己往上加，避免各处乱写） ---- */
#define AT24C02_ADDR_MAGIC      0x00U       /* 0x00~0x03  魔数（判断是否首次上电） */
#define AT24C02_ADDR_PARAM      0x10U       /* 0x10~0x7F  用户参数区 */
#define AT24C02_ADDR_CALIB      0x80U       /* 0x80~0xEF  校准数据区 */
#define AT24C02_MAGIC_VALUE     0xA5U       /* 首次上电读到 0xFF 说明没存过 */

/* 编译期自检：地址规划不能越界（越界会绕回，静默出错最难查） */
#if (AT24C02_ADDR_CALIB >= AT24C02_TOTAL_SIZE)
#error "AT24C02 address plan exceeds 256 bytes"
#endif


/* ================================================================
 *                    区块 2：基础功能（按字节/按段）
 * ================================================================ */
/* 初始化：开 I2C 总线并跑一次写周期
 * 参数 : speed —— 总线速率 Hz（0 = AT24C02_I2C_SPEED）
 * 返回 : 0 = 器件应答正常；1 = 无应答（查上拉/地址/焊接）
 * 说明 : 内部已含约 10ms 等待（上电后 EEPROM 需要准备时间）
 * 示例 : if (AT24C02_Init(0) != 0) printf("24C02 没应答\r\n"); */
uint8_t AT24C02_Init(uint32_t speed);

/* 器件是否在线（0x50 有 ACK 即在）
 * 返回 : 1 = 在线；0 = 不在
 * 说明 : 总线已由 SYS_I2C_Init 开好的前提下可直接调用 */
uint8_t AT24C02_IsOnline(void);

/* 写一个字节（内部自动等写周期完成，返回时数据已可靠落盘）
 * 参数 : addr —— 0x00~0xFF；data —— 要写的值
 * 返回 : 0 = 成功；非 0 = sys_i2c 错误码（可用 SYS_I2C_ErrStr 转文字）
 * 示例 : AT24C02_WriteByte(0x10, 0x5A); */
uint8_t AT24C02_WriteByte(uint8_t addr, uint8_t data);

/* 读一个字节
 * 返回 : 0 = 成功；非 0 = 错误码 */
uint8_t AT24C02_ReadByte(uint8_t addr, uint8_t *out);

/* 写一段（**自动分页**，跨页也安全）
 * 参数 : addr —— 起始地址；buf/len —— 数据与长度
 * 返回 : 0 = 成功；非 0 = 错误码
 * 说明 : 每写完一页等一次写周期，因此耗时为 (页数 × 5ms)，请勿放中断里
 * 示例 : uint8_t cfg[16] = {...}; AT24C02_WriteBytes(0x10, cfg, sizeof(cfg)); */
uint8_t AT24C02_WriteBytes(uint8_t addr, const uint8_t *buf, uint16_t len);

/* 读一段（连续读，无分页限制）
 * 返回 : 0 = 成功；非 0 = 错误码 */
uint8_t AT24C02_ReadBytes(uint8_t addr, uint8_t *buf, uint16_t len);


/* ================================================================
 *                    区块 3：按数据类型存取（最常用）
 * ================================================================ */
/* 存/取一个字节 */
uint8_t AT24C02_WriteU8 (uint8_t addr, uint8_t  val);
uint8_t AT24C02_ReadU8  (uint8_t addr, uint8_t  *out);

/* 存/取 16 位整数（小端序：低字节在前）
 * 示例 : AT24C02_WriteU16(0x20, 3000);   // 存一个 PID 目标值 */
uint8_t AT24C02_WriteU16(uint8_t addr, uint16_t val);
uint8_t AT24C02_ReadU16 (uint8_t addr, uint16_t *out);

/* 存/取 32 位整数（小端序） */
uint8_t AT24C02_WriteU32(uint8_t addr, uint32_t val);
uint8_t AT24C02_ReadU32 (uint8_t addr, uint32_t *out);

/* 存/取 32 位浮点（按 IEEE754 原样搬 4 字节，可直接存 PID 系数）
 * 示例 : AT24C02_WriteFloat(0x30, 1.25f); */
uint8_t AT24C02_WriteFloat(uint8_t addr, float val);
uint8_t AT24C02_ReadFloat (uint8_t addr, float *out);

/* 存/取字符串（自动补 '\0'，最长 len-1 个字符）
 * 返回 : 0 = 成功；1 = 参数非法 */
uint8_t AT24C02_WriteString(uint8_t addr, const char *str, uint8_t max_len);
uint8_t AT24C02_ReadString (uint8_t addr, char *str, uint8_t max_len);

/* "魔数"判首次上电：本函数把整片（或指定长度）与 magic 比较
 * 返回 : 1 = 读到的内容 != magic（说明从没存过，应写入默认参数）
 * 示例 : if (AT24C02_IsBlank(AT24C02_ADDR_MAGIC, AT24C02_MAGIC_VALUE)) {
 *            AT24C02_LoadDefaults();       // ① 写默认参数
 *            AT24C02_WriteU8(AT24C02_ADDR_MAGIC, AT24C02_MAGIC_VALUE);   // ② 打标记
 *        } */
uint8_t AT24C02_IsBlank(uint8_t addr, uint8_t magic);

/* 整片填 0xFF（"恢复出厂"用；256 字节约 0.2 秒） */
uint8_t AT24C02_Erase(void);

#endif /* __FWLIB_AT24C02_H */
