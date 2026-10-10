/* ================================================================
 *  板1 检测端 / 板2 数据通信端通用：上板自检与最小闭环联调程序
 *  目标板 STM32F407ZE（板1 GEC-M4）/ STM32F407ZG（板2 普中-天马）
 *  编译 Keil MDK(AC5) + SPL + FreeRTOS；本文件 GBK(936)、LF 换行
 *  自检清单与硬件接线见 Project_1_检测端\检测数据端设计.md
 * ================================================================ */

#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>

#include "gpio_core.h"
#include "led.h"
#include "delay.h"
#include "sys_clock.h"
#include "sys_tick.h"
#include "sys_nvic.h"
#include "sys_usart.h"
#include "sys_wdg.h"
#include "sys_rtc.h"      /* 板1 用 GetDate+GetTime，板2 用单个 SysRtc_t，见 test_rtc() */
#include "lora_e22.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"


/* ---------------- 0. 板号与调试口 ---------------- */
/* 板号：必须由 Keil 工程设置提供（Options for Target -> C/C++ -> Define 加 SYS_TEST_BOARD=1(板1) 或 =2(板2)）。
 * 文件内不写默认值，否则拷进别的工程会静默编成另一块板，上板才发现串口与 LoRa 控制脚不对。
 * 忘加则报下面的 #error。其余 Define 项不动：USE_STDPERIPH_DRIVER、STM32F40_41xxx、HSE_VALUE=8000000。
 * 本地工具\仓库巡检\sync_main.py 以文件里没有该宏定义来判断板号已交给工程设置。 */
#ifndef SYS_TEST_BOARD
  #error "没有定义 SYS_TEST_BOARD：请在 Keil 工程 Options for Target -> C/C++ -> Define 里加 SYS_TEST_BOARD=1 (板1) 或 =2 (板2)"
#elif ((SYS_TEST_BOARD != 1) && (SYS_TEST_BOARD != 2))
  #error "SYS_TEST_BOARD 只能是 1 (板1 检测端) 或 2 (板2 数据通信端)"
#endif

#if (SYS_TEST_BOARD == 1)
  #define BOARD_NAME   "板1 检测端 (Project_1_检测端)"
  #include "sys_dht11.h"
  #include "sys_modbus.h"        /* 借它的 CRC16 组帧 */
  #define HAS_DHT11   1
#else
  #define BOARD_NAME   "板2 数据通信端 (Project_2_数据通信端)"
  #include "w25qxx.h"
  #include "w25qxx_log.h"
  #include "modbus.h"            /* 板2 的 CRC16 在 modbus.h 里 */
  #define HAS_W25QXX  1
#endif

/* 引脚占用表（本工程 main.c 真正用到的脚）：只列本工程 main.c 实际初始化或使用的脚，不是全库清单。
 * 库内声明的全量清单由 本地工具\仓库巡检\pin_audit.py 扫 FWLIB\inc\*.h 的 *_PORT / *_PIN 生成；
 * 运行期实际配置由 gpio_core 的 GPIO_Claim 登记，main() 里 GPIO_ClaimDump() 打印。
 *
 * 板1 STM32F407ZE（GEC-M4）
 *   USART1 PA9/PA10 调试口，板载 CH340C，115200 8N1
 *   USART2 PA2/PA3  RS485 或上位机，板上跳线选 SP3485 / SP3232
 *   USART3 PB10/PB11 LoRa E22；M0/M1/AUX = PB0/PB1/PB2（普通 IO）
 *   DHT11 PG9（单总线，需 4.7k 上拉）；LED0~LED3 = PF9/PF10/PE13/PE14
 *   按键 KEY1~KEY4 = PA0/PE2/PE3/PE4（main.c 未初始化）
 *   固定脚 PH0/PH1 = 8MHz HSE，PC14/PC15 = 32.768kHz LSE，PA13/PA14 = SWD
 *   库内已声明但本工程未初始化：SPI1 与 SPI3 同为 PB3/PB4/PB5，SPI2 = PB13/PB14/PB15，
 *     I2C1 = PB8/PB9，I2C2 = PB10/PB11（与 LoRa 的 USART3 同脚），LCD_BL = PB15，
 *     以太网 PA1/PA2/PC1/PA7/PC4/PC5/PG11/PG13/PG14/PD3，UART4 = PC10/PC11，USART6 = PC6/PC7
 *
 * 板2 STM32F407ZG（普中-天马）
 *   USART1 PA9/PA10 调试口，板载 CH340C，115200 8N1
 *   USART2 PA2/PA3  RS485 或上位机，板上 P6 跳线选 SP3485 / SP3232
 *   W25Q128 走 SPI1：SCK/MISO/MOSI = PB3/PB4/PB5（上电为 JTAG，库内自动关闭），CS = PB14
 *   ESP8266 走 USART3 PB10/PB11；DHT11 PG9；LED0/LED1 = PF9/PF10
 *   LoRa E22 走 USART6 PC6/PC7；M0/M1/AUX = PE5/PE6/PE7
 *   固定脚 PH0/PH1 = 8MHz HSE，PC14/PC15 = 32.768kHz LSE，PA13/PA14 = SWD
 *   板上另有用途不要占用：PA15 = USB_PWR，PA4/PA5 = 模拟跳线 J8 公共端，PA11/PA12 = OTG_FS，PD0/PD1 = 板上 CAN
 *   库内已声明但本工程未初始化：SPI2 = PB13/PB14/PB15（PB14 与 W25QXX_CS 同脚，PB15 与 LCD_BL 同脚），
 *     I2C2 = PB10/PB11（与 ESP8266 的 USART3 同脚），SPI3 = PB3/PB4/PB5（与 SPI1 同脚），
 *     DS18B20 = PG9（与 DHT11 同脚），电机 ULN2003 = PC1/PC4，TB6612_STBY = PG13，RGB5X5_DATA = PC5
 *
 * PA2/PA3 二选一（板1 A4 项）：PA2 既是 USART2_TX 又是以太网 ETH_MDIO。
 * 本工程 main.c 未初始化 USART2，暂不冲突；若同时启用，后初始化者改写引脚模式，另一个静默失效。
 * 该约束由三处提示：sys_eth.h 第 25 行的自述、本表、运行期 GPIO_Claim 冲突打印。
 * 不用编译期断言：SPL 的 GPIOA 是指针、GPIO_Pin_2 含强制转换，AC5 报 #183，预处理器报 #29/#59/#18。
 * 本表由人工维护；改引脚后同步更新本表。 */
#if (SYS_TEST_BOARD == 2)
/* 编译期断言（板2 版）：条件为假则数组长度变负，编译报错。
 * 此处可用是因为 SYS_LORA_UART 与 ESP8266_USART 都是整数宏，比较结果是整型常量表达式；
 * 换成带强制转换或指针的 SPL 宏会报 #183。 */
