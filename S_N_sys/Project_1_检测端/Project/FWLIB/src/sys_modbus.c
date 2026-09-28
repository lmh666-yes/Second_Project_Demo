#include "sys_modbus.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */

#include "gpio_core.h"      /* Delay_us:顺带确保 DWT 计时使能 */

/* ================================================================
 *  sys_modbus.c —— 【系统】Modbus-RTU 从机协议模块  实现文件
 * ================================================================
 *  帧格式(RTU): [从机地址 1B][功能码 1B][数据 nB][CRC 低 1B][CRC 高 1B]
 *  通信流程 :
 *    ① Poll 把串口环形缓冲的字节搬进帧缓冲(边搬边记"最后字节时刻");
 *    ② 静默超过 3.5 字符时间 → 判定一帧结束(Modbus 判帧规则);
 *    ③ 地址过滤 → CRC 校验 → 功能码分发 → 组应答(CRC 由库补上);
 *    ④ 应答经"发送钩子"(RS485 场景)或直发串口。
 *
 *  实现要点 :
 *    ① 全程非阻塞:发帧、收帧、判帧间隔都不卡主循环;
 *    ② CRC 校验技巧:对"含 CRC 的完整帧"算一遍,结果为 0 即通过;
 *    ③ 异常应答:功能码 | 0x80 + 异常码(01 功能非法/02 地址非法/03 数据非法);
 *    ④ 统计计数(成功/出错)可用 SYS_MODBUS_Counters 随时查——联调神器
 * ================================================================ */


/* ================================================================
 *                    内部状态
 * ================================================================ */
typedef struct {
    SysUsartId_t      uart;
    uint8_t           addr;         /* 本机从机地址 */
    uint32_t          t35_cycles;   /* 3.5 字符时间的 CPU 周期数 */
    uint16_t         *hold;         /* 保持寄存器(0x03/0x06/0x10) */
    uint16_t          hold_n;
    uint16_t         *input;        /* 输入寄存器(0x04) */
    uint16_t          input_n;
    SYS_MODBUS_TxFn_t tx;           /* 应答发送钩子(可空) */
    uint16_t          ok_cnt;
    uint16_t          err_cnt;
} ModbusCtx_t;

static ModbusCtx_t mb;
static uint8_t     mb_inited;                       /* 1 = 已初始化 */
static uint8_t     mb_rx[SYS_MODBUS_BUF_SIZE];      /* 接收组帧缓冲 */
static uint16_t    mb_rx_len;
static uint32_t    mb_last_cycles;                  /* 最后收到字节的时刻 */
static uint8_t     mb_resp[SYS_MODBUS_BUF_SIZE];    /* 应答组帧缓冲 */

/* 功能码 */
#define MB_FC_READ_HOLD     0x03U
#define MB_FC_READ_INPUT    0x04U
#define MB_FC_WRITE_ONE     0x06U
#define MB_FC_WRITE_MANY    0x10U

/* 异常码 */
#define MB_EX_ILLEGAL_FUNC  0x01U   /* 功能码不支持 */
#define MB_EX_ILLEGAL_ADDR  0x02U   /* 寄存器地址/范围非法 */
#define MB_EX_ILLEGAL_VALUE 0x03U   /* 数据值/数量非法 */


/* ================================================================
 *                    内部辅助
 * ================================================================ */
/* 从帧缓冲取大端 16 位 */
static uint16_t mb_rx_u16(uint16_t idx)
{
    return (uint16_t)(((uint16_t)mb_rx[idx] << 8) | mb_rx[idx + 1U]);
}

/* 原始发送:有钩子走钩子,否则直发串口 */
static void mb_send_raw(const uint8_t *buf, uint16_t len)
{
    if (mb.tx != 0) mb.tx(buf, len);
    else            SYS_USART_SendBuf(mb.uart, buf, len);
}

/* 发送应答:自动补 CRC(低字节在前) */
static void mb_send_with_crc(uint16_t len)
{
    uint16_t crc = SYS_MODBUS_Crc16(mb_resp, len);

    mb_resp[len]        = (uint8_t)(crc & 0xFFU);
    mb_resp[len + 1U]   = (uint8_t)(crc >> 8);
    mb_send_raw(mb_resp, (uint16_t)(len + 2U));
}

