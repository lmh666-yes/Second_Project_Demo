#include "mqtt.h"
#include "gpio_core.h"      /* DWT_GetUs / DWT_ElapsedUs —— 心跳计时，免初始化 */
#include <string.h>

/* ================================================================
 *  mqtt.c —— MQTT 3.1.1 客户端实现（跑在 ESP8266 的 TCP 之上）
 * ================================================================
 *  硬件映射表
 *  ---------------------------------------------------------------
 *   功能        资源            板上位置            改这里
 *  ---------------------------------------------------------------
 *   WiFi 模块   USART3          P10 跳线选 WIFI     esp8266.h
 *   模块 TXD    PB11 (USART3_RX) 排针 PB11          ——
 *   模块 RXD    PB10 (USART3_TX) 排针 PB10          ——
 *   计时基准    DWT CYCCNT      内核自带            gpio_core.h
 *  ---------------------------------------------------------------
 *  ⚠ 本模块不额外占外设，全部复用 ESP8266 的串口
 *
 *  【报文格式速查（调试时对着看）】
 *  固定头: [类型<<4 | 标志] [剩余长度(变长1~4字节，每字节7位，最高位=续)]
 *  CONNECT  0x10 | 协议名"MQTT" | 版本0x04 | 连接标志 | 心跳 | 客户号...
 *  CONNACK  0x20 0x02 <本次会话标志> <返回码>
 *  PUBLISH  0x30|QoS<<1|保留 | 主题(2字节长度+内容) | [报文号] | 载荷
 *  SUBSCRIBE 0x82 | 报文号(2) | 主题过滤器(2字节长度+内容) | 请求QoS(1)
 *  PINGREQ  0xC0 0x00        DISCONNECT 0xE0 0x00
 *
 *  【下行数据怎么来的】
 *  ESP8266 单连接模式(CIPMUX=0)收到 TCP 数据后从串口吐：
 *      +IPD,<长度>:<MQTT报文原始字节>
 *  所以本模块要在接收缓冲里找 "+IPD,"，按长度取出后面的原始字节再拆包。
 *  ⚠ 必须用 ESP8266_GetRawRx() 取字节，用 GetLastReply() 会被 0x00 截断！
 * ================================================================ */

/* ================================================================
 *                      编译期护栏（配错立刻报错）
 * ================================================================ */
typedef char mqtt_txbuf_check[(MQTT_TX_BUF_SIZE >= 64U) ? 1 : -1];
typedef char mqtt_topic_check[(MQTT_TOPIC_MAX_LEN >= 8U) ? 1 : -1];
typedef char mqtt_pl_check[((MQTT_PAYLOAD_MAX_LEN) >= 8U) ? 1 : -1];
typedef char mqtt_ipd_check[(ESP8266_RX_BUF_SIZE >= 256U) ? 1 : -1];


/* ================================================================
 *                          模块内部状态
 * ================================================================ */
static uint8_t  s_connected  = 0U;      /* 1 = 收到过 CONNACK 且返回码 OK */
static uint8_t  s_connack    = 0xFFU;   /* broker 给的返回码（0xFF = 还没收到） */
static uint16_t s_pkt_id     = 1U;      /* 报文标识号，每发一包自增 */
static uint32_t s_last_tx    = 0U;      /* 上次发送时刻（DWT 微秒），供心跳用 */
static uint16_t s_rx_cursor  = 0U;      /* 下行已解析到哪了，防同一条消息反复报 */


/* ================================================================
 *                        内部小工具
 * ================================================================ */

/* 小端？不——MQTT 全部是大端（高字节在前），别写反了 */
static void mqtt_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFU);
}

static uint16_t mqtt_get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* 往 p 写一个"2字节长度 + 内容"的 MQTT 字符串，返回写了几字节 */
static uint16_t mqtt_put_str(uint8_t *p, const char *s)
{
    uint16_t len = 0U;

    while (s[len] != '\0') len++;
    mqtt_put_u16(p, len);
    if (len > 0U) memcpy(&p[2], s, (size_t)len);
    return (uint16_t)(len + 2U);
}