typedef char PIN_ASSERT_LORA_UART_MUST_NOT_BE_ESP8266_UART[
    (SYS_LORA_UART != ESP8266_USART) ? 1 : -1];
#endif


/* 本程序参与心跳汇总的任务个数：板1 = Sensor/Process/Comm 三个；监控任务不占报到位
 * （见 vTaskMonitor）。
 * 这里不改库里的 SYS_WDG_HEARTBEAT_COUNT，只在本文件记一个数用于模拟报到：
 * 它是 sys_wdg.h 的编译期常量，改动会改变整个库的语义（所有 SYS_WDG_Heartbeat(id) 的合法范围）。 */
#if (SYS_TEST_BOARD == 1)
  #define APP_TASK_COUNT   3
#else
  #error "本目录的 main.c 是板1（检测端）版本；板2 请编译 S_N_sys/Project_2_数据通信端/Project/main.c"
#endif

/* 调试口 : 两板都是 USART1 接板载 CH340C */
#define DBG          SYS_USART_1
#define DBG_BAUD     115200U

/* 看门狗超时 4s：监控任务 1s 一轮，取 4 个周期。DHT11 读取最长 8.4ms 关中断加
 * 2.2s 重试间隔，此值不宜低于 3s。 */
#define APP_WDG_TIMEOUT_MS   4000U


/* ---------------- 1. 打印与统计小工具 ---------------- */
static uint16_t s_pass;
static uint16_t s_fail;

/* 统一走 SYS_USART_Printf，它内部带了 TX 超时（不会卡死） */
#define P(...)   (void)SYS_USART_Printf(DBG, __VA_ARGS__)

static void banner(const char *title)
{
    P("\r\n------------------------------------------------\r\n");
    P("  %s\r\n", title);
    P("------------------------------------------------\r\n");
}

static void check(const char *name, uint8_t ok)
{
    if (ok) { s_pass++; P("  [ OK ] %s\r\n", name); }
    else    { s_fail++; P("  [FAIL] %s\r\n", name); }
}


/* ---------------- 2. 自检 01~05：内核、时基、看门狗 ---------------- */

/* 01 复位原因：读取并解码上次复位来源 */
static void test_reset_cause(void)
{
    uint32_t cause;
    char     txt[128];

    banner("01 复位原因 ");

    cause = SYS_WDG_ResetCause();
    P("  复位标志 = 0x%08X\r\n", (unsigned)cause);

    if (SYS_WDG_ResetCauseDecode(cause, txt, sizeof(txt)) != 0U) {
        P("  解码     = %s\r\n", txt);
    } else {
        P("  (解码函数返回 0，请对照 sys_wdg.h 的 SYS_WDG_RST_* 位定义)\r\n");
    }
    check("复位原因可读 ", (cause != 0xFFFFFFFFUL));
    if ((cause & SYS_WDG_RST_IWDG) != 0U) {
        P("  上次为独立看门狗复位：有任务未按时报到，\r\n");
        P("    若为首次烧录后的第一次上电，先清标志再观察。\r\n");
        SYS_WDG_ClearResetFlags();
    }
}

/* 02 RTC : 备份域有电时时间应该还在 */
static void test_rtc(void)
{
#if (SYS_TEST_BOARD == 1)   /* 板1 用 GetDate+GetTime，需要这几个变量；板2 用单个 SysRtc_t */
    uint8_t  h = 0, m = 0, s = 0;
    uint16_t y = 0;
    uint8_t  mo = 0, d = 0, wd = 0;
#endif
    uint8_t  r;

    banner("02 RTC 与备份域 ");

    r = SYS_RTC_Init();
    /* 0 = 时间还在(备份域有电) / 1 = 首次上电已初始化 / 2 = 失败 */
    P("  SYS_RTC_Init() = %u  (%s)\r\n", r,
      (r == 0U) ? "时间保留 " : ((r == 1U) ? "首次上电已初始化 " : "失败 "));
    check("RTC 初始化 ", (r != 2U));

    if (r != 2U) {
#if (SYS_TEST_BOARD == 1)
        /* 板1：sys_rtc 拆成 GetDate + GetTime 两个函数 */
        SYS_RTC_GetDate(&y, &mo, &d, &wd);
        SYS_RTC_GetTime(&h, &m, &s);
        P("  当前时间 = %04u-%02u-%02u 周%u  %02u:%02u:%02u\r\n",
          y, mo, d, wd, h, m, s);
#else
        /* 板2：sys_rtc 用一个 SysRtc_t 结构体一次读全。
         * 两板 API 不同是历史遗留，main.c 用条件编译抹平差异。 */
        {
            SysRtc_t t;
            SYS_RTC_GetTime(&t);
            P("  当前时间 = %04u-%02u-%02u 周%u  %02u:%02u:%02u\r\n",
              t.year, t.month, t.day, t.weekday,
              t.hour, t.minute, t.second);
        }
#endif
    }
}

/* 03 毫秒时基：验证 T1 修复。修复前 sys_tick.c 的 __weak SysTick_Handler 被
 * FreeRTOS port.c 的强定义顶替，systick_ms 恒 0；RTOS 模式下改走 xTaskGetTickCount()
 * 后 tick 正常增长。 */
static void test_tick(void)
{
    uint32_t t0, t1;

    banner("03 毫秒时基（T1 修复验证） ");

    t0 = SYS_TICK_GetTick();
    delay_ms_dwt(50);                   /* 用 DWT 延时，不依赖 SysTick */
    t1 = SYS_TICK_GetTick();

    P("  T0 = %u ms\r\n", (unsigned)t0);
    P("  T1 = %u ms   (间隔 %u ms，实际等了 50ms)\r\n",
      (unsigned)t1, (unsigned)(t1 - t0));

    check("SYS_TICK_GetTick() 会走 ", (t1 != t0));
    check("SYS_TICK_GetTick() 走得准 ", ((t1 - t0) >= 40U) && ((t1 - t0) <= 60U));

    if (t1 == t0) {
        P("  tick 不增长：检查 FreeRTOSConfig.h 的 configTICK_RATE_HZ，\r\n");
        P("    以及 sys_tick.c 的 SYS_TICK_USE_RTOS 模式判定。\r\n");
    }
}

/* 04 微秒延时 : DWT 周期计数器（与 SysTick 无关，RTOS 下也能用） */
static void test_us_delay(void)
{
    uint32_t c0, c1, us;

    banner("04 微秒级延时（DWT） ");

    c0 = DWT->CYCCNT;
    delay_us(100);
    c1 = DWT->CYCCNT;
    us = (uint32_t)((c1 - c0) / (SystemCoreClock / 1000000U));

    P("  SystemCoreClock = %u Hz\r\n", (unsigned)SystemCoreClock);
    P("  delay_us(100) 实测 ≈ %u us\r\n", (unsigned)us);
    check("DWT 周期计数可用 ", ((us >= 80U) && (us <= 130U)));
    if (SystemCoreClock != 168000000UL) {
        P("  主频不是 168MHz：检查 PLL_M 是否为 8（本板 HSE=8MHz）。\r\n");
    }
}

