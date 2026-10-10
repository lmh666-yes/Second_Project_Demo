#include "sys_eth.h"
/* STM32F4x7 以太网 MAC 基础驱动（LAN8720 + RMII），实现文件 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_ms 等）独立文件 */
/* 本板不适用：普中-天马 F407 开发板没有以太网 PHY，原理图上标为以太网模块
 * 接口的插座插的是 NRF24L01（CN1），也没有 LAN8720 / RMII / RJ45 网络名。
 * 因此本文件默认不参与编译，SYS_ETH_ENABLE = 0，见 sys_eth.h。
 * 换带 LAN8720 的板子或接 SPI 网卡模块后，把开关置 1 即可。 */
#if (SYS_ETH_ENABLE)
#include "stm32f4x7_eth.h"      /* ST 官方驱动（.\ETH 目录，include 路径已配置） */
#include <string.h>

/* 三层结构：硬件层为本文件的 bsp 辅助，负责时钟、GPIO(AF11)、RMII 选择和 PHY
 * 硬复位，对应官方 eth_bsp.c 的角色；驱动层由 STM32F4x7_ETH_Driver 完成 MAC 与
 * DMA 配置及 PHY 自协商；封装层为本文件对外的 Init / SMI / 链路 / 收发四组接口。
 * 收发流程与官方 LwIP 端口的轮询实现一致：发送取当前 Tx 描述符的缓冲，memcpy
 * 后 Prepare 交给 DMA，描述符仍归 DMA 时短暂重试；接收先 CheckFrameReceived 判
 * 有无完整帧，再 Get_Received_Frame 取走并 memcpy，最后把用过的描述符归还 DMA。
 * 网线未插时 ETH_Init 内部等待链路会超时并返回 2，插好网线后重新调用即可。 */

/* 官方驱动内部全局变量（库头未导出 extern，应用层按定义原样声明） */
extern __IO ETH_DMADESCTypeDef       *DMATxDescToSet;      /* 下一个可用的 Tx 描述符 */
extern __IO ETH_DMA_Rx_Frame_infos   *DMA_RX_FRAME_infos;  /* 收帧信息（多段计数） */


/* 硬件层辅助（bsp） */
/* RMII 模式选择：写 SYSCFG->PMC 的 MII_RMII_SEL 位，1 = RMII
 * F4 的 MII/RMII 切换不在 MAC 里，而在 SYSCFG 的 bit23 */
static void eth_rmii_select(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    SYSCFG->PMC |= (1UL << 23);          /* MII_RMII_SEL：1 = RMII */
}

/* RMII 引脚初始化：9 根线全部复用为 AF11（GPIO_AF_ETH）
 * 统一推挽复用（GPIO_Mode_AF + GPIO_OType_PP），速度档 GPIO_Speed_100MHz */
static void eth_gpio_init(void)
{
    GPIO_InitTypeDef gi;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA |
                           RCC_AHB1Periph_GPIOC |
                           RCC_AHB1Periph_GPIOG, ENABLE);

    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    /* REF_CLK / MDIO / CRS_DV 至 GPIOA */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource1, GPIO_AF_ETH);   /* REF_CLK */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_ETH);   /* MDIO    */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_ETH);   /* CRS_DV  */

    gi.GPIO_Pin = SYS_ETH_REFCLK_PIN; GPIO_Init(GPIOA, &gi);
    gi.GPIO_Pin = SYS_ETH_MDIO_PIN;   GPIO_Init(GPIOA, &gi);
    gi.GPIO_Pin = SYS_ETH_CRSDV_PIN;  GPIO_Init(GPIOA, &gi);

    /* MDC / RXD0 / RXD1 至 GPIOC */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource1, GPIO_AF_ETH);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource4, GPIO_AF_ETH);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource5, GPIO_AF_ETH);
    gi.GPIO_Pin = SYS_ETH_MDC_PIN;    GPIO_Init(GPIOC, &gi);
    gi.GPIO_Pin = SYS_ETH_RXD0_PIN;   GPIO_Init(GPIOC, &gi);
    gi.GPIO_Pin = SYS_ETH_RXD1_PIN;   GPIO_Init(GPIOC, &gi);

    /* TX_EN / TXD0 / TXD1 至 GPIOG */
    GPIO_PinAFConfig(GPIOG, GPIO_PinSource11, GPIO_AF_ETH);
    GPIO_PinAFConfig(GPIOG, GPIO_PinSource13, GPIO_AF_ETH);
    GPIO_PinAFConfig(GPIOG, GPIO_PinSource14, GPIO_AF_ETH);
    gi.GPIO_Pin = SYS_ETH_TXEN_PIN;   GPIO_Init(GPIOG, &gi);
    gi.GPIO_Pin = SYS_ETH_TXD0_PIN;   GPIO_Init(GPIOG, &gi);
    gi.GPIO_Pin = SYS_ETH_TXD1_PIN;   GPIO_Init(GPIOG, &gi);
}

