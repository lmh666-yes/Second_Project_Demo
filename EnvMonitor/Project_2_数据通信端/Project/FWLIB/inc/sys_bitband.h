#ifndef __FWLIB_SYS_BITBAND_H
#define __FWLIB_SYS_BITBAND_H

#include "stm32f4xx.h"

/*
 *  sys_bitband.h : 位带操作（Bit-Band，单比特读写）头文件
 *  功能 : 把某地址的某一位映射成别名区的一个 32 位字，写别名即写该位，
 *    读别名即读该位（返回 0 或 1）。地址在编译期算出，宏展开为一条
 *    STR/LDR，无函数调用开销。
 *    外设区 0x40000000 ~ 0x400FFFFF -> 别名区 0x42000000 起
 *    SRAM 区 0x20000000 ~ 0x200FFFFF -> 别名区 0x22000000 起
 *
 *  约束 :
 *    1) 只有上述两个区域支持位带（GPIO 属外设区）；CCM RAM(0x10000000)、
 *       FSMC 外扩、Flash 均不在位带区；
 *    2) Cortex-M7（F7/H7）已取消位带，跨芯片移植时不可依赖；
 *    3) 别名访问固定为 32 位读写，位号范围 0 ~ 31；
 *    4) 未使用的宏不产生代码。
 */
/* 通用位带地址宏：产出别名区指针，供查表、传参或预先算好地址使用
 * 约束 : 宏内含取整运算，不可放进静态初始化器（ARMCC 报 #1296），静态表用
 *        GPIO_BB_*_ADDR */
#define BITBAND_PERIPH_ADDR(addr, bit)  ((volatile uint32_t *)(0x42000000UL + (((uint32_t)(addr) & 0x000FFFFFUL) << 5) + ((uint32_t)(bit) << 2)))
#define BITBAND_SRAM_ADDR(addr, bit)    ((volatile uint32_t *)(0x22000000UL + (((uint32_t)(addr) & 0x000FFFFFUL) << 5) + ((uint32_t)(bit) << 2)))

/* 通用位带宏（左值形式）：addr = 目标地址，bit = 位号（0~31） */
#define BITBAND_PERIPH(addr, bit)   (*(BITBAND_PERIPH_ADDR(addr, bit)))
#define BITBAND_SRAM(addr, bit)     (*(BITBAND_SRAM_ADDR(addr, bit)))

/* GPIO 端口指针 -> 端口基址数值（纯整数运算，供编译期构造位带地址）
 * 依据 : 用 &(port)->ODR 计算属地址到整数再回指针的混合运算，ARMCC 对静态
 *   初始化器报 #1296；本宏全程整数域运算，可用于静态初始化器
 * 范围 : A ~ I；F407ZE 实际引出 A ~ G，H/I 供换更大封装或跨芯片移植时用 */
#define GPIO_BASE_NUM(port) ( \
      ((port) == GPIOA) ? GPIOA_BASE : ((port) == GPIOB) ? GPIOB_BASE : \
      ((port) == GPIOC) ? GPIOC_BASE : ((port) == GPIOD) ? GPIOD_BASE : \
      ((port) == GPIOE) ? GPIOE_BASE : ((port) == GPIOF) ? GPIOF_BASE : \
      ((port) == GPIOG) ? GPIOG_BASE : ((port) == GPIOH) ? GPIOH_BASE : \
      ((port) == GPIOI) ? GPIOI_BASE : 0UL )

/* GPIO 位带别名地址宏：产出指针，可存入静态表，用于静态初始化器无警告
 * 约束 : ODR 偏移 = 0x14，IDR 偏移 = 0x10（F4 系列 GPIO_TypeDef 固定布局） */
#define GPIO_BB_OUT_ADDR(port, n)   ((volatile uint32_t *)(0x42000000UL + (((GPIO_BASE_NUM(port) + 0x14UL) & 0x000FFFFFUL) << 5) + ((uint32_t)(n) << 2)))
#define GPIO_BB_IN_ADDR(port, n)    ((volatile uint32_t *)(0x42000000UL + (((GPIO_BASE_NUM(port) + 0x10UL) & 0x000FFFFFUL) << 5) + ((uint32_t)(n) << 2)))

/* GPIO 快捷宏（左值形式）：port = 端口指针，n = 引脚号（0~15）
 * 约束 : port 传常量端口时地址在编译期折叠；传运行时变量端口则回退为比较链 */
#define GPIO_BB_OUT(port, n)        (*(GPIO_BB_OUT_ADDR((port), (n))))
#define GPIO_BB_IN(port, n)         (*(GPIO_BB_IN_ADDR((port), (n))))

/* 单比特掩码 -> 位号（0~15）的编译期换算，供位带查表等需要常量处使用
 * 说明 : 运行时函数 GPIO_PinSource() 的编译期版本
 * 约束 : 只对单个 GPIO_Pin_x 掩码有效，组合或非法掩码返回 0xFF */
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

/* 51 风格快捷宏，A ~ I 全套；重名时可删 */
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
