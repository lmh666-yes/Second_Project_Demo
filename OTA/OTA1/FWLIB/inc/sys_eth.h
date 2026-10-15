#ifndef __FWLIB_SYS_ETH_H
#define __FWLIB_SYS_ETH_H

#include "stm32f4xx.h"

/* sys_eth.h — 以太网 MAC 基础驱动（LAN8720 + RMII）
 * 功能：SMI（PHY 管理）+ MAC/DMA 初始化 + 原始以太网帧收发；不含 TCP/IP 协议栈，接 LwIP 时本驱动即其网卡层。
 * 依据：ST 官方 STM32F4x7_ETH_Driver（工程 ETH\ 目录，本模块为薄封装）；RMII 时序见 sys_eth.c。
 * 接线（GEC-M4 原理图 · RMII）：REF_CLK=PA1、MDIO=PA2、MDC=PC1、CRS_DV=PA7、RXD0=PC4、RXD1=PC5、
 *        TX_EN=PG11、TXD0=PG13、TXD1=PG14、PHY nRST=PD3（低有效）；PA2 与 USART2_TX 复用，用网口时 USART2 不可用。
 * ================================================================ */


/* -------------------- 区块 1：定义与宏定义区（换板子只改这里） -------------------- */
/* -------------------- RMII 引脚（复用 ETH，AF11） -------------------- */
#define SYS_ETH_REFCLK_PORT   GPIOA
#define SYS_ETH_REFCLK_PIN    GPIO_Pin_1
#define SYS_ETH_MDIO_PORT     GPIOA
#define SYS_ETH_MDIO_PIN      GPIO_Pin_2
#define SYS_ETH_MDC_PORT      GPIOC
#define SYS_ETH_MDC_PIN       GPIO_Pin_1
#define SYS_ETH_CRSDV_PORT    GPIOA
#define SYS_ETH_CRSDV_PIN     GPIO_Pin_7
#define SYS_ETH_RXD0_PORT     GPIOC
#define SYS_ETH_RXD0_PIN      GPIO_Pin_4
#define SYS_ETH_RXD1_PORT     GPIOC
#define SYS_ETH_RXD1_PIN      GPIO_Pin_5
#define SYS_ETH_TXEN_PORT     GPIOG
#define SYS_ETH_TXEN_PIN      GPIO_Pin_11
#define SYS_ETH_TXD0_PORT     GPIOG
#define SYS_ETH_TXD0_PIN      GPIO_Pin_13
#define SYS_ETH_TXD1_PORT     GPIOG
#define SYS_ETH_TXD1_PIN      GPIO_Pin_14

/* -------------------- PHY 控制 -------------------- */
/* nRST 低电平复位；本模块初始化时拉低约 100ms 再拉高释放。 */
#define SYS_ETH_PHY_RST_PORT  GPIOD      /* LAN8720 硬复位脚（nRST，低有效） */
#define SYS_ETH_PHY_RST_PIN   GPIO_Pin_3
#define SYS_ETH_PHY_ADDR      0          /* LAN8720 地址由 PHYAD0 决定，通常 0 */

/* -------------------- 本机 MAC 地址 -------------------- */
/* 六字节 MAC；默认 02:00:00:12:34:56（本地实验用；正式产品需分配） */
#define SYS_ETH_MAC0          0x02
#define SYS_ETH_MAC1          0x00
#define SYS_ETH_MAC2          0x00
#define SYS_ETH_MAC3          0x12
#define SYS_ETH_MAC4          0x34
#define SYS_ETH_MAC5          0x56

/* -------------------- 超时 -------------------- */
/* 等 MAC 软复位完成（SWR 位自动清 0）的上限，单位微秒。SWR 由 ETH 外设时钟驱动，
 * 时钟未起时该位保持 SET；正常复位约几十微秒，此处留三个数量级余量。 */
#define SYS_ETH_RESET_TIMEOUT_US  100000UL

