#ifndef __FWLIB_SYS_RS485_H
#define __FWLIB_SYS_RS485_H

#include "stm32f4xx.h"
#include "sys_usart.h"

/* ================================================================
 *  sys_rs485.h —— RS485 半双工方向控制  头文件
 * ================================================================
 *  设计定位 : 在 sys_usart 之上再包一层"半双工方向控制"
 *             —— 底层串口收发、中断环形缓冲全部复用 sys_usart
 *             —— 本模块只负责一件事：**发送时把收发器切到发送、发完切回接收**
 *             —— 默认服务"板载那一路"（区块 1 宏）；也支持任意串口 / 任意
 *                方向脚（SYS_RS485_InitEx），多路可各配一套
 *  依赖     : sys_usart.h（收发 / 中断缓冲 / 等 TC）、gpio_core.h
 *  标准库关键词 : USART_GetFlagStatus(TC) —— 切方向前必须等"发送完成"
 *                 （本模块经 SYS_USART_FlushTx 完成这一步），
 *                 只等 TXE（发送寄存器空）会把最后一个字节截断
 *
 *  【天马 F407 开发板接线（普中-天马 F407开发板原理图）】
 *      收发器 : SP3485（U7）
 *      RO → PA3 (USART2_RX)   DI → PA2 (USART2_TX)
 *      RE/DE 并联 → PG8（网络名 RS485_RE）
 *      ⚠ PG8 = 1 → 发送模式；PG8 = 0 → 接收模式
 *        （RE 低有效=接收使能，DE 高有效=发送使能；PG8=1 时 RE 无效/DE 有效）
 *      ⚠⚠ PG8 在原理图上**同时**是 NRF24L01 的 NRF_IRQ——
 *        《普中 STM32F4xx 开发攻略》原话：“NRF24L01 模块的 NRF_IRQ 也
 *        使用了 PG8 管脚，所以它们不能同时使用”。
 *        用 RS485 时就别插 NRF24L01（反之亦然）。
 *      ⚠ 板上 PA2/PA3 经跳线 P6 同时连到 SP3232(RS232，DB9) 与 SP3485(RS485)：
 *        一次只能用一种（跳线 P6 的 485R/485T ↔ PA3R/PA2T ↔ 232R/232T 决定）。
 *
 *  使用方式 :
 *      // ① 板载一条龙（方向脚用区块 1 宏；串口一并开中断接收）
 *      SYS_RS485_Init(9600);
 *      SYS_RS485_SendString("hello modbus\r\n");   // 发一帧（自动切方向）
 *      if (SYS_RS485_Available()) {                // 收（非阻塞）
 *          int c = SYS_RS485_ReadByte();
 *      }
 *      // ② 任意串口 / 任意方向脚（多路场景；串口要自行初始化）
 *      SYS_USART_InitRxIT(SYS_USART_3, 9600);
 *      SYS_RS485_InitEx(SYS_USART_3, GPIOD, GPIO_Pin_4, 1);  // 方向脚 PD4，高=发送
 *      SYS_RS485_SendTo(SYS_USART_3, buf, len);              // 往那一路发
 *
 *  提醒 : 发送是阻塞式(直到最后一个字节完全移出才返回);帧与帧之间留
 *         3.5 字符以上间隙(Modbus 判帧规则)——做 Modbus 协议请直接用
 *         modbus 模块,它内置了帧间隔处理
 *  若你的板子是"自动方向"电路(无方向脚):只需 SYS_USART 收发,不必用本模块
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 方向控制脚（换板子改这三行） */
#define SYS_RS485_DE_PORT   GPIOG
#define SYS_RS485_DE_PIN    GPIO_Pin_8
/* 1 = 输出高电平进入发送模式（本板 SP3485 如此）；0 = 反相 */
#define SYS_RS485_TX_LEVEL  1

/* 使用哪一路串口：本板 RS485 接 USART2（PA2/PA3） */
#define SYS_RS485_USART     SYS_USART_2

