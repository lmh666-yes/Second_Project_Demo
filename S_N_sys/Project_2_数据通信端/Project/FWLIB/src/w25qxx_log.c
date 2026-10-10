#include "w25qxx_log.h"
#include <string.h>

/* w25qxx_log.c 外部 Flash 滚动记录实现
 * 硬件映射
 *   功能     资源              板上位置        改这里
 *   Flash    SPI1 @10MHz       U2 (GD25Q128)   w25qxx.h
 *   片选     PB14 (FLASH_CS)   板上直连        w25qxx.h
 *   分区     0x001000 起 64KB  :               w25qxx.h / 本文件区块1
 * 读写全走 w25qxx.h 接口。
 *
 * 环形结构：16 扇区 × 128 槽 = 2048 条；head 加满一圈回 0；最老的一条
 * = head 往回退 count 条；count 到 2048 后不再增长。
 *
 * 写入时序：1) 擦除，只在跨到脏槽位时做，平均 128 次擦一次
 *           2) 写槽偏移 [4..32)：时间戳、长度、CRC16、数据
 *           3) 写槽偏移 [0..4)：序号 seq，最后落盘
 * 读端看 seq：等于 0xFFFFFFFF 当空槽跳过；2)3) 之间断电则该条被忽略，
 * 下次 init 从它开始重写。
 * W25QXX_Write 要求目标已擦除：本模块 2)3) 写之前已保证，单条约 1ms，
 * 不需要 WriteSafe（每次约 50ms）。 */

/* 编译期护栏（配错立刻报错） */
/* 1) 槽长必须 4 字节对齐：槽内 seq / ts 是按 32 位小端拼出的字段 */
#if ((W25QXX_LOG_SLOT_SIZE % 4U) != 0U)
#error "W25QXX_LOG_SLOT_SIZE 必须是 4 的整数倍：槽内 seq/ts 是 32 位字段，槽首必须 4 字节对齐"
#endif

/* 2) 槽长必须整除扇区：否则一条记录跨 4KB 扇区，
 *    擦一个扇区只波及本扇区记录的前提不成立 */
#if ((W25QXX_SECTOR_SIZE % W25QXX_LOG_SLOT_SIZE) != 0U)
#error "W25QXX_LOG_SLOT_SIZE 必须能整除 W25QXX_SECTOR_SIZE(4096)：一条记录跨扇区，擦写时序不成立"
#endif

/* 3) 槽长必须整除页(256B)：W25QXX_Write 一次最多写一页，
 *    槽跨页则数据段要拆两次写，最后写 seq 的提交不再原子 */
#if ((W25QXX_PAGE_SIZE % W25QXX_LOG_SLOT_SIZE) != 0U)
#error "W25QXX_LOG_SLOT_SIZE 必须能整除 W25QXX_PAGE_SIZE(256)：一条记录跨页，没法一次写完"
#endif

/* 4) 载荷至少 20 字节：要装下 12B 业务数据 */
#if ((W25QXX_LOG_SLOT_SIZE <= W25QXX_LOG_HDR_SIZE) || \
     ((W25QXX_LOG_SLOT_SIZE - W25QXX_LOG_HDR_SIZE) < 20U))
#error "W25QXX_LOG_PAYLOAD 不足 20 字节：装不下 12B 业务数据与 19 字符示例文本"
#endif

/* 日志区必须落在分区表划的 LOG ~ FONT 之间，不能压到字库 */
typedef char w25qxx_log_region_check[
    ((W25QXX_LOG_BASE + ((uint32_t)W25QXX_LOG_SECTORS * W25QXX_SECTOR_SIZE))
        <= W25QXX_ADDR_FONT) ? 1 : -1];