/* 组包收尾：把"剩余长度"填回头部
 * 调用约定 : buf[0] = 报文类型，buf[1] = 先占 1 字节空位，报文正文从 buf[2] 起
 *            total = 含那 1 字节占位的整包长度
 * 返回     : 真正的整包长度（剩余长度字段可能要 2 字节，正文得右移）
 * 说明     : 剩余长度是按 7 位一段编码的，256 字节的包要 2 字节表示 */
static uint16_t mqtt_finalize(uint8_t *buf, uint16_t total)
{
    uint8_t  rl[4];
    uint8_t  rln = 0U;
    uint32_t body = (uint32_t)(total - 2U);     /* 去掉类型字节 + 占位字节 */
    uint32_t v = body;

    do {
        uint8_t b = (uint8_t)(v % 128U);
        v /= 128U;
        if (v > 0U) b = (uint8_t)(b | 0x80U);   /* 最高位 1 = 后面还有 */
        rl[rln++] = b;
    } while (v > 0U);

    if (rln > 1U) {
        /* 正文右移，给多出来的长度字节腾地方 */
        memmove(&buf[1U + rln], &buf[2], (size_t)body);
    }
    memcpy(&buf[1], rl, (size_t)rln);

    return (uint16_t)(total - 1U + rln);
}

/* 统一出口：把一包原始字节灌给 ESP8266（SendData 是二进制安全的）
 * 顺带刷新心跳计时、复位下行游标（发命令会清接收缓冲） */
static uint8_t mqtt_send(const uint8_t *buf, uint16_t len)
{
    if (buf == 0 || len == 0U) return MQTT_ERR_PARAM;
    if (len > 2048U) return MQTT_ERR_TOO_LONG;

    s_rx_cursor = 0U;                           /* 缓冲马上被 SendCmd 清掉 */
    s_last_tx   = DWT_GetUs();                  /* 发过东西就算"活着" */

    return (ESP8266_SendData(buf, len) == 0U) ? MQTT_OK : MQTT_ERR_TX;
}

/* 在接收缓冲里找 "+IPD,<长度>:"
 * 返回 : 报文首字节下标（>=1）；找不到 / 还没收全 返回 0xFFFF
 * 出参 : pkt_len —— 本次 TCP 数据的字节数
 * 注意 : 这里按**原始字节**扫描，不能用 strstr——报文里有 0x00 */
static uint16_t mqtt_find_ipd(const uint8_t *buf, uint16_t len, uint16_t *pkt_len)
{
    uint16_t i;
    uint16_t last;

    if (buf == 0 || len < 7U) return 0xFFFFU;

    /* 最少也要 "+IPD,1:x" = 8 字节，留个余量 */
    last = (uint16_t)(len - 7U);

    for (i = 0U; i <= last; i++) {
        uint16_t j;
        uint32_t n = 0U;
        uint8_t  digits = 0U;

        if (buf[i] != (uint8_t)'+') continue;
        if (memcmp(&buf[i], "+IPD,", 5U) != 0) continue;

        j = (uint16_t)(i + 5U);
        while (j < len && buf[j] >= (uint8_t)'0' && buf[j] <= (uint8_t)'9') {
            n = (n * 10U) + (uint32_t)(buf[j] - (uint8_t)'0');
            j++;
            digits++;
        }
        if (digits == 0U) continue;                 /* "+IPD," 后面不是数字 */
        if (j >= len || buf[j] != (uint8_t)':') continue;

        j++;                                        /* 跳过 ':' */
        if (n == 0U) continue;
        if ((uint32_t)j + n > (uint32_t)len) continue;  /* 数据还没收全，等下次 */

        if (pkt_len != 0) *pkt_len = (uint16_t)n;
        return j;
    }
    return 0xFFFFU;
}


/* ================================================================
 *                        区块 2：基础功能
 * ================================================================ */

