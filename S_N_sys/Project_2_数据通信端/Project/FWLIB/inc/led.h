#ifndef __FWLIB_LED_H
#define __FWLIB_LED_H

#include "stm32f4xx.h"

/* ================================================================
 *  led.h —— 【板载】LED 模块  头文件
 * ================================================================
 *  设计定位 : 板载 LED 抽象层（薄封装：只做"点亮/熄灭"语义）
 *  依赖     : gpio_core.h（底层 GPIO 工具）
 *  标准库关键词 : GPIO_SetBits / GPIO_ResetBits / GPIO_ToggleBits / GPIO_Init / RCC_AHB1PeriphClockCmd
 *  电平极性 : 由 LED_ACTIVE_LOW 决定，函数内部自动适配
 *
 *  使用方式 :
 *      LED_Init();       // ① 初始化（上电全灭）
 *      LED_On(0);        // ② 点亮 0 号灯
 *
 *  对外约定 :
 *      - 只暴露 id（0 ~ LED_COUNT-1），调用者无需知道端口/引脚；
 *      - 业务代码永远使用"点亮/熄灭"语义，不关心高电平还是低电平。
 *
 *  移植指引 :
 *      换板子只改"区块 1"的宏；引脚数量不变时 led.c 也不用动
 *      （增删数量需同步修改 led.c 的引脚表，详见 led.c 注释）。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* -------------------- LED 引脚 -------------------- */
/* 格式：LEDx_PORT / LEDx_PIN 成对出现，x 即对外暴露的 id
 * 修改引脚：只改这里（led.c 的引脚表引用这些宏，自动跟随）
 *
 * 【天马 F407 开发板 · 对照《普中-天马 F407开发板原理图》】
 *   板上只有 2 只用户指示灯（丝印 DS0 / DS1），阴极接 IO：
 *     DS0 → PF9（对应本库 id 0）
 *     DS1 → PF10（对应本库 id 1）
 *   ⚠ 换板子的注意：原参考板把 PE13/PE14 也当 LED 用，
 *     天马板上 PE13/PE14 是 FSMC_D10/FSMC_D11（SRAM/LCD 数据线），
 *     不可用作 LED，故本板 LED_COUNT = 2。
 *   另有"LED跑马灯"模块 J10（LED1~LED8），走 ULN2003/排针外接，
 *   不在本模块管辖范围内（需要时自行用 gpio_core 驱动）。 */
#define LED0_PORT   GPIOF
#define LED0_PIN    GPIO_Pin_9
#define LED1_PORT   GPIOF
#define LED1_PIN    GPIO_Pin_10

/* -------------------- 数量常量 -------------------- */
/* LED 数量：合法 id 为 0 ~ LED_COUNT-1
 * ⚠ 修改后必须同步修改 led.c 的引脚表（项数要保持一致） */
#define LED_COUNT   2

/* -------------------- 电平极性 -------------------- */
/* 1 = 低电平点亮；0 = 高电平点亮
 * 判断方法（看原理图）：
 *   LED 阴极接 IO（IO 拉低导通）→ 低电平点亮 → 填 1
 *   LED 阳极接 IO（IO 拉高导通）→ 高电平点亮 → 填 0 */
#define LED_ACTIVE_LOW   1

/* -------------------- 显示位序 -------------------- */
/* 仅影响 LED_ShowHex 的位映射：
 *   0 = 正序（bit0→LED0）；1 = 反序（bit0→LED1） */
#define LED_SHOW_REVERSE 0

/* -------------------- 编译期自检 -------------------- */
/* 把配置错误尽早暴露在编译阶段：
 * LED_ShowHex 以一个字节的位为数据源，最多支持 8 个 LED */
#if (LED_COUNT < 1) || (LED_COUNT > 8)
/* 注意 : #error 文本必须是 ASCII（AC5 对中文报错信息解析不稳） */
#error "LED_COUNT must be 1..8 (LED_ShowHex is 8-bit based)"
#endif  /* LED_COUNT 范围自检 */


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化所有 LED：配置为推挽输出并全部熄灭；内部自动使能端口时钟
 * 标准库 : 经 gpio_core → RCC_AHB1PeriphClockCmd + GPIO_Init（推挽输出）+ 写电平熄灭 */
void LED_Init   (void);

/* 点亮 / 熄灭 / 翻转 指定 LED
 * 参数 : id —— 0 ~ LED_COUNT-1
 * 越界保护：id 超范围时直接返回，不操作任何寄存器
 * 标准库 : GPIO_SetBits / GPIO_ResetBits / GPIO_ToggleBits（按极性宏二选一）
 * 示例 : LED_On(0);              // 点亮 0 号灯(本板 = DS0)
 *        LED_Toggle(1);          // 1 号灯翻转(闪烁用)
 * 扩展提示 : 增删灯 —— 区块 1 加 LEDx_PORT/PIN 宏并改 LED_COUNT,再同步
 *            led.c 引脚表;新灯效 —— 参照 LED_Flow:用 LED_On/Off + Delay_ms 组合 */
