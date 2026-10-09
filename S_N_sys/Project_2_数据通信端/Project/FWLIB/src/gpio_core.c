#include "gpio_core.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */

/* ================================================================
 *  gpio_core.c —— 【通用】GPIO 底层工具库  实现文件
 * ================================================================
 *  本文件不含任何硬件映射宏（端口/引脚全部由调用方传入），
 *  只负责：时钟换算、标准库配置、电平读写（延时已拆到 delay.c）。
 *  换板子时，本文件一行都不用动。
 * ================================================================ */


/* ================================================================
 *                  端口 → AHB1 时钟  映射表
 * ================================================================
 * 为什么需要这张表：
 *   标准库使能时钟要的是"时钟位掩码"（如 RCC_AHB1Periph_GPIOB），
 *   而上层传进来的是"端口指针"（如 GPIOB），本表负责二者换算，
 *   让上层不必关心"哪个端口对应哪个时钟位"。
 *
 * 兼容性设计：
 *   每项用 #if defined(GPIOx) 保护——只有芯片真实存在的端口才入表，
 *   因此本文件可直接复用到其它 STM32F4 型号，无需改动代码。
 * ================================================================ */
typedef struct {
    GPIO_TypeDef *port;
    uint32_t      clk;
} PortClock_t;

static const PortClock_t port_clock_map[] = {
#if defined(GPIOA)
    { GPIOA, RCC_AHB1Periph_GPIOA },
#endif
#if defined(GPIOB)
    { GPIOB, RCC_AHB1Periph_GPIOB },
#endif
#if defined(GPIOC)
    { GPIOC, RCC_AHB1Periph_GPIOC },
#endif
#if defined(GPIOD)
    { GPIOD, RCC_AHB1Periph_GPIOD },
#endif
#if defined(GPIOE)
    { GPIOE, RCC_AHB1Periph_GPIOE },
#endif
#if defined(GPIOF)
    { GPIOF, RCC_AHB1Periph_GPIOF },
#endif
#if defined(GPIOG)
    { GPIOG, RCC_AHB1Periph_GPIOG },
#endif
#if defined(GPIOH)
    { GPIOH, RCC_AHB1Periph_GPIOH },
#endif
#if defined(GPIOI)
    { GPIOI, RCC_AHB1Periph_GPIOI },
#endif
};

#define PORT_CLOCK_MAP_SIZE   (sizeof(port_clock_map) / sizeof(port_clock_map[0]))

/* 自动使能指定端口的 AHB1 时钟
 *   - 幂等：重复调用无副作用，可在每次初始化时放心调用
 *   - 未收录的端口：什么都不做（防御性，避免误操作其它寄存器）
 *   - 找到即返回：一次换算，开销极小 */
void GPIO_ClockEnable(GPIO_TypeDef *port)
{
    for (uint8_t i = 0; i < (uint8_t)PORT_CLOCK_MAP_SIZE; i++) {
        if (port_clock_map[i].port == port) {
            RCC_AHB1PeriphClockCmd(port_clock_map[i].clk, ENABLE);
            return;
        }
    }
}


/* ================================================================
 *                    通用 GPIO 输出
 * ================================================================ */
/* 推挽输出初始化（GPIO_OType_PP）：填写标准库初始化结构体后交给 GPIO_Init()
 *   Mode=GPIO_Mode_OUT / OType=GPIO_OType_PP / Speed=GPIO_Speed_100MHz / PuPd=GPIO_PuPd_NOPULL
 * 说明 : 这里只提供库内统一选用的参数；开漏输出版本见
 *       GPIO_OutInitOD，其它速度等级请用标准库 API 另行配置。 */
void GPIO_OutInit(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gi;
    GPIO_ClockEnable(port);

    gi.GPIO_Pin   = pin;
    gi.GPIO_Mode  = GPIO_Mode_OUT;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(port, &gi);
}

