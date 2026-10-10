#include "sys_flash.h"

/* 内部 Flash 擦写与参数保存
 * 扇区定位用 flash_sectors[] 地址表：F4 前 4 个扇区各 16KB，第 5 个 64KB，之后每扇区 128KB
 * 编程以 32 位字为单位调用 FLASH_ProgramWord，写完立即回读比对，不一致则返回错误
 * 参数区布局 [魔术字][序号][长度][数据(字补齐)][校验和]；A/B 两块扇区交替写，保存中途掉电时旧块仍完整
 */


/* 配置数据 */
/* Flash 扇区表（STM32F407ZG，1MB）
 * 数值来源 RM0090 Flash 模块组织；换型号需先核对手册 */
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


/* 内部辅助 */
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


/* 基础功能 */
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

/* 读取 Flash：按字节拷贝，无需解锁 */
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


/* 参数区保存与读回：每扇区一份，A/B 两块互为备份
 * 布局 [魔术字 4B][序号 4B][长度 4B][数据(字对齐补齐)][校验和 4B]，偏移 +0 / +4 / +8 / +12
 * 序号每次保存 +1（0xFFFFFFFF → 0 回绕），用于判断 A/B 两块谁更新
 * 校验和 = 长度 + 所有数据字节；保存写另一块，校验和最后落盘，掉电时读参数退回旧块
 */
#define SYS_FLASH_PH_SEQ        4UL     /* 序号字段偏移 */
#define SYS_FLASH_PH_LEN        8UL     /* 长度字段偏移 */

/* 数据区起始偏移 */
#define SYS_FLASH_PH_DATA       12UL

/* 从 base 读头部；magic/len/seq 任一出参可传 NULL */
static void sys_flash_read_head(uint32_t base, uint32_t *magic, uint32_t *seq, uint32_t *len)
{
    if (magic != NULL) SYS_FLASH_Read(base + 0UL,          magic, 4UL);
    if (seq   != NULL) SYS_FLASH_Read(base + SYS_FLASH_PH_SEQ, seq,   4UL);
    if (len   != NULL) SYS_FLASH_Read(base + SYS_FLASH_PH_LEN, len,   4UL);
}

/* 分块计算校验和：长度 + 所有数据字节
 * 不整段拷进调用方缓冲（LoadParams 先验证后拷贝），用 64 字节栈缓冲循环读 Flash */
static uint32_t sys_flash_sum_of(uint32_t base, uint32_t len)
{
    uint8_t  chunk[64];
    uint32_t sum = len;
    uint32_t off = 0UL;
    uint32_t n;
    uint32_t i;

    while (off < len) {
        n = len - off;
        if (n > (uint32_t)sizeof(chunk)) n = (uint32_t)sizeof(chunk);
        SYS_FLASH_Read(base + SYS_FLASH_PH_DATA + off, chunk, n);
        for (i = 0U; i < n; i++) sum += chunk[i];
        off += n;
    }
    return sum;
}

/* 校验 base 处是否为完整可用参数：有效返回 1，并通过 len_out / seq_out 带出长度与序号 */
static uint8_t sys_flash_valid(uint32_t base, uint32_t *len_out, uint32_t *seq_out)
{
    uint32_t magic;
    uint32_t seq;
    uint32_t len;
    uint32_t sum_stored;

    sys_flash_read_head(base, &magic, &seq, &len);
    if (magic != SYS_FLASH_MAGIC) return 0U;
    if ((len == 0UL) || (len > (uint32_t)SYS_FLASH_PARAM_MAX)) return 0U;

    SYS_FLASH_Read(base + SYS_FLASH_PH_DATA + ((len + 3UL) & ~3UL), &sum_stored, 4UL);
    if (sum_stored != sys_flash_sum_of(base, len)) return 0U;

    if (len_out != NULL) *len_out = len;
    if (seq_out != NULL) *seq_out = seq;
    return 1U;
}

/* seq_a 是否比 seq_b 更新（0xFFFFFFFF → 0 回绕也正确） */
static uint8_t sys_flash_seq_newer(uint32_t seq_a, uint32_t seq_b)
{
    uint32_t d = seq_a - seq_b;
    return (uint8_t)((d != 0UL) && (d < 0x80000000UL));
}

/* 保存参数：写入另一块，旧块保持完整直到新块校验通过
 * 返回 SYS_FLASH_OK 或错误码
 * len 取值范围 1 ~ SYS_FLASH_PARAM_MAX（字节） */
