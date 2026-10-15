#ifndef __FWLIB_ESP8266_H
#define __FWLIB_ESP8266_H

#include "stm32f4xx.h"
#include "sys_usart.h"

/* esp8266.h：ESP8266 WiFi 模块 AT 指令驱动头文件
 * 功能 : 提供"发一条命令 + 等期望回复"的通用原语，连路由 / 建 TCP / 发数据包成一行调用
 * 依赖 : sys_usart.h（USART3 中断接收环形缓冲）
 *
 * 接线（普中-天马 F407 开发板，模块接口 P10）
 *      USART3：TX = PB10，RX = PB11
 *      模块与 MCU 交叉接：模块 TXD 接 PB11(RX)，模块 RXD 接 PB10(TX)
 *      供电：ESP8266 发射瞬间电流 200mA 以上，不能由 STM32 的 3.3V 直接供，
 *            本板 WiFi 接口有独立供电；共地必须接
 *      ESP8266 为 3.3V 电平，默认波特率常见 115200，旧固件 9600
 *
 * 移植 : 换串口改 ESP8266_USART；换波特率改 ESP8266_DEFAULT_BAUD */


/* 区块 1：定义与宏定义区（换板子只改这里） */
#define ESP8266_USART           SYS_USART_3      /* 本板 WiFi 接 USART3（PB10/PB11） */
#define ESP8266_DEFAULT_BAUD    115200UL

/* 工作模式 */
#define ESP8266_MODE_STA        1               /* 站点模式，连路由器 */
#define ESP8266_MODE_AP         2               /* 自己当热点 */
#define ESP8266_MODE_STA_AP     3               /* 两者兼有 */

/* 单条 AT 命令默认超时（ms）；连路由/建 TCP 需要更久，可用参数覆盖 */
#define ESP8266_CMD_TIMEOUT_MS  2000
#define ESP8266_JOIN_TIMEOUT_MS 15000

/* 接收缓冲，等期望字符串用；一条 AT 回显较短，512 字节足够 */
#define ESP8266_RX_BUF_SIZE     512


/* 区块 2：基础功能 */
/* 初始化：串口中断接收 + 重启模块 + 等就绪
 * 参数 : baudrate，波特率（0 = ESP8266_DEFAULT_BAUD）
 * 返回 : 0 = 成功（模块应答 OK）；1 = 模块无应答（查供电/接线/波特率）
 * 说明 : 内部等待约 2~3 秒（模块重启慢），阻塞 */
uint8_t ESP8266_Init(uint32_t baudrate);

/* 底层原语：发一条 AT 命令并等待"期望字符串"出现在回复里
 * 参数 : cmd，命令（不含结尾 \r\n，函数内部补）
 *        expect，期望字符串（如 "OK" / ">" / "CONNECT"），传 0 或 "" 只发不等
 *        timeout_ms，超时（0 = ESP8266_CMD_TIMEOUT_MS）
 * 返回 : 0 = 收到期望字符串；1 = 超时；2 = 参数非法
 * 说明 : 收到的内容可用 ESP8266_GetLastReply() 取回打印 */
uint8_t ESP8266_SendCmd(const char *cmd, const char *expect, uint32_t timeout_ms);

/* 取回上一次命令的完整回复（ASCII，以 '\0' 结尾），便于打印排错 */
const char *ESP8266_GetLastReply(void);

/* 取原始接收缓冲，二进制安全，供 MQTT 这类二进制协议使用
 * 参数 : buf，输出指针（模块内部缓冲，不拷贝）；返回缓冲内有效字节数
 * 说明 : 与 ESP8266_GetLastReply() 为同一块缓冲，只是不作 C 字符串看待：
 *        MQTT 报文含 0x00（主题长度高字节），用字符串接口会被截断；
 *        指针在下一条 AT 命令后失效，须立即解析，不可保存；
 *        缓冲上限 ESP8266_RX_BUF_SIZE，超长时前半段被挤掉 */
uint16_t ESP8266_GetRawRx(const uint8_t **buf);

/* 清空接收缓冲（重同步用） */
void ESP8266_Flush(void);


/* 区块 3：常用动作（一条调用完成一件事） */
/* 测 AT：返回 0 = 正常 */
uint8_t ESP8266_TestAT(void);