uint8_t MQTT_Connect(const char *host, uint16_t port, const char *client_id,
                     const char *user, const char *pass)
{
    uint8_t  buf[MQTT_TX_BUF_SIZE];
    uint8_t  flags = 0x02U;                 /* bit1 = 清理会话：每次都当新客户 */
    uint8_t  userlen = 0U;
    uint8_t  passlen = 0U;
    uint16_t n = 0U;
    uint32_t t0;
    uint16_t need;

    if (host == 0 || client_id == 0) return MQTT_ERR_PARAM;
    if (port == 0U) port = MQTT_DEFAULT_PORT;

    if (user != 0) { while (user[userlen] != '\0') userlen++; }
    if (pass != 0) { while (pass[passlen] != '\0') passlen++; }

    /* ① 先算够不够装，不够直接说——别发一半把 broker 搞懵 */
    need = (uint16_t)(10U                              /* 类型+占位+协议名6+版本+标志+心跳2 */
                    + 2U + (uint16_t)strlen(client_id)
                    + (userlen ? (uint16_t)(2U + userlen) : 0U)
                    + (passlen ? (uint16_t)(2U + passlen) : 0U)
                    + 4U);                             /* 变长字段余量 */
    if (need > MQTT_TX_BUF_SIZE) return MQTT_ERR_TOO_LONG;

    s_connected = 0U;
    s_connack   = 0xFFU;

    /* ② 先把 TCP 打通（内部会发 AT+CIPMUX=0 + AT+CIPSTART） */
    if (ESP8266_ConnectTCP(host, port) != 0U) {
        (void)ESP8266_CloseTCP();
        return MQTT_ERR_WIFI;
    }

    /* ③ 组 CONNECT 报文 */
    buf[n++] = MQTT_PKT_CONNECT;
    buf[n++] = 0U;                                    /* 剩余长度占位，最后回填 */
    n = (uint16_t)(n + mqtt_put_str(&buf[n], "MQTT"));/* 协议名 */
    buf[n++] = 0x04U;                                 /* 协议级别 4 = MQTT 3.1.1 */

    if (userlen > 0U) flags = (uint8_t)(flags | 0x80U);
    if (passlen > 0U) flags = (uint8_t)(flags | 0x40U);
    buf[n++] = flags;

    mqtt_put_u16(&buf[n], MQTT_KEEPALIVE_S); n = (uint16_t)(n + 2U);

    n = (uint16_t)(n + mqtt_put_str(&buf[n], client_id));
    if (userlen > 0U) n = (uint16_t)(n + mqtt_put_str(&buf[n], user));
    if (passlen > 0U) n = (uint16_t)(n + mqtt_put_str(&buf[n], pass));

    n = mqtt_finalize(buf, n);

    if (mqtt_send(buf, n) != MQTT_OK) {
        (void)ESP8266_CloseTCP();
        return MQTT_ERR_TX;
    }

    /* ④ 等 CONNACK（broker 通过 +IPD 送回来） */
    t0 = DWT_GetUs();
    while (DWT_ElapsedUs(t0) < (MQTT_CONNACK_TIMEOUT_MS * 1000UL)) {
        const uint8_t *rx = 0;
        uint16_t rlen;
        uint16_t plen = 0U;
        uint16_t off;

        rlen = ESP8266_GetRawRx(&rx);
        off  = mqtt_find_ipd(rx, rlen, &plen);

        if (off != 0xFFFFU && plen >= 4U && (rx[off] & 0xF0U) == MQTT_PKT_CONNACK) {
            /* 0x20 0x02 <会话标志> <返回码> */
            s_connack = rx[off + 3U];
            if (s_connack == MQTT_CONNACK_ACCEPTED) {
                s_connected = 1U;
                s_pkt_id    = 1U;
                s_rx_cursor = (uint16_t)(off + plen);
                s_last_tx   = DWT_GetUs();
                return MQTT_OK;
            }
            (void)ESP8266_CloseTCP();
            return MQTT_ERR_CONNACK;
        }
    }

    /* ⑤ 超时：broker 没理我们 */
    (void)ESP8266_CloseTCP();
    return MQTT_ERR_TIMEOUT;
}

uint8_t MQTT_ConnectSimple(const char *host, const char *client_id)
{
    return MQTT_Connect(host, MQTT_DEFAULT_PORT, client_id, 0, 0);
}

uint8_t MQTT_IsConnected(void)
{
    return s_connected;
}

uint8_t MQTT_GetConnackCode(void)
{
    return s_connack;
}

