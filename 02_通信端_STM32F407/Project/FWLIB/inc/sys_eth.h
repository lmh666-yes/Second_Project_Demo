#ifndef __FWLIB_SYS_ETH_H
#define __FWLIB_SYS_ETH_H

#include "stm32f4xx.h"

/* sys_eth.h：以太网 MAC 基础驱动（LAN8720 + RMII）
 * SMI(PHY 管理) + MAC/DMA 初始化 + 原始以太网帧收发，不含 TCP/IP 协议栈
 * 依托 ST 官方 STM32F4x7_ETH_Driver，源码在工程 ETH\ 目录
 *
 * 本板接线（普中-天马 F407开发板原理图，RMII）:
 *      REF_CLK = PA1     MDIO = PA2      MDC  = PC1
 *      CRS_DV  = PA7     RXD0 = PC4      RXD1 = PC5
 *      TX_EN   = PG11    TXD0 = PG13     TXD1 = PG14
 *
 * 本板没有以太网 PHY，RMII 信号也未引到接插件，本文件是从参考工程保留的模板，
 * 在本板上不可用：PA2 已作 USART2_TX/RS485，PG11 悬空，PG13/PG14 未引出。
 * 保持下面 SYS_ETH_ENABLE = 0 时本模块不编译进固件。 */


/* 区块 1：定义与宏定义区 */
/* 0 = 不编译本模块；有 LAN8720 的板子改成 1
 * sys_eth.c 的全部内容被此宏包住，置 0 时不产生代码与依赖 */
#ifndef SYS_ETH_ENABLE
#define SYS_ETH_ENABLE   0
#endif

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

/* PHY 硬复位开关：
 *   1 = 初始化时先复位 PHY，需要板上连到 PHY 的复位脚
 *   0 = 跳过硬复位，PHY 在外部模块上、无复位脚时用（天马板填 0）
 * 复位极性按 nRST 低有效 */
#define SYS_ETH_PHY_RST_ENABLE 0

/* 下面两项仅在 SYS_ETH_PHY_RST_ENABLE = 1 时生效：参考板 GEC-M4 nRST = PD3 */
#define SYS_ETH_PHY_RST_PORT  GPIOD
#define SYS_ETH_PHY_RST_PIN   GPIO_Pin_3
#define SYS_ETH_PHY_ADDR      0          /* LAN8720 地址由 PHYAD0 决定，通常 0 */

/* -------------------- 本机 MAC 地址 -------------------- */
/* 六字节 MAC；默认 02:00:00:12:34:56（本地实验用，正式产品请分配 */
#define SYS_ETH_MAC0          0x02
#define SYS_ETH_MAC1          0x00
#define SYS_ETH_MAC2          0x00
#define SYS_ETH_MAC3          0x12
#define SYS_ETH_MAC4          0x34
#define SYS_ETH_MAC5          0x56

/* 等 MAC 软复位（SWR 位自动清 0）的上限，单位微秒。
 * SWR 由 ETH 外设自己的时钟驱动，MAC 时钟没起来时它一直是 SET，
 * 无上限就是死循环；正常复位几十微秒完成，100 ms 为超时上限 */
#define SYS_ETH_RESET_TIMEOUT_US  100000UL

#define SYS_ETH_LINK_LOOPS    20000000UL   /* 等自协商完成的循环上限 */
#define SYS_ETH_TX_LOOPS      10000UL      /* 发送重试上限：每轮约 10us，合计约 100ms */


/* 区块 2：基础功能 */
/* 初始化：时钟/引脚 → PHY 复位 → MAC+DMA → PHY 自协商 → 等链路
 * 返回 : 0 = 成功；1 = PHY 无响应(SMI 读不通)；2 = 自协商超时；
 *        3 = 链路未建立（网线没插/对端没通） */
uint8_t SYS_ETH_Init(void);

#if (SYS_ETH_ENABLE)

/* SMI 读 PHY 寄存器（MDC/MDIO 管理接口）
 * 参数 : reg, PHY 寄存器号（IEEE 标准区 0~31），常用:
 *                0x00 BCR(控制) / 0x01 BSR(状态) / 0x02~0x03 PHYID /
 *                0x1F SR(LAN8720 特殊模式:速度/双工判定) */
uint16_t SYS_ETH_ReadPHY(uint8_t reg);

/* SMI 写 PHY 寄存器：0 = 成功，1 = 超时（PHY 无响应）
 * 参数 : reg, 同 SYS_ETH_ReadPHY 的常用值；value, 16 位寄存器值
 *        整寄存器覆盖写；只改某几位时先读回、改位、写回 */
uint8_t SYS_ETH_WritePHY(uint8_t reg, uint16_t value);

/* 链路状态：1 = 链路已建立（网线通），0 = 未连接
 * 读 BSR 状态寄存器，第一次读清锁存 */
uint8_t SYS_ETH_LinkUp(void);

/* 发送一帧（阻塞式：等 DMA 发出为止）
 * 参数 : buf, 帧内容（目的 MAC 6 字节 + 源 MAC 6 字节 + 类型 2 字节 + 数据），至少 14 字节
 *        len, 帧长度（46 ~ 1500 字节，不含前导码/FCS，硬件补 FCS）
 * 返回 : 0 = 已发出；1 = 参数非法 / 发送超时（网线未通时为 1） */
uint8_t SYS_ETH_SendFrame(const uint8_t *buf, uint16_t len);

/* 收一帧（非阻塞轮询：有帧就取走，没帧立即返回）
 * 参数 : buf, 接收缓冲；maxlen, 缓冲容量
 * 返回 : 不小于 0 = 收到的帧长（字节）；-1 = 当前没有帧；
 *        -2 = 帧长超过缓冲（该帧已丢弃） */
int32_t SYS_ETH_RecvFrame(uint8_t *buf, uint16_t maxlen);


/* 区块 3：扩展功能（待补充） */


/* 上面的函数声明只在 SYS_ETH_ENABLE = 1 时可见。
 * sys_eth.c 的实现包在 #if (SYS_ETH_ENABLE) 里且默认值为 0；
 * 声明一起隐藏后，调用方编译期即报隐式声明，不必等到链接报 undefined symbol */
#endif /* (SYS_ETH_ENABLE) */

#endif /* __FWLIB_SYS_ETH_H */
