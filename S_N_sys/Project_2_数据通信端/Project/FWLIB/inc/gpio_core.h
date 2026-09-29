#ifndef __FWLIB_GPIO_CORE_H
#define __FWLIB_GPIO_CORE_H

#include "stm32f4xx.h"

/* ================================================================
 *  gpio_core.h —— 【通用】GPIO 底层工具库  头文件
 * ================================================================
 *  设计定位 : 标准外设库之上的"薄封装"通用 GPIO 层
 *             —— 只提供"配置引脚 / 读写电平 / 粗延时"三类纯工具
 *             —— 不含任何外设语义（LED/按键/蜂鸣器等在各自文件里）
 *
 *  被谁使用 :
 *      led.c / key.c / beep.c / ext_io.c 均依赖本模块
 *      换板子时，本文件一行都不用改（不含任何硬件映射）
 *
 *  标准库关键词 : RCC_AHB1PeriphClockCmd / GPIO_Init / GPIO_SetBits / GPIO_ResetBits
 *                 / GPIO_ToggleBits / GPIO_ReadInputDataBit / GPIO_ReadOutputDataBit
 *
 *  调用关系 :
 *      业务代码优先使用 led / key / beep / ext_io 的语义化函数；
 *      只有当需要操作"库尚未封装"的其它引脚时，才直接调用本层。
 *
 *  命名约定 :
 *      GPIO_OutXxx —— 输出方向（推挽 GPIO_OType_PP / 开漏 GPIO_OType_OD 的配置与操作）
 *      GPIO_InXxx  —— 输入方向（读取引脚电平）
 *      位带宏类    —— BITBAND_* / GPIO_BB_* / Pxout(n) A~I（单比特零开销读写）
 *
 *  结构说明（与其它模块的"区块"对应）:
 *      区块 1 —— 无硬件映射（换板子零改动，相当于区块 1 为空）
 *      区块 2 —— 基础功能：端口时钟 / 输出 / 输入 / 粗延时 / 精准延时(DWT)
 *      区块 3 —— 扩展功能：位带操作（Bit-Band 宏）
 * ================================================================ */


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* ----------------------------------------------------------------
 *  小节 1：端口时钟辅助
 * ---------------------------------------------------------------- */
/* 自动使能指定 GPIO 端口的 AHB1 时钟（幂等，可重复调用）
 *
 * 为什么单列一个函数 :
 *   标准库操作任何 GPIO 寄存器前必须先开该端口时钟，否则写寄存器
 *   会被硬件忽略（引脚毫无反应）——这是最常见的隐性故障之一。
 *   本函数把"端口指针 → 时钟位"的换算封装起来，杜绝"漏开时钟"。
 *
 * 使用时机 :
 *   GPIO_OutInit / GPIO_InInit 内部已自动调用本函数，通常无需手动调用
 *   仅当直接使用标准库 API 操作引脚时，才需要先手动调用一次。
 *
 * 实现说明 :
 *   内部维护映射表，支持 GPIOA ~ GPIOI（以芯片实际存在的端口为准）；
 *   找不到对应端口时不做任何事（防御性设计）。
 * 标准库 : RCC_AHB1PeriphClockCmd（查表换算"端口→时钟位"后调用）
 * 示例 : GPIO_ClockEnable(GPIOB);     // 需要单独开时钟时用(Out/InInit 内部已自动调) */
void GPIO_ClockEnable(GPIO_TypeDef *port);


/* ================================================================
 *                    通用 GPIO 输出操作
 * ================================================================ */
/* 配置为推挽输出：GPIO_OType_PP / GPIO_Speed_100MHz / GPIO_PuPd_NOPULL；内部自动使能端口时钟
 *
 * 参数 : port —— 端口指针，如 GPIOF
 *        pin  —— 引脚掩码，如 GPIO_Pin_9（可用 | 组合多个引脚）
 *
 * 说明 :
 *   ① 调用后引脚即进入输出模式，初始输出电平为 0（低）；
 *   ② 需要"上电即高"时，应在 Init 后立即调用 GPIO_OutSet；
 *   ③ 需要开漏输出（GPIO_OType_OD）请改用 GPIO_OutInitOD（见下方）；
 *      其它速度等级请直接用标准库另行配置。
 * 标准库 : RCC_AHB1PeriphClockCmd（经 GPIO_ClockEnable）+ GPIO_Init
 * 示例 : GPIO_OutInit(GPIOF, GPIO_Pin_8);               // PF8 推挽输出(GPIO_OType_PP)
 *        GPIO_OutInit(GPIOB, GPIO_Pin_0 | GPIO_Pin_1);   // 两个脚一起配
 * 扩展提示 : 想"换速度/加上拉"——复制本函数、只改 gi.GPIO_Speed /
 *            gi.GPIO_PuPd 两行即可（先例:下方 OutInitOD 是只改 OType 的变体）*/
