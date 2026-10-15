/* ================================================================
 * 板2（STM32F407ZGT6）数据通信端 -- main.c 骨架
 *
 * 这个文件由你自己从零实现。打开「标准库速查 - 项目引导 - 板2」，
 * 按阶段顺序一段一段填充，每填一段就按 F7 编译一次。
 *
 * 参考实现（旧代码）已备份到：
 *   D:\GitHub_Project\_参考实现备份\20261012_195704\板2_main.c
 * 遇到不会的 API，用速查工具搜函数名看原型与用法。
 *
 * 文件格式约定：GBK(936) 编码 + LF 换行，不要改成 UTF-8/CRLF。
 * ================================================================ */

#include "stm32f4xx.h"
#include <stdio.h>

#include "gpio_core.h"
#include "led.h"
#include "beep.h"
#include "key.h"
#include "delay.h"
#include "sys_clock.h"
#include "sys_tick.h"
#include "sys_usart.h"
#include "sys_frame.h"
#include "lora_e22.h"
#include "esp8266.h"
#include "lcd.h"

/* 调试串口：USART1（PA9/PA10，板载 CH340C，板2 需插两枚跳线帽） */
#define DBG        SYS_USART_1
#define DBG_BAUD   115200U

int main(void)
{
    /* ---------- 阶段 2：时钟、滴答与基础外设 ---------- */
    SYS_CLK_ToHighSpeed();                 /* HSE 8MHz -> HCLK 168MHz */
    SYS_TICK_Init();                       /* 裸机毫秒滴答；阶段 3 上了 FreeRTOS 就删掉这一行 */
    SYS_USART_Init(DBG, DBG_BAUD);
    LED_Init();
    KEY_Init();
    BEEP_Init();

    /* 板载 3.5 寸 ILI9481 并口屏（FSMC：CS=PG12/NE4，RS=PF12/A6，WR=PD5，RD=PD4，BL=PB15） */
    LCD_Init();
    LCD_Clear(0x0000);
    LCD_BackLight(1);

    SYS_USART_SendString(DBG, "\r\n[board2] skeleton ready\r\n");

    /* ---------- 阶段 3：FreeRTOS 任务与队列 ----------
     * LoraRx 收板1 的环境帧 -> Forward 转发给上位机(USART2) + 落 Flash 缓存
     * CmdRx 收上位机下发指令 -> 转发板1 或应答；Replay 断网补传；Monitor 喂狗
     * 注意：上 FreeRTOS 后不要调用 SYS_TICK_Init()。 */

    /* ---------- 阶段 4：LoRa 接收与转发 ----------
     * LoRa 收包走 SYS_LORA_UART = USART6（M0/M1/AUX 见 lora_e22.h）；
     * 转发给上位机走 USART2（PA2/PA3 -> P6 跳线选 SP3232 -> USB-TTL）；
     * 联网走 ESP8266_USART = USART3 */

    /* ---------- 阶段 5：协议解析与校验 ----------
     * SYS_FRAME_Poll/Get/Feed，CRC16-MODBUS 校验；脏帧计数上报上位机 */

    /* ---------- 阶段 6：Flash 缓存与断网补传 ----------
     * W25QXX_* + w25qxx_log（日志区 0x001000，16 扇区 x 128 槽，槽 32 字节）；
     * 补传命令 0x15 历史帧 / 0x16 启停 / 0x96 进度 */

    /* ---------- 阶段 7：上行到网络 ----------
     * ESP8266_Init/JoinAP/ConnectTCP + MQTT_*（可选） */

    /* ---------- 阶段 8：本地彩屏显示 ----------
     * LCD_ShowString / LCD_ShowFixed / XPT2046 触摸 */

    /* ---------- 阶段 9：联调与验收 ----------
     * 与板1、上位机跑通环境帧、下行控制与补传 */

    for (;;)
    {
        LED_Toggle(0);
        SYS_TICK_Delay_ms(500);
    }
}
