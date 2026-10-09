#ifndef __FWLIB_MQTT_H
#define __FWLIB_MQTT_H

#include "stm32f4xx.h"
#include "esp8266.h"

/* ================================================================
 *  mqtt.h —— 【协议】MQTT 3.1.1 客户端（跑在 ESP8266 的 TCP 之上）
 * ================================================================
 *  设计定位 : ESP8266 只把 TCP 通道打通；本模块在它上面再包一层 MQTT，
 *             让"上报一条数据到云平台"变成一行调用。
 *             面向【智能环境监测小车 / 智能家居终端 / 工业数据采集网关】。
 *  依赖     : esp8266.h（TCP 通道）→ sys_usart.h
 *             时间基准用 DWT，见 gpio_core.h（开机自动启用，免初始化）
 *  标准库关键词 : 无——纯字节流组包/拆包
 *
 *  【接线（普中-天马 F407开发板）】
 *      ⚠ 本模块不额外接线，全靠 ESP8266 那一套：
 *        P10 跳线选 WIFI → USART3  模块 TXD→PB11(RX) 模块 RXD→PB10(TX)
 *        ESP8266 发射瞬间 200mA+，别用 STM32 的 3.3V 直接供
 *
 *  【使用方式（连路由 → 连云 → 上报 → 订阅 → 收控制）】
 *      ESP8266_Init(115200);
 *      ESP8266_SetMode(ESP8266_MODE_STA);
 *      if (ESP8266_JoinAP("my_wifi", "12345678") != 0) { 失败了... }
 *
 *      if (MQTT_Connect("broker.emqx.io", 1883, "f407-001", 0, 0) == MQTT_OK) {
 *          MQTT_Subscribe("cmd/f407-001", 0);
 *          MQTT_PublishStr("data/f407-001", "hello");
 *      }
 *
 *      for (;;) {
 *          MqttMsg_t m;
 *          if (MQTT_Poll(&m) == MQTT_OK) {           // 收到下行消息
 *              if (strcmp(m.topic, "cmd/f407-001") == 0 && strcmp(m.payload, "on") == 0) {
 *                  LED_On(LED_DS0);
 *              }
 *          }
 *          MQTT_KeepAliveService();                  // 主循环里调，自动发心跳
 *          delay_ms(20);
 *      }
 *
 *  【和云平台对接】
 *      公共测试 broker（匿名、免费）：broker.emqx.io / test.mosquitto.org / 1883
 *      阿里云/腾讯云/OneNET/华为云：把平台的 user/password 填进 MQTT_Connect 后两个参数
 *      主题自己定，常见约定：data/<设备号> 上报、cmd/<设备号> 下发
 *
 *  【v1 限制（先说清楚，免得踩坑）】
 *      · 只实现 QoS0（发完即忘，broker 不重传）——监测类上报足够
 *      · 下行 payload 按**文本**解析（遇到 0x00 会截断）——
 *        on/off/1/0 这类控制指令没问题；要传二进制请自行扩展 MQTT_Poll
 *      · 单包上限 = MQTT_TX_BUF_SIZE；下行报文受 ESP8266_RX_BUF_SIZE(512) 限制
 *      · 不支持 TLS（8883）——STM32F407 跑 TLS 太重，要加密得换 AT 固件带 SSL
 *
 *  移植指引 : 换 broker 只改 MQTT_Connect 的四个参数；
 *             换 WiFi 串口改 esp8266.h 里的 ESP8266_USART；
 *             要更大的上行包改 MQTT_TX_BUF_SIZE（注意栈开销！）；
 *             要 QoS1：发布时传 qos=1，然后等 broker 回 PUBACK（包类型 0x40）。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换场景只改这里）
 * ================================================================ */
#define MQTT_DEFAULT_PORT       1883U   /* 明文 MQTT 端口 */
#define MQTT_KEEPALIVE_S        60U     /* 心跳周期（秒）；broker 一般 1.5 倍没动静就踢人 */
#define MQTT_TX_BUF_SIZE        256U    /* 上行缓冲：报文头 + 主题 + 载荷 全算在内 */
#define MQTT_TOPIC_MAX_LEN      64U     /* 下行主题最大长度（含 '\0'） */
#define MQTT_PAYLOAD_MAX_LEN    128U    /* 下行载荷最大长度（含 '\0'） */
#define MQTT_CONNACK_TIMEOUT_MS 5000U   /* 等 broker 回 CONNACK 的最长时间 */

