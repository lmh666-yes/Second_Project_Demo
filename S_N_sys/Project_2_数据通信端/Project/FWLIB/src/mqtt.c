#include "mqtt.h"
#include "gpio_core.h"      /* 引脚/位 */
#include "delay.h"          /* DWT_GetUs / DWT_ElapsedUs: 心跳计时，免初始化 */
#include <string.h>

/* MQTT 3.1.1 客户端，传输层用 ESP8266 的 TCP
 *
 * 硬件映射
 * ---------------------------------------------------------------
 *  功能        资源            板上位置            改这里
 * ---------------------------------------------------------------
 *  WiFi 模块   USART3          P10 跳线选 WIFI     esp8266.h
 *  模块 TXD    PB11 (USART3_RX) 排针 PB11          --
 *  模块 RXD    PB10 (USART3_TX) 排针 PB10          --
 *  计时基准    DWT CYCCNT      内核自带            gpio_core.h
 * ---------------------------------------------------------------
 *
 * 报文格式
 *  固定头: [类型<<4 | 标志] [剩余长度(变长1~4字节，每字节7位，最高位=续)]
 *  CONNECT  0x10 | 协议名"MQTT" | 版本0x04 | 连接标志 | 心跳 | 客户号...
 *  CONNACK  0x20 0x02 <本次会话标志> <返回码>
 *  PUBLISH  0x30|QoS<<1|保留 | 主题(2字节长度+内容) | [报文号] | 载荷
 *  SUBSCRIBE 0x82 | 报文号(2) | 主题过滤器(2字节长度+内容) | 请求QoS(1)
 *  PINGREQ  0xC0 0x00        DISCONNECT 0xE0 0x00
 *
 * 下行数据: ESP8266 单连接模式(CIPMUX=0)收到 TCP 数据后从串口吐出
 *  "+IPD,<长度>:<MQTT报文原始字节>"
 *  本模块在接收缓冲里找 "+IPD,"，按长度取出后面的原始字节再拆包。
 *  取字节必须用 ESP8266_GetRawRx()，GetLastReply() 会被 0x00 截断。 */

/* 编译期检查：缓冲与长度宏配小了直接编译报错 */
typedef char mqtt_txbuf_check[(MQTT_TX_BUF_SIZE >= 64U) ? 1 : -1];
typedef char mqtt_topic_check[(MQTT_TOPIC_MAX_LEN >= 8U) ? 1 : -1];
typedef char mqtt_pl_check[((MQTT_PAYLOAD_MAX_LEN) >= 8U) ? 1 : -1];
typedef char mqtt_ipd_check[(ESP8266_RX_BUF_SIZE >= 256U) ? 1 : -1];


/* 模块内部状态 */
static uint8_t  s_connected  = 0U;      /* 1 = 收到过 CONNACK 且返回码 OK */
static uint8_t  s_connack    = 0xFFU;   /* broker 返回码（0xFF = 还没收到） */
static uint8_t  s_suback     = 0xFFU;   /* SUBACK 返回码（0xFF = 还没收到任何 SUBACK）
                                         * 0x80 = 服务器拒绝订阅（无权限 / 主题不被允许） */
static uint16_t s_pkt_id     = 1U;      /* 报文标识号，每发一包自增 */
static uint32_t s_last_tx    = 0U;      /* 上次发送时刻（DWT 微秒），供心跳用 */
static uint32_t s_last_rx    = 0U;      /* 上次收到 broker 任何字节的时刻（DWT 微秒）
                                         * 判活的唯一依据：TCP 可以半开（网线拔掉、路由器重启
                                         * 时本地 socket 收不到任何通知），只看发送是否成功
                                         * 会一直认为连接还在 */
static uint16_t s_raw_seen   = 0xFFFFU; /* 上次读取的 AT 缓冲长度，长度变化即期间收到过数据
                                         * 0xFFFF = 还没对齐过（刚连云/刚复位），首次读到的
                                         * 长度只记下来，不算收到 */
static uint16_t s_rx_cursor  = 0U;      /* 下行已解析位置，防同一条消息反复报 */


/* MQTT 字段一律大端，高字节在前 */
static void mqtt_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFU);
}

static uint16_t mqtt_get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* MQTT 字符串字段格式：2 字节大端长度 + 内容，返回写入字节数 */
static uint16_t mqtt_put_str(uint8_t *p, const char *s)
{
    uint16_t len = 0U;

    while (s[len] != '\0') len++;
    mqtt_put_u16(p, len);
    if (len > 0U) memcpy(&p[2], s, (size_t)len);
    return (uint16_t)(len + 2U);
}

