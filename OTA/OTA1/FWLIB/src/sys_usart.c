#include "sys_usart.h"
#include "gpio_core.h"
#include "delay.h"      /* delay_us 等延时函数 */
#include "sys_dma.h"

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

/* ========================================
 *  sys_usart.c — 串口(USART)模块实现（标准外设库）
 *  内部：usart_cfg[] 串口→硬件配置表(6 路)；rx_buf/head/tail 中断接收环形缓冲(每路一套)
 *  USART1/2/3_IRQHandler、UART4/5_IRQHandler、USART6_IRQHandler 本文件定义，应用层不得重复定义
 * ======================================== */


/* ====== 内部配置表与缓冲 ====== */
/* 串口编号（= SYS_USART_x 枚举顺序 = 数组下标）→ 硬件资源映射
 * usart 外设指针;tx/rx 收发引脚(端口+掩码);af 复用号;apb 总线(1=APB1,2=APB2);clk RCC 时钟位
 * irq 中断向量号;tx_stream/rx_stream/dma_ch 硬件固定 DMA 映射；换引脚只改 sys_usart.h 宏 */
typedef struct {
    USART_TypeDef *usart;
    GPIO_TypeDef  *tx_port;
    uint16_t       tx_pin;
    GPIO_TypeDef  *rx_port;
    uint16_t       rx_pin;
    uint8_t        af;        /* 引脚复用功能号（GPIO_AF_USARTx） */
    uint8_t        apb;       /* 1 = APB1, 2 = APB2 */
    uint32_t       clk;       /* 对应 RCC 时钟位 */
    IRQn_Type      irq;       /* 中断向量号 */
    DMA_Stream_TypeDef *tx_stream;   /* DMA 发送数据流（硬件固定） */
    DMA_Stream_TypeDef *rx_stream;   /* DMA 接收数据流（硬件固定） */
    uint32_t            dma_ch;      /* DMA 通道号（USART1~3 与 UART4/5 均为 4；USART6 为 5） */
} UsartCfg_t;

static const UsartCfg_t usart_cfg[SYS_USART_COUNT] = {
    { USART1, SYS_USART1_TX_PORT, SYS_USART1_TX_PIN, SYS_USART1_RX_PORT, SYS_USART1_RX_PIN,
      GPIO_AF_USART1, 2, RCC_APB2Periph_USART1, USART1_IRQn, DMA2_Stream7, DMA2_Stream5, DMA_Channel_4 },
    { USART2, SYS_USART2_TX_PORT, SYS_USART2_TX_PIN, SYS_USART2_RX_PORT, SYS_USART2_RX_PIN,
      GPIO_AF_USART2, 1, RCC_APB1Periph_USART2, USART2_IRQn, DMA1_Stream6, DMA1_Stream5, DMA_Channel_4 },
    { USART3, SYS_USART3_TX_PORT, SYS_USART3_TX_PIN, SYS_USART3_RX_PORT, SYS_USART3_RX_PIN,
      GPIO_AF_USART3, 1, RCC_APB1Periph_USART3, USART3_IRQn, DMA1_Stream3, DMA1_Stream1, DMA_Channel_4 },
    { UART4,  SYS_UART4_TX_PORT,  SYS_UART4_TX_PIN,  SYS_UART4_RX_PORT,  SYS_UART4_RX_PIN,
      GPIO_AF_UART4,  1, RCC_APB1Periph_UART4,  UART4_IRQn,  DMA1_Stream4, DMA1_Stream2, DMA_Channel_4 },
    { UART5,  SYS_UART5_TX_PORT,  SYS_UART5_TX_PIN,  SYS_UART5_RX_PORT,  SYS_UART5_RX_PIN,
      GPIO_AF_UART5,  1, RCC_APB1Periph_UART5,  UART5_IRQn,  DMA1_Stream7, DMA1_Stream0, DMA_Channel_4 },
    { USART6, SYS_USART6_TX_PORT, SYS_USART6_TX_PIN, SYS_USART6_RX_PORT, SYS_USART6_RX_PIN,
      GPIO_AF_USART6, 2, RCC_APB2Periph_USART6, USART6_IRQn, DMA2_Stream6, DMA2_Stream1, DMA_Channel_5 },
};