uint8_t MQTT_Publish(const char *topic, const uint8_t *payload, uint16_t len,
                     uint8_t qos, uint8_t retain)
{
    uint8_t  buf[MQTT_TX_BUF_SIZE];
    uint16_t n = 0U;
    uint16_t tl = 0U;

    if (s_connected == 0U) return MQTT_ERR_NOT_CONNECTED;
    if (topic == 0) return MQTT_ERR_PARAM;
    if (payload == 0 && len > 0U) return MQTT_ERR_PARAM;

    while (topic[tl] != '\0') tl++;

    /* 类型1 + 长度占位1 + 变长余量4 + 主题(2+tl) + [报文号2] + 载荷 */
    if (((uint32_t)tl + (uint32_t)len + 9U) > (uint32_t)MQTT_TX_BUF_SIZE) {
        return MQTT_ERR_TOO_LONG;
    }

    buf[n++] = (uint8_t)(MQTT_PKT_PUBLISH
                       | (uint8_t)((qos & 0x03U) << 1)
                       | (retain ? 0x01U : 0x00U));
    buf[n++] = 0U;                                    /* 剩余长度占位 */

    n = (uint16_t)(n + mqtt_put_str(&buf[n], topic));

    if (qos > 0U) {                                   /* QoS>0 才带报文标识号 */
        mqtt_put_u16(&buf[n], s_pkt_id);
        n = (uint16_t)(n + 2U);
        s_pkt_id++;
        if (s_pkt_id == 0U) s_pkt_id = 1U;            /* 0 是非法的 */
    }

    if (len > 0U) {
        memcpy(&buf[n], payload, (size_t)len);
        n = (uint16_t)(n + len);
    }

    n = mqtt_finalize(buf, n);
    return mqtt_send(buf, n);
}

uint8_t MQTT_PublishStr(const char *topic, const char *text)
{
    uint16_t len = 0U;

    if (text == 0) return MQTT_ERR_PARAM;
    while (text[len] != '\0') len++;
    return MQTT_Publish(topic, (const uint8_t *)text, len, 0U, 0U);
}

uint8_t MQTT_Subscribe(const char *topic_filter, uint8_t qos)
{
    uint8_t  buf[MQTT_TX_BUF_SIZE];
    uint16_t n = 0U;
    uint16_t tl = 0U;

    if (s_connected == 0U) return MQTT_ERR_NOT_CONNECTED;
    if (topic_filter == 0) return MQTT_ERR_PARAM;

    while (topic_filter[tl] != '\0') tl++;
    if (((uint32_t)tl + 9U) > (uint32_t)MQTT_TX_BUF_SIZE) return MQTT_ERR_TOO_LONG;

    buf[n++] = MQTT_PKT_SUBSCRIBE;                    /* 0x82，低4位固定 0010 */
    buf[n++] = 0U;

    mqtt_put_u16(&buf[n], s_pkt_id); n = (uint16_t)(n + 2U);
    s_pkt_id++;
    if (s_pkt_id == 0U) s_pkt_id = 1U;

    n = (uint16_t)(n + mqtt_put_str(&buf[n], topic_filter));
    buf[n++] = (uint8_t)(qos & 0x03U);                /* 请求的 QoS */

    n = mqtt_finalize(buf, n);
    return mqtt_send(buf, n);                         /* 不等 SUBACK，回包靠 Poll 收 */
}

uint8_t MQTT_Unsubscribe(const char *topic_filter)
{
    uint8_t  buf[MQTT_TX_BUF_SIZE];
    uint16_t n = 0U;
    uint16_t tl = 0U;

    if (s_connected == 0U) return MQTT_ERR_NOT_CONNECTED;
    if (topic_filter == 0) return MQTT_ERR_PARAM;

    while (topic_filter[tl] != '\0') tl++;
    if (((uint32_t)tl + 9U) > (uint32_t)MQTT_TX_BUF_SIZE) return MQTT_ERR_TOO_LONG;

    buf[n++] = MQTT_PKT_UNSUBSCRIBE;                  /* 0xA2，低4位固定 0010 */
    buf[n++] = 0U;

    mqtt_put_u16(&buf[n], s_pkt_id); n = (uint16_t)(n + 2U);
    s_pkt_id++;
    if (s_pkt_id == 0U) s_pkt_id = 1U;

    n = (uint16_t)(n + mqtt_put_str(&buf[n], topic_filter));

    n = mqtt_finalize(buf, n);
    return mqtt_send(buf, n);
}

