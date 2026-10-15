#include "ota_app.h"

#include <string.h>

#include "ota_part.h"
#include "ota_crc16.h"
#include "sys_flash.h"
#include "sys_frame.h"
#include "sys_usart.h"

/* FreeRTOS：只用到"延时让出"和最后复位前的等待，不引入任务/信号量，方便单独看懂 */
#include "FreeRTOS.h"
#include "task.h"

/* OTA 数据走 ESP8266 那路串口（工程 sys_usart.h 标明 USART3 = PB10/PB11 接 ESP8266） */
#define OTA_UART            SYS_USART_3
/* 调试口：把进度写出来给人看 */
#define OTA_LOG_UART        SYS_USART_1

/* 烧写分块大小：SYS_FLASH_Write 内部已按字编程并即时回读校验，块大点少几次解锁/加锁 */
#define OTA_WRITE_CHUNK     4096U

/* 日志用 ASCII（ARMCC 对字符串里的中文报 #870-D；中文只写在注释里，本工程要 0 警告） */

/* ------------------------------------------------------------
 *  RAM 缓冲：收集中的镜像（不写 Flash 之前，这就是唯一副本）
 * ------------------------------------------------------------ */
static uint8_t  s_img[OTA_MAX_IMAGE];
static uint32_t s_size;          /* BEGIN 声明的镜像字节数 */
static uint32_t s_got;           /* 已收字节数 */
static uint16_t s_crc;           /* 边收边累计的整镜像 CRC16 */
static uint16_t s_ver;           /* 版本号（只给人看） */
static uint8_t  s_state = OTA_ST_IDLE;

/* ---------------- 小工具 ---------------- */

static void ota_log(const char *s)
{
    SYS_USART_SendString(OTA_LOG_UART, s);
}