void    GPIO_OutInit  (GPIO_TypeDef *port, uint16_t pin);

/* 配置为开漏输出：GPIO_OType_OD / GPIO_Speed_100MHz / GPIO_PuPd_NOPULL；内部自动使能端口时钟
 *
 * 典型用途 :
 *   ① "线与"总线：软件模拟 I2C 的 SCL/SDA 必须开漏（GPIO_OType_OD）+ 上拉（GPIO_PuPd_UP）；
 *   ② 电平转换：低压 MCU 经开漏 + 上拉到 5V，驱动 5V 器件；
 *   ③ 多设备"占线"：任一设备拉低则总线为低。
 *
 * 注意 :
 *   开漏输出高电平时引脚为"高阻态"，必须依赖上拉电阻才呈现高电平；
 *   没有上拉时测到的"高"是悬空电压，逻辑不可靠。
 * 标准库 : RCC_AHB1PeriphClockCmd（经 GPIO_ClockEnable）+ GPIO_Init
 * 示例 : GPIO_OutInitOD(GPIOB, GPIO_Pin_7);   // PB7 开漏输出(GPIO_OType_OD;如电平转换/模拟 I2C) */
void    GPIO_OutInitOD(GPIO_TypeDef *port, uint16_t pin);

/* 输出高电平 / 输出低电平 / 电平翻转
 * 前提 : 引脚已用 GPIO_OutInit 配置为输出
 *       （本组只写 BSRR 数据寄存器，不重复开时钟、不改模式）
 * 标准库 : GPIO_SetBits / GPIO_ResetBits / GPIO_ToggleBits
 * 示例 : GPIO_OutSet(GPIOF, GPIO_Pin_9);          // PF9 输出高(点亮 LED0)
 *        GPIO_OutToggle(GPIOF, GPIO_Pin_9);       // 翻转(闪烁用) */
void    GPIO_OutSet   (GPIO_TypeDef *port, uint16_t pin);   /* 输出高电平 */
void    GPIO_OutReset (GPIO_TypeDef *port, uint16_t pin);   /* 输出低电平 */
void    GPIO_OutToggle(GPIO_TypeDef *port, uint16_t pin);   /* 电平翻转   */

/* 按参数写电平：level 非 0 → 输出高；level = 0 → 输出低
 * 用途 : 电平值来自变量 / 数组时的统一出口（等价 Set / Reset 二选一）
 * 标准库 : GPIO_SetBits / GPIO_ResetBits（二选一调用）
 * 示例 : GPIO_OutWrite(GPIOF, GPIO_Pin_9, 0);   // 写低电平(电平值来自变量时的统一出口) */
void    GPIO_OutWrite (GPIO_TypeDef *port, uint16_t pin, uint8_t level);

/* 读"输出数据寄存器 ODR"：1 = 软件设置为输出高，0 = 输出低
 * 注意 : 读的是"软件想输出的值"，不是引脚上的真实电平——
 *       引脚被外部电路拉动时，本函数不会反映出来
 * 标准库 : GPIO_ReadOutputDataBit
 * 示例 : uint8_t lv = GPIO_OutRead(GPIOF, GPIO_Pin_9);   // lv = 上一步软件写入的值 */
uint8_t GPIO_OutRead  (GPIO_TypeDef *port, uint16_t pin);


/* ================================================================
 *                    通用 GPIO 输入操作
 * ================================================================ */
/* 配置为输入：内部自动使能端口时钟
 *
 * 参数 : pull —— 上下拉选择（与标准库 GPIO_PuPd_* 编码一致）
 *                GPIO_PuPd_NOPULL = 0，无上下拉（浮空）
 *                GPIO_PuPd_UP     = 1，上拉（空闲时引脚为高）
 *                GPIO_PuPd_DOWN   = 2，下拉（空闲时引脚为低）
 *                传入其它值等同 GPIO_PuPd_NOPULL
 *
 * 提示 :
 *   数字输入引脚必须给"确定电平"，否则悬空时电平不定、极易误触发。
 *   常见搭配：低电平按下的按键 → 上拉（GPIO_PuPd_UP），空闲高、按下低。
 * 标准库 : RCC_AHB1PeriphClockCmd（经 GPIO_ClockEnable）+ GPIO_Init
 * 示例 : GPIO_InInit(GPIOA, GPIO_Pin_0, GPIO_PuPd_UP);      // PA0 输入+上拉(按键形式)
 *        GPIO_InInit(GPIOC, GPIO_Pin_6, GPIO_PuPd_NOPULL);  // PC6 输入+浮空
 * 扩展提示 : 不同脚想要不同上下拉——直接按脚分多次调用本函数即可 */
