/*====================引用定义区=====================*/
/*板2（STM32F407ZGT6）数据通信端*/

#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>


#include "gpio_core.h"
#include "led.h"
#include "beep.h"
#include "key.h"
#include "delay.h"


#include "sys_clock.h"
#include "sys_nvic.h"
#include "sys_fault.h"
#include "sys_usart.h"
#include "sys_frame.h"


//外设函数
#include "lora_e22.h"
#include "esp8266.h"
#include "mqtt.h"
#include "w25qxx.h"
#include "w25qxx_log.h"
#include "lcd.h"
#include "sys_ind.h"


//Free rtos
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
/*=================================================*/


/*====================宏定义与全局声明区================*/
//调试串口：USART1(PA9/PA10)
#define DBG        SYS_USART_1
#define DBG_BAUD   115200U

//WiFi
#define WIFI_SSID    "this"
#define WIFI_PASS    "88888888"

//MQTT服务器：巴法云 bemfa.com:9501，本地 mosquitto 用电脑IP:1883
#define MQTT_HOST    "bemfa.com"
#define MQTT_PORT    9501U

//客户端ID：巴法云填私钥
#define MQTT_CLIENT_ID   "203a752c04b8ae4eb0602fb00ac6d680"

//上报主题：巴法云只填主题名
#define MQTT_TOPIC_UP    "VsqXry2QF004"

//上报格式 #温度#湿度#气压#光照#TVOC#MQ135#节点号
#define NODE_ID      2U


//一条环境帧
typedef struct {
    uint8_t  raw[20];       
    int16_t  temp_x100;
    int16_t  humi_x100;
    int16_t  press_x10;
    uint16_t light;
    uint16_t tvoc;
    uint16_t mq135;
    uint32_t ts;            //收到时刻 hh*10000+mm*100+ss
} EnvMsg_t;

static QueueHandle_t s_q_env;        //收到帧的队列，深度 8
static EnvMsg_t         s_ui_env;    //最近一帧，专门给 LCD 用（不占队列）
static volatile uint8_t s_ui_dirty = 0U;        //环境帧队列，深度 8

static volatile uint8_t  s_fwd_on   = 1U;   //转发开关 0x10
static volatile uint8_t  s_cache_on = 1U;   //断网缓存开关 0x11
static volatile uint8_t  s_replay   = 0U;   //1 = 上位机要求补传

static volatile uint16_t s_rx_ok   = 0U;    //收帧成功
static volatile uint16_t s_rx_bad  = 0U;    //收帧出错/丢弃
static volatile uint16_t s_pub_ok  = 0U;    //发云端成功
static volatile uint16_t s_pub_fail = 0U;   //发云端失败
static volatile uint16_t s_cache_fail = 0U;  //落盘失败次数（原来失败是静默的）
static volatile uint16_t s_replay_sent = 0U;
static volatile uint16_t s_replay_bad  = 0U;   //历史记录长度异常跳过计数

static uint8_t  s_net_ok = 0U;              //1 = MQTT 已连上
static char     s_ip[20] = "0.0.0.0";

static TaskHandle_t s_h_rx, s_h_cmd, s_h_net, s_h_replay, s_h_ui, s_h_mon;
/*=================================================*/


/*====================附加函数=====================*/

//开机时长，编码成 hh*10000+mm*100+ss
//注意：现在还不是真实时间，接了 RTC 再改
static uint32_t now_ts(void)
{
    uint32_t sec = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);

    return ((sec / 3600U) * 10000U)
         + (((sec % 3600U) / 60U) * 100U)
         +  (sec % 60U);
}

//大端写 2 字节
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFU);
}

//大端写 4 字节
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFFU);
}

//发应答帧，cmd 自动带上 0x80
static void send_ack(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    SYS_FRAME_Send(DBG, (uint8_t)(cmd | SYS_FRAME_ACK_FLAG), data, len);
}


//连WiFi + 连MQTT，成功返回 1
static void net_dump_reply(const char *tag)
{
    const char *r = ESP8266_GetLastReply();

    if ((r == 0) || (r[0] == '\0')) {
        SYS_USART_Printf(DBG, "[NET] %s: 模块没有任何回复\r\n", tag);
        return;
    }
    SYS_USART_Printf(DBG, "[NET] %s 原始回复: %s\r\n", tag, r);
}