/* 05 看门狗：验证 W1 修复。修复前 SYS_WDG_HEARTBEAT_COUNT=0 时 HeartbeatPoll()
 * 直接 return 0U 且不喂狗，按 sys_wdg.h 的示例写主循环会反复复位；修复后该情形
 * 退化为直接喂狗。此处不启动看门狗：后面 DHT11/LoRa 自检要十几秒，
 * SYS_WDG_Init 放在启动调度器之前。 */
static void test_watchdog(void)
{
    uint8_t i;

    banner("05 看门狗（W1 修复验证） ");

    P("  SYS_WDG_HEARTBEAT_COUNT = %u  (0 = 库默认，未启用心跳汇总)\r\n",
      (unsigned)SYS_WDG_HEARTBEAT_COUNT);
    P("  本程序用模式甲：%u 个任务各报自己的位，监控任务\r\n",
      (unsigned)APP_TASK_COUNT);
    P("  （优先级最高）跑 HeartbeatPoll()，全员到齐才喂狗。\r\n");

    /* 此处未调 SYS_WDG_Init()；HeartbeatPoll 只做报到计数与喂狗，不访问未初始化硬件。 */
    SYS_WDG_HeartbeatClear();
    P("  一个人都没报到时 HeartbeatPoll() = %u\r\n",
      (unsigned)SYS_WDG_HeartbeatPoll());
    check("模式甲：未到齐不喂狗（汇总生效） ",
          (SYS_WDG_HeartbeatPending() != 0U));

    /* 所有任务位各报到一次，Pending 应归 0 */
    for (i = 0U; i < APP_TASK_COUNT; i++) {
        SYS_WDG_Heartbeat(i);
    }
    P("  %u 个任务位报完后 Pending = 0x%08X\r\n",
      (unsigned)APP_TASK_COUNT, (unsigned)SYS_WDG_HeartbeatPending());
    P("  Pending 仍为全 1 属正常：库的 SYS_WDG_HEARTBEAT_COUNT\r\n");
    P("    仍为 0，心跳汇总未启用，即模式乙。\r\n");
    P("    要启用模式甲：把 sys_wdg.h 的宏改成 %u。\r\n",
      (unsigned)APP_TASK_COUNT);

    /* W1 修复点：COUNT=0 时 HeartbeatPoll 不能是空操作，修复前不喂狗，照文档写会复位循环。 */
    SYS_WDG_HeartbeatClear();
    P("  库默认配置下 HeartbeatPoll() = %u  (修复前恒为 0 且不喂狗)\r\n",
      (unsigned)SYS_WDG_HeartbeatPoll());
    check("库默认配置下 HeartbeatPoll 会喂狗（W1 修复点） ",
          (SYS_WDG_HeartbeatPoll() != 0U));
}


/* ---------------- 3. 自检 06~07：W25QXX（仅板2） ---------------- */
#if HAS_W25QXX
static void test_flash_id(void)
{
    uint32_t id;
    uint8_t  r;
    uint8_t  vendor;

    banner("06 Flash 身份（S4 修复验证） ");

    id     = W25QXX_ReadID();
    vendor = (uint8_t)((id >> 16) & 0xFFUL);

    P("  JEDEC ID = 0x%06X\r\n", (unsigned)(id & 0xFFFFFFUL));
    P("  厂商     = 0x%02X  (%s)\r\n", (unsigned)vendor,
      (vendor == 0xC8U) ? "GigaDevice 兆易创新 " :
      ((vendor == 0xEFU) ? "Winbond 华邦 " : "未知/未识别 "));
    P("  容量字节 = 0x%02X  (0x18 = 128Mbit = 16MB，即 GD25Q128)\r\n",
      (unsigned)(id & 0xFFUL));

    /* S4 修复点：修复前 W25QXX_Init 只认 Winbond 的 0xEF4018，本板 GD25Q128(0xC84018)
 * 会一直返回 1，调用方按 w25qxx.h 示例直接跳过整个断网缓存模块；修复后只看容量字节。 */
    r = W25QXX_Init(0U);
    P("  W25QXX_Init() = %u  (0=成功 1=没器件 2=容量不符)\r\n", r);
    check("W25QXX_Init 接受非 Winbond 的 128Mbit 器件 ", (r == 0U));

    if ((id == 0xFFFFFFFFUL) || (id == 0x00000000UL)) {
        P("  ID 全 0/全 F：Flash 无应答。查 SPI1 接线、片选脚，\r\n");
        P("    以及 SPI1 是否被 NRF24L01 占用（原理图上共用一组脚）。\r\n");
    }
}

/* 07 断网缓存闭环：验证 S1 状态字与 S2 扇区擦除修复。
 * 流程为写入、读回、断电重启后仍可读出。
 * 修复前状态字恒写为 0xFFFFFFFF(EMPTY)，每条记录都判为空槽，LogCount 数出 0 条，
 * 写入报成功而补传永远 0 条。 */
static void test_flash_log(void)
{
    uint8_t  buf[W25QXX_LOG_PAYLOAD + 1];   /* 必须 +1：读函数会在结尾写 0 */
    uint32_t ts = 0;
    uint16_t n = 0, count, cap, head, lost;
    uint8_t  r;
    uint16_t i;

    banner("07 断网缓存日志（S1/S2 修复验证） ");

    r = W25QXX_LogInit();
    P("  W25QXX_LogInit() = %u  (0=OK 2=忘了先 W25QXX_Init)\r\n", r);

    /* W25QXX_LogStat 只有 3 个出参，丢失条数单独调 W25QXX_LogLost */
    W25QXX_LogStat(&count, &cap, &head);
    lost = W25QXX_LogLost();
    P("  上电时已有 %u 条 / 容量 %u 条 / 写指针 %u / 已丢 %u 条\r\n",
      count, cap, head, lost);
    if (lost > 0U) {
        P("  已丢过 %u 条：缓存被绕圈覆盖（断网太久或补传太慢），\r\n",
          lost);
    }
    if (count > 0U) {
        P("  以上 %u 条是上次断电前留下的，掉电不丢数据\r\n", count);
    }

    /* 写 3 条新记录 */
    for (i = 0U; i < 3U; i++) {
        memset(buf, 0, sizeof(buf));
        (void)snprintf((char *)buf, sizeof(buf), "TEST-REC-%u", (unsigned)i);
        r = W25QXX_LogWrite(1000U + (uint32_t)i, buf,
                            (uint16_t)strlen((char *)buf));
        if (r != W25QXX_LOG_OK) {
            P("  第 %u 条写入失败，返回 %u\r\n", (unsigned)i, r);
        }
    }

    count = W25QXX_LogCount();
    P("  写入后 W25QXX_LogCount() = %u\r\n", count);
    /* 修复前这里恒为 0：所有记录都被当成空槽（状态字为 EMPTY） */
    check("写入的记录能被数出来（状态字写成 VALID 了） ", (count > 0U));

    if (count > 0U) {
        memset(buf, 0, sizeof(buf));
        n = 0;
        r = W25QXX_LogRead((uint16_t)(count - 1U), &ts, buf, &n);
        if (r == W25QXX_LOG_OK) {
            P("  读回 index=%u ts=%u len=%u text=\"%s\"\r\n",
              (unsigned)(count - 1U), (unsigned)ts, (unsigned)n, (char *)buf);
            check("读回内容与写入一致 ",
                  (strncmp((char *)buf, "TEST-REC-", 9) == 0));
        } else {
            P("  读取失败，返回 %u  (5=空槽 6=CRC 校验错)\r\n", r);
            check("读回刚写的记录 ", 0U);
        }
    }

    P("\r\n  现在断电再上电，重看这一段：\r\n");
    P("    若上电时 count ≥ 3 且能读出 TEST-REC-2，S1/S2 通过。\r\n");
    P("    若上电时 count 仍为 0，状态字没落盘，按 S1 排查。\r\n");
}
#endif  /* HAS_W25QXX */


