#include "sys_eth.h"
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_ms 等）独立文件 */
#include "stm32f4x7_eth.h"      /* ST 官方驱动（.\ETH 目录，include 路径已配置） */
#include <string.h>

/* sys_eth.c: 以太网 MAC 驱动（LAN8720 + RMII）实现: bsp(时钟/GPIO AF11/
 * RMII/PHY 复位) → STM32F4x7_ETH_Driver(MAC+DMA/自协商) → 本文件接口
 * (Init/SMI/链路/收发)。网线未插时 ETH_Init 等链路超时返回 2，插好后
 * 重新调用 SYS_ETH_Init。收发按描述符轮询: memcpy 后 Prepare 交 DMA。 */

/* 官方驱动内部全局变量（库头未 extern 导出，按定义原样声明） */
extern __IO ETH_DMADESCTypeDef       *DMATxDescToSet;      /* 下一个可用的 Tx 描述符 */
extern __IO ETH_DMA_Rx_Frame_infos   *DMA_RX_FRAME_infos;  /* 收帧信息（多段计数） */


/* ---- 硬件层辅助（bsp）---- */
/* RMII 选择: 写 SYSCFG->PMC 的 MII_RMII_SEL 位（bit23，1 = RMII） */
static void eth_rmii_select(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    SYSCFG->PMC |= (1UL << 23);          /* MII_RMII_SEL：1 = RMII */
}

/* RMII 引脚初始化: 9 根线复用 AF11(GPIO_AF_ETH)，推挽 GPIO_OType_PP + 100MHz */
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

    /* REF_CLK / MDIO / CRS_DV → GPIOA */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource1, GPIO_AF_ETH);   /* REF_CLK */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_ETH);   /* MDIO    */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_ETH);   /* CRS_DV  */

    gi.GPIO_Pin = SYS_ETH_REFCLK_PIN; GPIO_Init(GPIOA, &gi);
    gi.GPIO_Pin = SYS_ETH_MDIO_PIN;   GPIO_Init(GPIOA, &gi);
    gi.GPIO_Pin = SYS_ETH_CRSDV_PIN;  GPIO_Init(GPIOA, &gi);

    /* MDC / RXD0 / RXD1 → GPIOC */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource1, GPIO_AF_ETH);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource4, GPIO_AF_ETH);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource5, GPIO_AF_ETH);
    gi.GPIO_Pin = SYS_ETH_MDC_PIN;    GPIO_Init(GPIOC, &gi);
    gi.GPIO_Pin = SYS_ETH_RXD0_PIN;   GPIO_Init(GPIOC, &gi);
    gi.GPIO_Pin = SYS_ETH_RXD1_PIN;   GPIO_Init(GPIOC, &gi);

    /* TX_EN / TXD0 / TXD1 → GPIOG */
    GPIO_PinAFConfig(GPIOG, GPIO_PinSource11, GPIO_AF_ETH);
    GPIO_PinAFConfig(GPIOG, GPIO_PinSource13, GPIO_AF_ETH);
    GPIO_PinAFConfig(GPIOG, GPIO_PinSource14, GPIO_AF_ETH);
    gi.GPIO_Pin = SYS_ETH_TXEN_PIN;   GPIO_Init(GPIOG, &gi);
    gi.GPIO_Pin = SYS_ETH_TXD0_PIN;   GPIO_Init(GPIOG, &gi);
    gi.GPIO_Pin = SYS_ETH_TXD1_PIN;   GPIO_Init(GPIOG, &gi);
}

/* PHY 硬复位: nRST 拉低 100ms 后释放，再等 100ms（LAN8720 复位后需稳定时间才响应 SMI） */
static void eth_phy_hw_reset(void)
{
    GPIO_OutInit(SYS_ETH_PHY_RST_PORT, SYS_ETH_PHY_RST_PIN);

    GPIO_OutReset(SYS_ETH_PHY_RST_PORT, SYS_ETH_PHY_RST_PIN);   /* 复位中 */
    delay_ms(100);
    GPIO_OutSet  (SYS_ETH_PHY_RST_PORT, SYS_ETH_PHY_RST_PIN);   /* 释放 */
    delay_ms(100);
}


/* 初始化: 时钟/GPIO/RMII/PHY 复位 → 外设复位 → PHY 预检 → ETH_Init
 * (自协商+等链路) → MAC 地址 → 启动收发。返回 0 成功(链路已通)，1 PHY ID
 * 全 0/全 1，2 ETH_Init 失败，3 软复位超时 */
