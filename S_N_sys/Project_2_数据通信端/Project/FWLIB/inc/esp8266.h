#ifndef __FWLIB_ESP8266_H
#define __FWLIB_ESP8266_H

#include "stm32f4xx.h"
#include "sys_usart.h"

/* ================================================================
 *  esp8266.h —— 【外接】ESP8266 WiFi 模块 AT 指令驱动  头文件
 * ================================================================
 *  设计定位 : 用 AT 指令把 WiFi 模块"跑通到能收发 TCP 数据"的薄封装
 *             —— 提供"发一条命令 + 等期望回复"的通用原语，
 *                再把常用动作（连路由 / 建 TCP / 发数据）包成一行调用
 *  依赖     : sys_usart.h（USART3 中断接收环形缓冲）
 *  标准库关键词 : 无——纯串口字符串交互
 *
 *  【接线（普中-天马 F407开发板）】
 *      模块接口 P10：USART3 —— TX=PB10  RX=PB11
 *      ⚠ 模块与 MCU 要**交叉接**：模块 TXD → PB11(RX)，模块 RXD → PB10(TX)
 *      ⚠ 供电：ESP8266 发射瞬间要 200mA+，**别用 STM32 的 3.3V 直接供**，
 *        本板 WiFi 接口有独立供电；共地一定要接
 *      ⚠ ESP8266 是 3.3V 电平，且默认波特率常见 115200（旧固件 9600）
 *
 *  【使用方式（连路由 → 建 TCP → 发一条数据）】
 *      char ip[16];
 *      if (ESP8266_Init(115200) == 0) {                    // ① 等模块就绪
 *          ESP8266_SetMode(ESP8266_MODE_STA);              // ② 站模式
 *          ESP8266_JoinAP("my_wifi", "12345678");          // ③ 连路由
 *          ESP8266_GetIP(ip, sizeof(ip));                  // ④ 看拿到什么 IP
 *          ESP8266_ConnectTCP("192.168.1.100", 8080);      // ⑤ 连上位机
 *          ESP8266_SendData("hello", 5);                   // ⑥ 发数据
 *      }
 *
 *  【排错顺序（AT 指令调不通时按这个顺序查）】
 *      ① 供电 & 共地 → ② 波特率 → ③ 交叉接线(TX/RX) → ④ 模块是否在启动打印乱码
 *      → ⑤ 用 ESP8266_SendCmd("AT", "OK", 1000) 看返回什么
 *
 *  移植指引 : 换串口改 ESP8266_USART；换波特率改 ESP8266_DEFAULT_BAUD。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
#define ESP8266_USART           SYS_USART_3      /* 本板 WiFi 接 USART3（PB10/PB11） */
#define ESP8266_DEFAULT_BAUD    115200UL

/* 工作模式 */
#define ESP8266_MODE_STA        1               /* 站点（连路由，最常用） */
#define ESP8266_MODE_AP         2               /* 自己当热点 */
#define ESP8266_MODE_STA_AP     3               /* 两者兼有 */

/* 单条 AT 命令的默认超时（ms）——连路由/建 TCP 要的久，可用参数覆盖 */
#define ESP8266_CMD_TIMEOUT_MS  2000
#define ESP8266_JOIN_TIMEOUT_MS 15000

/* 接收缓冲（用于"等期望字符串"）：ESP8266 一条 AT 回显不长，512 足够 */
#define ESP8266_RX_BUF_SIZE     512


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：串口（中断接收）+ 重启模块 + 等就绪
 * 参数 : baudrate —— 波特率（0 = ESP8266_DEFAULT_BAUD）
 * 返回 : 0 = 成功（模块应答 OK）；1 = 模块无应答（查供电/接线/波特率）
 * 说明 : 内部含约 2~3 秒等待（模块重启慢），阻塞
 * 示例 : if (ESP8266_Init(115200) != 0) { printf("ESP8266 没应答\r\n"); } */
uint8_t ESP8266_Init(uint32_t baudrate);

