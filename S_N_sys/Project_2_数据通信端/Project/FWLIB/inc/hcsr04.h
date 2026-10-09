#ifndef __FWLIB_HCSR04_H
#define __FWLIB_HCSR04_H

#include "stm32f4xx.h"
#include "gpio_core.h"

/* ================================================================
 *  hcsr04.h —— 【传感器】HC-SR04 超声波测距（2cm ~ 400cm）
 * ================================================================
 *  设计定位 : 小车上"前面有没有墙"、智能家居"人靠多近"——
 *             需要非接触测距就用它。便宜、好使、不用编解码协议。
 *             面向【智能环境监测小车 / 智能家居终端】。
 *  依赖     : gpio_core.h + delay.h（GPIO 工具 + delay_us + DWT 微秒计时）
 *  标准库关键词 : RCC_AHB1PeriphClockCmd / GPIO_Init / GPIO_SetBits /
 *                 GPIO_ReadInputDataBit（后两者经 gpio_core 封装）
 *
 *  【接线（4 针模块正面朝自己，从左到右）】
 *      VCC  —— 5V（**必须 5V**，3.3V 测不准甚至不出回波）
 *      TRIG —— 随便找个空闲 GPIO（本模块 **引脚运行时指定**，不用改头文件）
 *      ECHO —— 随便找个空闲 GPIO
 *      GND  —— GND
 *
 *      ⚠ ECHO 输出的是 **5V 电平**！STM32F407 绝大多数脚是 5V 容忍(FT)，
 *        直连能用（本库的验证工程就是这么测的）；要绝对稳妥就在 ECHO 和
 *        MCU 脚之间串一个 1kΩ、再对地接一个 2kΩ 分压到 3.3V。
 *      ⚠ 模块发射瞬间要吃 15mA 左右，从板子 5V 排针取电，别从 GPIO 供电。
 *      ⚠ 装在金属盒里 / 贴着墙测 / 角度偏 >15°，都会读到 0 或乱跳，属正常。
 *
 *  【使用方式】
 *      HCSR04_Init(GPIOE, GPIO_Pin_0, GPIOE, GPIO_Pin_1);   // TRIG=PE0 ECHO=PE1
 *
 *      uint16_t cm;
 *      if (HCSR04_ReadCm(&cm) == HCSR04_OK) {
 *          printf("[US] %u cm\r\n", cm);
 *      }
 *
 *  移植指引 : 换引脚只改 HCSR04_Init 两个参数；
 *             量程/超时改下面区块 1；要温度补偿调 HCSR04_SetTempC10()；
 *             要接到定时器输入捕获请自行改 —— 本模块用 DWT 忙等，
 *             一次测量最长阻塞 HCSR04_TIMEOUT_US(32ms)，逻辑很简单，
 *             换成输入捕获能非阻塞但复杂得多，看需求取舍。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换场景只改这里）
 * ================================================================ */
#define HCSR04_TRIG_PULSE_US    12U         /* 触发脉冲宽度，手册要求 >=10us */
#define HCSR04_TIMEOUT_US       32000U      /* 等回波超时：32ms ≈ 5.5m，超 4m 量程就报错 */
#define HCSR04_MIN_CM           2U          /* 量程下限，小于它算无效 */
#define HCSR04_MAX_CM           400U        /* 量程上限，大于它算无效 */
#define HCSR04_DEFAULT_SPEED    343400UL    /* 声速 mm/s（20℃ 干燥空气 ≈ 343.4m/s） */
#define HCSR04_MEDIAN_MAX       9U          /* 中值滤波最多几次 */

/* 返回码（全库统一：0 成功） */
#define HCSR04_OK               0U          /* 成功 */
#define HCSR04_ERR_PARAM        1U          /* 空指针 */
#define HCSR04_ERR_NO_ECHO      2U          /* 没等到回波：超距 / 角度偏 / 线没插好 */
#define HCSR04_ERR_RANGE        3U          /* 有回波但超出 2~400cm 量程 */
#define HCSR04_ERR_NOT_INIT     4U          /* 忘了先 HCSR04_Init */