void    GPIO_InInit(GPIO_TypeDef *port, uint16_t pin, uint8_t pull);

/* 读"输入数据寄存器 IDR"：引脚上的真实电平
 * 返回 : 1 = 高电平，0 = 低电平
 * 注意 : 即时读取、不含消抖；抖动信号（如机械按键）请在上层滤波
 * 标准库 : GPIO_ReadInputDataBit
 * 示例 : if (GPIO_InRead(GPIOA, GPIO_Pin_0) == 0) { ... }   // PA0 被按下 */
uint8_t GPIO_InRead(GPIO_TypeDef *port, uint16_t pin);

/* 单引脚掩码 → 位号（0~15，即 GPIO_PinSourcex）
 * 用途 : GPIO_PinAFConfig 等需要"第几号引脚"而不是掩码的场合
 * 返回 : 0~15 = 对应位号；多引脚组合/0 等非法输入返回 0xFF
 * 标准库 : 无——用位运算替代 GPIO_PinSource0~15 宏组
 * 示例 : uint8_t s = GPIO_PinSource(GPIO_Pin_9);    // s = 9
 *        GPIO_PinAFConfig(GPIOA, s, GPIO_AF_TIM5);  // 典型配套用法
 * 深入 : 复用功能(AF0~AF15)全表 + GPIO_PinAFConfig 逐句解析 → 文末附录 */
uint8_t GPIO_PinSource(uint16_t pin);


/* ================================================================
 *          区块 3：扩展功能 —— 位带操作（Bit-Band，单比特读写）
 * ================================================================
 * 原理（像 51 的 sbit 一样读写某一位）:
 *   把"某地址的某一位"映射成别名区的一个 32 位字——
 *   写别名 = 写该位；读别名 = 读该位（返回 0 或 1）。
 *   外设区 0x40000000 ~ 0x400FFFFF  →  别名区 0x42000000 起
 *   SRAM 区 0x20000000 ~ 0x200FFFFF  →  别名区 0x22000000 起
 *
 * 优点 :
 *   ① 地址在编译期算出，宏展开就是一条 STR/LDR —— 零函数调用开销；
 *   ② 单比特原子写，不影响同寄存器其它位（不用 |= / &= 读改写）；
 *   ③ 任何"外设寄存器 / SRAM 变量"的某一位都能这样操作。
 *
 * 注意 :
 *   ① 只有上面两个区域能位带（GPIO 属于外设区 ✓）；
 *      CCM RAM(0x10000000)、FSMC 外扩、Flash 均不在位带区；
 *   ② Cortex-M7（F7/H7）取消了位带——跨芯片移植时不要依赖；
 *   ③ 别名访问固定为 32 位读/写；位号范围 0 ~ 31。
 *
 * 用法示例 :
 *   GPIO_BB_OUT(GPIOF, 9) = 0;         // PF9 输出低（点亮 LED0，低有效）
 *   GPIO_BB_OUT(GPIOF, 9) = 1;         // PF9 输出高（熄灭）
 *   if (GPIO_BB_IN(GPIOA, 0) == 0) {}  // 读 PA0（KEY1）电平
 *   PFout(9) = 0;                      // 51 风格等价写法
 *   BITBAND_PERIPH(&TIM2->CR1, 0) = 1; // 任意寄存器位：启动 TIM2
 *
 * 与常见教程 sys.h（BIT_ADDR / Pxout 风格）的对照:
 *   同一套公式 : 别名 = 0x42000000 + ((地址 & 0xFFFFF) << 5) + 位号×4
 *   教程 PAout(n) = BIT_ADDR(GPIOA_ODR_Addr, n)   // ODR 偏移 0x14
 *   本库 PAout(n) = GPIO_BB_OUT(GPIOA, n)         // 完全等价
 *   教程 BITBAND / MEM_ADDR / BIT_ADDR ↔ 本库 BITBAND_PERIPH / _ADDR
 *   端口覆盖 : 两边都是 A ~ I 全套一一对应
 * ================================================================ */
/* 通用位带"地址"宏：产出别名区指针（供查表 / 传参 / 预先算好地址）
 * 读写写法：*BITBAND_PERIPH_ADDR(&某变量, 位号) = 值;
 * 提醒 : 本宏含"地址取整"运算 —— 在函数体里用没问题，但别放进
 *       静态初始化器（ARMCC 会报 #1296 扩展常量警告）；
 *       需要静态表请用下方的 GPIO_BB_*_ADDR（纯整数公式版）。 */
