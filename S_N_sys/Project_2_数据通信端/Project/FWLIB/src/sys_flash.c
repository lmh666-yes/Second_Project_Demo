#include "sys_flash.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */

/* ================================================================
 *  sys_flash.c —— 【系统】内部 Flash 擦写与参数保存模块  实现文件
 * ================================================================
 *  实现要点 :
 *   ① 扇区定位：F4 前 4 个扇区各 16KB，第 5 个 64KB，
 *      之后每扇区 128KB —— 用地址表查（见 flash_sectors[]）；
 *   ② "字"编程：逐个 32 位字调用 FLASH_ProgramWord，
 *      写完立即回读比对（不一致立刻返回错误）；
 *   ③ 参数区布局：[魔术字][长度][数据(字补齐)][校验和]，
 *      读回时逐项验证，魔术字不符 = 从未保存过。
 * ================================================================ */


/* ================================================================
 *                    配置数据
 * ================================================================ */
/* Flash 扇区表（STM32F407ZG，1MB）
 * 数值来源：RM0090"Flash 模块组织"——换型号请先核对手册 */
typedef struct {
    uint32_t base;      /* 扇区起始地址 */
    uint32_t size;      /* 扇区大小（字节） */
    uint16_t sector;    /* SPL 扇区常量（FLASH_Sector_x） */
} SysFlashSector_t;

static const SysFlashSector_t flash_sectors[] = {
    { 0x08000000UL, 0x04000UL, FLASH_Sector_0  },   /* 16KB  */
    { 0x08004000UL, 0x04000UL, FLASH_Sector_1  },
    { 0x08008000UL, 0x04000UL, FLASH_Sector_2  },
    { 0x0800C000UL, 0x04000UL, FLASH_Sector_3  },
    { 0x08010000UL, 0x10000UL, FLASH_Sector_4  },   /* 64KB  */
    { 0x08020000UL, 0x20000UL, FLASH_Sector_5  },   /* 128KB */
    { 0x08040000UL, 0x20000UL, FLASH_Sector_6  },
    { 0x08060000UL, 0x20000UL, FLASH_Sector_7  },
    { 0x08080000UL, 0x20000UL, FLASH_Sector_8  },
    { 0x080A0000UL, 0x20000UL, FLASH_Sector_9  },
    { 0x080C0000UL, 0x20000UL, FLASH_Sector_10 },
    { 0x080E0000UL, 0x20000UL, FLASH_Sector_11 },
};

#define SYS_FLASH_SECTOR_COUNT  (sizeof(flash_sectors) / sizeof(flash_sectors[0]))

/* 编译期护栏：扇区表 12 项（1MB 型号），增删时提醒同步 */
typedef char sys_flash_sector_check[(SYS_FLASH_SECTOR_COUNT == 12U) ? 1 : -1];


/* ================================================================
 *                    内部辅助
 * ================================================================ */
/* 地址 → 扇区常量；不在 Flash 范围内返回 0xFF */
static uint16_t sys_flash_sector_of(uint32_t addr)
{
    for (uint8_t i = 0U; i < (uint8_t)SYS_FLASH_SECTOR_COUNT; i++) {
        if ((addr >= flash_sectors[i].base) &&
            (addr <  (flash_sectors[i].base + flash_sectors[i].size))) {
            return flash_sectors[i].sector;
        }
    }
    return 0xFFU;
}

/* SPL 操作状态 → 本模块错误码 */
static uint8_t sys_flash_map_status(FLASH_Status st)
{
    if (st == FLASH_COMPLETE) return SYS_FLASH_OK;
    return SYS_FLASH_ERR_WRITE;      /* 写保护 / 编程错误等统一归为写失败 */
}

/* 擦写前的公共准备：解锁 + 清挂起标志 */
static void sys_flash_prepare(void)
{
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 擦除 addr 所在扇区 */
uint8_t SYS_FLASH_EraseSector(uint32_t addr)
{
    FLASH_Status st;
    uint16_t     sect;

    sect = sys_flash_sector_of(addr);
    if (sect == 0xFFU) return SYS_FLASH_ERR_PARAM;

    sys_flash_prepare();
    st = FLASH_EraseSector((uint32_t)sect, VoltageRange_3);
    FLASH_Lock();

    return sys_flash_map_status(st);
}

/* 写 len 字节（addr 字对齐；逐字编程 + 回读校验；尾字补 0xFF） */
uint8_t SYS_FLASH_Write(uint32_t addr, const void *data, uint32_t len)
{
    const uint8_t *src = (const uint8_t *)data;
    FLASH_Status   st;
    uint32_t i;
    uint8_t  b;

    if ((data == NULL) || (len == 0U)) return SYS_FLASH_ERR_PARAM;
    if ((addr & 0x3UL) != 0UL)         return SYS_FLASH_ERR_PARAM;
    if (sys_flash_sector_of(addr) == 0xFFU)              return SYS_FLASH_ERR_PARAM;
    if (sys_flash_sector_of(addr + len - 1UL) == 0xFFU)  return SYS_FLASH_ERR_PARAM;

    sys_flash_prepare();

    for (i = 0U; i < len; i += 4UL) {
        uint32_t word = 0xFFFFFFFFUL;       /* 未用字节保持 0xFF */

        for (b = 0U; (b < 4U) && ((i + b) < len); b++) {
            word &= ~(0xFFUL << (8U * b));
            word |= ((uint32_t)src[i + b] << (8U * b));
        }

        st = FLASH_ProgramWord(addr + i, word);
        if (st != FLASH_COMPLETE) {
            FLASH_Lock();
            return sys_flash_map_status(st);
        }
        if (*(volatile uint32_t *)(addr + i) != word) {     /* 回读校验 */
            FLASH_Lock();
            return SYS_FLASH_ERR_WRITE;
        }
    }

    FLASH_Lock();
    return SYS_FLASH_OK;
}

/* 读取 Flash（直接按字节拷贝；Flash 可随意读） */
void SYS_FLASH_Read(uint32_t addr, void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)addr;
    uint8_t       *d = (uint8_t *)buf;
    uint32_t i;

    if ((buf == NULL) || (len == 0U)) return;

    for (i = 0U; i < len; i++) {
        d[i] = p[i];
    }
}


