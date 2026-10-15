#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

/* ============================================================
 *  boot_flash —— Bootloader 用的最小 Flash 擦/写/读（直接操作寄存器）
 *
 *  为什么不直接用工程的 sys_flash.c：
 *    sys_flash.c 依赖 STM32 标准外设库（FLASH_Unlock/FLASH_EraseSector 等），
 *    而 Bootloader 只有 16KB 空间，带 FreeRTOS + 整套 SPL 会把 Sector 0 撑爆。
 *    所以这里只做"擦一个扇区 / 写几个字 / 读回来"，不引入任何 SPL 源文件。
 *
 *  适用范围：板1 STM32F407ZE（512KB），Sector 0~3 = 16KB、Sector 4 = 64KB、Sector 5~7 = 128KB。
 *            寄存器位定义按 RM0090（F4 参考手册）的 FLASH 章节，与 SPL 里的宏一一对应。
 * ============================================================ */

#include <stdint.h>

/* 擦掉 addr 所在的整个扇区（最小擦除单位；addr 必须在芯片 Flash 内）。
   返回 1 = 成功，0 = 失败（地址不在 Flash / 超时）。 */
uint8_t Boot_FlashEraseSector(uint32_t addr);

/* 从 addr 写入 len 字节（addr 必须 4 字节对齐；目标必须先擦过）。
   按字编程，逐字回读校验；len 不是 4 的倍数时尾部补 0xFF（Flash 里本来就是 0xFF，写入等于不变）。
   返回 1 = 成功，0 = 失败（对齐/校验不过/超时）。 */
uint8_t Boot_FlashProgram(uint32_t addr, const void *data, uint32_t len);

/* 读 Flash（Flash 映射在地址空间，memcpy 直读）。 */
void Boot_FlashRead(uint32_t addr, void *buf, uint32_t len);

/* addr 所在扇区号；不在 Flash 内返回 0xFFFFFFFF。 */
uint32_t Boot_FlashSectorOf(uint32_t addr);

#endif /* BOOT_FLASH_H */