/* 模块内部状态 */
static volatile uint8_t  s_inited   = 0U;    /* 1 = 已经 Scan 过 */
static volatile uint16_t s_head     = 0U;    /* 下一条要写哪个槽 */
static volatile uint16_t s_count    = 0U;    /* 现有多少条有效记录 */
static volatile uint32_t s_next_seq = 0UL;   /* 下一条记录的序号（0 是合法值，0xFFFFFFFF 才是空槽） */
static volatile uint16_t s_lost     = 0U;    /* 因整扇区擦除而被连带丢弃的记录数（只增） */


/* 内部小工具 */

/* 槽里统一用小端存（低字节在前）。不用 memcpy 强转是为了换平台也正确 */
/* 本模块被两方并发使用：缓存任务调 W25QXX_LogWrite 写，补传任务调
 * W25QXX_LogRead / LogTail 读。s_head / s_count / s_lost 必须原子更新，
 * seq 与数据段的两次页编程也不能交错，否则读端会看到序号跳号或 count 不符。
 *
 * 有 FreeRTOS 时用 vTaskSuspendAll()/xTaskResumeAll()，不关中断，只禁止任务
 * 切换，支持嵌套；没有 RTOS 时为空操作（单任务轮询模型无并发）。
 * 锁内包含页编程与扇区擦除，擦除最坏约 50ms，用挂起调度而非临界区可避免
 * 关中断 50ms。 */
#if (defined(__CORTEX_M) || defined(__ARM_ARCH)) && defined(configUSE_PREEMPTION)
#include "FreeRTOS.h"
#include "task.h"
#define W25QXX_LOG_LOCK()    vTaskSuspendAll()
#define W25QXX_LOG_UNLOCK()  (void)xTaskResumeAll()
#else
#define W25QXX_LOG_LOCK()    do { } while (0)
#define W25QXX_LOG_UNLOCK()  do { } while (0)
#endif

static void log_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFUL);
    p[1] = (uint8_t)((v >> 8) & 0xFFUL);
    p[2] = (uint8_t)((v >> 16) & 0xFFUL);
    p[3] = (uint8_t)((v >> 24) & 0xFFUL);
}

static uint32_t log_get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* ------------------------------------------------------------------
 *  CRC-16/MODBUS（初值 0xFFFF、反射多项式 0xA001 = 0x8005 的反转）
 *  与板1 sys_modbus.h 的 SYS_MODBUS_Crc16、板2 modbus.h 的 MODBUS_CRC16
 *  一致，收发两端可用同一个校验函数自检。
 *
 *  用标准 CRC16 而非 16 位字节累加和：累加和检不出成对错误（0x01<->0xFF）
 *  与字节换序，会把写坏的记录当正常数据返回。
 *
 *  log_crc16_upd 做成可续算形式：LogInit 扫描时只有 12 字节槽头，数据段在
 *  Flash 里另一处，必须分段喂进同一条 CRC 才与写入端等价。 */
static uint16_t log_crc16_upd(uint16_t crc, const uint8_t *p, uint16_t n)
{
    uint16_t i;
    uint8_t  b;

    for (i = 0U; i < n; i++) {
        crc ^= (uint16_t)p[i];
        for (b = 0U; b < 8U; b++) {
            crc = (uint16_t)(((crc & 0x0001U) != 0U) ? ((crc >> 1) ^ 0xA001U) : (crc >> 1));
        }
    }
    return crc;
}

/* 算一条记录的 CRC16：时间戳(4) + 长度(2) + 数据(len)
 * 不含 seq（seq 是最后落盘的提交标志，不参与校验）
 * 也不含 CRC16 自身（CRC 字段在槽偏移 10） */
static uint16_t log_checksum(const uint8_t *rec, uint16_t len)
{
    uint16_t crc = 0xFFFFU;

    crc = log_crc16_upd(crc, &rec[4], 6U);                  /* ts + len */
    crc = log_crc16_upd(crc, &rec[W25QXX_LOG_HDR_SIZE], len);  /* 数据段（从偏移 12 起） */
    return crc;
}

