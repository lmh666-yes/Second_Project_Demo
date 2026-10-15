#include "w25qxx.h"
/* 接口说明见 w25qxx.h；本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_us / delay_ms 等）独立文件 */

/* W25Q 系列 SPI Flash 通信序列：
 * CS 拉低 → 发 1 字节命令 → 发 3 字节地址(高位在前) → 收发数据 → CS 拉高。
 * CS 拉高触发芯片执行命令。
 * 写/擦除前必须先发 0x06(WriteEnable)，漏发时命令被静默忽略，写后读回仍为 0xFF。
 * 0x05(读状态寄存器1)：bit0 = BUSY，bit1 = WEL(写使能锁存)。
 * BUSY=1 时芯片不接收新命令。 */


/* W25Q 命令表（数据手册 8.1 节） */
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

/* 超时保护：芯片未接好时不会一直等待 */
#define W25_TIMEOUT_MS_SMALL    100U    /* 写 256 字节 / 擦 4KB 足够 */
#define W25_TIMEOUT_MS_BIG      60000U  /* 全片擦除最多 100 秒，取 60 秒上限 */

/* 4KB 读-改-写缓冲（WriteSafe 用，常驻占用 4KB RAM） */
static uint8_t w25_sector_buf[W25QXX_SECTOR_SIZE];

/* 总线占用锁，保护整条 SPI1 而不只是 W25QXX。
 * 必须存在的原因：
 *  1) SPI1 上有两个从机：W25Q128(CS = PB14) 和原理图 CN4 引出的 NRF24L01；
 *     同一时刻只允许一个从机通信，否则两个器件都会收到对方的时钟和数据。
 *  2) w25_sector_buf 是全模块共用的一份 4KB 缓冲，WriteSafe() 的流程是
 *     读回整扇区、改、擦、整扇区写回。两个任务（板2 的采集任务写缓存、
 *     补传任务读缓存）并发进入时会依次用同一个缓冲，写回的是两份数据的
 *     混合体，而两边都返回成功。
 * 锁的范围：本模块每个公开操作进入时抢、退出前放；锁被占用时返回 4。
 * 同总线的第二个器件通过 W25QXX_Lock() / W25QXX_Unlock() 参与互斥。
 * 实现：关中断加测试置位保证原子（关中断同时挡住中断和 FreeRTOS 任务切换）；
 * 拿不到锁立刻返回错误 4，不阻塞等待：本模块要能用在裸机工程里，没有 osMutex，
 * 自旋等待会长时间占用 CPU 或造成优先级反转。
 * 公开操作不可在中断中调用（退出时会提前开中断）。
 * 锁不可重入：同一上下文内第二次抢锁返回 4；内部嵌套调用一律走 _raw 版本。 */
static volatile uint8_t w25_lock = 0U;      /* 0 = 空闲；1 = 有人正在用 */

/* 获取占用锁：1 = 拿到；0 = 已被别的上下文占用（调用方应稍后重试） */
static uint8_t w25_lock_acquire(void)
{
    uint8_t ok;

    __disable_irq();
    ok = (w25_lock == 0U) ? 1U : 0U;
    if (ok != 0U) w25_lock = 1U;
    __enable_irq();

    return ok;
}

static void w25_lock_release(void)
{
    w25_lock = 0U;
}

/* 对外暴露的操作级总线锁：同一条 SPI1 上的第二个器件（板2 = NRF24L01 插座 CN4）
 * 用它们和本模块互斥。内部就是同一把 w25_lock。 */
uint8_t W25QXX_Lock(void)
{
    return w25_lock_acquire();
}

void W25QXX_Unlock(void)
{
    w25_lock_release();
}

uint8_t W25QXX_IsBusy(void)
{
    return (w25_lock != 0U) ? 1U : 0U;
}

/* _raw 操作体：只干活，不碰锁。
 * 每个公开操作都是同一形状：抢锁 → 调对应 _raw → 放锁；抢不到返回 4。
 * 拆出 _raw 的原因：WriteSafe 要读扇区、擦扇区、写回，IsBlank 要调 Read，
 * Init 要调 WakeUp/ReadID。锁非递归，嵌套调用第二次抢锁必然得到 0，
 * 所以内部一律调 _raw，不调同名公开函数。
 * 嵌套抢锁得到 4 是故意设计：把程序错误变成可见的返回值，而不是死锁。 */
