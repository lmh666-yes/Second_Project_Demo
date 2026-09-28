#ifndef __FWLIB_SYS_SPI_H
#define __FWLIB_SYS_SPI_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_spi.h —— 【系统】SPI 主机模块  头文件
 * ================================================================
 *  设计定位 : 标准外设库 SPI 的"薄封装"—— "发一个字节收一个字节"
 *             的同步收发原语，驱动 OLED / W25Q128 / NRF24L01 等
 *             器件时，板级协议（命令码、寄存器）由上层驱动完成
 *  标准库关键词 : SPI_StructInit / SPI_Init / SPI_Cmd / SPI_I2S_SendData /
 *                 SPI_I2S_ReceiveData / SPI_I2S_GetFlagStatus / GPIO_PinAFConfig
 *
 *  本板接线（对照 GEC-M4 原理图确认；换板子改"区块 1"）:
 *      SPI1 : SCK = PB3   MISO = PB4   MOSI = PB5  (AF5)
 *             —— 板载 W25Q128(SPI Flash) / NRF24L01 共用
 *             —— 片选各自独立: F_CS = PB14（通用 GPIO，由上层驱动拉）
 *      SPI2 : SCK = PB13  MISO = PB14  MOSI = PB15 (AF5)
 *             —— 与 F_CS(PB14) 等信号冲突，本板不推荐，仅保留配置
 *      SPI3 : SCK = PB3   MISO = PB4   MOSI = PB5  (AF6)
 *             —— 与 SPI1 同引脚、不同外设，二选一
 *
 *  ⚠ PB3/PB4 上电默认是 JTAG 的 JTDO/NJTRST！启用 SPI1 前必须
 *    关闭 JTAG（本模块 SYS_SPI_Init 已自动处理），只关 JTAG、
 *    保留 SWD 调试（PA13/PA14 不受影响）；换用其它调试口时留意。
 *
 *  ⚠ 片选(CS)不在本模块内：习惯做法是 CS 用普通 GPIO 输出，
 *    收发前拉低、收发后拉高。示例（W25Q128，F_CS=PB14）:
 *        GPIO_OutInit(GPIOB, GPIO_Pin_14);
 *        GPIO_OutSet (GPIOB, GPIO_Pin_14);          // 空闲拉高
 *        ...
 *        GPIO_OutReset(GPIOB, GPIO_Pin_14);         // 选中
 *        SYS_SPI_TransferByte(SYS_SPI_1, 0x9F);     // 读 JEDEC ID
 *        ...
 *        GPIO_OutSet(GPIOB, GPIO_Pin_14);           // 释放
 *
 *  使用方式 :
 *      SYS_SPI_Init(SYS_SPI_1, 1000000, SYS_SPI_MODE_0);  // ① 1MHz 模式0
 *      uint8_t id = SYS_SPI_TransferByte(SYS_SPI_1, 0x9F); // ② 全双工收发
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

/* -------------------- 默认速率 -------------------- */
/* SYS_SPI_Init 传入 0 时使用该默认值（1MHz 保守值，先通再提速） */
#define SYS_SPI_DEFAULT_SPEED   1000000UL

/* -------------------- JTAG 释放 -------------------- */
/* 1 = 当所用引脚落在 PB3/PB4 上（JTAG 的 JTDO/NJTRST）时自动关闭 JTAG
 *     （只关 JTAG、保留 SWD 调试；换到其它引脚后此逻辑自动跳过，不碰调试口）
 *     不关 JTAG 时 PB3/PB4 被调试口占用，SPI 收发全是 0 */
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
 *   MODE_0 : CPOL=SPI_CPOL_Low,  CPHA=SPI_CPHA_1Edge —— 空闲低,上升沿采样（最常见）
 *   MODE_1 : CPOL=SPI_CPOL_Low,  CPHA=SPI_CPHA_2Edge
 *   MODE_2 : CPOL=SPI_CPOL_High, CPHA=SPI_CPHA_1Edge
 *   MODE_3 : CPOL=SPI_CPOL_High, CPHA=SPI_CPHA_2Edge（W25Q128 支持 0/3） */
typedef enum {
    SYS_SPI_MODE_0 = 0,
    SYS_SPI_MODE_1 = 1,
    SYS_SPI_MODE_2 = 2,
    SYS_SPI_MODE_3 = 3
} SysSpiMode_t;