/* 编译期护栏：配置表项数必须与 SYS_USART_COUNT 一致 */
typedef char usart_cfg_count_check[(sizeof(usart_cfg) / sizeof(usart_cfg[0]) == SYS_USART_COUNT) ? 1 : -1];

/* 中断接收环形缓冲（每路独立；head 中断推进，tail 主循环推进） */
static volatile uint8_t  rx_buf[SYS_USART_COUNT][SYS_USART_RX_BUF_SIZE];
static volatile uint16_t rx_head[SYS_USART_COUNT];
static volatile uint16_t rx_tail[SYS_USART_COUNT];

/* DMA 接收状态：active 是否正在用 DMA 收;done 是否检测到一帧收完(空闲/收满)
 * len 本次收到的字节数;max 本次缓冲上限(用于反推已收长度) */
static volatile uint8_t  rx_dma_active[SYS_USART_COUNT];
static volatile uint8_t  rx_dma_done[SYS_USART_COUNT];
static volatile uint16_t rx_dma_len[SYS_USART_COUNT];
static volatile uint16_t rx_dma_max[SYS_USART_COUNT];


/* ====== 内部辅助 ====== */
/* 引脚掩码 → 复用编号：走 gpio_core 的 GPIO_PinSource */


/* ====== 基础功能 ====== */
/* 初始化：时钟 → 引脚复用 → 串口参数 */
void SYS_USART_Init(SysUsartId_t id, uint32_t baudrate)
{
    const UsartCfg_t *p;
    USART_InitTypeDef ui;
    GPIO_InitTypeDef  gi;

    if (id >= SYS_USART_COUNT) return;
    p = &usart_cfg[id];

    if (baudrate == 0U) baudrate = SYS_USART_DEFAULT_BAUD;

    /* 时钟：串口外设 + 引脚端口；端口时钟由 gpio_core 使能 */
    if (p->apb == 2U) RCC_APB2PeriphClockCmd(p->clk, ENABLE);
    else              RCC_APB1PeriphClockCmd(p->clk, ENABLE);
    GPIO_ClockEnable(p->tx_port);
    GPIO_ClockEnable(p->rx_port);

    /* 引脚复用：TX 复用推挽(GPIO_Mode_AF+GPIO_OType_PP)，RX 复用输入+上拉(GPIO_PuPd_UP) */
    GPIO_PinAFConfig(p->tx_port, GPIO_PinSource(p->tx_pin), p->af);
    gi.GPIO_Pin   = p->tx_pin;
    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(p->tx_port, &gi);

    GPIO_PinAFConfig(p->rx_port, GPIO_PinSource(p->rx_pin), p->af);
    gi.GPIO_Pin   = p->rx_pin;
    GPIO_Init(p->rx_port, &gi);

    /* 串口参数：8 数据位 / 1 停止位 / 无校验 / 无流控
     * 标准库按当前 PCLK 计算波特率分频，切换主频后需重调本函数 */
    USART_StructInit(&ui);
    ui.USART_BaudRate = baudrate;
    ui.USART_Mode     = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(p->usart, &ui);
    USART_Cmd(p->usart, ENABLE);

    /* 清空接收缓冲 */
    rx_head[id] = 0;
    rx_tail[id] = 0;

    /* 复位 DMA 接收状态 */
    rx_dma_active[id] = 0;
    rx_dma_done[id]   = 0;
    rx_dma_len[id]    = 0;
}

/* 发送一个字节（阻塞等 TXE 置位） */
void SYS_USART_SendByte(SysUsartId_t id, uint8_t byte)
{
    const UsartCfg_t *p;
    uint32_t to;

    if (id >= SYS_USART_COUNT) return;
    p = &usart_cfg[id];

    /* 未初始化时直接返回，避免空等 */
    if ((p->usart->CR1 & USART_CR1_UE) == 0U) return;

    /* 带超时等待：标志不置位(时钟关/引脚占/硬件异常)时必须返回 */
    to = SYS_USART_TX_TIMEOUT;
    while (USART_GetFlagStatus(p->usart, USART_FLAG_TXE) == RESET) {
        if (to-- == 0U) return;                 /* 超时丢弃本字节 */
    }
    USART_SendData(p->usart, (uint16_t)byte);
}

