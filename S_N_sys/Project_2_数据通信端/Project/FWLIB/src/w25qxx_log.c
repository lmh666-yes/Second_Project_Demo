#include "w25qxx_log.h"
#include <string.h>

/* ================================================================
 *  w25qxx_log.c —— 外部 Flash 滚动记录实现
 * ================================================================
 *  硬件映射表
 *  ---------------------------------------------------------------
 *   功能        资源                板上位置         改这里
 *  ---------------------------------------------------------------
 *   Flash       SPI1 @10MHz         U2 (GD25Q128)    w25qxx.h
 *   片选        PB14 (FLASH_CS)     板上直连          w25qxx.h
 *   分区        0x001000 起 64KB    ——               w25qxx.h / 本文件区块1
 *  ---------------------------------------------------------------
 *  ⚠ 本模块不额外占任何外设，全走 w25qxx.h 的接口
 *
 *  【环形结构长这样（16 扇区 × 64 槽 = 1024 条）】
 *      slot 0    slot 1    slot 2   ...  slot 63 │ slot 64  ...  slot 1023
 *      └────────── 扇区 0 (4KB) ──────────────┘ └───── 扇区 1 ─────────┘
 *
 *      写指针 head 从 0 一路加到 1023 再回 0
 *      最老的一条 = head 往回退 count 条
 *      count 涨到 1024 就不再涨了（等于"满了，开始盖旧的"）
 *
 *  【写入时序：为什么不会读到半截数据】
 *      ① 擦（仅在跨到脏槽位时，平均 64 次才擦一次）
 *      ② 写 rec[4..64)  —— 序号/时间戳/长度/校验/数据
 *      ③ 写 rec[0..4)    —— 状态字 0x5A5A5A5A
 *      读的时候先看状态字：不是 0x5A5A5A5A 一律当空槽跳过。
 *      所以 ②③ 之间断电 → 这条被忽略，下次 init 从它开始重写 ✓
 *
 *  ⚠ W25QXX_Write 要求目标已擦除 —— 本模块②③写之前保证了这点，
 *    所以能用快的那个（~1ms），不需要 WriteSafe（每次 ~50ms）。
 * ================================================================ */

/* ================================================================
 *                      编译期护栏（配错立刻报错）
 * ================================================================ */
/* 槽长必须能整除扇区和页 —— 否则一条记录会跨扇区/跨页，擦写时序就不成立了 */
typedef char w25qxx_log_slot_check[
    (((W25QXX_SECTOR_SIZE % W25QXX_LOG_SLOT_SIZE) == 0UL) &&
     ((W25QXX_PAGE_SIZE   % W25QXX_LOG_SLOT_SIZE) == 0UL)) ? 1 : -1];

/* 日志区必须落在分区表划的 LOG ~ FONT 之间，不能压到字库 */
typedef char w25qxx_log_region_check[
    ((W25QXX_LOG_BASE + ((uint32_t)W25QXX_LOG_SECTORS * W25QXX_SECTOR_SIZE))
        <= W25QXX_ADDR_FONT) ? 1 : -1];

/* 槽长要装得下"头部 + 至少 8 字节数据"，否则这日志没意义 */
typedef char w25qxx_log_payload_check[
    (((uint32_t)W25QXX_LOG_SLOT_SIZE - (uint32_t)W25QXX_LOG_HDR_SIZE) >= 8UL) ? 1 : -1];


/* ================================================================
 *                          模块内部状态
 * ================================================================ */
static uint8_t  s_inited   = 0U;    /* 1 = 已经 Scan 过 */
static uint16_t s_head     = 0U;    /* 下一条要写哪个槽 */
static uint16_t s_count    = 0U;    /* 现有多少条有效记录 */
static uint32_t s_next_seq = 1UL;   /* 下一条记录的序号 */


/* ================================================================
 *                        内部小工具
 * ================================================================ */

/* 槽里统一用**小端**存（低字节在前）。不用 memcpy 强转是为了换平台也正确 */
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