#define BITBAND_PERIPH_ADDR(addr, bit)  ((volatile uint32_t *)(0x42000000UL + (((uint32_t)(addr) & 0x000FFFFFUL) << 5) + ((uint32_t)(bit) << 2)))
#define BITBAND_SRAM_ADDR(addr, bit)    ((volatile uint32_t *)(0x22000000UL + (((uint32_t)(addr) & 0x000FFFFFUL) << 5) + ((uint32_t)(bit) << 2)))

/* 通用位带宏（左值形式）：addr = 目标地址，bit = 位号（0~31）
 * 例：BITBAND_PERIPH(&TIM2->CR1, 0) = 1;  启动 TIM2 */
#define BITBAND_PERIPH(addr, bit)   (*(BITBAND_PERIPH_ADDR(addr, bit)))
#define BITBAND_SRAM(addr, bit)     (*(BITBAND_SRAM_ADDR(addr, bit)))

/* GPIO 端口指针 → 端口基址数值（纯整数，供编译期机械构造位带地址）
 * 例：GPIO_BASE_NUM(GPIOF) → GPIOF_BASE 的值；比较链在编译期折叠为常量
 * 说明（为什么绕这一下）：位带地址若用 &(port)->ODR 直接算，是
 *   "地址→整数→再回指针"的混合运算，ARMCC 对静态初始化器会报
 *   #1296 扩展常量警告；换成"整数域全程运算、最后一步才转指针"
 *   就干干净净（led.c / key.c 的位带表借此通过 0 警告编译）。
 * 范围：A ~ I（与常见教程 sys.h 的端口覆盖一致,教程代码原样可编译；
 *       本板 F407ZG 实装 A ~ G,H/I 供换更大封装型号/跨芯片移植用,
 *       若头文件未定义对应 GPIOx_BASE 可删该行）。
 * ⭐ 全程用标准库的 GPIOx_BASE 宏运算（不写裸地址）。 */
#define GPIO_BASE_NUM(port) ( \
      ((port) == GPIOA) ? GPIOA_BASE : ((port) == GPIOB) ? GPIOB_BASE : \
      ((port) == GPIOC) ? GPIOC_BASE : ((port) == GPIOD) ? GPIOD_BASE : \
      ((port) == GPIOE) ? GPIOE_BASE : ((port) == GPIOF) ? GPIOF_BASE : \
      ((port) == GPIOG) ? GPIOG_BASE : ((port) == GPIOH) ? GPIOH_BASE : \
      ((port) == GPIOI) ? GPIOI_BASE : 0UL )

/* GPIO 位带"别名地址"宏：产出指针，可存入静态表（表驱动位带）
 * 全程"基址整数 + 偏移"运算 —— 结果可用于静态初始化器（0 警告）
 * 库内示范：led.c 的 led_bb[] / key.c 的 key_bb[] —— 表里存地址、操作即 *p
 * 说明 : 库内仅 led / key 提供位带版函数 —— 位带收益只在"高频单比特"
 *       场景才明显；其余模块（蜂鸣器/外接/背光等）操作频率在毫秒级，
 *       位带省下的开销无实际意义，故不做；需要时照此模式自建即可。
 * 例：volatile uint32_t *p = GPIO_BB_OUT_ADDR(GPIOF, 9);  *p = 0;
 * 注意 : ODR 偏移 = 0x14、IDR 偏移 = 0x10（F4 系列 GPIO_TypeDef 固定布局） */
#define GPIO_BB_OUT_ADDR(port, n)   ((volatile uint32_t *)(0x42000000UL + (((GPIO_BASE_NUM(port) + 0x14UL) & 0x000FFFFFUL) << 5) + ((uint32_t)(n) << 2)))
#define GPIO_BB_IN_ADDR(port, n)    ((volatile uint32_t *)(0x42000000UL + (((GPIO_BASE_NUM(port) + 0x10UL) & 0x000FFFFFUL) << 5) + ((uint32_t)(n) << 2)))

/* GPIO 快捷宏（左值形式）：port = 端口指针，n = 引脚号（0~15，如 PF9 就是 9）
 * 例：GPIO_BB_OUT(GPIOF, 9) = 0;  →  PF9 输出低
 * 说明 : port 传库配置里的常量端口（如 GPIOF）→ 地址全在编译期折叠、
 *       零开销；传运行时变量端口 → 退回比较链（稍慢），该场景可改用
 *       BITBAND_PERIPH(&(port)->ODR, n) 手动构造更快。 */
#define GPIO_BB_OUT(port, n)        (*(GPIO_BB_OUT_ADDR((port), (n))))
#define GPIO_BB_IN(port, n)         (*(GPIO_BB_IN_ADDR((port), (n))))

