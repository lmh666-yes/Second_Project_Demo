#ifndef __FWLIB_SYS_BITBAND_H
#define __FWLIB_SYS_BITBAND_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_bitband.h —— 【通用】位带操作（Bit-Band，单比特读写）  头文件
 * ================================================================
 *  来源 : 由 gpio_core.h 拆分独立（2026-10-08）——原来混在 GPIO 里,
 *         现在宏集中一处、更清晰;led/key 里的位带实现同步撤除,
 *         需要"单比特直写/直读"统一来这里取。
 *  性能 : 地址编译期算出 —— 宏展开就是一条 STR/LDR,零函数调用开销
 *
 *  原理（像 51 的 sbit 一样读写某一位）:
 *    把"某地址的某一位"映射成别名区的一个 32 位字——
 *    写别名 = 写该位;读别名 = 读该位（返回 0 或 1）。
 *    外设区 0x40000000 ~ 0x400FFFFF  →  别名区 0x42000000 起
 *    SRAM 区 0x20000000 ~ 0x200FFFFF  →  别名区 0x22000000 起
 *
 *  优点 :
 *    ① 地址在编译期算出,宏展开就是一条 STR/LDR —— 零函数调用开销;
 *    ② 单比特原子写,不影响同寄存器其它位（不用 |= / &= 读改写）;
 *    ③ 任何"外设寄存器 / SRAM 变量"的某一位都能这样操作。
 *
 *  注意 :
 *    ① 只有上面两个区域能位带（GPIO 属于外设区 ✓）;
 *       CCM RAM(0x10000000)、FSMC 外扩、Flash 均不在位带区;
 *    ② Cortex-M7（F7/H7）取消了位带——跨芯片移植时不要依赖;
 *    ③ 别名访问固定为 32 位读/写;位号范围 0 ~ 31;
 *    ④ 未使用的宏不产生任何代码——include 本文件无成本。
 *
 *  用法示例 :
 *    GPIO_BB_OUT(GPIOF, 9) = 0;         // PF9 输出低（点亮 LED0，低有效）
 *    GPIO_BB_OUT(GPIOF, 9) = 1;         // PF9 输出高（熄灭）
 *    if (GPIO_BB_IN(GPIOA, 0) == 0) {}  // 读 PA0（KEY1）电平
 *    PFout(9) = 0;                      // 51 风格等价写法
 *    BITBAND_PERIPH(&TIM2->CR1, 0) = 1; // 任意寄存器位：启动 TIM2
 *
 *  与常见教程 sys.h（BIT_ADDR / Pxout 风格）的对照:
 *    同一套公式 : 别名 = 0x42000000 + ((地址 & 0xFFFFF) << 5) + 位号×4
 *    教程 PAout(n) = BIT_ADDR(GPIOA_ODR_Addr, n)   // ODR 偏移 0x14
 *    本库 PAout(n) = GPIO_BB_OUT(GPIOA, n)         // 完全等价
 *    教程 BITBAND / MEM_ADDR / BIT_ADDR ↔ 本库 BITBAND_PERIPH / _ADDR
 *    端口覆盖 : 两边都是 A ~ I 全套一一对应
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
 *   就干干净净（需要"静态表存别名地址"时借它通过 0 警告编译）。
 * 范围：A ~ I（与常见教程 sys.h 的端口覆盖一致；F407ZE 封装实际
 *       引出 A ~ G,H/I 供换更大封装型号或跨芯片移植时用）。
 * ⭐ 全程用标准库的 GPIOx_BASE 宏运算（不写裸地址）。 */
#define GPIO_BASE_NUM(port) ( \
      ((port) == GPIOA) ? GPIOA_BASE : ((port) == GPIOB) ? GPIOB_BASE : \
      ((port) == GPIOC) ? GPIOC_BASE : ((port) == GPIOD) ? GPIOD_BASE : \
      ((port) == GPIOE) ? GPIOE_BASE : ((port) == GPIOF) ? GPIOF_BASE : \
      ((port) == GPIOG) ? GPIOG_BASE : ((port) == GPIOH) ? GPIOH_BASE : \
      ((port) == GPIOI) ? GPIOI_BASE : 0UL )

/* GPIO 位带"别名地址"宏：产出指针，可存入静态表（表驱动位带）
 * 全程"基址整数 + 偏移"运算 —— 结果可用于静态初始化器（0 警告）
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
 *       静态初始化（如"别名地址表"）；
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
 * F407ZE 封装实际引出 A ~ G,加 H/I 是为了换更大封装/跨芯片时
 * 教程代码原样可编译；如与其它代码重名可删掉本组）
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