/* 算一条记录的校验和：序号(4) + 时间戳(4) + 长度(2) + 数据(len)
 * 故意**不含**状态字和校验和本身 —— 不然自己算自己了 */
static uint16_t log_checksum(const uint8_t *rec, uint16_t len)
{
    uint16_t sum = 0U;
    uint16_t i;

    for (i = 4U; i < 14U; i++) sum = (uint16_t)(sum + (uint16_t)rec[i]);
    for (i = W25QXX_LOG_HDR_SIZE; i < (uint16_t)(W25QXX_LOG_HDR_SIZE + len); i++) {
        sum = (uint16_t)(sum + (uint16_t)rec[i]);
    }
    return sum;
}

/* 按槽号把一条记录读出来（内部用，index 版的最终都落到这里） */
static uint8_t log_read_slot(uint16_t slot, uint32_t *ts, uint8_t *buf, uint16_t *len)
{
    uint8_t  rec[W25QXX_LOG_SLOT_SIZE];
    uint32_t addr;
    uint32_t st;
    uint16_t n;
    uint16_t sum;
    uint16_t i;

    addr = (uint32_t)W25QXX_LOG_BASE + ((uint32_t)slot * W25QXX_LOG_SLOT_SIZE);

    if (W25QXX_Read(rec, addr, (uint32_t)W25QXX_LOG_SLOT_SIZE) != 0U) {
        return W25QXX_LOG_ERR_FLASH;
    }

    /* ① 状态字不对 = 空槽（也可能是上次写到一半断电） */
    st = log_get_u32(&rec[0]);
    if (st != W25QXX_LOG_VALID) return W25QXX_LOG_ERR_EMPTY;

    /* ② 长度得合法，不然下面的循环会越界 */
    n = (uint16_t)((uint16_t)rec[12] | ((uint16_t)rec[13] << 8));
    if (n > (uint16_t)W25QXX_LOG_PAYLOAD) return W25QXX_LOG_ERR_CORRUPT;

    /* ③ 校验和 —— 写坏了的数据宁可报错也别拿去用 */
    sum = log_checksum(rec, n);
    if ((uint16_t)((uint16_t)rec[14] | ((uint16_t)rec[15] << 8)) != sum) {
        return W25QXX_LOG_ERR_CORRUPT;
    }

    if (ts  != 0) *ts  = log_get_u32(&rec[8]);
    if (len != 0) *len = n;
    if (buf != 0) {
        for (i = 0U; i < n; i++) buf[i] = rec[W25QXX_LOG_HDR_SIZE + i];
        buf[n] = (uint8_t)'\0';               /* 当文本读的时候省得自己补 */
    }
    return W25QXX_LOG_OK;
}


/* ================================================================
 *                        区块 2：基础功能
 * ================================================================ */

