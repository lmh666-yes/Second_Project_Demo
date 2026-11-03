#ifndef __FWLIB_SYS_SPI_H
#define __FWLIB_SYS_SPI_H

#include "stm32f4xx.h"

/*
 *  sys_spi.h : SPI 主机模块头文件
 *  标准外设库 SPI 主机驱动：提供按字节同步收发的原语，
 *  命令码与寄存器访问等器件协议由上层驱动完成。
 *  标准库调用 : SPI_StructInit / SPI_Init / SPI_Cmd / SPI_I2S_SendData /
 *               SPI_I2S_ReceiveData / SPI_I2S_GetFlagStatus / GPIO_PinAFConfig
 *
 *  本板接线（普中-天马 F407 开发板原理图）:
 *      SPI1 : SCK = PB3   MISO = PB4   MOSI = PB5  (AF5)
 *             板载 W25Q128 与 NRF24L01 共用，W25Q128 片选 F_CS = PB14
 *      SPI2 : SCK = PB13  MISO = PB14  MOSI = PB15 (AF5)
 *             本板 PB13/PB14/PB15 被 I2S_SCLK / FLASH_CS / LCD_BL 占用，不可用
 *      SPI3 : SCK = PB3   MISO = PB4   MOSI = PB5  (AF6)
 *             与 SPI1 同引脚不同外设，二选一
 *  SPI1 上两个从机靠各自的片选分时复用，同一时刻只能选中一个。
 *  PB3/PB4 上电默认是 JTAG 的 JTDO/NJTRST，启用 SPI1 前必须关闭 JTAG
 *  （SYS_SPI_Init 已自动处理），只关 JTAG 并保留 SWD（PA13/PA14 不受影响）。
 *  片选不在本模块内：CS 用普通 GPIO 输出，收发前拉低、收发后拉高。 */


/* -------------------- 引脚定义 -------------------- */
#define SYS_SPI1_SCK_PORT   GPIOB
#define SYS_SPI1_SCK_PIN    GPIO_Pin_3
#define SYS_SPI1_MISO_PORT  GPIOB
#define SYS_SPI1_MISO_PIN   GPIO_Pin_4
#define SYS_SPI1_MOSI_PORT  GPIOB
#define SYS_SPI1_MOSI_PIN   GPIO_Pin_5

#define SYS_SPI2_SCK_PORT   GPIOB
#define SYS_SPI2_SCK_PIN    GPIO_Pin_13
#define SYS_SPI2_MISO_PORT  GPIOB
#define SYS_SPI2_MISO_PIN   GPIO_Pin_14
#define SYS_SPI2_MOSI_PORT  GPIOB
#define SYS_SPI2_MOSI_PIN   GPIO_Pin_15

#define SYS_SPI3_SCK_PORT   GPIOB
#define SYS_SPI3_SCK_PIN    GPIO_Pin_3
#define SYS_SPI3_MISO_PORT  GPIOB
#define SYS_SPI3_MISO_PIN   GPIO_Pin_4
#define SYS_SPI3_MOSI_PORT  GPIOB
#define SYS_SPI3_MOSI_PIN   GPIO_Pin_5

/* -------------------- 忙等超时 -------------------- */
/* 等 BSY / TXE / RXNE 标志的最大循环次数，跑满即放弃并返回失败
 * 按 168MHz 每循环约 3~6 拍估算，20 万次约 3.6~7ms；SPI 最慢挡（PCLK/256）
 * 发一个字节几十微秒，余量足够。只有忘调 SYS_SPI_Init、时钟没开、
 * 引脚被占才会跑满，此时必须返回，否则占用总线的任务永久挂死。 */
#define SYS_SPI_TIMEOUT     200000UL

/* -------------------- 默认速率 -------------------- */
/* SYS_SPI_Init 传入 0 时使用该默认值（1MHz） */
#define SYS_SPI_DEFAULT_SPEED   1000000UL

/* -------------------- JTAG 释放 -------------------- */
/* 1 = 所用引脚落在 PB3/PB4 上（JTAG 的 JTDO/NJTRST）时自动关闭 JTAG，
 *     只关 JTAG 并保留 SWD 调试；换到其它引脚时此逻辑自动跳过。
 *     不关 JTAG 时 PB3/PB4 被调试口占用，SPI 收发全是 0 */
#define SYS_SPI_FREE_JTAG       1


/* SPI 编号（与内部配置表一一对应） */
typedef enum {
    SYS_SPI_1 = 0,          /* SPI1 引脚 PB3 / PB4 / PB5 (AF5) */
    SYS_SPI_2 = 1,          /* SPI2 引脚 PB13 / PB14 / PB15 (AF5) */
    SYS_SPI_3 = 2,          /* SPI3 引脚 PB3 / PB4 / PB5 (AF6) */
    SYS_SPI_COUNT = 3
} SysSpiId_t;

/* 时钟模式（对照器件手册的 CPOL/CPHA 选择）
 *   MODE_0 : SPI_CPOL_Low  + SPI_CPHA_1Edge，空闲低，上升沿采样（最常用）
 *   MODE_1 : SPI_CPOL_Low  + SPI_CPHA_2Edge
 *   MODE_2 : SPI_CPOL_High + SPI_CPHA_1Edge
 *   MODE_3 : SPI_CPOL_High + SPI_CPHA_2Edge（W25Q128 支持 MODE_0/3） */