/* ---------------- 4. 自检 08：DHT11（仅板1） ---------------- */
#if HAS_DHT11
static void test_dht11(void)
{
    float   t = 0.0f, h = 0.0f;
    uint8_t raw[5];
    int     r = 0;
    uint8_t try_n;
    uint8_t ok = 0U;

    banner("08 DHT11 温湿度（D1 修复验证） ");

    SYS_DHT11_Init(SYS_DHT11_PORT, SYS_DHT11_PIN);
    delay_ms_dwt(1500);          /* 器件上电需稳定约 1s 才能读 */

    /* 最多重试 3 次：单总线对时序敏感，偶发失败正常；连续 3 次失败则基本是接线或时序问题。 */
    for (try_n = 0U; try_n < 3U; try_n++) {
        r = SYS_DHT11_Read(&t, &h);
        SYS_DHT11_GetRaw(raw);
        P("  第 %u 次: 返回 %d  裸字节 %02X %02X %02X %02X %02X\r\n",
          (unsigned)(try_n + 1U), r,
          raw[0], raw[1], raw[2], raw[3], raw[4]);
        if (r == 0) { ok = 1U; break; }
        delay_ms_dwt(2200);      /* DHT11 采样率 1Hz，间隔不足会读到旧值 */
    }

    if (ok) {
        P("  温度 = %.1f ℃   湿度 = %.1f %%RH\r\n", t, h);
        P("  (DHT11 的小数位实际为 0，这是器件特性不是读错)\r\n");
    } else if (r == -1) {
        P("  无响应：查 PG9 接线与器件方向\r\n");
        P("    (网面朝自己：左起 1=VCC 2=DATA 3=NC 4=GND)\r\n");
    } else if (r == -2) {
        P("  校验失败，按裸字节判断：\r\n");
        P("     1) 全 FF：器件未应答（接线/供电/没上拉）\r\n");
        P("     2) 全 00：引脚被拉死或方向设错\r\n");
        P("     3) 个别位不同：时序被打断，确认 SYS_DHT11_LOCK_IRQ=1\r\n");
    }
    /* 修复前必然失败：位相位错一位，校验和恒不成立，返回 -2 */
    check("DHT11 读成功（位相位已修正） ", ok);
}
#endif  /* HAS_DHT11 */


/* ---------------- 5. 自检 09：LoRa E22 ---------------- */
static void test_lora(void)
{
    char    cfg[128];
    uint8_t r;

    banner("09 LoRa E22");

    r = LORA_E22_Init();
    P("  LORA_E22_Init() = %u  (0=OK 1=AUX一直忙 2=没模块)\r\n", r);
    if (r != LORA_E22_OK) {
        P("  排查顺序：\r\n");
        P("     1) 模块是否先接天线再上电（不接天线发射会烧功放）\r\n");
        P("     2) M0/M1 是否接好（悬空会随机进配置模式）\r\n");
        P("     3) 本端与模块波特率是否都是 9600\r\n");
        P("     4) TXD/RXD 是否接反（模块 TXD → MCU RX）\r\n");
        check("LoRa 初始化 ", 0U);
        return;
    }
    check("LoRa 初始化 ", 1U);

    /* 读一次配置：核对地址/网络ID/信道是否与对端一致 */
    memset(cfg, 0, sizeof(cfg));
    r = LORA_E22_ReadConfig(cfg, sizeof(cfg));
    P("  LORA_E22_ReadConfig() = %u\r\n", r);
    if (r == LORA_E22_OK) {
        P("  模块配置 = %s\r\n", cfg);
    }
    P("  两端互通要求网络ID / 信道 / 地址 三项完全相同。\r\n");

    /* AT 连通性：能回 OK 说明切配置模式与串口收发均正常 */
    r = LORA_E22_SendAT("AT");
    P("  AT 返回 = %u  (0=OK 2=无回应 3=模块回ERROR)\r\n", r);
    check("AT 指令可通（切配置模式没问题） ", (r == LORA_E22_OK));

    r = LORA_E22_SetMode(LORA_E22_MODE_TRANSPARENT);
    check("能切回透传模式 ", (r == LORA_E22_OK));
    P("  当前 AUX = %u  (1=模块忙，不能灌数据)\r\n", LORA_E22_IsBusy());
}

#if (SYS_TEST_BOARD == 2)
/* ================================================================
 *          5b. 自检 10：MQTT 通道与判活修复验证（仅板2）
 * mqtt.c 原来只发 PINGREQ、不看 PINGRESP，也没有多久没收到数据算掉线的判定。
 * TCP 半开时（网线拔出、路由器重启、NAT 表被清）本地收不到任何通知，AT 也不报错，
 * MQTT_IsConnected() 恒为 1，而它正是断网转 W25QXX 缓存的判据，判据失效即静默丢数据。
 * 修复后 MQTT_IsAlive() 按距上次收到 broker 任意字节的时长判活，
 * MQTT_KeepAliveService() 负责判死并清零 s_connected。
 * 用法：填好下面 4 个宏后运行，未填会打印 SKIP；连上后拔掉网线等 2 分钟
 * （MQTT_ALIVE_TIMEOUT_S = 120s），KeepAliveService() 应返回 8、IsConnected() 变 0。
 * ================================================================ */
#define SYS_MQTT_TEST     1
#define ST_WIFI_SSID      "wifi-name-here"
#define ST_WIFI_PASS      "wifi-pass-here"
#define ST_MQTT_HOST      "broker.emqx.io"
#define ST_MQTT_CLIENT    "f407-board2-test"

