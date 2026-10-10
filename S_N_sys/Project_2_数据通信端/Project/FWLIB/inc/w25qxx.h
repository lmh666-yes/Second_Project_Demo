#ifndef __FWLIB_W25QXX_H
#define __FWLIB_W25QXX_H

#include "stm32f4xx.h"
#include "sys_spi.h"

/* W25Q 系列 SPI Flash 驱动头文件，依赖 sys_spi.h
 * 本板 W25Q128 挂 SPI1：SCK=PB3、MISO=PB4、MOSI=PB5，片选 CS=PB14
 * PB14 复用 JTAG 的 NJTRST，SYS_SPI_Init 会解除占用
 * NRF24L01 与 W25Q128 共用 SPI1，CS 分别是 PG7 与 PB14，同一时刻只能有一个片选拉低 */


/* 换板子只改本区的宏 */
#define W25QXX_SPI_ID       SYS_SPI_1        /* 本板挂 SPI1（PB3/PB4/PB5） */
#define W25QXX_SPI_SPEED    10000000UL       /* 10MHz；线长或干扰大时降到 2MHz */

#define W25QXX_CS_PORT      GPIOB
#define W25QXX_CS_PIN       GPIO_Pin_14      /* FLASH_CS */

/* 容量与擦除单位，W25Q128 的值；换型号只改这四个数 */
#define W25QXX_SIZE_BYTES   (16UL * 1024UL * 1024UL)    /* 16MB */
#define W25QXX_SECTOR_SIZE  4096UL                      /* 最小擦除单位 4KB */
#define W25QXX_BLOCK_SIZE   (64UL * 1024UL)             /* 块擦除单位 64KB（EraseBlock 用） */
#define W25QXX_PAGE_SIZE    256U                        /* 最大一次写 256 字节 */

/* 判器件只看容量字节，不认厂商字节：各家用不同厂商码，等值比较必失败
 * 0x18 = 128Mbit = 16MB，GD25Q128(0xC84018) 与 Winbond(0xEF4018) 都命中 */
#define W25QXX_ID_CAP_128MBIT   0x18UL          /* JEDEC ID 低字节：容量 = 128Mbit */
#define W25QXX_ID_W25Q128       0xEF4018UL      /* 仅供打印辨认型号，不作 Init 判据 */

/* Flash 分区建议 */
#define W25QXX_ADDR_PARAM   0x000000UL      /* 0x000000  参数/配置（1 扇区） */
#define W25QXX_ADDR_LOG     0x001000UL      /* 0x001000  运行日志（滚存） */
#define W25QXX_ADDR_FONT    0x020000UL      /* 0x020000  字库/图片（按需） */


/* 初始化：配 SPI1 与 CS 引脚，退出掉电模式
 * 参数 : speed 为 SPI 速率 Hz；0 = W25QXX_SPI_SPEED
 * 返回 : 0 = 成功；1 = 读不到 ID */
uint8_t W25QXX_Init(uint32_t speed);

/* 读 JEDEC ID（3 字节），W25Q128 为 0xEF4018
 * 返回 : 0xFFFFFFFF = 无器件或总线被占用 */
uint32_t W25QXX_ReadID(void);

/* 读数据，无对齐要求
 * 参数 : dst 为目标缓冲；addr 为 24 位地址；len 为长度
 * 返回 : 0 = 成功；1 = 参数非法或越界；4 = 总线忙 */
uint8_t W25QXX_Read(uint8_t *dst, uint32_t addr, uint32_t len);

/* 写数据，要求目标区域已擦除；内部按 256 字节页拆分
 * 返回 : 0 = 成功；1 = 参数非法或越界；2 = 等待 BUSY 超时；4 = 总线忙 */
uint8_t W25QXX_Write(const uint8_t *src, uint32_t addr, uint32_t len);

/* 写数据，安全版：自动读-擦-写，不改动目标扇区内的其它数据
 * 返回 : 0 = 成功；1 = 参数非法或越界；2 = 超时或擦写失败；4 = 模块忙
 * 说明 : 内部有 4KB 静态缓冲（ZI +4KB），不可重入；抢不到锁返回 4 时本次不做任何改动
 *        调用方拿到 4 时跳过本次、下次再写；一次调用约 50ms，不可在中断里调用 */
uint8_t W25QXX_WriteSafe(const uint8_t *src, uint32_t addr, uint32_t len);

/* 擦除一个 4KB 扇区，addr 必须 4KB 对齐
 * 返回 : 0 = 成功；1 = 越界或未对齐；2 = 超时；3 = 写使能失败
 * 说明 : 芯片发擦除命令只看地址高位，非对齐地址会擦掉整个 0x1000 扇区且不报错
 *        本驱动对未对齐地址返回 1，不静默多擦
 *        要擦包含 addr 的扇区，先自行 addr &= ~0xFFFUL */
uint8_t W25QXX_EraseSector(uint32_t addr);

/* 擦除一个 64KB 块，addr 必须 64KB 对齐，比擦 16 个扇区快
 * 返回 : 0 = 成功；1 = 越界或未对齐；2 = 超时；3 = 写使能失败 */
uint8_t W25QXX_EraseBlock(uint32_t addr);

/* 全片擦除，16MB 约需 20~40 秒，只在恢复出厂时用 */
uint8_t W25QXX_EraseChip(void);

/* 读回一段判断是否全为 0xFF，即该区域能否直接写入
 * 返回 : 1 = 全 0xFF（空）；0 = 有数据或总线忙 */
uint8_t W25QXX_IsBlank(uint32_t addr, uint32_t len);


/* 等 BUSY 位清零，即写或擦除完成
 * 返回 : 0 = 正常；2 = 超时
 * 说明 : Write 与 Erase 内部已等待，一般不必自己调 */
uint8_t W25QXX_WaitBusy(void);

/* 进低功耗模式；WakeUp 退出，约 3µs 唤醒 */
void W25QXX_PowerDown(void);
void W25QXX_WakeUp(void);

/* SPI1 总线占用锁，供同一总线上的第二个器件（NRF24L01）使用
 * 本模块公开操作执行期间都持此锁，同时保护共用的 4KB 扇区缓冲
 * 非阻塞：拿不到锁立刻返回 0；总线被占用时公开操作返回 4 且本次不做任何动作
 * W25QXX_IsBlank 例外返回 0，W25QXX_ReadID 返回 0xFFFFFFFF
 * 不可在中断里调用（内部短暂关中断）；不可重入，同一上下文内不可嵌套调用 */
uint8_t W25QXX_Lock(void);      /* 1 = 拿到总线；0 = 已被别人占用（稍后重试） */
void    W25QXX_Unlock(void);     /* 释放总线（没拿也调不坏，直接置空闲） */
uint8_t W25QXX_IsBusy(void);     /* 1 = 总线正被占用（诊断/打印用） */

#endif /* __FWLIB_W25QXX_H */