/* 单比特掩码 → 位号（0~15）的【编译期】换算（位带查表等需要常量处）
 * 例：GPIO_PIN_NUM(GPIO_Pin_9) 展开为常量 9
 * 说明 : 是运行时函数 GPIO_PinSource() 的编译期版本 —— 结果可直接用于
 *       静态初始化（如 led.c / key.c 的位带别名地址表）；
 *       只应对单个 GPIO_Pin_x 掩码使用（组合/非法掩码返回 0xFF）。 */
#define GPIO_PIN_NUM(pin) (                    \
      ((pin) == GPIO_Pin_0 ) ?  0 :            \
      ((pin) == GPIO_Pin_1 ) ?  1 :            \
      ((pin) == GPIO_Pin_2 ) ?  2 :            \
      ((pin) == GPIO_Pin_3 ) ?  3 :            \
      ((pin) == GPIO_Pin_4 ) ?  4 :            \
      ((pin) == GPIO_Pin_5 ) ?  5 :            \
      ((pin) == GPIO_Pin_6 ) ?  6 :            \
      ((pin) == GPIO_Pin_7 ) ?  7 :            \
      ((pin) == GPIO_Pin_8 ) ?  8 :            \
      ((pin) == GPIO_Pin_9 ) ?  9 :            \
      ((pin) == GPIO_Pin_10) ? 10 :            \
      ((pin) == GPIO_Pin_11) ? 11 :            \
      ((pin) == GPIO_Pin_12) ? 12 :            \
      ((pin) == GPIO_Pin_13) ? 13 :            \
      ((pin) == GPIO_Pin_14) ? 14 :            \
      ((pin) == GPIO_Pin_15) ? 15 : 0xFF )

/* 51 风格快捷宏（A ~ I 全套,与常见教程 sys.h 的命名/覆盖一致；
 * 本板 F407ZG 实装 A ~ G,H/I 供换更大封装/跨芯片时教程代码原样可编译;
 * 如与其它代码重名可删掉本组）
 * 例：PFout(9) = 0;   if (PAin(0) == 0) { ... } */
#define PAout(n)  GPIO_BB_OUT(GPIOA, (n))
#define PAin(n)   GPIO_BB_IN (GPIOA, (n))
#define PBout(n)  GPIO_BB_OUT(GPIOB, (n))
#define PBin(n)   GPIO_BB_IN (GPIOB, (n))
#define PCout(n)  GPIO_BB_OUT(GPIOC, (n))
#define PCin(n)   GPIO_BB_IN (GPIOC, (n))
#define PDout(n)  GPIO_BB_OUT(GPIOD, (n))
#define PDin(n)   GPIO_BB_IN (GPIOD, (n))
#define PEout(n)  GPIO_BB_OUT(GPIOE, (n))
#define PEin(n)   GPIO_BB_IN (GPIOE, (n))
#define PFout(n)  GPIO_BB_OUT(GPIOF, (n))
#define PFin(n)   GPIO_BB_IN (GPIOF, (n))
#define PGout(n)  GPIO_BB_OUT(GPIOG, (n))
#define PGin(n)   GPIO_BB_IN (GPIOG, (n))
#define PHout(n)  GPIO_BB_OUT(GPIOH, (n))
#define PHin(n)   GPIO_BB_IN (GPIOH, (n))
#define PIout(n)  GPIO_BB_OUT(GPIOI, (n))
#define PIin(n)   GPIO_BB_IN (GPIOI, (n))


/* ================================================================
 *                    粗延时（软件空循环）
 * ================================================================
 * 未做硬件标定：实际时长随主频、编译优化等级、Flash 等待周期变化，
 * 仅适用于 LED 闪烁 / 按键消抖 / 蜂鸣器节拍等对精度不敏感的场景；
 * 需要较长的高精度延时请用 sys_tick 模块；"纳秒 ~ 微秒"级短延时
 * 见下方精准延时小节（DWT 硬件计时;纳秒 ~ 毫秒）。
 * 示例 : Delay_ms(500);       // 粗延时约半秒
 *        Delay_loop(1000);    // 空转 1000 次(最短的延时单元) */
void Delay_ms  (uint32_t ms);                    /* 毫秒级粗延时 */
void Delay_loop(volatile uint32_t n);            /* 空转 n 次 */


