#ifndef __FWLIB_SYS_RS485_H
#define __FWLIB_SYS_RS485_H

#include "stm32f4xx.h"
#include "sys_usart.h"

/* ================================================================
 *  sys_rs485.h —— 【系统】RS485 半双工收发切换模块  头文件
 * ================================================================
 *  设计定位 : RS485"方向脚(DE/RE)与串口配合"的薄封装——发送自动切
 *             "发送态"、发完(等 TC)自动切回"接收态",应用只管发数据
 *  标准库关键词 : 经 sys_usart → USART_GetFlagStatus(TC) 等;方向脚为普通 GPIO
 *
 *  本板 : USART2(PA2/PA3) 接板载 SP3485 收发器(跳线选通,见 sys_usart.h);
 *         方向控制脚的位置由实际接线决定——"区块 2 的 Init 参数"传入:
 *         (收发器上找 DE / RE / DIR 字样;单脚控制时 DE 与 RE 并联)
 *         若你的板子是"自动方向"电路(无方向脚),则只需 SYS_USART 收发,
 *         不必用本模块
 *
 *  使用方式 :
 *      SYS_RS485_Init(SYS_USART_2, GPIOD, GPIO_Pin_4, SYS_RS485_TX_LEVEL); // ① 方向脚
 *      SYS_USART_InitRxIT(SYS_USART_2, 9600);            // ② 串口先就绪(中断收)
 *      SYS_RS485_Send(SYS_USART_2, frame, len);          // ③ 发送(自动切向)
 *      // 接收直接用 SYS_USART_Available / SYS_USART_RxRead(不经过本模块)
 *  提醒 : 发送是阻塞式(直到最后一个字节完全移出才返回);帧与帧之间留
 *         3.5 字符以上间隙(Modbus 判帧规则)——做 Modbus 协议请直接用
 *         sys_modbus 模块,它内置了帧间隔处理
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 方向脚默认极性:1 = 方向脚高电平为"发送"。SP3485 的 DE 高有效、
 * RE 低有效,常见接法(DE 与 RE 并联)即"高 = 发送";
 * 仅作参考——实际以 SYS_RS485_Init 的参数为准 */
#define SYS_RS485_TX_LEVEL  1


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化方向控制: 记录方向脚 + 置为空闲(接收)态
 * 参数 : uart —— 串口编号(须与后续 Send 的编号一致);
 *        de_port/de_pin —— 方向脚(普通推挽输出 GPIO_OType_PP,自动开时钟);
 *        tx_level —— "发送态"的电平(1 或 0)——以收发器手册/实测为准
 *        (接错极性的现象:发送时总线不动、接收正常或整条总线被占死)
 * 标准库调用链 : 经 gpio_core → RCC_AHB1PeriphClockCmd + GPIO_Init
 * 示例 : SYS_RS485_Init(SYS_USART_2, GPIOD, GPIO_Pin_4, SYS_RS485_TX_LEVEL); */
void SYS_RS485_Init(SysUsartId_t uart, GPIO_TypeDef *de_port, uint16_t de_pin,
                    uint8_t tx_level);

/* 发送一段数据(阻塞): 切发送 → 逐字节发 → 等 TC → 切回接收
 * 前提 : 已 SYS_USART_Init(同编号串口);已 SYS_RS485_Init;
 *        未初始化时本函数直接返回(防裸切方向)
 * 标准库 : 数据发送经 sys_usart;TC 等待经 SYS_USART_FlushTx
 * 示例 : SYS_RS485_Send(SYS_USART_2, buf, len); */
void SYS_RS485_Send(SysUsartId_t uart, const uint8_t *buf, uint16_t len);

/* 发送字符串(阻塞,自动切向;内部算出长度后走 Send)
 * 示例 : SYS_RS485_SendString(SYS_USART_2, "hello"); */
void SYS_RS485_SendString(SysUsartId_t uart, const char *str);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 手动切方向(调试/自定义时序用;正常收发不需要)
 * 示例 : SYS_RS485_SetTx(SYS_USART_2, 1);   // 切"发送态"占住总线 */
void SYS_RS485_SetTx(SysUsartId_t uart, uint8_t on);

#endif /* __FWLIB_SYS_RS485_H */
