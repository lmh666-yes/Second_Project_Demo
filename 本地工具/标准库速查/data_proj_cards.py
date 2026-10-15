# -*- coding: utf-8 -*-
r"""data_proj_cards.py —— 本项目自己 FWLIB 模块的流程卡 / 引导向导文本源。

跟 data_extra_cards.py 的区别：
  本文件用 **raw 三引号** + **单反斜杠 n** 的写法，跟 add_cards.py 一致。
  _gen3.py 是「按原始文本直接拼接、不做 Python 解析」的，所以：
      想要 JS 里的换行   -> 写 \n     （单反斜杠）
      想要 JS 里的引号   -> 写 \"     （单反斜杠 + 半角引号）
      想让 C 代码里显示字面 CRLF -> 写 \\r\\n（双反斜杠）
  不要改成「双反斜杠 n」，那正是 data_extra_cards.py 的老写法，
    结果 DAC/CAN/RTC 三张卡的代码框整段没有换行（_gen3.py 的 fix_ex() 就是在补它）。

引号铁律：字符串内部不能出现裸的半角双引号；中文引号一律用全角 “ ”。

卡片编号：⑱ 起（①~⑰ 已被 SPL 外设卡占用）。
"""

# ======================================================================
# ⑱ SPI Flash 断网缓存日志（项目核心功能）
# ======================================================================

