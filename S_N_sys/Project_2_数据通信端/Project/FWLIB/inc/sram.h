#ifndef __FWLIB_SRAM_H
#define __FWLIB_SRAM_H

#include "stm32f4xx.h"

/* ================================================================
 *  sram.h —— 【板载】FSMC 外扩 SRAM（IS62WV51216）  头文件
 * ================================================================
 *  设计定位 : 把"挂在 FSMC 上的那片 1MB SRAM"变成一块可以像数组一样
 *             直接读写的外部内存 —— 适合放大容量缓存、图片、DMA 缓冲
 *  依赖     : StdPeriph 的 FSMC（**工程 RTE 里必须勾上
 *             StdPeriph Drivers → FSMC**，否则 stm32f4xx_fsmc.c 不会被编译）
 *  标准库关键词 : FSMC_NORSRAMInit / FSMC_NORSRAMStructInit /
 *                 FSMC_NORSRAMCmd / RCC_AHB3PeriphClockCmd(FSMC) /
 *                 GPIO_Init(FSMC_xx 复用)
 *
 *  【为什么 MCU 要外挂 SRAM（面试点）】
 *    F407ZGT6 内部只有 128KB RAM + 64KB CCM —— 存一张 320×240 的
 *    16 位图片就要 150KB，直接放不下。FSMC（可变静态存储控制器）就是
 *    专门干这个的：把外部存储器芯片"映射"进 MCU 的地址空间，
 *    访问它跟访问普通变量一样（一句 `p[i] = x`，不需要任何函数）。
 *
 *  【接线（普中-天马 F407开发板 · 实物核对：U12 = IS62WV51216BLL-55TLI）】
 *      数据线 : FSMC_D0 ~ FSMC_D15（与 LCD 共用同一组总线）
 *      地址线 : FSMC_A0  ~ FSMC_A18      ← ⚠ 见下方"地址线交换"提醒
 *      片选   : CE = FSMC_NE3  (PG10)     ← ★ 与 LCD 的 NE4(PG12) 不同 bank，
 *                                            所以 LCD 和 SRAM 可以同时用
 *      写使能 : WE = FSMC_NWE  (PD5)
 *      读使能 : OE = FSMC_NOE  (PD4)
 *      字节选择: LB = FSMC_NBL0 (PE0)   UB = FSMC_NBL1 (PE1)
 *
 *  ⚠⚠ 本板的一个"奇怪接线"（正点原子/普中都有的坑，知道就行，不影响使用）:
 *      SRAM 的 A10 脚接的是 FSMC_A17，SRAM 的 A17 脚接的是 FSMC_A10
 *      —— 也就是这两根地址线**交换了**。
 *      后果：芯片内部的"页/行列"顺序被打乱，但整片仍是一块**可正常
 *      读写的 RAM**（写入什么读出来就是什么）。只有当你要做 DMA 到
 *      LCD 的"地址对齐搬运"、或研究内存布局时才需要留意。
 *
 *  【地址空间】
 *      FSMC Bank1 的 NE3 子区 → 基地址 0x68000000，长度 64MB（硬件预留），
 *      实际只用前 1MB（512K × 16bit）。
 *
 *  使用方式 :
 *      if (SRAM_Init() == 0) {                       // ① 初始化 + 自检
 *          SRAM_WriteBytes(0, buf, 1024);            // ② 按字节写
 *          volatile uint16_t *p = SRAM_Ptr();         // ③ 或当普通数组用
 *          p[0] = 0x1234;
 *      }
 *
 *  移植指引 : 换 bank 改 SYS_SRAM_BANK / 基地址；速度不够或读写出错改
 *             区块 1 的三个时序宏。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 0 = 不编译本模块（板上没焊 SRAM 时置 0，省 Flash） */
#ifndef SYS_SRAM_ENABLE
#define SYS_SRAM_ENABLE     1
#endif

/* 本板：FSMC Bank1 · 子区 NE3（PG10 做片选） */
#define SYS_SRAM_BANK       FSMC_Bank1_NORSRAM3
#define SYS_SRAM_BASE_ADDR  0x68000000UL        /* NE3 子区基地址 */
#define SYS_SRAM_SIZE_BYTES (1024UL * 1024UL)   /* 512K × 16bit = 1MB */
#define SYS_SRAM_WORDS      (512UL * 1024UL)    /* 半字数（16 位） */