/* 异常应答:功能码最高位置 1 + 异常码 */
static void mb_exception(uint8_t fc, uint8_t code)
{
    mb_resp[0] = mb.addr;
    mb_resp[1] = (uint8_t)(fc | 0x80U);
    mb_resp[2] = code;
    mb_send_with_crc(3U);
}

/* 处理 0x03/0x04:读寄存器(regs 为数组、n 为个数) */
static void mb_read_regs(uint8_t fc, const uint16_t *regs, uint16_t n)
{
    uint16_t start = mb_rx_u16(2);
    uint16_t qty   = mb_rx_u16(4);
    uint16_t i;

    if (qty == 0U || qty > 125U) { mb_exception(fc, MB_EX_ILLEGAL_VALUE); return; }
    if (regs == 0 || start >= n || ((uint32_t)start + qty) > n) {
        mb_exception(fc, MB_EX_ILLEGAL_ADDR);
        return;
    }

    mb_resp[0] = mb.addr;
    mb_resp[1] = fc;
    mb_resp[2] = (uint8_t)(qty * 2U);               /* 字节数 */
    for (i = 0U; i < qty; i++) {
        uint16_t v = regs[start + i];
        mb_resp[3U + 2U * i] = (uint8_t)(v >> 8);
        mb_resp[4U + 2U * i] = (uint8_t)(v & 0xFFU);
    }
    mb_send_with_crc((uint16_t)(3U + 2U * qty));
}

/* 处理 0x06:写单个保持寄存器(应答 = 原样回显请求前 6 字节) */
static void mb_write_single(void)
{
    uint16_t start = mb_rx_u16(2);
    uint16_t value = mb_rx_u16(4);

    if (mb.hold == 0 || start >= mb.hold_n) {
        mb_exception(MB_FC_WRITE_ONE, MB_EX_ILLEGAL_ADDR);
        return;
    }

    mb.hold[start] = value;

    mb_resp[0] = mb_rx[0];  mb_resp[1] = mb_rx[1];
    mb_resp[2] = mb_rx[2];  mb_resp[3] = mb_rx[3];
    mb_resp[4] = mb_rx[4];  mb_resp[5] = mb_rx[5];
    mb_send_with_crc(6U);
}

/* 处理 0x10:写多个保持寄存器(应答 = 地址+功能+起始+数量 + CRC) */
static void mb_write_multi(uint16_t len)
{
    uint16_t start = mb_rx_u16(2);
    uint16_t qty   = mb_rx_u16(4);
    uint8_t  bytes = mb_rx[6];
    uint16_t i;

    /* 帧长必须 = 9 + 字节数(地址/功能/起始2/数量2/字节数1 + 数据 + CRC2) */
    if ((uint16_t)(9U + bytes) != len) { mb.err_cnt++; return; }

    if (qty == 0U || qty > 123U) {
        mb_exception(MB_FC_WRITE_MANY, MB_EX_ILLEGAL_VALUE);
        return;
    }
    if (bytes != (uint8_t)(qty * 2U)) {
        mb_exception(MB_FC_WRITE_MANY, MB_EX_ILLEGAL_VALUE);
        return;
    }
    if (mb.hold == 0 || start >= mb.hold_n || ((uint32_t)start + qty) > mb.hold_n) {
        mb_exception(MB_FC_WRITE_MANY, MB_EX_ILLEGAL_ADDR);
        return;
    }

    for (i = 0U; i < qty; i++) {
        mb.hold[start + i] = (uint16_t)(((uint16_t)mb_rx[7U + 2U * i] << 8) |
                                        mb_rx[8U + 2U * i]);
    }

    mb_resp[0] = mb_rx[0];  mb_resp[1] = mb_rx[1];
    mb_resp[2] = mb_rx[2];  mb_resp[3] = mb_rx[3];
    mb_resp[4] = mb_rx[4];  mb_resp[5] = mb_rx[5];
    mb_send_with_crc(6U);
}

