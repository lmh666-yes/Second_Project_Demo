/* ================================================================
 * 板1（STM32F407ZET6）检测端 -- main.c 骨架
 *
 * 这个文件由你自己从零实现。打开「标准库速查 - 项目引导 - 板1」，
 * 按阶段顺序一段一段填充，每填一段就按 F7 编译一次。
 *
 * 参考实现（旧代码）已备份到：
 *   D:\GitHub_Project\_参考实现备份\20261012_195704\板1_main.c
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

/* 调试串口：USART1（PA9/PA10）-- CH340 -- 上位机 */
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

    SYS_USART_SendString(DBG, "\r\n[board1] skeleton ready\r\n");

    /* ---------- 阶段 3：FreeRTOS 任务与队列 ----------
     * Sensor(pri3,512) 采集 -> s_q_sensor -> Process(pri3,512) 换算判阈 -> s_q_frame
     * -> Comm(pri4,512) 组帧发送；LoraRx(pri3,512) 收下行；Monitor(pri5,256) 喂狗
     * 注意：上 FreeRTOS 后不要调用 SYS_TICK_Init()，时基改用 xTaskGetTickCount()。 */

    /* ---------- 阶段 4：六路传感器采集 ----------
     * SYS_DHT11_Init/Read、SYS_BMP280_Read、SYS_BH1750_ReadLux、
     * SYS_CCS811_Read、板载光敏（ADC3_IN5/PF7）、MQ-135 模拟通道 */

    /* ---------- 阶段 5：换算、滤波与阈值 ----------
     * 统一成 temp_x100 / humi_x100 / press_x10 / light / tvoc / mq135 六个量 */

    /* ---------- 阶段 6：协议组帧与 CRC16 ----------
     * SYS_FRAME_BuildEnv()：AA 55 | CMD 0x01 | LEN 0x0C | 数据 | CRC16-MODBUS | 55 AA */

    /* ---------- 阶段 7：LoRa 发送与下行接收 ----------
     * LORA_E22_Init() / LORA_E22_Send()，USART3，M0=PB0 / M1=PB1 / AUX=PB2 */

    /* ---------- 阶段 8：OLED 本地显示 ----------
     * SYS_OLED_Init(SYS_I2C_1, 0x3C)，PB8=SCL / PB9=SDA */

    /* ---------- 阶段 9：联调与验收 ----------
     * 与板2、上位机跑通环境帧、下行控制与断网补传 */

    for (;;)
    {
        LED_Toggle(0);
        SYS_TICK_Delay_ms(500);
    }
}