/* ================================================================
 *                    区块 3：扩展功能（参数区保存 / 读回）
 * ================================================================ */
/* 保存参数：[魔术字][长度][数据(字对齐补齐)][校验和(字节累加)] */
uint8_t SYS_FLASH_SaveParams(const void *data, uint16_t len)
{
    const uint8_t *src = (const uint8_t *)data;
    uint32_t magic = SYS_FLASH_MAGIC;
    uint32_t lenw  = (uint32_t)len;
    uint32_t sum   = 0U;
    uint32_t sumaddr;
    uint32_t i;
    uint8_t  rc;

    if ((data == NULL) || (len == 0U) || (len > SYS_FLASH_PARAM_MAX)) {
        return SYS_FLASH_ERR_PARAM;
    }

    /* ① 整扇区擦除（参数区所在扇区必须没有程序代码） */
    rc = SYS_FLASH_EraseSector(SYS_FLASH_PARAM_ADDR);
    if (rc != SYS_FLASH_OK) return rc;

    /* ② 校验和 = 长度 + 所有数据字节（简易累加，够用且省事） */
    sum = (uint32_t)len;
    for (i = 0U; i < (uint32_t)len; i++) sum += src[i];

    /* ③ 头 → 数据 → 校验和（数据后的校验和地址按字对齐） */
    sumaddr = SYS_FLASH_PARAM_ADDR + 8UL + ((((uint32_t)len + 3UL)) & ~3UL);

    rc = SYS_FLASH_Write(SYS_FLASH_PARAM_ADDR,      &magic, 4UL);
    if (rc == SYS_FLASH_OK) rc = SYS_FLASH_Write(SYS_FLASH_PARAM_ADDR + 4UL, &lenw, 4UL);
    if (rc == SYS_FLASH_OK) rc = SYS_FLASH_Write(SYS_FLASH_PARAM_ADDR + 8UL, src, (uint32_t)len);
    if (rc == SYS_FLASH_OK) rc = SYS_FLASH_Write(sumaddr, &sum, 4UL);

    return rc;
}

/* 读回并校验；out_len 可传 0（NULL） */
uint8_t SYS_FLASH_LoadParams(void *data, uint16_t max_len, uint16_t *out_len)
{
    uint32_t       magic;
    uint32_t       len;
    uint32_t       sum_stored;
    uint32_t       sum_calc;
    const uint8_t *p;
    uint32_t       i;

    if (data == NULL) return SYS_FLASH_ERR_PARAM;

    /* ① 魔术字：不符 = 从未保存过（首次上电的正常现象） */
    SYS_FLASH_Read(SYS_FLASH_PARAM_ADDR, &magic, 4UL);
    if (magic != SYS_FLASH_MAGIC) return SYS_FLASH_ERR_EMPTY;

    /* ② 长度合法性 */
    SYS_FLASH_Read(SYS_FLASH_PARAM_ADDR + 4UL, &len, 4UL);
    if ((len == 0U) || (len > (uint32_t)max_len) || (len > SYS_FLASH_PARAM_MAX)) {
        return SYS_FLASH_ERR_PARAM;
    }

    /* ③ 拷贝数据、重算校验和、与存储值比对 */
    SYS_FLASH_Read(SYS_FLASH_PARAM_ADDR + 8UL, data, len);

    sum_calc = len;
    p = (const uint8_t *)data;
    for (i = 0U; i < len; i++) sum_calc += p[i];

    SYS_FLASH_Read(SYS_FLASH_PARAM_ADDR + 8UL + (((len + 3UL)) & ~3UL),
                   &sum_stored, 4UL);
    if (sum_stored != sum_calc) return SYS_FLASH_ERR_SUM;

    if (out_len != NULL) *out_len = (uint16_t)len;
    return SYS_FLASH_OK;
}