/* 发送字符串 */
void SYS_USART_SendString(SysUsartId_t id, const char *str)
{
    if (str == 0) return;

    while (*str != '\0') {
        SYS_USART_SendByte(id, (uint8_t)(*str));
        str++;
    }
}

/* 发送字符串 + 回车换行 */
void SYS_USART_SendLine(SysUsartId_t id, const char *str)
{
    SYS_USART_SendString(id, str);
    SYS_USART_SendString(id, "\r\n");
}

/* 发送一段数据（阻塞） */
void SYS_USART_SendBuf(SysUsartId_t id, const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if ((buf == NULL) || (len == 0U)) return;

    for (i = 0U; i < len; i++) {
        SYS_USART_SendByte(id, buf[i]);
    }
}

/* 等待发送彻底完成（TC：最后一位移出移位寄存器） */
void SYS_USART_FlushTx(SysUsartId_t id)
{
    const UsartCfg_t *p;
    uint32_t to;

    if (id >= SYS_USART_COUNT) return;
    p = &usart_cfg[id];

    /* 未初始化时直接返回（同 SendByte） */
    if ((p->usart->CR1 & USART_CR1_UE) == 0U) return;

    /* 带超时等待 TC */
    to = SYS_USART_TX_TIMEOUT;
    while (USART_GetFlagStatus(p->usart, USART_FLAG_TC) == RESET) {
        if (to-- == 0U) return;
    }
}

/* 查询可读（轮询） */
uint8_t SYS_USART_DataReady(SysUsartId_t id)
{
    const UsartCfg_t *p;

    if (id >= SYS_USART_COUNT) return 0;
    p = &usart_cfg[id];

    if ((p->usart->CR1 & USART_CR1_UE) == 0U) return 0;

    return (USART_GetFlagStatus(p->usart, USART_FLAG_RXNE) != RESET) ? 1 : 0;
}

/* 轮询读一个字节（非阻塞） */
int SYS_USART_ReadByte(SysUsartId_t id)
{
    const UsartCfg_t *p;

    if (id >= SYS_USART_COUNT) return -1;
    p = &usart_cfg[id];

    if ((p->usart->CR1 & USART_CR1_UE) == 0U) return -1;

    if (USART_GetFlagStatus(p->usart, USART_FLAG_RXNE) != RESET) {
        return (int)(USART_ReceiveData(p->usart) & 0xFFU);
    }
    return -1;
}


/* ====== 扩展功能：中断接收 ====== */
/* 中断接收初始化：Init + RXNE 中断与 NVIC */
void SYS_USART_InitRxIT(SysUsartId_t id, uint32_t baudrate)
{
    const UsartCfg_t *p;
    NVIC_InitTypeDef ni;

    if (id >= SYS_USART_COUNT) return;
    p = &usart_cfg[id];

    SYS_USART_Init(id, baudrate);

    USART_ITConfig(p->usart, USART_IT_RXNE, ENABLE);

    ni.NVIC_IRQChannel                   = p->irq;
    ni.NVIC_IRQChannelPreemptionPriority = SYS_USART_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_USART_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);
}

