#include "esp8266.h"
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_ms 等）独立文件 */
#include <string.h>

/* ESP8266 AT 指令驱动实现
 * 收：把串口环形缓冲里的字节搬进本地回复缓冲；找：esp_find 匹配期望字符串
 * SendCmd 即发送后反复执行收与找，直到命中或超时
 * 采用环形缓冲加字符串查找，不用 DMA/IDLE：AT 回显长度不定，且可能夹杂异步提示
 * 缓冲满后停止接收，需要时先 ESP8266_Flush()；SendCmd 会先清空缓冲 */


/* 回复缓冲（ASCII，末尾强制补 '\0'）
 * volatile：Flush / GetRawRx / GetLastReply 与轮询状态机可能在不同任务中调用，
 * 不加限定符时编译器会缓存本函数内未改动的读，读到旧内容 */
static volatile char     esp_rx[ESP8266_RX_BUF_SIZE];
static volatile uint16_t esp_rx_len = 0;
static volatile uint8_t  esp_async = 0;      /* 1 = 缓冲里有"主动上报"的数据 */

/* TCP 服务器状态（区块 4 用） */
static volatile uint8_t  s_cli_online = 0;   /* 1 = 有上位机连进来 */
static volatile uint8_t  s_cli_link   = 0;   /* 该上位机的连接号（0~4） */


/* 内部小工具 */

/* 在缓冲里找关键词，返回其首次出现的下标，找不到返回 -1
 * 返回下标而非存在性：期望词与 ERROR 同时出现时，先出现的才是本命令的结果 */
static int esp_find(const char *token)
{
    uint16_t i, n;
    char     c;

    if (token == 0 || token[0] == '\0') return 0;

    n = (uint16_t)strlen(token);

    for (i = 0; (uint16_t)(i + n) <= esp_rx_len; i++) {
        uint16_t k;
        for (k = 0; k < n; k++) {
            c = esp_rx[i + k];
            if (c != token[k]) break;
        }
        if (k == n) return (int)i;      /* 整段匹配上 */
    }
    return -1;
}

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

        /* 缓冲将满时丢掉前一半旧数据，保留最新收到的尾部
         * 要等的 "OK" / "SEND OK" 只会出现在最新的数据里 */
        if (esp_rx_len >= (uint16_t)(ESP8266_RX_BUF_SIZE - 1U)) {
            uint16_t half   = (uint16_t)(ESP8266_RX_BUF_SIZE / 2U);
            uint16_t tail_n = (uint16_t)(esp_rx_len - half);   /* 保留尾部字节数 */
            uint16_t i;

            for (i = 0; i < tail_n; i++) esp_rx[i] = esp_rx[half + i];
            esp_rx_len         = tail_n;
            esp_rx[esp_rx_len] = '\0';
        }
    }
    return got;
}

/* 在缓冲里找子串（存在性判断，给调用方用） */
static uint8_t esp_has(const char *token)
{
    return (esp_find(token) >= 0) ? 1U : 0U;
}


/* 区块 2：基础功能 */
uint8_t ESP8266_SendCmd(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    uint32_t t0;

    if (cmd == 0) return 2U;
    if (timeout_ms == 0U) timeout_ms = ESP8266_CMD_TIMEOUT_MS;

    /* 清空旧内容，否则会匹配到上一次的 "OK" */
    ESP8266_Flush();

    /* 发命令加回车换行（AT 指令必须以 \r\n 结尾） */
    SYS_USART_SendString(ESP8266_USART, cmd);
    SYS_USART_SendString(ESP8266_USART, "\r\n");

    if (expect == 0 || expect[0] == '\0') return 0U;    /* 只发不等 */

    /* 反复搬入并查找，直到命中或超时；用 DWT 毫秒计时，不占 SysTick
     * 同时检查 "ERROR"/"FAIL"：模块拒绝命令时立即返回，不与超时混为一种结果 */
    t0 = DWT_GetUs();
    while (DWT_ElapsedUs(t0) < (timeout_ms * 1000UL)) {
        int hit, bad;

        (void)esp_pump();

        hit = esp_find(expect);
        bad = esp_find("ERROR");
        if (bad < 0) bad = esp_find("FAIL");
        /* 出现 ERROR 即判失败，即使回显里也含期望词 */
        if (bad >= 0) return 2U;
        if (hit >= 0) return 0U;
    }

    (void)esp_pump();                  /* 超时前再收一次，保留完整回复供调试 */
    if (esp_find(expect) >= 0) return 0U;
    if (esp_find("ERROR") >= 0 || esp_find("FAIL") >= 0) return 2U;
    return 1U;
}

/* 返回码含义（本文件 SendCmd 一致）：
 *   0 = 命中期望词（成功）
 *   1 = 超时，模块无回复（未接好 / 波特率不对 / 未上电）
 *   2 = 模块回 ERROR 或 FAIL（命令不合法 / 模块拒绝） */

const char *ESP8266_GetLastReply(void)
{
    /* esp_rx 带 volatile，这里显式转换后按 C 字符串返回；只读，语义不变 */
    return (const char *)esp_rx;
}

