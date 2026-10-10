#ifndef __FWLIB_W25QXX_LOG_H
#define __FWLIB_W25QXX_LOG_H

#include "stm32f4xx.h"
#include "w25qxx.h"

/* w25qxx_log.h 外部 Flash 滚动日志
 * 在 W25Q128 上划一块环形日志区，写满自动覆盖最旧记录，只保留最新 N 条。
 * 依赖 w25qxx.h 与底层 SPI 驱动。
 *
 * 一条记录 32 字节一槽，4KB 扇区正好 128 槽，不跨页不跨扇区：
 *   偏移  长度  内容
 *   0     4     序号 seq，自增，用来找最新一条；0xFFFFFFFF 表示空槽
 *   4     4     时间戳 ts，单位为 RTC 秒数或开机秒数，由调用方约定
 *   8     2     数据长度 len，范围 0 ~ W25QXX_LOG_PAYLOAD
 *   10    2     CRC16 校验，低字节在前
 *   12    20    数据
 *
 * 掉电保护：先写 [4..32) 的时间戳、长度、CRC16、数据，再单独写 [0..4) 的 seq。
 * 两步之间断电时 seq 仍为 0xFFFFFFFF，该槽按空槽处理，init 从它开始写，
 * 半截数据不会被当成有效记录读出。因此写一条只需一次页编程，约 1ms，
 * 不像 W25QXX_WriteSafe 那样读-擦-写整个扇区。
 *
 * 调用顺序：W25QXX_Init(0) 成功后再 W25QXX_LogInit()，之后才能读写。
 *
 * 移植：Flash 型号改 w25qxx.h；日志区大小改 W25QXX_LOG_SECTORS；
 * 单条容量改 W25QXX_LOG_SLOT_SIZE；起始地址改 w25qxx.h 分区表中的 W25QXX_ADDR_LOG。
 */

/* 区块 1：定义与宏定义区 */

/* 日志区起止：必须落在 W25QXX_ADDR_LOG ~ W25QXX_ADDR_FONT 之间，越界编译报错 */
#define W25QXX_LOG_BASE         W25QXX_ADDR_LOG     /* 起始地址 0x001000 */
#define W25QXX_LOG_SECTORS      16U                 /* 占 16 个扇区 = 64KB = 2048 条 */

/* 一条记录占多少字节：必须是 4 的整数倍且能整除扇区与页，保证记录不跨页不跨扇区 */
#define W25QXX_LOG_SLOT_SIZE    32U                 /* 32 字节/条，4KB 扇区正好 128 条 */

/* 槽内头部固定长度（序号4 + 时间4 + 长度2 + CRC2） */
#define W25QXX_LOG_HDR_SIZE     12U

/* 单条记录的数据字节数，由 SLOT_SIZE 自动算出 */
#define W25QXX_LOG_PAYLOAD      (W25QXX_LOG_SLOT_SIZE - W25QXX_LOG_HDR_SIZE)   /* = 20 */

/* 空槽判据：seq 等于该值即空槽，也是擦除后的全 1 状态 */
#define W25QXX_LOG_EMPTY        0xFFFFFFFFUL

/* 返回码（全库统一：0 成功） */
#define W25QXX_LOG_OK           0U      /* 成功 */
#define W25QXX_LOG_ERR_PARAM    1U      /* 空指针 */
#define W25QXX_LOG_ERR_NOT_INIT 2U      /* 忘了先 W25QXX_LogInit */
#define W25QXX_LOG_ERR_TOO_LONG 3U      /* 数据超过 W25QXX_LOG_PAYLOAD 字节 */
#define W25QXX_LOG_ERR_FLASH    4U      /* Flash 没应答 / 读改写失败 */
#define W25QXX_LOG_ERR_EMPTY    5U      /* 里面还没东西，或序号超出范围 */
#define W25QXX_LOG_ERR_CORRUPT  6U      /* 校验和不对，这条数据坏了 */


/* 区块 2：基础功能 */

/* 建日志，上电调一次；内部扫描全部槽位找写入断点
 * 前提：W25QXX_Init(0) 已返回 0
 * 返回：W25QXX_LOG_OK / W25QXX_LOG_ERR_FLASH
 * 耗时约 总槽数 × 11us（2048 条约 22ms），已有记录不清除 */
uint8_t W25QXX_LogInit(void);

/* 追加一条记录
 * 参数：ts   时间戳，单位由调用方约定，仅原样保存
 *       data 数据首地址，数据可含 0x00
 *       len  数据字节数，范围 0 ~ W25QXX_LOG_PAYLOAD(20)
 * 返回：W25QXX_LOG_OK / W25QXX_LOG_ERR_*
 * 单条耗时约 1ms；写满一圈自动覆盖最旧的一条 */
uint8_t W25QXX_LogWrite(uint32_t ts, const uint8_t *data, uint16_t len);

/* 追加一条文本记录，长度自动计算
 * 返回：W25QXX_LOG_OK / W25QXX_LOG_ERR_* */
uint8_t W25QXX_LogWriteStr(uint32_t ts, const char *text);

/* 现有记录条数，上限为 W25QXX_LogCapacity() */
uint16_t W25QXX_LogCount(void);

/* 全部清除：16 个扇区各擦一次，约 0.8 秒，不宜频繁调用
 * 返回：W25QXX_LOG_OK / W25QXX_LOG_ERR_FLASH */
uint8_t W25QXX_LogClear(void);


/* 区块 3：扩展功能 */

/* 读第 index 条：0 为最老的一条，Count()-1 为最新的一条
 * 参数：index 序号；ts 出参时间戳
 *       buf 出参数据，传 0 表示只要时间戳和长度
 *       len 出参实际数据长度，不看长度时可传 0
 * 返回：W25QXX_LOG_OK           读成功
 *       W25QXX_LOG_ERR_EMPTY    index 超出已有条数
 *       W25QXX_LOG_ERR_CORRUPT  CRC16 校验失败
 *       其它                    W25QXX_LOG_ERR_*
 * 缓冲区至少 W25QXX_LOG_PAYLOAD + 1 字节，末尾补 '\0' */
uint8_t W25QXX_LogRead(uint16_t index, uint32_t *ts, uint8_t *buf, uint16_t *len);

/* 读最新一条
 * 返回：W25QXX_LOG_OK / W25QXX_LOG_ERR_* */
uint8_t W25QXX_LogTail(uint32_t *ts, uint8_t *buf, uint16_t *len);

/* 总容量，等于 W25QXX_LOG_SECTORS × 每扇区槽数 */
uint16_t W25QXX_LogCapacity(void);

/* 读内部状态，不用的出参传 0
 * 参数：count 现有条数；cap 总容量；head 下一条要写的槽号 */
void W25QXX_LogStat(uint16_t *count, uint16_t *cap, uint16_t *head);

/* 因整扇区擦除被连带丢弃的记录条数，只增不减
 * Flash 最小擦除单位是 4KB 扇区（128 个槽）；环形缓冲绕回某扇区写首槽时
 * 必须整扇擦除，同扇区中尚未读出的记录一并丢失，增量计入本计数
 * 非 0 表示缓存已在丢数据（断网太久或补传太慢），应报警或加大日志区 */
uint16_t W25QXX_LogLost(void);

/* 返回码转文本说明，供调试打印 */
const char *W25QXX_LogErrStr(uint8_t err);

#endif /* __FWLIB_W25QXX_LOG_H */
