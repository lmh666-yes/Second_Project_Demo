#include "ota_image.h"

#include <string.h>

#include "boot_flash.h"
#include "ota_crc16.h"
#include "ota_part.h"

/* 逐块搬运/复验用的中间缓冲：1024 字节足够把 Flash 访问与 CRC 计算穿插起来，
   又小到不影响 Bootloader 的 RAM 占用（Boot 只用 SRAM，不占堆）。 */
#define BOOT_CHUNK      1024U
static uint8_t s_buf[BOOT_CHUNK];

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint8_t OtaImage_Pending(OtaImageInfo *info)
{
    uint8_t hdr[OTA_HDR_SIZE];
    uint16_t hcrc;
    uint32_t size;

    Boot_FlashRead(OTA_DL_ADDR, hdr, OTA_HDR_SIZE);

    /* 标识：四个字节逐字节比，不依赖端序 */
    if (hdr[OTA_HDR_OFF_MAGIC + 0u] != OTA_HDR_MAGIC_0 ||
        hdr[OTA_HDR_OFF_MAGIC + 1u] != OTA_HDR_MAGIC_1 ||
        hdr[OTA_HDR_OFF_MAGIC + 2u] != OTA_HDR_MAGIC_2 ||
        hdr[OTA_HDR_OFF_MAGIC + 3u] != OTA_HDR_MAGIC_3)
        return 0u;

    /* 头自身自校验：头损坏（比如写头那一刻掉电）就当"没有待装固件"，绝不猜 */
    hcrc = OTA_Crc16Block(hdr, 12u);
    if (hcrc != le16(&hdr[OTA_HDR_OFF_HCRC]))
        return 0u;

    size = le32(&hdr[OTA_HDR_OFF_SIZE]);
    if (size == 0u || size > OTA_MAX_IMAGE || size > OTA_APP_SIZE)
        return 0u;

    if (info != 0) {
        info->size = size;
        info->crc  = le16(&hdr[OTA_HDR_OFF_CRC]);
        info->ver  = le16(&hdr[OTA_HDR_OFF_VER]);
    }
    return 1u;
}

uint8_t OtaImage_Verify(const OtaImageInfo *info)
{
    uint32_t off = 0u;
    uint16_t crc = 0xFFFFu;

    if (info == 0 || info->size == 0u || info->size > OTA_MAX_IMAGE)
        return 0u;

    while (off < info->size) {
        uint32_t n = info->size - off;
        if (n > BOOT_CHUNK)
            n = BOOT_CHUNK;
        Boot_FlashRead(OTA_DL_ADDR + OTA_HDR_IMG_OFF + off, s_buf, n);
        crc = OTA_Crc16(s_buf, n, crc);
        off += n;
    }
    return (uint8_t)(crc == info->crc);
}

uint8_t OtaImage_Copy(const OtaImageInfo *info)
{
    uint32_t off = 0u;
    uint16_t crc = 0xFFFFu;
    uint32_t sector;
    uint32_t zero = 0u;

    if (info == 0 || info->size == 0u || info->size > OTA_APP_SIZE)
        return 0u;

    /* 1) 擦掉 App 运行区：Sector 1~3，各 16KB（48KB 总共）。
          注意 wipe 的是"运行区"，下载区（Sector 4）与参数区（Sector 7）都不碰。 */
    for (sector = 0u; sector < (OTA_APP_SIZE / 0x4000UL); sector++) {
        if (!Boot_FlashEraseSector(OTA_APP_ADDR + sector * 0x4000UL))
            return 0u;
    }

    /* 2) 逐块搬运：读下载区 → 写 App 区（Boot_FlashProgram 内部逐字回读校验） */
    while (off < info->size) {
        uint32_t n = info->size - off;
        if (n > BOOT_CHUNK)
            n = BOOT_CHUNK;
        Boot_FlashRead(OTA_DL_ADDR + OTA_HDR_IMG_OFF + off, s_buf, n);
        if (!Boot_FlashProgram(OTA_APP_ADDR + off, s_buf, n))
            return 0u;                 /* 失败：不动下载区头，下次上电重试 */
        off += n;
    }

    /* 3) 从 App 区回读复验：确认搬过去的字节就是下载区那一份 */
    off = 0u;
    while (off < info->size) {
        uint32_t n = info->size - off;
        if (n > BOOT_CHUNK)
            n = BOOT_CHUNK;
        Boot_FlashRead(OTA_APP_ADDR + off, s_buf, n);
        crc = OTA_Crc16(s_buf, n, crc);
        off += n;
    }
    if (crc != info->crc)
        return 0u;                     /* 复验不过：头保留，下次重来 */

    /* 4) 全流程成功，才作废下载区头：把头 4 字节写成 0。
          只写一个字、不用擦 64KB 扇区（擦一次是秒级），代价最小。 */
    (void)Boot_FlashProgram(OTA_DL_ADDR, &zero, 4u);
    return 1u;
}

uint8_t OtaImage_AppValid(void)
{
    const uint32_t sp = *(volatile uint32_t *)OTA_APP_ADDR;
    const uint32_t pc = *(volatile uint32_t *)(OTA_APP_ADDR + 4u);

    /* 栈顶必须落在 RAM 里：SRAM1/2 = 0x20000000~0x20020000，CCM = 0x10000000~0x1000FFFF */
    if (!((sp >= 0x20000000u && sp <= 0x20020000u) ||
          (sp >= 0x10000000u && sp <= 0x10010000u)))
        return 0u;

    /* 复位向量必须落在 App 区，且末位为 1（Thumb） */
    if (pc < OTA_APP_ADDR || pc >= OTA_APP_END)
        return 0u;
    if ((pc & 1u) == 0u)
        return 0u;

    return 1u;
}