/* 按槽号把一条记录读出来（内部用，index 版的最终都落到这里） */
static uint8_t log_read_slot(uint16_t slot, uint32_t *ts, uint8_t *buf, uint16_t *len)
{
    uint8_t  rec[W25QXX_LOG_SLOT_SIZE];
    uint32_t addr;
    uint32_t sq;
    uint16_t n;
    uint16_t sum;
    uint16_t i;

    addr = (uint32_t)W25QXX_LOG_BASE + ((uint32_t)slot * W25QXX_LOG_SLOT_SIZE);

    if (W25QXX_Read(rec, addr, (uint32_t)W25QXX_LOG_SLOT_SIZE) != 0U) {
        return W25QXX_LOG_ERR_FLASH;
    }

    /* 1) seq == 0xFFFFFFFF = 空槽（也可能是上次写到一半断电） */
    sq = log_get_u32(&rec[0]);
    if (sq == W25QXX_LOG_EMPTY) return W25QXX_LOG_ERR_EMPTY;

    /* 2) 长度得合法，不然下面的循环会越界 */
    n = (uint16_t)((uint16_t)rec[8] | ((uint16_t)rec[9] << 8));
    if (n > (uint16_t)W25QXX_LOG_PAYLOAD) return W25QXX_LOG_ERR_CORRUPT;

    /* 3) CRC16 校验不过就返回错误码，坏数据不返回给调用方 */
    sum = log_checksum(rec, n);
    if ((uint16_t)((uint16_t)rec[10] | ((uint16_t)rec[11] << 8)) != sum) {
        return W25QXX_LOG_ERR_CORRUPT;
    }

    if (ts  != 0) *ts  = log_get_u32(&rec[4]);
    if (len != 0) *len = n;
    if (buf != 0) {
        for (i = 0U; i < n; i++) buf[i] = rec[W25QXX_LOG_HDR_SIZE + i];
        buf[n] = (uint8_t)'\0';               /* 当文本读的时候省得自己补 */
    }
    return W25QXX_LOG_OK;
}


/* 区块 2：基础功能 */