/* ================================================================
 *        精准延时（DWT 周期计数器 —— 非软件空循环;纳秒 ~ 毫秒）
 * ================================================================
 * 原理 :
 *   Cortex-M4 内核自带 DWT->CYCCNT —— 每个 CPU 周期自动 +1 的
 *   硬件计数器；延时 = 忙等到"两次读数之差"达到目标周期数，
 *   周期数↔时间换算使用 SystemCoreClock（主频变了自动跟随）。
 *
 * 特点 :
 *   ① 不用任何定时器 / 中断、无需初始化 —— 首次调用自动使能 DWT；
 *   ② ⭐ FreeRTOS 及其它 RTOS 下依然可用（它不占用 SysTick）；
 *   ③ 忙等实现：被中断打断时总时长顺延（ISR 耗时计入其中）；
 *   ④ 精度：周期级（1 周期 ≈ 6ns @168MHz），换算与循环粒度
 *      合计误差约 ±几十 ns；
 *   ⑤ 毫秒级精准延时两种选择：裸机优先 sys_tick（中断计时、不占 CPU）；
 *      不想依赖 SysTick（如已上 RTOS）用本节的 Delay_ms_DWT——忙等、不占中断。
 *
 * 适用 : 单总线时序（WS2812 / DS18B20）、传感器建立-保持时间、
 *        脉冲宽度、移位寄存器时钟等"纳秒 ~ 毫秒"级场合。
 *
 * 范围提醒（32 位换算，防溢出）:
 *   us / ns / ms 与主频的乘积需小于 2^32 ——
 *   @168MHz：Delay_us 最大约 25.5 秒、Delay_ns 最大约 25.5 毫秒、
 *   Delay_ms_DWT 单次最大约 25.5 秒（超出自动按上限执行）；
 *   更长且不想占 CPU 的毫秒延时请用 sys_tick 的 SYS_TICK_Delay_ms。
 * ================================================================ */
void Delay_cycles(uint32_t cycles);   /* 原语：忙等 cycles 个 CPU 周期; 例:Delay_cycles(168) ≈ 1µs@168MHz */
void Delay_us    (uint32_t us);       /* 微秒级精准延时; 例:Delay_us(10) = 10µs */
void Delay_ns    (uint32_t ns);       /* 纳秒级精准延时; 例:Delay_ns(500) = 0.5µs */
void Delay_ms_DWT(uint32_t ms);       /* 毫秒级精准延时(不用 SysTick——RTOS 下可用); 例:Delay_ms_DWT(100) = 100ms */

/* ---- 同一个 DWT 计数器的"读数"接口（测时间用，不是延时）----
 * 用途 : 单总线/自定义协议的位宽测量（DHT11 驱动就靠它）、
 *        测某段代码耗时、信号脉宽测量等
 * 说明 : 首次调用自动使能 DWT；CYCCNT 自由运行、约 25.6s 回绕一次，
 *        所以比较时间请用 DWT_ElapsedUs（差值法，回绕安全）
 * 示例 : uint32_t t = DWT_GetUs();
 *        ... 一段代码 ...
 *        uint32_t 耗时 = DWT_ElapsedUs(t);   // 单位 µs */
uint32_t DWT_GetCycles (void);              /* 原始周期计数（1 周期 ≈ 6ns @168MHz） */
uint32_t DWT_GetUs     (void);              /* 微秒时间戳（可直接相减） */
uint32_t DWT_ElapsedUs (uint32_t start_us); /* 距时间戳已过多少微秒 */

/* ---- 同一个 DWT 计数器的"读数"接口（测时间用，不是延时）----
 * 用途 : 单总线/自定义协议的位宽测量（DHT11 驱动就靠它）、
 *        测某段代码耗时、信号脉宽捕获等
 * 说明 : 首次调用自动使能 DWT；CYCCNT 自由运行、约 25.6s 回绕，
 *        所以比较时间请用 DWT_ElapsedUs（差值法，回绕安全）
 * 示例 : uint32_t t = DWT_GetUs();
 *        ... 一段代码 ...
 *        uint32_t 耗时 = DWT_ElapsedUs(t);   // 单位 µs */
uint32_t DWT_GetCycles(void);          /* 原始周期计数（1 周期 ≈ 6ns @168MHz） */
uint32_t DWT_GetUs    (void);          /* 微秒时间戳（总时间线，可直接相减） */
uint32_t DWT_ElapsedUs(uint32_t start_us);  /* 距时间戳已过多少微秒 */