/* 底层原语：发一条 AT 命令并等待"期望字符串"出现在回复里
 * 参数 : cmd       —— 命令（不含结尾的 \r\n，函数内部会自动补）
 *        expect    —— 期望出现在回复中的字符串（如 "OK" / ">" / "CONNECT"）
 *                     传 0 或 "" 表示"只发不等"
 *        timeout_ms—— 超时（0 = ESP8266_CMD_TIMEOUT_MS）
 * 返回 : 0 = 收到期望字符串；1 = 超时；2 = 参数非法
 * 说明 : 收到的内容可通过 ESP8266_GetLastReply() 取回查看（调试神器）
 * 示例 : ESP8266_SendCmd("AT+CWMODE=1", "OK", 1000); */
uint8_t ESP8266_SendCmd(const char *cmd, const char *expect, uint32_t timeout_ms);

/* 取回"上一次命令"的完整回复（ASCII，以 '\0' 结尾），便于打印排错
 * 示例 : ESP8266_SendCmd("AT", "OK", 500);
 *        printf("[ESP] %s\r\n", ESP8266_GetLastReply()); */
const char *ESP8266_GetLastReply(void);

/* 取原始接收缓冲（**二进制安全**）——给 MQTT 这类二进制协议用
 * 参数 : buf —— 输出指针（指向模块内部缓冲，**不拷贝**）
 * 返回 : 缓冲内有效字节数
 * 说明 : 和 ESP8266_GetLastReply() 是同一块缓冲，只是不当 C 字符串看待——
 *        MQTT 报文里必然出现 0x00（主题长度高字节），用字符串接口会被截断
 * ⚠ 返回的指针在下一条 AT 命令后就失效，请**立即解析、不要保存**
 * ⚠ 缓冲上限 ESP8266_RX_BUF_SIZE；超长会把前半段挤掉（脏数据保护）
 * 示例 : const uint8_t *p; uint16_t n = ESP8266_GetRawRx(&p); */
uint16_t ESP8266_GetRawRx(const uint8_t **buf);

/* 清空接收缓冲（重同步用） */
void ESP8266_Flush(void);


/* ================================================================
 *                    区块 3：常用动作（一条调用完成一件事）
 * ================================================================ */
/* 测 AT：返回 0 = 正常 */
uint8_t ESP8266_TestAT(void);

/* 软重启模块（等 "ready"） */
uint8_t ESP8266_Reset(void);

/* 设置工作模式：ESP8266_MODE_STA / AP / STA_AP */
uint8_t ESP8266_SetMode(uint8_t mode);

/* 连接路由器（阻塞最长 ESP8266_JOIN_TIMEOUT_MS）
 * 返回 : 0 = 成功（回 WIFI GOT IP）/ OK；1 = 失败（密码错或信号差）
 * 示例 : ESP8266_JoinAP("iPhone", "12345678"); */
uint8_t ESP8266_JoinAP(const char *ssid, const char *password);

/* 断开路由器连接 */
uint8_t ESP8266_QuitAP(void);

/* 读回本机 IP（从 AT+CIFSR 的回复里解析 "STAIP" 后面的地址）
 * 参数 : ip —— 输出缓冲（至少 16 字节）；len —— 缓冲长度
 * 返回 : 0 = 成功；1 = 失败（没连上 / 解析不到）
 * 示例 : char ip[16]; if (ESP8266_GetIP(ip, sizeof(ip)) == 0) printf("IP=%s\r\n", ip); */
uint8_t ESP8266_GetIP(char *ip, uint16_t len);

/* 建立 TCP 连接（单连接模式下的 CIPSEND）
 * 返回 : 0 = 成功；1 = 失败
 * 示例 : ESP8266_ConnectTCP("192.168.1.100", 8080); */
uint8_t ESP8266_ConnectTCP(const char *host, uint16_t port);

/* 关闭 TCP 连接 */
uint8_t ESP8266_CloseTCP(void);

/* 发送一段数据（长度已知，二进制安全）
 * 返回 : 0 = 成功；1 = 发送失败（没连上 / 没等到 '>' 提示符）
 * 示例 : ESP8266_SendData("hello", 5); */
uint8_t ESP8266_SendData(const uint8_t *data, uint16_t len);

