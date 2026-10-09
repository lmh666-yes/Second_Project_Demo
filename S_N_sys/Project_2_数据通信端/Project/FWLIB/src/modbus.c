#include "modbus.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"          /* delay_us:顺带确保 DWT 计时使能 */

/* ================================================================
 *  modbus.c —— Modbus-RTU 从机  实现文件
 * ================================================================
 *  三个实现要点 :
 *    ① 收帧不靠定时器：用 DWT 微秒时间戳判断"总线静默 ≥3.5 字符时间"
 *       → 帧结束。所以 MODBUS_Poll() 要在主循环里频繁调用；
 *    ② 寄存器区用"指针 + 数量"绑定，本模块不分配数据（应用层定义数组）；
 *    ③ 响应帧就地拼在发送缓冲里，一次 SYS_RS485_SendBuf 发出去
 *       （sys_rs485 会自己切方向并等 TC）。
 * ================================================================ */


/* ================================================================
 *                      内部状态
 * ================================================================ */
static uint8_t  mb_addr   = MODBUS_DEFAULT_ADDR;
static uint32_t mb_baud   = MODBUS_DEFAULT_BAUD;

/* 绑定的数据区（指针为 0 表示该区未绑定 → 访问它返回异常码 02） */
static uint16_t       *mb_hold  = 0;  static uint16_t mb_hold_n = 0;
static const uint16_t *mb_inreg = 0;  static uint16_t mb_inreg_n = 0;
static uint8_t        *mb_coil  = 0;  static uint16_t mb_coil_n = 0;
static const uint8_t  *mb_disc  = 0;  static uint16_t mb_disc_n = 0;

/* 收/发缓冲 */
static uint8_t  mb_rx[MODBUS_BUF_SIZE];
static uint16_t mb_rx_len = 0;
static uint32_t mb_rx_last_us = 0;        /* 最后一个字节到达的时刻 */

static uint8_t  mb_tx[MODBUS_BUF_SIZE];

/* 统计与回调 */
static uint32_t mb_cnt_rx  = 0;
static uint32_t mb_cnt_tx  = 0;
static uint32_t mb_cnt_err = 0;
static void (*mb_write_cb)(uint8_t func, uint16_t addr, uint16_t value) = 0;

/* 帧间隔（µs）：波特率改变时重算 */
static uint32_t mb_gap_us = 4000;

/* 广播标志：1 = 当前帧是广播帧（地址 0）——按协议只执行、不回响应。
 * 放在发送层统一拦截，这样各功能码处理函数里不用到处写"要不要回"的判断。 */
static uint8_t mb_broadcast = 0;


/* ================================================================
 *                      小工具
 * ================================================================ */