/* 返回码（全库统一：0 成功，非 0 见下） */
#define MQTT_OK                 0U      /* 成功 */
#define MQTT_ERR_PARAM          1U      /* 参数不对（空指针） */
#define MQTT_ERR_WIFI           2U      /* ESP8266 没连上 / TCP 建不起来 */
#define MQTT_ERR_TX             3U      /* 数据没发出去 */
#define MQTT_ERR_CONNACK        4U      /* 连云被拒，具体看 MQTT_GetConnackCode() */
#define MQTT_ERR_TIMEOUT        5U      /* 等回复超时 */
#define MQTT_ERR_NOT_CONNECTED  6U      /* 还没连上 broker 就调了发布/订阅 */
#define MQTT_ERR_TOO_LONG       7U      /* 主题或载荷超出缓冲 */

/* broker 在 CONNACK 里给的返回码（MQTT_Connect 失败后用 MQTT_GetConnackCode 看） */
#define MQTT_CONNACK_ACCEPTED        0U /* 连上了 */
#define MQTT_CONNACK_BAD_PROTOCOL    1U /* 协议版本不支持（老 broker 试 level=3） */
#define MQTT_CONNACK_ID_REJECTED     2U /* client id 被拒（换个名字，或加长） */
#define MQTT_CONNACK_SERVER_UNAVAIL  3U /* 服务器不可用 */
#define MQTT_CONNACK_BAD_USERPASS    4U /* 用户名/密码错 */
#define MQTT_CONNACK_NOT_AUTHORIZED  5U /* 没授权 */

/* 报文类型首字节（扩展/调试时用） */
#define MQTT_PKT_CONNECT        0x10U   /* 客户端 → 服务器：请求连接 */
#define MQTT_PKT_CONNACK        0x20U   /* 服务器 → 客户端：连接应答 */
#define MQTT_PKT_PUBLISH        0x30U   /* 双向：发布消息 */
#define MQTT_PKT_PUBACK         0x40U   /* QoS1 的发布确认 */
#define MQTT_PKT_SUBSCRIBE      0x82U   /* 订阅（低 4 位固定 0010） */
#define MQTT_PKT_SUBACK         0x90U   /* 订阅应答 */
#define MQTT_PKT_UNSUBSCRIBE    0xA2U   /* 取消订阅 */
#define MQTT_PKT_UNSUBACK       0xB0U   /* 取消订阅应答 */
#define MQTT_PKT_PINGREQ        0xC0U   /* 心跳请求 */
#define MQTT_PKT_PINGRESP       0xD0U   /* 心跳应答 */
#define MQTT_PKT_DISCONNECT     0xE0U   /* 断开 */

/* 一条下行消息（MQTT_Poll 的出口） */
typedef struct {
    char     topic[MQTT_TOPIC_MAX_LEN];         /* 主题（已补 '\0'） */
    char     payload[MQTT_PAYLOAD_MAX_LEN];     /* 载荷（已补 '\0'，文本用） */
    uint16_t topic_len;                         /* 主题实际长度 */
    uint16_t payload_len;                       /* 载荷实际长度 */
    uint8_t  qos;                               /* 0 / 1 / 2 */
    uint8_t  retain;                            /* 1 = broker 保留消息 */
} MqttMsg_t;


/* ================================================================
 *                        区块 2：基础功能
 * ================================================================ */

/* 【连 broker】内部会先建 TCP，再发 MQTT CONNECT 并等 CONNACK
 * 参数 : host      —— broker 域名或 IP，如 "broker.emqx.io"
 *        port      —— 端口，填 0 用 MQTT_DEFAULT_PORT(1883)
 *        client_id —— 设备唯一名，**同一 broker 上不能重名**，建议 "f407-001"
 *        user      —— 用户名，匿名 broker 传 0
 *        pass      —— 密码，  匿名 broker 传 0
 * 返回 : MQTT_OK / MQTT_ERR_*
 * ⚠ 调用前请先 ESP8266_Init + SetMode + JoinAP（WiFi 得先联网）
 * 示例 : MQTT_Connect("broker.emqx.io", 1883, "f407-001", 0, 0); */
uint8_t MQTT_Connect(const char *host, uint16_t port, const char *client_id,
                     const char *user, const char *pass);

/* 【连 broker（省事版）】默认端口 1883 + 匿名登录
 * 返回 : MQTT_OK / MQTT_ERR_*
 * 示例 : MQTT_ConnectSimple("test.mosquitto.org", "f407-001"); */
uint8_t MQTT_ConnectSimple(const char *host, const char *client_id);

/* 【查连接状态】1 = 已连上 broker（收到过 CONNACK 且没断） */
uint8_t MQTT_IsConnected(void);

