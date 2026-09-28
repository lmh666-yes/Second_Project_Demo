#include "sys_rs485.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */

#include "gpio_core.h"

/* ================================================================
 *  sys_rs485.c —— 【系统】RS485 半双工收发切换模块  实现文件
 * ================================================================
 *  实现要点 :
 *    ① 方向脚只在"发送窗口"内改变:发前切发送态 → 整段发完(等 TC)
 *       → 切回接收态——TC 是关键:TXE 只表示"数据寄存器空了",
 *       最后一个字节可能还在移位寄存器里,过早切向会把尾字节丢在
 *       发送器嘴里(现象:收到的帧少最后一个字节);
 *    ② 按串口编号存一套方向配置(3 路各自独立);
 *    ③ 未 Init 时 Send 直接返回——防止"裸切方向"占住总线
 * ================================================================ */


/* ================================================================
 *                    内部状态
 * ================================================================ */
static GPIO_TypeDef *rs485_de_port[SYS_USART_COUNT];
static uint16_t      rs485_de_pin[SYS_USART_COUNT];
static uint8_t       rs485_tx_level[SYS_USART_COUNT];


/* ================================================================
 *                    内部辅助
 * ================================================================ */
/* 切方向: tx = 1 → 发送态;tx = 0 → 接收态 */
static void rs485_dir(SysUsartId_t uart, uint8_t tx)
{
    if (rs485_de_port[uart] == 0) return;

    if (tx) {
        GPIO_OutWrite(rs485_de_port[uart], rs485_de_pin[uart], rs485_tx_level[uart]);
    } else {
        GPIO_OutWrite(rs485_de_port[uart], rs485_de_pin[uart],
                      (uint8_t)(1U - rs485_tx_level[uart]));
    }
}


/* ================================================================
 *                    基础功能
 * ================================================================ */
void SYS_RS485_Init(SysUsartId_t uart, GPIO_TypeDef *de_port, uint16_t de_pin,
                    uint8_t tx_level)
{
    if (uart >= SYS_USART_COUNT || de_port == 0) return;

    rs485_de_port[uart]  = de_port;
    rs485_de_pin[uart]   = de_pin;
    rs485_tx_level[uart] = (tx_level != 0U) ? 1U : 0U;

    /* 方向脚 = 普通推挽输出(GPIO_OType_PP);上电先置"接收态"(空闲不能让总线被占) */
    GPIO_OutInit(de_port, de_pin);
    rs485_dir(uart, 0U);
}

void SYS_RS485_Send(SysUsartId_t uart, const uint8_t *buf, uint16_t len)
{
    if (uart >= SYS_USART_COUNT || buf == 0 || len == 0U) return;
    if (rs485_de_port[uart] == 0) return;      /* 未初始化:先 SYS_RS485_Init */

    rs485_dir(uart, 1U);                       /* ① 切发送态占住总线 */
    SYS_USART_SendBuf(uart, buf, len);         /* ② 逐字节发出(阻塞) */
    SYS_USART_FlushTx(uart);                   /* ③ 等 TC:最后一位完全移出 */
    rs485_dir(uart, 0U);                       /* ④ 切回接收态 */
}

void SYS_RS485_SendString(SysUsartId_t uart, const char *str)
{
    uint16_t n = 0U;

    if (str == 0) return;

    while (str[n] != '\0') n++;
    SYS_RS485_Send(uart, (const uint8_t *)str, n);
}


/* ================================================================
 *                    扩展功能
 * ================================================================ */
void SYS_RS485_SetTx(SysUsartId_t uart, uint8_t on)
{
    if (uart >= SYS_USART_COUNT) return;
    rs485_dir(uart, (on != 0U) ? 1U : 0U);
}