/* 组包收尾：把剩余长度填回头部
 * 调用约定 : buf[0] = 报文类型，buf[1] = 先占 1 字节空位，报文正文从 buf[2] 起
 *            total = 含那 1 字节占位的整包长度
 * 返回     : 真正的整包长度（剩余长度可能要 2 字节，正文得右移）
 * 说明     : 剩余长度按 7 位一段编码，256 字节的包要 2 字节表示 */
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

/* 统一出口：把一包原始字节发给 ESP8266（SendData 是二进制安全的）
 * 复位下行游标（发 AT 命令会清接收缓冲） */
static uint8_t mqtt_send(const uint8_t *buf, uint16_t len)
{
    if (buf == 0 || len == 0U) return MQTT_ERR_PARAM;
    if (len > 2048U) return MQTT_ERR_TOO_LONG;

    s_rx_cursor = 0U;                           /* 缓冲马上被 SendCmd 清掉 */
    s_last_tx   = DWT_GetUs();                  /* 发过东西就算"活着" */

    return (ESP8266_SendData(buf, len) == 0U) ? MQTT_OK : MQTT_ERR_TX;
}

/* 判断自上次以来有没有从 broker 收到过任何字节
 * 原理 : ESP8266_GetRawRx() 给的是 AT 接收缓冲的当前长度，长度只在收到数据时
 *        增长、在发送 AT 命令清缓冲时归零，所以长度变了等价于期间收到过数据
 * 返回 : 1 = 收到过（同时刷新 s_last_rx），0 = 什么也没收到
 *        不看内容，PINGRESP(0xD0 0x00)、PUBLISH、SUBACK 都算 */
static uint8_t mqtt_alive_check_rx(void)
{
    uint16_t now = ESP8266_GetRawRx(0);     /* 只取长度，内部会 esp_pump() */

    if (s_raw_seen == 0xFFFFU) {            /* 刚对齐，这一次只记长度 */
        s_raw_seen = now;
        return 0U;
    }
    if (now != s_raw_seen) {
        s_raw_seen = now;
        s_last_rx  = DWT_GetUs();
        return 1U;
    }
    return 0U;
}

/* 在接收缓冲里找 "+IPD,<长度>:"
 * 返回 : 报文首字节下标（>=1），找不到 / 还没收全返回 0xFFFF
 * 出参 : pkt_len 本次 TCP 数据的字节数
 * 注意 : 报文里有 0x00，只能按原始字节扫描，不能用 strstr */
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

    /* 协议约束：MQTT 3.1.1 规定密码标志只能在使用者名标志为 1 时置 1，
     * 无使用者名时密码无处安放，这里把 passlen 一起清零 */
    if (userlen == 0U) {
        passlen = 0U;
    }

    /* 先算装不装得下，不够就返回，避免发出半包 */
    need = (uint16_t)(10U                              /* 类型+占位+协议名6+版本+标志+心跳2 */
                    + 2U + (uint16_t)strlen(client_id)
                    + (userlen ? (uint16_t)(2U + userlen) : 0U)
                    + (passlen ? (uint16_t)(2U + passlen) : 0U)
                    + 4U);                             /* 变长字段余量 */
    if (need > MQTT_TX_BUF_SIZE) return MQTT_ERR_TOO_LONG;

    s_connected = 0U;
    s_connack   = 0xFFU;
    s_suback    = 0xFFU;                    /* 新一轮连接：订阅结果重新开始等 */
    /* 判活计时复位；s_raw_seen 置哨兵值，让下一轮先对齐一次长度，
     * 不把连 TCP 之前缓冲里的旧数据当成 broker 刚发来的应答 */
    s_last_rx   = DWT_GetUs();
    s_raw_seen  = 0xFFFFU;

    /* 先把 TCP 打通（内部会发 AT+CIPMUX=0 和 AT+CIPSTART） */
    if (ESP8266_ConnectTCP(host, port) != 0U) {
        (void)ESP8266_CloseTCP();
        return MQTT_ERR_WIFI;
    }

    /* 组 CONNECT 报文 */
    buf[n++] = MQTT_PKT_CONNECT;
    buf[n++] = 0U;                                    /* 剩余长度占位，最后回填 */
    n = (uint16_t)(n + mqtt_put_str(&buf[n], "MQTT"));/* 协议名 */
    buf[n++] = 0x04U;                                 /* 协议级别 4 = MQTT 3.1.1 */

    /* 标志位：passlen 已在上面按无使用者名清零，组合一定合法 */
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

    /* 等 CONNACK：broker 通过 +IPD 送回 */
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
                s_last_rx   = DWT_GetUs();   /* 判活起点：CONNACK 是本次会话第一次收到 */
                s_rx_cursor = (uint16_t)(off + plen);
                s_last_tx   = DWT_GetUs();
                return MQTT_OK;
            }
            (void)ESP8266_CloseTCP();
            return MQTT_ERR_CONNACK;
        }
    }

    /* 超时：broker 无应答 */
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