static uint32_t s_baud = 0U;   /* 自动识别出的模块波特率，0=未识别 */

static uint8_t net_connect(void)
{
    uint8_t rc;

    if (s_baud == 0U) {
        s_baud = ESP8266_AutoBaud();
        if (s_baud == 0U) {
            SYS_USART_Printf(DBG, "[NET] ESP8266 四个波特率全无应答（查 3.3V 供电 / CH_PD / TX-RX 交叉）\r\n");
            /* 环路自检：如果 PB10 与 PB11 短接，这里能读到刚发出去的 AT，说明 MCU 这一路没坏 */
            SYS_USART_InitRxIT(ESP8266_USART, 115200UL);
            delay_ms(100);
            ESP8266_Flush();
            (void)ESP8266_SendCmd("AT", "OK", 500U);
            net_dump_reply("环路回读");
            return 0U;
        }
        SYS_USART_Printf(DBG, "[NET] ESP8266 波特率 = %u\r\n", (unsigned)s_baud);
    }

    rc = ESP8266_Init(s_baud);
    if (rc != 0U) {
        SYS_USART_Printf(DBG, "[NET] ESP8266 初始化失败 rc=%u（复位或 AT 无回应）\r\n", (unsigned)rc);
        net_dump_reply("Init");
        s_baud = 0U;
        return 0U;
    }

    rc = ESP8266_SetMode(ESP8266_MODE_STA);
    if (rc != 0U) {
        SYS_USART_Printf(DBG, "[NET] SetMode 失败 rc=%u\r\n", (unsigned)rc);
        net_dump_reply("CWMODE");
        return 0U;
    }

    /* 一次性扫描周围热点：确认模块能不能看见自家 AP、加密方式是否匹配 */
    {
        uint8_t src = ESP8266_SendCmd("AT+CWLAP", "OK", 9000U);
        SYS_USART_Printf(DBG, "[NET] CWLAP rc=%u\r\n", (unsigned)src);
        net_dump_reply("CWLAP");
    }
    /* 调试：把实际要发出去的 SSID/密码打出来，确认没有乱码或截断 */
    SYS_USART_Printf(DBG, "[NET] SSID=\"%s\" len=%u  PWD len=%u\r\n",
                     WIFI_SSID, (unsigned)strlen(WIFI_SSID), (unsigned)strlen(WIFI_PASS));
    rc = ESP8266_JoinAP(WIFI_SSID, WIFI_PASS);
    if (rc != 0U) {
        SYS_USART_Printf(DBG, "[NET] 连不上 WiFi rc=%u（查 SSID/密码/2.4G 信号）\r\n", (unsigned)rc);
        net_dump_reply("CWJAP");
        /* 再问一句当前关联状态：能分出"其实连上了只是慢"还是"被拒绝" */
        (void)ESP8266_SendCmd("AT+CWJAP?", "OK", 3000U);
        net_dump_reply("CWJAP?");

        return 0U;
    }

    ESP8266_GetIP(s_ip, sizeof(s_ip));
    SYS_USART_Printf(DBG, "[NET] WiFi OK, IP=%s\r\n", s_ip);

    /* 诊断：先看本机 IP，再测 DNS 能不能解析服务器*/
    (void)ESP8266_SendCmd("AT+CIFSR", "OK", 2000U);
    net_dump_reply("CIFSR");
    (void)ESP8266_SendCmd("AT+CIPDOMAIN=\"bemfa.com\"", "OK", 6000U);
    net_dump_reply("DNS");

    rc = MQTT_Connect(MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID, 0, 0);
    if (rc != MQTT_OK) {
        SYS_USART_Printf(DBG, "[NET] MQTT 连接失败 rc=%u connack=%u\r\n",
                         (unsigned)rc, (unsigned)MQTT_GetConnackCode());
        net_dump_reply("MQTT");
        return 0U;
    }

    SYS_USART_Printf(DBG, "[NET] MQTT 已连接 %s:%u 主题 %s\r\n",
                     MQTT_HOST, (unsigned)MQTT_PORT, MQTT_TOPIC_UP);

    return 1U;
}