/* 中断统一处理：收字节入环形缓冲并清溢出错误；缓冲写满丢弃新数据（不覆盖未读数据） */
static void usart_isr(SysUsartId_t id)
{
    const UsartCfg_t *p = &usart_cfg[id];

    if (USART_GetITStatus(p->usart, USART_IT_RXNE) != RESET) {
        uint8_t  data = (uint8_t)(USART_ReceiveData(p->usart) & 0xFFU);
        uint16_t next = (uint16_t)((rx_head[id] + 1U) % SYS_USART_RX_BUF_SIZE);

        if (next != rx_tail[id]) {              /* 缓冲未满 → 存入 */
            rx_buf[id][rx_head[id]] = data;
            rx_head[id] = next;
        }
    }

    /* 空闲检测(IDLE)：一帧结束，停 DMA 并定格长度（DMA 接收用） */
    if (USART_GetITStatus(p->usart, USART_IT_IDLE) != RESET) {
        uint16_t got;

        (void)p->usart->SR;     /* 读 SR + DR 清 IDLE 标志（顺序不能反） */
        (void)p->usart->DR;

        SYS_DMA_Stop(p->rx_stream);
        got = (uint16_t)(rx_dma_max[id] - SYS_DMA_Remain(p->rx_stream));
        rx_dma_len[id]    = got;
        rx_dma_done[id]   = 1;
        rx_dma_active[id] = 0;
        USART_ITConfig(p->usart, USART_IT_IDLE, DISABLE);
        USART_DMACmd(p->usart, USART_DMAReq_Rx, DISABLE);
    }

    /* ORE：依次读 SR、DR 清除，防止中断反复触发 */
    if (USART_GetFlagStatus(p->usart, USART_FLAG_ORE) != RESET) {
        (void)p->usart->SR;
        (void)p->usart->DR;
    }
}

/* 本组 ISR 为弱定义(__weak)：应用层同名 USARTx_IRQHandler 会顶替并停用该路库回调(见 sys_tim.c) */
__weak void USART1_IRQHandler(void) { usart_isr(SYS_USART_1); }
__weak void USART2_IRQHandler(void) { usart_isr(SYS_USART_2); }
__weak void USART3_IRQHandler(void) { usart_isr(SYS_USART_3); }
/* 第 4~6 路 ISR：缺少则编译通过，但该路中断接收静默失效 */
__weak void UART4_IRQHandler(void)  { usart_isr(SYS_UART_4);  }
__weak void UART5_IRQHandler(void)  { usart_isr(SYS_UART_5);  }
__weak void USART6_IRQHandler(void) { usart_isr(SYS_USART_6); }

/* 缓冲内未读字节数 */
uint16_t SYS_USART_Available(SysUsartId_t id)
{
    if (id >= SYS_USART_COUNT) return 0;

    if (rx_head[id] >= rx_tail[id]) {
        return (uint16_t)(rx_head[id] - rx_tail[id]);
    }
    return (uint16_t)(rx_head[id] + SYS_USART_RX_BUF_SIZE - rx_tail[id]);
}

/* 从缓冲读一个字节（非阻塞） */
int SYS_USART_RxRead(SysUsartId_t id)
{
    int c = -1;

    if (id >= SYS_USART_COUNT) return -1;

    if (rx_head[id] != rx_tail[id]) {
        c = (int)rx_buf[id][rx_tail[id]];
        rx_tail[id] = (uint16_t)((rx_tail[id] + 1U) % SYS_USART_RX_BUF_SIZE);
    }
    return c;
}

/* 清空缓冲 */
void SYS_USART_RxFlush(SysUsartId_t id)
{
    if (id >= SYS_USART_COUNT) return;

    rx_tail[id] = rx_head[id];
}


/* ====== 扩展功能：按结束符接收（不定长字符串） ====== */
/* 超时用 DWT 周期计数(与 delay_us 同套硬件)；每收一字节刷新字节间超时基准，
 * 发送方停顿超过 timeout_ms 判超时;timeout_ms=0 为非阻塞用法 */
int SYS_USART_ReadUntil(SysUsartId_t id, char *buf, uint16_t max, char end_ch, uint32_t timeout_ms)
{
    uint16_t n = 0U;
    uint32_t to_cycles;
    uint32_t t0;
    int c;

    if (id >= SYS_USART_COUNT || buf == 0 || max < 2U) return -2;

    delay_us(1);                                  /* 确保 DWT 计数已使能 */
    if (timeout_ms > 25000U) timeout_ms = 25000U; /* 32 位周期计数上限约 25.5s@168MHz */
    to_cycles = timeout_ms * (SystemCoreClock / 1000U);
    t0 = DWT->CYCCNT;

    for (;;) {
        c = SYS_USART_RxRead(id);
        if (c >= 0) {
            t0 = DWT->CYCCNT;                     /* 收到字节:刷新字节间超时 */
            if ((char)c == end_ch) {              /* 结束符 → 成帧(不存入) */
                buf[n] = '\0';
                return (int)n;
            }
            if (n >= (uint16_t)(max - 1U)) {      /* 放不下:报错(剩余字节留缓冲) */
                buf[n] = '\0';
                return -2;
            }
            buf[n] = (char)c;
            n++;
        } else if ((uint32_t)(DWT->CYCCNT - t0) >= to_cycles) {
            return -1;                            /* 超时:半帧作废 */
        }
    }
}

