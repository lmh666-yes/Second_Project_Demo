#ifndef __FWLIB_W25QXX_H
#define __FWLIB_W25QXX_H

#include "stm32f4xx.h"
#include "sys_spi.h"

/* ================================================================
 *  w25qxx.h —— 【板载】W25Q 系列 SPI Flash 驱动  头文件
 * ================================================================
 *  设计定位 : 器件级驱动（薄封装）—— 依赖 sys_spi 的字节收发原语，
 *             提供"擦除 / 写 / 读 / 查空"四个动作，通用于 W25Q16~W25Q256
 *  依赖     : sys_spi.h（本板 W25Q128 挂在 SPI1：SCK=PB3/MISO=PB4/MOSI=PB5，
 *                        片选 CS=PB14，由本模块自己控制）
 *  标准库关键词 : 无——全部走 SYS_SPI_TransferByte / SYS_SPI_Write / Read
 *
 *  【这块芯片解决什么问题（面试点）】
 *    EEPROM(24C02) 只有 256 字节，存不了"图片 / 字库 / 日志 / 音频"。
 *    W25Q128 = 128Mbit = **16MB**，成本却和 EEPROM 差不多，
 *    所以开发板上"大容量、偶尔写"的数据都放它。
 *    代价是三条必须记住的规矩 ↓
 *
 *  【三条铁律（写 Flash 翻车的 90% 都是违反它们）】
 *    ① **只能 1→0，不能 0→1**：所以写之前必须先"擦除"，擦除整片/整扇区
 *       会把区域里所有字节变成 0xFF；
 *    ② **擦除的最小单位是 4KB 扇区**（Sector），
 *       所以"改 1 个字节"其实要：读回整个 4KB → 改那 1 个字节 → 擦扇区 → 整扇区写回。
 *       本驱动的 W25QXX_WriteSafe 就是帮你干这件事的；
 *    ③ 写入必须**先发 WriteEnable(0x06) 再写**，写完要等 BUSY 位清零，
 *       期间不能碰它（本驱动每步都自动等了）。
 *
 *  【接线（普中-天马 F407开发板）】
 *      SPI1 : SCK=PB3  MISO=PB4  MOSI=PB5
 *      FLASH_CS = PB14（PB14 还接在 JTAG 的 NJTRST 上，SYS_SPI_Init 会自动解占用）
 *      ⚠ 板载 NRF24L01 与 W25Q128 共用 SPI1，CS 分别是 PG7 与 PB14 ——
 *        用谁就把谁的 CS 拉低，另一个必须拉高（本模块只动自己那根）
 *
 *  【使用方式（存一段数据再读回来）】
 *      W25QXX_Init(0);                              // ① 10MHz 初始化
 *      printf("ID=%06X\r\n", W25QXX_ReadID());       // ② 应为 0xEF4018
 *      W25QXX_WriteSafe(data, 0x000000, len);       // ③ 自动擦+写（安全）
 *      W25QXX_Read(rbuf, 0x000000, len);            // ④ 读回
 *
 *  移植指引 : 换 CS 引脚改 W25QXX_CS_PORT / W25QXX_CS_PIN；
 *             换到别的 SPI 改 W25QXX_SPI_ID。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
#define W25QXX_SPI_ID       SYS_SPI_1        /* 本板挂 SPI1（PB3/PB4/PB5） */
#define W25QXX_SPI_SPEED    10000000UL       /* 10MHz；线长/干扰大时降到 2MHz */

#define W25QXX_CS_PORT      GPIOB
#define W25QXX_CS_PIN       GPIO_Pin_14      /* FLASH_CS */

/* 容量与擦除单位（W25Q128 的值；换型号只改这三个数） */
#define W25QXX_SIZE_BYTES   (16UL * 1024UL * 1024UL)    /* 16MB */
#define W25QXX_SECTOR_SIZE  4096UL                      /* 最小擦除单位 4KB */
#define W25QXX_PAGE_SIZE    256U                        /* 最大一次写 256 字节 */

/* 通用 JEDEC ID（低位 2 字节即容量：0x18 = 128Mbit） */
#define W25QXX_ID_W25Q128   0xEF4018UL