void LED_On     (uint8_t id);
void LED_Off    (uint8_t id);
void LED_Toggle (uint8_t id);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
void LED_AllOn  (void);              /* 点亮全部 LED */
void LED_AllOff (void);              /* 熄灭全部 LED */

/* 按位显示：value 的每个二进制位对应一个 LED，位为 1 则点亮
 * 位序由 LED_SHOW_REVERSE 决定（见区块 1）
 * 示例（本板 2 灯、正序）：0x01（01b）→ LED0 亮；0x03（11b）→ 全亮 */
void LED_ShowHex(uint8_t value);

/* ----------------------------------------------------------------
 * 位带直写版本（与区块 2 的 LED_On / LED_Off / LED_Toggle 结果相同，实现不同）
 * ----------------------------------------------------------------
 * 库函数版：查"端口 + 掩码"表 → 调 GPIO_SetBits / ResetBits（写 BSRR）；
 * 位带版  ：查"别名地址"表 → *p = 0/1 一条 STR 直写该 ODR 位，
 *           地址在编译期算好、无函数调用、无运算，开销最低。
 * 越界保护、极性适配、翻转语义均与库函数版一致；翻转同样是
 * "读-改-写"两步、非原子。原理见 gpio_core.h "位带"小节；
 * 换板子时自动跟随区块 1 宏，无需改动。
 * 标准库 : 无——编译期算好别名地址后一条 STR 直写 ODR 位,
 *          比"标准库函数调用"路径更短、开销最低 */
void LED_BB_On    (uint8_t id);      /* 位带直写：点亮; 例:LED_BB_On(0) */
void LED_BB_Off   (uint8_t id);      /* 位带直写：熄灭; 例:LED_BB_Off(0) */
void LED_BB_Toggle(uint8_t id);      /* 位带直写：翻转; 例:LED_BB_Toggle(1) */

/* ----------------------------------------------------------------
 * 常用灯效（全部基于区块 2 的 LED_On / LED_Off 组合实现）
 *
 * 说明 : 
 *   ① 下列"阻塞式"灯效内部用 gpio_core 的粗延时 Delay_ms，
 *      执行期间占用 CPU —— 适合主循环里简单的视觉效果；
 *      需要"边跑灯效边干别的"请用非阻塞的 LED_FlowStep；
 *   ② interval_ms 传 0 时直接返回（避免无意义的忙循环）；
 *   ③ 所有阻塞式灯效结束后一律全部熄灭，方便连续调用。
 * ---------------------------------------------------------------- */

/* 闪烁：指定 LED 亮 interval_ms → 灭 interval_ms，重复 times 次
 * 总耗时约 2×interval_ms×times（阻塞）
 * 示例 : LED_Blink(1, 5, 200);     // 1 号灯闪 5 次(亮 200ms/灭 200ms) */
void LED_Blink   (uint8_t id, uint32_t times, uint32_t interval_ms);

/* 全部闪烁：所有 LED 同步亮灭，重复 times 次
 * 示例 : LED_AllBlink(3, 300);     // 全体同步闪 3 次(亮 300ms/灭 300ms) */
void LED_AllBlink(uint32_t times, uint32_t interval_ms);

/* 交替闪烁：偶 id（LED0/2/…）与奇 id（LED1/3/…）两组轮流亮灭
 * 例（本板 2 灯）：{0}亮{1}灭 ↔ {0}灭{1}亮，重复 times 次
 * 示例 : LED_Alternate(5, 150);   // 两组交替闪 5 轮 */
void LED_Alternate(uint32_t times, uint32_t interval_ms);

/* 流水灯（单向）：每次只亮 1 个灯并依次移动 0→1→…→N-1，
 * 到末尾后回卷到 0 继续，共跑 times 圈
 * 例（本板 2 灯）：LED0 → LED1 → LED0 → …
 * 示例 : LED_Flow(3, 100);         // 跑 3 圈,每格 100ms */
void LED_Flow   (uint32_t times, uint32_t interval_ms);

/* 跑马灯（往返）：每次只亮 1 个灯，走到末尾后折返回来，
 * "去 + 回"为一趟，共跑 times 趟
 * 例（本板 2 灯）：LED0 → LED1 → LED0 → …
 * 示例 : LED_Marquee(2, 80);      // 往返跑 2 趟(每格 80ms) */
void LED_Marquee(uint32_t times, uint32_t interval_ms);

/* 流水单步（非阻塞）：调用一次，点亮"下一格"并返回其 id
 *   dir > 0 → 向前（id 增大方向）；dir < 0 → 向后；到端点自动回卷
 *   只亮当前位置的灯，其余熄灭
 * 用途 : 把灯效节奏交给自己的主循环（可配合 sys_tick 控时）：
 *      if (SYS_TICK_Timeout(t, 200)) { t = SYS_TICK_GetTick(); LED_FlowStep(1); }
 * 注意 : 内部位置从 0 出发（首次调用点亮 LED0）；
 *       LED_Init() 会把位置复位为 0
 * 示例 : uint8_t here = LED_FlowStep(1);   // 向前走一格(dir=-1 则后退) */
uint8_t LED_FlowStep(int8_t dir);

#endif /* __FWLIB_LED_H */