static void test_mqtt(void)
{
    MqttMsg_t m;
    uint16_t  i;
    uint16_t  n_pub  = 0U;
    uint16_t  n_alive = 0U;
    uint8_t   got;
    uint8_t   r;
    uint8_t   hb;

    banner("10 MQTT channel (liveness fix)");

#if (SYS_MQTT_TEST == 0)
    P("  SYS_MQTT_TEST = 0, skipped.\r\n");
    P("  [SKIP] MQTT test not enabled\r\n");
#else
    if (strcmp(ST_WIFI_SSID, "wifi-name-here") == 0) {
        P("  WiFi not filled in. Edit these 4 macros at the top of main.c:\r\n");
        P("    ST_WIFI_SSID / ST_WIFI_PASS / ST_MQTT_HOST / ST_MQTT_CLIENT\r\n");
        P("  Then this section will check:\r\n");
        P("    1) ESP8266 joins the AP and gets an IP\r\n");
        P("    2) TCP + CONNACK to the broker\r\n");
        P("    3) whether IsAlive() really watches for incoming bytes\r\n");
        P("       -- after it runs, UNPLUG THE CABLE and wait 2 minutes:\r\n");
        P("          KeepAliveService() should return 8 (ALIVE_TIMEOUT)\r\n");
        P("          IsConnected() should become 0 -> fall back to W25QXX\r\n");
        P("  [SKIP] MQTT test not configured\r\n");
        return;
    }

    /* ---- 1) ESP8266 ---- */
    if (ESP8266_Init(115200U) != 0U) {
        P("  [FAIL] ESP8266_Init: check power (200mA+ peak) and TXD/RXD\r\n");
        check("ESP8266 init", 0U);
        return;
    }
    check("ESP8266 init", 1U);

    if (ESP8266_SetMode(ESP8266_MODE_STA) != 0U) {
        P("  [FAIL] set STA mode failed\r\n");
        check("ESP8266 set STA", 0U);
        return;
    }
    check("ESP8266 set STA", 1U);

    P("  joining AP, up to 15s...\r\n");
    if (ESP8266_JoinAP(ST_WIFI_SSID, ST_WIFI_PASS) != 0U) {
        P("  [FAIL] cannot join AP: check name/pass/band (2.4G only)\r\n");
        check("ESP8266 join AP", 0U);
        return;
    }
    check("ESP8266 join AP", 1U);

    /* ---- 2) MQTT CONNECT / CONNACK ---- */
    r = MQTT_ConnectSimple(ST_MQTT_HOST, ST_MQTT_CLIENT);
    P("  MQTT_ConnectSimple() = %u (%s)\r\n", r, MQTT_ErrStr(r));
    if (r != MQTT_OK) {
        P("  CONNACK code = %u (0=ok 1=bad proto 2=id rejected 4=bad user/pass)\r\n",
          MQTT_GetConnackCode());
        check("MQTT connect", 0U);
        return;
    }
    check("MQTT connect", 1U);
    check("MQTT_IsConnected() = 1", (MQTT_IsConnected() != 0U));
    check("MQTT_IsAlive() = 1", (MQTT_IsAlive() != 0U));
    P("  note: IsConnected only means *we think* we are online;\r\n");
    P("        IsAlive counts how long since we last HEARD something.\r\n");

    (void)MQTT_Subscribe("cmd/f407-001", 0U);

    /* ---- 3) run 3s of keepalive ---- */
    P("  running 3s of keepalive...\r\n");
    for (i = 0U; i < 150U; i++) {
        if (MQTT_Poll(&m) == MQTT_OK) {
            n_pub++;
            P("  [rx] %s = %s\r\n", m.topic, m.payload);
        }

        hb = MQTT_KeepAliveService();
        if (hb == MQTT_ERR_ALIVE_TIMEOUT) {
            P("  * liveness timeout: link is dead, s_connected cleared\r\n");
            break;
        }
        if (MQTT_IsAlive() != 0U) n_alive++;

        if ((i % 25U) == 0U) {
            (void)MQTT_PublishStr("data/f407-002", "alive-test");
        }
        delay_ms_dwt(20U);
    }

    P("  3s: %u downlink msgs, %u alive probes\r\n", n_pub, n_alive);
    check("still alive after 3s", (MQTT_IsAlive() != 0U));

    got = MQTT_IsConnected();
    P("  for reference: MQTT_IsConnected() = %u\r\n", got);
    check("IsConnected and IsAlive agree (both 1)", (got != 0U));
    P("  Now UNPLUG the cable and wait 2 minutes:\r\n");
    P("     before the fix IsConnected() stayed 1 (silent data loss)\r\n");
    P("     after  the fix KeepAliveService() returns 8, IsConnected() -> 0\r\n");
#endif  /* SYS_MQTT_TEST */
}
#endif  /* SYS_TEST_BOARD == 2 */



/* ================================================================
 *                    6. 组帧（两板共用）
 * 20 字节整帧，协议见《检测数据端设计.md》§5：
 *   [0][1] = 0xAA55 帧头；[2] = 命令 0x01 环境数据上报；[3] = 长度 0x0C，数据段 12 字节；
 *   [4..15] 数据段（大端，高字节在前）；[16][17] = CRC16-MODBUS（低字节在前，
 *   范围 [2..15] 共 14 字节）；[18][19] = 帧尾 0x55AA。
 * 帧尾 0x55AA 与下一帧帧头 0xAA55 会拼成 55 AA AA 55，解析必须用帧头+长度+CRC 三重确认。
 * 气压必须 ×10 而非 ×100：1013.25×100 = 101325 超出 uint16 上限。
 * ================================================================ */
#define FRAME_CMD_ENV     0x01U
#define FRAME_LEN_ENV     0x0CU
#define FRAME_TOTAL       20U
#define FRAME_TEMP_OFF    4U
#define FRAME_HUMI_OFF    6U
#define FRAME_PRESS_OFF   8U
#define FRAME_LIGHT_OFF   10U
#define FRAME_TVOC_OFF    12U
#define FRAME_MQ135_OFF   14U
#define FRAME_CRC_OFF     16U

#if (SYS_TEST_BOARD == 1)   /* 组帧只有板1（发送端）用到；板2 只解析，不组帧 */
static void put_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFU);
}

static uint16_t frame_crc16(const uint8_t *buf, uint16_t len)
{
#if (SYS_TEST_BOARD == 1)
    return SYS_MODBUS_Crc16(buf, len);      /* 板1：sys_modbus.h */
#else
    return MODBUS_CRC16(buf, len);          /* 板2：modbus.h（同名不同文件） */
#endif
}