static uint8_t  w25_wait_busy_raw(void);
static uint8_t  w25_init_raw(uint32_t speed);
static uint32_t w25_read_id_raw(void);
static uint8_t  w25_read_raw(uint8_t *dst, uint32_t addr, uint32_t len);
static uint8_t  w25_write_raw(const uint8_t *src, uint32_t addr, uint32_t len);
static uint8_t  w25_erase_sector_raw(uint32_t addr);
static uint8_t  w25_erase_block_raw(uint32_t addr);
static uint8_t  w25_erase_chip_raw(void);
static uint8_t  w25_is_blank_raw(uint32_t addr, uint32_t len);
static void     w25_power_down_raw(void);
static void     w25_wake_up_raw(void);


/* 内部小工具 */
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
    /* 24 位地址，高位在前（大端），与 STM32 的小端相反 */
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

/* 写完使能后确认 WEL 置位。WREN 只在上一次操作的 CS 已拉高之后才会被接受；
 * 芯片还在忙或 CS 时序被打断时 WREN 被静默忽略，后续页写/擦除命令也一并忽略，
 * 表现为写函数返回成功、读回仍是 0xFF。
 * 返回：0 = WEL 已置位（可发写/擦命令）；1 = 未置位 */
static uint8_t w25_write_enable_checked(void)
{
    w25_write_enable();
    if ((w25_read_status() & 0x02U) == 0U) {     /* bit1 = WEL */
        w25_write_enable();                      /* 重试一次 */
        if ((w25_read_status() & 0x02U) == 0U) return 1U;
    }
    return 0U;
}

static uint8_t w25_wait_busy_raw(void)
{
    uint32_t t0  = DWT_GetUs();
    uint32_t gap = 20U;                 /* 起始探测间隔 20µs */

    while ((w25_read_status() & 0x01U) != 0U) {
        /* 超时上限 = W25_TIMEOUT_MS_SMALL * 1000UL = 100ms */
        if (DWT_ElapsedUs(t0) > (W25_TIMEOUT_MS_SMALL * 1000UL)) return 2U;

        /* 退避探测：短操作立刻返回，长操作不占满 CPU */
        delay_us(gap);
        if (gap < 2000U) gap = gap * 2U;      /* 20→40→...→2000µs 封顶 */
    }
    return 0U;
}

uint8_t W25QXX_WaitBusy(void)
{
    uint8_t ret;

    if (w25_lock_acquire() == 0U) return 4U;      /* 4 = 总线被别人占着 */
    ret = w25_wait_busy_raw();
    w25_lock_release();

    return ret;
}

/* 大操作（全片擦除）专用等待：上限 60 秒（W25_TIMEOUT_MS_BIG）、1ms 退避。
 * 这段等待是忙等，调用前需喂狗，必要时挂起调度器。 */
static uint8_t w25_wait_busy_long(void)
{
    uint32_t t0  = DWT_GetUs();
    uint32_t gap = 1000U;               /* 起始 1ms */

    while ((w25_read_status() & 0x01U) != 0U) {
        if (DWT_ElapsedUs(t0) > (W25_TIMEOUT_MS_BIG * 1000UL)) return 2U;
        delay_ms(gap / 1000U);
        if (gap < 20000U) gap = gap * 2U;     /* 1→2→...→20ms 封顶 */
    }
    return 0U;
}


