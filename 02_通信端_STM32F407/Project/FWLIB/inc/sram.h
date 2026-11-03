#ifndef __FWLIB_SRAM_H
#define __FWLIB_SRAM_H

#include "stm32f4xx.h"

/*
 *  sram.h : 板载 FSMC 外扩 SRAM（IS62WV51216）头文件
 *  功能 : 把挂在 FSMC 上的 1MB SRAM 映射成可直接读写的地址空间，可当普通
 *         数组使用，用于大容量缓存、图片、DMA 缓冲
 *  依赖 : 工程 RTE 需勾选 StdPeriph Drivers -> FSMC，否则 stm32f4xx_fsmc.c
 *         不参与编译
 *  标准库接口 : FSMC_NORSRAMInit / FSMC_NORSRAMStructInit /
 *               FSMC_NORSRAMCmd / RCC_AHB3PeriphClockCmd(FSMC)
 *  依据 : F407ZGT6 内部 128KB RAM + 64KB CCM，320x240 的 16 位图片需
 *         150KB，放不下，故外扩
 *
 * 约束 : 仅在 SRAM_Init() 返回 0 后可用；未初始化时读写结果不确定
 *
 *  接线（普中-天马 F407 开发板，U12 = IS62WV51216BLL-55TLI）:
 *      数据线  : FSMC_D0 ~ FSMC_D15，与 LCD 共用同一组总线
 *      地址线  : FSMC_A0 ~ FSMC_A18
 *      片选    : CE = FSMC_NE3 (PG10)，与 LCD 的 NE4 (PG12) 不同 bank
 *      写使能  : WE = FSMC_NWE (PD5)
 *      读使能  : OE = FSMC_NOE (PD4)
 *      字节选择: LB = FSMC_NBL0 (PE0)，UB = FSMC_NBL1 (PE1)
 *  地址线交换 : SRAM 的 A10 接 FSMC_A17，A17 接 FSMC_A10，内部页与行列顺序随之
 *      改变，但整片仍可正常读写，只在地址对齐搬运时需留意
 *  地址空间 : FSMC Bank1 的 NE3 子区基地址 0x68000000，硬件预留 64MB，
 *      实际只用前 1MB（512K x 16bit）
 *  移植 : 换 bank 改 SYS_SRAM_BANK 与基地址；读写出错查区块 1 的时序宏
 */


/* 区块 1：定义与宏定义区（换板子只改这里） */
/* 0 = 不编译本模块（未焊 SRAM 时置 0，节省 Flash） */
#ifndef SYS_SRAM_ENABLE
#define SYS_SRAM_ENABLE     1
#endif

/* 本板：FSMC Bank1 · 子区 NE3（PG10 做片选） */
#define SYS_SRAM_BANK       FSMC_Bank1_NORSRAM3
#define SYS_SRAM_BASE_ADDR  0x68000000UL        /* NE3 子区基地址 */
#define SYS_SRAM_SIZE_BYTES (1024UL * 1024UL)   /* 512K × 16bit = 1MB */
#define SYS_SRAM_WORDS      (512UL * 1024UL)    /* 半字数（16 位） */

/* FSMC 时序，单位 HCLK 周期，1 周期 ≈ 5.95ns @168MHz
 * U12 型号 IS62WV51216BLL-55TLI，-55 为存取时间 55ns。FSMC 模式 A 一次访问
 * ≈ (ADDR_SETUP + DATA_SETUP) 个 HCLK：9 周期 ≈ 53.5ns 小于 55ns，不可靠；
 * 12 周期 ≈ 71.4ns 为默认值，余量充足
 * DATA_SETUP 不得压到 9 以下，否则偶发读回错值，长时间运行才暴露；125ns 以内均
 * 安全。要更快只能换 -45 / -35 芯片或走页模式突发读 */
#define SYS_SRAM_ADDR_SETUP 0
#define SYS_SRAM_ADDR_HOLD  0
#define SYS_SRAM_DATA_SETUP 12
#define SYS_SRAM_BUS_TURN   0

/* 编译期自检：容量必须是 2 的整数次幂，取模依赖低位掩码 */
#if ((SYS_SRAM_SIZE_BYTES & (SYS_SRAM_SIZE_BYTES - 1UL)) != 0UL)
#error "SRAM size must be a power of two"
#endif


/* 区块 2：基础功能 */
/* 初始化 FSMC 并做芯片自检
 * 返回 : 0 = 成功（全片抽检通过）；1 = 自检失败（未焊 / 时序过紧 / 片选错误）
 * 说明 : 在若干位置写入互补图案再读回比对 */
uint8_t SRAM_Init(void);

/* 是否已初始化（0 = 未调用过 SRAM_Init） */
uint8_t SRAM_IsReady(void);

/* 容量（字节），固定返回 SYS_SRAM_SIZE_BYTES */
uint32_t SRAM_GetSize(void);

/* 返回基地址指针，之后按 p[i] 直接读写，FSMC 已映射进地址空间 */
volatile uint16_t *SRAM_Ptr(void);

/* 按"半字(16 位)"读写：offset 为第几个半字（0 ~ SYS_SRAM_WORDS-1）
 * 返回 : 0 = 成功；1 = 越界 */
uint16_t SRAM_ReadWord (uint32_t offset);
uint8_t  SRAM_WriteWord(uint32_t offset, uint16_t value);

/* 按字节读写：offset 为字节偏移（0 ~ SYS_SRAM_SIZE_BYTES-1）
 * 返回 : 0 = 成功；1 = 越界。高低字节由 FSMC 用 NBL0/NBL1 自动选择 */
uint8_t SRAM_ReadBytes (uint32_t offset, uint8_t  *buf, uint32_t len);
uint8_t SRAM_WriteBytes(uint32_t offset, const uint8_t *buf, uint32_t len);

/* 按半字数组批量读写，快于逐字节；返回 0 = 成功，1 = 越界 */
uint8_t SRAM_ReadWords (uint32_t offset, uint16_t *buf, uint32_t count);
uint8_t SRAM_WriteWords(uint32_t offset, const uint16_t *buf, uint32_t count);


/* 区块 3：扩展功能 */
/* 内存自检：在指定范围内做图案测试，会破坏该区域原有数据
 * 参数 : offset / len 为字节偏移与长度，均传 0 表示全片抽检
 * 返回 : 0 = 全通过；非 0 = 第一个出错的半字序号（1 起）；0xFFFFFFFF = 参数越界 */
uint32_t SRAM_Test(uint32_t offset, uint32_t len);

/* 全片清零，1MB 约 3~5 毫秒 */
void SRAM_Clear(void);

/* 测一次写满 1MB 并读回校验的耗时，供换板或改时序后对比
 * 返回 : 微秒数（DWT 计时；FSMC 区域不走 Cache） */
uint32_t SRAM_SpeedTestUs(void);

#endif /* __FWLIB_SRAM_H */
