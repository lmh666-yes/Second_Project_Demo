#ifndef OTA_PART_H
#define OTA_PART_H

/* ============================================================
 *  ota_part.h —— OTA 分区 / 固件头 / 帧命令字：Boot 与 App 的**唯一事实来源**
 *
 *  为什么单独建这个头：
 *    分区地址、固件头布局、帧命令字这三样，Bootloader 与 App 必须完全一致；
 *    写两份迟早会分叉（一边改了地址，另一边还在老地址找固件，表现为"升级成功但没生效"）。
 *    所以两边都 #include 本文件，改一处两边同时生效。
 *
 *  数值来源（不是拍脑袋）：
 *    · 分区与「STM32 速查 · OTA 分区与标志位表」一致（Boot S0 / App 0x08004000 48KB /
 *      下载区 0x08010000 / 参数区沿用工程现值）；
 *    · 参数区地址 = 工程 sys_flash.h 里正在用的 SYS_FLASH_PARAM_ADDR / ADDR2；
 *      它那条"程序代码不得越过 0x08060000"的约束，本方案（Boot+App+下载区 全在 0x08020000 以下）
 *      天然满足。
 *
 *  板1 = STM32F407ZE（512KB Flash）专用。换板2（ZG 1MB）要改下面的下载区大小。
 * ============================================================ */

#include <stdint.h>

/* ============================================================
 *  1. 分区表（全部落在扇区边界上，四段互不重叠）
 * ============================================================ */
#define OTA_BOOT_ADDR       0x08000000UL   /* Bootloader：Sector 0，16KB，复位入口，永不覆盖 */
#define OTA_BOOT_SIZE       0x00004000UL
#define OTA_APP_ADDR        0x08004000UL   /* App 运行区：Sector 1~3，48KB（= Keil IROM1 起始） */
#define OTA_APP_SIZE        0x0000C000UL   /* 图纸上限 48KB；实测模板约 30KB，够但不宽裕 */
#define OTA_APP_END         (OTA_APP_ADDR + OTA_APP_SIZE)      /* 0x08010000 */
#define OTA_DL_ADDR         0x08010000UL   /* 下载区：新固件先写这里，Sector 4，64KB */
#define OTA_DL_SIZE         0x00010000UL

/* 参数区（工程 sys_flash.h 已在用；升级流程里任何擦除都不许落在它身上） */
#define OTA_PARAM_A_ADDR    0x08070000UL   /* SYS_FLASH_PARAM_ADDR  */
#define OTA_PARAM_B_ADDR    0x08060000UL   /* SYS_FLASH_PARAM_ADDR2 */

/* 单个镜像的字节数上限。本方案（App 侧先在 RAM 里攒完整镜像）取运行区大小；
   代价说清楚：要占 48KB RAM，且镜像不能超过 48KB。 */
#define OTA_MAX_IMAGE       OTA_APP_SIZE

/* ============================================================
 *  2. 下载区固件头（16 字节，4 字节对齐）
 *
 *  头就放在下载区最前面，镜像数据紧随其后（偏移 OTA_HDR_IMG_OFF）。
 *
 *  写盘铁律：**先写镜像，最后写头**。
 *    头不完整 = "没有待安装固件" → 掉电、坏包都不会误刷，App 区永远是"完整的新程序"
 *    或"完整的旧程序"，不存在刷了一半的中间态。
 *
 *  作废头的办法：把头 4 字节写成 0（只写一个字，不必擦 64KB 扇区，秒级）。
 * ============================================================ */
#define OTA_HDR_MAGIC_0     0x4FU          /* 'O' —— 逐字节比较，不与端序绑定 */
#define OTA_HDR_MAGIC_1     0x54U          /* 'T' */
#define OTA_HDR_MAGIC_2     0x41U          /* 'A' */
#define OTA_HDR_MAGIC_3     0x31U          /* '1' */

#define OTA_HDR_SIZE        16U
#define OTA_HDR_IMG_OFF     OTA_HDR_SIZE   /* 镜像数据在下载区内的偏移 */

/* 头内偏移（全部按字节下标，写的时候"攒够 16 字节一次写"，别逐字段写：
   SYS_FLASH_Write 按字编程，逐字段写 2 字节会把同字的另 2 字节填成 0xFF） */
#define OTA_HDR_OFF_MAGIC   0U             /* [0..3]   标识 'O''T''A''1' */
#define OTA_HDR_OFF_SIZE    4U             /* [4..7]   镜像字节数（小端 uint32） */
#define OTA_HDR_OFF_CRC     8U             /* [8..9]   整镜像 CRC16-MODBUS（小端） */
#define OTA_HDR_OFF_VER     10U            /* [10..11] 版本号（给人看的，小端） */
#define OTA_HDR_OFF_HCRC    12U            /* [12..13] 头前 12 字节的 CRC16（小端） */
                                           /* [14..15] 保留，写 0xFFFF */

/* ============================================================
 *  3. 帧命令字（复用工程 sys_frame 的 AA55…CRC16…55AA 帧；载荷上限 64 字节）
 *
 *  ESP8266-01S 用 AT+CIPMODE=1 进透传后，串口上就是一条**裸字节管道**，
 *  帧由 PC 端按同一协议组好，板子直接用 SYS_FRAME_Feed/Get 收 —— 协议模块一行不改。
 *
 *  多字节字段一律**大端**（高字节在前），与 sys_frame.h 的环境帧约定一致；
 *  只有帧尾那 2 字节 CRC 是低字节在前（协议原文如此，别改）。
 * ============================================================ */
#define OTA_CMD_BEGIN       0x10U   /* PC→板  载荷 = [版本 2B][镜像字节数 4B]       */
#define OTA_CMD_DATA        0x11U   /* PC→板  载荷 = [偏移 4B][数据 56B]            */
#define OTA_CMD_END         0x12U   /* PC→板  载荷 = [整镜像 CRC16 2B]              */
#define OTA_CMD_STAT        0x13U   /* 板→PC  载荷 = [状态码 1B][已收字节数 4B]     */
#define OTA_CMD_QUERY       0x14U   /* PC→板  空载荷：问一下板子当前状态（可选用）   */

/* 每帧数据字节数：必须是 4 的倍数。
   原因：下载区里每个字节都要经 SYS_FLASH_Write 按字编程，它要求地址 4 字节对齐；
   头长 16（4 的倍数）+ 每帧 56（4 的倍数）⇒ 每帧落点天然对齐，不用额外暂存。 */
#define OTA_DATA_CHUNK      56U

/* ============================================================
 *  4. 状态码（OTA_CMD_STAT 载荷第 1 字节）
 * ============================================================ */
#define OTA_ST_OK           0U      /* 这一步收到且处理完毕 */
#define OTA_ST_BUSY         1U      /* 上一轮还没结束（别急着发下一帧） */
#define OTA_ST_ERASING      2U      /* 正在擦下载区（秒级）；发送方收到后要等 ≥5s */
#define OTA_ST_BADCMD       3U      /* 命令非法：参数缺、偏移不对、超界 */
#define OTA_ST_CRCERR       4U      /* END 时整镜像 CRC 不符：一个字节都没写进 Flash */
#define OTA_ST_DONE         5U      /* 已写镜像 + 已写头，马上复位交给 Boot 搬运 */
#define OTA_ST_TOOBIG       6U      /* 镜像超过运行区（48KB）：本方案用 RAM 缓冲，装不下 */
#define OTA_ST_IDLE         7U      /* 空闲：还没开始收 */

#endif /* OTA_PART_H */