/* 基础功能 */
static uint8_t w25_init_raw(uint32_t speed)
{
    if (speed == 0U) speed = W25QXX_SPI_SPEED;

    /* 1) CS 先拉高：SPI 从机要求 CS 高电平 = 空闲 */
    GPIO_ClockEnable(W25QXX_CS_PORT);
    GPIO_OutInit(W25QXX_CS_PORT, W25QXX_CS_PIN);
    w25_cs_high();

    /* 2) 开 SPI（SYS_SPI_Init 内部解掉 PB3/PB4 的 JTAG 占用） */
    SYS_SPI_Init(W25QXX_SPI_ID, speed, SYS_SPI_MODE_0);

    /* 3) 唤醒（芯片可能还在上次的掉电模式里） */
    w25_wake_up_raw();
    delay_ms(10);                 /* 唤醒后要等 tRES1(约 3µs~1ms) */

    /* 只校验容量字节（JEDEC ID 最低字节 0x18 = 128Mbit），厂商字节（EF/C8/C2 等）
     * 不参与判定：本板 U2 为 GD25Q128，JEDEC ID = 0xC84018，写死 0xEF4018 会误判失败 */
    {
        uint32_t id = w25_read_id_raw();
        if ((id == 0xFFFFFFFFUL) || (id == 0x00000000UL)) return 1U;  /* 无器件/无供电 */
        if ((id & 0xFFUL) != 0x18UL) return 2U;                       /* 容量不是 128Mbit */
    }
    return 0U;
}

uint8_t W25QXX_Init(uint32_t speed)
{
    uint8_t ret;

    if (w25_lock_acquire() == 0U) return 4U;
    ret = w25_init_raw(speed);
    w25_lock_release();

    return ret;
}

static uint32_t w25_read_id_raw(void)
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

/* 返回 0xFFFFFFFF 有两种可能：1) 总线上没有器件或没供电；2) 总线被别人占着，
 * 用 W25QXX_IsBusy() 分辨。 */
uint32_t W25QXX_ReadID(void)
{
    uint32_t ret;

    if (w25_lock_acquire() == 0U) return 0xFFFFFFFFUL;
    ret = w25_read_id_raw();
    w25_lock_release();

    return ret;
}

static uint8_t w25_read_raw(uint8_t *dst, uint32_t addr, uint32_t len)
{
    uint32_t done = 0;

    if (dst == 0 || len == 0UL) return 1U;
    /* 越界检查不能用 (addr + len)：无符号加法回绕会绕过检查，
     * 例如 addr = 0x00FFFFF0、len = 0x20 时 sum = 0x10 < SIZE */
    if (addr >= W25QXX_SIZE_BYTES) return 1U;
    if (len > (W25QXX_SIZE_BYTES - addr)) return 1U;

    (void)w25_wait_busy_raw();

    /* SYS_SPI_Read 的长度参数是 16 位，超长读要分块 */
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

uint8_t W25QXX_Read(uint8_t *dst, uint32_t addr, uint32_t len)
{
    uint8_t ret;

    if (w25_lock_acquire() == 0U) return 4U;      /* 总线忙：本次未读，buf 内容无效 */
    ret = w25_read_raw(dst, addr, len);
    w25_lock_release();

    return ret;
}

static uint8_t w25_write_raw(const uint8_t *src, uint32_t addr, uint32_t len)
{
    uint32_t done = 0;

    if (src == 0 || len == 0UL) return 1U;
    /* 越界检查不能用 (addr + len)：无符号加法回绕会绕过检查 */
    if (addr >= W25QXX_SIZE_BYTES) return 1U;
    if (len > (W25QXX_SIZE_BYTES - addr)) return 1U;

    while (done < len) {
        /* 本次最多写入量：不跨 256 字节页（跨页会绕回页首覆盖自己） */
        uint16_t in_page = (uint16_t)(W25QXX_PAGE_SIZE -
                                      ((addr + done) & (uint32_t)(W25QXX_PAGE_SIZE - 1U)));
        uint16_t n       = (uint16_t)((len - done) > (uint32_t)in_page ?
                                      (uint32_t)in_page : (len - done));

        /* 1) 发写使能，并确认 WEL 置位，漏发时写命令被静默忽略 */
        if (w25_write_enable_checked() != 0U) return 3U;

        /* 2) 页写命令 + 地址 + 数据 */
        w25_cs_low();
        SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_PAGE_PROGRAM);
        w25_send_addr(addr + done);
        SYS_SPI_Write(W25QXX_SPI_ID, &src[done], n);
        w25_cs_high();                 /* CS 拉高才真正开始写 */

        /* 3) 等待忙结束（典型 0.4ms / 页） */
        if (w25_wait_busy_raw() != 0U) return 2U;

        done += (uint32_t)n;
    }
    return 0U;
}

