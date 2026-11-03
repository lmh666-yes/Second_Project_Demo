#ifndef __FWLIB_SYS_SPI_H
#define __FWLIB_SYS_SPI_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_spi.h — SPI 主机模块头文件
 * ================================================================
 *  功能：同步全双工收发原语（发一字节、收一字节）；命令码、寄存器等
 *        板级协议由上层器件驱动完成；不控制片选，不提供总线互斥。
 *  本板接线（对照 GEC-M4 原理图；换板改"区块 1"）：
 *      SPI1：SCK=PB3  MISO=PB4  MOSI=PB5  AF5 — 板载 W25Q128 / NRF24L01
 *            共用；片选独立，F_CS=PB14（通用 GPIO，由上层驱动拉）
 *      SPI2：SCK=PB13 MISO=PB14 MOSI=PB15 AF5 — 与 F_CS(PB14) 冲突，
 *            本板不推荐，仅保留配置
 *      SPI3：SCK=PB3  MISO=PB4  MOSI=PB5  AF6 — 与 SPI1 同引脚，二选一
 *  约束：PB3/PB4 上电为 JTAG 的 JTDO/NJTRST，启用 SPI1 前须关 JTAG、保留
 *        SWD（PA13/PA14）；SYS_SPI_Init 已处理。片选由上层驱动收发前后拉低/拉高。
 *  详细说明见 检测数据端设计.md
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
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
/* 等 BSY / TXE / RXNE 标志的最大循环次数，跑满即放弃并返回失败。
 * 数值出处：168MHz 下每循环约 3~6 拍，20 万次 ≈ 3.6~7ms；SPI 最慢挡
 * （PCLK/256）发一个字节仅几十微秒。SPE=0、片选未拉低、接线松时跑满。 */
#define SYS_SPI_TIMEOUT     200000UL

/* -------------------- 默认速率 -------------------- */
/* SYS_SPI_Init 传入 0 时使用的默认速率（1MHz） */
#define SYS_SPI_DEFAULT_SPEED   1000000UL

/* -------------------- JTAG 释放 -------------------- */
/* 1 = 所用引脚在 PB3/PB4（JTAG 的 JTDO/NJTRST）时自动关闭 JTAG：写
 *     SYSCFG->MEMRMP 的 SWJ_CFG = 0b010，只关 JTAG、保留 SWD，其余引脚
 *     自动跳过；不关 JTAG 时 PB3/PB4 被调试口占用，SPI 收发全为 0 */
#define SYS_SPI_FREE_JTAG       1


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* SPI 编号（与内部配置表一一对应） */
typedef enum {
    SYS_SPI_1 = 0,          /* SPI1 → PB3 / PB4 / PB5 (AF5) */
    SYS_SPI_2 = 1,          /* SPI2 → PB13 / PB14 / PB15 (AF5) */
    SYS_SPI_3 = 2,          /* SPI3 → PB3 / PB4 / PB5 (AF6) */
    SYS_SPI_COUNT = 3
} SysSpiId_t;

/* 时钟模式（对照器件手册的 CPOL/CPHA 选择）
 *   MODE_0 : CPOL=SPI_CPOL_Low,  CPHA=SPI_CPHA_1Edge — 空闲低，上升沿采样
 *   MODE_1 : CPOL=SPI_CPOL_Low,  CPHA=SPI_CPHA_2Edge
 *   MODE_2 : CPOL=SPI_CPOL_High, CPHA=SPI_CPHA_1Edge
 *   MODE_3 : CPOL=SPI_CPOL_High, CPHA=SPI_CPHA_2Edge（W25Q128 支持 0/3） */
typedef enum {
    SYS_SPI_MODE_0 = 0,
    SYS_SPI_MODE_1 = 1,
    SYS_SPI_MODE_2 = 2,
    SYS_SPI_MODE_3 = 3
} SysSpiMode_t;

