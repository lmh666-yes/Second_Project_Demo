#include "esp8266.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_ms 等）独立文件 */
#include <string.h>

/* ================================================================
 *  esp8266.c —— ESP8266 AT 指令驱动  实现文件
 * ================================================================
 *  核心只有两个动作，其余全是组合：
 *    ① 收：把串口环形缓冲里的字节搬进本地回复缓冲（带超时）
 *    ② 找：在回复缓冲里用 strstr 找"期望字符串"
 *  所以 SendCmd 就是"发出去 → 反复做①②直到找到或超时"。
 *
 *  ⚠ 为什么不用 DMA/IDLE 收？AT 指令的回显长度不定、还可能夹杂
 *    异步提示（WIFI CONNECTED 之类），"环形缓冲 + 字符串查找"最皮实。
 *  ⚠ 两个注意点：
 *     · 回复缓冲满了就停止接收（防溢出），需要时可 ESP8266_Flush() 重来；
 *     · SendCmd 会先清空缓冲，所以"上一次的回显"要先取走再发新命令。
 * ================================================================ */


/* 回复缓冲（ASCII，末尾强制补 '\0'） */
static char     esp_rx[ESP8266_RX_BUF_SIZE];
static uint16_t esp_rx_len = 0;
static uint8_t  esp_async = 0;      /* 1 = 缓冲里有"主动上报"的数据 */

/* TCP 服务器状态（区块 4 用） */
static uint8_t  s_cli_online = 0;   /* 1 = 有上位机连进来 */
static uint8_t  s_cli_link   = 0;   /* 该上位机的连接号（0~4） */


/* ================================================================
 *                      内部小工具
 * ================================================================ */

/* 把串口缓冲里的字节搬进本地回复缓冲；返回新搬进来的字节数 */
static uint16_t esp_pump(void)
{
    uint16_t got = 0;

    while (SYS_USART_Available(ESP8266_USART) != 0U) {
        int c = SYS_USART_RxRead(ESP8266_USART);
        if (c < 0) break;

        if (esp_rx_len < (uint16_t)(ESP8266_RX_BUF_SIZE - 1U)) {
            esp_rx[esp_rx_len++] = (char)c;
            esp_rx[esp_rx_len]   = '\0';       /* 始终保持 C 字符串可用 */
        }
        got++;

        /* 已经收了 512 字节还没匹配，说明脏数据/异步信息刷屏，
         * 丢掉最前面一半腾地方（保证还能继续等本次命令的回复） */
        if (esp_rx_len >= (uint16_t)(ESP8266_RX_BUF_SIZE - 1U)) {
            uint16_t half = (uint16_t)(ESP8266_RX_BUF_SIZE / 2U);
            memmove(esp_rx, &esp_rx[half], (size_t)(esp_rx_len - half));
            esp_rx_len = (uint16_t)(esp_rx_len - half);
            esp_rx[esp_rx_len] = '\0';
        }
    }
    return got;
}