uint8_t W25QXX_Write(const uint8_t *src, uint32_t addr, uint32_t len)
{
    uint8_t ret;

    if (w25_lock_acquire() == 0U) return 4U;
    ret = w25_write_raw(src, addr, len);
    w25_lock_release();

    return ret;
}

/* WriteSafe 工作体；只由 W25QXX_WriteSafe() 调用 */
static uint8_t w25_write_safe_locked(const uint8_t *src, uint32_t addr, uint32_t len);

uint8_t W25QXX_WriteSafe(const uint8_t *src, uint32_t addr, uint32_t len)
{
    uint8_t r;

    if (src == 0 || len == 0UL) return 1U;
    /* 越界检查不能用 (addr + len)：无符号加法回绕会绕过检查 */
    if (addr >= W25QXX_SIZE_BYTES) return 1U;
    if (len > (W25QXX_SIZE_BYTES - addr)) return 1U;

    /* 参数校验通过后才抢锁。抢不到说明另一个上下文正在用同一个 w25_sector_buf，
     * 此时必须直接返回：继续执行会把对方的 4KB 缓冲覆盖成两份数据的混合体，
     * 而两边都返回成功。 */
    if (w25_lock_acquire() == 0U) return 4U;

    r = w25_write_safe_locked(src, addr, len);

    w25_lock_release();
    return r;
}

/* WriteSafe 的工作体：只由上面的 W25QXX_WriteSafe() 调用，参数校验和总线锁
 * 由调用方负责。 */
static uint8_t w25_write_safe_locked(const uint8_t *src, uint32_t addr, uint32_t len)
{
    uint32_t pos = addr;
    uint32_t remain = len;

    while (remain > 0UL) {
        /* 当前扇区的基地址，以及本扇区起始处已写掉的偏移 */
        uint32_t sector_base = pos & ~(W25QXX_SECTOR_SIZE - 1UL);
        uint32_t off_in_sec  = pos - sector_base;
        uint32_t can_write   = W25QXX_SECTOR_SIZE - off_in_sec;

        if (can_write > remain) can_write = remain;

        /* 1) 先读回整个扇区，保护同一扇区里别的数据。
         * 调 _raw：本函数已被 W25QXX_WriteSafe() 持锁，再调公开的 W25QXX_Read()
         * 会第二次抢锁并得到总线忙。 */
        if (w25_read_raw(w25_sector_buf, sector_base, W25QXX_SECTOR_SIZE) != 0U) return 2U;

        /* 2) 目标区域本来就是 0xFF 时可跳过擦除，减少擦写次数 */
        {
            uint8_t all_ff = 1U;
            uint32_t i;
            for (i = 0; i < can_write; i++) {
                if (w25_sector_buf[off_in_sec + i] != 0xFFU) { all_ff = 0U; break; }
            }

            if (!all_ff) {
                /* 3) 原地改数据 → 擦扇区 → 整扇区写回（同样走 _raw） */
                for (i = 0; i < can_write; i++) {
                    w25_sector_buf[off_in_sec + i] = src[len - remain + i];
                }
                if (w25_erase_sector_raw(sector_base) != 0U) return 2U;
                if (w25_write_raw(w25_sector_buf, sector_base, W25QXX_SECTOR_SIZE) != 0U) return 2U;
            } else {
                /* 空白区：直接写，不需擦除 */
                if (w25_write_raw(&src[len - remain], pos, can_write) != 0U) return 2U;
            }
        }

        pos    += can_write;
        remain -= can_write;
    }
    return 0U;
}

static uint8_t w25_erase_sector_raw(uint32_t addr)
{
    if (addr >= W25QXX_SIZE_BYTES) return 1U;
    /* 对齐校验：芯片只看地址高位，非 4KB 对齐的地址（如 0x1000 + 0x800）
     * 会擦掉整个 4KB 扇区，把同扇区别人的数据一起抹掉且不报错 */
    if ((addr & (W25QXX_SECTOR_SIZE - 1UL)) != 0UL) return 1U;

    /* 写使能须确认 WEL 置位，否则擦除命令被静默忽略 */
    if (w25_write_enable_checked() != 0U) return 3U;

    /* 发命令前确认芯片不忙：忙时发的命令同样被丢弃 */
    if (w25_wait_busy_raw() != 0U) return 2U;

    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_SECTOR_ERASE);
    w25_send_addr(addr);
    w25_cs_high();

    return w25_wait_busy_raw();
}