/* 取原始接收缓冲（二进制安全），供 MQTT 等二进制协议使用
 * 与 GetLastReply 是同一块缓冲，仅不当 C 字符串看待 */
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

    /* 手工拼 "AT+CWMODE=<n>"，不用 sprintf 以省 Flash */
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
    uint16_t cap;

    if (dst == 0 || src == 0 || dst_size == 0U) return;

    cap = (uint16_t)(dst_size - 1U);        /* 最多能放到这个下标 */

    /* 边界判断必须在解引用之前，否则 dst 已满时会越界读 dst[dst_size] */
    while (n < cap && dst[n] != '\0') n++;
    while (n < cap && *src != '\0') dst[n++] = *src++;
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
    int      at;
    uint16_t i = 0;

    if (ip == 0 || len == 0U) return 1U;
    ip[0] = '\0';

    /* CIFSR 的回复里既有 STAIP 也有 APIP，先找 STAIP */
    if (ESP8266_SendCmd("AT+CIFSR", "STAIP", 2000U) != 0U) {
        if (ESP8266_SendCmd("AT+CIFSR", "OK", 2000U) != 0U) return 1U;
    }

    /* 用 esp_find 取下标后自行扫分隔符：esp_rx 是 volatile，标准库字符串函数不能直接接
     * 两种固件格式：新固件 STAIP,"192.168.x.x"，老固件 STAIP:192.168.x.x */
    at = esp_find("STAIP");
    if (at < 0) return 1U;

    i = (uint16_t)at + 5U;                       /* 跳过 "STAIP" 本身 */
    while (i < esp_rx_len && esp_rx[i] != '"' && esp_rx[i] != ':') i++;
    if (i < esp_rx_len) i++;                     /* 跳过那个分隔符 */

    {
        uint16_t w = 0U;
        while (i < esp_rx_len && esp_rx[i] != '"' && esp_rx[i] != '\r' &&
               esp_rx[i] != '\n' && w < (uint16_t)(len - 1U)) {
            ip[w++] = esp_rx[i++];
        }
        ip[w] = '\0';
        return (w == 0U) ? 1U : 0U;
    }
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
        /* 长度从 "AT+CIPSEND=" 末尾（下标 11）开始追加，用 strlen 起算，
         * 防止长度位数变化时覆盖 '=' 拼出非法命令 */
        k = (uint8_t)strlen(cmd);
        while (n > 0U) { cmd[k] = tmp[--n]; k++; }
        cmd[k] = '\0';
    }

    if (ESP8266_SendCmd(cmd, ">", 3000U) != 0U) {
        /* 等 '>' 超时后必须重同步再返回
         * 若模块已回 '>' 而等待窗口错过，模块处于数据接收模式，
         * 之后的 AT 命令都会被当作载荷吃掉，直到复位
         * 依据 ESP8266 AT 手册：发 "+++"（不加 CRLF）退出数据模式，
         * 再发 "AT" 确认回到命令模式；已在命令模式下时 "+++" 只会回 ERROR */
        SYS_USART_SendString(ESP8266_USART, "+++");
        delay_ms(50);                            /* 手册要求 >20ms 静默 */
        (void)ESP8266_SendCmd("AT", "OK", 500U);
        return 1U;
    }

    /* 此处直接往串口写原始字节，不能再加 \r\n */
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

    /* 出现这些关键词说明模块主动上报，而不是对本条命令的回显 */
    if (esp_has("+IPD") ||
        esp_has("WIFI DISCONNECT") ||
        esp_has("WIFI CONNECTED") ||
        esp_has("CLOSED")) {
        esp_async = 1U;
    }
    return esp_async;
}


/* 区块 4：TCP 服务器（由上位机主动连接）
 * 上位机 IP 每次开机变化，板子连上路由器后 IP 固定，故由上位机连板子
 * 流程：AT+CIPMUX=1 开多连接（服务器必需，单连接下 CIPSERVER 报错）
 *       AT+CIPSERVER=1,8080 监听 8080
 *       上位机连入，模块上报 "0,CONNECT"
 *       AT+CIPSEND=0,33 向连接号 0 发 33 字节，等 '>' 后灌数据
 *       上位机断开，模块上报 "0,CLOSED"
 *       AT+CIPSERVER=0 关监听 */

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

    /* 用二进制安全的取缓冲接口：上位机可能发任意字节，按 C 字符串读会被 0x00 截断 */
    n = ESP8266_GetRawRx(&rx);

    for (i = 0U; i < n; i++) {
        uint8_t id;

        if ((rx[i] < (uint8_t)'0') || (rx[i] > (uint8_t)'9')) continue;

        id = (uint8_t)(rx[i] - (uint8_t)'0');

        /* "<连接号>,CONNECT" 后跟 8 个字符 */
        if (((uint16_t)(i + 9U) <= n) &&
            (memcmp(&rx[i + 1U], ",CONNECT", 8U) == 0)) {
            s_cli_online = 1U;
            s_cli_link   = id;
            i = (uint16_t)(i + 8U);
        }
        /* "<连接号>,CLOSED" 后跟 7 个字符 */
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

    /* 此处直接往串口写原始字节，不能再加 \r\n */
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