/* 开漏输出初始化（GPIO_OType_OD）：仅 OType 与推挽不同（OD = Open Drain）
 * 实现与 GPIO_OutInit 完全对称，方便对照记忆 */
void GPIO_OutInitOD(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gi;
    GPIO_ClockEnable(port);

    gi.GPIO_Pin   = pin;
    gi.GPIO_Mode  = GPIO_Mode_OUT;
    gi.GPIO_OType = GPIO_OType_OD;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(port, &gi);
}

/* 纯电平操作：直接写 BSRR 寄存器，单次写生效、无需读-改-写
 * 前提 : 引脚已由 GPIO_OutInit 配置为输出（本组不负责时钟/模式） */
void GPIO_OutSet   (GPIO_TypeDef *port, uint16_t pin) { GPIO_SetBits   (port, pin); }
void GPIO_OutReset (GPIO_TypeDef *port, uint16_t pin) { GPIO_ResetBits (port, pin); }
void GPIO_OutToggle(GPIO_TypeDef *port, uint16_t pin) { GPIO_ToggleBits(port, pin); }

/* 按参数写电平：非 0 → 高（BSRR 置位）；0 → 低（BSRR 复位位） */
void GPIO_OutWrite (GPIO_TypeDef *port, uint16_t pin, uint8_t level)
{
    if (level != 0U) {
        GPIO_SetBits(port, pin);
    } else {
        GPIO_ResetBits(port, pin);
    }
}

/* 读输出数据寄存器 ODR：返回"软件设置的电平"（不是引脚真实电平）
 * 用途 : 确认自己刚才写了什么、或做输出状态的记录 */
uint8_t GPIO_OutRead(GPIO_TypeDef *port, uint16_t pin)
{
    return GPIO_ReadOutputDataBit(port, pin);
}


/* ================================================================
 *                    通用 GPIO 输入
 * ================================================================ */
/* 输入初始化：Mode=IN，按 pull 参数选择上下拉
 *   pull：1=上拉（空闲高） / 2=下拉（空闲低） / 其它=浮空
 * 提示 : 数字输入引脚建议配上下拉，避免悬空导致电平不定、误触发 */
void GPIO_InInit(GPIO_TypeDef *port, uint16_t pin, uint8_t pull)
{
    GPIO_InitTypeDef gi;
    GPIO_ClockEnable(port);

    gi.GPIO_Pin   = pin;
    gi.GPIO_Mode  = GPIO_Mode_IN;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    gi.GPIO_OType = GPIO_OType_PP;

    switch (pull) {
        case 1:  gi.GPIO_PuPd = GPIO_PuPd_UP;     break;
        case 2:  gi.GPIO_PuPd = GPIO_PuPd_DOWN;   break;
        default: gi.GPIO_PuPd = GPIO_PuPd_NOPULL; break;
    }
    GPIO_Init(port, &gi);
}

/* 读输入数据寄存器 IDR：引脚上的真实电平（1=高，0=低）
 * 注意 : 即时读取、不含消抖——按键类抖动信号需上层做软件滤波 */
uint8_t GPIO_InRead(GPIO_TypeDef *port, uint16_t pin)
{
    return GPIO_ReadInputDataBit(port, pin);
}

/* 单引脚掩码 → 位号（0~15）：GPIO_PinAFConfig 要的是"第几号引脚"。
 * 说明 : 库内引脚均以单引脚掩码配置；多引脚组合/0 返回 0xFF（非法） */
uint8_t GPIO_PinSource(uint16_t pin)
{
    for (uint8_t s = 0; s < 16U; s++) {
        if (pin == (uint16_t)(1UL << s)) return s;
    }
    return 0xFFU;
}


/* ================================================================
 *  延时实现已拆到 delay.c ——
 *  delay_ms / delay_loop（粗延时）、delay_cycles / delay_us /
 *  delay_ns / delay_ms_dwt（DWT 精准延时）与 DWT 测时读数
 *  （DWT_GetCycles / DWT_GetUs / DWT_ElapsedUs）均在那边。
 * ================================================================ */
