/* ============================================================
 *  main.c —— 板1（F407ZE）OTA 升级示例：**App 侧**入口
 *
 *  本工程（OTA/OTA1）链接地址从 0x08004000 开始（Sector 1~3，48KB），
 *  是被 Bootloader（OTA/OTA1_Boot）搬运/跳转过来运行的"运行区程序"。
 *
 *  App 干的活：
 *    ① 把中断向量表搬到 0x08004000（必须，否则中断还跳去 Boot 的表）
 *    ② 起串口、ESP8266 上链，进透传
 *    ③ OTA 接收任务：收上位机下发的帧 → 先攒 RAM → CRC 通过才写下载区 → 复位
 *
 *  升级流程（配合 OTA/tools/ota_send.py）：
 *    PC 发 BEGIN/DATA/END 帧 → 透传进 USART3 → 本工程收下 → 写下载区 → 复位
 *    → Bootloader 看到有效固件头 → 搬运到 0x08004000 → 跳过去跑新程序。
 *
 *  日志说明：串口上打的都是 ASCII（ARMCC 对字符串里的中文报 #870-D 警告，
 *  本工程要 0 警告），中文说明全部写在注释与本工程的 说明.md 里。
 * ============================================================ */

#include "stm32f4xx.h"

#include "FreeRTOS.h"
#include "task.h"

#include "led.h"
#include "sys_clock.h"
#include "sys_nvic.h"
#include "sys_tick.h"
#include "sys_usart.h"

#include "esp8266_at.h"
#include "esp8266_cfg.h"
#include "ota_app.h"
#include "ota_part.h"

/* ---- 调试口（CH340）：升级过程看这里 ---- */
#define APP_LOG_UART        SYS_USART_1
#define APP_LOG_BAUD        115200UL

/* ============================================================
 *  App 运行在 0x08004000，中断向量表必须一起搬过来
 *  只在这里改 SCB->VTOR，**不要**去改 system_stm32f4xx.c 的 VECT_TAB_OFFSET：
 *  那份文件 Bootloader 也要用，改了对 Boot 是灾难。必须在开中断之前执行。
 * ============================================================ */
static void App_RelocateVectorTable(void)
{
    SCB->VTOR = OTA_APP_ADDR;
}

/* ---------------- 任务 1：ESP8266 链路管理 ---------------- */
static void vTaskEsp(void *pvParameters)
{
    (void)pvParameters;

    for (;;) {
        if (!ESP8266_Transparent()) {
            SYS_USART_SendString(APP_LOG_UART, "\r\n[App] connecting ESP8266 / server ...\r\n");
            if (!ESP8266_ConnectChain(ESP_WIFI_SSID, ESP_WIFI_PASS,
                                      ESP_SERVER_IP, ESP_SERVER_PORT, 30000u)) {
                SYS_USART_SendString(APP_LOG_UART, "[App] link failed, retry in 5s\r\n");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }
        /* 透传态下这里没事干；断线（透传态被清）时会重新上链 */
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ---------------- 任务 2：OTA 收帧 ---------------- */
static void vTaskOta(void *pvParameters)
{
    static uint8_t last_pct = 0u;

    (void)pvParameters;

    for (;;) {
        OTA_App_Poll();

        /* 每涨 10% 往调试口写一行，方便肉眼盯进度（正式产品里换成喂狗 + LED） */
        if ((OTA_App_Progress() / 10u) != (last_pct / 10u)) {
            last_pct = OTA_App_Progress();
            SYS_USART_Printf(APP_LOG_UART, "[App] OTA progress %u%%\r\n", (unsigned)last_pct);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ---------------- 任务 3：心跳灯 ---------------- */
static void vTaskLed(void *pvParameters)
{
    (void)pvParameters;

    for (;;) {
        /* 收到一半时快闪、空闲时慢闪，一眼能看出板子在干嘛 */
        if (OTA_App_State() == OTA_ST_OK && OTA_App_Progress() > 0u) {
            LED_OnTo(0u);
            vTaskDelay(pdMS_TO_TICKS(80));
            LED_OnTo(3u);
            vTaskDelay(pdMS_TO_TICKS(80));
        } else {
            LED_OnTo(0u);
            vTaskDelay(pdMS_TO_TICKS(500));
            LED_OnTo(3u);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

/* ============================================================
 *  入口
 * ============================================================ */
int main(void)
{
    App_RelocateVectorTable();      /* 第一件事，必须在任何中断使能之前 */

    SYS_CLK_ToHighSpeed();          /* 168MHz（启动文件已配好，这里幂等确认一次） */
    SYS_NVIC_Init();                /* 优先级分组 = NVIC_PriorityGroup_4（FreeRTOS 要求） */
    SYS_TICK_Init();                /* 1ms 滴答（FreeRTOS 启动后由内核接管） */

    SYS_USART_Init(APP_LOG_UART, APP_LOG_BAUD);
    SYS_USART_SendString(APP_LOG_UART,
        "\r\n================================\r\n"
        " OTA App (board1 / 0x08004000) boot\r\n"
        "================================\r\n");

    OTA_App_Init();                 /* 清 OTA 状态 + 复位收帧状态机 */
    ESP8266_Init();                 /* USART3(PB10/PB11) 中断接收，接 ESP8266 */

    LED_Init();
    LED_OnTo(3u);

    xTaskCreate(vTaskEsp, "Esp", 512, 0, 3, 0);
    xTaskCreate(vTaskOta, "Ota", 640, 0, 4, 0);   /* 优先级最高：别让 OTA 卡在别人后面 */
    xTaskCreate(vTaskLed, "Led", 256, 0, 1, 0);

    vTaskStartScheduler();          /* 正常情况下不返回 */

    for (;;) { }                    /* 走到这里说明堆不够，调度器没起来 */
}
