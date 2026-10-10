#ifndef __FWLIB_MQTT_H
#define __FWLIB_MQTT_H

#include "stm32f4xx.h"
#include "esp8266.h"

/* mqtt.h : MQTT 3.1.1 客户端，基于 esp8266.h 的 TCP 通道
 * 依赖 : esp8266.h（TCP）-> sys_usart.h；时间基准 DWT，见 gpio_core.h
 *
 * 接线（普中-天马 F407 开发板）:
 *     P10 跳线选 WIFI -> USART3，模块 TXD->PB11(RX)，模块 RXD->PB10(TX)
 *     ESP8266 发射瞬时电流 200mA 以上，不能用 STM32 的 3.3V 直接供电
 *
 * 连接判据:
 *     MQTT_IsConnected() 只表示本方认为已连上，TCP 半开时仍返回 1
 *     链路是否可用看 MQTT_IsAlive()（依据 = 距上次收到 broker 任何字节的时长）
 *     判死由 MQTT_KeepAliveService() 完成，它负责清零 s_connected
 *
 * 公共测试 broker（匿名）: broker.emqx.io / test.mosquitto.org，端口 1883
 * 阿里云/腾讯云/OneNET/华为云: 平台 user/password 填 MQTT_Connect 后两个参数
 * 主题约定: data/<设备号> 上报，cmd/<设备号> 下发
 *
 * v1 限制:
 *     只实现 QoS0，broker 不重传
 *     下行 payload 按文本解析，遇到 0x00 截断；二进制需扩展 MQTT_Poll
 *     单包上限 = MQTT_TX_BUF_SIZE；下行报文受 ESP8266_RX_BUF_SIZE(512) 限制
 *     不支持 TLS(8883)
 * 移植: 换 broker 改 MQTT_Connect 参数；换串口改 esp8266.h 的 ESP8266_USART
 */


/* 宏定义区：换 broker、缓冲大小、超时只改这里 */
#define MQTT_DEFAULT_PORT       1883U   /* 明文 MQTT 端口 */
#define MQTT_KEEPALIVE_S        60U     /* 心跳周期（秒）；broker 一般 1.5 倍没动静就踢人 */
#define MQTT_TX_BUF_SIZE        256U    /* 上行缓冲：报文头 + 主题 + 载荷 全算在内 */
#define MQTT_TOPIC_MAX_LEN      64U     /* 下行主题最大长度（含 '\0'） */
#define MQTT_PAYLOAD_MAX_LEN    128U    /* 下行载荷最大长度（含 '\0'） */
#define MQTT_CONNACK_TIMEOUT_MS 5000U   /* 等 broker 回 CONNACK 的最长时间 */

/* 判活超时：多久没收到 broker 的任何字节就认为连接已断
 * 取值：默认 = 心跳周期 x 2 = 120 秒。TCP 半开（网线拔出、路由器重启、
 * NAT 表被清）时本地 socket 与 AT 层都不报错，只看 PINGREQ 是否发出会永远认为还连着；
 * broker 的 PINGRESP 正常在毫秒级返回，两个周期没有任何字节即可判死。
 * 调小可更快发现断链，代价是网络抖动时误判。 */
#define MQTT_ALIVE_TIMEOUT_S    (MQTT_KEEPALIVE_S * 2U)

/* 返回码（全库统一：0 成功，非 0 见下） */
#define MQTT_OK                 0U      /* 成功 */
#define MQTT_ERR_PARAM          1U      /* 参数不对（空指针） */
#define MQTT_ERR_WIFI           2U      /* ESP8266 没连上 / TCP 建不起来 */
#define MQTT_ERR_TX             3U      /* 数据没发出去 */
#define MQTT_ERR_CONNACK        4U      /* 连云被拒，具体看 MQTT_GetConnackCode() */
#define MQTT_ERR_TIMEOUT        5U      /* 等回复超时 */
#define MQTT_ERR_NOT_CONNECTED  6U      /* 还没连上 broker 就调了发布/订阅 */
#define MQTT_ERR_TOO_LONG       7U      /* 主题或载荷超出缓冲 */
#define MQTT_ERR_ALIVE_TIMEOUT  8U      /* 判活超时：已判定链路断开并清零 s_connected，
                                         * 应转 W25QXX 缓存并稍后重连 */

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


