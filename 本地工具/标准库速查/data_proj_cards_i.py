# -*- coding: utf-8 -*-
r"""data_proj_cards_i.py —— ㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）

写作约定与 data_proj_cards.py 完全一致：raw 三引号 + 单反斜杠 n 表示换行、半角引号写 \"、
字段内不出现裸半角双引号（中文引号一律全角）。两条 main.c 是同一份双板共用源，
靠 SYS_TEST_BOARD 宏在编译期切两条完全不同的数据链，本卡把这套双板工程一次讲完。
"""

# ======================================================================
# ㉖ FreeRTOS 双板闭环联调总览
# ======================================================================

FLOW_P26_RTOS_TWObOARD = r"""
/* ===================== ㉖ FreeRTOS 双板闭环联调总览 ===================== */
{m:"流程", t:"flow", n:"㉖ FreeRTOS 双板闭环联调总览（检测端采集 → LoRa → 网关端 → MQTT 云端 / 断网落盘）",
 d:"两块 STM32F407ZE/ZG 各跑一份 FreeRTOS：板1 检测端（DHT11 采集 → 组 20 字节帧 → LoRa E22 发出）、板2 数据通信端（LoRa 收 → 解析校验 → MQTT 上报，连不上就写 W25QXX 断网缓存、恢复后补传）。两板 main.c 是同一份双板共用源，靠 SYS_TEST_BOARD 宏切两条链。启动顺序是铁律：先 SYS_NVIC_Init、再把 01~10 阻塞式自检跑完、最后才建任务 + 开看门狗 + vTaskStartScheduler；自检阶段调度器还没跑，那时用 vTaskDelay、互斥量都是错的（SYS_I2C_Lock 此时是空操作，这是刻意的）。",
 f:[
  ["第 0 步：先认清「这份源码是双板的」","两板 main.c 是同一份共用源：开头 #ifndef SYS_TEST_BOARD 给板1 默认 1、板2 默认 2，两个 Keil 工程的 Define 里都没有注入这个宏，各吃自己文件里的默认值。所以板1 里 (SYS_TEST_BOARD == 2) 的分支、板2 里 (SYS_TEST_BOARD == 1) 的分支全是死代码，压根不会被编进去。","板1 = STM32F407ZE + -O1；板2 = STM32F407ZG + -O3。板1 FWLIB 里没有 w25qxx.c，靠的就是这个宏把整段剪掉，否则链接期就报未定义。"],
  ["铁律：SYS_NVIC_Init 必须是 main 的第一句","它把 NVIC 优先级分组设成 Group_4。分组一旦错位，所有中断的抢占优先级会被压缩到低位，看着合法却会越过 FreeRTOS 的 BASEPRI 阈值，直接撕开内核临界区，这类死机偶发、无规律、极难查。","板1 main.c:1045、板2 main.c:1054 是 int main(void) 的第一句；FreeRTOSConfig.h 里也写明「必须在任何 nnn_Init 和建任务之前」，因为后来的 SYS_nnn_Init 才按分组含义去写 IPR。"],
  ["阶段一：阻塞式自检 01~09（调度器未运行）","按 test_reset_cause → test_rtc → test_tick → test_us_delay → test_watchdog →（板2 才有的 test_flash_id / test_flash_log）→ test_dht11 → test_lora 顺序逐个调，全是阻塞式、直接调驱动，不用队列不用信号量。目的是用最笨的方式验硬件，任何一处 FAIL 都能把范围缩到一个模块。","01~09 的 banner 两板编号一致；06 Flash 身份、07 断网缓存日志只在板2 编译（HAS_W25QXX）。04 微秒延时用 DWT->CYCCNT，delay_us(100) 实测应落在 80~130us。"],
  ["阶段二：板2 独有 10 号 MQTT 判活测试","连上 broker 之后要求「把网线拔了，什么都别动，等 2 分钟」。修复前的 mqtt.c 只发 PINGREQ、从不看 PINGRESP，TCP 半开（拔网线 / 路由器重启 / 运营商清 NAT 表）时本地 socket 收不到任何通知、AT 也不报错，IsConnected() 永远返回 1；而它正是「断网 → 转 W25QXX 缓存」的触发判据，判据不可靠就是数据静默丢失。修复后新增 MQTT_IsAlive()（依据距上次收到 broker 任何字节的时长）与 MQTT_KeepAliveService()（负责判死并把 s_connected 清零）。","验证判据：拔网线 2 分钟后 KeepAliveService() 返回 8（MQTT_ERR_ALIVE_TIMEOUT）、IsConnected() 变 0，于是后续帧自动转 Flash 缓存。MQTT_ALIVE_TIMEOUT_S = 心跳周期 × 2 = 120 秒。"],
  ["阶段三：建队列（堆够不够在这里暴露）","先 s_q_frame = xQueueCreate(4, 20)（板1 再建 s_q_sensor = xQueueCreate(4, sizeof(SensorMsg_t))）。返回 NULL 就打印 FAIL 并停在自检阶段死循环里闪灯，这几乎是 configTOTAL_HEAP_SIZE 不够的唯一症状，heap_4 一共只有 30KB。","队列长度取 4 的道理：下游最坏会阻塞几秒（Flash 擦扇区、MQTT 等应答），上游 2 秒一帧，4 项足够吸收抖动又不会把 RAM 吃掉。xQueueCreate 的第二参单位是字节，所以帧队列传的是 FRAME_TOTAL = 20。"],
  ["阶段四：建任务，栈的单位是 word 不是字节","xTaskCreate 的栈参数单位是 StackType_t（Cortex-M 上 = 4 字节 word）：512 表示 512 words = 2KB，256 表示 1KB。板1 三任务 Sensor/Process/Comm 都是 512 words、优先级 3/3/4；板2 两任务 LoraRx/Forward 是 512 words、优先级 3/4；监控任务 Monitor 是 256 words、优先级 5。","最经典的问题：在任务里写 uint8_t buf[2048] 这种局部大数组，编译能过（RAM 是运行期分配的），一跑就随机 HardFault，因为溢出的是任务栈。自检里的 W25QXX_LogRead 示例就要求 buf[W25QXX_LOG_PAYLOAD + 1]（21 字节），写成 20 会越界写 1 字节。"],
  ["阶段五：先开看门狗，再启动调度器","两板都是 vTaskStartScheduler() 之前才 SYS_WDG_Init(APP_WDG_TIMEOUT_MS = 4000U)。理由写在源码注释里：前面 DHT11 / LoRa 自检要跑十几秒，提前开狗会在自检途中把板子咬复位，现象看起来像「程序根本跑不起来」。","超时 4s 的依据见 sys_wdg.h 的喂狗周期对照表：毫秒级轮询 1s 够，整屏刷新 2s，Flash 扇区擦除（128K 扇区可达约 1s）要 ≥3s，本程序监控任务 1s 一轮，取 4s 留余量。IWDG 用 LSI（约 32kHz），最大超时约 32.8s。"],
  ["阶段六：监控任务统一喂狗，绝不各喂各的","vTaskMonitor 优先级 5（最高）、1s 一轮：先调 test_after_scheduler() 验证调度器节拍，然后自己报到位 SYS_WDG_Heartbeat(APP_TASK_COUNT - 1)，再 SYS_WDG_HeartbeatPoll()，全员到齐才真正喂狗。任何任务卡死 → 位不到齐 → 不喂狗 → 看门狗复位，这才是看门狗真正管用的用法；到处随手喂狗会把死任务掩盖掉。","心跳汇总模式下 SYS_WDG_HEARTBEAT_COUNT 要配成任务数；本程序故意不动库里那个编译期常量，只在自己文件里记 APP_TASK_COUNT（板1 = 3、板2 = 2）模拟报到，因为它是 sys_wdg.h 的常量、改了要重编库。位最多 32 个，sys_wdg.c 有编译期断言 wdg_hb_cnt_check 卡着。"],
  ["阶段七：跑闭环，看四路证据","板1 每 2 秒：DHT11 → 组帧 → LoRa 发出，串口打「发出 / 失败」；板2 每收到一帧打一行 [接收]，坏帧单独计数，每帧都落 W25QXX 缓存。联调时四路证据一起看：板1 的发出计数、板2 的接收计数、板2 的 Flash 缓存条数、MQTT 侧收到的 JSON。","发出 > 收到就是空口丢了（信道 / 天线 / 距离 / 波特率）；收到 = 0 但板1 发出在涨，先查两端 LoRa 的网络 ID、信道、地址三项是否完全相同（透传模式三项不同就永远收不到）。"],
  ["第一次上板怎么走（这套程序实机一次都没跑过）","两板目前只有 0 Error / 0 Warning 的编译结果，一次实机都没跑过。第一步不直接跑闭环：板1 单独上电、只看 01~05；板2 单独上电、看 01~09；各自自检全 OK 了再进下一步。","然后才是单板 LoRa 自发自收（拔掉另一块板，看自己发自己收），再两板同时上电看板2 的 [接收]，再板2 单独验 10 号 MQTT 判活，最后才跑 11 闭环。任何一层没过就别往下走，否则错误会串成一片、无法定位。"],
  ["接线与上电前的三个硬约束","① LoRa 模块必须先接天线再上电，否则发射会烧功放；② 板1 的 LoRa 走 USART3（PB10/PB11，原 ESP8266 座），板2 走 USART6（PC6/PC7，P1 排针）；③ 两板调试口 USART1(PA9/PA10) 都是板载 CH340C，板2 的 USART1 还要插两枚跳线帽（PA9→RXD、PA10→TXD）。","串口助手 115200 打开，上电后先看到的是 banner 和 01~09 的逐条 [ OK ]；DHT11 数据脚在 PG9，网面朝自己左起 1=VCC 2=DATA 3=NC 4=GND。"]
 ],
 a:[
  ["main 第一句","SYS_NVIC_Init();","板1 :1045 / 板2 :1054；设 NVIC 分组为 Group_4，必须早于一切 nnn_Init 与建任务"],
  ["自检顺序","01~09 阻塞跑完 → 建队列 → 建任务 → SYS_WDG_Init(4000) → vTaskStartScheduler()","调度器启动前不能用 vTaskDelay / 互斥量；SYS_I2C_Lock 此时是空操作"],
  ["看门狗超时","APP_WDG_TIMEOUT_MS = 4000U","sys_wdg.h 对照表：Flash 扇区擦除约 1s 要 ≥3s；IWDG 走 LSI 32kHz，最大约 32.8s"],
  ["喂狗方式","监控任务 1s 一轮：自己 Heartbeat + HeartbeatPoll()","全员到齐才喂狗（心跳汇总）；各任务各喂会掩盖死任务"],
  ["队列","xQueueCreate(4, 20) 帧队列；板1 另有 xQueueCreate(4, sizeof(SensorMsg_t))","长度单位字节；堆只有 30KB（heap_4），建失败返回 NULL"],
  ["任务栈","板1 Sensor/Process/Comm = 512 words；板2 LoraRx/Forward = 512 words；Monitor = 256 words","xTaskCreate 栈参数单位是 word（4 字节），512 = 2KB；局部大数组爆栈表现为随机 HardFault"],
  ["任务优先级","板1 3/3/4；板2 3/4；Monitor = 5","数字越大越高；configMAX_PRIORITIES = 8（0~7）。采集最低、通信略高、监控最高"],
  ["两板宏","SYS_TEST_BOARD：板1 = 1（DHT11 + LoRa 发送）、板2 = 2（LoRa 收 + W25QXX 缓存）","Keil 工程 Define 未注入该宏，各吃文件默认值，另一半是死代码"],
  ["编译档位","板1 -O1（STM32F407ZE）/ 板2 -O3（STM32F407ZG）","板2 是 -O3，ISR 与任务共享变量漏写 volatile 会被优化掉、变成死循环"],
  ["LoRa 口","板1 USART3（PB10/PB11）/ 板2 USART6（PC6/PC7）","透传模式不吐包边界，靠字节间停顿切帧，必须 10~50ms 轮询一次；两端网络ID/信道/地址三项必须相同"]
 ],
 e:"/* ============================================================\n *  ㉖ FreeRTOS 双板闭环联调，总览骨架（板1 检测端 / 板2 数据通信端）\n * ------------------------------------------------------------\n *  这份骨架把「两板共用一个 main.c」的写法、启动顺序铁律、\n *  队列与任务的参数、各模块的锁与 ISR 边界、看门狗的心脏跳汇总，\n *  一次串成一份能直接看着改的完整例子。\n *\n *  两个 Keil 工程的 <Define> 都只写了\n *      USE_STDPERIPH_DRIVER, STM32F40_41xxx, HSE_VALUE=8000000\n *  没有注入 SYS_TEST_BOARD，所以两板各吃下面这个文件里的默认值：\n *      板1（STM32F407ZE, -O1）用 1，DHT11 采集 + LoRa 发送\n *      板2（STM32F407ZG, -O3）用 2，LoRa 接收 + W25QXX 断网缓存\n *  因此板1 里 (SYS_TEST_BOARD == 2) 的分支、板2 里 == 1 的分支\n *  都是死代码，根本不会被编译进去；板1 的 FWLIB 里没有 w25qxx.c\n *  也正是靠这个宏把整段剪掉才链得过。\n * ============================================================ */

#ifndef SYS_TEST_BOARD
#define SYS_TEST_BOARD      1       /* 板1 检测端；板2 的 main.c 这里写 2 */
#endif

#include \"stm32f4xx.h\"
#include \"stdio.h\"
#include \"string.h\"
#include \"gpio_core.h\"
#include \"led.h\"
#include \"delay.h\"
#include \"sys_clock.h\"
#include \"sys_tick.h\"
#include \"sys_nvic.h\"
#include \"sys_usart.h\"
#include \"sys_wdg.h\"
#include \"sys_rtc.h\"
#include \"lora_e22.h\"
#include \"esp8266.h\"
#include \"mqtt.h\"
#include \"sys_i2c.h\"
#include \"FreeRTOS.h\"
#include \"task.h\"
#include \"queue.h\"

#if (SYS_TEST_BOARD == 1)
    #define BOARD_NAME          \"板1 检测端 (Project_1_检测端)\"
    #include \"sys_dht11.h\"
    #include \"sys_modbus.h\"      /* 板1 的 CRC16 叫 SYS_MODBUS_Crc16 */
    #define HAS_DHT11           1
    #define APP_TASK_COUNT      3   /* Sensor / Process / Comm */
#else
    #define BOARD_NAME          \"板2 数据通信端 (Project_2_数据通信端)\"
    #include \"w25qxx.h\"
    #include \"w25qxx_log.h\"
    #include \"modbus.h\"          /* 板2 的 CRC16 叫 MODBUS_CRC16 */
    #define HAS_W25QXX          1
    #define APP_TASK_COUNT      2   /* LoraRx / Forward */
#endif

/* 调试口：两板都是 USART1(PA9/PA10) 走板载 CH340C；板2 还要插两枚跳线帽 */
#define DBG                 SYS_USART_1
#define DBG_BAUD            115200U

/* 看门狗超时：必须大于最慢任务的报到周期。
 * 监控任务 1s 一轮，取 4s 留够余量。
 * sys_wdg.h 的喂狗周期对照表：毫秒级轮询 1s 够用；整屏刷新 2s；
 * Flash 扇区擦除（128K 扇区可达约 1s）必须 ≥3s。 */
#define APP_WDG_TIMEOUT_MS  4000U

/* ---------------- 一、四个必须共享的全局量 ---------------- */

/* 队列句柄：采集任务写、处理任务读，靠队列本身同步，不用额外加锁 */
static QueueHandle_t s_q_sensor;        /* 板1 才有：DHT11 → 组帧 */
static QueueHandle_t s_q_frame;         /* 两板都有：20 字节整帧的中转站 */

/* ISR 与任务共享的计数：两板都是 -O1/-O3 优化，
 *   漏写 volatile 的后果不一样但都错：
 *     板2 是 -O3，编译器会把「循环里读一次就不再变」的变量
 *     提到循环外，ISR 改了任务也看不见，循环永不退出；
 *     板1 是 -O1，多半还能凑合跑，但换编译器 / 换优化档就炸。
 *   凡是「中断里写、任务里读」的变量，一律 volatile。 */
static volatile uint32_t s_tx_ok   = 0U;
static volatile uint32_t s_tx_fail = 0U;
static volatile uint32_t s_rx_ok   = 0U;
static volatile uint32_t s_rx_bad  = 0U;

/* ---------------- 二、20 字节环境数据帧（两板同一套协议） ---------------- */
/*
 *  [0][1]      0xAA 0x55   帧头（双字节，固定）
 *  [2]         0x01        命令：环境数据上报
 *  [3]         0x0C        数据段长度 = 12 字节
 *  [4..15]     数据段      六个字段，一律大端（高字节在前）
 *  [16][17]    CRC16       低字节在前，范围 = [2..15] 共 14 字节
 *  [18][19]    0x55 0xAA   帧尾（与帧头恰好相反）
 *
 * 帧尾 55 AA 和下一帧帧头 AA 55 首尾相连，线上是 ...55 AA AA 55...
 *   接缝处凭空多出一段 AA AA；一旦接收方从字节流中间接入、或路上丢了
 *   一个字节，「AA 55」这种两字节巧合还会大量出现在数据段里。
 *   所以解析必须三重确认：帧头 + 长度自洽 + CRC 完全相等。
 * 气压只能 ×10 不能 ×100：1013.25×100 = 101325 已经超 uint16。
 */
#define FRAME_CMD_ENV       0x01U
#define FRAME_LEN_ENV       0x0CU
#define FRAME_TOTAL         20U
#define FRAME_TEMP_OFF      4U
#define FRAME_HUMI_OFF      6U
#define FRAME_PRESS_OFF     8U
#define FRAME_LIGHT_OFF     10U
#define FRAME_TVOC_OFF      12U
#define FRAME_MQ135_OFF     14U
#define FRAME_CRC_OFF       16U

static void put_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFFU);
    p[1] = (uint8_t)(v & 0xFFU);
}

/* CRC 范围 = CMD + LEN + DATA，既不含帧头，也不含 CRC 自己与帧尾。
 * 附带性质：把 CRC 两字节也一起算进去结果为 0，
 * 所以「整帧重算 = 0」可以直接当校验通过的判据。
 * 两板的 CRC16 名字不同：板1 = SYS_MODBUS_Crc16，板2 = MODBUS_CRC16。 */
static uint16_t frame_crc16(const uint8_t *buf, uint16_t len)
{
#if (SYS_TEST_BOARD == 1)
    return SYS_MODBUS_Crc16(buf, len);
#else
    return MODBUS_CRC16(buf, len);
#endif
}

/* 组帧：DHT11 的浮点值统一放大成整型再大端填入 */
static void frame_build(uint8_t *f, float temp_c, float humi_rh,
                        float press_hpa, uint16_t light_lx,
                        uint16_t tvoc_ppb, uint16_t mq135_raw)
{
    uint16_t crc;

    memset(f, 0, FRAME_TOTAL);
    f[0] = 0xAAU;                       /* 帧头第 1 字节 */
    f[1] = 0x55U;                       /* 帧头第 2 字节 */
    f[2] = FRAME_CMD_ENV;
    f[3] = FRAME_LEN_ENV;

    put_u16_be(&f[FRAME_TEMP_OFF],  (uint16_t)(int16_t)(temp_c  * 100.0f));
    put_u16_be(&f[FRAME_HUMI_OFF],  (uint16_t)(humi_rh * 100.0f));
    put_u16_be(&f[FRAME_PRESS_OFF], (uint16_t)(press_hpa * 10.0f));
    put_u16_be(&f[FRAME_LIGHT_OFF], light_lx);
    put_u16_be(&f[FRAME_TVOC_OFF],  tvoc_ppb);
    put_u16_be(&f[FRAME_MQ135_OFF], mq135_raw);

    crc = frame_crc16(&f[2], 14U);      /* CMD + LEN + 12 字节数据段 */
    f[FRAME_CRC_OFF]      = (uint8_t)(crc & 0xFFU);   /* 低字节在前 */
    f[FRAME_CRC_OFF + 1U] = (uint8_t)((crc >> 8) & 0xFFU);
    f[18] = 0x55U;                      /* 帧尾第 1 字节 */
    f[19] = 0xAAU;                      /* 帧尾第 2 字节 */
}

/* 解帧：三重确认，任一不成立就当没收到这一帧 */
static uint8_t frame_parse(const uint8_t *f, uint16_t n,
                           int16_t *temp100, uint16_t *humi100)
{
    if (n != FRAME_TOTAL)                     return 0U;  /* 长度自洽 */
    if ((f[0] != 0xAAU) || (f[1] != 0x55U))   return 0U;  /* 帧头 */
    if ((f[18] != 0x55U) || (f[19] != 0xAAU)) return 0U;  /* 帧尾 */
    if (f[2] != FRAME_CMD_ENV)                return 0U;
    if (f[3] != FRAME_LEN_ENV)                return 0U;
    if (MODBUS_CRC16(&f[2], 16U) != 0U)       return 0U;  /* 整段重算 = 0 */

    *temp100 = (int16_t)(((uint16_t)f[FRAME_TEMP_OFF] << 8) |
                          (uint16_t)f[FRAME_TEMP_OFF + 1U]);
    *humi100 = (uint16_t)(((uint16_t)f[FRAME_HUMI_OFF] << 8) |
                           (uint16_t)f[FRAME_HUMI_OFF + 1U]);
    return 1U;
}

/* ---------------- 三、共享资源的锁：I2C 与 SPI1 ---------------- */
/*
 * I2C1 上挂了 OLED / MPU6050 / 24C02 三样东西，共用一条总线。
 * 谁都没加锁时会出现「OLED 刷一屏刷到一半，另一个任务把 MPU6050 的
 * 起始条件插进去」，现象是偶发花屏或某次读回全 0。库给了
 * SYS_I2C_Lock(bus) / SYS_I2C_Unlock(bus)，内部是递归互斥量，
 * 并且有五个守卫：总线号越界直接返回；__get_IPSR() != 0 时返回
 * （中断上下文里不能取会阻塞的互斥量）；调度器没跑起来时返回
 * （上电初始化阶段是单线程，加锁是空操作，这是刻意的不是漏加）；
 * 惰性建锁用临界区做 double-check；建锁失败也不卡死。
 * 刷整屏这种「要多次事务原子」的场景，就是递归锁的典型用法：
 *   SYS_I2C_Lock() → 多次读写 → SYS_I2C_Unlock()。
 * SPI1 上挂 W25QXX，而 sys_spi.c 里没有任何互斥，总线互斥得
 * 应用层自己做：谁用谁 SYS_SPI_* 之前先拿同一把锁。
 */
static void i2c_oled_full_refresh(void)
{
    SYS_I2C_Lock(SYS_I2C_1);
    /* 这里整段刷屏都不会被别的任务插进总线 */
    SYS_I2C_Unlock(SYS_I2C_1);
}

/* ---------------- 四、板1：三个任务 ---------------- */

#if (SYS_TEST_BOARD == 1)

typedef struct {
    float    temp_c;
    float    humi_rh;
    uint8_t  valid;     /* 0 = 本轮没读到，组帧任务直接跳过 */
    uint32_t tick;
} SensorMsg_t;

/* 采集任务：最慢、优先级最低。
 * DHT11 一次读要阻塞约 5ms（读期间还关中断），失败要等 2.2s 再试，
 * 连续 3 次失败才认输，这种任务绝不能和通信挤在一个任务里。 */
static void vTaskSensor(void *arg)
{
    SensorMsg_t m;
    int8_t      t = 0;
    uint8_t     h = 0;
    uint8_t     ok = 0U;
    uint8_t     i;

    (void)arg;
    SYS_DHT11_Init(SYS_DHT11_PORT, SYS_DHT11_PIN);

    for (;;) {
        for (i = 0U; i < 3U; i++) {
            if (SYS_DHT11_Read(&t, &h) == 0) { ok = 1U; break; }
            vTaskDelay(pdMS_TO_TICKS(2200));   /* DHT11 采样率只有 1Hz */
        }

        m.temp_c = (float)t;
        m.humi_rh = (float)h;
        m.valid  = ok;
        m.tick   = SYS_TICK_GetTick();

        /* 队列满时丢最旧的一帧，绝不让采集任务阻塞在发送上。
         *   宁可少一帧，也绝不能让采集停摆，这是实时性的底线。 */
        if (xQueueSend(s_q_sensor, &m, 0) != pdPASS) {
            SensorMsg_t old;
            (void)xQueueReceive(s_q_sensor, &old, 0);
            (void)xQueueSend(s_q_sensor, &m, 0);
        }

        LED_Toggle(0);
        SYS_WDG_Heartbeat(0U);              /* 心跳位 0：采集活着 */
        vTaskDelay(pdMS_TO_TICKS(2000));    /* 2 秒一帧 */
    }
}

/* 处理任务：把传感器读数组成 20 字节帧，交给发送任务。
 * 组帧 + CRC 是纯 CPU 活，几十微秒就完，但它和「发送」分开的理由是
 * 发送会阻塞（LoRa 模块忙、要等 AUX），不能让组帧跟着一起等。 */
static void vTaskProcess(void *arg)
{
    SensorMsg_t m;
    uint8_t     f[FRAME_TOTAL];
    int16_t     t100;
    uint16_t    h100;

    (void)arg;
    for (;;) {
        if (xQueueReceive(s_q_sensor, &m, portMAX_DELAY) != pdPASS) continue;

        if (m.valid == 0U) {
            SYS_USART_SendLine(DBG, \"[处理] 本轮 DHT11 没读到，跳过组帧\");
            continue;
        }

        frame_build(f, m.temp_c, m.humi_rh, 1013.2f, 320U, 15U, 400U);
        if (frame_parse(f, FRAME_TOTAL, &t100, &h100) != 0U) {
            (void)SYS_USART_Printf(DBG, \"[处理] 反解校验 OK：T=%d.%02d H=%u.%02u\",
                                   (int)(t100 / 100), (int)(t100 % 100),
                                   (unsigned)(h100 / 100U), (unsigned)(h100 % 100U));
        }

        if (xQueueSend(s_q_frame, f, 0) != pdPASS) {
            SYS_USART_SendLine(DBG, \"[处理] 发送队列满，丢这一帧\");
        }
        SYS_WDG_Heartbeat(1U);              /* 心跳位 1：处理活着 */
    }
}

/* 通信任务：优先级比采集高一级（4 > 3），因为它有硬实时成分
 * （LoRa 透传靠字节间停顿切帧，必须及时把数据灌出去）。
 * 但它仍然可以被更高级的监控任务打断，且它等队列 / 等模块时会让出
 * CPU，队列阻塞不是忙等，这正是「优先级高不会饿死低优先级」的前提。 */
static void vTaskComm(void *arg)
{
    uint8_t f[FRAME_TOTAL];

    (void)arg;
    for (;;) {
        if (xQueueReceive(s_q_frame, f, portMAX_DELAY) != pdPASS) continue;

        /* 发出前自检：把 CRC 两字节也算进去，结果必须是 0。
         *   组帧与 CRC 不自洽的 bug 在这一行就会被抓住，不用等下位机。 */
        if (frame_crc16(&f[2], 16U) == 0U) {
            SYS_USART_SendLine(DBG, \"[发送] 帧自检 OK（整段 CRC = 0）\");
        } else {
            SYS_USART_SendLine(DBG, \"[发送] 帧自检失败，组帧与 CRC 不自洽\");
        }

        if (LORA_E22_Send(f, FRAME_TOTAL) == 0) {
            s_tx_ok++;
        } else {
            s_tx_fail++;
            SYS_USART_SendLine(DBG, \"[发送] LoRa 发失败，检查天线与 AUX\");
        }

        LED_Toggle(1);
        SYS_WDG_Heartbeat(2U);              /* 心跳位 2：通信活着 */
    }
}

#endif /* SYS_TEST_BOARD == 1 */

/* ---------------- 五、板2：两个任务 + 断网落盘 ---------------- */

#if (SYS_TEST_BOARD == 2)

/* 接收任务：LoRa E22 透传模式不吐「包开始 / 包结束」标记，
 * 只能靠字节之间的停顿切帧，所以必须 10~50ms 就轮询一次；
 * 太稀会把两帧粘成一段，长度对不上就被 CRC 拦掉、白丢一帧。 */
static void vTaskLoraRx(void *arg)
{
    uint8_t  buf[64];
    uint16_t n = 0U;
    int16_t  t100;
    uint16_t h100;

    (void)arg;
    LORA_E22_Init();
    LORA_E22_SetMode(LORA_E22_MODE_TRANSPARENT);

    for (;;) {
        if (LORA_E22_Recv(buf, (uint16_t)sizeof(buf), &n) == 0) {
            if (frame_parse(buf, n, &t100, &h100) != 0U) {
                s_rx_ok++;
                if (xQueueSend(s_q_frame, buf, 0) != pdPASS) {
                    SYS_USART_SendLine(DBG, \"[接收] 处理队列满，丢这一帧\");
                }
            } else {
                s_rx_bad++;                 /* 坏帧单独计数，看链路质量 */
            }
        }

        LED_Toggle(0);
        SYS_WDG_Heartbeat(0U);
        vTaskDelay(pdMS_TO_TICKS(20));      /* 20ms 一轮，防止粘帧 */
    }
}

/* 转发任务：优先级 4，比接收高一级。
 * 联网时上报 MQTT；判活失败或上报失败就转 Flash 缓存。
 * W25QXX 的读写由「缓存任务写、补传任务读」两方共用，
 *   w25qxx_log.c 内部用一把递归互斥量（log_lock / log_unlock）：
 *     任务上下文 → 取锁；中断上下文或调度器未启动 → 不取锁；
 *   不关中断，只有同样读写日志的任务会互相等，调度器照常跑。
 *   锁内仍会调页编程与扇区擦除，erase 一次约 50ms，因此整片
 *   清空要用 W25QXX_LogClearStep() 一次擦一个扇区，让界面刷新
 *   与喂狗任务能插进来；早先用 vTaskSuspendAll() 挂调度器的
 *   写法已经改掉（那会把所有任务一起停掉）。 */
static void vTaskForward(void *arg)
{
    uint8_t f[FRAME_TOTAL];
    SysRtc_t now;
    uint32_t ts;
    uint8_t  fail_streak = 0U;

    (void)arg;
    for (;;) {
        if (xQueueReceive(s_q_frame, f, portMAX_DELAY) != pdPASS) continue;

        SYS_RTC_GetTime(&now);
        ts = (uint32_t)now.hour * 10000U
           + (uint32_t)now.min  * 100U
           + (uint32_t)now.sec;             /* 简易序号：时分秒拼成一个数 */

        /* 判活失败也算通信失败，和上报失败走同一条落盘路径 */
        if ((fail_streak < 3U) && (MQTT_IsAlive() != 0U)) {
            if (MQTT_Publish(\"data/f407-002\", f, FRAME_TOTAL, 0, 0) == 0) {
                fail_streak = 0U;
                SYS_USART_SendLine(DBG, \"[转发] MQTT 上报成功\");
                SYS_WDG_Heartbeat(1U);
                continue;
            }
            fail_streak++;
        }

        /* 断网 / 连续失败：写本板 SPI Flash 的环形日志区，先存下来 */
        if (W25QXX_LogWrite(ts, f, FRAME_TOTAL) == W25QXX_LOG_OK) {
            (void)SYS_USART_Printf(DBG, \"[缓存] 已落盘，未上传 %u 条\",
                                   (unsigned)W25QXX_LogCount());
        } else {
            SYS_USART_SendLine(DBG, \"[缓存] 落盘失败，检查 W25QXX 与 SPI1\");
        }

        SYS_WDG_Heartbeat(1U);
    }
}

/* 补传：网络恢复后从最旧一条开始读回来重发。
 * 读接口要求调用方缓冲是 PAYLOAD + 1 = 21 字节，末尾那个结束符是
 *   函数自己写的；写成 20 字节就会在 buf[20] 上越界写 1 字节。 */
static void backfill_cache(void)
{
    uint32_t count;
    uint32_t cap;
    uint16_t head;
    uint8_t  buf[W25QXX_LOG_PAYLOAD + 1U];
    uint32_t ts = 0U;
    uint16_t n  = 0U;

    W25QXX_LogStat(&count, &cap, &head);        /* 丢了多少条要另调 LogLost */
    while (MQTT_IsAlive() != 0U) {
        if (count == 0U) break;
        if (W25QXX_LogRead(0U, &ts, buf, &n) != W25QXX_LOG_OK) break;
        if (MQTT_Publish(\"data/f407-002\", buf, n, 0, 0) != 0) break;
        count--;
    }
}

#endif /* SYS_TEST_BOARD == 2 */

/* ---------------- 六、ISR 与任务的边界 ---------------- */

/*
 * 串口接收中断里只做两件事：把字节搬进环形缓冲、置一个标志。
 * sys_usart.c 里 rx_buf / rx_head / rx_tail 全是 static volatile，
 * 因为 ISR 写、任务读；解析（找帧头、算 CRC）一律放在任务里。
 * 绝不能在 ISR 里做的事：
 *     I2C 读写（要等总线，且互斥量不能在中断上下文取）
 *     Flash 擦写（一次擦除几十毫秒到一秒）
 *     printf（串口发一个字符要等 TXE，会拖长中断时间）
 * 中断里能做的只有：搬运字节、置标志、必要时给信号量。
 */
static void app_uart_isr_skeleton(void)
{
    /* 真实实现在 sys_usart.c 的 usart_isr() 里：
     *   rx_buf[port][rx_head[port]] = 数据寄存器;
     *   rx_head[port] = (rx_head[port] + 1U) % SYS_USART_RX_BUF_SIZE;
     * 就这么两句，别的都交给任务。 */
}

/* ---------------- 七、监控任务：心跳汇总 + 统一喂狗 ---------------- */

/* 优先级 5 最高、1s 一轮。自己先报到位，再看是不是全员到齐：
 *   SYS_WDG_HeartbeatPoll() 非 0 → 所有任务都来过 → 它内部已经喂了狗；
 *   返回 0 → 有任务没报到 → 不喂狗 → 看门狗把整块板复位。
 * 这才是看门狗真正管用的用法；每个任务各自喂狗，会让一个卡死的
 * 任务被别的任务掩盖掉，看门狗形同虚设。 */
static void vTaskMonitor(void *arg)
{
    uint32_t t0;
    uint32_t t1;

    (void)arg;

    t0 = SYS_TICK_GetTick();
    vTaskDelay(pdMS_TO_TICKS(200));
    t1 = SYS_TICK_GetTick();
    SYS_TICK_Delay_ms(200U);            /* T1 修复后这里必须能返回 */

    (void)SYS_USART_Printf(DBG, \"[监控] 调度器节拍验证：%u ms\", (unsigned)(t1 - t0));

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        SYS_WDG_Heartbeat((uint8_t)(APP_TASK_COUNT - 1U));

        if (SYS_WDG_HeartbeatPoll() == 0U) {
            (void)SYS_USART_Printf(DBG, \"[监控] 有任务没报到：Pending = %08X\",
                                   (unsigned)SYS_WDG_HeartbeatPending());
        }

#if (SYS_TEST_BOARD == 1)
        (void)SYS_USART_Printf(DBG, \"[监控] tick=%u 发出 %u 帧 / 失败 %u 次\",
                               (unsigned)SYS_TICK_GetTick(),
                               (unsigned)s_tx_ok, (unsigned)s_tx_fail);
#else
        (void)SYS_USART_Printf(DBG, \"[监控] tick=%u 收到 %u 帧 / 坏帧 %u 个\",
                               (unsigned)SYS_TICK_GetTick(),
                               (unsigned)s_rx_ok, (unsigned)s_rx_bad);
#endif
        LED_Toggle(3);
    }
}

/* ---------------- 八、main：先自检、再建任务、最后开狗启调度器 ---------------- */

int main(void)
{
    uint32_t t0;
    uint32_t t1;

    /* 第一句必须是它：把 NVIC 优先级分组设成 Group_4。
     *   分组错位会让中断抢占优先级被压缩到低位，越过 FreeRTOS 的
     *   BASEPRI 阈值、撕开内核临界区，偶发死机，最难查的一类。 */
    SYS_NVIC_Init();

    SYS_USART_Init(DBG, DBG_BAUD);
    SYS_USART_InitRxIT(DBG, DBG_BAUD);
    delay_ms_dwt(200);                  /* 等 CH340C 枚举 + 串口助手打开 */

    (void)SYS_USART_Printf(DBG, \"#  %s  SPL + FreeRTOS 上板自检  #\", BOARD_NAME);
    (void)SYS_USART_Printf(DBG, \"#  编译时间 %s %s  #\", __DATE__, __TIME__);

    LED_Init();
    LED_AllOn();
    delay_ms_dwt(200);
    LED_AllOff();

    /* ---------- 阶段一：阻塞式自检，调度器还没跑 ---------- */
    /* 这段时间里不能用 vTaskDelay，也不能用互斥量：
     * 没有调度器就没有「任务」概念，SYS_I2C_Lock 在此时是空操作。 */
    t0 = SYS_TICK_GetTick();
    delay_ms_dwt(50);
    t1 = SYS_TICK_GetTick();
    if ((t1 != t0) && ((t1 - t0) >= 40U) && ((t1 - t0) <= 60U)) {
        SYS_USART_SendLine(DBG, \"[ OK ] 03 毫秒时基（T1 修复验证）\");
    } else {
        SYS_USART_SendLine(DBG, \"[FAIL] 03 毫秒时基：GetTick 不前进，检查 T1 修复\");
    }

    if (SYS_WDG_HeartbeatPending() != 0U) {
        SYS_USART_SendLine(DBG, \"[ OK ] 05 看门狗（W1 修复验证）\");
    } else {
        SYS_USART_SendLine(DBG, \"[FAIL] 05 看门狗：心跳位没置起来，检查 Heartbeat\");
    }

#if (SYS_TEST_BOARD == 1)
    /* 板1 只有 DHT11 自检；板2 才有 Flash 身份与断网缓存日志 */
    SYS_USART_SendLine(DBG, \"[ -- ] 08 DHT11 温湿度（D1 修复验证）\");
#else
    if (W25QXX_Init(0U) == 0U) {
        uint32_t id = W25QXX_ReadID();
        if (((id >> 16) & 0xFFU) == 0xC8U) {
            SYS_USART_SendLine(DBG, \"[ OK ] 06 Flash 身份：GigaDevice GD25Q128\");
        } else {
            /* S4 修复点：旧代码只认 Winbond 的 0xEF4018，
             *   而本板是 GD25Q128(0xC84018)，Init 永远失败，
             *   调用方按示例直接 return，整个断网缓存模块被跳过。
             *   修复后只看容量字节 == 0x18。 */
            SYS_USART_SendLine(DBG, \"[FAIL] 06 Flash 身份：不是 GD25Q128\");
        }
    }
    W25QXX_LogInit();
    if (W25QXX_LogCount() > 0U) {
        SYS_USART_SendLine(DBG, \"[ OK ] 07 断网缓存日志（S1/S2 修复验证）\");
    } else {
        SYS_USART_SendLine(DBG, \"[FAIL] 07 断网缓存日志：一条都读不出\");
    }
#endif

    /* ---------- 阶段二：建队列 ---------- */
    SYS_USART_SendLine(DBG, \"准备启动 FreeRTOS 调度器\");
    (void)SYS_USART_Printf(DBG, \"configTOTAL_HEAP_SIZE = %u 字节\",
                           (unsigned)configTOTAL_HEAP_SIZE);

    s_q_frame = xQueueCreate(4, FRAME_TOTAL);
    if (s_q_frame == NULL) {
        SYS_USART_SendLine(DBG, \"[FAIL] 帧队列创建失败（堆不够）\");
        for (;;) { SYS_WDG_Feed(); LED_Toggle(0); delay_ms_dwt(500); }
    }

#if (SYS_TEST_BOARD == 1)
    s_q_sensor = xQueueCreate(4, sizeof(SensorMsg_t));
    if (s_q_sensor == NULL) {
        SYS_USART_SendLine(DBG, \"[FAIL] 传感器队列创建失败（堆不够）\");
        for (;;) { SYS_WDG_Feed(); LED_Toggle(0); delay_ms_dwt(500); }
    }
#endif

    /* ---------- 阶段三：建任务 ---------- */
    /* 栈参数单位是 word（4 字节）：512 = 2KB、256 = 1KB。
     * 局部大数组会爆任务栈，表现是随机 HardFault，最经典的问题。 */
#if (SYS_TEST_BOARD == 1)
    xTaskCreate(vTaskSensor,  \"Sensor\",  512, NULL, 3, NULL);
    xTaskCreate(vTaskProcess, \"Process\", 512, NULL, 3, NULL);
    xTaskCreate(vTaskComm,    \"Comm\",    512, NULL, 4, NULL);
#else
    xTaskCreate(vTaskLoraRx,  \"LoraRx\",  512, NULL, 3, NULL);
    xTaskCreate(vTaskForward, \"Forward\", 512, NULL, 4, NULL);
#endif
    xTaskCreate(vTaskMonitor, \"Monitor\", 256, NULL, 5, NULL);

    /* ---------- 阶段四：开看门狗，再启调度器 ---------- */
    /* 看门狗放在这里才开：前面 DHT11 / LoRa 自检要十几秒，
     * 提前开狗会在自检途中把板子咬复位，看起来像「程序跑不起来」。 */
    SYS_WDG_Init(APP_WDG_TIMEOUT_MS);
    (void)SYS_USART_Printf(DBG, \"独立看门狗已启动，超时 %u ms\",
                           (unsigned)APP_WDG_TIMEOUT_MS);

    vTaskStartScheduler();

    /* 正常永远到不了这里 */
    SYS_USART_SendLine(DBG, \"[FAIL] 调度器没启动，几乎一定是堆不够\");
    for (;;) { }
}

/* ============================================================
 *  上板自查三句话：
 *    1. 板1 单跑：01~05 + 08 + 09 全 OK，再看串口每 2 秒一行发出计数；
 *    2. 板2 单跑：01~09 全 OK，拔网线等 2 分钟看 MQTT 判活是否转 0；
 *    3. 两板同跑：板2 应持续打印 [接收]，每帧都落进 W25QXX 缓存，
 *      断电再上电 count 不减，掉电不丢才算闭环成立。
 * ============================================================ */",
 r:"1) 先确认两板的 Keil 工程 <Define> 里都没有 SYS_TEST_BOARD，各自吃 main.c 文件里的默认值 1 和 2，别手贱去工程里注入，一注入另一块板的整条数据链就会跟着编进来。2) 板1 单独上电，只看 01 复位原因 → 05 看门狗：01 若报 IWDG，说明上次是看门狗复位，先查是不是有任务卡死；03 毫秒时基如果不前进，就是 T1 没修好，后面所有 vTaskDelay 全会乱。3) 板1 继续看 08 DHT11：返回 -1 查 PG9 接线，返回 -2 查是否 SYS_DHT11_LOCK_IRQ = 1（位相位错一位就会校验和恒失败）。4) 板1 看 09 LoRa：先读回配置打印，确认网络ID / 信道 / 地址三项；再发 AT 看有没有 OK；AUX 一直忙就先接天线。5) 板2 单独上电，看 06 Flash 身份是否报 GD25Q128（不是 Winbond）、07 断网缓存日志能否读出条数；07 读出 0 条就是 S1/S2 没修好，后面的断网落盘等于没写。6) 板2 单独跑 10 MQTT：连上后拔网线，什么都别动等 2 分钟，KeepAliveService 必须返回 8、IsConnected 必须变 0，这一步不过，断网判据就是假的，数据会静默丢。7) 做单板 LoRa 自发自收：拔掉另一块板，让板1 自己发自己收不到才对（板1 没有接收任务），真正能验的是板2 配一个临时发送循环。8) 两板同时上电，板2 应每 2 秒打一行 [接收]；板2 收到数在涨而 MQTT 侧没数据，就是上报段的问题，与空口无关。9) 最后跑 11 闭环：板2 每帧都该落进 W25QXX 缓存，串口会打印未上传条数；断电再上电，count 不减，掉电不丢才算真闭环。10) 全程看监控任务那行：tick 在涨、发出/收到/坏帧三个计数都在动、从不打印「有任务没报到」，才算三件事（节拍、数据、心跳）同时成立。",
 k:"FreeRTOS 双板闭环 检测端 数据通信端 SYS_TEST_BOARD 共用源 死代码 SYS_NVIC_Init Group_4 BASEPRI 临界区 优先级分组 vTaskStartScheduler xTaskCreate 栈 word 2KB 512 256 HardFault 爆栈 局部大数组 xQueueCreate 4 20 帧队列 s_q_frame s_q_sensor configTOTAL_HEAP_SIZE 30KB heap_4 优先级 3 4 5 configMAX_PRIORITIES 8 vTaskDelay pdMS_TO_TICKS taskYIELD 优先级反转 互斥量 递归互斥量 SYS_I2C_Lock SYS_I2C_Unlock __get_IPSR xTaskGetSchedulerState taskSCHEDULER_RUNNING 惰性建锁 double-check SPI1 W25QXX 总线互斥 递归互斥量 log_lock log_unlock taskENTER_CRITICAL 临界区 50ms 扇区擦除 volatile -O1 -O3 优化 ISR 环形缓冲 rx_head rx_tail 只要字节 解析放任务 MQTT_IsAlive MQTT_KeepAliveService MQTT_ERR_ALIVE_TIMEOUT 120秒 PINGREQ PINGRESP TCP半开 拔网线 断网缓存 W25QXX_LogWrite W25QXX_LogRead W25QXX_LogStat W25QXX_LogLost W25QXX_LOG_PAYLOAD 21字节 越界 SYS_WDG_Init APP_WDG_TIMEOUT_MS 4000 IWDG LSI 32kHz 32.8s 心跳汇总 SYS_WDG_Heartbeat SYS_WDG_HeartbeatPoll 各任务各喂 掩盖死任务 20字节帧 0xAA55 0x55AA AA AA 接缝 三重确认 帧头 长度自洽 CRC16-MODBUS 0xA001 低字节在前 大端 气压×10 SYS_MODBUS_Crc16 MODBUS_CRC16 板1 USART3 PB10 PB11 板2 USART6 PC6 PC7 CH340C 跳线帽 天线先接后上电 DHT11 PG9 自检 01复位原因 02RTC 03毫秒时基 04DWT 05看门狗 06Flash身份 07断网缓存日志 08DHT11 09LoRaE22 10MQTT判活 11闭环联调 GD25Q128 0xC84018 0xEF4018 联调排错顺序 一次实机都没跑过" }"""

