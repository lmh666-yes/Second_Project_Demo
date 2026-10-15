#ifndef __FWLIB_SYS_RS485_H
#define __FWLIB_SYS_RS485_H

#include "stm32f4xx.h"
#include "sys_usart.h"

/* sys_rs485.h — RS485 半双工收发方向切换模块。
 * 本板: USART2(PA2/PA3) 接板载 SP3485 收发器(跳线选通,见 sys_usart.h);方向脚位置由实际接线决定,
 *       SYS_RS485_Init 传入(收发器上的 DE/RE/DIR,单脚控制时 DE 与 RE 并联)。
 * 发送阻塞至 TC(USART_GetFlagStatus 的 USART_FLAG_TC)置位才返回;帧间隔 3.5 字符以上(Modbus 判帧规则),Modbus 协议请用 sys_modbus。 */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* 方向脚默认极性:1 = 高电平为发送态。SP3485 的 DE 高有效、RE 低有效,
 * DE 与 RE 并联时即高为发送;实际以 SYS_RS485_Init 的 tx_level 为准 */
#define SYS_RS485_TX_LEVEL  1


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化方向控制: 记录方向脚并置为接收(空闲)态。
 * uart — 串口编号,须与后续 Send 的编号一致;
 * de_port/de_pin — 方向脚,普通推挽输出(GPIO_OType_PP),自动开时钟;
 * tx_level — 发送态电平(1 或 0),以收发器手册或实测为准。
 * 依据: 经 gpio_core → RCC_AHB1PeriphClockCmd + GPIO_Init */
void SYS_RS485_Init(SysUsartId_t uart, GPIO_TypeDef *de_port, uint16_t de_pin,
                    uint8_t tx_level);

/* 发送一段数据(阻塞): 切发送 → 逐字节发送 → 等 TC → 切回接收。
 * 前提: 同编号串口已 SYS_USART_Init,且已 SYS_RS485_Init;未初始化时直接返回。
 * 依据: 数据发送经 sys_usart;TC 等待经 SYS_USART_FlushTx。
 * 接收不经本模块,直接用 SYS_USART_Available / SYS_USART_RxRead 读取。 */
void SYS_RS485_Send(SysUsartId_t uart, const uint8_t *buf, uint16_t len);

/* 发送字符串(阻塞,自动切向);内部取长度后调用 Send。 */
void SYS_RS485_SendString(SysUsartId_t uart, const char *str);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 手动切方向(调试或自定义时序用,正常收发不需要):
 * on 非 0 切发送态占住总线,0 切回接收态。 */
void SYS_RS485_SetTx(SysUsartId_t uart, uint8_t on);

#endif /* __FWLIB_SYS_RS485_H */