/* ---- 建议的 Flash 分区（16MB 很大，随便切） ---- */
#define W25QXX_ADDR_PARAM   0x000000UL      /* 0x000000  参数/配置（1 扇区） */
#define W25QXX_ADDR_LOG     0x001000UL      /* 0x001000  运行日志（滚存） */
#define W25QXX_ADDR_FONT    0x020000UL      /* 0x020000  字库/图片（按需） */


/* ================================================================
 *                    区块 2：基础功能（把 Flash 当字节数组用）
 * ================================================================ */
/* 初始化：配好 SPI1 + CS 引脚 + 退出掉电模式
 * 参数 : speed —— SPI 速率 Hz（0 = W25QXX_SPI_SPEED）
 * 返回 : 0 = 成功；1 = 读不到 ID（查 CS/接线/供电）
 * 示例 : if (W25QXX_Init(0) != 0) printf("W25Q 没应答\r\n"); */
uint8_t W25QXX_Init(uint32_t speed);

/* 读 JEDEC ID（3 字节）：W25Q128 应为 0xEF4018
 * 说明 : 这是"判断芯片到底通没通"最快的一招——比查寄存器靠谱得多
 * 示例 : uint32_t id = W25QXX_ReadID();
 *        if (id == W25QXX_ID_W25Q128) { ... } */
uint32_t W25QXX_ReadID(void);

/* 读数据（任意地址、任意长度，无对齐要求）
 * 参数 : dst —— 目标缓冲；addr —— 24 位地址；len —— 长度
 * 返回 : 0 = 成功；1 = 参数非法或越界
 * 示例 : W25QXX_Read(buf, 0x1000, 100); */
uint8_t W25QXX_Read(uint8_t *dst, uint32_t addr, uint32_t len);

/* 写数据（**要求目标区域已擦除**；内部自动按 256 字节页拆分）
 * 返回 : 0 = 成功；1 = 参数非法/越界；2 = 等待 BUSY 超时
 * 说明 : 比 WriteSafe 快得多，适合"整扇区刚擦完接着整块写"的场景
 * 示例 : W25QXX_EraseSector(0x1000);  W25QXX_Write(buf, 0x1000, 4096); */
uint8_t W25QXX_Write(const uint8_t *src, uint32_t addr, uint32_t len);

/* 写数据（**安全版：自动读-擦-写**，不改动目标扇区里的其它数据）
 * 返回 : 0 = 成功；1 = 参数非法；2 = 超时
 * 说明 : 内部有一个 4KB 的静态缓冲（ZI +4KB），所以**不能重入**
 *        （别同时在主循环和中断里调），也**别放中断里**（一次要 ~50ms）
 * 示例 : W25QXX_WriteSafe(&cfg, W25QXX_ADDR_PARAM, sizeof(cfg));  // 存参数首选 */
uint8_t W25QXX_WriteSafe(const uint8_t *src, uint32_t addr, uint32_t len);

/* 擦除一个 4KB 扇区（addr 落在扇区内即可，会自动对齐）
 * 返回 : 0 = 成功；1 = 越界；2 = 超时 */
uint8_t W25QXX_EraseSector(uint32_t addr);

/* 擦除一个 64KB 块（比擦 16 个扇区快很多，批量清零用它） */
uint8_t W25QXX_EraseBlock(uint32_t addr);

/* 全片擦除（16MB 约需 20~40 秒！只在"恢复出厂"时用） */
uint8_t W25QXX_EraseChip(void);

/* 读回一段校验是不是 0xFF（"查空"，判断这块区域还能不能直接写）
 * 返回 : 1 = 全 0xFF（空）；0 = 有数据 */
uint8_t W25QXX_IsBlank(uint32_t addr, uint32_t len);


/* ================================================================
 *                    区块 3：低层原语（想自己实现更复杂的时序时用）
 * ================================================================ */
/* 等 BUSY 位清零（写/擦除完成）。返回 0 = 正常；2 = 超时
 * 说明 : 一般不用自己调——Write/Erase 内部都等了 */
uint8_t W25QXX_WaitBusy(void);

/* 进/出低功耗模式（省电用；ExitPowerDown 约 3µs 就能唤醒） */
void W25QXX_PowerDown(void);
void W25QXX_WakeUp(void);

#endif /* __FWLIB_W25QXX_H */
