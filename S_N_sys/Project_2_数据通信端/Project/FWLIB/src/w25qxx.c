#include "w25qxx.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"

/* ================================================================
 *  w25qxx.c —— W25Q 系列 SPI Flash 驱动  实现文件
 * ================================================================
 *  SPI Flash 的通信套路（和 EEPROM 完全不一样，记住这套就通杀所有 W25Q）：
 *
 *   ① 片选 CS 拉低  → ② 发 1 字节命令 → ③ 发地址(3 字节，高位在前)
 *   → ④ 收发数据 → ⑤ CS 拉高（**必须拉高，CS 高电平才触发"执行"**）
 *
 *   写/擦除前必须先发 0x06(WriteEnable)，否则命令被忽略且**不报错**
 *   ——这是最容易踩的坑：现象是"写进去了但读出来还是 0xFF"。
 *
 *   0x05(读状态寄存器1)的 bit0 = BUSY，bit1 = WEL(写使能锁存)。
 *   只要 BUSY=1 就说明芯片在忙，此时发什么命令它都不理。
 * ================================================================ */


/* ================================================================
 *                    W25Q 命令表（数据手册 8.1 节）
 * ================================================================ */
#define W25_CMD_WRITE_ENABLE    0x06U
#define W25_CMD_WRITE_DISABLE   0x04U
#define W25_CMD_READ_STATUS1    0x05U
#define W25_CMD_WRITE_STATUS    0x01U
#define W25_CMD_READ_DATA       0x03U
#define W25_CMD_FAST_READ       0x0BU
#define W25_CMD_PAGE_PROGRAM    0x02U
#define W25_CMD_SECTOR_ERASE    0x20U   /* 4KB  */
#define W25_CMD_BLOCK_ERASE     0xD8U   /* 64KB */
#define W25_CMD_CHIP_ERASE      0xC7U
#define W25_CMD_POWER_DOWN      0xB9U
#define W25_CMD_RELEASE_PD      0xABU
#define W25_CMD_JEDEC_ID        0x9FU

/* 超时保护（防止芯片没接好时死等） */
#define W25_TIMEOUT_MS_SMALL    100U    /* 写 256 字节 / 擦 4KB 足够 */
#define W25_TIMEOUT_MS_BIG      60000U  /* 全片擦除留足 60 秒 */

/* 4KB 读-改-写缓冲（WriteSafe 用；只在用到时才占 RAM，但也一直占着 4KB） */
static uint8_t w25_sector_buf[W25QXX_SECTOR_SIZE];


/* ================================================================
 *                      内部小工具
 * ================================================================ */
static void w25_cs_low(void)
{
    GPIO_OutReset(W25QXX_CS_PORT, W25QXX_CS_PIN);
}

static void w25_cs_high(void)
{
    GPIO_OutSet(W25QXX_CS_PORT, W25QXX_CS_PIN);
}

static void w25_send_addr(uint32_t addr)
{
    /* 24 位地址，高位在前（大端）——注意不是 STM32 的小端习惯 */
    SYS_SPI_TransferByte(W25QXX_SPI_ID, (uint8_t)((addr >> 16) & 0xFFU));
    SYS_SPI_TransferByte(W25QXX_SPI_ID, (uint8_t)((addr >> 8)  & 0xFFU));
    SYS_SPI_TransferByte(W25QXX_SPI_ID, (uint8_t)( addr        & 0xFFU));
}

static uint8_t w25_read_status(void)
{
    uint8_t st;

    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_READ_STATUS1);
    st = SYS_SPI_TransferByte(W25QXX_SPI_ID, 0xFFU);
    w25_cs_high();

    return st;
}

static void w25_write_enable(void)
{
    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_WRITE_ENABLE);
    w25_cs_high();
}

uint8_t W25QXX_WaitBusy(void)
{
    uint32_t t0 = DWT_GetUs();

    while ((w25_read_status() & 0x01U) != 0U) {
        if (DWT_ElapsedUs(t0) > (W25_TIMEOUT_MS_SMALL * 3000UL)) return 2U;
        Delay_us(50);
    }
    return 0U;
}