/* 组一帧；暂时拿不到的项填 0（板1 目前只有 DHT11 能出数） */
static void frame_build(uint8_t *f, float temp_c, float humi_rh,
                        float press_hpa, uint16_t light_lx,
                        uint16_t tvoc_ppb, uint16_t mq135_raw)
{
    uint16_t crc;

    memset(f, 0, FRAME_TOTAL);

    f[0] = 0xAAU;
    f[1] = 0x55U;
    f[2] = FRAME_CMD_ENV;
    f[3] = FRAME_LEN_ENV;

    /* 温度 int16 ×100（负数为补码；-5.00℃ → 0xFE0C） */
    put_u16_be(&f[FRAME_TEMP_OFF], (uint16_t)(int16_t)(temp_c * 100.0f));
    /* 湿度 uint16 ×100 */
    put_u16_be(&f[FRAME_HUMI_OFF], (uint16_t)(humi_rh * 100.0f));
    /* 气压 uint16 ×10；不能 ×100，会超 uint16 上限 */
    put_u16_be(&f[FRAME_PRESS_OFF], (uint16_t)(press_hpa * 10.0f));
    put_u16_be(&f[FRAME_LIGHT_OFF], light_lx);
    put_u16_be(&f[FRAME_TVOC_OFF],  tvoc_ppb);
    put_u16_be(&f[FRAME_MQ135_OFF], mq135_raw);

    /* CRC 范围 = 命令 + 长度 + 数据段（共 14 字节），不含帧头帧尾 */
    crc = frame_crc16(&f[2], 14U);
    f[FRAME_CRC_OFF]      = (uint8_t)(crc & 0xFFU);   /* 低字节在前 */
    f[FRAME_CRC_OFF + 1U] = (uint8_t)(crc >> 8);

    f[18] = 0x55U;
    f[19] = 0xAAU;
}
#endif  /* 组帧段（仅板1） */

/* ---------------- 7. 任务层（两板各一套） ---------------- */
/* 统计量（volatile：任务里写、监控任务里读） */
static volatile uint32_t s_tx_ok, s_tx_fail;
static volatile uint32_t s_rx_ok, s_rx_bad;
static volatile uint32_t s_cache_ok, s_cache_fail;

#if (SYS_TEST_BOARD == 1)
/* ---------------- 板1：采集 → 队列 → 处理 → 发送 ---------------- */
typedef struct {
    float    temp_c;
    float    humi_rh;
    uint8_t  valid;          /* 0 = 本轮没读到（下游据此跳过组帧） */
    uint32_t tick;
} SensorMsg_t;

static QueueHandle_t s_q_sensor;   /* 采集 → 处理 */
static QueueHandle_t s_q_frame;    /* 处理 → 发送（每项 20 字节帧） */