/* ================================================================
 *  附:标准库结构体速查 —— GPIO_TypeDef（定义在 stm32f4xx.h）
 * ================================================================
 *  官方头文件的英文注释看不懂就来这;库通过指针（如 GPIOA->MODER）操作。
 *  ⭐ 一律写标准库宏名——下表左边就是宏名,填参数 / 读代码照名字对:
 *    MODER     模式:GPIO_Mode_IN（输入） / GPIO_Mode_OUT（输出） /
 *              GPIO_Mode_AF（复用） / GPIO_Mode_AN（模拟）
 *              （GPIO_Init 配"方向"写的就是它;寄存器编码 00/01/10/11）
 *    OTYPER    输出类型:GPIO_OType_PP（推挽） / GPIO_OType_OD（开漏）
 *              （GPIO_OutInitOD 选的就是 GPIO_OType_OD）
 *    OSPEEDR   输出速度:GPIO_Speed_2MHz / GPIO_Speed_25MHz /
 *              GPIO_Speed_50MHz / GPIO_Speed_100MHz
 *              （GPIO_Init 的 Speed 字段;寄存器编码 00/01/10/11）
 *    PUPDR     上下拉:GPIO_PuPd_NOPULL（无） / GPIO_PuPd_UP（上拉） /
 *              GPIO_PuPd_DOWN（下拉）
 *              （GPIO_InInit 的 pull 参数;按键空闲电平靠它）
 *    IDR       输入数据:引脚上的真实电平（GPIO_InRead / 位带读它）
 *    ODR       输出数据:软件写入的电平（位带直写就是写它的某一位;
 *              GPIO_OutRead 读它）
 *    BSRRL/H   置位/复位:F4 拆成两个 16 位半字——BSRRL 写 1 置位、
 *              BSRRH 写 1 复位（GPIO_SetBits/ResetBits/ToggleBits 写它）
 *    LCKR      配置锁定:锁住引脚配置防误改,库未使用
 *    AFR[2]    复用功能:每个引脚 4 位,存复用号 0~15
 *              （GPIO_PinAFConfig 写它;GPIO_AF_TIM5 等取的就是这的值）
 *
 *  附:标准库结构体速查 —— DWT_Type（定义在 core_cm4.h;精准短延时用）
 *    CTRL      控制:位 0 = CYCCNT 周期计数使能
 *              （delay_dwt_enable 置位后计数器开始数）
 *    CYCCNT    周期计数:每个 CPU 周期 +1 —— 延时 = 忙等到达目标差值;
 *              32 位约 25.6s @168MHz 回绕,差值法天然安全
 *    CPICNT / EXCCNT / SLEEPCNT / LSUCNT / FOLDCNT  性能计数,库未用
 *    PCSR      程序计数器采样,库未用
 *    COMP/MASK/FUNCTION × 4   数据观察点硬件,库未用
 *    （配套:CoreDebug->DEMCR 的 TRCENA 位是跟踪总开关,
 *      使能 DWT 时一起打开——见 gpio_core.c 的 delay_dwt_enable）
 * ================================================================ */