/* PHY 硬复位：nRST 拉低保持 100ms，释放后再等 100ms 稳定
 * LAN8720 上电或复位后需要一段稳定时间才能响应 SMI。
 * SYS_ETH_PHY_RST_ENABLE = 0 时本函数不占用任何引脚，见 sys_eth.h */
#if SYS_ETH_PHY_RST_ENABLE
static void eth_phy_hw_reset(void)
{
    GPIO_OutInit(SYS_ETH_PHY_RST_PORT, SYS_ETH_PHY_RST_PIN);

    GPIO_OutReset(SYS_ETH_PHY_RST_PORT, SYS_ETH_PHY_RST_PIN);   /* 复位中 */
    delay_ms(100);
    GPIO_OutSet  (SYS_ETH_PHY_RST_PORT, SYS_ETH_PHY_RST_PIN);   /* 释放 */
    delay_ms(100);
}
#else
/* 无复位脚：只做上电稳定等待，保持调用点一致 */
static void eth_phy_hw_reset(void)
{
    delay_ms(20);
}
#endif


/* 初始化，成功时链路已通，失败原因见返回值
 * 流程：时钟、GPIO、RMII、PHY 复位，外设复位，PHY 通信预检，
 * 官方 ETH_Init（含自协商与等链路），写 MAC 地址，启动收发 */
uint8_t SYS_ETH_Init(void)
{
    ETH_InitTypeDef ei;
    uint8_t  mac[6] = { SYS_ETH_MAC0, SYS_ETH_MAC1, SYS_ETH_MAC2,
                        SYS_ETH_MAC3, SYS_ETH_MAC4, SYS_ETH_MAC5 };
    uint16_t id1;

    /* 1) 时钟：MAC 三路加引脚端口；RMII 选择；PHY 硬复位 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_ETH_MAC    |
                           RCC_AHB1Periph_ETH_MAC_Tx |
                           RCC_AHB1Periph_ETH_MAC_Rx, ENABLE);
    eth_gpio_init();
    eth_rmii_select();
    eth_phy_hw_reset();

    /* 2) 外设复位：寄存器恢复缺省，DMA 软复位等完成 */
    ETH_DeInit();
    ETH_SoftwareReset();

    /* 等待软复位完成必须带超时：ETH_GetSoftwareResetStatus() 读的是 MAC 里的
     * SWR 位，由 ETH 外设自己的时钟驱动。MAC 时钟没起来时 SWR 永远为 SET，
     * 裸 while 会死循环在 ETH 初始化里。正常几十微秒完成，超时后返回 3。 */
    {
        uint32_t t0 = DWT_GetUs();
        while (ETH_GetSoftwareResetStatus() == SET) {
            if (DWT_ElapsedUs(t0) > SYS_ETH_RESET_TIMEOUT_US) return 3U;
        }
    }

    /* 3) PHY 通信预检：读 PHY ID1，全 0 或全 1 说明 SMI 不通，返回 1
     * 检查项：PHY 供电、PD3 复位脚、MDC/MDIO 接线、PHY 地址宏 */
    id1 = ETH_ReadPHYRegister(SYS_ETH_PHY_ADDR, PHY_ID1);
    if (id1 == 0xFFFFU || id1 == 0x0000U) return 1U;

    /* 4) 官方驱动初始化：MAC、DMA、PHY 自协商
     * 各字段含义见 ETH/stm32f4x7_eth.h 的字段注释 */
    ETH_StructInit(&ei);
    ei.ETH_AutoNegotiation       = ETH_AutoNegotiation_Enable;       /* 自协商 */
    ei.ETH_Speed                 = ETH_Speed_100M;                   /* 预置（协商后库会修正） */
    ei.ETH_Mode                  = ETH_Mode_FullDuplex;
    ei.ETH_LoopbackMode          = ETH_LoopbackMode_Disable;
    ei.ETH_RetryTransmission     = ETH_RetryTransmission_Disable;
    ei.ETH_AutomaticPadCRCStrip  = ETH_AutomaticPadCRCStrip_Disable;
    ei.ETH_ReceiveAll            = ETH_ReceiveAll_Disable;
    ei.ETH_BroadcastFramesReception = ETH_BroadcastFramesReception_Enable;
    ei.ETH_PromiscuousMode       = ETH_PromiscuousMode_Disable;
    ei.ETH_MulticastFramesFilter = ETH_MulticastFramesFilter_Perfect;
    ei.ETH_UnicastFramesFilter   = ETH_UnicastFramesFilter_Perfect;  /* 只收本机单播和广播 */

    /* 无网线时这里等链路超时并返回失败，插线后重新调用即可 */
    if (ETH_Init(&ei, SYS_ETH_PHY_ADDR) == ETH_ERROR) return 2U;

    /* 5) 本机 MAC 地址写入 Address0，配合完美过滤只收本机单播和广播 */
    ETH_MACAddressConfig(ETH_MAC_Address0, mac);
    ETH_MACAddressPerfectFilterCmd(ETH_MAC_Address0, ENABLE);

    /* 6) 启动 MAC 收发与 DMA，初始化完成，可开始收发帧 */
    ETH_Start();

    return 0U;
}