/* 初始化 SPI 主机：时钟 / 引脚复用 / 速率 / 模式
 *
 * 标准库调用链（库内部依次调用，可对照学习）:
 *   ① RCC_APB1/2PeriphClockCmd  开 SPI 总线时钟（SPI1 在 APB2,SPI2/3 在 APB1）
 *   ② (引脚在 PB3/PB4 时) RCC_APB2PeriphClockCmd(SYSCFG) + SYSCFG->MEMRMP 释放 JTAG
 *   ③ GPIO_PinAFConfig + GPIO_Init  SCK/MISO/MOSI 引脚复用
 *   ④ SPI_StructInit + SPI_Init  速率 / 模式 / 全双工主机(SPI_Direction_2Lines_FullDuplex)
 *   ⑤ SPI_Cmd                    使能 SPI
 *
 * 参数 : id —— SPI 编号，三选一: SYS_SPI_1 / SYS_SPI_2 / SYS_SPI_3
 *        speed —— 速率 Hz（0 = 默认 1MHz）
 *              （实际取"不超过 speed 的最高可达速率"）
 *        mode —— 时钟模式，四选一: SYS_SPI_MODE_0 / _1 / _2 / _3
 *              （CPOL/CPHA 对照表见上方枚举注释;最常见 0 或 3） */
void SYS_SPI_Init(SysSpiId_t id, uint32_t speed, SysSpiMode_t mode);

/* 全双工收发一个字节：发送 tx，同时返回收到的字节
 * 说明 : 只操作数据寄存器，不碰片选——CS 由上层先拉低再调用
 * 标准库 : SPI_I2S_GetFlagStatus(TXE 等空) + SPI_I2S_SendData +
 *          SPI_I2S_GetFlagStatus(RXNE 等满) + SPI_I2S_ReceiveData
 * 示例 : GPIO_OutReset(GPIOB, GPIO_Pin_14);                 // ① 拉低 CS
 *        uint8_t id = SYS_SPI_TransferByte(SYS_SPI_1, 0x9F); // ② 读 JEDEC ID
 *        GPIO_OutSet  (GPIOB, GPIO_Pin_14);                 // ③ 释放 CS */
uint8_t SYS_SPI_TransferByte(SysSpiId_t id, uint8_t tx);

/* 连续收发：txbuf 与 rxbuf 可为 NULL
 *   txbuf = NULL → 发送 0xFF（读器件时的"空拍"占位）
 *   rxbuf = NULL → 丢弃收到的数据（只写器件时）
 * 示例 : uint8_t cmd[4] = {0x03, 0, 0, 0};      // W25Q128 读命令
 *        uint8_t data[16];
 *        SYS_SPI_Transfer(SYS_SPI_1, cmd, 0, 4);      // 先发命令
 *        SYS_SPI_Transfer(SYS_SPI_1, 0, data, 16);    // 再空拍读 16 字节
 * 扩展提示 : 任何器件的"命令+数据"组合都能用本函数两段式拼出来 */
void SYS_SPI_Transfer(SysSpiId_t id, const uint8_t *txbuf, uint8_t *rxbuf, uint16_t len);


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


/* ================================================================
 *  附:标准库结构体速查 —— SPI_TypeDef（定义在 stm32f4xx.h）
 * ================================================================
 *  成员一览（含库中用法）:
 *    CR1     控制 1:CPOL/CPHA(库宏 SYS_SPI_MODE_0 ~ SYS_SPI_MODE_3;底层
 *            SPI_CPOL_Low/_High + SPI_CPHA_1Edge/_2Edge) / 主机模式
 *            (SPI_Mode_Master) / 速率分频(SPI_BaudRatePrescaler_2 ~ _256) /
 *            SPI 使能（SPI_Init、SPI_Cmd、SetSpeed 写它）
 *    CR2     控制 2:中断/DMA 相关,库未用
 *    SR      状态:TXE = 发送空 / RXNE = 收到 / BSY = 忙（宏 SPI_I2S_FLAG_TXE/_RXNE/_BSY）
 *            （收发前都在轮询——TransferByte 里的两个 while）
 *    DR      数据:写入 = 发送、读出 = 接收（SPI_I2S_SendData/ReceiveData）
 *    CRCPR / RXCRCR / TXCRCR   CRC 校验:库未使用
 *    I2SCFGR / I2SPR           I2S 模式:本库只用 SPI,未使用
 *
 *  附:标准库结构体速查 —— SYSCFG_TypeDef（要点行,完整表见 sys_exti.h）
 *    MEMRMP  SWJ_CFG 位选择调试口占用——置 0b010 = 只关 JTAG 保留 SWD;
 *            SYS_SPI_Init 释放 PB3/PB4 的 JTAG 占用时写它
 * ================================================================ */

#endif /* __FWLIB_SYS_SPI_H */