/* ================================================================
 *  附:GPIO 复用功能(AF)速查表 —— GPIO_PinAFConfig 的"选号依据"
 * ================================================================
 *  背景 : 每根引脚内部像一个"多路开关",可接到不同外设信号;
 *         AF0 ~ AF15 是 16 套固定映射——写哪个号,引脚就接哪套信号。
 *         使能一个复用功能需两步（库内各模块的调用链都这么走）:
 *           ① GPIO_PinAFConfig(端口, 脚序号, 复用号)  选号
 *           ② GPIO_Init(复用模式)                      切到复用
 *         两步缺一不生效;先后顺序无所谓。
 *
 *  AF 号 → 外设 速查（按 F407ZG 整理,即本模板目标芯片;
 *                [ ] 内为仅高阶型号有的功能,本芯片没有）:
 *    AF0  系统 : RTC_50Hz / MCO1 / MCO2 / TAMPER / SWJ(复位默认) / TRACE
 *    AF1  TIM1、TIM2
 *    AF2  TIM3、TIM4、TIM5
 *    AF3  TIM8、TIM9、TIM10、TIM11
 *    AF4  I2C1、I2C2、I2C3
 *    AF5  SPI1、SPI2（含 I2S2）            [SPI4/5/6 仅 F42x/43x]
 *    AF6  SPI3（含 I2S3）                  [SAI1     仅 F42x/43x]
 *    AF7  USART1、USART2、USART3、I2S3ext
 *    AF8  UART4、UART5、USART6             [UART7/8  仅 F42x/43x]
 *    AF9  CAN1、CAN2、TIM12、TIM13、TIM14
 *    AF10 OTG_FS、OTG_HS
 *    AF11 ETH
 *    AF12 FSMC、SDIO、OTG_HS_FS            [FMC 仅 F42x/43x(FSMC 改名)]
 *    AF13 DCMI
 *    AF14 （本芯片未用）                    [LTDC     仅 F429/439]
 *    AF15 EVENTOUT（内核事件输出到引脚,调试用）
 *
 *  外设 → AF 号 反查（从"我想用什么"出发,平时查这个更快）:
 *    USART1/2/3 → AF7      USART6、UART4/5 → AF8
 *    I2C1/2/3   → AF4      SPI1/2 → AF5      SPI3 → AF6
 *    TIM1/2 → AF1   TIM3/4/5 → AF2   TIM8~11 → AF3   TIM12~14 → AF9
 *    CAN1/2 → AF9   ETH → AF11   FSMC/SDIO → AF12   DCMI → AF13
 *    （库函数的 af 参数就填同名宏: TIM5 → GPIO_AF_TIM5;本表即答案）
 *
 *  ⚠ 三个必须知道的点:
 *   ① AF 号只说明"哪套映射",引脚支不支持要看数据手册的
 *      "Alternate function mapping" 表（以引脚为行）:该脚那行
 *      列出几项,就说明它能干几件事、分别用哪个号。
 *   ② 同一外设信号可以出现在多根引脚、且复用号相同:
 *      如 USART1_TX 在 PA9 或 PB6 都是 AF7——换引脚时
 *      af 参数照填不动,只改 port / pin。
 *   ③ 一根引脚同一时刻只能干一件事（被外设 A 占了就不能再给 B）:
 *      如 PB10/PB11 给了 I2C2,USART3 就不能再选它——
 *      各模块注释里"与 xx 引脚复用,二选一"说的就是这种情况。
 *
 *  附:GPIO_PinAFConfig 逐句解析（对照标准库源码读,顺带学位运算）
 *  ----------------------------------------------------------------
 *  函数原型 : void GPIO_PinAFConfig(GPIOx, GPIO_PinSource, GPIO_AF)
 *  数据在哪 : AFR 是 uint32_t[2] —— AFR[0] 管 0~7 号脚(AFRL);
 *             AFR[1] 管 8~15 号脚(AFRH);每根脚占 4 位(半字节),
 *             脚 0 占 bit[3:0]、脚 1 占 bit[7:4] …… 脚 7 占 bit[31:28]
 *
 *  源码四步（源码里的 temp / temp_2 只是标准库的写法习惯,
 *            本质就是第③④两步,不必被两个临时变量绕晕）:
 *    ① 选寄存器 : AFR[GPIO_PinSource >> 3]        // >>3 即除以 8
 *                  0~7 号脚 → AFR[0];8~15 号 → AFR[1]
 *    ② 算偏移   : (GPIO_PinSource & 7) * 4        // 取余再乘 4
 *    ③ 清零     : AFR[..] &= ~(0xF << 偏移);      // 只清该脚那 4 位
 *    ④ 写入     : AFR[..] |= (GPIO_AF << 偏移);   // 放入新复用号
 *    （读-改-写:只动这 4 位,同寄存器其它引脚不受影响）
 *
 *  例:把 PA9 配成 USART1_TX（AF7）:
 *    9 >> 3 = 1        → 动 AFR[1]
 *    (9 & 7) * 4 = 4   → 动 bit[7:4] 这格
 *    结果 : AFR[1] = (AFR[1] & ~0xF0) | 0x70
 *    库内等价调用 : GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
 *
 *  ⚠ 最大坑（务必记住）: 第二参数是"脚序号 0~15"（GPIO_PinSourcex
 *    宏）,不是 GPIO_Pin_x 位掩码!两者数值完全不同:
 *      GPIO_PinSource9 = 9;   而 GPIO_Pin_9 = 0x0200（512）
 *    把掩码当序号传 → 索引越界,写坏内存、程序跑飞。
 *    库内配套了两种转换工具:
 *      运行时 GPIO_PinSource(pin) 函数 / 编译期 GPIO_PIN_NUM(pin) 宏;
 *    手写标准库时最省事:直接写 GPIO_PinSource9 这样的常量
 *    （lcd.c / sys_eth.c 里就是这种写法）。
 *
 *  手写标准库的完整姿势（以 PA9 = USART1_TX 为例,两步缺一不生效）:
 *    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);   // ①选号
 *    GPIO_InitTypeDef gi;                                        // ②切模式
 *    gi.GPIO_Pin   = GPIO_Pin_9;
 *    gi.GPIO_Mode  = GPIO_Mode_AF;          // 复用模式(MODER = 10)
 *    gi.GPIO_OType = GPIO_OType_PP;
 *    gi.GPIO_Speed = GPIO_Speed_100MHz;
 *    gi.GPIO_PuPd  = GPIO_PuPd_UP;
 *    GPIO_Init(GPIOA, &gi);
 *    （库内 sys_usart 的 TX 脚就是这样配的——见其"标准库调用链"第②步）
 *
 *  冷知识 : 复位后 AFR 全 0 = 所有脚处于 AF0——所以 SWD 调试口
 *    (PA13/PA14)一上电就能用,不需要任何配置。
 * ================================================================ */

#endif /* __FWLIB_GPIO_CORE_H */