uint8_t MQTT_Ping(void)
{
    uint8_t buf[2];

    if (s_connected == 0U) return MQTT_ERR_NOT_CONNECTED;

    buf[0] = MQTT_PKT_PINGREQ;                        /* 0xC0 0x00，两字节走人 */
    buf[1] = 0x00U;
    return mqtt_send(buf, 2U);
}

void MQTT_Disconnect(void)
{
    uint8_t buf[2];

    buf[0] = MQTT_PKT_DISCONNECT;                     /* 0xE0 0x00 */
    buf[1] = 0x00U;
    if (s_connected != 0U) (void)mqtt_send(buf, 2U);

    (void)ESP8266_CloseTCP();
    s_connected = 0U;
}


/* ================================================================
 *                        区块 3：扩展功能
 * ================================================================ */

uint8_t MQTT_Poll(MqttMsg_t *msg)
{
    const uint8_t *rx = 0;
    uint16_t rlen;
    uint16_t plen = 0U;
    uint16_t off;
    uint16_t p;
    uint16_t end;
    uint16_t tl;
    uint16_t copy;
    uint16_t i;
    uint32_t rem = 0U;
    uint8_t  shift = 0U;
    uint8_t  b;
    uint8_t  b0;

    if (msg == 0) return MQTT_ERR_PARAM;
    if (s_connected == 0U) return MQTT_ERR_NOT_CONNECTED;

    msg->topic[0]     = '\0';
    msg->payload[0]   = '\0';
    msg->topic_len    = 0U;
    msg->payload_len  = 0U;
    msg->qos          = 0U;
    msg->retain       = 0U;

    rlen = ESP8266_GetRawRx(&rx);
    if (rlen < 8U) return MQTT_ERR_TIMEOUT;

    /* 缓冲被 AT 命令冲过 → 游标复位，从头找 */
    if (s_rx_cursor >= rlen) s_rx_cursor = 0U;

    off = mqtt_find_ipd(&rx[s_rx_cursor], (uint16_t)(rlen - s_rx_cursor), &plen);
    if (off == 0xFFFFU) return MQTT_ERR_TIMEOUT;
    off = (uint16_t)(off + s_rx_cursor);

    s_rx_cursor = (uint16_t)(off + plen);             /* 无论解不解得出都往后走 */

    p   = off;
    end = (uint16_t)(off + plen);
    b0  = rx[p++];

    if ((b0 & 0xF0U) != MQTT_PKT_PUBLISH) return MQTT_ERR_TIMEOUT;

    /* ① 解剩余长度（1~4 字节，每字节 7 位有效） */
    do {
        if (p >= end) return MQTT_ERR_TIMEOUT;
        b = rx[p++];
        rem |= ((uint32_t)(b & 0x7FU)) << shift;
        shift = (uint8_t)(shift + 7U);
    } while ((b & 0x80U) != 0U && shift < 28U);

    if (rem < 2U) return MQTT_ERR_TIMEOUT;

    /* ② 主题（2字节长度 + 内容） */
    if ((uint16_t)(p + 2U) > end) return MQTT_ERR_TIMEOUT;
    tl = mqtt_get_u16(&rx[p]);
    p  = (uint16_t)(p + 2U);
    if (tl == 0U || ((uint32_t)tl + 2U) > rem) return MQTT_ERR_TIMEOUT;
    if ((uint32_t)p + (uint32_t)tl > (uint32_t)end) return MQTT_ERR_TIMEOUT;

    copy = tl;
    if (copy >= (uint16_t)MQTT_TOPIC_MAX_LEN) copy = (uint16_t)(MQTT_TOPIC_MAX_LEN - 1U);
    for (i = 0U; i < copy; i++) msg->topic[i] = (char)rx[p + i];
    msg->topic[copy]  = '\0';
    msg->topic_len    = copy;
    p = (uint16_t)(p + tl);

    /* ③ QoS>0 才有报文标识号，跳过 */
    msg->qos    = (uint8_t)((b0 >> 1) & 0x03U);
    msg->retain = (uint8_t)(b0 & 0x01U);
    if (msg->qos > 0U) {
        if ((uint16_t)(p + 2U) > end) return MQTT_ERR_TIMEOUT;
        p = (uint16_t)(p + 2U);
    }

    /* ④ 剩下全是载荷 */
    if (p > end) return MQTT_ERR_TIMEOUT;

    copy = (uint16_t)(end - p);
    if (copy >= (uint16_t)MQTT_PAYLOAD_MAX_LEN) {
        copy = (uint16_t)(MQTT_PAYLOAD_MAX_LEN - 1U);
    }
    for (i = 0U; i < copy; i++) msg->payload[i] = (char)rx[p + i];
    msg->payload[copy] = '\0';
    msg->payload_len   = copy;

    return MQTT_OK;
}

