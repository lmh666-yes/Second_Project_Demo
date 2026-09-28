#ifndef __FWLIB_W25QXX_LOG_H
#define __FWLIB_W25QXX_LOG_H

#include "stm32f4xx.h"
#include "w25qxx.h"

/* ================================================================
 *  w25qxx_log.h —— 【存储】外部 Flash 滚动记录（掉电不丢的数据黑匣子）
 * ================================================================
 *  设计定位 : 采集的数据要**留下来**——断网了先存着、网恢复再补传；
 *             或者单纯就要"最近 1000 条历史"。
 *             16MB 的 W25Q128 划一块当环形日志区，写满了自动从头覆盖，
 *             永远只保留**最新的 N 条**，不用管文件系统、不用管碎片。
 *             面向【工业数据采集网关】的历史记录。
 *  依赖     : w25qxx.h（底层读写擦）、sys_spi.h
 *  标准库关键词 : 无——纯粹是 Flash 上的数据组织
 *
 *  【它和别的存法的区别】
 *      W25QXX_WriteSafe : 每次写要读-擦-写整个扇区，~50ms —— 存配置首选
 *      W25QXX_LOG_Write : 追加一条 ~1ms（槽位预先擦好，只发一次页编程）
 *                         → 适合"每 10 秒记一条"这种高频写入
 *
 *  【一条记录长什么样（64 字节一槽，4KB 扇区正好 64 槽，不跨页不跨扇区）】
 *      偏移  长度  内容
 *      0     4     状态：0xFFFFFFFF=空槽  0x5A5A5A5A=有效
 *      4     4     序号 seq —— 自增，用来找哪条最新
 *      8     4     时间戳 ts —— RTC 秒数 / 开机秒数，你自己约定
 *      12    2     数据长度 len（0 ~ W25QXX_LOG_PAYLOAD）
 *      14    2     校验和 —— 防止读到写坏的数据
 *      16    48    数据
 *
 *  【为什么不怕掉电写坏】
 *      写入顺序是【先写数据，最后写状态】：
 *          ① 写 [4..64) 这一段（序号/时间/长度/校验/数据）
 *          ② 再单独写 [0..4) 的状态字
 *      如果在 ①② 之间断电，状态位还是 0xFFFFFFFF —— 这条被当成"空槽"，
 *      init 时会直接从它开始写，不会把半截数据当成真记录读出来。
 *
 *  【使用方式】
 *      W25QXX_Init(0);                     // ① 先把底层 Flash 初始化
 *      W25QXX_LogInit();                   // ② 建日志（内部会扫一遍找断点，~11ms）
 *
 *      W25QXX_LogWriteStr(ts, "T=25.3,H=60");      // ③ 存一条
 *
 *      // ④ 读第 0 条（最老的一条）
 *      uint32_t ts; uint8_t buf[W25QXX_LOG_PAYLOAD]; uint16_t len;
 *      if (W25QXX_LogRead(0, &ts, buf, &len) == W25QXX_LOG_OK) {
 *          printf("[%lu] %s\r\n", ts, buf);
 *      }
 *
 *  移植指引 : 换 Flash 型号只改 w25qxx.h（本模块全用它，不直接碰 SPI）；
 *             日志区大小改 W25QXX_LOG_SECTORS；
 *             单条能存多少改 W25QXX_LOG_SLOT_SIZE（载荷自动跟着算）；
 *             起始地址改 W25QXX_ADDR_LOG（分区表在 w25qxx.h 里）。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换场景只改这里）
 * ================================================================ */

/* 日志区从哪开始、占多大（必须落在 W25QXX_ADDR_LOG ~ W25QXX_ADDR_FONT 之间，
 * 下面的编译期护栏会替你检查，超了直接编译报错） */
#define W25QXX_LOG_BASE         W25QXX_ADDR_LOG     /* 起始地址 0x001000 */
#define W25QXX_LOG_SECTORS      16U                 /* 占 16 个扇区 = 64KB = 1024 条 */

/* 一条记录占多少字节（必须是 64 的整数倍、且能整除扇区/页，
 * 这样一条记录永远不跨页也不跨扇区，写入时序最简单） */
#define W25QXX_LOG_SLOT_SIZE    64U                 /* 64 字节/条，4KB 扇区正好 64 条 */

/* 槽内头部的固定长度（状态4 + 序号4 + 时间4 + 长度2 + 校验2）——别改 */
#define W25QXX_LOG_HDR_SIZE     16U

/* 单条记录能存多少数据字节（由 SLOT_SIZE 自动算出，改上面那个就行） */
#define W25QXX_LOG_PAYLOAD      (W25QXX_LOG_SLOT_SIZE - W25QXX_LOG_HDR_SIZE)   /* = 48 */

/* 状态字（0xFFFFFFFF 是擦除后的自然值＝空） */
#define W25QXX_LOG_EMPTY        0xFFFFFFFFUL        /* 空槽 */
#define W25QXX_LOG_VALID        0x5A5A5A5AUL        /* 有效记录 */

