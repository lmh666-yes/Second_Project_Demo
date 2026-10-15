#include "esp8266_at.h"

#include <stdio.h>
#include <string.h>

#include "sys_usart.h"
#include "sys_tick.h"

/* ============================================================
 *  内部：AT 命令收发
 *
 *  日志一律用 ASCII：ARMCC v5 对**字符串字面量里的中文**会报 #870-D
 *  （本工程口径是 0 警告），中文只写在注释里。想在串口上看中文就自己改，
 *  改完记得编译器选项要接受多字节字符（本仓库选择不改，保持 0 警告）。
 * ============================================================ */

/* 调试串口（CH340）——上链过程从这看 */
#define ESP_LOG_UART        SYS_USART_1
/* 模块挂的串口——工程 sys_usart.h 标明 USART3(PB10/PB11) 接 ESP8266 */
#define ESP_UART            SYS_USART_3
#define ESP_BAUD            115200UL

static uint8_t s_transparent = 0;

static void esp_log(const char *s)
{
    SYS_USART_SendString(ESP_LOG_UART, s);
}

/* 发一条 AT 命令（自动补 CRLF），并在 timeout_ms 内等 expect 出现。
   返回 1 = 等到了 expect；0 = 超时或模块回了 ERROR/FAIL（两种情况都在日志里写明）。
   注意：进了透传以后就不能再用它发命令了——那时串口是裸数据。 */
static uint8_t at_cmd(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    char line[192];
    uint32_t spent = 0;
    uint8_t got = 0;
    uint8_t failed = 0;

    ESP8266_RxFlush();

    if (cmd != 0) {
        SYS_USART_SendString(ESP_UART, cmd);
        SYS_USART_SendString(ESP_UART, "\r\n");
        esp_log("  >> ");
        esp_log(cmd);
        esp_log("\r\n");
    }

    /* 逐行收：模块每回一行就查一次关键词；超时按"分片超时"累计，别一次等死 */
    while (spent < timeout_ms) {
        const int n = SYS_USART_ReadUntil(ESP_UART, line, (uint16_t)sizeof(line), '\n', 50u);
        spent += 50u;
        if (n < 0)
            continue;                       /* 这一段没收到完整行，继续等 */

        esp_log("  << ");
        esp_log(line);
        esp_log("\r\n");

        if (expect != 0 && strstr(line, expect) != 0) {
            got = 1;
            break;
        }
        if (strstr(line, "ERROR") != 0 || strstr(line, "FAIL") != 0) {
            failed = 1;
            break;
        }
    }

    if (!got && !failed)
        esp_log("  !! timeout, no expected response\r\n");
    return got;
}

/* ============================================================
 *  对外接口
 * ============================================================ */

void ESP8266_Init(void)
{
    /* 中断接收方式：字节自动进环形缓冲，ReadUntil / Read 都从它取 */
    SYS_USART_InitRxIT(ESP_UART, ESP_BAUD);
    s_transparent = 0;
}

uint8_t ESP8266_Transparent(void)
{
    return s_transparent;
}

int ESP8266_Read(uint8_t *buf, uint16_t max)
{
    uint16_t n = 0;

    if (buf == 0 || max == 0u)
        return 0;
    while (n < max) {
        const int c = SYS_USART_RxRead(ESP_UART);
        if (c < 0)
            break;
        buf[n++] = (uint8_t)c;
    }
    return (int)n;
}

void ESP8266_Send(const uint8_t *buf, uint16_t len)
{
    if (buf == 0 || len == 0u)
        return;
    SYS_USART_SendBuf(ESP_UART, buf, len);
    SYS_USART_FlushTx(ESP_UART);
}

void ESP8266_SendString(const char *s)
{
    if (s != 0)
        SYS_USART_SendString(ESP_UART, s);
}

void ESP8266_RxFlush(void)
{
    SYS_USART_RxFlush(ESP_UART);
}

uint8_t ESP8266_ConnectChain(const char *ssid, const char *pass,
                             const char *ip, uint16_t port, uint32_t timeout_ms)
{
    char cmd[160];

    s_transparent = 0;
    esp_log("\r\n[ESP8266] linking up ...\r\n");

    /* 1) 确认模块在（老固件 9600 波特的话这一步就会失败 → 去改 ESP_BAUD） */
    if (!at_cmd("AT", "OK", 2000u)) {
        esp_log("[ESP8266] no reply to AT: check 3.3V / CH_PD / baudrate\r\n");
        return 0;
    }

    /* 2) 关回显：后面每步的响应干净，好判断 */
    (void)at_cmd("ATE0", "OK", 2000u);

    /* 3) Station 模式（要连路由器的 WiFi） */
    if (!at_cmd("AT+CWMODE=1", "OK", 2000u)) {
        esp_log("[ESP8266] set CWMODE failed\r\n");
        return 0;
    }

    /* 4) 连 WiFi：这一步最慢（DHCP 拿 IP），给足时间 */
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", ssid, pass);
    if (!at_cmd(cmd, "OK", 20000u)) {
        esp_log("[ESP8266] join WIFI failed: SSID/PASS? 2.4G only\r\n");
        return 0;
    }

    /* 5) 单连接模式 */
    if (!at_cmd("AT+CIPMUX=0", "OK", 2000u)) {
        esp_log("[ESP8266] CIPMUX=0 failed\r\n");
        return 0;
    }

    /* 6) 连到 PC 上跑的 ota_send.py（TCP server） */
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%u", ip, (unsigned)port);
    if (!at_cmd(cmd, "OK", 15000u)) {
        esp_log("[ESP8266] connect server failed: ota_send.py running? IP/port?\r\n");
        return 0;
    }

    /* 7) 透传模式 */
    if (!at_cmd("AT+CIPMODE=1", "OK", 2000u)) {
        esp_log("[ESP8266] CIPMODE=1 failed (firmware without transparent mode?)\r\n");
        return 0;
    }

    /* 8) 进透传：模块回 '>' 表示"后面我不管了，字节直通" */
    if (!at_cmd("AT+CIPSEND", ">", 5000u))
        esp_log("[ESP8266] no '>' from CIPSEND, transparent NOT entered\r\n");
    else
        s_transparent = 1;

    if (s_transparent)
        esp_log("[ESP8266] transparent now: uart = raw byte pipe, OTA frames go on it\r\n");
    (void)timeout_ms;
    return s_transparent;
}