/* 【上次连云被拒时 broker 给了什么码】见 MQTT_CONNACK_xxx */
uint8_t MQTT_GetConnackCode(void);

/* 【发一条数据到主题】
 * 参数 : topic   —— 主题，如 "data/f407-001"
 *        payload —— 载荷首地址（**可以是二进制**，允许含 0x00）
 *        len     —— 载荷字节数；len=0 表示发空消息
 *        qos     —— 填 0（本模块只做 QoS0）；填 1 只是把标志位置 1，不处理 PUBACK
 *        retain  —— 1 = broker 保留这条，新订阅者一上来就能收到
 * 返回 : MQTT_OK / MQTT_ERR_*
 * 示例 : MQTT_Publish("data/f407-001", buf, n, 0, 0); */
uint8_t MQTT_Publish(const char *topic, const uint8_t *payload, uint16_t len,
                     uint8_t qos, uint8_t retain);

/* 【发一条文本数据】最常用，自动算长度
 * 示例 : MQTT_PublishStr("data/f407-001", "T=25.3,H=60"); */
uint8_t MQTT_PublishStr(const char *topic, const char *text);

/* 【订阅主题】让 broker 把别人发到该主题的消息转发给自己
 * 参数 : topic_filter —— 支持通配符：'+' 一级、"#" 后面全部
 *                        如 "cmd/f407-001"、"cmd/+"、"#"
 *        qos          —— 0（本模块只用 0）
 * 返回 : MQTT_OK / MQTT_ERR_*
 * ⚠ 不阻塞等 SUBACK；订阅成功后消息靠 MQTT_Poll() 取
 * 示例 : MQTT_Subscribe("cmd/f407-001", 0); */
uint8_t MQTT_Subscribe(const char *topic_filter, uint8_t qos);

/* 【取消订阅】参数同 MQTT_Subscribe */
uint8_t MQTT_Unsubscribe(const char *topic_filter);

/* 【发心跳】"在" 一下，防止 broker 以为掉线把自己踢了
 * 返回 : MQTT_OK / MQTT_ERR_*
 * 说明 : 用 MQTT_KeepAliveService() 可以自动发，一般不用手调 */
uint8_t MQTT_Ping(void);

/* 【主动断开】发 DISCONNECT + 关 TCP；会置 IsConnected()=0
 * 说明 : 想重连就再调一次 MQTT_Connect() */
void MQTT_Disconnect(void);


/* ================================================================
 *                        区块 3：扩展功能
 * ================================================================ */

/* 【取一条下行消息】查 ESP8266 收到的数据，解出一条 PUBLISH
 * 参数 : msg —— 输出，见 MqttMsg_t
 * 返回 : MQTT_OK       收到一条，msg 已填好
 *        其它          这一轮没有消息 / 没连上（都不算错，下轮再调）
 * ⚠ 放主循环里周期性调用，别在中断里调
 * ⚠ payload 按文本解析，遇 0x00 截断（见文件头"v1 限制"）
 * 示例 : MqttMsg_t m;
 *        if (MQTT_Poll(&m) == MQTT_OK) { LED_On(LED_DS0); } */
uint8_t MQTT_Poll(MqttMsg_t *msg);

/* 【自动心跳】主循环里调；距上次发送超过 MQTT_KEEPALIVE_S 就自动发 PINGREQ
 * 返回 : MQTT_OK 没到点 / 心跳发成功；其它见 MQTT_ERR_*
 * 说明 : 依赖 DWT 计时，无需额外初始化 */
uint8_t MQTT_KeepAliveService(void);

/* 【上报一个整数】自动转成文本，省得 sprintf
 * 参数 : topic —— 主题；v —— 要上报的数
 * 返回 : MQTT_OK / MQTT_ERR_*
 * 示例 : MQTT_PublishInt("data/f407-001/temp", 253);   -> 发 "253" */
uint8_t MQTT_PublishInt(const char *topic, int32_t v);

/* 【上报一个浮点数】不用 sprintf（省 2KB 代码）
 * 参数 : decimals —— 小数位数（0~3）
 * 返回 : MQTT_OK / MQTT_ERR_*
 * 示例 : MQTT_PublishFloat("data/f407-001/temp", 25.34f, 1); -> 发 "25.3" */
uint8_t MQTT_PublishFloat(const char *topic, float v, uint8_t decimals);

/* 【返回码转中文说明】调试打印用
 * 示例 : printf("[MQTT] %s\r\n", MQTT_ErrStr(MQTT_Connect(...))); */
const char *MQTT_ErrStr(uint8_t err);

#endif /* __FWLIB_MQTT_H */