/* 返回码（全库统一：0 成功） */
#define W25QXX_LOG_OK           0U      /* 成功 */
#define W25QXX_LOG_ERR_PARAM    1U      /* 空指针 */
#define W25QXX_LOG_ERR_NOT_INIT 2U      /* 忘了先 W25QXX_LogInit */
#define W25QXX_LOG_ERR_TOO_LONG 3U      /* 数据超过 W25QXX_LOG_PAYLOAD 字节 */
#define W25QXX_LOG_ERR_FLASH    4U      /* Flash 没应答 / 读改写失败 */
#define W25QXX_LOG_ERR_EMPTY    5U      /* 里面还没东西，或序号超出范围 */
#define W25QXX_LOG_ERR_CORRUPT  6U      /* 校验和不对，这条数据坏了 */


/* ================================================================
 *                        区块 2：基础功能
 * ================================================================ */

/* 【建日志】上电调一次；内部扫一遍找到该从哪继续写
 * 前提 : **必须先 W25QXX_Init(0) 且返回 0**
 * 返回 : W25QXX_LOG_OK / W25QXX_LOG_ERR_FLASH
 * 说明 : 耗时 ≈ 总槽数 × 11us（默认 1024 条 ≈ 11ms），上电时跑没问题；
 *        里面已有的记录**不会被清掉**，断电重启接着写 ✓
 * 示例 : W25QXX_Init(0);  W25QXX_LogInit(); */
uint8_t W25QXX_LogInit(void);

/* 【追加一条记录】★核心
 * 参数 : ts   —— 时间戳，单位随你（RTC 秒数 / 开机毫秒都行，只是存着）
 *        data —— 数据首地址，**可以是二进制**（含 0x00 没关系）
 *        len  —— 数据字节数，0 ~ W25QXX_LOG_PAYLOAD(48)
 * 返回 : W25QXX_LOG_OK / W25QXX_LOG_ERR_*
 * 说明 : 单条耗时 ≈ 1ms（先查槽是不是空的，空了就直接写）。
 *        写满一圈会**自动覆盖最老的那条**，永远不会"写不进去"。
 * 示例 : W25QXX_LogWrite(SYS_RTC_GetCounter(), buf, n); */
uint8_t W25QXX_LogWrite(uint32_t ts, const uint8_t *data, uint16_t len);

/* 【追加一条文本记录】最常用，自动算长度、自动补 '\0'
 * 返回 : W25QXX_LOG_OK / W25QXX_LOG_ERR_*
 * 示例 : W25QXX_LogWriteStr(ts, "T=25.3,H=60,P=1013"); */
uint8_t W25QXX_LogWriteStr(uint32_t ts, const char *text);

/* 【现有多少条】上限 = W25QXX_LogCapacity() */
uint16_t W25QXX_LogCount(void);

/* 【全部清掉】16 个扇区各擦一次，约 0.8 秒，调一次就别频繁调
 * 返回 : W25QXX_LOG_OK / W25QXX_LOG_ERR_FLASH
 * 示例 : if (按键按住上电) W25QXX_LogClear();   // 做"恢复出厂" */
uint8_t W25QXX_LogClear(void);


/* ================================================================
 *                        区块 3：扩展功能
 * ================================================================ */

/* 【读第 index 条】0 = 最老的一条，Count()-1 = 最新的一条
 * 参数 : index —— 序号；ts —— 出参时间戳；
 *        buf   —— 出参数据（**传 0 表示我只想要时间戳/长度，不要数据**）
 *        len   —— 出参实际数据长度（也是入参方式：不看长度就传 0）
 * 返回 : W25QXX_LOG_OK            读到了，buf 里是数据且已补 '\0'
 *        W25QXX_LOG_ERR_EMPTY     index 超范围（还没那么多条）
 *        W25QXX_LOG_ERR_CORRUPT   校验和不对，这条坏了
 *        其它                      W25QXX_LOG_ERR_*
 * 说明 : 从最老往最新遍历就循环调 0,1,2...；缓冲区至少要有
 *        W25QXX_LOG_PAYLOAD + 1 字节（末尾要补 '\0'）
 * 示例 : W25QXX_LogRead(0, &ts, buf, &len); */
uint8_t W25QXX_LogRead(uint16_t index, uint32_t *ts, uint8_t *buf, uint16_t *len);

/* 【读最新一条】网关"补传最后一条"时用
 * 返回 : W25QXX_LOG_OK / W25QXX_LOG_ERR_*
 * 示例 : W25QXX_LogTail(&ts, buf, &len); */
uint8_t W25QXX_LogTail(uint32_t *ts, uint8_t *buf, uint16_t *len);

/* 【总共能存多少条】= W25QXX_LOG_SECTORS × 每扇区槽数 */
uint16_t W25QXX_LogCapacity(void);

/* 【看内部状态】调参/调试用；不想要的出参传 0
 * 参数 : count —— 现有条数；cap —— 总容量；head —— 下一条要写的槽号
 * 示例 : W25QXX_LogStat(&n, 0, 0);  printf("已存 %u 条\r\n", n); */
void W25QXX_LogStat(uint16_t *count, uint16_t *cap, uint16_t *head);

/* 【返回码转中文说明】调试打印用
 * 示例 : printf("[LOG] %s\r\n", W25QXX_LogErrStr(W25QXX_LogInit())); */
const char *W25QXX_LogErrStr(uint8_t err);

#endif /* __FWLIB_W25QXX_LOG_H */