uint16_t MODBUS_CRC16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t  j;

    if (buf == 0) return 0U;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i];
        for (j = 0; j < 8U; j++) {
            if (crc & 0x0001U) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

/* 往发送缓冲追加 CRC（低字节在前）并发送
 * 广播帧直接丢弃（不发），但统计里仍算一帧——方便调试时看到"确实收到过" */
static void mb_send_with_crc(uint16_t len)
{
    uint16_t crc;

    if (len + 2U > MODBUS_BUF_SIZE) return;

    if (mb_broadcast) return;              /* 广播：只执行、不回 */

    crc = MODBUS_CRC16(mb_tx, len);
    mb_tx[len]     = (uint8_t)(crc & 0xFFU);
    mb_tx[len + 1] = (uint8_t)(crc >> 8);

    SYS_RS485_SendBuf(mb_tx, (uint16_t)(len + 2U));
    mb_cnt_tx++;
}

/* 回一个异常响应：功能码 | 0x80 + 异常码 */
static void mb_send_exception(uint8_t func, uint8_t exc)
{
    mb_tx[0] = mb_addr;
    mb_tx[1] = (uint8_t)(func | 0x80U);
    mb_tx[2] = exc;
    mb_send_with_crc(3U);
}

static uint16_t mb_get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
void MODBUS_Init(uint8_t slave_addr, uint32_t baudrate)
{
    mb_addr = (slave_addr == 0U) ? MODBUS_DEFAULT_ADDR : slave_addr;
    mb_baud = (baudrate == 0U) ? MODBUS_DEFAULT_BAUD : baudrate;

    mb_gap_us = MODBUS_GAP_US(mb_baud);
    if (mb_gap_us < 1000U) mb_gap_us = 1000U;     /* 高波特率下给个下限，防抖 */

    /* 物理层：RS485（方向脚 + USART2 中断接收） */
    SYS_RS485_Init(mb_baud);

    mb_rx_len      = 0;
    mb_rx_last_us  = 0;
    mb_cnt_rx = mb_cnt_tx = mb_cnt_err = 0;
}

void MODBUS_BindHolding(uint16_t *regs, uint16_t count)      { mb_hold  = regs; mb_hold_n  = (regs ? count : 0U); }
void MODBUS_BindInput(const uint16_t *regs, uint16_t count)  { mb_inreg = regs; mb_inreg_n = (regs ? count : 0U); }
void MODBUS_BindCoils(uint8_t *coils, uint16_t count)        { mb_coil  = coils; mb_coil_n = (coils ? count : 0U); }
void MODBUS_BindDiscrete(const uint8_t *inputs, uint16_t cnt) { mb_disc = inputs; mb_disc_n = (inputs ? cnt : 0U); }

void MODBUS_SetWriteCallback(void (*callback)(uint8_t, uint16_t, uint16_t))
{
    mb_write_cb = callback;
}

uint32_t MODBUS_GetRxFrames(void)  { return mb_cnt_rx; }
uint32_t MODBUS_GetTxFrames(void)  { return mb_cnt_tx; }
uint32_t MODBUS_GetErrFrames(void) { return mb_cnt_err; }
void     MODBUS_ResetCounters(void) { mb_cnt_rx = mb_cnt_tx = mb_cnt_err = 0; }
uint8_t  MODBUS_GetAddr(void)      { return mb_addr; }
void     MODBUS_SetAddr(uint8_t addr) { if (addr >= 1U && addr <= 247U) mb_addr = addr; }


/* ================================================================
 *                    功能码处理
 * ================================================================ */
/* 读位区（功能码 01 线圈 / 02 离散输入）
 * 请求：addr,func,startHi,startLo,qtyHi,qtyLo,crcLo,crcHi   （8 字节）
 * 响应：addr,func,byteCount,data...,crcLo,crcHi */
static void mb_read_bits(uint8_t func, const uint8_t *req, uint16_t reqlen)
{
    const uint8_t *src;
    uint16_t src_n;
    uint16_t start;
    uint16_t qty;
    uint16_t bytes;
    uint16_t i;
    uint8_t  bit;

    if (reqlen != 8U) { mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE); return; }

    start = mb_get_u16(&req[2]);
    qty   = mb_get_u16(&req[4]);

    /* 数量合法性：1 ~ 2000 */
    if (qty < 1U || qty > MODBUS_MAX_READ_BITS) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE);
        return;
    }

    if (func == 0x01U) { src = mb_coil; src_n = mb_coil_n; }
    else               { src = mb_disc; src_n = mb_disc_n; }

    /* 地址越界（未绑定也算越界） */
    if (src == 0 || (uint32_t)start + qty > src_n) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_ADDR);
        return;
    }

    bytes = (uint16_t)((qty + 7U) / 8U);
    if ((uint16_t)(3U + bytes + 2U) > MODBUS_BUF_SIZE) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE);
        return;
    }

    mb_tx[0] = mb_addr;
    mb_tx[1] = func;
    mb_tx[2] = (uint8_t)bytes;

    /* 位打包：每个字节装 8 个位，**低位在先**（Modbus 规定） */
    for (i = 0; i < bytes; i++) mb_tx[3U + i] = 0x00U;

    for (i = 0; i < qty; i++) {
        bit = (src[start + i] != 0U) ? 1U : 0U;
        if (bit) mb_tx[3U + (i >> 3)] |= (uint8_t)(1U << (i & 0x07U));
    }

    mb_send_with_crc((uint16_t)(3U + bytes));
}

/* 读寄存器区（功能码 03 保持 / 04 输入）
 * 请求：addr,func,startHi,startLo,qtyHi,qtyLo,crcLo,crcHi
 * 响应：addr,func,byteCount(=qty*2),data...,crcLo,crcHi */
static void mb_read_regs(uint8_t func, const uint8_t *req, uint16_t reqlen)
{
    const uint16_t *src;
    uint16_t src_n;
    uint16_t start;
    uint16_t qty;
    uint16_t i;
    uint16_t v;

    if (reqlen != 8U) { mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE); return; }

    start = mb_get_u16(&req[2]);
    qty   = mb_get_u16(&req[4]);

    if (qty < 1U || qty > MODBUS_MAX_READ_REGS) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE);
        return;
    }

    if (func == 0x03U) { src = mb_hold;   src_n = mb_hold_n; }
    else               { src = mb_inreg;  src_n = mb_inreg_n; }

    if (src == 0 || (uint32_t)start + qty > src_n) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_ADDR);
        return;
    }

    if ((uint16_t)(3U + qty * 2U + 2U) > MODBUS_BUF_SIZE) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE);
        return;
    }

    mb_tx[0] = mb_addr;
    mb_tx[1] = func;
    mb_tx[2] = (uint8_t)(qty * 2U);

    for (i = 0; i < qty; i++) {
        v = src[start + i];
        mb_tx[3U + i * 2U]      = (uint8_t)(v >> 8);    /* 大端：高字节在前 */
        mb_tx[3U + i * 2U + 1U] = (uint8_t)(v & 0xFFU);
    }

    mb_send_with_crc((uint16_t)(3U + qty * 2U));
}

