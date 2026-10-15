#include "sys_rs485.h"
#include "gpio_core.h"

/* sys_rs485.c: RS485 半双工方向控制实现文件
 * 切方向必须等 TC，不能只等 TXE：
 *     TXE（发送数据寄存器空）：数据刚被搬进移位寄存器，总线还在发
 *     TC （发送完成）        ：移位寄存器也发完，总线真正空闲
 *     只等 TXE 就切回接收，最后 1~2 个字节还在线上，接收器会把自己的尾巴吃掉或发不全
 *     流程：SYS_USART_SendBuf() 发完 → SYS_USART_FlushTx() 等 TC → 才能切回接收
 *
 * 方向脚只在发送窗口内改变；按串口编号各存一套方向配置（SYS_RS485_InitEx 配置）
 * 未配置方向脚的路：SendTo 只发数据不切向，手动切向调用静默忽略，避免裸切方向占住总线 */


/* 内部状态 */
/* 每路一套方向配置（下标 = SYS_USART_x；全 0 = 该路未配置） */
static GPIO_TypeDef *rs485_de_port[SYS_USART_COUNT];
static uint16_t      rs485_de_pin[SYS_USART_COUNT];
static uint8_t       rs485_tx_level[SYS_USART_COUNT];

/* 发送进行中标志（给上层判断回帧时机，例如 Modbus 要等 TC 后才能收）
 * 发送函数阻塞，同一时刻只有一路在发，单标志够用 */
static volatile uint8_t rs485_txing = 0;


/* 内部辅助 */
/* 切方向：tx = 1 → 发送态，tx = 0 → 接收态
 * 未配置方向脚的路直接忽略 */
static void rs485_dir(SysUsartId_t uart, uint8_t tx)
{
    uint8_t lv;

    if (uart >= SYS_USART_COUNT || rs485_de_port[uart] == 0) return;

    lv = rs485_tx_level[uart];
    if (tx == 0U) lv = (uint8_t)(1U - lv);

    GPIO_OutWrite(rs485_de_port[uart], rs485_de_pin[uart], lv);
}


/* 基础功能 */
void SYS_RS485_InitEx(SysUsartId_t uart, GPIO_TypeDef *de_port, uint16_t de_pin, uint8_t tx_level)
{
    if (uart >= SYS_USART_COUNT || de_port == 0) return;

    rs485_de_port[uart]  = de_port;
    rs485_de_pin[uart]   = de_pin;
    rs485_tx_level[uart] = (tx_level != 0U) ? 1U : 0U;

    /* 方向脚 = 普通推挽输出（GPIO_OType_PP，内部自动开时钟）；先置接收态，空闲不占总线 */
    GPIO_OutInit(de_port, de_pin);
    rs485_dir(uart, 0U);
}

void SYS_RS485_Init(uint32_t baudrate)
{
    if (baudrate == 0U) baudrate = SYS_RS485_DEFAULT_BAUD;

    /* 1) 方向脚：板载宏配置，默认接收 */
    SYS_RS485_InitEx(SYS_RS485_USART, SYS_RS485_DE_PORT, SYS_RS485_DE_PIN, SYS_RS485_TX_LEVEL);

    /* 2) 串口：中断接收版（数据进 64 字节环形缓冲） */
    SYS_USART_InitRxIT(SYS_RS485_USART, baudrate);

    /* 3) 清开机噪声 */
    SYS_USART_RxFlush(SYS_RS485_USART);

    rs485_txing = 0;
}

void SYS_RS485_SendTo(SysUsartId_t uart, const uint8_t *buf, uint16_t len)
{
    if (uart >= SYS_USART_COUNT || buf == 0 || len == 0U) return;

    rs485_txing = 1U;

    rs485_dir(uart, 1U);                     /* 1) 切发送态占住总线 */
    SYS_USART_SendBuf(uart, buf, len);       /* 2) 逐字节发出（阻塞，等最后一个字节进移位寄存器） */
    SYS_USART_FlushTx(uart);                 /* 3) 等 TC：整帧真正发完 */
    rs485_dir(uart, 0U);                     /* 4) 切回接收态 */

    rs485_txing = 0;
}

void SYS_RS485_SendStringTo(SysUsartId_t uart, const char *str)
{
    uint16_t n = 0;

    if (str == 0) return;

    while (str[n] != '\0') n++;              /* 先算长度：一次性切方向比逐字节省 */
    if (n == 0U) return;

    SYS_RS485_SendTo(uart, (const uint8_t *)str, n);
}

void SYS_RS485_SetTxTo(SysUsartId_t uart, uint8_t on)
{
    rs485_dir(uart, (on != 0U) ? 1U : 0U);
}

/* 板载那一路的快捷接口（内部全部转调通用版） */
void SYS_RS485_SetTx(void)
{
    rs485_dir(SYS_RS485_USART, 1U);
}

void SYS_RS485_SetRx(void)
{
    rs485_dir(SYS_RS485_USART, 0U);
}

void SYS_RS485_SendBuf(const uint8_t *buf, uint16_t len)
{
    SYS_RS485_SendTo(SYS_RS485_USART, buf, len);
}

void SYS_RS485_SendByte(uint8_t byte)
{
    SYS_RS485_SendBuf(&byte, 1U);
}

void SYS_RS485_SendString(const char *str)
{
    SYS_RS485_SendStringTo(SYS_RS485_USART, str);
}


/* 扩展功能（接收，板载那一路） */
uint16_t SYS_RS485_Available(void)
{
    return SYS_USART_Available(SYS_RS485_USART);
}

int SYS_RS485_ReadByte(void)
{
    if (SYS_USART_Available(SYS_RS485_USART) == 0U) return -1;
    return SYS_USART_RxRead(SYS_RS485_USART);
}

uint16_t SYS_RS485_ReadBuf(uint8_t *buf, uint16_t maxlen)
{
    uint16_t n = 0;

    if (buf == 0) return 0U;

    while (n < maxlen) {
        if (SYS_USART_Available(SYS_RS485_USART) == 0U) break;
        {
            int c = SYS_USART_RxRead(SYS_RS485_USART);
            if (c < 0) break;
            buf[n++] = (uint8_t)c;
        }
    }
    return n;
}

void SYS_RS485_RxFlush(void)
{
    SYS_USART_RxFlush(SYS_RS485_USART);
}

void SYS_RS485_RxSkip(uint16_t len)
{
    if (len == 0U) { SYS_RS485_RxFlush(); return; }

    while (len-- != 0U) {
        if (SYS_USART_Available(SYS_RS485_USART) == 0U) break;
        (void)SYS_USART_RxRead(SYS_RS485_USART);
    }
}

uint8_t SYS_RS485_IsSending(void)
{
    return rs485_txing;
}