/* SMI 读 PHY 寄存器，直通官方实现；PHY 不在时读回全 0 或全 1 */
uint16_t SYS_ETH_ReadPHY(uint8_t reg)
{
    return ETH_ReadPHYRegister(SYS_ETH_PHY_ADDR, (uint16_t)reg);
}

/* SMI 写 PHY 寄存器：0 = 成功，1 = 超时/失败 */
uint8_t SYS_ETH_WritePHY(uint8_t reg, uint16_t value)
{
    return (ETH_WritePHYRegister(SYS_ETH_PHY_ADDR, (uint16_t)reg, value) != 0U) ? 0U : 1U;
}

/* 链路状态：读两次 BMSR，第一次清锁存位，第二次反映实时状态 */
uint8_t SYS_ETH_LinkUp(void)
{
    (void)ETH_ReadPHYRegister(SYS_ETH_PHY_ADDR, PHY_BSR);   /* 第一次：清锁存 */
    return (ETH_ReadPHYRegister(SYS_ETH_PHY_ADDR, PHY_BSR) & PHY_Linked_Status) ? 1U : 0U;
}

/* 发送一帧，描述符忙时短暂重试；成功表示已交给 DMA 发送 */
uint8_t SYS_ETH_SendFrame(const uint8_t *buf, uint16_t len)
{
    __IO ETH_DMADESCTypeDef *tx;
    uint8_t  *dst;
    uint32_t  tries;

    if (buf == 0 || len < 14U || len > 1514U) return 1U;   /* 长度不合法 */

    for (tries = 0; tries < SYS_ETH_TX_LOOPS; tries++) {
        tx = DMATxDescToSet;

        /* 描述符归 CPU（OWN=0）才可写入，否则正在发送，稍等重试 */
        if ((tx->Status & ETH_DMATxDesc_OWN) == (uint32_t)RESET) {
            dst = (uint8_t *)tx->Buffer1Addr;
            memcpy(dst, buf, len);

            /* 交给 DMA，成功即返回；长度不超过 1514 只用到单个缓冲 */
            if (ETH_Prepare_Transmit_Descriptors(len) != ETH_ERROR) return 0U;
        }

        {   /* 忙等一小段，约几微秒 */
            volatile uint32_t d;
            for (d = 0; d < 2000U; d++);
        }
    }
    return 1U;      /* 重试耗尽：未初始化或网线没通 */
}

/* 收一帧，非阻塞；流程与官方 LwIP 端口一致，含描述符归还 */
int32_t SYS_ETH_RecvFrame(uint8_t *buf, uint16_t maxlen)
{
    FrameTypeDef frame;
    __IO ETH_DMADESCTypeDef *desc;
    uint32_t i;

    if (buf == 0 || maxlen == 0U) return -1;

    if (ETH_CheckFrameReceived() == 0U) return -1;      /* 当前没有完整帧 */

    frame = ETH_Get_Received_Frame();                   /* 取帧并前进读指针 */

    if (frame.length != 0U && frame.length <= maxlen) {
        memcpy(buf, (const void *)frame.buffer, frame.length);
    }

    /* 归还描述符给 DMA，多段帧按段数逐个归还，官方轮询流程 */
    desc = frame.descriptor;
    for (i = 0; i < DMA_RX_FRAME_infos->Seg_Count; i++) {
        desc->Status = ETH_DMARxDesc_OWN;
        desc = (__IO ETH_DMADESCTypeDef *)(desc->Buffer2NextDescAddr);
    }
    DMA_RX_FRAME_infos->Seg_Count = 0U;

    /* 接收缓冲不可用（RBUS）置位时清掉并唤醒 RX DMA */
    if ((ETH->DMASR & ETH_DMASR_RBUS) != (uint32_t)RESET) {
        ETH->DMASR   = ETH_DMASR_RBUS;                  /* 写 1 清除 */
        ETH->DMARPDR = 0U;                              /* 恢复接收轮询 */
    }

    if (frame.length == 0U) return -1;                  /* 空帧，异常 */
    if (frame.length > maxlen) return -2;               /* 帧长超缓冲：已丢弃 */
    return (int32_t)frame.length;
}
#endif /* SYS_ETH_ENABLE */
/* --- end of sys_eth.c --- */
