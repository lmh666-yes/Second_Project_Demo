#ifndef __FWLIB_LED_H
#define __FWLIB_LED_H

#include "stm32f4xx.h"

/* led.h — 板载 LED 模块头文件
 * 依赖 : gpio_core.h
 * 标准库 : GPIO_SetBits / GPIO_ResetBits / GPIO_ToggleBits / GPIO_Init / RCC_AHB1PeriphClockCmd
 * 接口 : 只暴露 id（0 ~ LED_COUNT-1），高/低电平由 LED_ACTIVE_LOW 适配；换板只改区块 1 宏 */


/* 区块 1：定义与宏定义区（换板子只改这里） */
/* LEDx_PORT / LEDx_PIN 成对出现，x 即对外暴露的 id；led.c 引脚表引用这些宏 */
#define LED0_PORT   GPIOF
#define LED0_PIN    GPIO_Pin_9
#define LED1_PORT   GPIOF
#define LED1_PIN    GPIO_Pin_10
#define LED2_PORT   GPIOE
#define LED2_PIN    GPIO_Pin_13
#define LED3_PORT   GPIOE
#define LED3_PIN    GPIO_Pin_14

/* LED 数量：合法 id 为 0 ~ LED_COUNT-1；修改后须同步改 led.c 引脚表项数 */
#define LED_COUNT   4

/* 电平极性：1 = 低电平点亮；0 = 高电平点亮
 * 依据原理图：LED 阴极接 IO（IO 拉低导通）→ 1；阳极接 IO（IO 拉高导通）→ 0 */
#define LED_ACTIVE_LOW   1

/* 显示位序，仅影响 LED_ShowHex：0 = bit0→LED0；1 = bit0→LED3 */
#define LED_SHOW_REVERSE 0

/* 编译期自检：LED_ShowHex 按一个字节取位，最多 8 个 LED */
#if (LED_COUNT < 1) || (LED_COUNT > 8)
/* #error 文本必须是 ASCII（AC5 解析中文报错不稳） */
#error "LED_COUNT must be 1..8 (LED_ShowHex is 8-bit based)"
#endif  /* LED_COUNT 范围自检 */


/* 区块 2：基础功能 */
/* 初始化所有 LED：推挽输出并全部熄灭；内部使能端口时钟
 * 标准库 : 经 gpio_core → RCC_AHB1PeriphClockCmd + GPIO_Init + 写电平熄灭 */
void LED_Init   (void);

/* 点亮 / 熄灭 / 翻转指定 LED
 * 参数 : id — 0 ~ LED_COUNT-1；越界直接返回，不写寄存器
 * 标准库 : GPIO_SetBits / GPIO_ResetBits / GPIO_ToggleBits（按极性宏二选一） */
void LED_On     (uint8_t id);
void LED_Off    (uint8_t id);
void LED_Toggle (uint8_t id);


/* 区块 3：扩展功能 */
void LED_AllOn  (void);              /* 点亮全部 LED */
void LED_AllOff (void);              /* 熄灭全部 LED */

/* 按位显示：value 各位对应一个 LED，位为 1 则点亮；位序由 LED_SHOW_REVERSE 决定
 * 例（4 灯、正序）：0x05（0101b）→ LED0、LED2 亮，其余灭 */
void LED_ShowHex(uint8_t value);

/* 点亮 0 号到第 n 号的连续 LED；n 超范围按全部处理；用途：进度条 / 电量格
 * 例(4 灯): LED_OnTo(2) → LED0/LED1/LED2 亮、LED3 灭 */
void LED_OnTo(uint8_t n);

/* 频率 + 占空比闪灯（非阻塞，与定时器中断/软定时器联动）
 * 参数 : period_ms 周期；duty_permille 占空比（‰）
 * 关系 : 频率 = 1000/period_ms (Hz)；亮时长 = period_ms × duty_permille / 1000 (ms)
 *        period=1000、duty=100 → 1Hz 亮 100ms/灭 900ms；duty=1000 常亮；duty=0 常灭
 * 驱动 : 每 1ms 调一次 LED_BlinkUpdate()（软定时器任务或 SYS_TICK 计时） */
void LED_BlinkStart(uint8_t id, uint16_t period_ms, uint16_t duty_permille);
void LED_BlinkStop (uint8_t id);   /* 停止并熄灭 */
void LED_BlinkUpdate(void);        /* 每 1ms 调一次 */

/* 位带直写 LED_BB_On/Off/Toggle 已撤除，位带宏由 sys_bitband.h 提供（如 PFout(9) = 0;）；日常用 LED_On/Off/Toggle */

/* 常用灯效（基于 LED_On / LED_Off 组合）
 * 阻塞式灯效内部用 delay.h 的 delay_ms 粗延时，执行期间占用 CPU；非阻塞版见 LED_FlowStep
 * interval_ms 传 0 时直接返回；阻塞式灯效结束后全部熄灭 */

/* 闪烁：指定 LED 亮 interval_ms → 灭 interval_ms，重复 times 次；总耗时约 2×interval_ms×times（阻塞） */
void LED_Blink   (uint8_t id, uint32_t times, uint32_t interval_ms);

/* 全部闪烁：所有 LED 同步亮灭，重复 times 次 */
void LED_AllBlink(uint32_t times, uint32_t interval_ms);

/* 交替闪烁：偶 id（LED0/2/…）与奇 id（LED1/3/…）两组轮流亮灭，重复 times 次
 * 例（4 灯）：{0,2}亮{1,3}灭 ↔ {0,2}灭{1,3}亮 */
void LED_Alternate(uint32_t times, uint32_t interval_ms);

/* 流水灯（单向）：每次亮 1 个灯，依次移动 0→1→…→N-1，到末尾回卷到 0，共 times 圈
 * 例（4 灯）：LED0 → LED1 → LED2 → LED3 → LED0 → … */
void LED_Flow   (uint32_t times, uint32_t interval_ms);

/* 跑马灯（往返）：每次亮 1 个灯，到末尾折返，"去 + 回"为一趟，共 times 趟
 * 例（4 灯）：LED0 → LED1 → LED2 → LED3 → LED2 → LED1 → LED0 → … */
void LED_Marquee(uint32_t times, uint32_t interval_ms);

/* 流水单步（非阻塞）：调用一次点亮下一格并返回其 id
 * dir > 0 向前（id 增大方向）；dir < 0 向后；到端点回卷；只亮当前位置的灯，其余熄灭
 * 位置从 0 出发（首次调用点亮 LED0）；LED_Init() 复位为 0
 * 用途 : 节奏交给调用方主循环，如
 *        if (SYS_TICK_Timeout(t, 200)) { t = SYS_TICK_GetTick(); LED_FlowStep(1); } */
uint8_t LED_FlowStep(int8_t dir);

#endif /* __FWLIB_LED_H */

