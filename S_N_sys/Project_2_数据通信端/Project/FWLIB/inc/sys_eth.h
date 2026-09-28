#ifndef __FWLIB_SYS_ETH_H
#define __FWLIB_SYS_ETH_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_eth.h —— 【系统】以太网 MAC 基础驱动（LAN8720 + RMII）
 * ================================================================
 *  设计定位 : 板载以太网的"基础版"驱动 —— 把网口跑通到"能收发
 *             原始以太网帧"的最小闭环：
 *               SMI(PHY 管理) + MAC/DMA 初始化 + 原始帧收发
 *             （不含 TCP/IP 协议栈；需要联网协议时再配 LwIP，
 *               本驱动就是它的网卡层）
 *
 *  实现依托 : ST 官方 STM32F4x7_ETH_Driver（源码在工程 `ETH\` 目录，
 *             随库分发，许可见 ETH/README.md）—— 本模块是它的薄封装
 *  标准库关键词 : ETH_Init / ETH_StructInit / ETH_ReadPHYRegister / ETH_WritePHYRegister /
 *                 ETH_Start / ETH_MACAddressConfig（来自官方 ETH 驱动库;标准外设库不含 ETH）
 *
 *  本板接线（普中-天马 F407开发板原理图 · RMII 模式）:
 *      REF_CLK = PA1     MDIO = PA2      MDC  = PC1
 *      CRS_DV  = PA7     RXD0 = PC4      RXD1 = PC5
 *      TX_EN   = PG11    TXD0 = PG13     TXD1 = PG14
 *
 *  ⚠⚠⚠ 【重要·本板不适用】已经逐字核过《普中-天马 F407开发板原理图》：
 *      本板**没有以太网 PHY**，也**没有把 RMII 信号引到任何接插件**。
 *      证据：
 *        ① 全图搜不到 LAN8720 / PHY / RJ45 / TP_OUT / TP_IN / REF_CLK
 *           等任何以太网网络名；只有 MCU 引脚名里的 ETH_MII/ETH_RMII
 *           备用功能描述（属于芯片文档，不代表本板引出了）；
 *        ② 原理图上那个叫“以太网模块接口”的块，里面的 8 脚接插件
 *           是 CN1，实际引的是 NRF_CS/NRF_CE/NRF_IRQ/SPI1_*，
 *           即 **NRF24L01 无线模块座**（标题是原理图作者的遗留误标）。
 *      所以：本文件只是从参考工程保留下来的**通用模板代码**，
 *      在本板上不可用（上面那组 RMII 引脚也与本板现有外设
 *      相冲：PA2 是 USART2_TX/RS485、PG11 悬空、PG13/PG14 未引出）。
 *      把 SYS_ETH_ENABLE 置 0（默认）时，本模块不会编译进固件。
 *      要真正用以太网：买一块 SPI 接口的 W5500/ENC28J60 模块，
 *      接到 CN1（与 NRF24L01 共用那个座）或 P1 排针，再自己封一个
 *      SPI 网卡驱动；或者换一块带 LAN8720 的板子并把下面引脚改成
 *      真实接线。
 *
 *  使用方式 :
 *      #define SYS_ETH_ENABLE 1        // ① 先在硬件上确认有 PHY，再打开
 *      if (SYS_ETH_Init() == 0) {                     // ② 初始化(含自协商)
 *          SYS_ETH_SendFrame(frame, len);             // ③ 发一帧裸帧
 *          int32_t n = SYS_ETH_RecvFrame(rbuf, 512);  // ④ 轮询收一帧
 *      }
 *
 *  自测建议 : 接上路由器/交换机后
 *      ① SYS_ETH_LinkUp() 应返回 1（指示灯亮/闪）;
 *      ② 用"发 ARP 请求"或与电脑直连互发原始帧验证收发链路。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* -------------------- 总开关（本板默认关闭） -------------------- */
/* 0 = 不编译本模块（因本板无 PHY）；有 LAN8720 的板子改成 1
 * 说明 : sys_eth.c 的全部内容都被此宏包住（除了头文件声明），
 *        置 0 时整个模块不会产生任何代码与依赖。 */
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

/* -------------------- PHY 控制 -------------------- */
/* PHY 硬复位开关：
 *   1 = 初始化时先给 PHY 硬复位（需要板上真有连到 PHY 的复位脚）
 *   0 = 跳过硬复位（PHY 在外部模块上、无复位脚时用这个）
 * 【天马板】PHY 在"以太网模块接口"上，板上无复位脚 → 填 0
 * ⚠ 复位极性默认按 nRST（低有效）；若你的 PHY 是高电平复位，
 *    把 sys_eth.c 里 eth_phy_hw_reset() 的 Set/Reset 对调 */
#define SYS_ETH_PHY_RST_ENABLE 0

/* 以下两项仅在 SYS_ETH_PHY_RST_ENABLE = 1 时生效
 * 【参考板 GEC-M4】nRST = PD3；【天马板】无（PD3 是 DCMI_PWDN） */
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

/* -------------------- 超时（循环次数上限，防死等） -------------------- */
#define SYS_ETH_LINK_LOOPS    20000000UL   /* 等自协商完成的循环上限 */
#define SYS_ETH_TX_LOOPS      100000UL     /* 发送时描述符忙的重试上限 */


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：时钟/引脚 → PHY 复位 → MAC+DMA → PHY 自协商 → 等链路
 *
 * 标准库调用链（库内部依次调用，可对照学习）:
 *   ① RCC_APB2PeriphClockCmd(SYSCFG) + RCC_AHB1PeriphClockCmd(GPIOx)  开时钟
 *   ② GPIO_PinAFConfig(GPIO_AF_ETH) + GPIO_Init   11 根 RMII 引脚复用
 *   ③ RCC_AHB1PeriphClockCmd(ETH MAC/Tx/Rx)        开 ETH 外设时钟
 *   ④ ETH_DeInit + ETH_SoftwareReset  复位 MAC+DMA（等复位完成）
 *   ⑤ ETH_ReadPHYRegister             探测 PHY（读 PHYID）
 *   ⑥ ETH_StructInit + ETH_Init       MAC 参数 + PHY 自协商
 *   ⑦ ETH_MACAddressConfig + ETH_Start  写入 MAC 地址并启动
 *
 * 返回 : 0 = 成功；1 = PHY 无响应(SMI 读不通)；2 = 自协商超时；
 *        3 = 链路未建立（网线没插/对端没通）
 * 示例 : if (SYS_ETH_Init() == 0) {          // 先插网线再上电最顺
 *            ...开始收发...
 *        } */
uint8_t SYS_ETH_Init(void);

/* SMI 读 PHY 寄存器（MDC/MDIO 管理接口）
 * 参数 : reg —— PHY 寄存器号（IEEE 标准区 0~31），常用:
 *                0x00 BCR(控制) / 0x01 BSR(状态) / 0x02~0x03 PHYID /
 *                0x1F SR(LAN8720 特殊模式:速度/双工判定，见 ETH 目录)
 * 标准库 : ETH_ReadPHYRegister（来自官方 ETH 驱动库）
 * 示例 : uint16_t id1 = SYS_ETH_ReadPHY(0x02);   // 读 PHYID1 验证 SMI 通 */
uint16_t SYS_ETH_ReadPHY(uint8_t reg);

/* SMI 写 PHY 寄存器：0 = 成功，1 = 超时（PHY 无响应）
 * 参数 : reg —— 同 SYS_ETH_ReadPHY 的常用值；value —— 16 位寄存器值
 *              （整寄存器覆盖写；想改某几位:先读回 → 改位 → 写回）
 * 标准库 : ETH_WritePHYRegister
 * 示例 : SYS_ETH_WritePHY(0x00, 0x8000);   // BCR 写 1 触发 PHY 软复位 */
uint8_t SYS_ETH_WritePHY(uint8_t reg, uint16_t value);

/* 链路状态：1 = 链路已建立（网线通），0 = 未连接
 * 标准库 : ETH_ReadPHYRegister（读 BSR 状态寄存器;第一次读清锁存）
 * 示例 : if (SYS_ETH_LinkUp()) { ... }   // 网线接好且对端通电 */
uint8_t SYS_ETH_LinkUp(void);

/* 发送一帧（阻塞式：等 DMA 发出为止）
 * 参数 : buf —— 帧内容（目的 MAC + 源 MAC + 类型 + 数据，至少 14 字节）
 *        len —— 帧长度（46 ~ 1500 字节，不含前导码/FCS，硬件补 FCS）
 * 返回 : 0 = 已发出；1 = 参数非法 / 发送超时（网线未通时返回 1）
 * 标准库 : 经官方 ETH 驱动库的收发接口（MAC+DMA 描述符,详见 ETH 目录）
 * 示例 : uint8_t frame[64] = { ...目的MAC(6) + 源MAC(6) + 类型(2) + 数据... };
 *        SYS_ETH_SendFrame(frame, 64);   // 发一帧裸包（至少 14 字节）*/
uint8_t SYS_ETH_SendFrame(const uint8_t *buf, uint16_t len);

/* 收一帧（非阻塞轮询：有帧就取走，没帧立即返回）
 * 参数 : buf —— 接收缓冲；maxlen —— 缓冲容量
 * 返回 : ≥0 = 收到的帧长（字节）；-1 = 当前没有帧；
 *        -2 = 帧长超过缓冲（该帧已丢弃）
 * 标准库 : ETH_CheckFrameReceived + 官方 ETH 驱动库取帧接口
 * 示例 : int32_t n = SYS_ETH_RecvFrame(rbuf, 512);
 *        if (n >= 0) { ... }    // 收到 n 字节的帧 */
int32_t SYS_ETH_RecvFrame(uint8_t *buf, uint16_t maxlen);


/* ================================================================
 *                    区块 3：扩展功能（待补充）
 * ================================================================
 * 预留方向（需要时按本库统一风格添加）:
 *      - 中断/DMA 回调式收包（当前为轮询式基础版）
 *      - 收发统计计数（好帧/坏帧/丢弃帧）
 *      - 与 FreeRTOS 结合：任务 + 信号量做收发队列
 *      - LwIP 协议栈集成（本驱动即网卡层，接 ethernetif.c 即可）
 * ================================================================ */

#endif /* __FWLIB_SYS_ETH_H */