/* 软重启模块（等 "ready"） */
uint8_t ESP8266_Reset(void);

/* 设置工作模式：ESP8266_MODE_STA / AP / STA_AP */
uint8_t ESP8266_SetMode(uint8_t mode);

/* 连接路由器（阻塞最长 ESP8266_JOIN_TIMEOUT_MS）
 * 返回 : 0 = 成功（回 WIFI GOT IP 或 OK）；1 = 失败（密码错或信号差） */
uint8_t ESP8266_JoinAP(const char *ssid, const char *password);

/* 断开路由器连接 */
uint8_t ESP8266_QuitAP(void);

/* 读回本机 IP（从 AT+CIFSR 回复里解析 "STAIP" 后的地址）
 * 参数 : ip，输出缓冲（至少 16 字节）；len，缓冲长度
 * 返回 : 0 = 成功；1 = 失败（没连上 / 解析不到） */
uint8_t ESP8266_GetIP(char *ip, uint16_t len);

/* 建立 TCP 连接（单连接模式下的 CIPSEND）
 * 返回 : 0 = 成功；1 = 失败 */
uint8_t ESP8266_ConnectTCP(const char *host, uint16_t port);

/* 关闭 TCP 连接 */
uint8_t ESP8266_CloseTCP(void);

/* 发送一段数据（长度已知，二进制安全）
 * 返回 : 0 = 成功；1 = 发送失败（没连上 / 没等到 '>' 提示符） */
uint8_t ESP8266_SendData(const uint8_t *data, uint16_t len);

/* 发送字符串（自动算长度） */
uint8_t ESP8266_SendString(const char *str);

/* 查是否有新数据从模块输出（对方发来的 TCP 数据 / 异步提示如 WIFI DISCONNECT）
 * 返回 : 1 = 有新内容可取（用 ESP8266_GetLastReply 读取） */
uint8_t ESP8266_HasAsyncData(void);


/* 区块 4：TCP 服务器（上位机主动连板子；板子 IP 固定，上位机 IP 会变）
 * 约束 : 客户端用单连接（CIPMUX=0，ESP8266_ConnectTCP），服务器用多连接
 *        （CIPMUX=1，ESP8266_StartServer）；两者互斥，切换前先
 *        CloseTCP / StopServer，否则切换失败 */

/* 开 TCP 服务器：监听指定端口，等上位机连过来
 * 参数 : port，监听端口（1~65535，如 8080 / 8266）
 * 返回 : 0 = 成功 / 1 = 模块没应答 / 2 = 参数非法
 * 说明 : 内部先发 AT+CIPMUX=1（服务器必须开多连接）；须先 JoinAP 连上路由器，
 *        再用 GetIP 查板子 IP 告知上位机；与 ESP8266_ConnectTCP 互斥 */
uint8_t ESP8266_StartServer(uint16_t port);

/* 关 TCP 服务器
 * 返回 : 0 = 成功 / 1 = 模块没应答 */
uint8_t ESP8266_StopServer(void);

/* 查有没有客户端连进来，需在主循环轮询
 * 参数 : link_id，出参，连接号（回发数据时要带上）；不想要传 0
 * 返回 : 1 = 有上位机在线；0 = 没有
 * 说明 : 模块在连接/断开时主动上报 "<连接号>,CONNECT" / "<连接号>,CLOSED"；
 *        本函数只扫当前接收缓冲，缓冲会被下一条 AT 命令清掉，
 *        故需经常调用（如每 10~50ms 一次），否则会漏事件 */
uint8_t ESP8266_ServerHasClient(uint8_t *link_id);

/* 往指定连接发数据，服务器模式下用（客户端模式用 ESP8266_SendData）
 * 参数 : link_id，连接号（从 ServerHasClient 拿到）
 *        data / len，数据（长度已知，二进制安全）
 * 返回 : 0 = 成功；1 = 发送失败（连接已断 / 没等到 '>'）
 * 说明 : 内部发 AT+CIPSEND=<连接号>,<长度> */
uint8_t ESP8266_SendDataTo(uint8_t link_id, const uint8_t *data, uint16_t len);

/* 往指定连接发字符串，自动算长度 */
uint8_t ESP8266_SendStringTo(uint8_t link_id, const char *str);

#endif /* __FWLIB_ESP8266_H */