//上报一条到云端，成功返回 1
//带"长阻塞"标记的联网封装：连 WiFi 期间暂停心跳判定
static uint8_t net_connect_guarded(void)
{
    uint8_t rc;

    SYS_IND_Busy(1U);
    rc = net_connect();
    SYS_IND_Busy(0U);

    return rc;
}

static uint8_t publish_env(const EnvMsg_t *m)
{
    char buf[128];
    int  n;

    n = snprintf(buf, sizeof(buf), "#%d.%02d#%d.%02d#%d.%d#%u#%u#%u#%u",
                 (int)(m->temp_x100 / 100), (int)(m->temp_x100 % 100),
                 (int)(m->humi_x100 / 100), (int)(m->humi_x100 % 100),
                 (int)(m->press_x10 / 10),  (int)(m->press_x10 % 10),
                 (unsigned)m->light, (unsigned)m->tvoc,
                 (unsigned)m->mq135, (unsigned)NODE_ID);

    if (n <= 0) {
        return 0U;
    }

    return (MQTT_PublishStr(MQTT_TOPIC_UP, buf) == MQTT_OK) ? 1U : 0U;
}
/*=================================================*/


/*====================任务=====================*/

//LoRa收帧：板1 每 2 秒来一帧 0x01
static void vTaskLoraRx(void *p)
{
    uint8_t  cmd;
    uint8_t  pl[64];
    uint16_t len;
    uint8_t  n;
    EnvMsg_t m;

    (void)p;

    for (;;) {
        n = SYS_FRAME_Poll(SYS_LORA_UART);

        while (n > 0U) {
            n--;

            if (SYS_FRAME_Get(SYS_LORA_UART, &cmd, pl, sizeof(pl), &len) != 0U) {
                continue;
            }

            if ((cmd == SYS_FRAME_CMD_ENV) && (len >= SYS_FRAME_ENV_LEN)) {
                memset(&m, 0, sizeof(m));

                if (SYS_FRAME_UnpackEnv(pl, len, &m.temp_x100, &m.humi_x100,
                                        &m.press_x10, &m.light, &m.tvoc,
                                        &m.mq135) == SYS_FRAME_OK) {
                    m.ts = now_ts();

                    //存一份给 LCD，队列被网络任务取走后屏幕也能显示
                    s_ui_env   = m;
                    s_ui_dirty = 1U;

                    //重新组一帧存着，补传和落盘都直接用它
                    (void)SYS_FRAME_BuildEnv(m.raw, sizeof(m.raw), m.temp_x100,
                                             m.humi_x100, m.press_x10,
                                             m.light, m.tvoc, m.mq135);
                    //转发一帧给上位机（Qt）
                    if (s_fwd_on != 0U) { SYS_FRAME_Send(DBG, SYS_FRAME_CMD_ENV, pl, len); }

                    s_rx_ok++;

                    if (xQueueSend(s_q_env, &m, pdMS_TO_TICKS(20)) != pdPASS) {
                        s_rx_bad++;      //队列满：板1 发太快或网络任务卡住
                    }
                } else {
                    s_rx_bad++;
                }
            } else if ((cmd & 0x80U) != 0U) {
                //板1 的应答帧（0x80 应答位），原样回传上位机
                SYS_FRAME_Send(DBG, cmd, pl, len);
                SYS_USART_Printf(DBG, "[LoRa] relay ack cmd=0x%02X len=%u\r\n",
                                 (unsigned)cmd, (unsigned)len);
            } else {
                s_rx_bad++;          //不是环境帧，记一笔
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


//上位机下行命令 0x10~0x16
static void vTaskCmd(void *p)
{
    uint8_t  cmd;
    uint8_t  pl[64];
    uint8_t  ack[24];
    uint16_t len;
    uint8_t  n;
    uint16_t cnt;
    uint16_t cap;
    uint16_t head;

    (void)p;

    for (;;) {
        n = SYS_FRAME_Poll(DBG);

        while (n > 0U) {
            n--;

            if (SYS_FRAME_Get(DBG, &cmd, pl, sizeof(pl), &len) != 0U) {
                continue;
            }

            memset(ack, 0, sizeof(ack));
            ack[0] = SYS_FRAME_ST_OK;

            switch (cmd) {
            //0x10 设置转发开关
            case SYS_FRAME_CMD_SET_FWD:
                if (len < 1U) {
                    ack[0] = SYS_FRAME_ST_PARAM;
                } else {
                    s_fwd_on = (pl[0] != 0U) ? 1U : 0U;
                    ack[SYS_FRAME_ACK_VAL_OFF] = s_fwd_on;
                }
                send_ack(cmd, ack, 2U);
                break;

            //0x11 设置断网缓存开关
            case SYS_FRAME_CMD_SET_CACHE:
                if (len < 1U) {
                    ack[0] = SYS_FRAME_ST_PARAM;
                } else {
                    s_cache_on = (pl[0] != 0U) ? 1U : 0U;
                    ack[SYS_FRAME_ACK_VAL_OFF] = s_cache_on;
                }
                send_ack(cmd, ack, 2U);
                break;

            //0x12 查状态，应答 16 字节
            case SYS_FRAME_CMD_QUERY:
                W25QXX_LogStat(&cnt, &cap, &head);
                ack[SYS_FRAME_QA_ST]       = SYS_FRAME_ST_OK;
                ack[SYS_FRAME_QA_FWD]      = s_fwd_on;
                ack[SYS_FRAME_QA_CACHE]    = s_cache_on;
                put_u16(&ack[SYS_FRAME_QA_RXOK], s_rx_ok);
                put_u16(&ack[SYS_FRAME_QA_RXBAD], s_rx_bad);
                put_u16(&ack[SYS_FRAME_QA_CACHECNT], cnt);
                put_u16(&ack[SYS_FRAME_QA_LOST], W25QXX_LogLost());
                put_u32(&ack[SYS_FRAME_QA_UPTIME], now_ts());
                send_ack(cmd, ack, SYS_FRAME_QUERY_ACK_LEN);
                (void)cap;
                (void)head;
                break;

            //0x13 清空 Flash 缓存
            case SYS_FRAME_CMD_CLR_CACHE:
                s_replay = 0U;
                if (W25QXX_LogClear() != W25QXX_LOG_OK) {
                    ack[0] = SYS_FRAME_ST_BUSY;
                }
                ack[SYS_FRAME_ACK_CNT_OFF]     = 0U;
                ack[SYS_FRAME_ACK_CNT_OFF + 1] = 0U;
                send_ack(cmd, ack, 3U);
                break;

            //0x14 重启：先应答再复位
            case SYS_FRAME_CMD_REBOOT:
                send_ack(cmd, ack, 1U);
                vTaskDelay(pdMS_TO_TICKS(50));
                NVIC_SystemReset();
                break;

            //0x16 补传，DATA[0] 0=停 1=开始
            case SYS_FRAME_CMD_REPLAY: {
                uint8_t on = (len >= 1U) ? pl[0] : 1U;

                if (on != 0U) {
                    s_replay_sent = 0U;
                    s_replay = 1U;
                } else {
                    s_replay = 0U;
                }

                ack[0] = (on != 0U) ? SYS_FRAME_REPLAY_ST_REPLAYING
                                    : SYS_FRAME_REPLAY_ST_IDLE;
                put_u16(&ack[2], s_replay_sent);
                put_u16(&ack[4], 0U);
                SYS_FRAME_Send(DBG, SYS_FRAME_REPLAY_ACK_CMD, ack, 4U);
                break;
            }

            //不认识的命令
            default:
                //0x20 以上：板1 专用命令，原样透传，应答由 vTaskLoraRx 回传 Qt
                if (cmd >= 0x20U) {
                    SYS_FRAME_Send(SYS_LORA_UART, cmd, pl, len);
                    SYS_USART_Printf(DBG, "[CMD] fwd->b1 cmd=0x%02X len=%u\r\n",
                                     (unsigned)cmd, (unsigned)len);
                } else {
                    ack[0] = SYS_FRAME_ST_UNSUPPORTED;
                    send_ack(cmd, ack, 1U);
                }
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


//网络与上报
static uint16_t s_net_retry_cnt = 0U;

static uint32_t s_backoff_s = 2U;

void vTaskNet(void *p)
{
    EnvMsg_t m;

    (void)p;

    if (net_connect_guarded() != 0U) {
        s_net_ok = 1U;
        SYS_IND_NetState(1U);
        s_backoff_s = 2U;
    }

    for (;;) {
        if (s_net_retry_cnt < s_backoff_s) { s_net_retry_cnt++; }

        if ((MQTT_IsConnected() == 0U) && (s_net_retry_cnt >= s_backoff_s)) {
            s_net_retry_cnt = 0U;
            if (s_net_ok != 0U) {
                SYS_USART_Printf(DBG, "[NET] 链路断开，开始重连\r\n");
                s_net_ok = 0U;
                SYS_IND_NetState(0U);
            }
            SYS_IND_Busy(1U);
            (void)MQTT_KeepAliveService();
            if (net_connect_guarded() != 0U) {
                s_net_ok = 1U;
                SYS_IND_NetState(1U);
                s_backoff_s = 2U;
                /* 断网期间缓存下来的记录，联网后自动补传，不用上位机点按钮 */
                if (W25QXX_LogCount() > 0U) {
                    SYS_USART_Printf(DBG, "[NET] 有 %u 条缓存，开始自动补传\r\n",
                                     (unsigned)W25QXX_LogCount());
                    s_replay = 1U;
                }
            } else {
                s_backoff_s = (s_backoff_s < 60U) ? (s_backoff_s * 2U) : 60U;
                SYS_USART_Printf(DBG, "[NET] 重连失败，%u 秒后再试\r\n", (unsigned)s_backoff_s);
            }
            SYS_IND_Busy(0U);
        }

        if (xQueueReceive(s_q_env, &m, pdMS_TO_TICKS(1000)) == pdPASS) {
            /* 云端上报只看网络在不在；转发（fwd）只影响串口回传上位机，两者解耦 */
            SYS_IND_Busy(1U);
            if (s_net_ok != 0U) {
                if (publish_env(&m) != 0U) {
                    s_pub_ok++;
                } else {
                    s_pub_fail++;
                    if ((s_cache_on != 0U) && (W25QXX_LogWrite(m.ts, m.raw, sizeof(m.raw)) != W25QXX_LOG_OK)) {
                        s_cache_fail++;
                    }
                }
            } else if (s_cache_on != 0U) {
                /* 断网：落盘等补传，写失败要计数，不能再静默 */
                if (W25QXX_LogWrite(m.ts, m.raw, sizeof(m.raw)) != W25QXX_LOG_OK) {
                    s_cache_fail++;
                }
            }
            SYS_IND_Busy(0U);
        }

        SYS_IND_Busy(1U);
        (void)MQTT_KeepAliveService();
        SYS_IND_Busy(0U);
    }
}


//补传：50ms 一条 0x15 历史帧
static void vTaskReplay(void *p)
{
    uint16_t idx;
    uint16_t total;
    uint32_t ts;
    uint16_t len;
    uint8_t  buf[W25QXX_LOG_PAYLOAD + 1U];
    uint8_t  pay[SYS_FRAME_HISTORY_LEN];
    uint8_t  ack[4];

    (void)p;

    for (;;) {
        if (s_replay == 0U) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        total = W25QXX_LogCount();

        for (idx = 0U; (idx < total) && (s_replay != 0U); idx++) {

            if (W25QXX_LogRead(idx, &ts, buf, &len) == W25QXX_LOG_OK) {

                //长度不对就跳过：读数被截断时 memcpy 会把缓冲区残余当帧发出去
                if (len != (uint16_t)(SYS_FRAME_ENV_LEN + SYS_FRAME_OVERHEAD)) {
                    s_replay_bad++;
                    SYS_USART_Printf(DBG, "[RP] hist len bad %u\r\n", (unsigned)len);
                } else {
                    put_u32(&pay[SYS_FRAME_HISTORY_TS_OFF], ts);
                    memcpy(&pay[SYS_FRAME_HISTORY_FRAME_OFF], buf, len);
                    SYS_FRAME_Send(DBG, SYS_FRAME_CMD_HISTORY, pay, SYS_FRAME_HISTORY_LEN);
                    s_replay_sent++;
                }
            }

            vTaskDelay(pdMS_TO_TICKS(50));      //50ms/条，上位机来得及处理
        }

        if (s_replay != 0U) {
            //整段发完：清缓存 + 主动回一帧完成应答
            uint8_t st = SYS_FRAME_REPLAY_ST_DONE;

            if (W25QXX_LogClear() != W25QXX_LOG_OK) {
                st = SYS_FRAME_REPLAY_ST_ERR;
            }

            memset(ack, 0, sizeof(ack));
            ack[0] = st;
            put_u16(&ack[2], s_replay_sent);
            SYS_FRAME_Send(DBG, SYS_FRAME_REPLAY_ACK_CMD, ack, 4U);

            s_replay = 0U;
        }
    }
}


//显示配色：正常白、超上限红、低于下限蓝
//门限与板1 采集侧一致（温度 x100、湿度 x100、气压 x10、光照 lux、TVOC ppb）
#define UI_COL_NORMAL 0xFFFFU
#define UI_COL_HIGH   0xF800U
#define UI_COL_LOW    0x001FU
#define UI_COL_LABEL  0x07E0U

#define UI_T_HI  2800
#define UI_T_LO  1200
#define UI_H_HI  8800
#define UI_H_LO  3500
#define UI_P_HI  9950
#define UI_L_HI  2000
#define UI_P_LO  9700
#define UI_L_LO    20
#define UI_V_HI  1000

//按门限取颜色：超上限红、低于下限蓝、其余白
static uint16_t ui_color(int32_t v, int32_t lo, int32_t hi)
{
    if ((hi != 0) && (v > hi)) {
        return UI_COL_HIGH;
    }
    if ((lo != 0) && (v < lo)) {
        return UI_COL_LOW;
    }
    return UI_COL_NORMAL;
}

//LCD显示
static void vTaskUi(void *p)
{
    EnvMsg_t m;
    static uint8_t s_ui_first = 0U;
    uint16_t cnt;
    uint16_t cap;
    uint16_t head;

    (void)p;

    LCD_Clear(0x0000);
    LCD_ShowString(10, 10, "Board2 / Node 2", 0xFFFF, 0x0000, 1);
    LCD_ShowString(10, 40, "T:", 0x07E0, 0x0000, 1);
    LCD_ShowString(10, 70, "H:", 0x07E0, 0x0000, 1);
    LCD_ShowString(10, 100, "P:", 0x07E0, 0x0000, 1);
    LCD_ShowString(10, 130, "L:", 0x07E0, 0x0000, 1);
    LCD_ShowString(10, 160, "TV:", 0x07E0, 0x0000, 1);

    for (;;) {
        if (s_ui_dirty != 0U) {
            m = s_ui_env;
            s_ui_dirty = 0U;
            if (s_ui_first == 0U) {
                s_ui_first = 1U;
                SYS_USART_Printf(DBG, "[UI] first frame\r\n");
            }
            //正常白、超上限红、低于下限蓝
            LCD_ShowFixed(60, 40,  m.temp_x100, 2, 3,
                          ui_color(m.temp_x100, UI_T_LO, UI_T_HI), 0x0000, 1);
            LCD_ShowFixed(60, 70,  m.humi_x100, 2, 3,
                          ui_color(m.humi_x100, UI_H_LO, UI_H_HI), 0x0000, 1);
            LCD_ShowFixed(60, 100, m.press_x10, 1, 5,
                          ui_color(m.press_x10, UI_P_LO, UI_P_HI), 0x0000, 1);
            LCD_ShowNum(60, 130, m.light, 5,
                        ui_color((int32_t)m.light, UI_L_LO, UI_L_HI), 0x0000, 1);
            LCD_ShowNum(60, 160, m.tvoc, 5,
                        ui_color((int32_t)m.tvoc, 0, UI_V_HI), 0x0000, 1);
        }

        W25QXX_LogStat(&cnt, &cap, &head);

        LCD_ShowString(10, 200, s_net_ok ? "Net: ONLINE " : "Net: OFFLINE",
                       s_net_ok ? 0x07E0 : 0xF800, 0x0000, 1);
        LCD_ShowString(10, 230, "Cache:", 0xFFFF, 0x0000, 1);
        LCD_ShowNum(90, 230, cnt, 4, 0xFFFF, 0x0000, 1);
        LCD_ShowString(10, 260, "RX ok:", 0xFFFF, 0x0000, 1);
        LCD_ShowNum(90, 260, s_rx_ok, 5, 0xFFFF, 0x0000, 1);
        LCD_ShowString(10, 290, "IP:", 0xFFFF, 0x0000, 1);
        LCD_ShowString(60, 290, s_ip, 0xFFFF, 0x0000, 1);

        LCD_ShowString(160, 260, "bad:", 0xFFFF, 0x0000, 1);
        LCD_ShowNum(220, 260, s_rx_bad, 5, 0xFFFF, 0x0000, 1);
        LCD_ShowString(10, 350, "STATE:", 0xFFFF, 0x0000, 1);
        LCD_ShowString(90, 350,
                       (SYS_IND_State() == SYS_IND_FAULT) ? "FAULT" : "RUN",
                       (SYS_IND_State() == SYS_IND_FAULT) ? 0xF800 : 0x07E0, 0x0000, 1);
        LCD_ShowString(10, 320, "TX ok:", 0xFFFF, 0x0000, 1);
        LCD_ShowNum(90, 320, s_pub_ok, 5, 0xFFFF, 0x0000, 1);
        LCD_ShowString(160, 320, "fail:", 0xFFFF, 0x0000, 1);
        LCD_ShowNum(220, 320, s_pub_fail, 5, 0xFFFF, 0x0000, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


//运行统计，5 秒一次
static void vTaskMonitor(void *p)
{
    uint16_t cnt;
    uint16_t cap;
    uint16_t head;
    uint8_t  tick10 = 0U;

    (void)p;

    for (;;) {
        SYS_IND_Heartbeat();               //每 100ms 喂一次心跳

        if (++tick10 < 50U) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        tick10 = 0U;

        W25QXX_LogStat(&cnt, &cap, &head);

        SYS_USART_Printf(DBG,
            "[MON] rx=%u bad=%u pub=%u fail=%u cfail=%u cache=%u/%u lost=%u net=%u "
            "stack(rx=%u cmd=%u net=%u rp=%u ui=%u)\r\n",
            (unsigned)s_rx_ok, (unsigned)s_rx_bad,
            (unsigned)s_pub_ok, (unsigned)s_pub_fail, (unsigned)s_cache_fail,
            (unsigned)cnt, (unsigned)cap, (unsigned)W25QXX_LogLost(),
            (unsigned)s_net_ok,
            (unsigned)uxTaskGetStackHighWaterMark(s_h_rx),
            (unsigned)uxTaskGetStackHighWaterMark(s_h_cmd),
            (unsigned)uxTaskGetStackHighWaterMark(s_h_net),
            (unsigned)uxTaskGetStackHighWaterMark(s_h_replay),
            (unsigned)uxTaskGetStackHighWaterMark(s_h_ui));
    }
}
/*=================================================*/


/*=============== LCD 启动画面 ===============*/
//启动阶段还没有 RTOS 节拍，用 DWT 忙等自己闪两下
static void boot_blink(void)
{
    LED_On(0);
    delay_ms_dwt(60);
    LED_Off(0);
    delay_ms_dwt(60);
}

//启动步骤显示：step 1~5，进度条 + 文字
static void boot_show(uint8_t step, const char *text)
{
    uint16_t y;

    if (step == 1U) {
        LCD_Clear(0x0000);
        LCD_ShowString(20, 110, "Board2  Gateway", 0x07E0, 0x0000, 1);
        LCD_DrawRect(18, 140, 302, 330, 0x4208);
    }

    y = (uint16_t)(160U + ((uint16_t)step - 1U) * 26U);
    LCD_ShowNum(30, y, step, 1, 0x8410, 0x0000, 1);
    LCD_ShowString(50, y, text, 0xFFFF, 0x0000, 1);
    LCD_ShowString(258, y, "OK", 0x07E0, 0x0000, 1);
    boot_blink();
}

//某一步失败：把该行的 OK 改成红色 ERR
static void boot_fail(uint8_t step)
{
    uint16_t y = (uint16_t)(160U + ((uint16_t)step - 1U) * 26U);
    LCD_ShowString(258, y, "ERR", 0xF800, 0x0000, 1);
}

/*====================main=====================*/
int main(void)
{
    //上了 FreeRTOS 就不能再调 SYS_TICK_Init()，
    //port.c 的 SysTick_Handler 会顶掉 sys_tick.c 的弱版本，
    //调度器没启动时进中断会踩空 pxCurrentTCB，表现为打印半句就停住

    SYS_CLK_ToHighSpeed();              //HSE 8MHz -> HCLK 168MHz
    SYS_NVIC_Init();                    //分组 4，必须在开中断前

    SYS_USART_InitRxIT(DBG, DBG_BAUD);  //带接收中断，下行命令靠它收帧
    SYS_USART_SendString(DBG, "\r\n[board2] boot...\r\n");  //不接屏也能看到走到哪一步
    LED_Init();
    SYS_IND_Init();                    //两灯归位：LED0 心跳、LED1 网络
    KEY_Init();
    BEEP_Init();

    LCD_Init();                         //3.5 寸 ILI9481，FSMC
    LCD_Clear(0x0000);
    LCD_BackLight(1);
    SYS_USART_Printf(DBG, "[board2] LCD %ux%u ok\r\n", LCD_GetWidth(), LCD_GetHeight());

    //屏 ID：0x9481 才是 ILI9481；读到 0 或 FFFFFF 说明 FSMC 总线没通
    SYS_USART_Printf(DBG, "[board2] LCD ID = 0x%08X\r\n", LCD_ReadID());

    //配色图例：白=正常 红=超上限 蓝=低于下限
    LCD_ShowString(10, 400, "WHITE = normal", UI_COL_NORMAL, 0x0000, 1);
    LCD_ShowString(10, 425, "RED   = over high", UI_COL_HIGH, 0x0000, 1);
    LCD_ShowString(10, 450, "BLUE  = under low", UI_COL_LOW, 0x0000, 1);
    boot_show(1U, "CLOCK/NVIC/USART");

    SYS_FAULT_Init();                   //硬件错误时能打现场，别静默停机



    boot_show(2U, "LoRa E22 (UART3)");
    //E22-400T22D 必须 M0=M1=0 才是透传
    if (LORA_E22_Init() != LORA_E22_OK) {
        SYS_USART_SendString(DBG, "[LORA] init fail\r\n");
        boot_fail(2U);
    } else {
        SYS_USART_SendString(DBG, "[LORA] init ok\r\n");
    }

    boot_show(3U, "W25Q128 FLASH");

    //W25Q128：断网缓存和补传都靠它
    if (W25QXX_Init(0U) != 0U) {
        SYS_USART_SendString(DBG, "[FLASH] no chip\r\n");
        boot_fail(3U);
    } else if (W25QXX_LogInit() != W25QXX_LOG_OK) {
        SYS_USART_SendString(DBG, "[FLASH] log init fail\r\n");
        boot_fail(3U);
    } else {
        SYS_USART_Printf(DBG, "[FLASH] ok, cached=%u\r\n",
                         (unsigned)W25QXX_LogCount());
    }


    s_q_env = xQueueCreate(8, sizeof(EnvMsg_t));
    if (s_q_env == 0) {
        SYS_USART_SendString(DBG, "[RTOS] queue create fail\r\n");
        for (;;) { }
    }


    boot_show(4U, "QUEUE + TASKS");
    xTaskCreate(vTaskLoraRx,  "lora",   512, 0, 4, &s_h_rx);
    xTaskCreate(vTaskCmd,     "cmd",    512, 0, 4, &s_h_cmd);
    xTaskCreate(vTaskNet,     "net",   1024, 0, 3, &s_h_net);
    xTaskCreate(vTaskReplay,  "replay", 512, 0, 2, &s_h_replay);
    xTaskCreate(vTaskUi,      "ui",     512, 0, 2, &s_h_ui);
    xTaskCreate(vTaskMonitor, "mon",    512, 0, 1, &s_h_mon);

    boot_show(5U, "START RTOS");
    SYS_IND_EnterRun();                //两灯转为心跳模式

    SYS_USART_SendString(DBG, "[RTOS] start scheduler\r\n");
    vTaskStartScheduler();

    //走到这里说明堆不够，建任务失败
    for (;;) {

        delay_ms(200);
    }
}
/*=================================================*/