/* ================================================================
 *                        区块 2：基础功能
 * ================================================================ */

/* 【初始化】指定 TRIG / ECHO 用哪两个脚（内部自动开 GPIO 时钟）
 * 参数 : trig_port/trig_pin —— 触发脚，配成推挽输出（GPIO_OType_PP），空闲低
 *        echo_port/echo_pin —— 回波脚，配成上拉输入
 *                              （故意用上拉：传感器没插时该脚恒为高，
 *                                于是"等上升沿"立刻过、"等下降沿"超时，
 *                                稳定返回 HCSR04_ERR_NO_ECHO，不会瞎报数）
 * 返回 : HCSR04_OK / HCSR04_ERR_PARAM
 * 示例 : HCSR04_Init(GPIOE, GPIO_Pin_0, GPIOE, GPIO_Pin_1); */
uint8_t HCSR04_Init(GPIO_TypeDef *trig_port, uint16_t trig_pin,
                    GPIO_TypeDef *echo_port, uint16_t echo_pin);

/* 【测一次回波宽度】原始值，单位微秒，不做量程判断
 * 返回 : HCSR04_OK / HCSR04_ERR_*
 * 示例 : uint32_t us; if (HCSR04_ReadUs(&us) == HCSR04_OK) ... */
uint8_t HCSR04_ReadUs(uint32_t *us);

/* 【测一次距离】原始值，单位毫米，按当前声速换算，不做量程判断
 * 说明 : 1 米 ≈ 5800us；算的时候先除 1000 避免 32 位溢出
 * 返回 : HCSR04_OK / HCSR04_ERR_* */
uint8_t HCSR04_ReadMm(uint32_t *mm);

/* 【测一次距离（厘米）★最常用】带量程判断
 * 返回 : HCSR04_OK        *cm 有效（2~400）
 *        HCSR04_ERR_RANGE  超出量程，*cm 别用
 *        其它              HCSR04_ERR_*
 * 示例 : uint16_t cm;
 *        if (HCSR04_ReadCm(&cm) == HCSR04_OK) { 用 cm }
 *        else { 当作"前方无遮挡"处理 } */
uint8_t HCSR04_ReadCm(uint16_t *cm);


/* ================================================================
 *                        区块 3：扩展功能
 * ================================================================ */

/* 【改声速】默认 343400 mm/s（20℃）
 * 说明 : 声速随温度变化约 0.6m/s per ℃，要精确就用下面那个 */
void HCSR04_SetSpeed(uint32_t mm_per_s);

/* 【温度补偿】按摄氏度×10 自动算声速（如 253 = 25.3℃）
 * 换算 : v(mm/s) = 331400 + 60 × t(℃)；25℃ → 346400
 * 示例 : HCSR04_SetTempC10(253);    // 环境 25.3℃
 * 说明 : 温度可以用板上的 DS18B20 / DHT11 读到，一举两得 */
void HCSR04_SetTempC10(int16_t t_c10);

/* 【连测几次取中值】超声波偶尔会窜一个野值，中值滤波最省事
 * 参数 : cm —— 出参；times —— 测几次（1~9，建议 3 或 5，**传奇数**）
 * 返回 : HCSR04_OK 有有效中值；其它 全失败
 * 示例 : HCSR04_ReadCmMedian(&cm, 3);   // 测 3 次取中间值
 * ⚠ 阻塞时间 = times × 最长 32ms，别放中断里、别在主循环里连轴转 */
uint8_t HCSR04_ReadCmMedian(uint16_t *cm, uint8_t times);

/* 【返回码转中文说明】调试打印用
 * 示例 : printf("[US] %s\r\n", HCSR04_ErrStr(HCSR04_ReadCm(&cm))); */
const char *HCSR04_ErrStr(uint8_t err);

#endif /* __FWLIB_HCSR04_H */