/* 大操作（全片擦除）专用等待 */
static uint8_t w25_wait_busy_long(void)
{
    uint32_t t0 = DWT_GetUs();

    while ((w25_read_status() & 0x01U) != 0U) {
        if (DWT_ElapsedUs(t0) > (W25_TIMEOUT_MS_BIG * 1000UL)) return 2U;
        Delay_ms(10);
    }
    return 0U;
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
uint8_t W25QXX_Init(uint32_t speed)
{
    if (speed == 0U) speed = W25QXX_SPI_SPEED;

    /* ① CS 先拉高（SPI 从机要求 CS 高电平 = 空闲） */
    GPIO_ClockEnable(W25QXX_CS_PORT);
    GPIO_OutInit(W25QXX_CS_PORT, W25QXX_CS_PIN);
    w25_cs_high();

    /* ② 开 SPI（SYS_SPI_Init 内部会解掉 PB3/PB4 的 JTAG 占用） */
    SYS_SPI_Init(W25QXX_SPI_ID, speed, SYS_SPI_MODE_0);

    /* ③ 唤醒（芯片可能还在上次的掉电模式里） */
    W25QXX_WakeUp();
    Delay_ms(10);                 /* 唤醒后要等 tRES1(约 3µs~1ms) */

    return (W25QXX_ReadID() == W25QXX_ID_W25Q128) ? 0U : 1U;
}

uint32_t W25QXX_ReadID(void)
{
    uint32_t id = 0;

    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_JEDEC_ID);
    id  = (uint32_t)SYS_SPI_TransferByte(W25QXX_SPI_ID, 0xFFU) << 16;
    id |= (uint32_t)SYS_SPI_TransferByte(W25QXX_SPI_ID, 0xFFU) << 8;
    id |= (uint32_t)SYS_SPI_TransferByte(W25QXX_SPI_ID, 0xFFU);
    w25_cs_high();

    return id;
}

uint8_t W25QXX_Read(uint8_t *dst, uint32_t addr, uint32_t len)
{
    uint32_t done = 0;

    if (dst == 0 || len == 0UL) return 1U;
    if ((addr + len) > W25QXX_SIZE_BYTES) return 1U;

    (void)W25QXX_WaitBusy();

    /* SYS_SPI_Read 的长度参数是 16 位——超长读要自己分块。
     * 一次 CS 拉低期间可以连续读任意长度，但分块更安全（也顺便支持超长读）。 */
    while (done < len) {
        uint32_t n = len - done;
        if (n > 4096UL) n = 4096UL;    /* 4KB 一块 */

        w25_cs_low();
        SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_READ_DATA);
        w25_send_addr(addr + done);    /* 03 命令每次都要重新给地址 */
        SYS_SPI_Read(W25QXX_SPI_ID, &dst[done], (uint16_t)n);
        w25_cs_high();

        done += n;
    }
    return 0U;
}

uint8_t W25QXX_Write(const uint8_t *src, uint32_t addr, uint32_t len)
{
    uint32_t done = 0;

    if (src == 0 || len == 0UL) return 1U;
    if ((addr + len) > W25QXX_SIZE_BYTES) return 1U;

    while (done < len) {
        /* 本次最多写多少：不跨 256 字节页（跨页会绕回页首覆盖自己） */
        uint16_t in_page = (uint16_t)(W25QXX_PAGE_SIZE -
                                      ((addr + done) & (uint32_t)(W25QXX_PAGE_SIZE - 1U)));
        uint16_t n       = (uint16_t)((len - done) > (uint32_t)in_page ?
                                      (uint32_t)in_page : (len - done));

        /* ① 发写使能（**漏了这一步，写命令会被静默忽略**） */
        w25_write_enable();

        /* ② 页写命令 + 地址 + 数据 */
        w25_cs_low();
        SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_PAGE_PROGRAM);
        w25_send_addr(addr + done);
        SYS_SPI_Write(W25QXX_SPI_ID, &src[done], n);
        w25_cs_high();                 /* CS 拉高才真正开始写 */

        /* ③ 等它忙完（典型 0.4ms / 页） */
        if (W25QXX_WaitBusy() != 0U) return 2U;

        done += (uint32_t)n;
    }
    return 0U;
}

