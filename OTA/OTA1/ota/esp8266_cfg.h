#ifndef ESP8266_CFG_H
#define ESP8266_CFG_H

/* ============================================================
 *  esp8266_cfg.h —— 要你自己填的四个值（就这一处）
 *
 *  改完重新编译烧进板子；板子上电后 ESP8266 会按这四项自动上链，
 *  连到 PC 上跑的 OTA/tools/ota_send.py（--tcp-server 模式）。
 *
 *  ⚠ 下面四行**只放 ASCII 占位符**，别在宏里写中文：
 *    ARMCC v5 对字符串字面量里的中文报 #870-D，本工程要求 0 警告。
 *    （这四行会被拼进 AT 命令，都是要发给路由器/服务器的字符串。）
 * ============================================================ */

/* 路由器 WiFi（ESP8266-01S 只支持 2.4G，5G 连不上）
   把下面引号里的内容换成你的 WiFi 名 / 密码；名字必须保持 ASCII：
   中文 WiFi 名在 ARMCC 下会触发 #870-D 警告（要中文名就得自己加编译选项）。 */
#define ESP_WIFI_SSID       "your-wifi-ssid"
#define ESP_WIFI_PASS       "your-wifi-password"

/* 跑 ota_send.py 的那台电脑在局域网里的地址与监听端口
   （cmd 里 ipconfig 查 IPv4；端口随便挑一个没被占用的，要和脚本 --tcp-server 一致） */
#define ESP_SERVER_IP       "192.168.1.100"
#define ESP_SERVER_PORT     8899

#endif /* ESP8266_CFG_H */
