#ifndef __FWLIB_SYS_BITBAND_H
#define __FWLIB_SYS_BITBAND_H

#include "stm32f4xx.h"

/* sys_bitband.h — 位带读写（Bit-Band，Cortex-M4 别名区）
 * 32 位字别名映射：写别名 = 写该位，读别名 = 该位（0 或 1）
 * 别名地址 = 0x42000000 + ((地址 & 0xFFFFF) << 5) + 位号×4
 * 外设区 0x40000000~0x400FFFFF → 别名区 0x42000000 起
 * SRAM 区 0x20000000~0x200FFFFF → 别名区 0x22000000 起
 * 限制：仅上述两区可位带，CCM RAM(0x10000000)、FSMC 外扩、Flash 不适用；
 *       Cortex-M7（F7/H7）已取消位带；别名固定 32 位访问，位号 0~31 */
/* 位带别名地址（指针），供查表、传参或预先算出地址
 * addr：目标地址；bit：位号 0~31
 * 例：*BITBAND_PERIPH_ADDR(&TIM2->CR1, 0) = 1;
 * 含地址取整运算，不能用于静态初始化器（ARMCC 报 #1296）；
 * 静态表改用 GPIO_BB_OUT_ADDR / GPIO_BB_IN_ADDR。 */
#define BITBAND_PERIPH_ADDR(addr, bit)  ((volatile uint32_t *)(0x42000000UL + (((uint32_t)(addr) & 0x000FFFFFUL) << 5) + ((uint32_t)(bit) << 2)))
#define BITBAND_SRAM_ADDR(addr, bit)    ((volatile uint32_t *)(0x22000000UL + (((uint32_t)(addr) & 0x000FFFFFUL) << 5) + ((uint32_t)(bit) << 2)))

/* 位带宏（左值形式）：addr = 目标地址，bit = 位号 0~31
 * 例：BITBAND_PERIPH(&TIM2->CR1, 0) = 1; */
#define BITBAND_PERIPH(addr, bit)   (*(BITBAND_PERIPH_ADDR(addr, bit)))
#define BITBAND_SRAM(addr, bit)     (*(BITBAND_SRAM_ADDR(addr, bit)))

/* GPIO 端口指针 → 端口基址数值（编译期常量比较链，全程用 GPIOx_BASE 运算，不写裸地址）
 * 范围 A~I；非上述端口返回 0UL
 * 位带地址经整数域构造，可用于静态初始化器（避免 ARMCC #1296） */
#define GPIO_BASE_NUM(port) ( \
      ((port) == GPIOA) ? GPIOA_BASE : ((port) == GPIOB) ? GPIOB_BASE : \
      ((port) == GPIOC) ? GPIOC_BASE : ((port) == GPIOD) ? GPIOD_BASE : \
      ((port) == GPIOE) ? GPIOE_BASE : ((port) == GPIOF) ? GPIOF_BASE : \
      ((port) == GPIOG) ? GPIOG_BASE : ((port) == GPIOH) ? GPIOH_BASE : \
      ((port) == GPIOI) ? GPIOI_BASE : 0UL )

/* GPIO 位带别名地址（指针），可存入静态表：全程基址整数 + 偏移运算
 * 例：volatile uint32_t *p = GPIO_BB_OUT_ADDR(GPIOF, 9);  *p = 0;
 * ODR 偏移 0x14、IDR 偏移 0x10（F4 系列 GPIO_TypeDef 固定布局） */
#define GPIO_BB_OUT_ADDR(port, n)   ((volatile uint32_t *)(0x42000000UL + (((GPIO_BASE_NUM(port) + 0x14UL) & 0x000FFFFFUL) << 5) + ((uint32_t)(n) << 2)))
#define GPIO_BB_IN_ADDR(port, n)    ((volatile uint32_t *)(0x42000000UL + (((GPIO_BASE_NUM(port) + 0x10UL) & 0x000FFFFFUL) << 5) + ((uint32_t)(n) << 2)))

/* GPIO 快捷宏（左值形式）：port = 端口指针，n = 引脚号 0~15
 * 例：GPIO_BB_OUT(GPIOF, 9) = 0;   GPIO_BB_IN(GPIOA, 0)
 * port 传常量端口（如 GPIOF）时地址在编译期折叠，零开销；
 * 传运行时变量端口则走比较链，该场景可用 BITBAND_PERIPH(&(port)->ODR, n) */
#define GPIO_BB_OUT(port, n)        (*(GPIO_BB_OUT_ADDR((port), (n))))
#define GPIO_BB_IN(port, n)         (*(GPIO_BB_IN_ADDR((port), (n))))

/* 单比特掩码 → 位号 0~15 的编译期换算（结果可用于静态初始化）
 * 例：GPIO_PIN_NUM(GPIO_Pin_9) 展开为常量 9
 * 只可用于单个 GPIO_Pin_x 掩码；组合或非法掩码返回 0xFF */
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

/* 51 风格快捷宏，覆盖 A ~ I（F407ZE 实际引出 A ~ G，H/I 备用）
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

#endif /* __FWLIB_SYS_BITBAND_H */