/* 在缓冲里找子串 */
static uint8_t esp_has(const char *token)
{
    if (token == 0 || token[0] == '\0') return 1U;
    return (strstr(esp_rx, token) != 0) ? 1U : 0U;
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
uint8_t ESP8266_SendCmd(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    uint32_t t0;

    if (cmd == 0) return 2U;
    if (timeout_ms == 0U) timeout_ms = ESP8266_CMD_TIMEOUT_MS;

    /* ① 清空旧内容（否则会匹配到上一次的 "OK"，导致"永远成功"） */
    ESP8266_Flush();

    /* ② 发命令 + 回车换行（AT 指令必须以 \r\n 结尾） */
    SYS_USART_SendString(ESP8266_USART, cmd);
    SYS_USART_SendString(ESP8266_USART, "\r\n");

    if (expect == 0 || expect[0] == '\0') return 0U;    /* 只发不等 */

    /* ③ 反复"搬 + 找"，直到命中或超时
     *    用 DWT 毫秒计时（不占 SysTick，RTOS 下也能用） */
    t0 = DWT_GetUs();
    while (DWT_ElapsedUs(t0) < (timeout_ms * 1000UL)) {
        (void)esp_pump();
        if (esp_has(expect)) return 0U;
    }

    (void)esp_pump();                  /* 超时前再捞一次，尽量把回复留全给调试 */
    return esp_has(expect) ? 0U : 1U;
}

const char *ESP8266_GetLastReply(void)
{
    return esp_rx;
}

/* 取原始接收缓冲（二进制安全）——给 MQTT 这类二进制协议用
 * 与 GetLastReply 是同一块缓冲，只是不当 C 字符串看待 */
uint16_t ESP8266_GetRawRx(const uint8_t **buf)
{
    (void)esp_pump();

    if (buf != 0) *buf = (const uint8_t *)esp_rx;
    return esp_rx_len;
}

void ESP8266_Flush(void)
{
    SYS_USART_RxFlush(ESP8266_USART);
    esp_rx_len = 0;
    esp_rx[0]  = '\0';
    esp_async  = 0;
}

uint8_t ESP8266_Init(uint32_t baudrate)
{
    if (baudrate == 0U) baudrate = ESP8266_DEFAULT_BAUD;

    SYS_USART_InitRxIT(ESP8266_USART, baudrate);
    delay_ms(200);                       /* 等模块上电/复位信息吐完 */

    /* 先试"复位 + 等 ready"；老固件可能不回 ready，就退回测 AT */
    if (ESP8266_Reset() == 0U) return 0U;
    return ESP8266_TestAT();
}

uint8_t ESP8266_TestAT(void)
{
    return ESP8266_SendCmd("AT", "OK", 1000U);
}

uint8_t ESP8266_Reset(void)
{
    if (ESP8266_SendCmd("AT+RST", "ready", 3000U) == 0U) return 0U;
    return ESP8266_SendCmd("AT+RST", "OK", 3000U);
}

uint8_t ESP8266_SetMode(uint8_t mode)
{
    char cmd[20];

    if (mode < 1U || mode > 3U) mode = ESP8266_MODE_STA;

    /* 手工拼 "AT+CWMODE=<n>"（不用 sprintf，省 Flash 且无 % 兼容问题） */
    cmd[0] = 'A'; cmd[1] = 'T'; cmd[2] = '+'; cmd[3] = 'C';
    cmd[4] = 'W'; cmd[5] = 'M'; cmd[6] = 'O'; cmd[7] = 'D';
    cmd[8] = 'E'; cmd[9] = '=';
    cmd[10] = (char)('0' + mode);
    cmd[11] = '\0';

    return ESP8266_SendCmd(cmd, "OK", ESP8266_CMD_TIMEOUT_MS);
}

/* 内部：把字符串拼进缓冲（简化版 strcat，带边界保护） */
static void esp_append(char *dst, uint16_t dst_size, const char *src)
{
    uint16_t n = 0;

    if (dst == 0 || src == 0 || dst_size == 0U) return;

    while (dst[n] != '\0' && n < dst_size) n++;
    while (*src != '\0' && n < (uint16_t)(dst_size - 1U)) {
        dst[n++] = *src++;
    }
    dst[n] = '\0';
}

uint8_t ESP8266_JoinAP(const char *ssid, const char *password)
{
    char cmd[96];

    if (ssid == 0) return 1U;

    cmd[0] = '\0';
    esp_append(cmd, sizeof(cmd), "AT+CWJAP=\"");
    esp_append(cmd, sizeof(cmd), ssid);
    esp_append(cmd, sizeof(cmd), "\",\"");
    esp_append(cmd, sizeof(cmd), (password != 0) ? password : "");
    esp_append(cmd, sizeof(cmd), "\"");

    return ESP8266_SendCmd(cmd, "OK", ESP8266_JOIN_TIMEOUT_MS);
}

uint8_t ESP8266_QuitAP(void)
{
    return ESP8266_SendCmd("AT+CWQAP", "OK", ESP8266_CMD_TIMEOUT_MS);
}

uint8_t ESP8266_GetIP(char *ip, uint16_t len)
{
    const char *p;
    uint16_t i = 0;

    if (ip == 0 || len == 0U) return 1U;
    ip[0] = '\0';

    /* 注意：CIFSR 的回复里既有 STAIP 也有 APIP，先找 STAIP */
    if (ESP8266_SendCmd("AT+CIFSR", "STAIP", 2000U) != 0U) {
        if (ESP8266_SendCmd("AT+CIFSR", "OK", 2000U) != 0U) return 1U;
    }

    p = strstr(esp_rx, "STAIP");
    if (p == 0) return 1U;

    p = strchr(p, '"');                 /* 地址在引号里："192.168.x.x" */
    if (p == 0) {
        p = strchr(strstr(esp_rx, "STAIP"), ':');   /* 老固件格式：STAIP:192.168.x.x */
        if (p == 0) return 1U;
        p++;
    } else {
        p++;
    }

    while (*p != '\0' && *p != '"' && *p != '\r' && *p != '\n' &&
           i < (uint16_t)(len - 1U)) {
        ip[i++] = *p++;
    }
    ip[i] = '\0';

    return (i == 0U) ? 1U : 0U;
}

uint8_t ESP8266_ConnectTCP(const char *host, uint16_t port)
{
    char cmd[80];
    char port_str[8];
    uint16_t v = port;
    uint8_t  k = 0;

    if (host == 0 || port == 0U) return 1U;

    /* 端口转十进制字符串（不用 sprintf） */
    {
        char tmp[8];
        uint8_t n = 0;
        while (v > 0U && n < 5U) { tmp[n++] = (char)('0' + (v % 10U)); v /= 10U; }
        while (n > 0U) port_str[k++] = tmp[--n];
        port_str[k] = '\0';
    }

    /* 单连接模式：AT+CIPMUX=0（要同时连多个服务器才改 1） */
    (void)ESP8266_SendCmd("AT+CIPMUX=0", "OK", ESP8266_CMD_TIMEOUT_MS);

    cmd[0] = '\0';
    esp_append(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"");
    esp_append(cmd, sizeof(cmd), host);
    esp_append(cmd, sizeof(cmd), "\",");
    esp_append(cmd, sizeof(cmd), port_str);

    return ESP8266_SendCmd(cmd, "OK", ESP8266_JOIN_TIMEOUT_MS);
}

uint8_t ESP8266_CloseTCP(void)
{
    return ESP8266_SendCmd("AT+CIPCLOSE", "OK", ESP8266_CMD_TIMEOUT_MS);
}

uint8_t ESP8266_SendData(const uint8_t *data, uint16_t len)
{
    char cmd[24];
    char tmp[8];
    uint16_t v = len;
    uint8_t  k = 0;
    uint8_t  n = 0;

    if (data == 0 || len == 0U) return 1U;
    if (len > 2048U) return 1U;                 /* ESP8266 单包上限 */

    /* AT+CIPSEND=<len>，模块回 '>' 后再灌数据 */
    {
        while (v > 0U && n < 5U) { tmp[n++] = (char)('0' + (v % 10U)); v /= 10U; }
        cmd[0] = '\0';
        esp_append(cmd, sizeof(cmd), "AT+CIPSEND=");
        while (n > 0U) { cmd[10 + k] = tmp[--n]; k++; }
        cmd[10 + k] = '\0';
    }

    if (ESP8266_SendCmd(cmd, ">", 3000U) != 0U) return 1U;

    /* 这里必须**直接**往串口写原始字节（不能再加 \r\n） */
    SYS_USART_SendBuf(ESP8266_USART, data, len);

    /* 等模块回 "SEND OK" */
    return ESP8266_SendCmd("", "SEND OK", 5000U);
}

uint8_t ESP8266_SendString(const char *str)
{
    uint16_t n = 0;

    if (str == 0) return 1U;
    while (str[n] != '\0') n++;
    return ESP8266_SendData((const uint8_t *)str, n);
}

uint8_t ESP8266_HasAsyncData(void)
{
    (void)esp_pump();

    /* 出现这些关键词就是"模块主动说话"，而不是对本条命令的回显 */
    if (esp_has("+IPD") ||
        esp_has("WIFI DISCONNECT") ||
        esp_has("WIFI CONNECTED") ||
        esp_has("CLOSED")) {
        esp_async = 1U;
    }
    return esp_async;
}


/* ================================================================
 *                区块 4：TCP 服务器（让上位机主动连过来）
 * ================================================================
 *  【为什么遥控小车要用服务器模式】
 *      Qt 上位机跑在电脑/手机上，IP 每次开机都在变，板子找不到它；
 *      反过来板子连上路由器后 IP 是固定的，而且能主动打印出来。
 *      所以让**上位机去连板子** —— 板子当服务器。
 *
 *  【AT 指令流程】
 *      AT+CIPMUX=1            ← 服务器必须多连接模式（单人模式不支持监听）
 *      AT+CIPSERVER=1,8080    ← 开始监听 8080
 *      ...上位机连进来 → 模块主动上报 "0,CONNECT"
 *      AT+CIPSEND=0,33        ← 往连接号 0 发 33 字节（等下 '>' 再灌数据）
 *      ...上位机断开 → 模块主动上报 "0,CLOSED"
 *      AT+CIPSERVER=0         ← 关监听
 * ================================================================ */

uint8_t ESP8266_StartServer(uint16_t port)
{
    char     cmd[32];
    char     tmp[8];
    uint16_t v = port;
    uint8_t  n = 0U;
    uint8_t  k;

    if (port == 0U) return 2U;

    /* 服务器必须开多连接；单连接(CIPMUX=0)下 CIPSERVER 会报错 */
    if (ESP8266_SendCmd("AT+CIPMUX=1", "OK", ESP8266_CMD_TIMEOUT_MS) != 0U) {
        return 1U;
    }

    /* 把端口号拆成十进制字符（低位在前，待会儿倒着填） */
    while ((v > 0U) && (n < 5U)) {
        tmp[n] = (char)('0' + (v % 10U));
        v /= 10U;
        n++;
    }

    cmd[0] = '\0';
    esp_append(cmd, sizeof(cmd), "AT+CIPSERVER=1,");
    k = (uint8_t)strlen(cmd);
    while (n > 0U) { n--; cmd[k] = tmp[n]; k++; }
    cmd[k] = '\0';

    s_cli_online = 0U;
    s_cli_link   = 0U;

    return ESP8266_SendCmd(cmd, "OK", ESP8266_CMD_TIMEOUT_MS);
}

uint8_t ESP8266_StopServer(void)
{
    s_cli_online = 0U;
    s_cli_link   = 0U;
    return ESP8266_SendCmd("AT+CIPSERVER=0", "OK", ESP8266_CMD_TIMEOUT_MS);
}

uint8_t ESP8266_ServerHasClient(uint8_t *link_id)
{
    const uint8_t *rx = 0;
    uint16_t n;
    uint16_t i;

    /* 这里必须用**二进制安全**的取缓冲接口：
     * 上位机发来的可能是任意字节，当 C 字符串看会被 0x00 截断 */
    n = ESP8266_GetRawRx(&rx);

    for (i = 0U; i < n; i++) {
        uint8_t id;

        if ((rx[i] < (uint8_t)'0') || (rx[i] > (uint8_t)'9')) continue;

        id = (uint8_t)(rx[i] - (uint8_t)'0');

        /* "<连接号>,CONNECT" —— 后跟 8 个字符 */
        if (((uint16_t)(i + 9U) <= n) &&
            (memcmp(&rx[i + 1U], ",CONNECT", 8U) == 0)) {
            s_cli_online = 1U;
            s_cli_link   = id;
            i = (uint16_t)(i + 8U);
        }
        /* "<连接号>,CLOSED" —— 后跟 7 个字符 */
        else if (((uint16_t)(i + 8U) <= n) &&
                 (memcmp(&rx[i + 1U], ",CLOSED", 7U) == 0)) {
            if (s_cli_link == id) s_cli_online = 0U;
            i = (uint16_t)(i + 7U);
        }
    }

    if (link_id != 0) *link_id = s_cli_link;
    return s_cli_online;
}

uint8_t ESP8266_SendDataTo(uint8_t link_id, const uint8_t *data, uint16_t len)
{
    char     cmd[32];
    char     tmp[8];
    uint16_t v = len;
    uint8_t  n = 0U;
    uint8_t  k;

    if (data == 0 || len == 0U) return 1U;
    if (len > 2048U) return 1U;                 /* ESP8266 单包上限 */

    while ((v > 0U) && (n < 5U)) {
        tmp[n] = (char)('0' + (v % 10U));
        v /= 10U;
        n++;
    }

    /* 多连接模式下长度要带连接号：AT+CIPSEND=<连接号>,<长度> */
    cmd[0] = '\0';
    esp_append(cmd, sizeof(cmd), "AT+CIPSEND=");
    k = (uint8_t)strlen(cmd);
    cmd[k] = (char)('0' + (link_id % 10U)); k++;
    cmd[k] = ',';                           k++;
    while (n > 0U) { n--; cmd[k] = tmp[n]; k++; }
    cmd[k] = '\0';

    if (ESP8266_SendCmd(cmd, ">", 3000U) != 0U) return 1U;

    /* 这里必须**直接**往串口写原始字节（不能再加 \r\n） */
    SYS_USART_SendBuf(ESP8266_USART, data, len);

    return ESP8266_SendCmd("", "SEND OK", 5000U);
}

uint8_t ESP8266_SendStringTo(uint8_t link_id, const char *str)
{
    uint16_t n = 0;

    if (str == 0) return 1U;
    while (str[n] != '\0') n++;
    return ESP8266_SendDataTo(link_id, (const uint8_t *)str, n);
}
