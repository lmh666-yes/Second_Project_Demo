#ifndef __FWLIB_GPIO_CORE_H
#define __FWLIB_GPIO_CORE_H

#include "stm32f4xx.h"
#include "delay.h"    /* 兼容:老代码 include gpio_core.h 仍可用（延时见 delay.h）;
                       * 新代码建议直接 #include "delay.h"（函数名小写开头） */

/* ================================================================
 *  gpio_core.h —— 【通用】GPIO 底层工具库  头文件
 * ================================================================
 *  设计定位 : 标准外设库之上的"薄封装"通用 GPIO 层
 *             —— 只提供"配置引脚 / 读写电平"两类纯工具（延时可选用 delay.h）
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
 *      位带宏类    —— 已拆到 sys_bitband.h（BITBAND_* / GPIO_BB_* / Pxout(n) A~I）
 *
 *  结构说明（与其它模块的"区块"对应）:
 *      区块 1 —— 无硬件映射（换板子零改动，相当于区块 1 为空）
 *      区块 2 —— 基础功能：端口时钟 / 输出 / 输入（延时已拆到 delay.h）
 *      （原“区块 3：位带操作”已拆分为独立文件 sys_bitband.h）
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
 *   ③ 需要开漏输出(GPIO_OType_OD)请改用 GPIO_OutInitOD（见下方）；
 *      其它速度等级请直接用标准库另行配置。
 * 标准库 : RCC_AHB1PeriphClockCmd（经 GPIO_ClockEnable）+ GPIO_Init
 * 示例 : GPIO_OutInit(GPIOF, GPIO_Pin_8);               // PF8 推挽输出
 *        GPIO_OutInit(GPIOB, GPIO_Pin_0 | GPIO_Pin_1);   // 两个脚一起配
 * 扩展提示 : 想"换速度/加上拉"——复制本函数、只改 gi.GPIO_Speed /
 *            gi.GPIO_PuPd 两行即可（先例:下方 OutInitOD 是只改 OType 的变体）*/
void    GPIO_OutInit  (GPIO_TypeDef *port, uint16_t pin);

/* 配置为开漏输出：GPIO_OType_OD / GPIO_Speed_100MHz / GPIO_PuPd_NOPULL；内部自动使能端口时钟
 *
 * 典型用途 :
 *   ① "线与"总线：软件模拟 I2C 的 SCL/SDA 必须开漏(GPIO_OType_OD) + 上拉(GPIO_PuPd_UP)；
 *   ② 电平转换：低压 MCU 经开漏 + 上拉到 5V，驱动 5V 器件；
 *   ③ 多设备"占线"：任一设备拉低则总线为低。
 *
 * 注意 :
 *   开漏输出高电平时引脚为"高阻态"，必须依赖上拉电阻才呈现高电平；
 *   没有上拉时测到的"高"是悬空电压，逻辑不可靠。
 * 标准库 : RCC_AHB1PeriphClockCmd（经 GPIO_ClockEnable）+ GPIO_Init
 * 示例 : GPIO_OutInitOD(GPIOB, GPIO_Pin_7);   // PB7 开漏输出(如电平转换/模拟 I2C) */
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
 * 参数 : pull —— 上下拉选择（直接填标准库宏名,与库数值一一对应）:
 *                GPIO_PuPd_NOPULL = 0 —— 无上下拉（浮空）
 *                GPIO_PuPd_UP     = 1 —— 上拉（空闲时引脚为高）
 *                GPIO_PuPd_DOWN   = 2 —— 下拉（空闲时引脚为低）
 *                传入其它值等同 GPIO_PuPd_NOPULL
 *
 * 提示 :
 *   数字输入引脚必须给"确定电平"，否则悬空时电平不定、极易误触发。
 *   常见搭配：低电平按下的按键 → 上拉（1），空闲高、按下低。
 * 标准库 : RCC_AHB1PeriphClockCmd（经 GPIO_ClockEnable）+ GPIO_Init
 * 示例 : GPIO_InInit(GPIOA, GPIO_Pin_0, GPIO_PuPd_UP);       // PA0 输入+上拉(按键形式)
 *        GPIO_InInit(GPIOC, GPIO_Pin_6, GPIO_PuPd_NOPULL);   // PC6 输入+浮空
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
 *  位带操作（Bit-Band）已拆分为独立文件 —— sys_bitband.h
 * ================================================================
 *  （2026-10-08 模块拆分:单比特读写宏集中一处更清晰）
 *  内容 : BITBAND_PERIPH / BITBAND_SRAM / GPIO_BB_OUT / _IN /
 *         GPIO_BB_*_ADDR / GPIO_PIN_NUM / Pxout(n)~PIin(n) 等宏。
 *  用法 : 需要时 #include "sys_bitband.h"（纯宏,无编译成本） */


/* ================================================================
 *  延时函数已拆分为独立文件 —— delay.h / delay.c
 * ================================================================
 *  （2026-10-08 模块拆分:延时工具集中一处,查找/移植更方便）
 *  内容 : delay_ms / delay_loop（粗延时）;
 *         delay_cycles / delay_us / delay_ns / delay_ms_dwt（DWT 精准延时）。
 *  说明 : 老代码继续 #include "gpio_core.h" 即可（本文件已转含 delay.h）;
 *         新代码建议直接 #include "delay.h"——函数名统一小写开头,
 *         旧大写名（Delay_xxx）已取消。 */


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
 *  AF 号 → 外设 速查（按 F407ZE 整理,即本模板目标芯片;
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
 *      运行时 GPIO_PinSource(pin) 函数 / 编译期 GPIO_PIN_NUM(pin) 宏（见 sys_bitband.h）;
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