/* 处理"一帧收完"的数据 */
static void mb_process(uint16_t len)
{
    uint8_t addr;
    uint8_t fc;

    if (len < 4U) { mb.err_cnt++; return; }         /* 最短 = 地址+功能+CRC2 */

    addr = mb_rx[0];
    if (addr != mb.addr) return;                    /* 不是问本机(含广播 0):不理 */

    if (SYS_MODBUS_Crc16(mb_rx, len) != 0U) {       /* 含 CRC 整帧结果为 0 即通过 */
        mb.err_cnt++;
        return;
    }

    fc = mb_rx[1];
    switch (fc) {
        case MB_FC_READ_HOLD:
        case MB_FC_READ_INPUT:
            if (len != 8U) { mb.err_cnt++; return; }        /* 读请求固定 8 字节 */
            if (fc == MB_FC_READ_HOLD) mb_read_regs(fc, mb.hold, mb.hold_n);
            else                       mb_read_regs(fc, mb.input, mb.input_n);
            break;

        case MB_FC_WRITE_ONE:
            if (len != 8U) { mb.err_cnt++; return; }
            mb_write_single();
            break;

        case MB_FC_WRITE_MANY:
            if (len < 11U) { mb.err_cnt++; return; }        /* 最少:写 1 个(9+2) */
            mb_write_multi(len);
            break;

        default:
            mb_exception(fc, MB_EX_ILLEGAL_FUNC);
            break;
    }
    mb.ok_cnt++;
}


/* ================================================================
 *                    基础功能
 * ================================================================ */
uint16_t SYS_MODBUS_Crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t  b;

    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)buf[i];
        for (b = 0U; b < 8U; b++) {
            if (crc & 0x0001U) crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            else               crc = (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

void SYS_MODBUS_Init(SysUsartId_t uart, uint8_t addr, uint32_t baud,
                     uint16_t *hold, uint16_t hold_n,
                     uint16_t *input, uint16_t input_n)
{
    uint32_t t35_us;

    if (uart >= SYS_USART_COUNT || addr == 0U) return;   /* 地址 0 = 广播,不支持 */

    mb.uart   = uart;
    mb.addr   = addr;
    mb.hold   = hold;   mb.hold_n  = hold_n;
    mb.input  = input;  mb.input_n = input_n;
    mb.tx     = 0;
    mb.ok_cnt = 0U;
    mb.err_cnt = 0U;

    /* 3.5 字符时间(按每字符 11 位): us = 38500000 / 波特率;
     * 波特率 > 19200 时按规范固定 1750us */
    if (baud == 0U) baud = 9600U;
    t35_us = (baud > 19200U) ? 1750U : (38500000UL / baud);

    Delay_us(1);                                    /* 确保 DWT 计时已使能 */
    mb.t35_cycles = t35_us * (SystemCoreClock / 1000000U);

    mb_rx_len = 0U;
    mb_last_cycles = DWT->CYCCNT;
    mb_inited = 1U;
}

uint8_t SYS_MODBUS_Poll(void)
{
    int c;

    if (mb_inited == 0U) return 0U;

    /* ① 把串口环形缓冲里的字节搬进帧缓冲(一次搬空,边搬边记时刻) */
    while ((c = SYS_USART_RxRead(mb.uart)) >= 0) {
        if (mb_rx_len < SYS_MODBUS_BUF_SIZE) {
            mb_rx[mb_rx_len] = (uint8_t)c;
            mb_rx_len++;
        } else {
            mb_rx_len = 0U;             /* 超长帧:整帧丢弃重新收 */
            mb.err_cnt++;
        }
        mb_last_cycles = DWT->CYCCNT;
    }

    /* ② 静默超过 3.5 字符 → 一帧结束,处理并应答 */
    if ((mb_rx_len != 0U) &&
        ((uint32_t)(DWT->CYCCNT - mb_last_cycles) > mb.t35_cycles)) {
        uint16_t len = mb_rx_len;
        mb_rx_len = 0U;
        mb_process(len);
        return 1U;
    }
    return 0U;
}


/* ================================================================
 *                    扩展功能
 * ================================================================ */
void SYS_MODBUS_SetTxHook(SYS_MODBUS_TxFn_t fn)
{
    mb.tx = fn;
}

void SYS_MODBUS_Counters(uint16_t *ok, uint16_t *err)
{
    if (ok)  *ok  = mb.ok_cnt;
    if (err) *err = mb.err_cnt;
}