static void ota_txflush(void)
{
    SYS_USART_FlushTx(OTA_UART);
    SYS_USART_FlushTx(OTA_LOG_UART);
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* 回一条状态帧（板 → PC）：[状态码 1B][已收字节数 4B 大端] */
static void ota_reply(uint8_t code, uint32_t got)
{
    uint8_t p[5];

    p[0] = code;
    p[1] = (uint8_t)(got >> 24);
    p[2] = (uint8_t)(got >> 16);
    p[3] = (uint8_t)(got >> 8);
    p[4] = (uint8_t)got;
    SYS_FRAME_Send(OTA_UART, OTA_CMD_STAT, p, 5u);
    ota_txflush();
}

/* 拼固件头（16 字节）：多字节字段小端，与 ota_part.h 的偏移表一致。
   [14..15] 保留写 0xFF —— 保持整字的空闲位，不影响判读。 */
static void ota_build_header(uint8_t *h, uint32_t size, uint16_t crc, uint16_t ver)
{
    uint16_t hcrc;

    memset(h, 0xFF, OTA_HDR_SIZE);
    h[OTA_HDR_OFF_MAGIC + 0u] = OTA_HDR_MAGIC_0;   /* 'O' */
    h[OTA_HDR_OFF_MAGIC + 1u] = OTA_HDR_MAGIC_1;   /* 'T' */
    h[OTA_HDR_OFF_MAGIC + 2u] = OTA_HDR_MAGIC_2;   /* 'A' */
    h[OTA_HDR_OFF_MAGIC + 3u] = OTA_HDR_MAGIC_3;   /* '1' */

    h[OTA_HDR_OFF_SIZE + 0u] = (uint8_t)(size & 0xFFu);
    h[OTA_HDR_OFF_SIZE + 1u] = (uint8_t)((size >> 8) & 0xFFu);
    h[OTA_HDR_OFF_SIZE + 2u] = (uint8_t)((size >> 16) & 0xFFu);
    h[OTA_HDR_OFF_SIZE + 3u] = (uint8_t)((size >> 24) & 0xFFu);

    h[OTA_HDR_OFF_CRC + 0u] = (uint8_t)(crc & 0xFFu);
    h[OTA_HDR_OFF_CRC + 1u] = (uint8_t)(crc >> 8);

    h[OTA_HDR_OFF_VER + 0u] = (uint8_t)(ver & 0xFFu);
    h[OTA_HDR_OFF_VER + 1u] = (uint8_t)(ver >> 8);

    hcrc = OTA_Crc16Block(h, 12u);                 /* 头前 12 字节自校验 */
    h[OTA_HDR_OFF_HCRC + 0u] = (uint8_t)(hcrc & 0xFFu);
    h[OTA_HDR_OFF_HCRC + 1u] = (uint8_t)(hcrc >> 8);
}

/* 真正动 Flash：擦下载区 → 写镜像 → **最后**写头。成功返回 1。 */
static uint8_t ota_install(void)
{
    uint32_t off = 0u;
    uint8_t hdr[OTA_HDR_SIZE];
    uint8_t rc;

    ota_log("\r\n[OTA] CRC ok, writing download area (ERASING ack first, then erase 64KB sector)\r\n");
    ota_reply(OTA_ST_ERASING, s_got);

    /* 这一步是给上位机的：擦除期间 CPU 取指被硬件挂起，串口中断也停，
       所以必须先让 ERASING 这一帧真的发出去，再等一小会儿（发完再擦）。 */
    vTaskDelay(pdMS_TO_TICKS(50));

    rc = SYS_FLASH_EraseSector(OTA_DL_ADDR);
    if (rc != SYS_FLASH_OK) {
        ota_log("[OTA] erase failed (see sys_flash.h error codes)\r\n");
        ota_reply(OTA_ST_BADCMD, s_got);
        return 0;
    }

    /* 写镜像（从头部之后开始；块大小 4096，内部逐字编程 + 回读校验） */
    while (off < s_size) {
        uint32_t n = s_size - off;
        if (n > OTA_WRITE_CHUNK)
            n = OTA_WRITE_CHUNK;
        rc = SYS_FLASH_Write(OTA_DL_ADDR + OTA_HDR_IMG_OFF + off, &s_img[off], n);
        if (rc != SYS_FLASH_OK) {
            ota_log("[OTA] image write failed (readback mismatch), abort\r\n");
            ota_reply(OTA_ST_BADCMD, s_got);
            return 0;
        }
        off += n;
    }

    /* 铁律：镜像写完了，才写头。头写下去之前，Boot 会认为"没有待装固件"。 */
    ota_build_header(hdr, s_size, s_crc, s_ver);
    rc = SYS_FLASH_Write(OTA_DL_ADDR, hdr, OTA_HDR_SIZE);
    if (rc != SYS_FLASH_OK) {
        ota_log("[OTA] header write failed, this round void (nothing will be flashed)\r\n");
        ota_reply(OTA_ST_BADCMD, s_got);
        return 0;
    }

    ota_log("[OTA] image + header written; reboot lets the bootloader copy it\r\n");
    ota_reply(OTA_ST_DONE, s_got);
    return 1;
}

/* ---------------- 帧处理 ---------------- */

static void ota_on_begin(const uint8_t *p, uint16_t len)
{
    uint16_t ver;
    uint32_t size;

    if (len < 6u) {
        ota_reply(OTA_ST_BADCMD, s_got);
        return;
    }
    ver  = be16(&p[0]);
    size = be32(&p[2]);

    if (size == 0u || size > OTA_MAX_IMAGE) {
        /* 本方案在 RAM 里攒整镜像，超过运行区就装不下 —— 明确拒掉，别收一半 */
        ota_log("\r\n[OTA] BEGIN: size 0 or too big, rejected (limit = app region 48KB)\r\n");
        ota_reply(OTA_ST_TOOBIG, 0u);
        return;
    }

    s_size  = size;
    s_got   = 0u;
    s_crc   = 0xFFFFu;
    s_ver   = ver;
    s_state = OTA_ST_OK;
    ota_log("\r\n[OTA] BEGIN: receiving new firmware\r\n");
    ota_reply(OTA_ST_OK, 0u);
}

static void ota_on_data(const uint8_t *p, uint16_t len)
{
    uint32_t off;
    uint16_t dlen;

    if (len < 4u || s_size == 0u) {
        ota_reply(OTA_ST_BADCMD, s_got);
        return;
    }
    off  = be32(&p[0]);
    dlen = (uint16_t)(len - 4u);

    /* 只能"接着刚才的位置往下写"：跳号说明丢帧，重来一轮（BEGIN 会复位） */
    if (off != s_got || (off + (uint32_t)dlen) > s_size) {
        ota_log("\r\n[OTA] DATA offset mismatch (lost/dup frame), BEGIN again\r\n");
        ota_reply(OTA_ST_BADCMD, s_got);
        return;
    }

    memcpy(&s_img[off], &p[4], dlen);
    s_crc = OTA_Crc16(&p[4], dlen, s_crc);      /* 边收边累计，不缓存第二份 */
    s_got += dlen;

    ota_reply(OTA_ST_OK, s_got);                /* 每帧一条，上位机据此对齐进度 */
}

static void ota_on_end(const uint8_t *p, uint16_t len)
{
    uint16_t crc16;

    if (len < 2u) {
        ota_reply(OTA_ST_BADCMD, s_got);
        return;
    }
    crc16 = be16(&p[0]);

    if (s_got != s_size) {
        ota_log("\r\n[OTA] END: byte count mismatch (not complete), void\r\n");
        ota_reply(OTA_ST_BADCMD, s_got);
        return;
    }
    if (crc16 != s_crc) {
        ota_log("\r\n[OTA] END: whole-image CRC mismatch, nothing written to flash\r\n");
        ota_reply(OTA_ST_CRCERR, s_got);
        return;
    }

    if (ota_install()) {
        /* 复位：Bootloader 上电后看到有效固件头，搬运到运行区，然后跳过去跑新程序 */
        vTaskDelay(pdMS_TO_TICKS(300));
        NVIC_SystemReset();
    }
}

/* ---------------- 对外接口 ---------------- */

void OTA_App_Init(void)
{
    s_size  = 0u;
    s_got   = 0u;
    s_crc   = 0xFFFFu;
    s_ver   = 0u;
    s_state = OTA_ST_IDLE;
    SYS_FRAME_Reset(OTA_UART);
}

void OTA_App_Poll(void)
{
    uint8_t  cmd = 0u;
    uint8_t  payload[64];
    uint16_t len = 0u;

    /* Poll 把串口环形缓冲里的字节喂进收帧状态机；一收满一帧就取出来处理。
       上位机是"发一帧等一条 STAT"的节奏，所以同时不会有第二帧积压。 */
    while (SYS_FRAME_Poll(OTA_UART) > 0u) {
        if (SYS_FRAME_Get(OTA_UART, &cmd, payload, (uint16_t)sizeof(payload), &len) != 0u)
            break;                      /* 理论上不会：Poll 说有帧就能取 */

        switch (cmd) {
        case OTA_CMD_BEGIN: ota_on_begin(payload, len); break;
        case OTA_CMD_DATA:  ota_on_data(payload, len);  break;
        case OTA_CMD_END:   ota_on_end(payload, len);   break;
        case OTA_CMD_QUERY: ota_reply(s_state, s_got);  break;
        default:            ota_reply(OTA_ST_BADCMD, s_got); break;
        }
    }
}

uint8_t OTA_App_State(void)
{
    return s_state;
}

uint8_t OTA_App_Progress(void)
{
    if (s_size == 0u)
        return 0u;
    if (s_got >= s_size)
        return 100u;
    return (uint8_t)((s_got * 100u) / s_size);
}