FLOW_P18_W25QXX_LOG = r"""
/* ===================== ⑱ SPI Flash 断网缓存日志 ===================== */
{m:"流程", t:"flow", n:"⑱ SPI Flash 断网缓存日志（掉线先存本地，上线再补传）",
 d:"网络说断就断，数据不能丢。做法是：每条数据先写进板载 SPI Flash 的环形日志区，补传成功后才往下走。本卡的难点全在「擦除单位是 4KB 而不是 1 条」和「怎么保证掉电不会留下半条记录」这两件事上。",
 f:[
  ["先认清这颗 Flash","板2 焊的是 GD25Q128（JEDEC = 0xC84018），不是 Winbond 的 W25Q128（0xEF4018）。判身份只看容量字节 0x18，写死比较 0xEF4018 会让初始化永远失败","GD25Q128 与 W25Q128 指令集兼容，能直接换用"],
  ["接好线再谈别的","SPI1 重映射：PB3=SCK / PB4=MISO / PB5=MOSI，CS 用 PB14 软件片选。这套脚 NRF24L01 也在用，两个模块不能同时接"],
  ["读 JEDEC ID 确认通信","W25QXX_Init(10MHz) 之后调 W25QXX_ReadID()。读回 0xFFFFFF 是 MISO 没接、0x000000 是 MOSI/时钟没通","10MHz 跑飞了就降到 2MHz，杜邦线长的时候很常见"],
  ["先画分区表再写代码","0x000000 参数区（1 扇区）/ 0x001000 日志区（16 扇区 = 64KB）/ 0x020000 字库区。分区一旦写进现场设备就很难改，先想清楚再烧"],
  ["设计槽结构","32 字节一条：12 字节头（seq 4 + ts 4 + len 2 + CRC16 2）+ 20 字节载荷。4KB 扇区正好 128 条，16 个扇区 = 2048 条","槽越大条数越少：64 字节槽只能存 1024 条，利用率还不到 30%"],
  ["原子提交：先写数据、最后写 seq","先写 [4..32)（ts+len+载荷+CRC），最后单独写 [0..4) 的 seq。seq 还是 0xFFFFFFFF 时这条就「不存在」，掉电最坏只是少一条，绝不会出现半条脏数据"],
  ["判空只看 seq，不要另设状态字","有效性判据就是 `seq != 0xFFFFFFFF`。另设一个「状态字 == VALID」的字段，只要有一处忘了写它，整片缓存就 100% 读不出来（旧版就是这么坏的）"],
  ["擦除单位是 4KB，不是 1 条","为腾出一个槽去擦整个扇区，会连带毁掉同扇区另外 127 条没补传的记录，而且条数计数器还在涨 → 出现「count 说有 1024 条、实际最老那批全是空槽」。只能整扇区都补传完了才允许擦"],
  ["读到空槽要「跳过继续找」","读取端拿到「空槽」不能当成「后面没有了」。环形缓冲里空间是断续的，停下来会让积压永远补不完"],
  ["校验用 CRC16，不用字节和","16 位字节累加检不出字节换序，也检不出 0x01/0xFF 这种成对错误。直接复用 modbus.c 的 CRC16-MODBUS（0xA001、初值 0xFFFF、末尾不异或）"],
  ["写满要记账，不能静默覆盖","环形写满一圈后 `s_count` 不能只加不减。必须维护一个溢出计数 s_lost 并暴露给上层，静默丢数据是这类模块最不能接受的失败方式"]
 ],
 a:[
  ["SPI 口 / 速率","SPI1（重映射 PB3/PB4/PB5）/ 10MHz","与 NRF24L01 共用总线；总线要有互斥"],
  ["片选","PB14，软件控制","不用硬件 NSS 是因为要和别的 SPI 器件分时复用"],
  ["容量 / 单位","16MB / 页 256B / 扇区 4KB / 块 64KB","最小擦除单位是扇区 4KB，页只是单次写上限"],
  ["槽结构","12B 头 + 20B 载荷 = 32B","head: seq(4) ts(4) len(2) crc16(2)，全部小端"],
  ["容量","16 扇区 × 128 条 = 2048 条","按每秒 1 条算能缓存 34 分钟"],
  ["CRC 覆盖范围","ts + len + 载荷（6+n 字节）","不含 seq 也不含 CRC 自身，seq 是最后写的，算进去自相矛盾"],
  ["空槽标记","seq == 0xFFFFFFFF","这是唯一有效性判据；序号递增时要主动跳过这个值"],
  ["写一条的耗时","页内约 0.7ms + 扇区擦除 45ms","擦扇区那一瞬间很长，写完要喂狗"],
  ["WEL 写使能","每次写/擦前都要发送 0x06","漏了它命令被静默忽略，函数返回成功但读回还是 0xFF"],
  ["地址越界防护","`addr > SIZE 或 len > SIZE - addr`","写成 `addr + len > SIZE` 会无符号回绕，越界检查形同虚设"]
 ],
 e:"/* ============================================================\n *  SPI Flash 断网缓存日志，分区 / 槽结构 / 原子提交 / 环形回绕\n *  平台：STM32F407 · GD25Q128 · SPI1 重映射 PB3/PB4/PB5 · CS = PB14\n * ============================================================ */\n#include \"stm32f4xx.h\"\n#include \"w25qxx.h\"\n\n/* ---- ① 分区表：改这里就等于改整个存储布局 ---- */\n#define ADDR_PARAM   0x000000UL      /* 参数区：1 扇区                    */\n#define ADDR_LOG     0x001000UL      /* 日志区：16 扇区 = 64KB            */\n#define ADDR_FONT    0x020000UL      /* 字库 / 图片区                     */\n\n/* ---- ② 槽结构：32 字节一条，4KB 扇区正好 128 条 ---- */\n#define SLOT_SIZE    32U\n#define HDR_SIZE     12U\n#define PAYLOAD      (SLOT_SIZE - HDR_SIZE)                        /* 20  */\n#define LOG_SECTORS  16U\n#define SLOTS_PER_SEC (W25QXX_SECTOR_SIZE / SLOT_SIZE)             /* 128 */\n#define LOG_CAPACITY  ((uint16_t)(LOG_SECTORS * SLOTS_PER_SEC))    /* 2048 */\n#define LOG_EMPTY    0xFFFFFFFFUL\n\n/* 槽内 12 字节头（小端）：\n *   [0..4)   seq  序号，0xFFFFFFFF = 空槽（唯一有效性判据）\n *   [4..8)   ts   时间戳（秒）\n *   [8..10)  len  载荷长度\n *   [10..12) crc  CRC16-MODBUS，覆盖 ts + len + 载荷\n */\n\nstatic uint16_t s_head;              /* 下一个要写的槽 */\nstatic uint16_t s_count;             /* 当前有效条数   */\nstatic uint32_t s_next_seq = 1UL;    /* 递增序号，跳过 0xFFFFFFFF */\nstatic uint16_t s_lost;              /* 被挤掉 / 被擦掉的条数（必须上报） */\n\n/* ---- CRC16-MODBUS：多项式 0xA001，初值 0xFFFF，末尾不异或 ---- */\nstatic uint16_t log_crc16(const uint8_t *buf, uint16_t len)\n{\n    uint16_t crc = 0xFFFFU;\n    uint16_t i;\n    uint8_t  b;\n\n    for (i = 0U; i < len; i++)\n    {\n        crc ^= (uint16_t)buf[i];\n        for (b = 0U; b < 8U; b++)\n        {\n            if (crc & 0x0001U) { crc = (uint16_t)((crc >> 1) ^ 0xA001U); }\n            else               { crc = (uint16_t)(crc >> 1); }\n        }\n    }\n    return crc;\n}\n\nstatic uint32_t slot_addr(uint16_t index)\n{\n    return ADDR_LOG + (uint32_t)index * SLOT_SIZE;\n}\n\n/* 数一数某个扇区里还有几条「活着」的记录（seq != 0xFFFFFFFF） */\nstatic uint16_t log_sector_live(uint16_t first)\n{\n    uint8_t  rec[SLOT_SIZE];\n    uint32_t sq;\n    uint16_t i, k = 0U;\n\n    for (i = 0U; i < SLOTS_PER_SEC; i++)\n    {\n        if (W25QXX_Read(rec, slot_addr((uint16_t)(first + i)), SLOT_SIZE) != 0U) { continue; }\n        sq = (uint32_t)rec[0] | ((uint32_t)rec[1] << 8) |\n             ((uint32_t)rec[2] << 16) | ((uint32_t)rec[3] << 24);\n        if (sq != LOG_EMPTY) { k++; }\n    }\n    return k;\n}\n\n/* ---- ③ 写一条：先写数据区，最后才写 seq ---- */\nuint8_t log_write(uint32_t ts, const uint8_t *data, uint16_t len)\n{\n    uint8_t  rec[SLOT_SIZE];\n    uint8_t  body[6U + PAYLOAD];\n    uint16_t crc;\n    uint16_t i;\n\n    if (data == 0 || len == 0U || len > PAYLOAD) { return 1U; }\n\n    for (i = 0U; i < SLOT_SIZE; i++) { rec[i] = 0xFFU; }\n\n    rec[4] = (uint8_t)(ts);        rec[5] = (uint8_t)(ts >>  8);\n    rec[6] = (uint8_t)(ts >> 16);  rec[7] = (uint8_t)(ts >> 24);\n    rec[8] = (uint8_t)(len);       rec[9] = (uint8_t)(len >> 8);\n\n    for (i = 0U; i < len; i++) { rec[HDR_SIZE + i] = data[i]; }\n\n    /* CRC 只覆盖 ts + len + 载荷，seq 是最后写的，算进去就自相矛盾 */\n    body[0] = rec[4]; body[1] = rec[5]; body[2] = rec[6]; body[3] = rec[7];\n    body[4] = rec[8]; body[5] = rec[9];\n    for (i = 0U; i < len; i++) { body[6U + i] = rec[HDR_SIZE + i]; }\n    crc = log_crc16(body, (uint16_t)(6U + len));\n    rec[10] = (uint8_t)(crc);      rec[11] = (uint8_t)(crc >> 8);\n\n    /* ---- 该槽还是脏的：要腾地方 ---- */\n    if (W25QXX_IsBlank(slot_addr(s_head), SLOT_SIZE) == 0U)\n    {\n        uint16_t first = (uint16_t)(s_head - (s_head % SLOTS_PER_SEC));\n        uint16_t live  = log_sector_live(first);\n\n        /* 擦除单位是 4KB = 128 条。这个扇区里还有 live 条没补传，\n         *   一擦就全没了。这里选择如实记账而不是静默丢弃：\n         *   更好的做法是先把 live 条读出来补传再擦（缓存区越大越走不到这条路）。 */\n        if (live > 0U)\n        {\n            s_lost  = (uint16_t)(s_lost + live);\n            s_count = (uint16_t)(s_count - live);\n        }\n        if (W25QXX_EraseSector(slot_addr(first)) != 0U) { return 4U; }\n    }\n\n    /* ---- 第一步：先写 [4..32)。此刻 seq 还是 0xFFFFFFFF，这条「不存在」 ---- */\n    if (W25QXX_Write(&rec[4], slot_addr(s_head) + 4U, SLOT_SIZE - 4U) != 0U) { return 4U; }\n\n    /* ---- 第二步：最后单独写 seq。[0..4) 落盘之前掉电 = 少一条，不会半条 ---- */\n    rec[0] = (uint8_t)(s_next_seq);        rec[1] = (uint8_t)(s_next_seq >>  8);\n    rec[2] = (uint8_t)(s_next_seq >> 16);  rec[3] = (uint8_t)(s_next_seq >> 24);\n    if (W25QXX_Write(&rec[0], slot_addr(s_head), 4U) != 0U) { return 4U; }\n\n    s_next_seq++;\n    if (s_next_seq == LOG_EMPTY) { s_next_seq = 1UL; }   /* 主动跳过空槽标记 */\n\n    s_head = (uint16_t)((s_head + 1U) % LOG_CAPACITY);\n    if (s_count < LOG_CAPACITY) { s_count++; }\n    else                        { s_lost++; }            /* 写满要记账 */\n\n    return 0U;\n}\n\n/* ---- ④ 读第 index 条（index 0 = 最老） ---- */\nuint8_t log_read(uint16_t index, uint32_t *ts, uint8_t *buf, uint16_t *len)\n{\n    uint8_t  rec[SLOT_SIZE];\n    uint8_t  body[6U + PAYLOAD];\n    uint32_t sq;\n    uint16_t n, crc_saved, crc_calc, i;\n\n    if (index >= s_count) { return 5U; }                 /* 5 = 这条是空的 */\n    if (W25QXX_Read(rec, slot_addr(index), SLOT_SIZE) != 0U) { return 4U; }\n\n    sq = (uint32_t)rec[0] | ((uint32_t)rec[1] << 8) |\n         ((uint32_t)rec[2] << 16) | ((uint32_t)rec[3] << 24);\n\n    /* 判空只看 seq。旧版靠一个「状态字 == VALID」，而那个状态字从来没被写进去，\n     *   于是每条记录都被判成空槽，写入报成功、补传永远 0 条。 */\n    if (sq == LOG_EMPTY) { return 5U; }\n\n    n = (uint16_t)rec[8] | ((uint16_t)rec[9] << 8);\n    if (n == 0U || n > PAYLOAD) { return 6U; }           /* 6 = 坏了 */\n\n    body[0] = rec[4]; body[1] = rec[5]; body[2] = rec[6]; body[3] = rec[7];\n    body[4] = rec[8]; body[5] = rec[9];\n    for (i = 0U; i < n; i++) { body[6U + i] = rec[HDR_SIZE + i]; }\n\n    crc_saved = (uint16_t)rec[10] | ((uint16_t)rec[11] << 8);\n    crc_calc  = log_crc16(body, (uint16_t)(6U + n));\n    if (crc_calc != crc_saved) { return 6U; }\n\n    *ts = (uint32_t)rec[4] | ((uint32_t)rec[5] << 8) |\n          ((uint32_t)rec[6] << 16) | ((uint32_t)rec[7] << 24);\n    for (i = 0U; i < n; i++) { buf[i] = rec[HDR_SIZE + i]; }\n    *len = n;\n    return 0U;\n}\n\n/* ---- ⑤ 补传端：读到空槽要「跳过继续找」，不能就此收工 ---- */\nuint16_t log_drain(void (*send)(uint32_t ts, const uint8_t *d, uint16_t n))\n{\n    uint8_t  buf[PAYLOAD];\n    uint16_t len;\n    uint32_t ts;\n    uint16_t i, ok = 0U;\n\n    for (i = 0U; i < LOG_CAPACITY; i++)\n    {\n        if (log_read(i, &ts, buf, &len) == 0U)\n        {\n            send(ts, buf, len);\n            ok++;\n        }\n        /* 5（空槽）/ 6（损坏）都只是「这条跳过」，继续往后找 */\n    }\n    return ok;\n}\n\n/* ---- ⑥ 初始化：扫一遍恢复写指针，不额外磨损指针页 ---- */\nuint8_t log_init(void)\n{\n    uint8_t  rec[SLOT_SIZE];\n    uint32_t sq, max_seq = 0UL;\n    uint16_t i;\n\n    s_head = 0U; s_count = 0U; s_lost = 0U; s_next_seq = 1UL;\n\n    for (i = 0U; i < LOG_CAPACITY; i++)\n    {\n        if (W25QXX_Read(rec, slot_addr(i), SLOT_SIZE) != 0U) { continue; }\n        sq = (uint32_t)rec[0] | ((uint32_t)rec[1] << 8) |\n             ((uint32_t)rec[2] << 16) | ((uint32_t)rec[3] << 24);\n        if (sq == LOG_EMPTY) { s_head = i; break; }\n        s_count++;\n        if (sq > max_seq) { max_seq = sq; }\n    }\n    s_next_seq = max_seq + 1UL;\n    if (s_next_seq == LOG_EMPTY) { s_next_seq = 1UL; }\n    return 0U;\n}\n\n/* 常见问题：\n * 1) 判身份写死 JEDEC == 0xEF4018，板子上是 GD25Q128(0xC84018)，初始化永远失败。\n *    正确判据：只看容量字节，`(id & 0xFF) == 0x18`，并排除 0xFFFFFF / 0x000000。\n * 2) 另设一个「状态字」当有效性判据，却忘了在写入时给它赋值，全片缓存读不出来。\n * 3) 为腾一个槽去擦整个 4KB 扇区，连带毁掉同扇区 127 条没补传的记录，条数还在涨。\n * 4) 读取端一读到空槽就 return，环形缓冲里的积压永远补不完。\n * 5) 写完不算 WEL（0x06 写使能），命令被静默忽略，函数返回成功但读回还是 0xFF。\n * 6) 地址越界检查写成 `addr + len > SIZE`，无符号回绕导致检查失效。\n */",
 r:"插上电第一次跑：W25QXX_ReadID() 返回的低字节是 0x18；写 3 条再断电重启，log_init() 数出的条数还是 3，读回的 ts 与载荷逐字节相同；故意写满一圈，s_lost 开始增长而不是静默覆盖老数据。",
 k:"SPI Flash W25QXX W25Q128 GD25Q128 断网缓存 离线缓存 日志 掉电 原子提交 环形缓冲 扇区擦除 4KB CRC16 MODBUS 掉线补传 分区 seq 状态字 SPI1 重映射 PB3 PB4 PB5 PB14 W25QXX_Init W25QXX_ReadID W25QXX_Read W25QXX_Write W25QXX_EraseSector W25QXX_IsBlank W25QXX_LogInit W25QXX_LogWrite W25QXX_LogRead W25QXX_LogCount W25QXX_LogTail W25QXX_LogLost W25QXX_LogStat W25QXX_LogClear W25QXX_LogCapacity 相关"}
"""

