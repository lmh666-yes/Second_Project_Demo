#ifndef __FWLIB_HCSR04_H
#define __FWLIB_HCSR04_H

#include "stm32f4xx.h"
#include "gpio_core.h"

/* hcsr04.h: HC-SR04 超声波测距（2cm ~ 400cm）
 * 依赖 gpio_core.h + delay.h，用到 GPIO 工具、delay_us 与 DWT 微秒计时
 *
 * 接线（4 针模块正面朝自己，从左到右）：
 * VCC  接 5V，必须 5V，3.3V 供电测不准甚至不出回波
 * TRIG 空闲 GPIO，本模块由 HCSR04_Init 运行时指定引脚，不用改头文件
 * ECHO 空闲 GPIO
 * GND  接 GND
 * ECHO 输出 5V 电平，STM32F407 绝大多数脚为 5V 容忍(FT)，可直连；需稳妥则在 ECHO 与 MCU 脚之间串 1kΩ、再对地接 2kΩ 分压到 3.3V
 * 模块发射瞬间约 15mA，从板子 5V 排针取电，不从 GPIO 供电
 * 装在金属盒内、贴墙测量或偏角大于 15° 时读数无效
 * 本模块用 DWT 忙等，一次测量最长阻塞 HCSR04_TIMEOUT_US(32ms) */


/* 定义与宏定义区：量程、超时、声速在这里改 */
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


/* 基础功能 */

/* 初始化：指定 TRIG / ECHO 用哪两个脚，内部自动开 GPIO 时钟
 * trig_port/trig_pin 触发脚，配成推挽输出（GPIO_OType_PP），空闲低
 * echo_port/echo_pin 回波脚，配成上拉输入；传感器未插时该脚恒为高，等上升沿立即通过、等下降沿超时，稳定返回 HCSR04_ERR_NO_ECHO
 * 返回 HCSR04_OK / HCSR04_ERR_PARAM */
uint8_t HCSR04_Init(GPIO_TypeDef *trig_port, uint16_t trig_pin,
                    GPIO_TypeDef *echo_port, uint16_t echo_pin);

/* 测一次回波宽度，原始值，单位微秒，不做量程判断
 * 返回 HCSR04_OK / HCSR04_ERR_* */
uint8_t HCSR04_ReadUs(uint32_t *us);

/* 测一次距离，原始值，单位毫米，按当前声速换算，不做量程判断
 * 1 米 ≈ 5800us；换算先把声速降一个量级再相乘，避免 32 位溢出
 * 返回 HCSR04_OK / HCSR04_ERR_* */
uint8_t HCSR04_ReadMm(uint32_t *mm);

/* 测一次距离（厘米），带量程判断
 * 返回 HCSR04_OK 时 *cm 有效（2~400）；HCSR04_ERR_RANGE 表示超出量程，*cm 不可用；其它为 HCSR04_ERR_* */
uint8_t HCSR04_ReadCm(uint16_t *cm);


/* 扩展功能 */

/* 改声速，默认 343400 mm/s（20℃ 干燥空气 ≈ 343.4m/s）
 * 声速随温度变化约 0.6m/s 每 ℃，需精确用 HCSR04_SetTempC10 */
void HCSR04_SetSpeed(uint32_t mm_per_s);

/* 温度补偿：按摄氏度乘 10 自动算声速（如 253 表示 25.3℃）
 * 换算 v(mm/s) = 331400 + 60 × t(℃)；25℃ 时为 346400 */
void HCSR04_SetTempC10(int16_t t_c10);

/* 连测几次取中值，用于抑制偶发野值
 * cm 为出参；times 取 1~9，建议 3 或 5 这类奇数
 * 返回 HCSR04_OK 表示有有效中值，其它表示全部失败
 * 阻塞时间 = times × 最长 32ms，不可放在中断里调用 */
uint8_t HCSR04_ReadCmMedian(uint16_t *cm, uint8_t times);

/* 返回码转中文说明，调试打印用 */
const char *HCSR04_ErrStr(uint8_t err);

#endif /* __FWLIB_HCSR04_H */
