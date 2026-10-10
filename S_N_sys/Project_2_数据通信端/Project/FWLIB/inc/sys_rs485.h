#ifndef __FWLIB_SYS_RS485_H
#define __FWLIB_SYS_RS485_H

#include "stm32f4xx.h"
#include "sys_usart.h"

/* sys_rs485.h: RS485 半双工方向控制 头文件
 *
 * 功能: 在 sys_usart 之上负责方向切换，发送前切到发送态，发完切回接收态；
 *       串口收发与中断环形缓冲复用 sys_usart；SYS_RS485_InitEx 支持任意
 *       串口与方向脚，每路串口各配一套
 * 依赖: sys_usart.h（收发 / 中断缓冲 / 等 TC）、gpio_core.h
 * 依据: 切方向前必须等发送完成标志 TC（USART_GetFlagStatus），只等 TXE
 *       （发送寄存器空）会截断最后一个字节，本模块经 SYS_USART_FlushTx 完成
 *
 * 接线（普中-天马 F407 开发板原理图，收发器 SP3485 = U7）:
 *   RO 接 PA3 (USART2_RX)，DI 接 PA2 (USART2_TX)
 *   RE/DE 并联接 PG8（网络名 RS485_RE）: PG8 = 1 发送模式，PG8 = 0 接收模式
 *   RE 低有效为接收使能，DE 高有效为发送使能，PG8 = 1 时 RE 无效、DE 有效
 *   PG8 同时接 NRF24L01 的 NRF_IRQ，《普中 STM32F4xx 开发攻略》说明两者不能同时使用
 *   PA2/PA3 经跳线 P6 同时连到 SP3232（RS232，DB9）与 SP3485（RS485），
 *   一次只能用一种，由跳线 P6 的 485R/485T、PA3R/PA2T、232R/232T 决定
 * 约束: 发送为阻塞式，直到最后一个字节完全移出才返回；帧与帧之间需留 3.5 字符
 *       以上间隙（Modbus 判帧规则），做 Modbus 协议直接用 modbus 模块，其内置
 *       帧间隔处理；自动方向电路（无方向脚）只需 SYS_USART 收发，不必用本模块
 */


/* 区块 1: 定义与宏定义 */
/* 方向控制脚 */
#define SYS_RS485_DE_PORT   GPIOG
#define SYS_RS485_DE_PIN    GPIO_Pin_8
/* 1 = 输出高电平进入发送模式（本板 SP3485 如此）；0 = 反相 */
#define SYS_RS485_TX_LEVEL  1

/* 使用哪一路串口：本板 RS485 接 USART2（PA2/PA3） */
#define SYS_RS485_USART     SYS_USART_2

/* 默认波特率（MODBUS 常见：9600 / 19200 / 38400 / 115200） */
#define SYS_RS485_DEFAULT_BAUD  9600


/* 区块 2: 基础功能 */
/* 配置任意一路的方向控制：只登记配置并初始化方向脚，不改动串口
 * 参数: uart 串口编号（收发前自行 SYS_USART_Init / SYS_USART_InitRxIT）；
 *       de_port/de_pin 方向脚，推挽输出 GPIO_OType_PP 且内部使能端口时钟，
 *       收发器上标识 DE / RE / DIR；tx_level 发送态电平 1 或 0，按收发器手册定
 * 每路串口各存一套配置，方向脚初始置为接收态；未配置方向脚的路发送时不切方向；
 * 极性接反的现象是发送时总线不动，或整条总线被占死 */
void SYS_RS485_InitEx(SysUsartId_t uart, GPIO_TypeDef *de_port, uint16_t de_pin, uint8_t tx_level);

/* 板载那一路的初始化：方向脚用区块 1 宏，并开串口中断接收（环形缓冲）
 * 参数: baudrate 波特率，传 0 时用 SYS_RS485_DEFAULT_BAUD
 * 内部调用 SYS_RS485_InitEx 与 SYS_USART_InitRxIT */
void SYS_RS485_Init(uint32_t baudrate);

/* 发送一段数据到指定串口：阻塞，自动切发送、发完等 TC、再切回接收 */
void SYS_RS485_SendTo(SysUsartId_t uart, const uint8_t *buf, uint16_t len);

/* 发送字符串到指定串口：阻塞，自动切向 */
void SYS_RS485_SendStringTo(SysUsartId_t uart, const char *str);

/* 手动切换方向：调试或自定义时序用，on = 1 为发送态并占住总线
 * 正常收发由 Send 系列内部处理 */
void SYS_RS485_SetTxTo(SysUsartId_t uart, uint8_t on);

/* 以下为板载那一路（SYS_RS485_USART，区块 1 宏）的快捷接口 */

/* 只切换方向：正常收发由 Send 系列内部处理，自行拼帧时整帧前后各调一次可省去逐字节切换 */
void SYS_RS485_SetTx(void);      /* 切到发送 */
void SYS_RS485_SetRx(void);      /* 切回接收 */

/* 发送一段数据：阻塞，自动切发送、发完等 TC、再切回接收
 * 参数: buf 数据指针，len 字节长度 */
void SYS_RS485_SendBuf(const uint8_t *buf, uint16_t len);

/* 发一个字节或字符串：内部同样带方向切换与等 TC */
void SYS_RS485_SendByte(uint8_t byte);
void SYS_RS485_SendString(const char *str);


/* 区块 3: 扩展功能（接收，均针对板载那一路） */
/* 缓冲区里还有多少字节可读（非阻塞） */
uint16_t SYS_RS485_Available(void);

/* 从接收缓冲取一个字节；无数据返回 -1 */
int SYS_RS485_ReadByte(void);

/* 一次取走多个字节，适合 Modbus 整帧处理；返回实际取到的字节数 */
uint16_t SYS_RS485_ReadBuf(uint8_t *buf, uint16_t maxlen);

/* 清空接收缓冲（上电或帧出错后重同步用） */
void SYS_RS485_RxFlush(void);

/* 按帧头同步丢弃缓冲最前面的 len 个字节；len 传 0 等价于 RxFlush */
void SYS_RS485_RxSkip(uint16_t len);

/* 是否处于自己发送的状态：1 = 发送中，Modbus 判断回帧时机用 */
uint8_t SYS_RS485_IsSending(void);

#endif /* __FWLIB_SYS_RS485_H */