/* 采集任务：每 2s 读一次 DHT11（器件采样率 1Hz，间隔不足会读到旧值） */
static void vTaskSensor(void *pv)
{
    SensorMsg_t m;
    float   t, h;
    int     r;
    uint8_t retry;

    (void)pv;
    P("[任务] SensorTask 启动（优先级 %u）\r\n", (unsigned)uxTaskPriorityGet(NULL));

    for (;;) {
        r = -1;
        for (retry = 0U; retry < 3U; retry++) {   /* 连续 3 次失败才置无效 */
            r = SYS_DHT11_Read(&t, &h);
            if (r == 0) break;
            vTaskDelay(pdMS_TO_TICKS(2200));
        }

        if (r == 0) {
            m.temp_c  = t;
            m.humi_rh = h;
            m.valid   = 1U;
        } else {
            m.temp_c  = 0.0f;
            m.humi_rh = 0.0f;
            m.valid   = 0U;
        }
        m.tick = SYS_TICK_GetTick();

        /* 队列满时丢最旧，不让采集任务阻塞 */
        if (xQueueSend(s_q_sensor, &m, 0) != pdPASS) {
            SensorMsg_t drop;
            (void)xQueueReceive(s_q_sensor, &drop, 0);
            (void)xQueueSend(s_q_sensor, &m, 0);
        }

        LED_Toggle(0);
        SYS_WDG_Heartbeat(0U);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* 处理任务：收到采集结果 → 组帧 → 交给发送任务 */
static void vTaskProcess(void *pv)
{
    SensorMsg_t m;
    uint8_t     f[FRAME_TOTAL];
    int16_t     t100;
    uint16_t    h100;

    (void)pv;
    P("[任务] ProcessTask 启动\r\n");

    for (;;) {
        if (xQueueReceive(s_q_sensor, &m, portMAX_DELAY) == pdPASS) {
            if (m.valid == 0U) {
                P("[处理] 本轮 DHT11 没读到，跳过组帧\r\n");
            } else {
                frame_build(f, m.temp_c, m.humi_rh, 0.0f, 0U, 0U, 0U);

                t100 = (int16_t)((uint16_t)((f[FRAME_TEMP_OFF] << 8) |
                                             f[FRAME_TEMP_OFF + 1U]));
                h100 = (uint16_t)((f[FRAME_HUMI_OFF] << 8) |
                                   f[FRAME_HUMI_OFF + 1U]);

                P("[处理] 组帧 20B: %02X %02X %02X %02X ... | "
                  "T=%d.%02d℃ H=%u.%02u%%RH | CRC=%02X%02X\r\n",
                  f[0], f[1], f[2], f[3],
                  (int)(t100 / 100),
                  (int)((t100 < 0 ? -t100 : t100) % 100),
                  (unsigned)(h100 / 100U), (unsigned)(h100 % 100U),
                  f[FRAME_CRC_OFF + 1U], f[FRAME_CRC_OFF]);

                (void)xQueueSend(s_q_frame, f, 0);
            }
            SYS_WDG_Heartbeat(1U);
        }
    }
}

/* 发送任务：发出前先自检 CRC（对整帧再算一遍应为 0） */
static void vTaskComm(void *pv)
{
    uint8_t f[FRAME_TOTAL];
    uint8_t r;

    (void)pv;
    P("[任务] CommTask 启动\r\n");

    for (;;) {
        if (xQueueReceive(s_q_frame, f, portMAX_DELAY) == pdPASS) {
            /* 自检：CRC 覆盖 [2..17]（命令+长度+数据段+CRC 自身），整段再算一遍应为 0。 */
            if (frame_crc16(&f[2], 16U) == 0U) {
                P("[发送] 帧自检 OK（整段 CRC = 0）\r\n");
            } else {
                P("[发送] 帧自检失败：组帧与 CRC 不自洽\r\n");
            }

            r = LORA_E22_Send(f, FRAME_TOTAL);
            if (r == LORA_E22_OK) {
                s_tx_ok++;
                P("[发送] 已发出第 %u 帧\r\n", (unsigned)s_tx_ok);
            } else {
                s_tx_fail++;
                P("[发送] 失败(返回 %u)，累计失败 %u 次\r\n",
                  (unsigned)r, (unsigned)s_tx_fail);
            }
            LED_Toggle(1);
            SYS_WDG_Heartbeat(2U);
        }
    }
}

#else
/* ---------------- 板2：LoRa 接收 → 解析 → 缓存 / 转发 ---------------- */
static QueueHandle_t s_q_frame;      /* LoRa 收到 → 处理 */

/* 解析一帧：返回 1 = 合法。三重确认：帧头 + 长度 + CRC。 */
static uint8_t frame_parse(const uint8_t *f, uint16_t n,
                           int16_t *temp100, uint16_t *humi100)
{
    if (n != FRAME_TOTAL)                       return 0U;   /* 长度不对 */
    if ((f[0] != 0xAAU) || (f[1] != 0x55U))     return 0U;   /* 帧头不对 */
    if ((f[18] != 0x55U) || (f[19] != 0xAAU))   return 0U;   /* 帧尾不对 */
    if (f[2] != FRAME_CMD_ENV)                  return 0U;   /* 命令不认识 */
    if (f[3] != FRAME_LEN_ENV)                  return 0U;   /* 长度字段不对 */

    /* 第三重确认：对含 CRC 的整段再算一遍必须为 0。
 * 这一条同时挡掉帧尾 0x55AA 与下一帧帧头 0xAA55 错位拼接的情况，错位后 CRC 必不为 0。 */
    if (MODBUS_CRC16(&f[2], 16U) != 0U)         return 0U;

    *temp100 = (int16_t)((uint16_t)((f[FRAME_TEMP_OFF] << 8) |
                                     f[FRAME_TEMP_OFF + 1U]));
    *humi100 = (uint16_t)((f[FRAME_HUMI_OFF] << 8) | f[FRAME_HUMI_OFF + 1U]);
    return 1U;
}

/* 接收任务：周期性轮询 LoRa。E22 透传模式不输出包边界标记，只能靠字节间停顿切帧，
 * 必须每 10~50ms 调用一次 Recv；调用间隔过长会把两帧粘在一起。 */
static void vTaskLoraRx(void *pv)
{
    uint8_t  buf[64];
    uint16_t n;
    uint8_t  r;
    int16_t  t100 = 0;
    uint16_t h100 = 0;

    (void)pv;
    P("[任务] LoraRxTask 启动\r\n");

    for (;;) {
        n = 0;
        r = LORA_E22_Recv(buf, (uint16_t)sizeof(buf), &n);
        if (r == LORA_E22_OK) {
            if (frame_parse(buf, n, &t100, &h100)) {
                s_rx_ok++;
                P("[接收] 第 %u 帧合法: T=%d.%02d℃ H=%u.%02u%%RH\r\n",
                  (unsigned)s_rx_ok,
                  (int)(t100 / 100), (int)((t100 < 0 ? -t100 : t100) % 100),
                  (unsigned)(h100 / 100U), (unsigned)(h100 % 100U));
                (void)xQueueSend(s_q_frame, buf, 0);   /* 满就丢，不阻塞接收 */
            } else {
                s_rx_bad++;
                P("[接收] 第 %u 个坏帧（%u 字节，帧头/长度/CRC 不符），丢弃\r\n",
                  (unsigned)s_rx_bad, (unsigned)n);
            }
        } else if (r == LORA_E22_ERR_OVERFLOW) {
            s_rx_bad++;
            P("[接收] 收到超长数据（%u 字节），整帧丢弃\r\n", (unsigned)n);
        }
        LED_Toggle(0);
        SYS_WDG_Heartbeat(0U);
        vTaskDelay(pdMS_TO_TICKS(20));   /* 20ms << 1s 帧间隔，不会粘帧 */
    }
}

/* 处理任务：数据 → ① 落 Flash（断网缓存）② 转发 Qt（联调阶段先打印） */
static void vTaskForward(void *pv)
{
    uint8_t  f[FRAME_TOTAL];
    uint8_t  r;
    uint8_t  hh, mm, ss;
    uint32_t ts;

    (void)pv;
    P("[任务] ForwardTask 启动\r\n");

    for (;;) {
        if (xQueueReceive(s_q_frame, f, portMAX_DELAY) == pdPASS) {
            SYS_RTC_GetTime(&hh, &mm, &ss);
            ts = ((uint32_t)hh * 10000UL) + ((uint32_t)mm * 100UL) + ss;

            r = W25QXX_LogWrite(ts, f, FRAME_TOTAL);
            if (r == W25QXX_LOG_OK) {
                s_cache_ok++;
            } else {
                s_cache_fail++;
                P("[处理] 落盘失败，返回 %u\r\n", (unsigned)r);
            }

            /* 这里本应 UART2 转发给 Qt 上位机，联调阶段先打印 */
            P("[处理] 已缓存 %u 条 / 失败 %u 条，Flash 里共 %u 条待补传\r\n",
              (unsigned)s_cache_ok, (unsigned)s_cache_fail,
              (unsigned)W25QXX_LogCount());
            SYS_WDG_Heartbeat(1U);
        }
    }
}
#endif  /* SYS_TEST_BOARD */


/* 监控任务（两板共用）：优先级最高(5)，1s 一轮，汇总心跳并打印统计。
 * 心跳汇总模式下任一任务卡死都会导致位不到齐、不喂狗，进而看门狗复位。 */
static void test_after_scheduler(void);   /* 前向声明：MonitorTask 里要调 */

static void vTaskMonitor(void *pv)
{
    (void)pv;
    P("[任务] MonitorTask 启动（优先级 %u）\r\n",
      (unsigned)uxTaskPriorityGet(NULL));

    /* 调度器已运行，在此补做多任务环境才能验的两项：vTaskDelay 是否正常、
 * SYS_TICK_Delay_ms 在 RTOS 下会不会死等。此任务优先级最高，
 * 自检期间其他任务抢不到 CPU，串口输出不会互相穿插。 */
    test_after_scheduler();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        /* 监控任务不占报到位，此处不调用 SYS_WDG_Heartbeat：库里每个参与汇总的
 * 任务占 0~SYS_WDG_HEARTBEAT_COUNT-1 一格，监控任务只调 HeartbeatPoll；监控
 * 自己卡死时没人调 Poll，狗一样喂不上，所以它不需要报到。原先在这里用
 * APP_TASK_COUNT-1 报到会与第 3 个任务撞同一格，使那个任务卡死也不掉位。 */

        if (SYS_WDG_HeartbeatPoll() != 0U) {
            /* 全员到齐，HeartbeatPoll 内部已喂狗 */
        } else {
            P("[监控] 有任务没报到：Pending = 0x%08X\r\n",
              (unsigned)SYS_WDG_HeartbeatPending());
        }

#if (SYS_TEST_BOARD == 1)
        P("[监控] tick=%u  DHT11/队列: 发出 %u 帧 / 失败 %u 次\r\n",
          (unsigned)SYS_TICK_GetTick(),
          (unsigned)s_tx_ok, (unsigned)s_tx_fail);
#else
        P("[监控] tick=%u  LoRa: 收到 %u 帧 / 坏帧 %u 个 | "
          "Flash: 缓存 %u 条 / 失败 %u 条\r\n",
          (unsigned)SYS_TICK_GetTick(),
          (unsigned)s_rx_ok, (unsigned)s_rx_bad,
          (unsigned)s_cache_ok, (unsigned)s_cache_fail);
#endif
        LED_Toggle(3);
    }
}


/* ---------------- 8. 自检 10~11：调度器起来之后 ---------------- */
static void test_after_scheduler(void)
{
    uint32_t t0, t1;

    banner("10 任务与调度器 ");

    /* 调度器已在运行：vTaskDelay 能返回说明 port.c 的 SysTick 中断正常。 */
    t0 = SYS_TICK_GetTick();
    vTaskDelay(pdMS_TO_TICKS(200));
    t1 = SYS_TICK_GetTick();

    P("  vTaskDelay(200ms) 前后 SYS_TICK_GetTick(): %u → %u\r\n",
      (unsigned)t0, (unsigned)t1);
    check("vTaskDelay 正常运行 ", ((t1 - t0) >= 150U));
    check("SYS_TICK 与内核 tick 同步 ", ((t1 - t0) <= 260U));

    /* T1 修复点：修复前 SYS_TICK_Delay_ms 比较的是 systick_ms(恒 0)，(0-0) < ms 永真，会永久死等。 */
    t0 = SYS_TICK_GetTick();
    SYS_TICK_Delay_ms(200U);
    t1 = SYS_TICK_GetTick();
    P("  SYS_TICK_Delay_ms(200) 实测 %u ms\r\n", (unsigned)(t1 - t0));
    check("SYS_TICK_Delay_ms 在 RTOS 下不再死等 ", ((t1 - t0) >= 150U));

    banner("11 闭环联调 ");
#if (SYS_TEST_BOARD == 1)
    P("  接下来每 2 秒：DHT11 → 组 20 字节帧 → LoRa 发出。\r\n");
    P("  板2 那边应该每收到一帧就打印一行 [接收]。\r\n");
#else
    P("  正在监听 LoRa。板1 每 2 秒发一帧，这里应持续打印 [接收]。\r\n");
    P("  每帧都会落进 W25QXX 缓存，可随时断电重启验证掉电不丢。\r\n");
#endif
    P("\r\n  自检汇总: OK=%u  FAIL=%u\r\n", s_pass, s_fail);
    if (s_fail > 0U) {
        P("  有 %u 项失败，按每条后面的排查提示处理。\r\n", s_fail);
    } else {
        P("  全部自检项通过。\r\n");
    }
    P("================================================\r\n\r\n");
}


/* ---------------- 9. main ---------------- */
int main(void)
{
    /* ---------- 阶段一：调度器还没起来，先把自检做完 ---------- */
    /* SYS_NVIC_Init() 必须是第一句：它把 NVIC 优先级分组设为 Group_4。
 * 分组错位会把所有中断的抢占优先级压到低位，越过 FreeRTOS 的 BASEPRI 阈值，
 * 从而撕开内核临界区。 */
    SYS_NVIC_Init();

    SYS_USART_Init(DBG, DBG_BAUD);
    SYS_USART_InitRxIT(DBG, DBG_BAUD);

    delay_ms_dwt(200);              /* 等 CH340C 枚举 + 串口助手打开 */

    P("\r\n\r\n");
    P("################################################\r\n");
    P("#   SPL + FreeRTOS 模板库   上板自检与闭环联调   #\r\n");
    P("#   板号 : %d / %s\r\n", SYS_TEST_BOARD, BOARD_NAME);
    P("#   编译 : %s %s\r\n", __DATE__, __TIME__);
    P("################################################\r\n");

    LED_Init();
    LED_AllOn();
    delay_ms_dwt(200);
    LED_AllOff();

    test_reset_cause();
    test_rtc();
    test_tick();
    test_us_delay();
    test_watchdog();

#if HAS_W25QXX
    test_flash_id();
    test_flash_log();
#endif
#if HAS_DHT11
    test_dht11();
#endif
    test_lora();
#if (SYS_TEST_BOARD == 2)
    test_mqtt();        /* MQTT + liveness (SKIPs if no WiFi) */
#endif

    /* 引脚占用登记表：上面各模块已把自己用到的脚配过一遍，且调度器尚未启动，仍是单线程。
 * 带 taken by N files 标记的行表示同一个脚被两个源文件配过，后配者生效，前者静默失效。
 * 末行应为 "==== target : conflict 0 / lost 0 ===="，不为 0 时按打印的 文件:行号 修改引脚宏。 */
    GPIO_ClaimDump();

    /* ---------- 阶段二：建队列、建任务、启动调度器 ---------- */
    banner("准备启动 FreeRTOS 调度器 ");
    P("  configTOTAL_HEAP_SIZE = %u 字节\r\n", (unsigned)configTOTAL_HEAP_SIZE);

    s_q_frame = xQueueCreate(4, FRAME_TOTAL);
    if (s_q_frame == NULL) {
        P("  [FAIL] 帧队列创建失败（堆不够），停在自检阶段\r\n");
        for (;;) { SYS_WDG_Feed(); LED_Toggle(0); delay_ms_dwt(500); }
    }

#if (SYS_TEST_BOARD == 1)
    s_q_sensor = xQueueCreate(4, sizeof(SensorMsg_t));
    if (s_q_sensor == NULL) {
        P("  [FAIL] 采集队列创建失败（堆不够）\r\n");
        for (;;) { SYS_WDG_Feed(); LED_Toggle(1); delay_ms_dwt(500); }
    }
    P("  队列就绪：采集→处理(4 项) / 处理→发送(4×20B)\r\n");

    (void)xTaskCreate(vTaskSensor,  "Sensor",  512, NULL, 3, NULL);
    (void)xTaskCreate(vTaskProcess, "Process", 512, NULL, 3, NULL);
    (void)xTaskCreate(vTaskComm,    "Comm",    512, NULL, 4, NULL);
#else
    P("  队列就绪：LoRa→处理(4×20B)\r\n");

    (void)xTaskCreate(vTaskLoraRx,  "LoraRx",  512, NULL, 3, NULL);
    (void)xTaskCreate(vTaskForward, "Forward", 512, NULL, 4, NULL);
#endif

    /* 监控任务：优先级最高(5)，1s 一次汇总心跳 + 打印统计 */
    (void)xTaskCreate(vTaskMonitor, "Monitor", 256, NULL, 5, NULL);

    P("  队列 %u 个任务已建，启动调度器……\r\n", (unsigned)(APP_TASK_COUNT));

    /* 看门狗在这里才开：前面 DHT11/LoRa 自检要十几秒，提前开狗会在自检途中复位。 */
    SYS_WDG_Init(APP_WDG_TIMEOUT_MS);
    P("  独立看门狗已启动，超时 %u ms\r\n", (unsigned)APP_WDG_TIMEOUT_MS);

    vTaskStartScheduler();

    /* 正常到不了这里：堆不够时 vTaskStartScheduler 直接返回，并触发 freertos_hooks.c 里的
 * vApplicationMallocFailedHook。 */
    P("\r\n  [FAIL] 调度器没启动，几乎一定是 configTOTAL_HEAP_SIZE 不够\r\n");
    for (;;) { SYS_WDG_Feed(); LED_Toggle(3); delay_ms_dwt(500); }
}