/* 默认波特率（MODBUS 常见：9600 / 19200 / 38400 / 115200） */
#define SYS_RS485_DEFAULT_BAUD  9600


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 【通用】配置"任意一路"的方向控制：只记录 + 配置方向脚，不动串口
 * 参数 : uart  —— 串口编号（收发前请先自行 SYS_USART_Init / InitRxIT）;
 *        de_port/de_pin —— 方向脚（普通推挽输出 GPIO_OType_PP，自动开时钟；
 *              收发器上找 DE / RE / DIR 字样，单脚控制时 DE 与 RE 并联）;
 *        tx_level —— "发送态"的电平（1 或 0）——以收发器手册/实测为准
 *        （接错极性的现象：发送时总线不动、接收正常或整条总线被占死）
 * 说明 : 3 路各存一套配置，可对多路分别调用；方向脚初始置为接收态；
 *        未配置方向脚的路，*To 发送函数只发数据、不切方向（RS232 场景无害）
 * 示例 : SYS_RS485_InitEx(SYS_USART_3, GPIOD, GPIO_Pin_4, 1); */
void SYS_RS485_InitEx(SysUsartId_t uart, GPIO_TypeDef *de_port, uint16_t de_pin, uint8_t tx_level);

/* 【板载一条龙】初始化：方向脚用区块 1 宏 + 串口初始化 + 中断接收（环形缓冲）
 * 参数 : baudrate —— 波特率（传 0 = 用 SYS_RS485_DEFAULT_BAUD）
 * 说明 : 内部 = SYS_RS485_InitEx(宏配置) + SYS_USART_InitRxIT，收数据不会丢
 * 示例 : SYS_RS485_Init(9600); */
void SYS_RS485_Init(uint32_t baudrate);

/* 【通用】发送一段数据到指定串口（阻塞；自动"切发送 → 发完等 TC → 切回接收"）
 * 示例 : SYS_RS485_SendTo(SYS_USART_3, buf, len); */
void SYS_RS485_SendTo(SysUsartId_t uart, const uint8_t *buf, uint16_t len);

/* 【通用】发字符串到指定串口（阻塞，自动切向） */
void SYS_RS485_SendStringTo(SysUsartId_t uart, const char *str);

/* 【通用】手动切方向（调试 / 自定义时序用；正常收发不需要）
 * 示例 : SYS_RS485_SetTxTo(SYS_USART_3, 1);   // 切"发送态"占住总线 */
void SYS_RS485_SetTxTo(SysUsartId_t uart, uint8_t on);

/* ---- 以下为"板载那一路"（SYS_RS485_USART = 区块 1 宏）的快捷接口 ---- */

/* 只切换方向（一般不用手动调，Send* 系列内部已处理）
 * 用途 : 你自己拼帧时，整帧前后各调一次可以省掉逐字节的方向切换开销 */
void SYS_RS485_SetTx(void);      /* 切到发送 */
void SYS_RS485_SetRx(void);      /* 切回接收 */

/* 发送一段数据（阻塞；自动"切发送 → 发完等 TC → 切回接收"）
 * 参数 : buf/len —— 数据指针与长度（十六进制帧、Modbus 帧都用它）
 * 示例 : uint8_t f[] = {0x01,0x03,0x00,0x00,0x00,0x0A,0xC5,0xCD};
 *        SYS_RS485_SendBuf(f, sizeof(f)); */
void SYS_RS485_SendBuf(const uint8_t *buf, uint16_t len);

/* 发一个字节 / 发字符串（一次性小数据用；内部同样带方向切换与 TC 等待） */
void SYS_RS485_SendByte(uint8_t byte);
void SYS_RS485_SendString(const char *str);


/* ================================================================
 *                    区块 3：扩展功能（接收，均针对板载那一路）
 * ================================================================ */
/* 缓冲区里还有多少字节可读（非阻塞） */
uint16_t SYS_RS485_Available(void);

/* 从接收缓冲取一个字节；无数据返回 -1 */
int SYS_RS485_ReadByte(void);

/* 一次取走多个字节（适合 Modbus 整帧处理）
 * 返回 : 实际取到的字节数 */
uint16_t SYS_RS485_ReadBuf(uint8_t *buf, uint16_t maxlen);

/* 清空接收缓冲（上电或帧出错后重同步用） */
void SYS_RS485_RxFlush(void);

/* 丢弃缓冲里的 len 个字节（帧头同步用；len 传 0 等价于 RxFlush）
 * 示例 : 收到半帧时跳过一个字节重新找帧头 → SYS_RS485_RxSkip(1); */
void SYS_RS485_RxSkip(uint16_t len);

/* 查"总线是否正处于自己发送的状态"：1 = 发送中（Modbus 判断回帧时机用） */
uint8_t SYS_RS485_IsSending(void);

#endif /* __FWLIB_SYS_RS485_H */