uint8_t W25QXX_LogInit(void)
{
    uint16_t total;
    uint16_t i;
    uint16_t n = 0U;
    uint16_t max_slot = 0U;
    uint32_t max_seq = 0UL;
    uint8_t  found = 0U;

    s_inited = 0U;

    /* 确认 Flash 在线，同时拦住"忘了调 W25QXX_Init()"：
     * 没初始化时 SPI 没配，读回全 0 或全 FF */
    {
        uint32_t id = W25QXX_ReadID();
        if ((id == 0xFFFFFFFFUL) || (id == 0x00000000UL)) return W25QXX_LOG_ERR_FLASH;
    }

    total = W25QXX_LogCapacity();

    /* 扫一遍所有槽：数出有效条数，记录序号最大的那个槽。每条只读 12 字节
     * 槽头（seq + ts + len + CRC16），逐条校验 CRC16：写坏的记录不能算有效，
     * 否则补传任务会按虚高的 s_count 空转。 */
    for (i = 0U; i < total; i++) {
        uint8_t  hdr[W25QXX_LOG_HDR_SIZE];      /* = 12 字节：seq/ts/len/CRC */
        uint32_t addr = (uint32_t)W25QXX_LOG_BASE + ((uint32_t)i * W25QXX_LOG_SLOT_SIZE);
        uint32_t sq;
        uint16_t nd;
        uint16_t sum;

        if (W25QXX_Read(hdr, addr, (uint32_t)W25QXX_LOG_HDR_SIZE) != 0U) return W25QXX_LOG_ERR_FLASH;
        if (log_get_u32(&hdr[0]) == W25QXX_LOG_EMPTY) continue;

        /* 长度先判合法（不合法的话校验范围都会算错） */
        nd = (uint16_t)((uint16_t)hdr[8] | ((uint16_t)hdr[9] << 8));
        if (nd > (uint16_t)W25QXX_LOG_PAYLOAD) continue;

        /* 校验和：手上只有 12 字节槽头，数据段在槽的 [12 .. 12+nd)。CRC 分段
         * 续算：先喂 [4..10)（ts+len），再分批喂数据段，与写入端一致。 */
        {
            uint16_t crc = 0xFFFFU;
            uint16_t remain = nd;
            uint32_t da     = addr + W25QXX_LOG_HDR_SIZE;

            crc = log_crc16_upd(crc, &hdr[4], 6U);

            while (remain > 0U) {
                uint8_t  chunk[16];
                uint16_t take = (remain > 16U) ? 16U : remain;

                if (W25QXX_Read(chunk, da, (uint32_t)take) != 0U) return W25QXX_LOG_ERR_FLASH;
                crc = log_crc16_upd(crc, chunk, take);

                da     += take;
                remain = (uint16_t)(remain - take);
            }
            sum = crc;
        }

        if ((uint16_t)((uint16_t)hdr[10] | ((uint16_t)hdr[11] << 8)) != sum) continue;

        sq = log_get_u32(&hdr[0]);
        n++;

        if ((found == 0U) || (sq > max_seq)) {
            max_seq  = sq;
            max_slot = i;
            found    = 1U;
        }
    }

    s_count = n;

    if (found == 0U) {
        /* 一条都没有（要么全新，要么刚被 Clear） */
        s_head     = 0U;
        s_next_seq = 0UL;                           /* 序号从 0 开始用 */
    } else {
        /* 接着序号最大的那条往后写，断电重启不丢前面存的东西 */
        s_head     = (uint16_t)((uint16_t)(max_slot + 1U) % total);
        s_next_seq = max_seq + 1UL;
        /* 撞上 0xFFFFFFFF 就置 0：那是空槽判据，不能拿来当有效记录的序号 */
        if (s_next_seq == W25QXX_LOG_EMPTY) s_next_seq = 0UL;
    }

    s_inited = 1U;
    return W25QXX_LOG_OK;
}

