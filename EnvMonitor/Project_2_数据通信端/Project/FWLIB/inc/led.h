#ifndef __FWLIB_LED_H
#define __FWLIB_LED_H

#include "stm32f4xx.h"

/* 板载 LED 抽象层：只提供点亮与熄灭语义
 * 依赖 gpio_core.h；点亮电平极性由 LED_ACTIVE_LOW 决定，函数内自动适配
 * 对外只暴露 id（0 ~ LED_COUNT-1），调用者不需要知道端口与引脚
 * 移植：只改下面的引脚宏与数量宏；
 * 增删数量需同步修改 led.c 的引脚表 */


/* 引脚定义：LEDx_PORT / LEDx_PIN 成对出现，x 为对外暴露的 id
 * led.c 的引脚表引用这些宏，修改引脚只改这里
 *
 * 天马 F407 开发板（对照《普中-天马 F407开发板原理图》）：
 * 板上只有 2 只用户指示灯（丝印 DS0 / DS1），阴极接 IO：
 *   DS0 → PF9（id 0）
 *   DS1 → PF10（id 1）
 * 天马板上 PE13/PE14 是 FSMC_D10/FSMC_D11（SRAM/LCD 数据线），不可用作 LED，
 * 故 LED_COUNT = 2。另有"LED跑马灯"模块 J10（LED1~LED8）经 ULN2003/排针外接，
 * 不在本模块管辖范围内，需要时用 gpio_core 驱动。 */
#define LED0_PORT   GPIOF
#define LED0_PIN    GPIO_Pin_9
#define LED1_PORT   GPIOF
#define LED1_PIN    GPIO_Pin_10

/* LED 数量，合法 id 为 0 ~ LED_COUNT-1
 * 修改后必须同步修改 led.c 的引脚表，项数保持一致 */
#define LED_COUNT   2

/* 点亮电平极性：1 = 低电平点亮；0 = 高电平点亮
 * 判定依据是原理图：LED 阴极接 IO（IO 拉低导通）填 1，阳极接 IO 填 0 */
#define LED_ACTIVE_LOW   1

/* 仅影响 LED_ShowHex 的位映射：0 = 正序（bit0→LED0）；1 = 反序（bit0→LED1） */
#define LED_SHOW_REVERSE 0

/* 编译期自检：配置错误尽早暴露在编译阶段
 * LED_ShowHex 以一个字节的位为数据源，最多支持 8 个 LED */
#if (LED_COUNT < 1) || (LED_COUNT > 8)
/* #error 文本必须是 ASCII，AC5 对中文报错信息解析不稳 */
#error "LED_COUNT must be 1..8 (LED_ShowHex is 8-bit based)"
#endif  /* LED_COUNT 范围自检 */


/* 初始化所有 LED：配置为推挽输出并全部熄灭，内部使能端口时钟
 * 经 gpio_core 调用 RCC_AHB1PeriphClockCmd + GPIO_Init + 写电平熄灭 */
void LED_Init   (void);

/* 点亮 / 熄灭 / 翻转指定 LED
 * id 取值 0 ~ LED_COUNT-1，超范围时直接返回，不操作寄存器
 * 底层为 GPIO_SetBits / GPIO_ResetBits / GPIO_ToggleBits，按极性宏二选一 */
void LED_On     (uint8_t id);
void LED_Off    (uint8_t id);
void LED_Toggle (uint8_t id);


void LED_AllOn  (void);              /* 点亮全部 LED */
void LED_AllOff (void);              /* 熄灭全部 LED */

/* 按位显示：value 的每个二进制位对应一个 LED，位为 1 则点亮
 * 位序由 LED_SHOW_REVERSE 决定 */
void LED_ShowHex(uint8_t value);

/* 点亮 0 号到第 n 号的连续 LED，n 超范围时按全部处理 */
void LED_OnTo(uint8_t n);

/* ----------------------------------------------------------------
 * 频率 + 占空比闪灯（非阻塞，需由 1ms 周期任务调用 LED_BlinkUpdate）
 * ----------------------------------------------------------------
 * period_ms 为亮灭一个周期的毫秒数，duty_permille 为千分比占空比
 * 闪烁频率 = 1000 / period_ms (Hz)，点亮时长 = period_ms × duty_permille / 1000
 * duty_permille = 1000 时常亮，0 时常灭
 * 调用方式：主循环用 SYS_TICK 计时，或放进软定时器/定时器中断
 * （先例：sys_softimer 的 1ms 任务里调 LED_BlinkUpdate）*/
void LED_BlinkStart(uint8_t id, uint16_t period_ms, uint16_t duty_permille);
void LED_BlinkStop (uint8_t id);   /* 停止并熄灭 */
void LED_BlinkUpdate(void);        /* 每 1ms 调一次 */

/* 位带直写版（LED_BB_On/Off/Toggle）已撤除；
 * 位带操作统一由独立文件 sys_bitband.h 提供宏（如 PFout(9) = 0;） */

/* ----------------------------------------------------------------
 * 常用灯效（基于区块 2 的 LED_On / LED_Off 组合实现）
 *
 * 1) 下列阻塞式灯效内部用 delay.h 的粗延时 delay_ms，执行期间占用 CPU；
 *    需要边跑灯效边干别的用非阻塞的 LED_FlowStep
 * 2) interval_ms 传 0 时直接返回
 * 3) 阻塞式灯效结束后全部熄灭
 * ---------------------------------------------------------------- */

/* 闪烁：指定 LED 亮 interval_ms 后灭 interval_ms，重复 times 次
 * 总耗时约 2×interval_ms×times（阻塞） */
void LED_Blink   (uint8_t id, uint32_t times, uint32_t interval_ms);

/* 全部闪烁：所有 LED 同步亮灭，重复 times 次 */
void LED_AllBlink(uint32_t times, uint32_t interval_ms);

/* 交替闪烁：偶 id（LED0/2/…）与奇 id（LED1/3/…）两组轮流亮灭，重复 times 次 */
void LED_Alternate(uint32_t times, uint32_t interval_ms);

/* 流水灯（单向）：每次只亮 1 个灯并依次移动 0→1→…→N-1，
 * 到末尾后回卷到 0 继续，共跑 times 圈 */
void LED_Flow   (uint32_t times, uint32_t interval_ms);

/* 跑马灯（往返）：每次只亮 1 个灯，走到末尾后折返回来，
 * 去 + 回为一趟，共跑 times 趟 */
void LED_Marquee(uint32_t times, uint32_t interval_ms);

/* 流水单步（非阻塞）：调用一次，点亮下一格并返回其 id
 * dir > 0 向前（id 增大方向）；dir < 0 向后；到端点自动回卷
 * 只亮当前位置的灯，其余熄灭
 * 内部位置从 0 出发（首次调用点亮 LED0），LED_Init() 会把位置复位为 0 */
uint8_t LED_FlowStep(int8_t dir);

#endif /* __FWLIB_LED_H */