uint8_t W25QXX_WriteSafe(const uint8_t *src, uint32_t addr, uint32_t len)
{
    uint32_t pos = addr;
    uint32_t remain = len;

    if (src == 0 || len == 0UL) return 1U;
    if ((addr + len) > W25QXX_SIZE_BYTES) return 1U;

    while (remain > 0UL) {
        /* 当前扇区的基地址，以及本扇区起始处已写掉的偏移 */
        uint32_t sector_base = pos & ~(W25QXX_SECTOR_SIZE - 1UL);
        uint32_t off_in_sec  = pos - sector_base;
        uint32_t can_write   = W25QXX_SECTOR_SIZE - off_in_sec;

        if (can_write > remain) can_write = remain;

        /* ① 先把整个扇区读回来（保护同一扇区里别的数据） */
        if (W25QXX_Read(w25_sector_buf, sector_base, W25QXX_SECTOR_SIZE) != 0U) return 2U;

        /* ② 如果这段区域本来就是 0xFF，可以省掉"擦除"（Flash 寿命翻几倍） */
        {
            uint8_t all_ff = 1U;
            uint32_t i;
            for (i = 0; i < can_write; i++) {
                if (w25_sector_buf[off_in_sec + i] != 0xFFU) { all_ff = 0U; break; }
            }

            if (!all_ff) {
                /* ③ 原地改数据 → 擦扇区 → 整扇区写回 */
                for (i = 0; i < can_write; i++) {
                    w25_sector_buf[off_in_sec + i] = src[len - remain + i];
                }
                if (W25QXX_EraseSector(sector_base) != 0U) return 2U;
                if (W25QXX_Write(w25_sector_buf, sector_base, W25QXX_SECTOR_SIZE) != 0U) return 2U;
            } else {
                /* 空白区：直接写，不用擦 */
                if (W25QXX_Write(&src[len - remain], pos, can_write) != 0U) return 2U;
            }
        }

        pos    += can_write;
        remain -= can_write;
    }
    return 0U;
}

uint8_t W25QXX_EraseSector(uint32_t addr)
{
    if (addr >= W25QXX_SIZE_BYTES) return 1U;

    w25_write_enable();

    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_SECTOR_ERASE);
    w25_send_addr(addr);
    w25_cs_high();

    return W25QXX_WaitBusy();
}

uint8_t W25QXX_EraseBlock(uint32_t addr)
{
    if (addr >= W25QXX_SIZE_BYTES) return 1U;

    w25_write_enable();

    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_BLOCK_ERASE);
    w25_send_addr(addr);
    w25_cs_high();

    return W25QXX_WaitBusy();
}

uint8_t W25QXX_EraseChip(void)
{
    w25_write_enable();

    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_CHIP_ERASE);
    w25_cs_high();

    return w25_wait_busy_long();       /* 全片擦除要几十秒 */
}

uint8_t W25QXX_IsBlank(uint32_t addr, uint32_t len)
{
    uint8_t  buf[32];
    uint32_t i;

    if (len == 0UL) return 1U;
    if ((addr + len) > W25QXX_SIZE_BYTES) return 0U;

    while (len > 0UL) {
        uint32_t n = (len > sizeof(buf)) ? (uint32_t)sizeof(buf) : len;

        if (W25QXX_Read(buf, addr, n) != 0U) return 0U;

        for (i = 0; i < n; i++) {
            if (buf[i] != 0xFFU) return 0U;
        }

        addr += n;
        len  -= n;
    }
    return 1U;
}


/* ================================================================
 *                    区块 3：低层原语
 * ================================================================ */
void W25QXX_PowerDown(void)
{
    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_POWER_DOWN);
    w25_cs_high();
    Delay_us(3);                   /* tDP 上电到掉电的保持时间 */
}

void W25QXX_WakeUp(void)
{
    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_RELEASE_PD);
    w25_cs_high();
    Delay_us(10);                  /* tRES1 */
}
