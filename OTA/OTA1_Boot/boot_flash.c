#include "boot_flash.h"

#include <string.h>

#include "stm32f4xx.h"

/* ---- 寄存器位 ----
   能用设备头（stm32f4xx.h）里现成的就用它，别重复定义 —— 同名重定义会报 #47-D 警告。
   设备头里已有：FLASH_SR_BSY / FLASH_CR_PG / FLASH_CR_SER / FLASH_CR_SNB / FLASH_CR_PSIZE /
                 FLASH_CR_STRT / FLASH_CR_LOCK，本文件直接用。 */
#define FLASH_KEY1              0x45670123UL      /* SPL 的解锁键，设备头里没有 */
#define FLASH_KEY2              0xCDEF89ABUL
#define FLASH_CR_SNB_POS        3U                /* 扇区号在 CR 里的起始位（FLASH_CR_SNB = 0xF8） */
#define FLASH_CR_PSIZE_WORD     FLASH_CR_PSIZE_1  /* PSIZE = 10b = 32 位并行（2.7~3.6V） */

/* 扇区起始地址（F407ZE 512KB）：0~3 = 16KB，4 = 64KB，5~7 = 128KB */
static const uint32_t s_sectorStart[8] = {
    0x08000000UL, 0x08004000UL, 0x08008000UL, 0x0800C000UL,
    0x08010000UL, 0x08020000UL, 0x08040000UL, 0x08060000UL
};
static const uint32_t s_sectorSize[8] = {
    0x4000UL, 0x4000UL, 0x4000UL, 0x4000UL,
    0x10000UL, 0x20000UL, 0x20000UL, 0x20000UL
};

static void flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

/* 等 BSY 落下来；超时给个有限次数，避免 Bootloader 卡死在死循环里 */
static uint8_t flash_wait(void)
{
    uint32_t guard = 0x00800000UL;      /* 足够大的上限（擦一个 128KB 扇区也就毫秒级） */
    while ((FLASH->SR & FLASH_SR_BSY) != 0u) {
        if (--guard == 0u)
            return 0u;
    }
    return 1u;
}

uint32_t Boot_FlashSectorOf(uint32_t addr)
{
    uint32_t i;
    for (i = 0u; i < 8u; i++) {
        if (addr >= s_sectorStart[i] && addr < (s_sectorStart[i] + s_sectorSize[i]))
            return i;
    }
    return 0xFFFFFFFFu;
}

uint8_t Boot_FlashEraseSector(uint32_t addr)
{
    const uint32_t snb = Boot_FlashSectorOf(addr);
    if (snb == 0xFFFFFFFFu)
        return 0u;

    flash_unlock();
    if (!flash_wait()) {
        flash_lock();
        return 0u;
    }

    /* 清掉上次残留的错误标志（写 1 清除） */
    FLASH->SR = 0xFFFFFFFFu;

    FLASH->CR &= ~(FLASH_CR_SNB | FLASH_CR_PSIZE);
    FLASH->CR |= FLASH_CR_SER | (snb << FLASH_CR_SNB_POS) | FLASH_CR_PSIZE_WORD;
    FLASH->CR |= FLASH_CR_STRT;

    if (!flash_wait()) {
        FLASH->CR &= ~FLASH_CR_SER;
        flash_lock();
        return 0u;
    }

    FLASH->CR &= ~FLASH_CR_SER;
    flash_lock();
    return 1u;
}

uint8_t Boot_FlashProgram(uint32_t addr, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;

    if (p == 0 || len == 0u || (addr & 0x3u) != 0u)
        return 0u;

    flash_unlock();
    if (!flash_wait()) {
        flash_lock();
        return 0u;
    }
    FLASH->SR = 0xFFFFFFFFu;

    for (i = 0u; i < len; i += 4u) {
        uint32_t w = 0xFFFFFFFFu;       /* 尾部不足 4 字节时填 0xFF（等于不写） */
        uint32_t k;
        for (k = 0u; k < 4u && (i + k) < len; k++)
            ((uint8_t *)&w)[k] = p[i + k];

        FLASH->CR &= ~FLASH_CR_PSIZE;
        FLASH->CR |= FLASH_CR_PG | FLASH_CR_PSIZE_WORD;
        *(volatile uint32_t *)(addr + i) = w;

        if (!flash_wait() || *(volatile uint32_t *)(addr + i) != w) {
            FLASH->CR &= ~FLASH_CR_PG;
            flash_lock();
            return 0u;                  /* 回读不符：写失败，别继续 */
        }
        FLASH->CR &= ~FLASH_CR_PG;
    }

    flash_lock();
    return 1u;
}

void Boot_FlashRead(uint32_t addr, void *buf, uint32_t len)
{
    if (buf != 0 && len != 0u)
        memcpy(buf, (const void *)addr, len);
}