/* 基础功能 */

/* 连接 broker：内部先建 TCP，再发 MQTT CONNECT 并等 CONNACK
 * 参数 : host      broker 域名或 IP，如 "broker.emqx.io"
 *        port      端口，填 0 用 MQTT_DEFAULT_PORT(1883)
 *        client_id 设备唯一名，同一 broker 上不能重名，建议 "f407-001"
 *        user      用户名，匿名 broker 传 0
 *        pass      密码，匿名 broker 传 0
 * 返回 : MQTT_OK / MQTT_ERR_*
 * 调用前需先 ESP8266_Init + SetMode + JoinAP
 * 协议约束 : MQTT 3.1.1 规定 Password 只能在 User Name 存在时出现。
 * 传了 pass 但 user 传 0 时，密码会被一并丢弃，否则会发出无 User Name 的
 * 非法报文，严格的 broker 会断开或回 CONNACK 0x02。
 * 平台只给一个 token 时，把它当 user 传。 */
uint8_t MQTT_Connect(const char *host, uint16_t port, const char *client_id,
                     const char *user, const char *pass);

/* 连接 broker 的省事版：默认端口 1883 + 匿名登录
 * 返回 : MQTT_OK / MQTT_ERR_* */
uint8_t MQTT_ConnectSimple(const char *host, const char *client_id);

/* 查连接状态：1 = 收到过 CONNACK 且未被本方判定断开
 * 不能当网络是否可用的判据：TCP 半开（网线拔出、路由器重启、NAT 表被清）时
 * 本地收不到任何通知，本函数仍返回 1。
 * 判链路是否活着用 MQTT_IsAlive()。 */
uint8_t MQTT_IsConnected(void);

/* 判活：1 = 链路活着，0 = 已断或本来就没连上
 * 判据 : 距上次收到 broker 的任何字节不超过 MQTT_ALIVE_TIMEOUT_S 秒。
 *        PUBLISH、PINGRESP、SUBACK 都算，能收到东西就说明双向链路通。
 * 返回 0 时 : s_connected 已被清零，重新联网需再调 MQTT_Connect()。
 * 本函数没有副作用，推进判活状态的是 MQTT_KeepAliveService()。 */
uint8_t MQTT_IsAlive(void);

/* 上次连云被拒时 broker 返回的码，见 MQTT_CONNACK_xxx */
uint8_t MQTT_GetConnackCode(void);

/* 查询订阅结果：SUBACK 的返回码，由 MQTT_Poll() 识别并记录
 * 返回 : 0xFF = 还没收到 SUBACK；0x00/0x01/0x02 = 订阅成功，授予 QoS 0/1/2
 *        0x80 = 服务器拒绝订阅（无权限、主题不被允许、主题格式非法）
 * 拒绝原因 : 0x80 既不是错误帧也不是超时，而是服务器明确拒绝该主题。
 * 不查此码时订阅被拒不产生任何错误，表现为连上了、心跳正常但收不到消息。
 * 典型触发场合：云平台要求 topic 带 ProductKey/DeviceName 前缀，
 * 或客户端没有该 topic 的订阅权限（ACL）。 */
uint8_t MQTT_GetSubAck(void);

/* 发布一条消息到主题
 * 参数 : topic   主题，如 "data/f407-001"
 *        payload 载荷首地址，可为二进制，允许含 0x00
 *        len     载荷字节数，len=0 表示空消息
 *        qos     填 0（本模块只做 QoS0）；填 1 只置标志位，不处理 PUBACK
 *        retain  1 = broker 保留这条，新订阅者一上来就能收到
 * 返回 : MQTT_OK / MQTT_ERR_* */
uint8_t MQTT_Publish(const char *topic, const uint8_t *payload, uint16_t len,
                     uint8_t qos, uint8_t retain);

/* 发布文本，长度自动计算
 * 返回 : MQTT_OK / MQTT_ERR_* */