uint8_t W25QXX_EraseSector(uint32_t addr)
{
    uint8_t ret;

    if (w25_lock_acquire() == 0U) return 4U;
    ret = w25_erase_sector_raw(addr);
    w25_lock_release();

    return ret;
}

static uint8_t w25_erase_block_raw(uint32_t addr)
{
    if (addr >= W25QXX_SIZE_BYTES) return 1U;
    /* 同 EraseSector：非 64KB 对齐地址会擦掉整个 64KB 块 */
    if ((addr & (W25QXX_BLOCK_SIZE - 1UL)) != 0UL) return 1U;

    if (w25_write_enable_checked() != 0U) return 3U;
    if (w25_wait_busy_raw() != 0U) return 2U;

    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_BLOCK_ERASE);
    w25_send_addr(addr);
    w25_cs_high();

    return w25_wait_busy_raw();
}

uint8_t W25QXX_EraseBlock(uint32_t addr)
{
    uint8_t ret;

    if (w25_lock_acquire() == 0U) return 4U;
    ret = w25_erase_block_raw(addr);
    w25_lock_release();

    return ret;
}

static uint8_t w25_erase_chip_raw(void)
{
    if (w25_write_enable_checked() != 0U) return 3U;

    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_CHIP_ERASE);
    w25_cs_high();

    return w25_wait_busy_long();       /* 全片擦除需要几十秒 */
}

/* 全片擦除期间总线一直被本模块占用，同总线的第二个器件（NRF24L01）
 * 在这段时间调用 W25QXX_Lock() 会一直拿到忙，这是预期行为。 */
uint8_t W25QXX_EraseChip(void)
{
    uint8_t ret;

    if (w25_lock_acquire() == 0U) return 4U;
    ret = w25_erase_chip_raw();
    w25_lock_release();

    return ret;
}

static uint8_t w25_is_blank_raw(uint32_t addr, uint32_t len)
{
    uint8_t  buf[32];
    uint32_t i;

    if (len == 0UL) return 1U;
    if (addr >= W25QXX_SIZE_BYTES) return 0U;
    if (len > (W25QXX_SIZE_BYTES - addr)) return 0U;

    while (len > 0UL) {
        uint32_t n = (len > sizeof(buf)) ? (uint32_t)sizeof(buf) : len;

        if (w25_read_raw(buf, addr, n) != 0U) return 0U;

        for (i = 0; i < n; i++) {
            if (buf[i] != 0xFFU) return 0U;
        }

        addr += n;
        len  -= n;
    }
    return 1U;
}

uint8_t W25QXX_IsBlank(uint32_t addr, uint32_t len)
{
    uint8_t ret;

    /* IsBlank 语义：1 = 空、0 = 有数据。
     * 总线忙（抢不到锁）也返回 0（按不空处理）：把不空当成空会让调用方
     * 跳过擦除直接写，会写坏数据。 */
    if (w25_lock_acquire() == 0U) return 0U;
    ret = w25_is_blank_raw(addr, len);
    w25_lock_release();

    return ret;
}


/* 低层原语 */
static void w25_power_down_raw(void)
{
    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_POWER_DOWN);
    w25_cs_high();
    delay_us(3);                   /* tDP：上电到掉电的保持时间 */
}

void W25QXX_PowerDown(void)
{
    if (w25_lock_acquire() == 0U) return;      /* 总线忙：本次不发命令 */
    w25_power_down_raw();
    w25_lock_release();
}

static void w25_wake_up_raw(void)
{
    w25_cs_low();
    SYS_SPI_TransferByte(W25QXX_SPI_ID, W25_CMD_RELEASE_PD);
    w25_cs_high();
    delay_us(10);                  /* tRES1 */
}

void W25QXX_WakeUp(void)
{
    if (w25_lock_acquire() == 0U) return;      /* 总线忙：本次不发命令 */
    w25_wake_up_raw();
    w25_lock_release();
}
