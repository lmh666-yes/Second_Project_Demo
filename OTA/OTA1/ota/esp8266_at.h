#ifndef ESP8266_AT_H
#define ESP8266_AT_H

/* ============================================================
 *  esp8266_at —— ESP8266-01S（出厂 AT 固件）上链 + 透传驱动
 *
 *  用哪路串口：不自己挑——工程 sys_usart.h 已经写好"USART3 = PB10/PB11 接 ESP8266（跳线选通）"，
 *              USART1 是 CH340 调试、USART2 是 RS485，所以这里固定用 SYS_USART_3。
 *
 *  数据通路：AT+CIPMODE=1 + AT+CIPSEND 之后模块进入**透传**，串口上就是一条裸字节管道：
 *            写进去的字节发到 TCP 对端，对端发来的字节原样吐到串口。
 *            于是 OTA 的 AA55…CRC16…55AA 帧可以原封不动地在上面跑（sys_frame 一行不改）。
 *
 *  硬件注意（写进 OTA/说明.md 了，这里再提一遍）：
 *    · 3.3V 供电，峰值电流 ≥300mA：电源脚并 100µF 电解 + 100nF；
 *    · 模块 TXD → 单片机 PB11(RX)，模块 RXD → 单片机 PB10(TX)，GND 必须共地；
 *    · CH_PD(EN) 经 10k 上拉到 3.3V，否则模块不启动；
 *    · 老出厂固件波特率可能是 9600：先用 AT 命令确认，改不了就用 SYS_USART 初始化成对应波特率。
 * ============================================================ */

#include <stdint.h>

/* 初始化 USART3（中断接收方式）+ 复位内部状态。上电先调它。 */
void ESP8266_Init(void);

/* 走完整条上链流程（内部逐步打印到 USART1 调试串口）：
     ATE0（关回显，让响应干净）→ AT+CWMODE=1（Station）→ AT+CWJAP="ssid","pass"
     → AT+CIPMUX=0（单连接）→ AT+CIPSTART="TCP","ip",port → AT+CIPMODE=1 → AT+CIPSEND
   返回：1 = 已进入透传（此后可用 ESP8266_Read / ESP8266_Send 收发裸字节）
         0 = 中途失败（把调试串口的日志贴出来看是哪一步）。 */
uint8_t ESP8266_ConnectChain(const char *ssid, const char *pass,
                             const char *ip, uint16_t port, uint32_t timeout_ms);

/* 是否已在透传态（透传态下才允许收发裸字节）。 */
uint8_t ESP8266_Transparent(void);

/* 取走已收到的裸字节（非阻塞）。返回实际取到的字节数，0 = 暂时没有。 */
int ESP8266_Read(uint8_t *buf, uint16_t max);

/* 透传下发一段裸字节（内容会原样发到 TCP 对端）。 */
void ESP8266_Send(const uint8_t *buf, uint16_t len);
void ESP8266_SendString(const char *s);

/* 清空接收缓冲（重新上链前先清掉上一次连接的残留字节）。 */
void ESP8266_RxFlush(void);

#endif /* ESP8266_AT_H */