/* 写单个线圈（05）：value 0xFF00 = 置位，0x0000 = 复位；响应 = 原样回显 */
static void mb_write_coil(uint8_t func, const uint8_t *req, uint16_t reqlen)
{
    uint16_t addr;
    uint16_t val;

    if (reqlen != 8U) { mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE); return; }

    addr = mb_get_u16(&req[2]);
    val  = mb_get_u16(&req[4]);

    if (val != 0x0000U && val != 0xFF00U) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE);
        return;
    }
    if (mb_coil == 0 || addr >= mb_coil_n) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_ADDR);
        return;
    }

    mb_coil[addr] = (val == 0xFF00U) ? 1U : 0U;

    if (mb_write_cb != 0) mb_write_cb(func, addr, mb_coil[addr]);

    /* 回显请求（含 CRC 重新计算，等价于原帧） */
    mb_tx[0] = mb_addr; mb_tx[1] = func;
    mb_tx[2] = req[2];  mb_tx[3] = req[3];
    mb_tx[4] = req[4];  mb_tx[5] = req[5];
    mb_send_with_crc(6U);
}

/* 写单个保持寄存器（06）：响应 = 原样回显 */
static void mb_write_reg(uint8_t func, const uint8_t *req, uint16_t reqlen)
{
    uint16_t addr;
    uint16_t val;

    if (reqlen != 8U) { mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE); return; }

    addr = mb_get_u16(&req[2]);
    val  = mb_get_u16(&req[4]);

    if (mb_hold == 0 || addr >= mb_hold_n) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_ADDR);
        return;
    }

    mb_hold[addr] = val;
    if (mb_write_cb != 0) mb_write_cb(func, addr, val);

    mb_tx[0] = mb_addr; mb_tx[1] = func;
    mb_tx[2] = req[2];  mb_tx[3] = req[3];
    mb_tx[4] = req[4];  mb_tx[5] = req[5];
    mb_send_with_crc(6U);
}

/* 写多个线圈（0F）
 * 请求：addr,func,startHi,startLo,qtyHi,qtyLo,byteCount,data...,crc
 * 响应：addr,func,startHi,startLo,qtyHi,qtyLo,crc */
static void mb_write_coils(uint8_t func, const uint8_t *req, uint16_t reqlen)
{
    uint16_t start;
    uint16_t qty;
    uint8_t  bytes;
    uint16_t i;

    if (reqlen < 9U) { mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE); return; }

    start = mb_get_u16(&req[2]);
    qty   = mb_get_u16(&req[4]);
    bytes = req[6];

    if (qty < 1U || qty > MODBUS_MAX_WRITE_BITS) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE);
        return;
    }
    if (bytes != (uint8_t)((qty + 7U) / 8U)) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE);
        return;
    }
    if ((uint16_t)(7U + bytes + 2U) != reqlen) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE);
        return;
    }
    if (mb_coil == 0 || (uint32_t)start + qty > mb_coil_n) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_ADDR);
        return;
    }

    for (i = 0; i < qty; i++) {
        uint8_t b = req[7U + (i >> 3)];
        mb_coil[start + i] = ((b >> (i & 0x07U)) & 0x01U) ? 1U : 0U;
    }
    if (mb_write_cb != 0) mb_write_cb(func, start, mb_coil[start]);

    mb_tx[0] = mb_addr; mb_tx[1] = func;
    mb_tx[2] = req[2];  mb_tx[3] = req[3];
    mb_tx[4] = req[4];  mb_tx[5] = req[5];
    mb_send_with_crc(6U);
}

/* 写多个保持寄存器（10）
 * 请求：addr,func,startHi,startLo,qtyHi,qtyLo,byteCount,data...,crc */