/* 发送字符串（自动算长度） */
uint8_t ESP8266_SendString(const char *str);

/* 查"近段时间有没有新数据从模块吐出来"（对方发来的 TCP 数据 /
 * 模块的异步提示如 WIFI DISCONNECT）
 * 返回 : 1 = 有新内容可取（用 ESP8266_GetLastReply 看） */
uint8_t ESP8266_HasAsyncData(void);


/* ================================================================
 *                区块 4：TCP 服务器（让上位机主动连过来）
 * ================================================================
 *  【什么时候用服务器模式】
 *      ESP8266 既能当"客户端"（主动去连别人），也能当"服务器"（等别人来连）。
 *       · 板子主动上报云平台     → 客户端： ESP8266_ConnectTCP(host, port)
 *       · Qt / 手机遥控小车 ★ → **服务器**：ESP8266_StartServer(port)
 *         （因为电脑/手机的 IP 每次都在变，板子无法主动找到它；
 *           反过来板子 IP 固定，上位机去连就行）
 *
 *  【两个模式互斥】
 *      客户端用单连接(CIPMUX=0)、服务器用多连接(CIPMUX=1)。
 *      先 ConnectTCP 再 StartServer（或反过来）会失败——
 *      要切换请先 CloseTCP / StopServer。
 * ================================================================ */

/* 【开 TCP 服务器】监听指定端口，等上位机连过来
 * 参数 : port —— 监听端口（1~65535，用 8080/8266 这类不占用的）
 * 返回 : 0 = 成功 / 1 = 模块没应答 / 2 = 参数非法
 * 说明 : 内部会先 AT+CIPMUX=1（服务器必须开多连接）
 *        ⚠ 开服务器前要先 JoinAP 连上路由器，然后用 GetIP 查出板子 IP
 *          告诉上位机；上位机连这个 IP + 上面的 port 即可
 * ⚠ 与 ESP8266_ConnectTCP 互斥，详见上方说明
 * 示例 : ESP8266_StartServer(8080);
 *        char ip[16]; ESP8266_GetIP(ip, sizeof(ip));
 *        printf("上位机请连 %s:8080\r\n", ip); */
uint8_t ESP8266_StartServer(uint16_t port);

/* 【关 TCP 服务器】
 * 返回 : 0 = 成功 / 1 = 模块没应答 */
uint8_t ESP8266_StopServer(void);

/* 【查有没有客户端连进来】★主循环里轮询它
 * 参数 : link_id —— 出参，连接号（**回发数据时要带上**）；不想要传 0
 * 返回 : 1 = 有上位机在线；0 = 没有
 * 说明 : 模块收到连接/断开时会主动上报 "<连接号>,CONNECT" / "<连接号>,CLOSED"，
 *        本函数扫接收缓冲来更新状态。
 * ⚠ 它是靠扫**当前接收缓冲**判断的，缓冲会被下一条 AT 命令清掉——
 *    所以主循环里要**经常调**（如每 10~50ms 一次），事件才不会漏。
 * 示例 : uint8_t link;
 *        if (ESP8266_ServerHasClient(&link)) { ... 上位机在线 ... } */
uint8_t ESP8266_ServerHasClient(uint8_t *link_id);

/* 【往指定连接发数据】服务器模式下用（客户端模式请用 ESP8266_SendData）
 * 参数 : link_id —— 连接号（从 ServerHasClient 拿到）
 *        data/len —— 数据（长度已知，**二进制安全**）
 * 返回 : 0 = 成功；1 = 发送失败（连接已断 / 没等到 '>'）
 * 说明 : 内部发 AT+CIPSEND=<连接号>,<长度>
 * 示例 : ESP8266_SendDataTo(link, "STATE:MODE=MANUAL,DIST=25,ALARM=0\r\n", 33); */
uint8_t ESP8266_SendDataTo(uint8_t link_id, const uint8_t *data, uint16_t len);

/* 【往指定连接发字符串】自动算长度 */
uint8_t ESP8266_SendStringTo(uint8_t link_id, const char *str);

#endif /* __FWLIB_ESP8266_H */