uint8_t W25QXX_LogWrite(uint32_t ts, const uint8_t *data, uint16_t len)
{
    uint8_t  rec[W25QXX_LOG_SLOT_SIZE];
    uint32_t addr;
    uint16_t total;
    uint16_t sum;
    uint16_t i;

    if (s_inited == 0U) return W25QXX_LOG_ERR_NOT_INIT;
    if (len > (uint16_t)W25QXX_LOG_PAYLOAD) return W25QXX_LOG_ERR_TOO_LONG;
    if ((data == 0) && (len > 0U)) return W25QXX_LOG_ERR_PARAM;

    total = W25QXX_LogCapacity();
    addr  = (uint32_t)W25QXX_LOG_BASE + ((uint32_t)s_head * W25QXX_LOG_SLOT_SIZE);

    W25QXX_LOG_LOCK();      /* 整条记录的写入（可能含一次扇区擦除）必须一气呵成，
                             * s_head 在这里读、到函数末尾才推进，中间不能被别人改 */

    /* 1) 这个槽还是上一圈的旧数据，先把它所在的整个扇区擦掉
     *    一个扇区 128 条，平均写 128 次才擦一次，摊下来约 0.8ms/条 */
    if (W25QXX_IsBlank(addr, (uint32_t)W25QXX_LOG_SLOT_SIZE) == 0U) {
        /* 最小擦除单位是 4KB 扇区 = 128 个槽；这一擦下去，同扇区里另外
         * 127 条"写指针还没转到"的旧记录会一并消失，必须把它们从 s_count
         * 里扣掉并计入 s_lost，否则读端会出现 count 说有记录、实际读到
         * 空槽的断号，补传逻辑会卡在第一条上发不出去 */
        uint16_t per_sec = (uint16_t)(W25QXX_SECTOR_SIZE / W25QXX_LOG_SLOT_SIZE);
        uint16_t sec_0   = (uint16_t)((uint16_t)(s_head / per_sec) * per_sec);
        uint16_t lost    = 0U;
        uint16_t s2;

        for (s2 = 0U; s2 < per_sec; s2++) {
            uint16_t sl = (uint16_t)(sec_0 + s2);
            uint16_t d  = (uint16_t)((uint16_t)((uint16_t)(s_head + total) - sl) % total);
            if (d < s_count) lost++;        /* 这条还在有效范围内，会被擦掉 */
        }

        if (W25QXX_EraseSector(addr) != 0U) {
            W25QXX_LOG_UNLOCK();        /* 擦除失败：先解锁再返回 */
            return W25QXX_LOG_ERR_FLASH;
        }

        s_count = (uint16_t)((s_count > lost) ? (s_count - lost) : 0U);
        s_lost  = (uint16_t)(s_lost + lost);
    }

    /* 2) 把整条记录在 RAM 里拼好（seq 落到 Flash 是第 4) 步，这里先摆进缓冲区） */
    memset(rec, 0xFF, sizeof(rec));
    /* seq 既是序号，又是"这条算不算数"的提交标志：它的空槽值 0xFFFFFFFF
     * 不能写入，落盘是第 4) 步单独写槽偏移 [0..4) */
    log_put_u32(&rec[0], s_next_seq);
    log_put_u32(&rec[4], ts);
    rec[8] = (uint8_t)(len & 0xFFU);
    rec[9] = (uint8_t)(len >> 8);

    if (len > 0U) {
        for (i = 0U; i < len; i++) rec[W25QXX_LOG_HDR_SIZE + i] = data[i];
    }

    sum = log_checksum(rec, len);
    rec[10] = (uint8_t)(sum & 0xFFU);       /* CRC16 低字节在前 */
    rec[11] = (uint8_t)(sum >> 8);

    /* 3) 先写槽偏移 [4..32)：时间戳、长度、CRC16、数据落盘，但 seq 仍是 0xFF，
     *    即该槽仍为空 */
    if (W25QXX_Write(&rec[4], addr + 4U, (uint32_t)(W25QXX_LOG_SLOT_SIZE - 4U)) != 0U) {
        W25QXX_LOG_UNLOCK();        /* 出错也必须解锁，否则调度器/中断被永久挂起 */
        return W25QXX_LOG_ERR_FLASH;
    }

    /* 4) 再单独写槽偏移 [0..4) 的 seq，这一步落了这条记录才算数。
     *    两次写的是同一页里互不重叠的两段，NOR 闪存允许先写一段再补另一段 */
    if (W25QXX_Write(&rec[0], addr, 4U) != 0U) {
        W25QXX_LOG_UNLOCK();        /* 同上：先解锁再返回 */
        return W25QXX_LOG_ERR_FLASH;
    }

    /* 5) 推进环形指针 */
    s_head = (uint16_t)((uint16_t)(s_head + 1U) % total);
    if (s_count < total) s_count++;
    s_next_seq++;
    /* 跳过空槽判据 0xFFFFFFFF：自增后撞上就置 0，否则这条有效记录会被读端当成空槽 */
    if (s_next_seq == W25QXX_LOG_EMPTY) s_next_seq = 0UL;

    W25QXX_LOG_UNLOCK();    /* 解除并发保护（与上面 W25QXX_LOG_LOCK() 成对） */
    return W25QXX_LOG_OK;
}

uint8_t W25QXX_LogWriteStr(uint32_t ts, const char *text)
{
    uint16_t len = 0U;

    if (text == 0) return W25QXX_LOG_ERR_PARAM;
    while ((text[len] != '\0') && (len < (uint16_t)W25QXX_LOG_PAYLOAD)) len++;

    return W25QXX_LogWrite(ts, (const uint8_t *)text, len);
}

uint16_t W25QXX_LogCount(void)
{
    return s_count;
}