static void mb_write_regs(uint8_t func, const uint8_t *req, uint16_t reqlen)
{
    uint16_t start;
    uint16_t qty;
    uint8_t  bytes;
    uint16_t i;

    if (reqlen < 9U) { mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE); return; }

    start = mb_get_u16(&req[2]);
    qty   = mb_get_u16(&req[4]);
    bytes = req[6];

    if (qty < 1U || qty > MODBUS_MAX_WRITE_REGS) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE);
        return;
    }
    if (bytes != (uint8_t)(qty * 2U)) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE);
        return;
    }
    if ((uint16_t)(7U + bytes + 2U) != reqlen) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_VALUE);
        return;
    }
    if (mb_hold == 0 || (uint32_t)start + qty > mb_hold_n) {
        mb_send_exception(func, MODBUS_EXC_ILLEGAL_ADDR);
        return;
    }

    for (i = 0; i < qty; i++) {
        mb_hold[start + i] = mb_get_u16(&req[7U + i * 2U]);
    }
    if (mb_write_cb != 0) mb_write_cb(func, start, mb_hold[start]);

    mb_tx[0] = mb_addr; mb_tx[1] = func;
    mb_tx[2] = req[2];  mb_tx[3] = req[3];
    mb_tx[4] = req[4];  mb_tx[5] = req[5];
    mb_send_with_crc(6U);
}


/* ================================================================
 *                    帧处理与轮询
 * ================================================================ */
/* 处理一帧完整报文 */
static void mb_handle_frame(const uint8_t *f, uint16_t len)
{
    uint16_t crc_calc;
    uint16_t crc_recv;
    uint8_t  func;
    uint8_t  addr;

    if (len < 4U) { mb_cnt_err++; return; }           /* 最短合法帧 4 字节 */

    /* ① CRC 校验：范围 = 除最后两字节外的全部 */
    crc_calc = MODBUS_CRC16(f, (uint16_t)(len - 2U));
    crc_recv = (uint16_t)((uint16_t)f[len - 1U] << 8 | (uint16_t)f[len - 2U]);
    if (crc_calc != crc_recv) { mb_cnt_err++; return; }

    addr = f[0];
    func = f[1];

    /* ② 地址过滤：0 = 广播（执行但不回）；不是本机地址 → 静默丢弃 */
    if (addr != mb_addr && addr != 0U) { mb_cnt_err++; return; }

    switch (func) {
        case 0x01U:
        case 0x02U: mb_read_bits(func, f, len);        break;
        case 0x03U:
        case 0x04U: mb_read_regs(func, f, len);        break;
        case 0x05U: mb_write_coil(func, f, len);       break;
        case 0x06U: mb_write_reg(func, f, len);        break;
        case 0x0FU: mb_write_coils(func, f, len);      break;
        case 0x10U: mb_write_regs(func, f, len);       break;
        default:    mb_send_exception(func, MODBUS_EXC_ILLEGAL_FUNC); break;
    }
}

void MODBUS_Poll(void)
{
    /* ① 把串口缓冲里的字节搬进帧缓冲（一帧最多 MODBUS_BUF_SIZE-1） */
    while (SYS_RS485_Available() != 0U) {
        int c = SYS_RS485_ReadByte();
        if (c < 0) break;

        if (mb_rx_len < (uint16_t)(MODBUS_BUF_SIZE - 1U)) {
            mb_rx[mb_rx_len++] = (uint8_t)c;
        } else {
            mb_rx_len = 0;                 /* 撑爆了：丢帧重来 */
        }
        mb_rx_last_us = DWT_GetUs();
    }

    /* ② 静默超过 3.5 字符时间 → 本帧结束，交给协议层处理 */
    if (mb_rx_len >= 4U) {
        if (DWT_ElapsedUs(mb_rx_last_us) >= mb_gap_us) {
            uint16_t n = mb_rx_len;

            mb_rx_len = 0;

            /* 长度上限保护 */
            if (n > MODBUS_MAX_FRAME) { mb_cnt_err++; return; }

            mb_cnt_rx++;
            mb_broadcast = (mb_rx[0] == 0U) ? 1U : 0U;   /* 广播：只执行不回 */
            mb_handle_frame(mb_rx, n);
            mb_broadcast = 0;
        }
    }
}

void MODBUS_ReportHolding(uint16_t start, uint16_t count)
{
    if (mb_hold == 0) return;
    if (count == 0U || count > MODBUS_MAX_READ_REGS) return;
    if ((uint32_t)start + count > mb_hold_n) return;

    /* 主动上报按"03 响应"格式发（不含请求头）——上位机按响应帧解析即可 */
    mb_broadcast = 0;

    mb_tx[0] = mb_addr;
    mb_tx[1] = 0x03U;
    mb_tx[2] = (uint8_t)(count * 2U);
    for (uint16_t i = 0; i < count; i++) {
        uint16_t v = mb_hold[start + i];
        mb_tx[3U + i * 2U]      = (uint8_t)(v >> 8);
        mb_tx[3U + i * 2U + 1U] = (uint8_t)(v & 0xFFU);
    }
    mb_send_with_crc((uint16_t)(3U + count * 2U));
}