/* 初始化 SPI 主机：时钟 / 引脚复用 / 速率 / 模式。
 * 内部调用链：RCC_APB2/APB1PeriphClockCmd 开总线时钟（SPI1 在 APB2，
 * SPI2/3 在 APB1）→ SYSCFG->MEMRMP 释放 PB3/PB4 的 JTAG →
 * GPIO_PinAFConfig + GPIO_Init 引脚复用 → SPI_StructInit + SPI_Init
 * （SPI_Direction_2Lines_FullDuplex 全双工主机）→ SPI_Cmd 使能。
 * 参数：id — SYS_SPI_1 / _2 / _3；speed — 速率 Hz，取不超过该值的最高
 *       可达分频（SPI_BaudRatePrescaler_2 ~ _256，由 PCLK 分频），
 *       0 = 默认 1MHz；mode — SYS_SPI_MODE_0 ~ _3，CPOL/CPHA 见上方枚举。 */
void SYS_SPI_Init(SysSpiId_t id, uint32_t speed, SysSpiMode_t mode);

/* 全双工收发一个字节：发送 tx，同时返回收到的字节；只操作数据寄存器，
 * 不控制片选，调用前由上层拉低 CS。
 * 库调用：SPI_I2S_GetFlagStatus(TXE 发送空) + SPI_I2S_SendData +
 *         SPI_I2S_GetFlagStatus(RXNE 收到) + SPI_I2S_ReceiveData。 */
uint8_t SYS_SPI_TransferByte(SysSpiId_t id, uint8_t tx);

/* 连续收发 len 字节：txbuf = NULL 时发送 0xFF（读器件的空拍占位），
 * rxbuf = NULL 时丢弃回读数据（只写器件）。两段式拼接可完成器件的
 * "命令 + 数据"时序，命令码与数据段见器件手册。 */
void SYS_SPI_Transfer(SysSpiId_t id, const uint8_t *txbuf, uint8_t *rxbuf, uint16_t len);

/* 忙等超时计数（诊断用）：非 0 表示有传输未等到标志，常见原因是未调
 * SYS_SPI_Init（SPE=0）、片选未拉低、接线松。自检前调
 * SYS_SPI_TimeoutClear()，结束后查 SYS_SPI_TimeoutCount() 是否为 0。 */
uint16_t SYS_SPI_TimeoutCount(void);
void     SYS_SPI_TimeoutClear(void);

/* 总线互斥与片选：本驱动只操作时钟和数据，不控制 CS、不提供锁。
 * 同一 SPI 上挂多个从机（各自一根 CS）时，同一时刻只能有一个通信；
 * 一个事务（拉低 CS → 发命令 → 收发数据 → 拉高 CS）不可被打断，否则
 * 两个器件都会收到对方的时钟与数据，互斥由器件驱动负责（后加入的与
 * 先有的互斥）。SPI1 上 W25Q128 与 NRF24L01 的仲裁见 w25qxx.h"区块 4"
 * W25QXX_Lock() / W25QXX_Unlock()；总线被占时公开操作返回 4（总线忙）。 */


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 只写：连续发送 buf（丢弃回读数据）
 * 示例 : SYS_SPI_Write(SYS_SPI_1, cmd, 4);   // 发 4 字节命令 */
void SYS_SPI_Write(SysSpiId_t id, const uint8_t *buf, uint16_t len);

/* 只读：连续接收 len 个字节（发送 0xFF 产生空时钟）
 * 示例 : SYS_SPI_Read(SYS_SPI_1, data, 16);   // 读 16 字节 */
void SYS_SPI_Read(SysSpiId_t id, uint8_t *buf, uint16_t len);

/* 运行中改速率（如：W25Q128 上电用低速读 ID，通过后再提速）
 * 说明 : 会等当前传输结束再改，可安全调用
 * 示例 : SYS_SPI_SetSpeed(SYS_SPI_1, 10000000);   // 提到 10MHz */
void SYS_SPI_SetSpeed(SysSpiId_t id, uint32_t speed);


/* 所用寄存器（结构体定义见 stm32f4xx.h，位域完整表见 sys_exti.h）：
 *   SPI_TypeDef  CR1 = CPOL/CPHA、主机模式、速率分频
 *                （SPI_BaudRatePrescaler_2 ~ _256）、SPI 使能；
 *                SR 的 TXE = 发送空 / RXNE = 收到 / BSY = 忙（收发轮询 SR）；
 *                DR 写入 = 发送、读出 = 接收。
 *   SYSCFG_TypeDef  MEMRMP.SWJ_CFG = 0b010 只关 JTAG、保留 SWD。 */

#endif /* __FWLIB_SYS_SPI_H */