uint8_t W25QXX_LogInit(void)
{
    uint16_t total;
    uint16_t i;
    uint16_t n = 0U;
    uint16_t max_slot = 0U;
    uint32_t max_seq = 0UL;
    uint8_t  found = 0U;

    s_inited = 0U;

    /* 先摸摸 Flash 在不在 —— 顺便拦住"忘了调 W25QXX_Init()"
     * （没初始化时 SPI 没配，读回全 0 或全 FF） */
    {
        uint32_t id = W25QXX_ReadID();
        if ((id == 0xFFFFFFFFUL) || (id == 0x00000000UL)) return W25QXX_LOG_ERR_FLASH;
    }

    total = W25QXX_LogCapacity();

    /* 扫一遍所有槽：数出有效条数，同时记住序号最大的那个槽
     * 每条只读 8 字节（状态+序号），1024 条 ≈ 11ms */
    for (i = 0U; i < total; i++) {
        uint8_t  hdr[8];
        uint32_t addr = (uint32_t)W25QXX_LOG_BASE + ((uint32_t)i * W25QXX_LOG_SLOT_SIZE);
        uint32_t sq;

        if (W25QXX_Read(hdr, addr, 8U) != 0U) return W25QXX_LOG_ERR_FLASH;
        if (log_get_u32(&hdr[0]) != W25QXX_LOG_VALID) continue;

        sq = log_get_u32(&hdr[4]);
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
        s_next_seq = 1UL;
    } else {
        /* 接着序号最大的那条往后写 —— 断电重启不丢前面存的东西 ✓ */
        s_head     = (uint16_t)((uint16_t)(max_slot + 1U) % total);
        s_next_seq = max_seq + 1UL;
        if (s_next_seq == 0UL) s_next_seq = 1UL;    /* 序号回绕，绕开 0 */
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

    /* ① 这个槽还是上一圈的旧数据 → 先把它所在的整个扇区擦掉
     *    一个扇区 64 条，所以平均写 64 次才擦一次，摊下来开销很小（~0.8ms/条） */
    if (W25QXX_IsBlank(addr, (uint32_t)W25QXX_LOG_SLOT_SIZE) == 0U) {
        if (W25QXX_EraseSector(addr) != 0U) return W25QXX_LOG_ERR_FLASH;
    }

    /* ② 把整条记录在 RAM 里拼好（除了状态字，那个最后单独写） */
    memset(rec, 0xFF, sizeof(rec));
    log_put_u32(&rec[0], W25QXX_LOG_EMPTY);     /* 占位，等会儿才真正落下去 */
    log_put_u32(&rec[4], s_next_seq);
    log_put_u32(&rec[8], ts);
    rec[12] = (uint8_t)(len & 0xFFU);
    rec[13] = (uint8_t)(len >> 8);

    if (len > 0U) {
        for (i = 0U; i < len; i++) rec[W25QXX_LOG_HDR_SIZE + i] = data[i];
    }

    sum = log_checksum(rec, len);
    rec[14] = (uint8_t)(sum & 0xFFU);
    rec[15] = (uint8_t)(sum >> 8);

    /* ③ 先写 [4..64) —— 此刻状态字还是 0xFF，也就是"这槽是空的" */
    if (W25QXX_Write(&rec[4], addr + 4U, (uint32_t)(W25QXX_LOG_SLOT_SIZE - 4U)) != 0U) {
        return W25QXX_LOG_ERR_FLASH;
    }

    /* ④ 再单独写状态字 [0..4) —— 这一步落了，这条记录才算数
     *    两次写的是同一页里互不重叠的两段，NOR 闪存允许先写一段再补另一段 ✓ */
    if (W25QXX_Write(&rec[0], addr, 4U) != 0U) return W25QXX_LOG_ERR_FLASH;

    /* ⑤ 推进环形指针 */
    s_head = (uint16_t)((uint16_t)(s_head + 1U) % total);
    if (s_count < total) s_count++;
    s_next_seq++;
    if (s_next_seq == 0UL) s_next_seq = 1UL;

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

    for (i = 0UL; i < (uint32_t)W25QXX_LOG_SECTORS; i++) {
        uint32_t a = (uint32_t)W25QXX_LOG_BASE + (i * W25QXX_SECTOR_SIZE);
        if (W25QXX_EraseSector(a) != 0U) return W25QXX_LOG_ERR_FLASH;
    }

    s_head     = 0U;
    s_count    = 0U;
    s_next_seq = 1UL;

    return W25QXX_LOG_OK;
}


/* ================================================================
 *                        区块 3：扩展功能
 * ================================================================ */

uint8_t W25QXX_LogRead(uint16_t index, uint32_t *ts, uint8_t *buf, uint16_t *len)
{
    uint16_t total;
    uint16_t oldest;

    if (s_inited == 0U) return W25QXX_LOG_ERR_NOT_INIT;
    if (index >= s_count) return W25QXX_LOG_ERR_EMPTY;

    total = W25QXX_LogCapacity();

    /* 最老的一条 = 写指针往回退 count 条。
     * 先加 total 再取模，避免"退过头"变成负数 */
    oldest = (uint16_t)((uint16_t)((uint16_t)(s_head + total) - s_count) % total);

    return log_read_slot((uint16_t)((uint16_t)(oldest + index) % total), ts, buf, len);
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

/* ==================== w25qxx_log.c end ==================== */