/* 取走缓冲中现有字节（非阻塞） */
uint16_t SYS_USART_ReadBuf(SysUsartId_t id, uint8_t *buf, uint16_t max)
{
    uint16_t n = 0U;
    int c;

    if (id >= SYS_USART_COUNT || buf == 0 || max == 0U) return 0U;

    while ((n < max) && ((c = SYS_USART_RxRead(id)) >= 0)) {
        buf[n] = (uint8_t)c;
        n++;
    }
    return n;
}


/* ====== 扩展功能：DMA 收发 ====== */
/* DMA 发送：查空闲 → 配流 → 启动；数据流映射见配置表 */
uint8_t SYS_USART_SendDMA(SysUsartId_t id, const uint8_t *buf, uint16_t len)
{
    const UsartCfg_t *p;

    if (id >= SYS_USART_COUNT || buf == 0 || len == 0U) return 1U;
    p = &usart_cfg[id];

    if (SYS_USART_TxDmaBusy(id) != 0U) return 1U;   /* 上一笔未完，拒绝 */

    USART_DMACmd(p->usart, USART_DMAReq_Tx, ENABLE);
    SYS_DMA_MemToPeriph(p->tx_stream, p->dma_ch,
                        (uint32_t)&p->usart->DR, buf, len, 1, 0);
    return 0U;
}

/* DMA 发送忙：DMA 搬完且移位寄存器发完(TC=1) */
uint8_t SYS_USART_TxDmaBusy(SysUsartId_t id)
{
    const UsartCfg_t *p;

    if (id >= SYS_USART_COUNT) return 0U;
    p = &usart_cfg[id];

    if (SYS_DMA_Busy(p->tx_stream) != 0U) return 1U;
    if (USART_GetFlagStatus(p->usart, USART_FLAG_TC) == RESET) return 1U;
    return 0U;
}

/* DMA 接收：空闲中断 + DMA，专收不定长帧 */
uint8_t SYS_USART_RecvDMA(SysUsartId_t id, uint8_t *buf, uint16_t maxlen)
{
    const UsartCfg_t *p;
    NVIC_InitTypeDef  ni;

    if (id >= SYS_USART_COUNT || buf == 0 || maxlen == 0U) return 1U;
    p = &usart_cfg[id];

    if (rx_dma_active[id] != 0U) return 1U;         /* 已在收，拒绝 */

    rx_dma_done[id]   = 0;
    rx_dma_len[id]    = 0;
    rx_dma_max[id]    = maxlen;
    rx_dma_active[id] = 1;

    /* 关 RXNE 中断（本模式数据走 DMA，不走环形缓冲） */
    USART_ITConfig(p->usart, USART_IT_RXNE, DISABLE);

    /* 清残留 IDLE 标志（读 SR + DR） */
    (void)p->usart->SR;
    (void)p->usart->DR;

    SYS_DMA_PeriphToMem(p->rx_stream, p->dma_ch,
                        (uint32_t)&p->usart->DR, buf, maxlen, 1, 0);
    USART_DMACmd(p->usart, USART_DMAReq_Rx, ENABLE);
    USART_ITConfig(p->usart, USART_IT_IDLE, ENABLE);

    /* 开 NVIC（优先级用模块配置宏，同 InitRxIT） */
    ni.NVIC_IRQChannel                   = p->irq;
    ni.NVIC_IRQChannelPreemptionPriority = SYS_USART_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_USART_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);

    return 0U;
}