uint8_t MQTT_PublishStr(const char *topic, const char *text);

/* 订阅主题，broker 会把发到该主题的消息转发过来
 * 参数 : topic_filter 支持通配符：'+' 匹配一级，"#" 匹配后全部，
 *                      如 "cmd/f407-001"、"cmd/+"、"#"
 *        qos          0（本模块只用 0）
 * 返回 : MQTT_OK / MQTT_ERR_*
 * 不阻塞等 SUBACK，订阅成功后消息由 MQTT_Poll() 取 */
uint8_t MQTT_Subscribe(const char *topic_filter, uint8_t qos);

/* 取消订阅，参数同 MQTT_Subscribe */
uint8_t MQTT_Unsubscribe(const char *topic_filter);

/* 发心跳，防止 broker 判定掉线主动断开
 * 返回 : MQTT_OK / MQTT_ERR_*
 * 用 MQTT_KeepAliveService() 可自动发送，一般不用手调 */
uint8_t MQTT_Ping(void);

/* 主动断开：发 DISCONNECT 并关闭 TCP，MQTT_IsConnected() 变为 0
 * 重连需再调 MQTT_Connect() */
void MQTT_Disconnect(void);


/* 扩展功能 */

/* 取一条下行消息：处理 ESP8266 收到的数据，解出一条 PUBLISH
 * 参数 : msg 输出，见 MqttMsg_t
 * 返回 : MQTT_OK 收到一条，msg 已填好
 *        其它    本轮没有消息或未连接，都不算错，下轮再调
 * 主循环里周期性调用，不可在中断里调用
 * payload 按文本解析，遇 0x00 截断（见文件头 v1 限制） */
uint8_t MQTT_Poll(MqttMsg_t *msg);

/* 自动心跳 + 判活，主循环里周期调用，建议 1 秒一次
 * 功能 :
 *    1) 距上次发送超过半个心跳周期则补发 PINGREQ。半周期留余量，
 *       按整周期发送在 broker 眼中可能刚好踩线
 *    2) 收到 broker 的任何字节（PINGRESP、PUBLISH、SUBACK）即刷新判活计时
 *    3) 发过心跳后连续 MQTT_ALIVE_TIMEOUT_S 秒无任何回音则判定链路已断，
 *       清零 s_connected 并返回 MQTT_ERR_ALIVE_TIMEOUT
 * 返回 : MQTT_OK                未到点、心跳发送成功或仍在等 PINGRESP
 *        MQTT_ERR_ALIVE_TIMEOUT 链路判定已断，s_connected 已清零
 *        MQTT_ERR_NOT_CONNECTED 未连接
 *        MQTT_ERR_TX            PINGREQ 发送失败（ESP8266 侧失败）
 * 依赖 DWT 计时，无需额外初始化；必须周期性调用才具备判死能力 */
uint8_t MQTT_KeepAliveService(void);

/* 上报整数，自动转成文本，不使用 sprintf
 * 参数 : topic 主题；v 要上报的数
 * 返回 : MQTT_OK / MQTT_ERR_* */
uint8_t MQTT_PublishInt(const char *topic, int32_t v);

/* 上报浮点数，不使用 sprintf（省约 2KB 代码）
 * 参数 : decimals 小数位数，取值 0~3
 * 返回 : MQTT_OK / MQTT_ERR_*；v 为 NaN 时返回 MQTT_ERR_PARAM 且拒发
 * NaN 拒发的原因 : 若把 NaN 当 0 上报，云端会把传感器故障显示成 0 度，
 * 数据看起来正常，比上报失败更难排查；调用方应判返回码并告警。
 * v 为 ±Inf 或超出 int32 可表示范围时钳到 ±20 亿再发。
 * (int32_t)NaN、(int32_t)1e30 在 C 中是未定义行为，
 * ARM 的 VCVT 输出随实现而定。 */
uint8_t MQTT_PublishFloat(const char *topic, float v, uint8_t decimals);

/* 返回码转文本说明，用于调试打印 */
const char *MQTT_ErrStr(uint8_t err);

#endif /* __FWLIB_MQTT_H */