uint8_t W25QXX_LogClear(void)
{
    uint32_t i;

    if (s_inited == 0U) return W25QXX_LOG_ERR_NOT_INIT;

    W25QXX_LOG_LOCK();      /* 擦除期间不许别的任务写/读同一片 Flash */
    for (i = 0UL; i < (uint32_t)W25QXX_LOG_SECTORS; i++) {
        uint32_t a = (uint32_t)W25QXX_LOG_BASE + (i * W25QXX_SECTOR_SIZE);
        if (W25QXX_EraseSector(a) != 0U) {
            W25QXX_LOG_UNLOCK();        /* 中途失败：已擦的扇区保持全空，指针不动即可 */
            return W25QXX_LOG_ERR_FLASH;
        }
    }

    s_head     = 0U;
    s_count    = 0U;
    s_lost     = 0U;
    s_next_seq = 0UL;

    W25QXX_LOG_UNLOCK();
    return W25QXX_LOG_OK;
}


/* 区块 3：扩展功能 */

uint8_t W25QXX_LogRead(uint16_t index, uint32_t *ts, uint8_t *buf, uint16_t *len)
{
    uint16_t total;
    uint16_t oldest;
    uint8_t  ret;

    if (s_inited == 0U) return W25QXX_LOG_ERR_NOT_INIT;

    /* 从校验 index 到读完这一槽必须整体原子：否则写任务在中间转一圈环形指针，
     * oldest 就变了，读到的会是别的记录 */
    W25QXX_LOG_LOCK();

    if (index >= s_count) {
        W25QXX_LOG_UNLOCK();
        return W25QXX_LOG_ERR_EMPTY;
    }

    total = W25QXX_LogCapacity();

    /* 最老的一条 = 写指针往回退 count 条。
     * 先加 total 再取模，避免退过头变成负数 */
    oldest = (uint16_t)((uint16_t)((uint16_t)(s_head + total) - s_count) % total);

    ret = log_read_slot((uint16_t)((uint16_t)(oldest + index) % total), ts, buf, len);
    W25QXX_LOG_UNLOCK();
    return ret;
}

uint8_t W25QXX_LogTail(uint32_t *ts, uint8_t *buf, uint16_t *len)
{
    if (s_inited == 0U) return W25QXX_LOG_ERR_NOT_INIT;
    if (s_count == 0U) return W25QXX_LOG_ERR_EMPTY;

    return W25QXX_LogRead((uint16_t)(s_count - 1U), ts, buf, len);
}

uint16_t W25QXX_LogCapacity(void)
{
    return (uint16_t)(W25QXX_LOG_SECTORS * (W25QXX_SECTOR_SIZE / W25QXX_LOG_SLOT_SIZE));
}

void W25QXX_LogStat(uint16_t *count, uint16_t *cap, uint16_t *head)
{
    if (count != 0) *count = s_count;
    if (cap   != 0) *cap   = W25QXX_LogCapacity();
    if (head  != 0) *head  = s_head;
}

/* 因整扇区擦除被连带丢弃的记录数（只增，用于判断缓存是否已溢出丢数据） */
uint16_t W25QXX_LogLost(void)
{
    return s_lost;
}

const char *W25QXX_LogErrStr(uint8_t err)
{
    switch (err) {
    case W25QXX_LOG_OK:           return "OK";
    case W25QXX_LOG_ERR_PARAM:    return "ERR: null param";
    case W25QXX_LOG_ERR_NOT_INIT: return "ERR: call W25QXX_LogInit first";
    case W25QXX_LOG_ERR_TOO_LONG: return "ERR: data longer than payload";
    case W25QXX_LOG_ERR_FLASH:    return "ERR: flash no answer or r/w failed";
    case W25QXX_LOG_ERR_EMPTY:    return "ERR: no such record";
    case W25QXX_LOG_ERR_CORRUPT:  return "ERR: checksum mismatch";
    default:                      return "ERR: unknown";
    }
}

/* w25qxx_log.c end */