typedef enum {
    SYS_SPI_MODE_0 = 0,
    SYS_SPI_MODE_1 = 1,
    SYS_SPI_MODE_2 = 2,
    SYS_SPI_MODE_3 = 3
} SysSpiMode_t;

/* 初始化 SPI 主机：时钟 / 引脚复用 / 速率 / 模式
 *
 * 内部依次调用:
 *   RCC_APB1/2PeriphClockCmd 开 SPI 总线时钟（SPI1 在 APB2，SPI2/3 在 APB1）
 *   引脚在 PB3/PB4 时: RCC_APB2PeriphClockCmd(SYSCFG) + SYSCFG->MEMRMP 释放 JTAG
 *   GPIO_PinAFConfig + GPIO_Init 引脚复用 SCK/MISO/MOSI
 *   SPI_StructInit + SPI_Init 速率、模式、全双工主机（SPI_Direction_2Lines_FullDuplex）
 *   SPI_Cmd 使能 SPI
 *
 * 参数 : id,SPI 编号，SYS_SPI_1 / SYS_SPI_2 / SYS_SPI_3
 *        speed,速率 Hz，0 表示用默认值 1MHz；实际取不超过 speed 的最高可达速率
 *        mode,时钟模式，SYS_SPI_MODE_0 / _1 / _2 / _3，见上方枚举注释 */
void SYS_SPI_Init(SysSpiId_t id, uint32_t speed, SysSpiMode_t mode);

/* 全双工收发一个字节：发送 tx，同时返回收到的字节
 * 只操作数据寄存器，不碰片选，CS 由上层先拉低再调用
 * 标准库 : SPI_I2S_GetFlagStatus(SPI_I2S_FLAG_TXE 等空) + SPI_I2S_SendData +
 *          SPI_I2S_GetFlagStatus(SPI_I2S_FLAG_RXNE 等满) + SPI_I2S_ReceiveData */
uint8_t SYS_SPI_TransferByte(SysSpiId_t id, uint8_t tx);

/* 连续收发：txbuf 与 rxbuf 可为 NULL
 *   txbuf = NULL → 发送 0xFF（读器件时的空拍占位）
 *   rxbuf = NULL → 丢弃收到的数据（只写器件时） */
void SYS_SPI_Transfer(SysSpiId_t id, const uint8_t *txbuf, uint8_t *rxbuf, uint16_t len);

/* 忙等超时计数（诊断用）：非 0 说明有传输没等到标志
 * 原因 = 没调 SYS_SPI_Init（SPE=0）、片选没拉低、接线松。
 * 自检前调 SYS_SPI_TimeoutClear()，结束后查 SYS_SPI_TimeoutCount() 是否为 0 */
uint16_t SYS_SPI_TimeoutCount(void);
void     SYS_SPI_TimeoutClear(void);

/* 同一总线挂多个从机时的互斥由调用方负责，本驱动不提供锁，
 * 也不碰片选（CS）。
 * 各从机 CS 独立，同一时刻只能拉低一根；一个事务是拉低 CS、发命令、
 * 收发数据、拉高 CS 的整段过程，中途被其它任务插入同样会出错。
 * SPI1 上 W25Q128 与 NRF24L01 插座（本板 CN4）的互斥见 w25qxx.h 区块 4 的
 * W25QXX_Lock() / W25QXX_Unlock()；总线被占用时公开操作返回 4（总线忙）。 */


/* 只写：连续发送 len 个字节，丢弃回读数据 */
void SYS_SPI_Write(SysSpiId_t id, const uint8_t *buf, uint16_t len);

/* 只读：连续接收 len 个字节（发送 0xFF 产生空时钟） */
void SYS_SPI_Read(SysSpiId_t id, uint8_t *buf, uint16_t len);

/* 运行中改速率（如 W25Q128 上电用低速读 ID，通过后再提速）
 * 会等当前传输结束后再改，运行中可调用 */
void SYS_SPI_SetSpeed(SysSpiId_t id, uint32_t speed);

/*
 *  SPI_TypeDef（定义在 stm32f4xx.h）:
 *    CR1     控制 1:SPI_CPOL_Low/_High + SPI_CPHA_1Edge/_2Edge(时钟模式) /
 *            SPI_Mode_Master 主机模式 / SPI_BaudRatePrescaler_2~256 速率分频 /
 *            SPI 使能（SPI_Init、SPI_Cmd、SetSpeed 写它）
 *    CR2     控制 2:中断/DMA 相关,库未用
 *    SR      状态:SPI_I2S_FLAG_TXE（发送空）/ _RXNE（收到）/ _BSY（忙）,
 *            收发前轮询（TransferByte 里的两个 while）
 *    DR      数据:写入 = 发送、读出 = 接收（SPI_I2S_SendData/ReceiveData）
 *    CRCPR / RXCRCR / TXCRCR   CRC 校验:库未使用
 *    I2SCFGR / I2SPR           I2S 模式:本库只用 SPI,未使用
 *
 *  SYSCFG_TypeDef（要点行,完整表见 sys_exti.h）:
 *    MEMRMP  SWJ_CFG 位选择调试口占用,置 0b010 = 只关 JTAG 保留 SWD;
 *            SYS_SPI_Init 释放 PB3/PB4 的 JTAG 占用时写它 */

#endif /* __FWLIB_SYS_SPI_H */