uint8_t MQTT_KeepAliveService(void)
{
    if (s_connected == 0U) return MQTT_ERR_NOT_CONNECTED;

    /* 距上次发送超过一个心跳周期就"在"一下
     * DWT_ElapsedUs 自带回绕处理，CYCCNT 跑满 32 位也不会算错 */
    if (DWT_ElapsedUs(s_last_tx) >= ((uint32_t)MQTT_KEEPALIVE_S * 1000000UL)) {
        return MQTT_Ping();
    }
    return MQTT_OK;
}

uint8_t MQTT_PublishInt(const char *topic, int32_t v)
{
    char     s[12];
    char     t[12];
    uint32_t u;
    uint8_t  n = 0U;
    uint8_t  k = 0U;
    uint8_t  neg = 0U;

    if (topic == 0) return MQTT_ERR_PARAM;

    /* 注意 -2147483648 直接取负会溢出，先加 1 再取负绕开 */
    if (v < 0) { neg = 1U; u = (uint32_t)(-(v + 1)) + 1U; }
    else       { u = (uint32_t)v; }

    do {
        t[k++] = (char)('0' + (u % 10U));
        u /= 10U;
    } while (u > 0U && k < 10U);

    if (neg != 0U) t[k++] = '-';

    while (k > 0U) { k--; s[n++] = t[k]; }
    s[n] = '\0';

    return MQTT_PublishStr(topic, s);
}

uint8_t MQTT_PublishFloat(const char *topic, float v, uint8_t decimals)
{
    char     s[16];
    char     t[12];
    uint32_t u;
    int32_t  iv;
    uint8_t  n = 0U;
    uint8_t  k = 0U;
    uint8_t  neg = 0U;
    uint8_t  idx;
    uint32_t mul;

    if (topic == 0) return MQTT_ERR_PARAM;
    if (decimals > 3U) decimals = 3U;

    mul = (decimals == 0U) ? 1U : ((decimals == 1U) ? 10U : ((decimals == 2U) ? 100U : 1000U));

    /* 四舍五入到整数，后面全靠整数运算——不用 sprintf，省 2KB Flash */
    {
        float f = v * (float)mul;
        iv = (int32_t)((f >= 0.0f) ? (f + 0.5f) : (f - 0.5f));
    }

    if (iv < 0) { neg = 1U; u = (uint32_t)(-(int64_t)iv); }
    else        { u = (uint32_t)iv; }

    do {
        t[k++] = (char)('0' + (u % 10U));
        u /= 10U;
    } while (u > 0U && k < 10U);

    /* 位数不够补 0：0.05 要显示成 "0.0" 而不是 ".5" */
    while (k < (uint8_t)(decimals + 1U) && k < 10U) t[k++] = '0';

    if (neg != 0U) s[n++] = '-';

    /* t[] 是低位在前，倒着吐出来就是正常顺序；到小数点位置插一个 '.' */
    idx = k;
    while (idx > 0U) {
        if (decimals > 0U && idx == decimals) s[n++] = '.';
        idx--;
        s[n++] = t[idx];
    }
    s[n] = '\0';

    return MQTT_PublishStr(topic, s);
}

const char *MQTT_ErrStr(uint8_t err)
{
    switch (err) {
    case MQTT_OK:                return "OK";
    case MQTT_ERR_PARAM:         return "ERR: null param";
    case MQTT_ERR_WIFI:          return "ERR: wifi/tcp not up";
    case MQTT_ERR_TX:            return "ERR: send failed";
    case MQTT_ERR_CONNACK:       return "ERR: connack refused";
    case MQTT_ERR_TIMEOUT:       return "ERR: timeout";
    case MQTT_ERR_NOT_CONNECTED: return "ERR: not connected";
    case MQTT_ERR_TOO_LONG:      return "ERR: topic or payload too long";
    default:                     return "ERR: unknown";
    }
}

/* ==================== mqtt.c end ==================== */