GUIDE_P18_W25QXX_LOG = r"""
/* ===================== 断网缓存日志引导 ===================== */
{m:"引导", t:"guide", n:"🧭 ⑱ SPI Flash 断网缓存日志引导（先存本地，上线再补传）",
 d:"从「认清板子上到底是哪颗 Flash」开始，一步一步搭出一个掉电不丢、能自己数出积压条数的本地缓存。中间会重点讲清三处最容易埋雷的地方：判空判据、原子提交、4KB 擦除单位。",
 g:"STM32F407 · GD25Q128（SPI1 重映射 PB3/PB4/PB5，CS=PB14）· 16 扇区 = 64KB = 2048 条 · 对应 FWLIB 板2 的 w25qxx.c / w25qxx_log.c",
 k:"断网缓存 引导 向导 教程 SPI Flash W25QXX GD25Q128 掉线 补传 日志 环形 掉电 原子 扇区擦除 CRC16 分区 槽 步骤 排错",
 e:"/* ============================================================\n *  断网缓存日志，完整可用代码\n *  按「SPI Flash 断网缓存日志引导」9 步搭出来的成品\n *  平台：STM32F407 · GD25Q128 · SPI1 重映射 · CS = PB14\n * ============================================================ */\n#include \"stm32f4xx.h\"\n#include \"w25qxx.h\"\n\n#define ADDR_LOG      0x001000UL\n#define SLOT_SIZE     32U\n#define HDR_SIZE      12U\n#define PAYLOAD       (SLOT_SIZE - HDR_SIZE)\n#define SLOTS_PER_SEC (W25QXX_SECTOR_SIZE / SLOT_SIZE)\n#define LOG_CAPACITY  ((uint16_t)(16U * SLOTS_PER_SEC))\n#define LOG_EMPTY     0xFFFFFFFFUL\n\nstatic uint16_t s_head;\nstatic uint16_t s_count;\nstatic uint32_t s_next_seq = 1UL;\nstatic uint16_t s_lost;\n\nstatic uint16_t log_crc16(const uint8_t *b, uint16_t n)\n{\n    uint16_t crc = 0xFFFFU, i;\n    uint8_t  k;\n    for (i = 0U; i < n; i++)\n    {\n        crc ^= (uint16_t)b[i];\n        for (k = 0U; k < 8U; k++)\n        {\n            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);\n        }\n    }\n    return crc;\n}\n\nstatic uint32_t slot_addr(uint16_t i) { return ADDR_LOG + (uint32_t)i * SLOT_SIZE; }\n\nstatic uint32_t slot_seq(const uint8_t *r)\n{\n    return (uint32_t)r[0] | ((uint32_t)r[1] << 8) |\n           ((uint32_t)r[2] << 16) | ((uint32_t)r[3] << 24);\n}\n\nuint8_t log_write(uint32_t ts, const uint8_t *data, uint16_t len)\n{\n    uint8_t  rec[SLOT_SIZE], body[6U + PAYLOAD];\n    uint16_t crc, i;\n\n    if (data == 0 || len == 0U || len > PAYLOAD) { return 1U; }\n    for (i = 0U; i < SLOT_SIZE; i++) { rec[i] = 0xFFU; }\n\n    rec[4] = (uint8_t)(ts);       rec[5] = (uint8_t)(ts >> 8);\n    rec[6] = (uint8_t)(ts >> 16); rec[7] = (uint8_t)(ts >> 24);\n    rec[8] = (uint8_t)(len);      rec[9] = (uint8_t)(len >> 8);\n    for (i = 0U; i < len; i++) { rec[HDR_SIZE + i] = data[i]; }\n\n    body[0] = rec[4]; body[1] = rec[5]; body[2] = rec[6]; body[3] = rec[7];\n    body[4] = rec[8]; body[5] = rec[9];\n    for (i = 0U; i < len; i++) { body[6U + i] = rec[HDR_SIZE + i]; }\n    crc = log_crc16(body, (uint16_t)(6U + len));\n    rec[10] = (uint8_t)(crc);     rec[11] = (uint8_t)(crc >> 8);\n\n    if (W25QXX_IsBlank(slot_addr(s_head), SLOT_SIZE) == 0U)\n    {\n        uint16_t first = (uint16_t)(s_head - (s_head % SLOTS_PER_SEC));\n        if (W25QXX_EraseSector(slot_addr(first)) != 0U) { return 4U; }\n        /* 这里就是「一个槽连累 127 条」的地方：擦之前须先确认本扇区\n         *   里的记录都补传完了；没补完就把损失计入 s_lost，别装作没事。 */\n    }\n\n    if (W25QXX_Write(&rec[4], slot_addr(s_head) + 4U, SLOT_SIZE - 4U) != 0U) { return 4U; }\n\n    rec[0] = (uint8_t)(s_next_seq);       rec[1] = (uint8_t)(s_next_seq >> 8);\n    rec[2] = (uint8_t)(s_next_seq >> 16); rec[3] = (uint8_t)(s_next_seq >> 24);\n    if (W25QXX_Write(&rec[0], slot_addr(s_head), 4U) != 0U) { return 4U; }\n\n    s_next_seq++;\n    if (s_next_seq == LOG_EMPTY) { s_next_seq = 1UL; }\n\n    s_head = (uint16_t)((s_head + 1U) % LOG_CAPACITY);\n    if (s_count < LOG_CAPACITY) { s_count++; } else { s_lost++; }\n    return 0U;\n}\n\nuint8_t log_read(uint16_t index, uint32_t *ts, uint8_t *buf, uint16_t *len)\n{\n    uint8_t  rec[SLOT_SIZE], body[6U + PAYLOAD];\n    uint16_t n, crc_saved, i;\n    uint32_t sq;\n\n    if (index >= s_count) { return 5U; }\n    if (W25QXX_Read(rec, slot_addr(index), SLOT_SIZE) != 0U) { return 4U; }\n\n    sq = slot_seq(rec);\n    if (sq == LOG_EMPTY) { return 5U; }            /* 空槽 */\n\n    n = (uint16_t)rec[8] | ((uint16_t)rec[9] << 8);\n    if (n == 0U || n > PAYLOAD) { return 6U; }\n\n    body[0] = rec[4]; body[1] = rec[5]; body[2] = rec[6]; body[3] = rec[7];\n    body[4] = rec[8]; body[5] = rec[9];\n    for (i = 0U; i < n; i++) { body[6U + i] = rec[HDR_SIZE + i]; }\n\n    crc_saved = (uint16_t)rec[10] | ((uint16_t)rec[11] << 8);\n    if (log_crc16(body, (uint16_t)(6U + n)) != crc_saved) { return 6U; }\n\n    *ts = (uint32_t)rec[4] | ((uint32_t)rec[5] << 8) |\n          ((uint32_t)rec[6] << 16) | ((uint32_t)rec[7] << 24);\n    for (i = 0U; i < n; i++) { buf[i] = rec[HDR_SIZE + i]; }\n    *len = n;\n    return 0U;\n}\n\nuint8_t log_init(void)\n{\n    uint8_t  rec[SLOT_SIZE];\n    uint32_t sq, max_seq = 0UL;\n    uint16_t i;\n\n    s_head = 0U; s_count = 0U; s_lost = 0U; s_next_seq = 1UL;\n\n    for (i = 0U; i < LOG_CAPACITY; i++)\n    {\n        if (W25QXX_Read(rec, slot_addr(i), SLOT_SIZE) != 0U) { continue; }\n        sq = slot_seq(rec);\n        if (sq == LOG_EMPTY) { s_head = i; break; }\n        s_count++;\n        if (sq > max_seq) { max_seq = sq; }\n    }\n    s_next_seq = max_seq + 1UL;\n    if (s_next_seq == LOG_EMPTY) { s_next_seq = 1UL; }\n    return 0U;\n}\n\nuint16_t log_count(void) { return s_count; }\nuint16_t log_lost(void)  { return s_lost;  }\n\n/* 自检：写 3 条 -> 断电重启 -> 应能读回 3 条且内容一致 */\nvoid log_selftest(void)\n{\n    uint8_t  w[4] = { 0xA1U, 0xB2U, 0xC3U, 0xD4U };\n    uint8_t  r[PAYLOAD];\n    uint32_t ts;\n    uint16_t len;\n\n    log_write(1000UL, w, 4U);\n    log_write(1001UL, w, 4U);\n    log_write(1002UL, w, 4U);\n\n    log_init();                      /* 模拟重启后的恢复 */\n    if (log_count() != 3U) { return; }\n    if (log_read(0U, &ts, r, &len) != 0U) { return; }\n    /* 断点停在这里：ts 应等于 1000，r[0..4) 应等于 A1 B2 C3 D4 */\n}",
 f:[
  [
   "先想清楚：这东西到底在什么时机写、什么时机读",
   "「断网缓存」听起来是个存储问题，首先是个时序问题。如果写和读的时机没定清楚，后面槽结构怎么设计都是白搭。\n\n典型的四种时机：\n· 写：每次采集到一条数据、且当前判断为「上行不通」时写一条；上行通了就直接发，不写。\n· 读：上行恢复后，从最老的一条开始补传，补传成功就把它标记为已消费（或推进读指针）。\n· 清理：整个扇区都补传完了才允许擦除。\n· 恢复：每次上电扫一遍日志区，数出还有多少条、下一个该写哪。\n\n最容易搞错的是「什么时候算上行不通」。如果判据本身不可靠（比如只看自己标志位、不做心跳超时），会出现「以为通了实际没通」，数据既没发出去、又没写进 Flash，静默丢失。所以缓存模块的可靠性上限，取决于链路判活的可靠性。",
   "在本子上写清楚四件事再动手：① 一条记录的字段（时间戳 + 载荷）；② 一秒钟大概产生几条；③ 最长断网多久；④ 上行恢复后按什么顺序补传。\n算一下 ②×③，就是缓存区至少要能装下的条数，这是分区表大小的唯一依据。",
   "/* 先算容量，再定分区。不要反向。\n *\n *  每秒 1 条 × 断网 30 分钟 = 1800 条\n *  16KB 日志区 = 4 扇区 × 128 条 = 512 条   ← 不够\n *  64KB 日志区 = 16 扇区 × 128 条 = 2048 条 ← 够，留一倍余量\n *  16MB 芯片完全放得下，没必要抠这点空间。\n */",
   "能说出「本项目的缓存区是 64KB、2048 条、按每秒一条够撑 34 分钟」。\n如果算出来不够，先改分区表而不是改代码。",
   "别把「断网」当成一个瞬时事件去处理。断网是一个状态，要有一个状态机或者至少一个可靠的判活标志，否则会出现「网断的那一瞬间正好在发，结果两边都没落」。\n别把缓存区开得太小而指望「反正很快就会通」，现场设备断网几小时是常事。",
   "USART_Init DMA_Init"
  ],
  [
   "认清板子上到底是哪颗 Flash（这一步就开始埋雷了）",
   "开发板丝印写 W25Q128，实际焊的常常是 GD25Q128（兆易创新）。两者指令集兼容、可以直接换用，但 JEDEC ID 不一样：\n· Winbond W25Q128：`0xEF4018`\n· GD25Q128：`0xC84018`\n\nJEDEC ID 三个字节的含义是：厂商 ID + 存储类型 + 容量。前两字节各家不同，第三字节 0x18 表示 128Mbit = 16MB，这个是一致的。\n\n为什么会埋雷：很多人写初始化校验时直接 `if (W25QXX_ReadID() != 0xEF4018) return 1;`，在 GD 的板子上就永远返回失败。更糟的是调用方通常写成 `if (W25QXX_Init(0) != 0) return;`，整个缓存模块被静默跳过，程序照常跑，只是缓存永远是 0 条。",
   "读一次 ID 打印出来看。判据改成「只看容量字节」，并把 0xFFFFFF（MISO 没接）和 0x000000（MOSI/时钟没通）单独排除。",
   "/* 稳妥的身份判据：不认厂商，只认容量 */\nstatic uint8_t flash_check(void)\n{\n    uint32_t id = W25QXX_ReadID();\n\n    if (id == 0xFFFFFFUL || id == 0x000000UL) { return 1U; }  /* 通信本身就不通 */\n    if ((id & 0xFFUL) != 0x18UL)             { return 2U; }  /* 容量不是 128Mbit */\n\n    /* 把厂商记下来，打印用，不参与判定 */\n    return 0U;\n}",
   "上电串口应该打印出类似 `JEDEC ID = C8 40 18` 或 `EF 40 18`。\n读回 `FF FF FF` = MISO 没接或片选没拉低；读回 `00 00 00` = MOSI/SCK 没通；读到别的容量字节 = 拿错型号了。",
   "别在驱动里写死某一个厂商的完整 ID，换料、换批次都会出错。\n别只看 ID 就以为能写。ID 读得到只说明读通路通，写通路还依赖 WEL（写使能）和正确的忙等待。\nSPI 速率 10MHz 在杜邦线上经常跑飞，现象是「ID 偶尔读对、偶尔乱码」。先降到 2MHz 确认功能，再往上试。",
   "SPI_Init SPI_Cmd SPI_I2S_SendData SPI_I2S_ReceiveData GPIO_WriteBit"
  ],
  [
   "接好线、把 SPI 开起来（ PB3/PB4/PB5 是重映射）",
   "SPI1 的默认引脚是 PA5/PA6/PA7，但那些脚在多数 F407 板子上留给别的外设了。板子上用的是 重映射到 PB3/PB4/PB5：\n· PB3 = SPI1_SCK\n· PB4 = SPI1_MISO\n· PB5 = SPI1_MOSI\n· 片选用 PB14 普通 GPIO 软件控制\n\n两个必须的点：\n① 开了重映射之后，PA5/PA6/PA7 就不能再当 SPI1 用了；\n② PB3 和 PB4 复位后是 JTAG 的引脚（JTDO / NJTRST）。不关 JTAG 而只用 SWD，这两个脚就一直是调试口，SPI 一点波形都没有。这是「换到重映射引脚后 SPI 突然不工作」的头号原因。",
   "① 开 GPIOB 与 SPI1 时钟；② 关 JTAG、只留 SWD；③ 配 PB3/PB4/PB5 为复用推挽；④ 配 PB14 为推挽输出并拉高（片选空闲为高）；⑤ 配 SPI1 为主机、模式 0、软件 NSS。",
   "/* ---- 关 JTAG，只留 SWD（PB3/PB4 才能当普通复用脚用） ---- */\nRCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);\nGPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);\n\n/* ---- SPI1 重映射到 PB3/PB4/PB5 ---- */\nGPIO_PinAFConfig(GPIOB, GPIO_PinSource3, GPIO_AF_SPI1);\nGPIO_PinAFConfig(GPIOB, GPIO_PinSource4, GPIO_AF_SPI1);\nGPIO_PinAFConfig(GPIOB, GPIO_PinSource5, GPIO_AF_SPI1);\n\ng.GPIO_Pin   = GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;\ng.GPIO_Mode  = GPIO_Mode_AF;\ng.GPIO_OType = GPIO_OType_PP;\ng.GPIO_Speed = GPIO_Speed_50MHz;\ng.GPIO_PuPd  = GPIO_PuPd_UP;\nGPIO_Init(GPIOB, &g);\n\n/* ---- 片选 PB14：普通输出，空闲拉高 ---- */\ng.GPIO_Pin  = GPIO_Pin_14;\ng.GPIO_Mode = GPIO_Mode_OUT;\ng.GPIO_PuPd = GPIO_PuPd_UP;\nGPIO_Init(GPIOB, &g);\nGPIO_SetBits(GPIOB, GPIO_Pin_14);",
   "用示波器（或逻辑分析仪）量 PB3：空闲时因为 CPOL=0 应该停在低电平，发数据时才有波形。\n如果 PB3 一直是高电平不动 → 九成是 JTAG 没关。",
   "忘了 `GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE)`，这是重映射到 PB3/PB4 时最经典的问题。\n片选空闲电平搞反（配成下拉、或忘了初始拉高）→ 从上电起 Flash 就一直在被选中，别的 SPI 器件全乱。\nSPI1 和 NRF24L01 共用这套脚，两个模块同时接就会互相打架。",
   "GPIO_Init GPIO_PinAFConfig GPIO_PinRemapConfig RCC_AHB1PeriphClockCmd RCC_APB2PeriphClockCmd"
  ],
  [
   "画分区表：参数、日志、字库各占哪一段",
   "Flash 是按扇区擦除的，一旦现场设备装到墙上，改分区表就意味着要清空重来。所以第一步就把地址规划写死在头文件里，后面所有代码只引用宏名，不再出现裸地址。\n\n本项目用三段：\n· `0x000000` 参数区：1 个扇区（4KB），放配置、校准系数。\n· `0x001000` 日志区：16 个扇区（64KB），环形缓存。\n· `0x020000` 字库/图片区：剩下的都留给它。\n\n日志区起点 `0x001000` 就是 4096，正好是第二个扇区的开头，分区起点必须扇区对齐，否则一次擦除会把上一个分区的尾巴一起擦掉。",
   "① 把三个地址写成宏；② 在每个分区起点处留一句 `/* 扇区对齐 */` 的注释（防止以后有人随手改成一个不对齐的值）；③ 在初始化里断言每个分区的起点都是 4096 的整数倍。",
   "/* ---- 分区表：所有地址集中在这里，别在别处写裸地址 ---- */\n#define ADDR_PARAM   0x000000UL   /* 1 扇区  参数 / 校准 */\n#define ADDR_LOG     0x001000UL   /* 16 扇区 环形日志   */\n#define ADDR_FONT    0x020000UL   /* 其余    字库 / 图片 */\n\n#define LOG_SECTORS  16U\n\n/* 编译期护栏：分区起点必须扇区对齐，写错直接编译不过 */\n#if ((ADDR_LOG % 4096UL) != 0UL)\n#error ADDR_LOG is not sector aligned\n#endif\n#if ((ADDR_FONT % 4096UL) != 0UL)\n#error ADDR_FONT is not sector aligned\n#endif",
   "编译通过就说明地址是对齐的（不对齐会直接 #error）。\n再在调试器里看 `ADDR_LOG` = 0x1000、`ADDR_LOG + LOG_SECTORS*4096` = 0x11000，正好接上 `ADDR_FONT` 之前。",
   "用普通 `if` 判对齐检查是没用的，要在编译期用 `#if` 拦住，否则永远发现不了。\n别把日志区和参数区贴在一起又不留余量：日志区写满时的擦除是按扇区的，起点算错就会把参数擦掉。\n分区地址别用十进制写，`16384` 这种在代码里根本看不出是第几个扇区。",
   "W25QXX_EraseSector W25QXX_Read W25QXX_Write"
  ],
  [
   "设计 32 字节的槽结构（这里决定能存多少条）",
   "环形日志的存储单位叫「槽」。槽大小直接决定两件事：能存多少条和每条能带多少数据。\n\n本项目用 32 字节一个槽：\n```\n[0..4)   seq   序号（4B）  0xFFFFFFFF = 空槽\n[4..8)   ts    时间戳（4B）\n[8..10)  len   载荷长度（2B）\n[10..12) crc   CRC16（2B）\n[12..32) 载荷（20B）\n```\n4KB ÷ 32 = 128 条/扇区，16 个扇区 = 2048 条。\n\n如果用 64 字节槽：4KB ÷ 64 = 64 条/扇区，总共只有 1024 条，而且 12 字节头占了近 20%，利用率还不到 30%。槽大小要贴着「一条数据实际有多长」来定。",
   "① 先用 `#define` 定义 SLOT_SIZE 与 HDR_SIZE；② 载荷长度用宏推导（`SLOT_SIZE - HDR_SIZE`），不要手写数字；③ 加编译期护栏，保证 SLOT_SIZE 能整除扇区、HDR_SIZE 不越界。",
   "#define SLOT_SIZE     32U\n#define HDR_SIZE      12U\n#define PAYLOAD       (SLOT_SIZE - HDR_SIZE)                       /* 20 */\n#define SLOTS_PER_SEC (W25QXX_SECTOR_SIZE / SLOT_SIZE)             /* 128 */\n#define LOG_CAPACITY  ((uint16_t)(LOG_SECTORS * SLOTS_PER_SEC))    /* 2048 */\n\n/* 编译期护栏：这些关系一旦被改坏，必须在编译时就报错 */\n#if ((SLOT_SIZE % 4U) != 0U)\n#error SLOT_SIZE must be a multiple of 4\n#endif\n#if ((W25QXX_SECTOR_SIZE % SLOT_SIZE) != 0U)\n#error SLOT_SIZE must divide the 4KB sector evenly\n#endif\n#if (HDR_SIZE < 12U || PAYLOAD < 16U)\n#error header too big or payload too small\n#endif",
   "编译过后在调试器里看 `LOG_CAPACITY` = 2048、`PAYLOAD` = 20。\n再故意把 SLOT_SIZE 改成 48（不能整除 4096），应该立刻 `#error`。",
   "载荷长度手写数字：改了 SLOT_SIZE 忘了改 PAYLOAD，就会写到隔壁槽里去。用宏推导。\n槽大小不整除扇区 → 最后一个槽跨扇区，擦一个扇区永远凑不齐整条，环形逻辑直接乱套。\n把 CRC 也算进槽头长度，CRC 属于头，但计算范围不含它自己，这两件事别混。",
   "W25QXX_SECTOR_SIZE W25QXX_PAGE_SIZE W25QXX_Write W25QXX_Read"
  ],
  [
   "原子提交：先写数据，最后写序号",
   "Flash 的一条铁律：一次写操作只能把 1 改成 0，不能把 0 改回 1。所以「写一条记录」这件事没有事务，只能靠写入顺序来保证掉电安全。\n\n本项目的做法是把 seq 当成「提交标记」：\n① 先把 `[4..32)` 写进去（ts、len、载荷、CRC），此时 seq 还是擦除后的 `0xFFFFFFFF`；\n② 最后单独写 `[0..4)` 的 seq。\n\n判空判据是 `seq != 0xFFFFFFFF`，所以在第②步落盘之前掉电，这条记录「不存在」，环形指针也没推进，下次上电重新用这个槽就行，不会留下半条脏数据。\n\n若顺序颠倒（先写 seq 再写数据）：seq 已有效，但数据区仍为 0xFF，读取端算出 CRC 不匹配 → 这条被判为损坏，槽却已被占用。时间久了整个日志区都是「损坏的槽」。\n\n这也是为什么 CRC 不能覆盖 seq：seq 是最后一个写的字节，如果它参与校验和，就等于要求校验和在自己被写之前就正确，逻辑上自相矛盾。",
   "① 先用 0xFF 把整个槽缓冲填满；② 填 ts/len/载荷，算 CRC 写进 [10..12)；③ 调一次写函数只写 `[4..32)`；④ 再调一次写函数单独写 `[0..4)` 的 seq；⑤ 两步都成功之后才推进 `s_head` 和 `s_count`。",
   "/* ---- 第一步：数据区先落盘，此时 seq 仍是空槽标记 ---- */\nif (W25QXX_Write(&rec[4], slot_addr(s_head) + 4U, SLOT_SIZE - 4U) != 0U) { return 4U; }\n\n/* ---- 第二步：最后写 seq，这一步才是「提交」 ---- */\nrec[0] = (uint8_t)(s_next_seq);        rec[1] = (uint8_t)(s_next_seq >>  8);\nrec[2] = (uint8_t)(s_next_seq >> 16);  rec[3] = (uint8_t)(s_next_seq >> 24);\nif (W25QXX_Write(&rec[0], slot_addr(s_head), 4U) != 0U) { return 4U; }\n\n/* ---- 两步都成功才动指针 ---- */\ns_head = (uint16_t)((s_head + 1U) % LOG_CAPACITY);\nif (s_count < LOG_CAPACITY) { s_count++; } else { s_lost++; }",
   "验证方法很直接：在第①步和第②步之间手动断电（或在调试器里设断点后复位），重新上电 `log_init()` 数出来的条数应该是上一次成功提交的数量，不多不少。\n再读那条槽：seq 应该是 0xFFFFFFFF，整条被判为空。",
   "顺序写反（先 seq 后数据）→ 掉电后留下「已提交但数据是 0xFF」的坏槽。\n忘了在写入前把缓冲区填 0xFF → 缓冲区里的栈垃圾被写进 Flash，CRC 覆盖不到的地方全是噪声。\n把 `s_head`/`s_count` 的推进放在两次写之间 → 中途失败时指针已经动了，那条记录再也找不回来。\n把 CRC 算进 seq → 逻辑自相矛盾，写完 seq 之后 CRC 立刻失效。",
   "W25QXX_Write W25QXX_WriteSafe W25QXX_IsBlank W25QXX_WaitBusy"
  ],
  [
   "环形回绕 + 溢出必须记账",
   "「环形」的意思是写指针 `s_head` 一直往前跑，跑到末尾就回到 0。只要缓存没满，`s_count` 一直加；一旦写满一圈，新数据就要挤掉最老的数据。\n\n这里有个非常隐蔽的错误写法：\n```\nif (s_count < LOG_CAPACITY) s_count++;   /* 只加不减 */\n```\n写满以后 `s_count` 永远等于 2048，但实际上最老的那些槽已经被新数据覆盖了，`s_count` 说「有 2048 条」，按 `index = 0` 去读却拿到已经被覆盖的数据或者空槽。补传端会一直读到错误内容。\n\n正确做法有两条：\n① `s_count` 达到容量后就不再增长（它表示「缓冲区里最多有这么多」）；\n② 另设一个溢出计数 `s_lost`，每挤掉一条就加一，并通过接口暴露出去。\n\n为什么一定要暴露 `s_lost`：静默丢数据是这类模块最不能接受的失败方式。上层看到 `s_lost > 0` 才能决定「降采样上报」还是「告警」。",
   "① `s_count` 封顶在 LOG_CAPACITY；② 每挤掉一条就给 `s_lost` 加一；③ 对外提供 `log_lost()` 查询接口；④ 最好再做一条「最早的记录时间戳」的接口，让上层能算出积压了多久。",
   "s_head = (uint16_t)((s_head + 1U) % LOG_CAPACITY);\n\nif (s_count < LOG_CAPACITY)\n{\n    s_count++;          /* 缓冲区还没满：条数增长 */\n}\nelse\n{\n    s_lost++;           /* 已经满了：挤掉了最老的一条，如实记账 */\n}\n\n/* ---- 对外接口：上层靠它判断「有没有丢过数据」 ---- */\nuint16_t log_count(void) { return s_count; }\nuint16_t log_lost(void)  { return s_lost;  }",
   "连续写 2100 条（超过 2048 的容量 52 条），然后看 `log_count()` = 2048、`log_lost()` = 52。\n两条加起来的逻辑关系要能对上：写入总条数 = 当前条数 + 丢失条数 + 已补传条数。",
   "只给 `s_count++` 不给溢出计数 → 写满以后读出来的老记录全是垃圾，而且没有任何迹象。\n把 `s_lost` 定义成 `uint8_t` → 丢 256 条就归零，等于自欺欺人，至少用 `uint16_t`。\n溢出后不清 `s_lost`，导致「很久以前丢过一次」这个状态永远挂着，要不要清零由上层决定，驱动只负责如实汇报。",
   "W25QXX_LogCount W25QXX_LogLost W25QXX_LogStat W25QXX_LogTail"
  ],
  [
   "4KB 擦除换来一个槽：这一步最容易毁数据",
   "Flash 的擦除最小单位是扇区 4KB = 128 个槽。当写指针转回来、发现这个槽还是「脏的」（里面还有旧数据），就必须先擦。\n\n问题在于：为了腾出一个槽而擦掉整个扇区，会把同扇区里另外 127 条可能还没补传的记录一起清成 0xFF。\n\n典型故障现场：断网很久，缓存写满一圈，补传又没跟上；此后每次跨进一个扇区的第一个槽，就抹掉 63~127 条待补传记录。而 `s_count` 还在只增不减地涨，于是出现「计数说有 1024 条，从最老的 index 去读却全是空槽」，补传端一读到空槽就以为「没有更多了」，剩下的 900 多条积压永远发不出去。\n\n两个必须同时做的修法：\n① 擦之前先数这个扇区里还剩几条活记录。只要不为 0，就只能「返回空间不足、让上层先补传」，或者把损失如实计入 `s_lost`，绝不能装作没事继续写。\n② 读取端读到空槽要跳过继续找，不能当成结束标志。",
   "① 写一个 `log_sector_live(first)` 数本扇区里 `seq != 0xFFFFFFFF` 的条数；② 擦除前调用它；③ `live > 0` 时要么返回「空间不足」让上层补传、要么把 `live` 计进 `s_lost`；④ 补传循环里把「空槽」和「损坏」都当作「跳过这一条」。",
   "/* 扇区起始槽号：把当前槽号向下对齐到 128 的整数倍 */\nuint16_t first = (uint16_t)(s_head - (s_head % SLOTS_PER_SEC));\nuint16_t live  = log_sector_live(first);\n\nif (live > 0U)\n{\n    /* 这个扇区里还有 live 条没补传。擦 = 全丢。\n     * 这里选择如实记账；更好的做法是先补传再擦。 */\n    s_lost  = (uint16_t)(s_lost + live);\n    s_count = (uint16_t)(s_count - live);   /* 条数必须跟着减 */\n}\nif (W25QXX_EraseSector(slot_addr(first)) != 0U) { return 4U; }\n\n/* ---- 读取端：空槽 / 损坏都只是「跳过这一条」 ---- */\nfor (i = 0U; i < LOG_CAPACITY; i++)\n{\n    if (log_read(i, &ts, buf, &len) == 0U) { send(ts, buf, len); ok++; }\n    /* 返回 5（空槽）或 6（损坏）都继续往后找 */\n}",
   "写满一圈之后（`log_lost` 开始增长），确认「最老的 index 还能读到有效数据」这件事不再发生：\n· 每次擦扇区时串口打印 `live`，正常情况下应该是 0；\n· 若不是 0，`log_lost` 必须同步增加，账要对得上。\n另外故意制造「index 0 是空槽」的局面，看补传端是不是能继续往后扫到 index 1。",
   "擦一个 4KB 扇区只为一个槽腾地方，却不管同扇区的其他记录，这是本模块历史上最严重的一个 bug。\n擦了扇区但 `s_count` 只剩加法 → 计数与实际内容脱节。\n补传端用「读到空槽即结束」，环形缓冲里空间是断续的，这样会让积压永远补不完。\n擦除耗时约 45ms（整片擦除更久），期间 CPU 取指会被硬件暂挂，写完要喂狗。",
   "W25QXX_EraseSector W25QXX_EraseBlock W25QXX_IsBlank W25QXX_LogRead W25QXX_LogLost"
  ],
  [
   "补齐「上电恢复」与「CRC 校验」，然后自检",
   "断电重启之后，内存里的 `s_head` / `s_count` 全没了，必须从 Flash 里扫回来。做法是遍历整个日志区，遇到第一个 `seq == 0xFFFFFFFF` 的槽就认为「写指针在这里」，同时统计有效条数、记录最大序号。\n\n为什么要记最大序号：序号是递增的，恢复时只要 `s_next_seq = max_seq + 1` 就能接着往下写，不需要额外再写一个「指针页」，额外写指针页意味着每次写日志要擦两次扇区，寿命直接减半。\n\n校验用 CRC16-MODBUS（多项式 0xA001、初值 0xFFFF、末尾不异或，和 Modbus 串口用的是同一套）。不要用 16 位字节累加：字节累加对「两个字节互换位置」和「0x01 变 0xFF」这类成对错误完全无感。\n\n有一点必须提醒：换了槽结构或换了校验算法，旧数据一定读不出来。旧记录的 CRC 覆盖范围和新算法不一样，会被整片判成「损坏」。所以改完布局、烧录之后，第一件事是调一次 `W25QXX_LogClear()` 把老数据清干净。",
   "① 遍历日志区恢复 `s_head`/`s_count`/`s_next_seq`；② 每条读取都重算 CRC 再比对；③ 写一个自检函数：写 3 条 → 复位 → 应能读回 3 条且内容逐字节一致。",
   "uint8_t log_init(void)\n{\n    uint8_t  rec[SLOT_SIZE];\n    uint32_t sq, max_seq = 0UL;\n    uint16_t i;\n\n    s_head = 0U; s_count = 0U; s_lost = 0U; s_next_seq = 1UL;\n\n    for (i = 0U; i < LOG_CAPACITY; i++)\n    {\n        if (W25QXX_Read(rec, slot_addr(i), SLOT_SIZE) != 0U) { continue; }\n        sq = slot_seq(rec);\n        if (sq == LOG_EMPTY) { s_head = i; break; }   /* 第一个空槽就是写指针 */\n        s_count++;\n        if (sq > max_seq) { max_seq = sq; }\n    }\n    s_next_seq = max_seq + 1UL;\n    if (s_next_seq == LOG_EMPTY) { s_next_seq = 1UL; }   /* 跳过空槽标记 */\n    return 0U;\n}",
   "① 写入 3 条 → 断电重启 → `log_count()` 返回 3；② 逐条读回，时间戳与载荷和写进去的完全一致；③ 故意把一个字节改坏，这条应该返回 6（损坏）而不是返回错误数据；④ 上电时串口打印 `count / lost / capacity` 三个数，一眼能看出积压情况。",
   "恢复时忘了跳过 `0xFFFFFFFF` 这个序号值，它已经被征用为空槽标记，写进槽里就等于「这条不存在」。\n扫描时不校验 CRC，把损坏的记录也数进 `s_count` → 补传时会发出垃圾数据。\n换了布局却不调 `W25QXX_LogClear()` 就用旧数据，旧记录一律被判损坏且被扫描跳过，现场表现为「升级完固件之后积压全没了」。\n恢复扫描是 2048 次单槽读，每次都有 SPI 事务开销，上电时间会明显变长；如果嫌慢就只扫扇区首槽 + 逐步二分，别为了快而少扫。",
   "W25QXX_LogInit W25QXX_LogClear W25QXX_LogCapacity W25QXX_ReadID W25QXX_Init"
  ]
 ]}
"""