GUIDE_P26_RTOS_TWObOARD = r"""
/* ===================== ㉖ FreeRTOS 双板闭环联调 ===================== */
{m:"引导", t:"guide", n:"🧭 ㉖ FreeRTOS 双板闭环联调向导（从「编译过了」到「两板真的通」）",
 d:"这份向导不按文件顺序讲，按「在板子旁边该做什么」的顺序讲：先把编译期那条看不见的分界线认清，再把 NVIC 分组这把地基埋下去，然后把 01~09 的阻塞式自检一条条跑绿，接着才建队列、建任务、开狗、启调度器，最后才是两板闭环。每一步都给「怎么验证」和「常见问题」，照做就能把问题锁死在一层里。",
 g:"STM32F407 标准外设库 + FreeRTOS-Kernel V10.4.6（heap_4，30KB）；ARM Compiler 5；板1 STM32F407ZE @168MHz 编译 -O1，板2 STM32F407ZG @168MHz 编译 -O3；对应 FWLIB 模块：sys_nvic / sys_tick / sys_wdg / sys_usart / sys_i2c / sys_spi / sys_dht11 / lora_e22 / esp8266 / mqtt / w25qxx / w25qxx_log / sys_frame / modbus。",
 k:"FreeRTOS 双板 闭环联调 起步 顺序 SYS_TEST_BOARD 死代码 SYS_NVIC_Init Group_4 自检 01~09 阻塞 调度器未运行 vTaskDelay 不能用 互斥量 空操作 队列 栈 word HardFault 优先级 心跳汇总 看门狗 4000 原子提交 判活 120秒 断网落盘 补传 重入 上板顺序 排错分层",
 e:"/* ============================================================\n *  ㉖ 双板闭环联调 - 按本向导 9 步搭出来的成品（板1 检测端版）\n * ------------------------------------------------------------\n *  第 1 步 分清两板    : SYS_TEST_BOARD 决定编哪条数据链\n *  第 2 步 NVIC 分组   : SYS_NVIC_Init() 必须是 main 第一句\n *  第 3 步 阻塞自检    : 01~09 在调度器起来之前全部跑完\n *  第 4 步 建队列      : xQueueCreate(4, 20) 与 (4, sizeof(Msg))\n *  第 5 步 建任务      : 栈单位是 word，512 = 2KB\n *  第 6 步 优先级      : 采集 3 / 处理 3 / 通信 4 / 监控 5\n *  第 7 步 共享资源锁  : I2C 用 SYS_I2C_Lock，SPI1 应用层自管\n *  第 8 步 看门狗      : 建完任务再 SYS_WDG_Init(4000)\n *  第 9 步 闭环        : 发出 / 接收 / 缓存 / 补传 四路证据同看\n * ============================================================ */

#ifndef SYS_TEST_BOARD
#define SYS_TEST_BOARD      1
#endif

#include \"stm32f4xx.h\"
#include \"stdio.h\"
#include \"string.h\"
#include \"led.h\"
#include \"delay.h\"
#include \"sys_clock.h\"
#include \"sys_tick.h\"
#include \"sys_nvic.h\"
#include \"sys_usart.h\"
#include \"sys_wdg.h\"
#include \"sys_i2c.h\"
#include \"lora_e22.h\"
#include \"FreeRTOS.h\"
#include \"task.h\"
#include \"queue.h\"

#if (SYS_TEST_BOARD == 1)
    #include \"sys_dht11.h\"
    #include \"sys_modbus.h\"
    #define APP_TASK_COUNT  3
#else
    #include \"w25qxx.h\"
    #include \"w25qxx_log.h\"
    #include \"modbus.h\"
    #define APP_TASK_COUNT  2
#endif

#define DBG                 SYS_USART_1
#define APP_WDG_TIMEOUT_MS  4000U
#define FRAME_TOTAL         20U

static QueueHandle_t      s_q_sensor;
static QueueHandle_t      s_q_frame;
static volatile uint32_t  s_ok  = 0U;
static volatile uint32_t  s_bad = 0U;

typedef struct {
    float    temp_c;
    float    humi_rh;
    uint8_t  valid;
    uint32_t tick;
} SensorMsg_t;

/* 第 4 步的队列、第 6 步的优先级、第 8 步的看门狗，全在 main 里一眼看全 */
int main(void)
{
    uint32_t t0;
    uint32_t t1;

    SYS_NVIC_Init();                    /* 第 2 步：Group_4，必须第一句 */
    SYS_USART_Init(DBG, 115200U);
    SYS_USART_InitRxIT(DBG, 115200U);
    delay_ms_dwt(200);

    SYS_USART_SendLine(DBG, \"# 双板闭环联调 - 板1 检测端 #\");
    LED_Init();

    /* 第 3 步：调度器没跑，这里只有阻塞式自检 */
    t0 = SYS_TICK_GetTick();
    delay_ms_dwt(50);
    t1 = SYS_TICK_GetTick();
    if ((t1 - t0) >= 40U) {
        SYS_USART_SendLine(DBG, \"[ OK ] 03 毫秒时基\");
    }

    /* 第 4 步：建队列；返回 NULL 就是堆不够 */
    s_q_frame = xQueueCreate(4, FRAME_TOTAL);
    if (s_q_frame == NULL) { for (;;) { } }

#if (SYS_TEST_BOARD == 1)
    s_q_sensor = xQueueCreate(4, sizeof(SensorMsg_t));
    if (s_q_sensor == NULL) { for (;;) { } }
#endif

    /* 第 5 步：栈写的是 word 数，不是字节数 */
    xTaskCreate(vTaskSensor,  \"Sensor\",  512, NULL, 3, NULL);
    xTaskCreate(vTaskProcess, \"Process\", 512, NULL, 3, NULL);
    xTaskCreate(vTaskComm,    \"Comm\",    512, NULL, 4, NULL);
    xTaskCreate(vTaskMonitor, \"Monitor\", 256, NULL, 5, NULL);

    /* 第 8 步：自检跑完才开狗 */
    SYS_WDG_Init(APP_WDG_TIMEOUT_MS);
    vTaskStartScheduler();

    for (;;) { }
}

/* 第 7 步：I2C 是共享总线，锁由库提供；SPI1 库不管，应用层自管 */
static void oled_refresh_safe(void)
{
    SYS_I2C_Lock(SYS_I2C_1);
    /* 多次 I2C 事务在这里保持原子 */
    SYS_I2C_Unlock(SYS_I2C_1);
}

/* 第 9 步：监控任务统一喂狗 + 打印四路证据 */
static void vTaskMonitor(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        SYS_WDG_Heartbeat((uint8_t)(APP_TASK_COUNT - 1U));
        if (SYS_WDG_HeartbeatPoll() == 0U) {
            (void)SYS_USART_Printf(DBG, \"[监控] 有任务没报到 = %08X\",
                                   (unsigned)SYS_WDG_HeartbeatPending());
        }
        (void)SYS_USART_Printf(DBG, \"[监控] tick=%u 成功 %u / 异常 %u\",
                               (unsigned)SYS_TICK_GetTick(),
                               (unsigned)s_ok, (unsigned)s_bad);
    }
}",
 f:[
  ["① 先把「两块板是一个工程」这件事认清",
   "两板 main.c 是同一份双板共用源，开头用 #ifndef SYS_TEST_BOARD 给板1 默认 1、板2 默认 2。两个 Keil 工程的 <Define> 里都只写了 USE_STDPERIPH_DRIVER, STM32F40_41xxx, HSE_VALUE=8000000，没有任何一处注入 SYS_TEST_BOARD，所以各自吃自己文件里的默认值。\n\n于是产生了本工程最重要的一个「看不见的事实」：板1 里 (SYS_TEST_BOARD == 2) 的整段分支、板2 里 (SYS_TEST_BOARD == 1) 的整段分支，都是死代码，编译器根本不会碰。板1 的 FWLIB 目录里压根没有 w25qxx.c / w25qxx_log.c，也照样链接通过，靠的就是这一步，宏把整段剪掉了。\n\n但要#include \"esp8266.h\" / \"mqtt.h\" 这类包含是写在两条分支之外的、两板都无条件包含，宏只包住了 test_mqtt() 之类的函数体。所以看到「板1 里居然 include 了 mqtt.h」不要惊讶，也不要删。",
   "打开两板 main.c 的 70~110 行，确认 #ifndef SYS_TEST_BOARD 的默认值与 #if 分支里 HAS_DHT11 / HAS_W25QXX 的定义；再打开两个 .uvprojx 搜 <Define>，确认里面确实没有 SYS_TEST_BOARD。",
   "/* 两板各自吃自己文件里的默认值 */\n#ifndef SYS_TEST_BOARD\n#define SYS_TEST_BOARD      1       /* 板1；板2 的文件里这里写 2 */\n#endif\n\n#if (SYS_TEST_BOARD == 1)\n    #include \"sys_dht11.h\"\n    #include \"sys_modbus.h\"      /* 板1 的 CRC 叫 SYS_MODBUS_Crc16 */\n    #define HAS_DHT11   1\n    #define HAS_W25QXX  0\n    #define APP_TASK_COUNT  3       /* Sensor / Process / Comm */\n#else\n    #include \"w25qxx.h\"\n    #include \"w25qxx_log.h\"\n    #include \"modbus.h\"          /* 板2 的 CRC 叫 MODBUS_CRC16 */\n    #define HAS_W25QXX  1\n    #define HAS_DHT11   0\n    #define APP_TASK_COUNT  2       /* LoraRx / Forward */\n#endif",
   "两板分别编译，各看一次 0 Error 0 Warning。再故意在板1 里把默认值改成 2 编译一次，会立刻冒出一堆 w25qxx 相关的未定义符号，这正好反证了「板1 靠宏剪掉了整段」。验完记得改回来。",
   "① 手贱在 Keil 工程的 <Define> 里注入 SYS_TEST_BOARD：一旦注入，文件里的 #ifndef 就失效，另一块板的整条数据链会被编进来，板1 缺 w25qxx.c 立刻链接报错；而且两块板会出现「同一个宏两种含义」的混乱，后面所有 #if 都不可信。\n\n② 以为「0 Error / 0 Warning」等于程序能跑：链接器会把没被引用的驱动当死代码裁掉，所以过去的空骨架工程编译通过什么都没验证。本工程主动调用每个驱动，就是为了逼编译器把驱动真的编进来。\n\n③ 在板1 里删掉 #include \"mqtt.h\"：它虽然不属于板1 的数据链，但包含是无条件的，删了会连带影响别处；要精简应该连包含一起改，别只删一半。",
   "㉖ 本卡「阶段一/阶段二」；⑱ SPI Flash 断网缓存日志（板2 的落盘细节）；板1 `sys_dht11.h`、板2 `mqtt.h`"],
  ["② NVIC 分组：SYS_NVIC_Init 必须是 main 的第一句",
   "Cortex-M 的 NVIC 优先级寄存器只实现高若干位，具体几位由「优先级分组」决定。本工程统一用 Group_4：4 位全部给抢占优先级，没有任何子优先级位。\n\n为什么这件事排在第一位：FreeRTOS 用 BASEPRI 寄存器实现临界区，把 BASEPRI 设成一个阈值，所有数值上低于这个阈值（数值越小优先级越高）的中断就被屏蔽掉。而 IPR 里到底哪几位有效，完全由分组决定。如果分组是 Group_2，写进去的抢占优先级 5 会被截断成 1，实际生效的 IPR 值比 BASEPRI 阈值还「高」，于是这些中断能直接冲进内核临界区，把正在改链表的内核打断，现象是偶发死机、跑几小时才来一次、加打印就消失。\n\n还有一个更隐蔽的地方：分组必须在任何中断优先级被写之前设定。因为每个 SYS_nnn_Init 都是按「当前分组」的含义往 IPR 里写数的，先有中断写好 IPR、再改分组，那些已经写进去的数值含义会全部错位，而且是静默错位。所以它不是「建议第一句」，是「必须第一句」。",
   "打开 main.c，确认 int main(void) 的下一行就是 SYS_NVIC_Init();（板1 :1045、板2 :1054），中间不许插任何 nnn_Init。再去 sys_nvic.c 里确认它调的是 NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4)。",
   "int main(void)\n{\n    /* 第一句，不能有任何东西排在它前面 */\n    SYS_NVIC_Init();\n\n    /* 这些 SYS_nnn_Init 会按 Group_4 的含义去写各自的 IPR */\n    SYS_USART_Init(SYS_USART_1, 115200U);\n    SYS_USART_InitRxIT(SYS_USART_1, 115200U);\n    SYS_DHT11_Init(SYS_DHT11_PORT, SYS_DHT11_PIN);\n    LORA_E22_Init();\n\n    /* ... 自检、建队列、建任务 ... */\n}",
   "写一个最简复现：把 SYS_NVIC_Init() 挪到 SYS_USART_Init() 之后，然后让串口中断和任务同时高频跑。会看到原本正常的程序开始偶发卡死或数据错乱。\n\n更直接的验证：在 SYS_NVIC_Init() 前后各读一次 SCB->AIRCR 的分组位，确认是 0x05（Group_4）而不是别的值；再确认每个 ISR 的 IPR 高 4 位确实是预期值。",
   "① 以为「反正优先级都写成 5，分组无所谓」：分组决定了这 5 落在哪几位。Group_2 下 5 会被截断，实际阈值比想象中低，中断就能越过内核临界区，这是本工程最贵的一个问题。\n\n② 把它当成「初始化里的一项」，排到 USART 初始化后面：只要有一个中断先配好了 IPR，改分组就会静默改变它的含义，而且不会有任何编译警告。\n\n③ 以为库里某个模块会替做这件事：sys_wdg / sys_usart 都不会碰分组，只有 SYS_NVIC_Init 会，谁先谁后必须自己保证。\n\n④ 开了 FreeRTOS 之后又回头改分组：等于把所有中断优先级重定义一遍，出问题已属「必然」而非「偶发」。",
   "`sys_nvic.h` 的 SYS_NVIC_Init；㉖ 本卡「阶段一」；FreeRTOSConfig.h 里 configPRIO_BITS 4 与 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5 的注释"],
  ["③ 阶段一：调度器没跑，只能跑阻塞式自检 01~09",
   "FreeRTOS 起来之后，「阻塞」这个动作的含义就变了：vTaskDelay 会让出 CPU 给别的任务、互斥量会让任务挂起等锁。可在启动调度器之前，没有有「任务」这个概念，当前跑的就是 main 的那条栈，没有任何调度器在背后。\n\n这时候如果误用 vTaskDelay(100)，它内部要访问内核节拍和就绪链表，而调度器没启动，轻则延时不准，重则直接异常。所以自检阶段的原则是：只用最笨的阻塞方式（delay_ms_dwt / 轮询标志），一个一个模块验过去。这也正是 SYS_I2C_Lock 在调度器未运行时直接返回的原因，它判断 xTaskGetSchedulerState() != taskSCHEDULER_RUNNING 就返回，此时上电初始化本来就只有一条执行流，不存在并发，加锁是空操作，这是刻意设计而不是漏加。\n\n用最笨的方式还有一个好处：任何一处 FAIL 都能把故障范围缩到一个模块，而不用去怀疑「是不是调度器的问题」。01~09 的顺序也是按依赖排的，先复位原因（判断上次为什么复位），再 RTC（备份域），再时基（后面所有延时的基础），再看门狗，最后才是外设。",
   "上电后从串口一条条往下看：01 复位原因、02 RTC 与备份域、03 毫秒时基、04 微秒级延时（DWT）、05 看门狗，板2 再看 06 Flash 身份 / 07 断网缓存日志，板1 看 08 DHT11，两板都看 09 LoRa E22。每一行必须是 [ OK ]。",
   "/* 阶段一：全部是阻塞式调用，不用队列、不用信号量 */\ntest_reset_cause();     /* 01 看 SYS_WDG_ResetCauseDecode 的结果 */\ntest_rtc();             /* 02 备份域是否保住时间 */\ntest_tick();            /* 03 T1：GetTick 必须真的前进 */\ntest_us_delay();        /* 04 DWT->CYCCNT 实测 100us 应 80~130us */\ntest_watchdog();        /* 05 W1：心跳位能置起来 */\n\n#if HAS_W25QXX\ntest_flash_id();        /* 06 身份：GD25Q128 = 0xC84018 */\ntest_flash_log();       /* 07 S1/S2：写 3 条再读回来 */\n#endif\n\n#if HAS_DHT11\ntest_dht11();           /* 08 D1：-1 查接线，-2 查关中断 */\n#endif\n\ntest_lora();            /* 09 配置 + AT + 切回透传 */\n\n/* 到这里为止，一次 vTaskDelay 都没出现过 */",
   "看 03 是否 [ OK ]：它取 t0 → delay_ms_dwt(50) → t1，要求 (t1 - t0) 落在 40~60ms。不前进就说明 T1 没修好（旧版 sys_tick.c 的弱定义 SysTick_Handler 被 FreeRTOS 顶掉，systick_ms 恒 0）。\n\n看 05：SYS_WDG_HeartbeatClear() 之后 HeartbeatPending() 必须 != 0。旧版 W1 bug 是 COUNT = 0 时 HeartbeatPoll 直接 return 0，根本不喂狗，照文档写就是上电复位循环。\n\n板2 的 07 要连做两次：写完 3 条读回来，然后断电再上电，count 仍 ≥ 3 且能读出内容，才算 S1/S2 真通过。",
   "① 在自检阶段写 vTaskDelay(pdMS_TO_TICKS(10))：调度器还没跑，行为未定义。要用延时就用 delay_ms_dwt。\n\n② 以为「SYS_I2C_Lock 在自检阶段没用」于是手动删掉：它返回是判断了调度器状态的结果，不是失效；删掉以后上电阶段一旦有别的执行流（比如 DMA 回调）就会真出问题。\n\n③ 自检中途就 SYS_WDG_Init：后面 DHT11 / LoRa 自检要十几秒，看门狗会直接把板子咬复位，现象看起来像「程序根本跑不起来」。\n\n④ 07 只在通电状态下验：不断电就验不出「掉电不丢」，而掉电不丢才是缓存模块存在的意义。\n\n⑤ 看到 SKIP 就以为失败：SKIP 是「本板没这个硬件」，不算失败，要认清板1/板2 各自该有哪些编号。",
   "㉖ 本卡「阶段一/阶段五」；`sys_wdg.h` 的复位原因位定义与心跳说明；`sys_tick.h` 的 T1 修复说明；⑱ 断网缓存日志"],
  ["④ 阶段二：建队列，长度与元素大小怎么定",
   "队列是三段式流水线的传送带：采集写、处理读；处理写、发送读。它同时解决两件事，传递数据，以及在两队之间解耦速度差。\n\n长度取 4 的依据是「吸收抖动」：上游可能 2 秒才产一帧，下游却可能因为 Flash 擦扇区（一次 50ms 到 1 秒）或等 MQTT 应答而停顿好几秒。队列太短，上游一写就满、只能丢帧；队列太长，纯占 RAM，而 heap_4 一共只有 30KB。4 项 = 20 字节 × 4 = 80 字节，代价极小，足够吸收掉下游几秒的抖动。\n\n元素大小必须传「字节数」：帧队列传 FRAME_TOTAL(20)，传感器队列传 sizeof(SensorMsg_t)。这里最容易错的是把结构体指针塞进队列，一旦传指针，生产者在栈上建的临时结构体出了作用域就失效，消费者拿到的是野指针，现象是数值莫名其妙乱跳、偶尔 HardFault。要么传值（本工程的做法），要么传静态/堆上的结构体指针并且自己管生命周期。\n\n队列的阻塞语义也要分清：生产者用 xQueueSend(..., 0) 表示「不等待，满了就返回失败」，这样采集任务永远不会被下游拖住；消费者用 portMAX_DELAY 表示「一直等」，反正没数据时它本来也没事干。这就是实时系统里的常规分工：生产者绝不阻塞，消费者尽情等待。",
   "看串口初始化时打印的 configTOTAL_HEAP_SIZE（应为 30720 字节），再确认两个 xQueueCreate 都返回了非 NULL，NULL 就说明堆已经被前面创建的对象吃掉太多。\n\n运行时验证：在监控任务里周期性打印 uxQueueMessagesWaiting(s_q_frame)，正常情况下它应该长时间为 0，偶尔出现 1；如果长期是 4，说明下游处理不过来，要么优化下游，要么加长队列，绝不能让上游丢帧。",
   "/* 帧队列：元素大小必须是字节数 20，不是 sizeof(指针) */\ns_q_frame = xQueueCreate(4, FRAME_TOTAL);\nif (s_q_frame == NULL) {\n    /* heap_4 一共只有 30KB，创建失败几乎只有一个原因：堆不够 */\n    SYS_USART_SendLine(DBG, \"[FAIL] 帧队列创建失败（堆不够）\");\n    for (;;) { SYS_WDG_Feed(); LED_Toggle(0); delay_ms_dwt(500); }\n}\n\n/* 生产者：绝不阻塞，满了就丢最旧的一帧再写 */\nif (xQueueSend(s_q_sensor, &m, 0) != pdPASS) {\n    SensorMsg_t old;\n    (void)xQueueReceive(s_q_sensor, &old, 0);\n    (void)xQueueSend(s_q_sensor, &m, 0);\n}\n\n/* 消费者：没数据时本来也没事干，尽管一直等 */\nif (xQueueReceive(s_q_sensor, &m, portMAX_DELAY) != pdPASS) continue;",
   "两板都跑起来后，在监控任务里打印队列水位。正常应长期为 0、偶有 1；持续为 4 说明下游跟不上。\n\n再做一个反向验证：临时把 q 长度改成 1，会看到上游频繁走「丢最旧」分支、发出帧数明显变少，这正好证明了队列长度确实在起缓冲作用。验完改回 4。",
   "① 把局部结构体的地址塞进队列：出了作用域就成了野指针，消费者读到的是被覆盖过的栈内存，现象是数值乱跳 + 偶发 HardFault，而且极难复现。\n\n② 队列长度拍脑袋给 1 或给 100：给 1 等于没有缓冲，下游一停顿上游就丢帧；给 100 会白吃 RAM，还要和 30KB 堆里其它的对象抢。\n\n③ 用 xQueueSend 的默认等待（portMAX_DELAY）在采集任务里发送：下游一卡，采集就跟着停摆，整个流水线退化成串行，实时性全丢。\n\n④ 忘了检查 xQueueCreate 返回 NULL：后面所有 xQueueSend 都会对着 NULL 句柄操作，行为未定义且没有任何提示。\n\n⑤ 以为 20 是元素个数：xQueueCreate 第二个参数是每个元素的字节数，「4 项 × 20 字节」是两个参数合起来表达的。",
   "㉖ 本卡「阶段三」；`queue.h` 的 xQueueCreate / xQueueSend / xQueueReceive；⑱ 断网缓存日志（下游阻塞的典型来源）"],
  ["⑤ 阶段三：建任务，栈的单位是 word，不是字节",
   "xTaskCreate 的函数原型里，栈深度参数类型是 configSTACK_DEPTH_TYPE，单位是 StackType_t，在 Cortex-M 上就是 4 字节。所以 512 表示 512 words = 2048 字节 = 2KB，256 表示 1KB，不是 512 字节。\n\n为什么必须给够：任务栈上放的是局部变量、函数调用的返回地址、保存的寄存器上下文，以及被调用函数（包括库函数）自己的栈帧。printf 系列尤其吃栈：格式化一个浮点数可能一次用掉几百字节。\n\n最容易踩的问题是「在任务里定义一个看起来很正常的大数组」，比如 uint8_t buf[2048] 或 char line[512]。编译期完全看不出问题，那片空间属于任务的栈，只有运行时才会越界。而且越界的方向通常是往低地址（往下长），先踩坏相邻的堆或别的任务控制块。现象是典型的随机 HardFault：跑几秒到几小时才来一次、改一句无关的代码就换了复现率、加串口打印有时还「好了」（因为打印改变了栈的布局）。\n\n对策有三条：把大缓冲改成 static（放 .bss 不在栈上，代价是这函数不可重入，单任务调用没问题）；或者用 pvPortMalloc 从堆里要；或者在创建任务后调 uxTaskGetStackHighWaterMark 实测剩余水位再定尺寸。本工程选择给足 512 words，并把大缓冲都放成 static 或干脆不放。",
   "① 每个 xTaskCreate 的栈参数对齐检查：本工程板1 三任务与板2 两任务都是 512（2KB），监控任务 256（1KB）。② 在监控任务里对每个任务调 uxTaskGetStackHighWaterMark(handle)，打印「曾经最多剩多少个 word」，剩得越少越危险，个位数就说明马上就要爆。需要保存 xTaskCreate 的句柄。\n\n③ 故意做一次破坏性验证：在某个任务里写 uint8_t big[1024] 并 memset，跑一会儿看是否 HardFault，亲眼见过一次，以后就会本能地警惕局部大数组。",
   "/* 栈参数单位是 word：512 = 2KB，256 = 1KB */\nxTaskCreate(vTaskSensor,  \"Sensor\",  512, NULL, 3, NULL);\nxTaskCreate(vTaskProcess, \"Process\", 512, NULL, 3, NULL);\nxTaskCreate(vTaskComm,    \"Comm\",    512, NULL, 4, NULL);\nxTaskCreate(vTaskMonitor, \"Monitor\", 256, NULL, 5, NULL);\n\n/* 想知道还剩多少栈，就这么量（需要先存下句柄） */\nstatic TaskHandle_t s_h_sensor;\n(void)uxTaskGetStackHighWaterMark(s_h_sensor);   /* 返回历史最小剩余，单位 word */\n\n/* 大缓冲不要放栈上：放 static 或从堆里要 */\nstatic void task_with_big_buffer(void)\n{\n    static uint8_t buf[2048];       /* 放在 .bss，不占任务栈 */\n    memset(buf, 0, sizeof(buf));\n}",
   "跑十几分钟后在监控任务里打印各任务的 uxTaskGetStackHighWaterMark。判据：剩余 word 数应远大于 0；如果某任务只剩个位数，说明栈尺寸定小了或者有局部大数组，必须处理。",
   "① 以为 512 是 512 字节：实际是 2KB。反之，若认为给 256 有 256 字节，实际有 1KB，尺寸估错两个方向都会出问题。\n\n② 在任务里写局部大数组：编译通过、运行随机 HardFault，这是本工程最经典也最贵的问题。\n\n③ 把 configMINIMAL_STACK_SIZE（本工程 128 words = 512 字节）当成「够用的尺寸」直接给业务任务用：它只是给空闲任务级的最小值，业务任务要按实际调用深度估。\n\n④ 开 configCHECK_FOR_STACK_OVERFLOW = 2 之后忘了实现 vApplicationStackOverflowHook：检测到溢出时会进钩子，钩子没实现就只能干瞪眼。\n\n⑤ 把大缓冲改成 static 之后忘了「不可重入」：单任务调用没事，一旦被两个任务同时调就会互相覆盖数据。",
   "㉖ 本卡「阶段四」；`task.h` 的 xTaskCreate / uxTaskGetStackHighWaterMark；FreeRTOSConfig.h 的 configMINIMAL_STACK_SIZE 128 与 configCHECK_FOR_STACK_OVERFLOW 2"],
  ["⑥ 阶段三续：优先级怎么排，为什么要用互斥量",
   "FreeRTOS 的优先级是数字越大越高，configMAX_PRIORITIES = 8，所以合法范围是 0~7。本工程板1 是采集 3 / 处理 3 / 通信 4，板2 是接收 3 / 转发 4，监控任务统一 5（最高）。\n\n为什么通信不能低于采集：LoRa E22 是透传模块，它不告诉「一帧从哪开始、到哪结束」，只能靠字节之间的停顿切帧，所以必须及时、频繁地把数据灌出去（10~50ms 一轮）。如果通信任务优先级低、被别的任务长期抢占，模块侧就会出现「两帧粘成一段」，接收端长度对不上直接丢帧。\n\n为什么通信也不能无脑最高：如果通信任务比采集还高，且它会长时间忙等（比如轮询等模块 AUX 空闲），低优先级的采集就永远拿不到 CPU，现象是「板子还在跑，但数据再也不更新了」，比崩溃更难查。正确做法是让高优先级任务阻塞式等待（等队列、等信号量、等模块中断），而不是忙等。这才是「优先级高不会饿死低优先级」的前提：高优先级任务一旦阻塞，调度器立刻把 CPU 让给低优先级任务。\n\nvTaskDelay 与 taskYIELD 的区别：vTaskDelay 是「我睡 N 个 tick 再醒」，会让出 CPU 且进入阻塞态，适合周期性任务；taskYIELD 是「我让一次，但马上还会被调度回来」，它只是触发一次任务切换，如果当前任务优先级仍最高，下一秒又是它，所以 taskYIELD 不能用来做「等一会儿」，用它写轮询会变成忙等。\n\n优先级反转与互斥量：低优先级任务 L 拿了共享资源（比如 I2C 总线锁），高优先级任务 H 来要同一把锁，H 就必须等 L 释放；如果中途有个不碰这把锁的中优先级任务 M 一直抢 CPU，L 就跑不完，H 就一直等，这就是优先级反转。FreeRTOS 的互斥量（xSemaphoreCreateMutex）带优先级继承：H 在等锁时，L 会被临时提升到 H 的优先级，尽快跑完释放锁。继承只对互斥量有效，普通二值信号量没有这个机制，所以保护共享资源一律用互斥量，不要用二值信号量。\n\n另外本工程用的 SYS_I2C_Lock 内部是递归互斥量，因为「刷整屏」需要在持锁期间再调若干次加锁的公共事务接口，同一任务要能重复取到同一把锁而不死锁。",
   "① 看 xTaskCreate 的几个数字：板1 = 3/3/4 + 5，板2 = 3/4 + 5，确认监控最高。② 压测：把通信任务临时改成 while(1) 忙等（不阻塞），观察采集任务的计数是否停止增长，能亲手复现「高优先级忙等饿死低优先级」，以后就会本能地把高优先级任务写成阻塞式。\n\n③ 反转验证：让低优先级任务拿 I2C 锁并在锁内 delay 一段时间，高优先级任务再去拿同一把锁，观察低优先级任务是否被临时提前调度完（优先级继承生效）。",
   "/* 优先级：数字越大越高，范围 0 ~ configMAX_PRIORITIES-1 = 0~7 */\nxTaskCreate(vTaskLoraRx,  \"LoraRx\",  512, NULL, 3, NULL);  /* 采集类，最低 */\nxTaskCreate(vTaskForward, \"Forward\", 512, NULL, 4, NULL);  /* 通信类，高一级 */\nxTaskCreate(vTaskMonitor, \"Monitor\", 256, NULL, 5, NULL);  /* 监控，最高 */\n\n/* 高优先级任须须「阻塞式等待」，不能忙等 */\nfor (;;) {\n    xQueueReceive(s_q_frame, f, portMAX_DELAY);   /* 阻塞：没数据就让出 CPU */\n    /* 处理 + 发送 */\n}\n\n/* vTaskDelay = 睡 N 个 tick 并让出 CPU（周期性任务用这个） */\nvTaskDelay(pdMS_TO_TICKS(2000));\n\n/* taskYIELD = 只触发一次切换；若自己优先级仍最高，下一秒还是自己 */\ntaskYIELD();\n\n/* 保护共享资源用互斥量（带优先级继承），不要用二值信号量 */\nstatic SemaphoreHandle_t s_bus_mtx;\ns_bus_mtx = xSemaphoreCreateMutex();\n(void)xSemaphoreTake(s_bus_mtx, portMAX_DELAY);\n/* 临界区里只做该做的事，尽快 Give */\n(void)xSemaphoreGive(s_bus_mtx);",
   "跑一段时间后看监控任务的统计：采集计数与通信计数都应该在稳定增长，两边都不停。如果采集计数停了、通信计数还在涨，说明通信任务在忙等饿死了采集。\n\n用 configUSE_TIME_SLICING = 1 时，同优先级任务会按 tick 轮转；把两个任务设成同优先级，观察它们是否交替运行，可以直观看到时间片轮转。",
   "① 把通信任务设成最高优先级并且在里面写 while(LORA_E22_IsBusy()) 忙等：低优先级任务永远没机会跑，现象是「板子活着但数据不更新」。\n\n② 用 taskYIELD 代替 vTaskDelay 做周期延时：本质是忙等，CPU 全被这一个任务占着。\n\n③ 用二值信号量当互斥量：没有优先级继承，优先级反转发生时无人救场，出现「偶尔卡住几百毫秒」的诡异现象。\n\n④ 在互斥量保护的临界区里做长操作（比如等 MQTT 应答、擦 Flash）：持锁时间一长，所有要这把锁的任务全排队，系统响应变差；临界区里只放必须原子的部分。\n\n⑤ 在中断里 take 互斥量：中断上下文不能阻塞，会直接触发 configASSERT。",
   "㉖ 本卡「阶段四/阶段七」；`sys_i2c.c` 的 SYS_I2C_Lock 与递归互斥量说明；`semphr.h`；FreeRTOSConfig.h 的 configMAX_PRIORITIES 8 / configUSE_MUTEXES 1 / configUSE_TIME_SLICING 1"],
  ["⑦ 阶段四：共享资源谁要加锁，ISR 与任务的边界在哪",
   "先把「谁是共享资源」列清楚：I2C1 总线上挂了 OLED + MPU6050 + 24C02 三样；SPI1 上挂 W25QXX（可能还有 NRF24L01）；W25QXX 自身的读、写、擦；还有各个环形缓冲的读写指针。\n\nI2C 总线必须加锁，而且要用「总线级」而不是「设备级」的锁。原因是一帧完整的 I2C 事务包含起始条件、地址、若干数据字节、停止条件，中间不能被别的起始条件插进来，OLED 刷到一半被人插一个 MPU6050 的读，那 OLED 收到的就是错位的数据，现象是偶发花屏或者某个寄存器读出全 0。库把这件事做进了公共入口：每个公共函数都是「先 SYS_I2C_Lock → 调对应的 nnn_locked 实现 → SYS_I2C_Unlock」，函数体本身改成了 static nnn_locked，只剩一个出口，从结构上杜绝「提前 return 忘记解锁」。需要多次事务原子时，用递归锁把整段包起来。\n\nSPI1 这边要特别sys_spi.c 里没有任何互斥机制，它只提供 SYS_SPI_TransferByte / SYS_SPI_Transfer / SYS_SPI_Read / SYS_SPI_Write 这些裸接口。所以挂在同一根 SPI 上的两个器件之间互斥，必须由应用层自己用一把互斥量保护，这是和 I2C 最不一样的地方，很容易以为「库都管了」。\n\nW25QXX 的环形日志模块的锁策略是一把递归互斥量（w25qxx_log.c 顶部注释写得很清楚）：任务上下文里取一把递归互斥量（log_lock()/log_unlock()，静态变量惰性创建），中断上下文或调度器还没启动时直接跳过、不取锁；不关中断，只有同样读写本模块的任务会互相等，调度器照常运行。不用临界区的原因：因为锁内会调页编程和扇区擦除，一次擦除约 50ms，关中断 50ms 会让整块板子的实时性彻底崩掉。\n\n这里有一个仍然存在的代价：擦除仍在锁内进行（一次扇区擦除约 50ms），这期间别的任务调日志接口要等；为此驱动新增了 W25QXX_LogClearStep()，一次只擦一个扇区，好让界面刷新、心跳喂狗在两次调用之间插进去。所以锁内只做纯 Flash 操作。\n\n环形缓冲的读写指针：如果只有一个写者（典型情况是 ISR 或单个任务）和一个读者，可以做成免锁的，写者只改 head、读者只改 tail，各自单调推进。但这个免锁有三个前提：head / tail 必须是 volatile；索引回绕要用无符号运算；读者不能在读到「还没写完的数据」时判断有效。一旦有第二个写者，免锁立刻失效，必须退回到临界区。\n\nISR 与任务共享的变量必须是 volatile 加临界区。这里有一个两板都不一样的实际差异：板1 编译是 -O1，板2 是 -O3。在 -O3 下，编译器会把「循环体内没有被修改的变量」提到循环外，如果这个变量是被中断改的，任务就永远看不到变化，典型现象是 while(!s_flag) 死循环，而 s_flag 明明在中断里被置 1 了。volatile 的作用就是告诉编译器「这个变量可能在看不见的地方被改，每次都要重新读」。 volatile 只保证「每次访问都真的去内存读」，不保证原子性：如果是 32 位以上的量或复合操作，还是要配临界区。\n\nISR 与任务的边界铁律：中断里只做「把字节搬进环形缓冲、置一个标志」这种几微秒的事。解析帧、算 CRC、写 Flash、刷 OLED、printf 一律放任务里。原因有三条：中断里不能阻塞（互斥量、队列的阻塞版本都不能用）；中断执行时间越长，越容易丢掉后续的中断（串口 FIFO 只有几个字节）；Flash 擦除这类操作耗时以毫秒计，放在中断里会让整个系统的中断响应彻底瘫痪。\n\n本工程的串口接收就是这样分层的：sys_usart.c 里的 rx_buf / rx_head / rx_tail 全是 static volatile，ISR 只搬字节 + 推进 head，任务侧用 SYS_FRAME_Poll 之类的接口去消费。而且库提供了一个明确的选择，要么用 InitRxIT + Poll 的库方式（不要自己再写 USARTx_IRQHandler），要么用自己的 ISR 但每个字节调 SYS_FRAME_Feed，两种喂法二选一，绝对不能同时用，否则会互相抢字节。",
   "① 加锁验证：让两个任务同时高频访问 I2C1（一个刷 OLED、一个读 MPU6050），跑几个小时看是否出现花屏或异常读数；去掉 SYS_I2C_Lock 再跑一次，通常几十分钟就能复现。\n\n② volatile 验证：在未加 volatile 的变量上写一个「任务里轮询、ISR 里置位」的循环，用板2（-O3）编译，会看到循环永不退出；加上 volatile 立刻正常。这是最直观的一课。\n\n③ ISR 边界验证：在串口 ISR 里临时加一句 printf，观察在 115200 下高频收发时是否开始丢字节；把 printf 挪回任务里，丢字节消失。\n\n④ 锁内代价验证：用 DWT 测一次 W25QXX_LogWrite 的耗时（约 1ms，因为槽位预先擦好），再测一次整扇区擦除（约 50ms），体会为什么这里用互斥量而不是关中断。",
   "/* I2C：库已经把锁做进公共入口，内部是递归互斥量 */\nvoid i2c_refresh_oled(void)\n{\n    SYS_I2C_Lock(SYS_I2C_1);        /* 多次事务保持原子 */\n    /* ... 若干次 SYS_I2C_Write / SYS_I2C_Read ... */\n    SYS_I2C_Unlock(SYS_I2C_1);\n}\n\n/* SPI1：库不提供锁，应用层自己拿一把互斥量保护总线 */\nstatic SemaphoreHandle_t s_spi1_mtx;\nvoid spi1_write_locked(const uint8_t *buf, uint16_t len)\n{\n    (void)xSemaphoreTake(s_spi1_mtx, portMAX_DELAY);\n    SYS_SPI_Write(SYS_SPI_1, buf, len);\n    (void)xSemaphoreGive(s_spi1_mtx);\n}\n\n/* W25QXX 日志内部的锁（w25qxx_log.c 的做法）：递归互斥量 */\n#define W25QXX_LOG_LOCK()    log_lock()\n#define W25QXX_LOG_UNLOCK()  log_unlock()\n/* 中断上下文或调度器未启动时不取锁；持锁只挡同样用本模块的任务，不再挂调度器 */\n\n/* ISR 与任务共享的变量：volatile 是底线，-O3 下没有它就会被优化掉 */\nstatic volatile uint8_t  s_rx_flag = 0U;\nstatic volatile uint16_t s_rx_head = 0U;\n\nvoid USART1_IRQHandler(void)\n{\n    /* 只搬字节、只置标志，别的都交给任务 */\n    /* if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) { ... } */\n}\n\n/* 任务侧消费：volatile 保证每次都真的去内存读 */\nwhile (s_rx_flag == 0U) { /* 等待中断置位 */ }",
   "两板同时跑 I2C 高压测试（板1 刷 OLED、板2 读 MPU6050），持续几十分钟不出现花屏或异常读数，说明锁是有效的。\n\nvolatile 的效果用板2 最明显：写一段「未加 volatile 的标志位轮询」在 -O3 下会死循环，加上 volatile 立刻正常，这个对比本身就是最好的验证。\n\nISR 边界用串口压测：115200 下连续发几 KB，在 ISR 里加 printf 会丢字节，挪回任务里就不丢。",
   "① 以为「库会帮我管 SPI」：sys_spi.c 里一行互斥代码都没有，SPI1 上两个器件的互斥必须应用层自己做。\n\n② ISR 与任务共享变量漏写 volatile，且只在 -O1 的板1 上测过就以为没问题：板2 是 -O3，一上去就是死循环。\n\n③ 把 volatile 当锁用：它只保证「每次都去内存读」，不保证读改写是原子的，s_count++ 这种依然会丢更新，要配临界区。\n\n④ 以为改成互斥量后持锁期间可以随便慢：擦除仍在锁内（约 50ms），别在实时任务里干等；要整片清空用 W25QXX_LogClearStep() 分步做。\n\n⑤ 环形缓冲免锁的前提「单写者」被破坏：后来加了个定时器回调也往同一个环里写，免锁立刻失效，出现读到的数据半新半旧。\n\n⑥ 在 ISR 里调 I2C 或 Flash：前者要等总线且互斥量不能在中断里取，后者耗时以毫秒计，两个都会让系统崩。",
   "㉖ 本卡「阶段六/阶段七」；`sys_i2c.c` 的锁策略注释与递归互斥量；`sys_spi.c`（对比它没有锁）；`w25qxx_log.c` 顶部的递归互斥量锁说明；`sys_usart.c` 的 static volatile 环形缓冲"],
  ["⑧ 阶段四续：看门狗与硬件阻塞时间，心跳汇总才是正解",
   "独立看门狗 IWDG 的时钟源是内部的 LSI（约 32kHz RC 振荡器），完全不依赖主时钟，这意味着即使 PLL 配错、主频没起来，它照样计数，是最可靠的最后一道保险。本工程的最大超时约 32.8 秒（预分频 256 × 重装载 4096 / 32kHz），SYS_WDG_Init 内部会把超过上限的值钳位。\n\n超时该定多少，取决于系统里最长的连续阻塞时间。sys_wdg.h 给了一张很实用的对照表：毫秒级的轮询（LED、按键、串口收发、DWT 延时）给 1s 就够；整屏刷新、大块 SPI / Flash 连续写这种百毫秒级的给 2s；而秒级的 Flash 扇区擦除（128K 扇区的擦除时间可以到约 1s），以及等待外部事件的操作，必须给 ≥3s。本工程取 4000ms，正是因为监控任务 1s 一轮、而缓存落盘路径里可能出现扇区擦除。\n\n更本质的一点：每个任务不能各自喂狗。假设通信任务卡死在一个死循环里，而采集任务还在正常跑、还在喂狗，看门狗看到有人在喂，就永远不会复位，整个系统带着一个死掉的功能继续跑下去，这比直接复位更危险。所以正确做法是「心跳汇总」：每个任务周期性地报一次到位（SYS_WDG_Heartbeat(id)），只有监控任务在确认全员到齐之后才真正喂狗（SYS_WDG_HeartbeatPoll() 返回非 0 时它内部已经喂了）。任何一个任务卡死，位就凑不齐，看门狗把整块板复位，这才是看门狗真正管用的用法。\n\n还有一个时序问题：开狗的时机。本工程把 SYS_WDG_Init 放在所有任务创建完、vTaskStartScheduler 之前。如果提前到 main 开头就开，那么后面十几秒的 DHT11 / LoRa 自检还在跑、还没人喂狗，看门狗会直接把板子咬复位，现象是「程序怎么都跑不起来」。\n\n关于喂狗的另一个细节：sys_wdg.h 记录了一个修复过的经典 bug，旧版在 SYS_WDG_HEARTBEAT_COUNT = 0 时，HeartbeatPoll 直接 return 0，根本不喂狗，而库的注释却让主循环「只留这一句 HeartbeatPoll()」，照文档写就是上电复位循环。修复后无论 COUNT 配成几，主循环里只留 HeartbeatPoll 都是安全的。本工程故意不改库里的 COUNT（那是编译期常量，改了要重编库），只在自己文件里用 APP_TASK_COUNT 模拟报到。\n\n心跳位最多 32 个，sys_wdg.c 里有一行编译期断言 typedef char wdg_hb_cnt_check[(SYS_WDG_HEARTBEAT_COUNT <= 32U) ? 1 : -1]; 卡着，超了直接编译报错。\n\n最后是调试期的一个便利设置：SYS_WDG_DEBUG_FREEZE = 1 让调试器暂停 CPU 时看门狗冻结，在 Keil 里打断点、单步不会把板子复位。\n\n一提 sys_tick 的 T1 修复也是同一类问题的另一个面：旧版 sys_tick.c 用自己的 SysTick_Handler 累加 systick_ms，但这个弱定义被 FreeRTOS 的 port.c 强定义顶替了，于是 systick_ms 恒为 0；SYS_TICK_GetTick() 返回 0，而 SYS_TICK_Delay_ms 的判据是 (SYS_TICK_GetTick() - start) < ms，即 (0 - 0) < ms 永真，于是变成永久死等。修复方式是 RTOS 模式下直接读内核节拍 xTaskGetTickCount()。",
   "① 数心跳：确认每个任务都调了 SYS_WDG_Heartbeat(id)，且 id 在 0 ~ APP_TASK_COUNT-1 范围内不重复；监控任务自己用 APP_TASK_COUNT-1。② 看监控任务的输出：正常情况下永远不打印「有任务没报到」；Pending 应该是 0。\n\n③ 故意制造一次死任务：在某个任务里加 while(1){}（不喂狗），观察是否在 4 秒左右被复位，并且下次上电 01 复位原因应该报 IWDG。这是对整套机制最彻底的验收。",
   "/* 心跳汇总：各任务报到，监控任务确认全员到齐才喂狗 */\n#define APP_TASK_COUNT      2           /* 板2：LoraRx / Forward */\n#define APP_WDG_TIMEOUT_MS  4000U\n\n/* 业务任务：干完一轮活就报个到 */\nSYS_WDG_Heartbeat(0U);\n\n/* 监控任务：自己报最后一位，然后看是否全员到齐 */\nvTaskDelay(pdMS_TO_TICKS(1000));\nSYS_WDG_Heartbeat((uint8_t)(APP_TASK_COUNT - 1U));\nif (SYS_WDG_HeartbeatPoll() == 0U) {\n    (void)SYS_USART_Printf(DBG, \"[监控] 有任务没报到：Pending = %08X\",\n                           (unsigned)SYS_WDG_HeartbeatPending());\n}\n\n/* 开狗必须晚于所有阻塞式自检，早于 vTaskStartScheduler */\nSYS_WDG_Init(APP_WDG_TIMEOUT_MS);\nSYS_USART_Printf(DBG, \"独立看门狗已启动，超时 %u ms\",\n                 (unsigned)APP_WDG_TIMEOUT_MS);\nvTaskStartScheduler();\n\n/* 复位原因能告诉上次是不是被狗咬的 */\nif ((SYS_WDG_ResetCause() & SYS_WDG_RST_IWDG) != 0U) {\n    SYS_USART_SendLine(DBG, \"上次是独立看门狗复位\");\n    SYS_WDG_ClearResetFlags();\n}",
   "正常运行时监控任务永远不打印「有任务没报到」。破坏性验证：在某任务里写 while(1){}，应在 4 秒左右复位；重新上电后 01 复位原因应打印出 IWDG 字样，且 SYS_WDG_ResetCauseDecode 的结果里包含 IWDG。\n\n再验一次开狗时机：把 SYS_WDG_Init 挪到 main 开头，会看到板子在自检途中就复位，根本走不到调度器。",
   "① 每个任务各自喂狗：这是最危险也最常见的错误，一个任务卡死会被别的任务掩盖，系统带着故障继续跑。\n\n② 超时只按「主循环一圈多快」估算，不考虑 Flash 扇区擦除那 1 秒左右的硬件阻塞：擦除期间 CPU 取指被暂挂，喂狗语句根本执行不到，于是正常操作也会被咬复位。\n\n③ SYS_WDG_Init 放在 main 开头：后面十几秒自检还没人喂狗，直接复位。\n\n④ 忘了 SYS_WDG_HEARTBEAT_COUNT 的模式区别：配成 0 和配成任务数是两种语义，改动前先看 sys_wdg.h 的说明。\n\n⑤ 断电重启后不看复位原因：明明是被狗咬的（说明有任务卡死），却去怀疑电源或晶振问题，方向全错。\n\n⑥ 心跳 id 超出范围或重复：越界的 id 会被库忽略，某个任务实际上没被纳入监控，等于给自己留了一个盲区。",
   "㉖ 本卡「阶段五/阶段六」；`sys_wdg.h` 的喂狗周期对照表与复位原因位定义、`SYS_WDG_DEBUG_FREEZE`；`sys_tick.h` 的 T1 修复；FreeRTOSConfig.h 的 configUSE_IDLE_HOOK 0 / configUSE_MALLOC_FAILED_HOOK 1"],
  ["⑨ 阶段七：闭环联调，分层排错，每一层都要有可看的证据",
   "双板联调最忌讳「两板一起上电、一起看，出问题不知道怪谁」。正确顺序是分层推进，每层都要有一个明确的可观测证据，一层没过绝不往下走。\n\n第一层：两块板各自单独跑，把 01~09 的自检跑到全 OK。这一层验的是各自的外设，跟另一块板无关。\n\n第二层：单板 LoRa 自发自收。板1 只管发、没有接收任务，所以真正能验自发自收的是板2，临时写一个发送循环，让它发给自己。这一步验的是 LoRa 模块本身、波特率、模式切换是否正常，不涉及对端。\n\n第三层：两板同时上电，板2 应该每 2 秒打印一行 [接收]。如果板1 的发出计数在涨而板2 一行 [接收] 都没有，第一步查的并非代码，而是两端的网络 ID / 信道 / 地址三项是否完全相同，E22 在透传模式下，这三项任意一项不同就永远收不到，而且不会有任何报错。\n\n第四层：板2 单独的 10 号 MQTT 判活。连上 broker 之后把网线拔掉，什么都别动等 2 分钟，KeepAliveService 必须返回 8（MQTT_ERR_ALIVE_TIMEOUT）、IsConnected 必须变成 0。这一步不过，后面的「断网 → 落盘」判据就是假的：链路早就死了，程序却以为还连着，数据就在「发送成功」的假象里静默丢失。这也是本次 liveness 修复要解决的真正问题，旧版只发 PINGREQ 不看 PINGRESP，TCP 半开（拔网线、路由器重启、运营商清 NAT 表）时本地 socket 收不到任何通知、AT 也不报错，IsConnected 永远返回 1。\n\n第五层：11 闭环。板1 每 2 秒发一帧，板2 收到后落进 W25QXX 缓存（也能改成联网时直接上报）。这一层要同时看四路证据：板1 的发出计数、板2 的接收计数与坏帧计数、板2 打印的 Flash 缓存条数、MQTT 侧收到的 JSON。四个数一起看，问题立刻落到某一段。\n\n第六层：断网落盘与补传。拔掉板2 的网线，帧应该转进 Flash 缓存；恢复网络后，从最旧一条开始补传。这里必须做一次断电重启验证：上电后 W25QXX_LogCount 应该保持不断电前的条数，能读出之前写的内容，掉电不丢才是这个模块存在的意义。\n\n还有一条贯穿全程的排错原则：看计数器，不要靠猜。本工程在每个环节都埋了计数，s_tx_ok / s_tx_fail、s_rx_ok / s_rx_bad、s_cache_ok / s_cache_fail、SYS_FRAME_ErrCount、W25QXX_LogLost、SYS_WDG_HeartbeatPending。哪一段的计数不动，问题就在那一段。",
   "① 板1 看「发出 / 失败」、板2 看「收到 / 坏帧」两对计数是否都在稳定增长。② 板2 每收到一帧应打印一行 [接收]，且打印的未上传条数（W25QXX_LogCount）随帧数上升。\n\n③ MQTT 侧用任意客户端订阅 data/f407-002，确认能收到 JSON 且内容与板1 的 DHT11 读数一致。④ 拔板2 网线：帧应转缓存、条数继续涨；恢复网络后条数应逐渐回落（补传成功）。\n\n⑤ 断电重启板2：上电后 count 不应归零，且能读出重启前写的内容。",
   "/* 板1：监控任务打印发送侧证据 */\nSYS_USART_Printf(DBG, \"[监控] tick=%u 发出 %u 帧 / 失败 %u 次\",\n                 (unsigned)SYS_TICK_GetTick(),\n                 (unsigned)s_tx_ok, (unsigned)s_tx_fail);\n\n/* 板2：接收侧证据（坏帧单独计数，直接反映链路质量） */\nSYS_USART_Printf(DBG, \"[监控] tick=%u 收到 %u 帧 / 坏帧 %u 个\",\n                 (unsigned)SYS_TICK_GetTick(),\n                 (unsigned)s_rx_ok, (unsigned)s_rx_bad);\n\n/* 板2：断网落盘 + 未上传条数 */\nif (W25QXX_LogWrite(ts, f, FRAME_TOTAL) == W25QXX_LOG_OK) {\n    SYS_USART_Printf(DBG, \"[缓存] 已落盘，未上传 %u 条\",\n                     (unsigned)W25QXX_LogCount());\n}\n\n/* 板2：网络恢复后从最旧一条开始补传 */\nW25QXX_LogStat(&count, &cap, &head);      /* 丢了多少条另调 LogLost() */\nwhile ((count > 0U) && (MQTT_IsAlive() != 0U)) {\n    /* 缓冲要 PAYLOAD + 1 = 21 字节，读函数会自己写结尾的 0 */\n    uint8_t buf[W25QXX_LOG_PAYLOAD + 1U];\n    uint32_t ts = 0U;\n    uint16_t n  = 0U;\n    if (W25QXX_LogRead(0U, &ts, buf, &n) != W25QXX_LOG_OK) break;\n    if (MQTT_Publish(\"data/f407-002\", buf, n, 0, 0) != 0) break;\n    count--;\n}",
   "四路证据一起看：板1 发出数、板2 收到数与坏帧数、板2 的 Flash 缓存条数、MQTT 客户端收到的 JSON。\n\n判据：发出 ≈ 收到 + 少量空口丢失；坏帧数应该很低（偶尔几个正常，一直涨说明有干扰或粘帧）；缓存条数在断网时涨、联网时回落；MQTT 收到的温度湿度与板1 串口打印的一致。\n\n最后做断电验证：写完若干条断电，重新上电，count 不减，这才是闭环真正成立。",
   "① 两板一起上电联调：出问题时分不清是发送端、空口、还是接收端，排错成本成倍上升。\n\n② 只看到「板2 收到数在涨」就以为通了：接收正常和上报正常是两段，MQTT 侧没数据要单独查上报段。\n\n③ 两端 LoRa 配置不一致（网络 ID / 信道 / 地址），却一直在改代码：这三项不同时是彻底收不到、且不报错，先核对配置再动代码。\n\n④ 不做断电验证就宣布缓存模块 OK：不掉电就验不出「掉电不丢」，而这是缓存存在的全部意义。\n\n⑤ 读日志时缓冲只给 PAYLOAD（20）字节：读函数会往 buf[20] 写结尾的 0，越界一个字节，踩坏相邻变量。\n\n⑥ 只看「有没有数据」不看计数器：s_rx_bad、W25QXX_LogLost、SYS_FRAME_ErrCount 这些计数才是定位问题的关键，数据看着正常时它们已经在报警了。\n\n⑦ 忘了这套程序一次实机都没跑过（只有两板 0 Error / 0 Warning）：第一次上板要留出充裕时间，按 01→09→10→11 一层层来，别指望一把跑通。",
   "㉖ 本卡「阶段七/第一次上板」；⑱ SPI Flash 断网缓存日志；`esp8266.h` 与 `mqtt.h`（含 MQTT_IsAlive / MQTT_KeepAliveService / MQTT_ERR_ALIVE_TIMEOUT）；`lora_e22.h` 的透传模式与 AUX；`sys_frame.h` 的帧协议与三重确认"]
 ]}"""