/* 已收字节数：进行中 = 总数 - 剩余(NDTR)；收完 = 定格值 */
uint16_t SYS_USART_DmaRxLen(SysUsartId_t id)
{
    const UsartCfg_t *p;

    if (id >= SYS_USART_COUNT) return 0U;
    p = &usart_cfg[id];

    if (rx_dma_done[id] != 0U) return rx_dma_len[id];
    if (rx_dma_active[id] != 0U) {
        return (uint16_t)(rx_dma_max[id] - SYS_DMA_Remain(p->rx_stream));
    }
    return 0U;
}

/* 一帧是否收完：空闲中断已标记，或缓冲收满 */
uint8_t SYS_USART_DmaRxDone(SysUsartId_t id)
{
    const UsartCfg_t *p;

    if (id >= SYS_USART_COUNT) return 0U;
    p = &usart_cfg[id];

    if (rx_dma_done[id] != 0U) return 1U;

    /* 缓冲收满：DMA 已自然停，补一次收尾（同空闲路径） */
    if ((rx_dma_active[id] != 0U) && (SYS_DMA_Busy(p->rx_stream) == 0U)) {
        rx_dma_len[id]    = rx_dma_max[id];
        rx_dma_done[id]   = 1U;
        rx_dma_active[id] = 0U;
        USART_ITConfig(p->usart, USART_IT_IDLE, DISABLE);
        USART_DMACmd(p->usart, USART_DMAReq_Rx, DISABLE);
        return 1U;
    }
    return 0U;
}


/* ====== 扩展功能：格式化输出 ====== */
/* 格式化发送：先格式化到缓冲，再逐字节发出 */
int SYS_USART_Printf(SysUsartId_t id, const char *format, ...)
{
    char    buf[SYS_USART_PRINTF_BUF_SIZE];
    va_list ap;
    int     len;
    int     i;

    if (id >= SYS_USART_COUNT || format == 0) return 0;

    va_start(ap, format);
    len = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    if (len < 0) return 0;
    if ((size_t)len >= sizeof(buf)) len = (int)sizeof(buf) - 1;   /* 截断保护 */

    for (i = 0; i < len; i++) {
        SYS_USART_SendByte(id, (uint8_t)buf[i]);
    }
    return len;
}

/* 格式化到用户缓冲并发送 */
int SYS_USART_SendFormat(SysUsartId_t id, char *buf, uint16_t size, const char *format, ...)
{
    va_list ap;
    int     len;

    if (id >= SYS_USART_COUNT || buf == 0 || size == 0U || format == 0) return 0;

    va_start(ap, format);
    len = vsnprintf(buf, size, format, ap);
    va_end(ap);

    if (len < 0) { buf[0] = '\0'; return 0; }
    if ((size_t)len >= size) len = (int)size - 1;       /* 截断保护 */

    SYS_USART_SendBuf(id, (const uint8_t *)buf, (uint16_t)len);
    return len;
}

#if SYS_USART_FPUTC_ENABLE
/* printf 重定向：经 fputc 送到 SYS_USART_PRINTF_ID 串口；输出 %f 需 Keil 勾选 Use MicroLIB */
int fputc(int ch, FILE *f)
{
    const UsartCfg_t *p;
    uint32_t to;

    (void)f;

    if (SYS_USART_PRINTF_ID >= SYS_USART_COUNT) return ch;
    p = &usart_cfg[SYS_USART_PRINTF_ID];

    /* 未初始化(UE=0) 时直接丢弃字符，避免死等永不置位的 TXE */
    if ((p->usart->CR1 & USART_CR1_UE) == 0U) return ch;

    to = SYS_USART_TX_TIMEOUT;
    while (USART_GetFlagStatus(p->usart, USART_FLAG_TXE) == RESET) {
        if (to-- == 0U) return ch;              /* 超时放弃 */
    }
    USART_SendData(p->usart, (uint16_t)ch);
    /* 不等 TC：每字符等一次会使打印速度减半；需要时调用 SYS_USART_FlushTx() */
    return ch;
}
#endif