uint8_t SYS_ETH_Init(void)
{
    ETH_InitTypeDef ei;
    uint8_t  mac[6] = { SYS_ETH_MAC0, SYS_ETH_MAC1, SYS_ETH_MAC2,
                        SYS_ETH_MAC3, SYS_ETH_MAC4, SYS_ETH_MAC5 };
    uint16_t id1;

    /* 时钟: MAC 三路 + 引脚端口; RMII 选择; PHY 硬复位 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_ETH_MAC    |
                           RCC_AHB1Periph_ETH_MAC_Tx |
                           RCC_AHB1Periph_ETH_MAC_Rx, ENABLE);
    eth_gpio_init();
    eth_rmii_select();
    eth_phy_hw_reset();

    /* 外设复位: 寄存器恢复缺省 + DMA 软复位 */
    ETH_DeInit();
    ETH_SoftwareReset();

    /* 软复位等待必须带超时: MAC 时钟未起时 SWR 恒为 SET，无超时会死在
     * 初始化中。正常几十微秒完成，上限 SYS_ETH_RESET_TIMEOUT_US */
    {
        uint32_t waited = 0UL;
        while (ETH_GetSoftwareResetStatus() == SET) {
            delay_us(100U);                 /* 步进 100us，最多等到 100ms */
            waited += 100UL;
            if (waited > (SYS_ETH_RESET_TIMEOUT_US / 100UL)) return 3U;
        }
    }

    /* PHY 预检: 读 PHY_ID1，全 0/全 1 表示 SMI 不通（查供电/PD3/MDC-MDIO/PHY 地址宏） */
    id1 = ETH_ReadPHYRegister(SYS_ETH_PHY_ADDR, PHY_ID1);
    if (id1 == 0xFFFFU || id1 == 0x0000U) return 1U;

    /* ETH 驱动初始化: MAC+DMA+PHY 自协商，ETH_StructInit 默认值起步 */
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
    ei.ETH_UnicastFramesFilter   = ETH_UnicastFramesFilter_Perfect;  /* 只收自己的+广播 */

    /* 无网线时等链路超时返回失败（插线后重新调用） */
    if (ETH_Init(&ei, SYS_ETH_PHY_ADDR) == ETH_ERROR) return 2U;

    /* 本机 MAC 地址: Address0 完美过滤，只收本机单播+广播 */
    ETH_MACAddressConfig(ETH_MAC_Address0, mac);
    ETH_MACAddressPerfectFilterCmd(ETH_MAC_Address0, ENABLE);

    /* 启动 MAC 收发与 DMA */
    ETH_Start();

    return 0U;
}

/* SMI 读 PHY 寄存器; PHY 不在时读回全 0/全 1 */
uint16_t SYS_ETH_ReadPHY(uint8_t reg)
{
    return ETH_ReadPHYRegister(SYS_ETH_PHY_ADDR, (uint16_t)reg);
}

/* SMI 写 PHY 寄存器：0 = 成功，1 = 超时/失败 */
uint8_t SYS_ETH_WritePHY(uint8_t reg, uint16_t value)
{
    return (ETH_WritePHYRegister(SYS_ETH_PHY_ADDR, (uint16_t)reg, value) != 0U) ? 0U : 1U;
}

/* 链路状态: 读两次 BMSR，第一次清锁存位，第二次取实时值 */
uint8_t SYS_ETH_LinkUp(void)
{
    (void)ETH_ReadPHYRegister(SYS_ETH_PHY_ADDR, PHY_BSR);   /* 第一次：清锁存 */
    return (ETH_ReadPHYRegister(SYS_ETH_PHY_ADDR, PHY_BSR) & PHY_Linked_Status) ? 1U : 0U;
}

/* 发送一帧: 描述符忙则重试; 0 成功，1 参数非法或重试耗尽 */
uint8_t SYS_ETH_SendFrame(const uint8_t *buf, uint16_t len)
{
    __IO ETH_DMADESCTypeDef *tx;
    uint8_t  *dst;
    uint32_t  tries;

    if (buf == 0 || len < 14U || len > 1514U) return 1U;   /* 长度不合法 */

    for (tries = 0; tries < SYS_ETH_TX_LOOPS; tries++) {
        tx = DMATxDescToSet;

        /* OWN=0 归 CPU 才可写入，否则重试 */
        if ((tx->Status & ETH_DMATxDesc_OWN) == (uint32_t)RESET) {
            dst = (uint8_t *)tx->Buffer1Addr;
            memcpy(dst, buf, len);

            /* 交给 DMA（长度≤1514 只用单个缓冲） */
            if (ETH_Prepare_Transmit_Descriptors(len) != ETH_ERROR) return 0U;
        }

        {   /* 忙等待一小段（约几微秒级；不会死等） */
            volatile uint32_t d;
            for (d = 0; d < 2000U; d++);
        }
    }
    return 1U;      /* 重试耗尽：未初始化 / 网线没通 */
}

/* 收一帧(非阻塞): 返回帧长; -1 无帧/空帧/参数非法，-2 帧长超缓冲 */
int32_t SYS_ETH_RecvFrame(uint8_t *buf, uint16_t maxlen)
{
    FrameTypeDef frame;
    __IO ETH_DMADESCTypeDef *desc;
    uint32_t i;

    if (buf == 0 || maxlen == 0U) return -1;

    if (ETH_CheckFrameReceived() == 0U) return -1;      /* 当前没有完整帧 */

    frame = ETH_Get_Received_Frame();                   /* 取帧（并前进读指针） */

    if (frame.length != 0U && frame.length <= maxlen) {
        memcpy(buf, (const void *)frame.buffer, frame.length);
    }

    /* 归还描述符给 DMA（多段帧按 Seg_Count 逐个归还） */
    desc = frame.descriptor;
    for (i = 0; i < DMA_RX_FRAME_infos->Seg_Count; i++) {
        desc->Status = ETH_DMARxDesc_OWN;
        desc = (__IO ETH_DMADESCTypeDef *)(desc->Buffer2NextDescAddr);
    }
    DMA_RX_FRAME_infos->Seg_Count = 0U;

    /* RBUS 置位时清标志并恢复 RX DMA 轮询 */
    if ((ETH->DMASR & ETH_DMASR_RBUS) != (uint32_t)RESET) {
        ETH->DMASR   = ETH_DMASR_RBUS;                  /* 写 1 清除 */
        ETH->DMARPDR = 0U;                              /* 恢复接收轮询 */
    }

    if (frame.length == 0U) return -1;                  /* 空帧（异常） */
    if (frame.length > maxlen) return -2;               /* 帧长超缓冲：已丢弃 */
    return (int32_t)frame.length;
}