uint8_t SYS_FLASH_SaveParams(const void *data, uint16_t len)
{
    const uint8_t *src = (const uint8_t *)data;
    uint32_t magic = SYS_FLASH_MAGIC;
    uint32_t lenw  = (uint32_t)len;
    uint32_t sum   = 0U;
    uint32_t seq   = 0UL;
    uint32_t sumaddr;
    uint32_t target;
    uint32_t other;
    uint32_t va;
    uint32_t vb;
    uint32_t sa = 0UL;
    uint32_t sb = 0UL;
    uint32_t la = 0UL;
    uint32_t lb = 0UL;
    uint32_t i;
    uint8_t  rc;

    if ((data == NULL) || (len == 0U) || (len > SYS_FLASH_PARAM_MAX)) {
        return SYS_FLASH_ERR_PARAM;
    }

    /* 1) 检查两块各自是否有效、序号多大，决定这次写哪块、序号取多少 */
    va = sys_flash_valid(SYS_FLASH_PARAM_ADDR,  &la, &sa);
    vb = sys_flash_valid(SYS_FLASH_PARAM_ADDR2, &lb, &sb);

    if (va != 0U) {
        if ((vb == 0U) || sys_flash_seq_newer(sa, sb)) {
            target = SYS_FLASH_PARAM_ADDR2;   /* A 更新（或 B 坏）→ 写 B */
            other  = SYS_FLASH_PARAM_ADDR;
            seq    = sa + 1UL;
        } else {
            target = SYS_FLASH_PARAM_ADDR;    /* B 更新 → 写 A */
            other  = SYS_FLASH_PARAM_ADDR2;
            seq    = sb + 1UL;
        }
    } else {
        target = SYS_FLASH_PARAM_ADDR;        /* A 坏 → 直接写 A */
        other  = SYS_FLASH_PARAM_ADDR2;
        seq    = (vb != 0U) ? (sb + 1UL) : 0UL;
    }
    (void)other;    /* 旧块无需清掉：下次写入自然会轮到它被擦 */

    /* 2) 目标块非空才擦除，省一次擦写寿命，也缩小掉电窗口 */
    {
        uint32_t m0;
        SYS_FLASH_Read(target, &m0, 4UL);
        if (m0 != 0xFFFFFFFFUL) {
            rc = SYS_FLASH_EraseSector(target);
            if (rc != SYS_FLASH_OK) return rc;
        }
    }

    /* 3) 校验和 = 长度 + 所有数据字节 */
    sum = (uint32_t)len;
    for (i = 0U; i < (uint32_t)len; i++) sum += src[i];

    /* 4) 依次写头、数据、校验和；校验和地址按字对齐
     * 校验和最后写：它落盘成功才代表这份参数完整 */
    sumaddr = target + SYS_FLASH_PH_DATA + ((((uint32_t)len + 3UL)) & ~3UL);

    rc = SYS_FLASH_Write(target + 0UL,           &magic, 4UL);
    if (rc == SYS_FLASH_OK) rc = SYS_FLASH_Write(target + SYS_FLASH_PH_SEQ, &seq, 4UL);
    if (rc == SYS_FLASH_OK) rc = SYS_FLASH_Write(target + SYS_FLASH_PH_LEN, &lenw, 4UL);
    if (rc == SYS_FLASH_OK) rc = SYS_FLASH_Write(target + SYS_FLASH_PH_DATA, src, (uint32_t)len);
    if (rc == SYS_FLASH_OK) rc = SYS_FLASH_Write(sumaddr, &sum, 4UL);
    if (rc != SYS_FLASH_OK) return rc;

    /* 5) 写完后整体复验，含校验和 */
    if (sys_flash_valid(target, NULL, NULL) == 0U) return SYS_FLASH_ERR_SUM;

    return SYS_FLASH_OK;
}

/* 读回并校验；out_len 可传 NULL
 * 顺序：先验长度与校验和，最后才拷入调用方缓冲；校验失败时调用方缓冲不被改写 */
uint8_t SYS_FLASH_LoadParams(void *data, uint16_t max_len, uint16_t *out_len)
{
    uint32_t len_a = 0UL;
    uint32_t len_b = 0UL;
    uint32_t seq_a = 0UL;
    uint32_t seq_b = 0UL;
    uint32_t src;
    uint32_t len;
    uint8_t  va;
    uint8_t  vb;

    if (data == NULL) return SYS_FLASH_ERR_PARAM;

    /* 1) 两块各自校验完整性，取有效且序号较新的那块
     *    保存中途掉电时坏块在这里被识别，自动退回旧块 */
    va = sys_flash_valid(SYS_FLASH_PARAM_ADDR,  &len_a, &seq_a);
    vb = sys_flash_valid(SYS_FLASH_PARAM_ADDR2, &len_b, &seq_b);

    if ((va != 0U) && (vb != 0U)) {
        if (sys_flash_seq_newer(seq_b, seq_a)) { src = SYS_FLASH_PARAM_ADDR2; len = len_b; }
        else                                   { src = SYS_FLASH_PARAM_ADDR;  len = len_a; }
    } else if (va != 0U) {
        src = SYS_FLASH_PARAM_ADDR;  len = len_a;
    } else if (vb != 0U) {
        src = SYS_FLASH_PARAM_ADDR2; len = len_b;
    } else {
        /* 两块都没有完整数据：区分从未保存过与两块都损坏 */
        uint32_t magic;
        sys_flash_read_head(SYS_FLASH_PARAM_ADDR, &magic, NULL, NULL);
        if (magic != SYS_FLASH_MAGIC) return SYS_FLASH_ERR_EMPTY;
        return SYS_FLASH_ERR_SUM;
    }

    /* 2) 容量检查也在拷贝之前 */
    if (len > (uint32_t)max_len) return SYS_FLASH_ERR_PARAM;

    /* 3) 已确认数据完整，执行拷贝 */
    SYS_FLASH_Read(src + SYS_FLASH_PH_DATA, data, len);

    if (out_len != NULL) *out_len = (uint16_t)len;
    return SYS_FLASH_OK;
}