/* 判活：距上次收到 broker 任何字节是否在 MQTT_ALIVE_TIMEOUT_S 以内
 * 判活计时由 MQTT_KeepAliveService() 推进 */
uint8_t MQTT_IsAlive(void)
{
    if (s_connected == 0U) return 0U;

    if (DWT_ElapsedUs(s_last_rx) >= ((uint32_t)MQTT_ALIVE_TIMEOUT_S * 1000000UL)) {
        return 0U;
    }
    return 1U;
}

uint8_t MQTT_GetConnackCode(void)
{
    return s_connack;
}

/* 订阅结果：0x00/0x01/0x02 = 订阅成功，授予 QoS 0/1/2
 * 0x80 = 服务器拒绝订阅（无权限 / 主题不被允许 / 主题格式非法）
 * 初值 0xFF，表示还没收到 SUBACK
 * 订阅被拒时 MQTT_Subscribe 仍返回 MQTT_OK，只能靠本返回码判断
 * SUBACK 由 MQTT_Poll 识别并记录 */
uint8_t MQTT_GetSubAck(void)
{
    return s_suback;
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
    /* 发出去就返回，不等 SUBACK
     * 回包不会丢，MQTT_Poll() 识别 SUBACK 并记下返回码，用 MQTT_GetSubAck() 查
     * 服务器拒绝订阅时本函数仍返回 MQTT_OK，只有 SUBACK 的 0x80 说明主题不能订 */
    return mqtt_send(buf, n);
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

    buf[0] = MQTT_PKT_PINGREQ;                        /* 0xC0 0x00 两字节报文 */
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
    s_raw_seen  = 0xFFFFU;                  /* 下次连云重新对齐接收长度 */
}


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
    if (rlen > 0U) (void)mqtt_alive_check_rx();     /* 有数据 = 链路活着 */
    if (rlen < 8U) return MQTT_ERR_TIMEOUT;

    /* 缓冲被 AT 命令冲过 → 游标复位，从头找 */
    if (s_rx_cursor >= rlen) s_rx_cursor = 0U;

    off = mqtt_find_ipd(&rx[s_rx_cursor], (uint16_t)(rlen - s_rx_cursor), &plen);
    if (off == 0xFFFFU) return MQTT_ERR_TIMEOUT;
    off = (uint16_t)(off + s_rx_cursor);

    p   = off;
    end = (uint16_t)(off + plen);
    b0  = rx[p++];

    if ((b0 & 0xF0U) != MQTT_PKT_PUBLISH) return MQTT_ERR_TIMEOUT;

    /* 解剩余长度：1 到 4 字节，每字节低 7 位有效，最高位表示后面还有 */
    do {
        if (p >= end) return MQTT_ERR_TIMEOUT;
        b = rx[p++];
        rem |= ((uint32_t)(b & 0x7FU)) << shift;
        shift = (uint8_t)(shift + 7U);
    } while ((b & 0x80U) != 0U && shift < 28U);

    if (rem < 2U) return MQTT_ERR_TIMEOUT;

    /* 报文真正的结尾 = 固定头 + 剩余长度，不是本 TCP 段总长
     * 一个 TCP 段可以装多个 MQTT 报文（服务器批量下发、或 QoS1 的 PUBLISH 后紧跟 PUBACK）
     * 载荷若按 end - p 取，会把段内后续报文的字节当成载荷，游标跳到段尾还会丢掉这些报文
     * 固定头 = 1 字节类型 + 剩余长度字段，shift 每解一个字节加 7，故字段字节数 = (shift + 6) / 7
     * 先用 uint32_t 算再钳位，避免 uint16_t 相加回绕 */
    {
        uint32_t pkt_end = (uint32_t)off + 1U + ((uint32_t)shift + 6U) / 7U + rem;

        if (pkt_end > (uint32_t)end) pkt_end = (uint32_t)end;   /* 段内不完整则截到段尾 */
        end = (uint16_t)pkt_end;
    }

    /* 段内后续报文要按游标继续取，不能一律返回 TIMEOUT
     * 一个 +IPD 段可以装多个报文，丢弃后面的会让 QoS1 的 PUBACK 一直不到，服务器反复重发 */
    if ((b0 & 0xF0U) != MQTT_PKT_PUBLISH) {
        /* SUBACK 固定为 0x90：只能用高 4 位常量 0x90 判断
         * 用 MQTT_PKT_SUBSCRIBE(0x82U) 这类带低 4 位的常量比较永不相等（0x82 & 0xF0 = 0x80） */
        if ((b0 & 0xF0U) == 0x90U) {
            /* SUBACK : 0x90 <剩余长度> <报文标识号2> <返回码1..n>
             * 返回码 0x00/0x01/0x02 = 授予 QoS 0/1/2；0x80 = 服务器拒绝订阅
             * （没有权限 / 主题不被允许 / 主题格式非法）
             * 返回码写入 s_suback，用 MQTT_GetSubAck() 查 */
            if ((uint32_t)(p - off) + 3U <= (uint32_t)end) {
                s_suback = rx[(uint16_t)(p + 2U)];
            }
        }
        /* PINGRESP(0xD0) / UNSUBACK(0xB0) / PUBACK(0x40) 不需要额外处理，
         * 上面 mqtt_alive_check_rx() 已把这批字节记进判活计时 */
        s_rx_cursor = end;
        return MQTT_ERR_TIMEOUT;                 /* 非 PUBLISH 报文，对调用方是无消息 */
    }

    /* 游标推进到本报文结尾，不是本 TCP 段结尾，下次 MQTT_Poll() 从段内剩余字节接着找
     * 本报文不完整时 pkt_end 已钳到段尾，残缺报文的后半段晚到也拼不回 */
    s_rx_cursor = end;

    /* 主题：2 字节长度 + 内容 */
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

    /* QoS>0 才有 2 字节报文标识号，跳过 */
    msg->qos    = (uint8_t)((b0 >> 1) & 0x03U);
    msg->retain = (uint8_t)(b0 & 0x01U);
    if (msg->qos > 0U) {
        if ((uint16_t)(p + 2U) > end) return MQTT_ERR_TIMEOUT;
        p = (uint16_t)(p + 2U);
    }

    /* 剩余字节全是载荷 */
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
    uint8_t got;

    if (s_connected == 0U) return MQTT_ERR_NOT_CONNECTED;

    /* 先听：自上次以来 broker 有没有送来字节，PINGRESP / PUBLISH / SUBACK 都算
     * 必须在发 PINGREQ 之前做，否则刚发完心跳就把距上次接收重置，等不到超时 */
    got = mqtt_alive_check_rx();

    /* 按半个心跳周期发 PINGREQ：按整周期发，broker 侧可能已到 1.5 倍宽限期，
     * 网络抖动就被断开，半周期留余量 */
    if (DWT_ElapsedUs(s_last_tx) >= ((uint32_t)(MQTT_KEEPALIVE_S / 2U) * 1000000UL)) {
        if (MQTT_Ping() != MQTT_OK) {
            return MQTT_ERR_TX;             /* 发不出去，ESP8266 侧就失败了 */
        }
    }

    /* 判死：刚收到过东西，或距上次收到还没超过判活超时，就算还活着 */
    if (got != 0U) return MQTT_OK;
    if (DWT_ElapsedUs(s_last_rx) < ((uint32_t)MQTT_ALIVE_TIMEOUT_S * 1000000UL)) {
        return MQTT_OK;                     /* 发过心跳，还在等 PINGRESP */
    }

    /* 心跳发出后这么久没收到任何字节，链路已断
     * 清掉连接标志，让调用方走转 W25QXX 缓存的分支
     * TCP 半开时 s_connected 一直是 1，不在这里清掉缓存就永远不启用 */
    s_connected = 0U;
    s_raw_seen  = 0xFFFFU;                  /* 重连时重新对齐接收长度 */
    return MQTT_ERR_ALIVE_TIMEOUT;
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

    /* INT32_MIN 直接取负会溢出，先加 1 再取负绕开 */
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

    /* 挡掉 NaN / Inf / 超量程：(int32_t)f 是未定义行为，ARM 的 VCVT 对 NaN
     * 和超 int32 的值结果随实现而定（常见是 0 或 INT32_MIN）
     * NaN 不是数，拒发并返回 MQTT_ERR_PARAM，不能当 0 发出去：传感器故障会在云端
     * 显示成温度 0 度，看着正常，比上报失败难查
     * ±Inf / 超量程钳到边界再发，对方能看出是越界值 */
    {
        float f = v * (float)mul;

        if (!(f == f)) {                    /* NaN：与自身比较必为假 */
            return MQTT_ERR_PARAM;          /* 拒发，不把坏值伪装成 0 */
        }
        if (f > 2000000000.0f) {
            f = 2000000000.0f;              /* 留余量给 +0.5 的进位 */
        } else if (f < -2000000000.0f) {
            f = -2000000000.0f;
        }
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

    /* t[] 低位在前，倒序输出即正常顺序，到小数点位置插入 '.' */
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
    case MQTT_ERR_ALIVE_TIMEOUT: return "ERR: ping sent but no reply, link dead";
    default:                     return "ERR: unknown";
    }
}