/* FSMC 时序（单位：HCLK 周期，1 周期 ≈ 5.95ns @168MHz）
 *
 *  ★ 实物核对：板上 U12 丝印下方的芯片型号是 **IS62WV51216BLL-55TLI**
 *    —— `-55` 就是**存取时间 55ns**（BLL = 低压版，TLI = TSOP44 + 工业级）。
 *    FSMC 模式 A 的一次访问 ≈ (ADDR_SETUP + DATA_SETUP) 个 HCLK，所以：
 *        9 周期 ≈ 53.5ns  ← **小于 55ns，是超频用的，不可靠！**
 *       12 周期 ≈ 71.4ns  ← 默认值，留有充足余量
 *  ⚠ 千万不要为了"快一点"把 DATA_SETUP 压到 9 以下：
 *    典型的坏现象不是"完全不工作"，而是**偶发读回错值**（长时间跑才暴露），
 *    非常难查。125ns 以内都是安全的（-55 留了很多余量）。
 *  想要更快只能换 -45 / -35 速度等级的芯片，或改走"页模式突发读"。 */
#define SYS_SRAM_ADDR_SETUP 0
#define SYS_SRAM_ADDR_HOLD  0
#define SYS_SRAM_DATA_SETUP 12
#define SYS_SRAM_BUS_TURN   0

/* 编译期自检：容量必须是 2 的整数次幂（下面用掩码做取模，非 2 幂会错） */
#if ((SYS_SRAM_SIZE_BYTES & (SYS_SRAM_SIZE_BYTES - 1UL)) != 0UL)
#error "SRAM size must be a power of two"
#endif


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化 FSMC + 芯片自检
 * 返回 : 0 = 成功（全片抽检通过）；1 = 自检失败（没焊 / 时序太紧 / 片选错）
 * 说明 : 自检会在几个位置写"互补图案"再读回比对，耗时约几十微秒
 * 示例 : if (SRAM_Init() != 0) printf("SRAM 有问题\r\n"); */
uint8_t SRAM_Init(void);

/* 是否已初始化（0 = 还没 Init 过） */
uint8_t SRAM_IsReady(void);

/* 容量（字节）—— 固定返回 SYS_SRAM_SIZE_BYTES，方便上层算缓冲区 */
uint32_t SRAM_GetSize(void);

/* 拿"当普通数组用"的指针：返回基地址，之后 p[i] 直接读写
 * 说明 : 这是本模块最常用的入口 —— FSMC 已经把它映射进地址空间，
 *        读写它和读写内部 RAM 的写法完全一样
 * 示例 : volatile uint16_t *p = SRAM_Ptr();
 *        p[0] = 0xABCD;  uint16_t v = p[0]; */
volatile uint16_t *SRAM_Ptr(void);

/* 按"半字(16 位)"读写：offset 为第几个半字（0 ~ SYS_SRAM_WORDS-1）
 * 返回 : 0 = 成功；1 = 越界 */
uint16_t SRAM_ReadWord (uint32_t offset);
uint8_t  SRAM_WriteWord(uint32_t offset, uint16_t value);

/* 按"字节"读写：offset 为字节偏移（0 ~ SYS_SRAM_SIZE_BYTES-1）
 * 说明 : FSMC 会自动用 NBL0/NBL1 选中高/低字节，不用你关心
 * 返回 : 0 = 成功；1 = 越界 */
uint8_t SRAM_ReadBytes (uint32_t offset, uint8_t  *buf, uint32_t len);
uint8_t SRAM_WriteBytes(uint32_t offset, const uint8_t *buf, uint32_t len);

/* 按"半字数组"批量读写（比逐字节快，DMA/图片搬运首选）
 * 返回 : 0 = 成功；1 = 越界 */
uint8_t SRAM_ReadWords (uint32_t offset, uint16_t *buf, uint32_t count);
uint8_t SRAM_WriteWords(uint32_t offset, const uint16_t *buf, uint32_t count);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 内存自检（在指定范围内做图案测试）
 * 参数 : offset / len —— 字节偏移与长度（0 与 0 表示"全片抽检"）
 * 返回 : 0 = 全通过；非 0 = 第一个出错的半字序号（1 起）；0xFFFFFFFF = 参数越界
 * 说明 : 会**破坏**该区域原有数据；做正式产品时请留一块"自检专用区"
 * 示例 : uint32_t bad = SRAM_Test(0, 0);
 *        if (bad) printf("SRAM 坏点 @ 半字第 %lu 个\r\n", bad); */
uint32_t SRAM_Test(uint32_t offset, uint32_t len);

/* 全片清零（1MB 大约 3~5 毫秒，比内存里的 memset 稍慢） */
void SRAM_Clear(void);

/* 速度参考：测一次"写满 1MB + 读回校验"用多少微秒
 * 用途 : 换板/改时序后对比一下，确认没把时序配得太保守
 * 返回 : 微秒数（用 DWT 计时，不含 Cache 影响——FSMC 区域本来就不走 Cache） */
uint32_t SRAM_SpeedTestUs(void);

#endif /* __FWLIB_SRAM_H */
