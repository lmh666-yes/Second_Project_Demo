#ifndef OTA_IMAGE_H
#define OTA_IMAGE_H

/* ============================================================
 *  ota_image —— Bootloader 里的"待安装固件"处理
 *
 *  下载区（0x08010000，Sector 4）的布局：
 *      [0 .. 15]            固件头（见 ota_part.h：'O''T''A''1' + 大小 + CRC + 版本 + 头 CRC）
 *      [16 .. 16+size-1]    镜像本体
 *
 *  幂等性（防变砖的关键）：
 *    搬运过程只读下载区、只写 App 区，**最后**才把下载区头作废。
 *    所以搬运到一半掉电 → 下次上电头还在 → 重新完整搬一遍；App 区永远是
 *    "完整的新程序" 或 "完整的旧程序"，不会出现刷一半的中间态。
 * ============================================================ */

#include <stdint.h>

typedef struct {
    uint32_t size;      /* 镜像字节数 */
    uint16_t crc;       /* 整镜像 CRC16-MODBUS */
    uint16_t ver;       /* 版本号（只给人看） */
} OtaImageInfo;

/* 下载区有没有"待安装的合法固件"（头四项齐全：标识、头 CRC、大小范围）。 */
uint8_t OtaImage_Pending(OtaImageInfo *info);

/* 复验整镜像 CRC（从下载区逐块读、边读边算，不占大 RAM）。1 = 通过。 */
uint8_t OtaImage_Verify(const OtaImageInfo *info);

/* 把下载区的镜像搬到 App 运行区：擦 App 扇区 → 逐块编程 → 回读复验 → 作废下载区头。
   1 = 成功（此时头已作废，本次启动不会再搬）。 */
uint8_t OtaImage_Copy(const OtaImageInfo *info);

/* App 运行区看起来像不像一个能跑的程序（栈顶在 RAM 内、复位向量落在 App 区且带 Thumb 位）。 */
uint8_t OtaImage_AppValid(void);

#endif /* OTA_IMAGE_H */