#define SYS_ETH_LINK_LOOPS    20000000UL   /* 等自协商完成的循环上限 */
#define SYS_ETH_TX_LOOPS      10000UL      /* 发送重试上限：每轮约 10us，合计约 100ms */

/* -------------------- 区块 2：基础功能 -------------------- */
/* 初始化：时钟/引脚 → PHY 复位 → MAC+DMA → PHY 自协商 → 等链路。
 * 库内调用：RCC_APB2PeriphClockCmd(SYSCFG) + RCC_AHB1PeriphClockCmd(GPIOx) 开时钟；
 *   GPIO_PinAFConfig(GPIO_AF_ETH) + GPIO_Init 复用 11 根 RMII 引脚；RCC_AHB1PeriphClockCmd(ETH MAC/Tx/Rx)
 *   开 ETH 外设时钟；ETH_DeInit + ETH_SoftwareReset 复位 MAC+DMA；ETH_ReadPHYRegister 读 PHYID；
 *   ETH_StructInit + ETH_Init 配 MAC 与自协商；ETH_MACAddressConfig + ETH_Start 写入 MAC 并启动。
 * 返回：0 = 成功；1 = PHY 无响应（SMI 读不通）；2 = 自协商超时；3 = 链路未建立（网线未插或对端未通） */
uint8_t SYS_ETH_Init(void);

/* SMI 读 PHY 寄存器（MDC/MDIO 管理接口），库函数 ETH_ReadPHYRegister
 * 参数：reg — PHY 寄存器号（IEEE 标准区 0~31），常用 0x00 BCR（控制）、0x01 BSR（状态）、
 *       0x02~0x03 PHYID、0x1F SR（LAN8720 特殊模式：速度/双工判定，见 ETH 目录） */
uint16_t SYS_ETH_ReadPHY(uint8_t reg);

/* SMI 写 PHY 寄存器（ETH_WritePHYRegister）：0 = 成功，1 = 超时（PHY 无响应）
 * 参数：reg — 同 SYS_ETH_ReadPHY；value — 16 位寄存器值，整寄存器覆盖写；
 *       改单个位域需先读回、改位、再写回 */
uint8_t SYS_ETH_WritePHY(uint8_t reg, uint16_t value);

/* 链路状态：1 = 链路已建立（网线通），0 = 未连接；实现为读 BSR 状态寄存器
 * （第一次读清锁存位） */
uint8_t SYS_ETH_LinkUp(void);

/* 发送一帧（阻塞式：等 DMA 发出为止），经官方 ETH 驱动库 MAC+DMA 描述符接口，详见 ETH 目录
 * 参数：buf — 帧内容（目的 MAC + 源 MAC + 类型 + 数据，至少 14 字节）；
 *       len — 帧长度 46 ~ 1500 字节，不含前导码/FCS（硬件补 FCS）
 * 返回：0 = 已发出；1 = 参数非法或发送超时（网线未通时返回 1） */
uint8_t SYS_ETH_SendFrame(const uint8_t *buf, uint16_t len);

/* 收一帧（非阻塞轮询：有帧就取走，无帧立即返回）
 * 参数：buf — 接收缓冲；maxlen — 缓冲容量
 * 返回：≥0 = 收到的帧长（字节）；-1 = 当前没有帧；-2 = 帧长超过缓冲（该帧已丢弃）
 * 实现：ETH_CheckFrameReceived + 官方 ETH 驱动库取帧接口 */
int32_t SYS_ETH_RecvFrame(uint8_t *buf, uint16_t maxlen);


/* -------------------- 区块 3：扩展功能（预留） --------------------
 * 预留方向：中断/DMA 回调式收包、收发统计计数（好帧/坏帧/丢弃帧）、FreeRTOS 收发队列、
 *           LwIP 协议栈集成（本驱动即网卡层，接 ethernetif.c） */

#endif /* __FWLIB_SYS_ETH_H */
