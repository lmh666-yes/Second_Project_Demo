#include "sys_frame.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */

/* ================================================================
 *  sys_frame.c —— 【系统】串口自定义帧协议模块  实现文件
 * ================================================================
 *  收帧状态机（"帧同步"是怎么做的）:
 *
 *    静止 ──0xAA──▶ 收命令 ──▶ 收长度 ──▶ 收数据×n ──▶ 收校验 ──▶ 收帧尾
 *    WAIT_HEAD       CMD        LEN        DATA*n       CHECK      TAIL
 *      ▲                                                        │
 *      └──────── 任一步失败:计错 + 回到 WAIT_HEAD 重新找帧头 ◀──┘
 *              （若失败的那一字节恰好是 0xAA,直接当作新帧头,
 *                不错过紧挨着到来的下一帧——这就是"帧同步"）
 *
 *  校验 = 帧内除"校验位/帧尾"外所有字节的逐字节异或（含帧头）;
 *  收到完整帧后拷入"就绪槽"（只留最新一帧,取走即清）。
 * ================================================================ */


/* ================================================================
 *                    内部状态（收发各一套）
 * ================================================================ */
/* 状态机取值（简化模式下没有 LEN/DATA 两态） */
#define FX_ST_HEAD      0U
#define FX_ST_CMD       1U
#if SYS_FRAME_WITH_LEN
#define FX_ST_LEN       2U
#define FX_ST_DATA      3U
#define FX_ST_CHECK     4U
#define FX_ST_TAIL      5U
#else
#define FX_ST_CHECK     2U
#define FX_ST_TAIL      3U
#endif

/* 编译期护栏:通用帧负载上限须能塞进 1 字节长度字段 */
typedef char fx_max_payload_check[(SYS_FRAME_MAX_PAYLOAD <= 255U) ? 1 : -1];

/* —— 收帧进行中 —— */
static uint8_t  fx_state = FX_ST_HEAD;                  /* 当前状态 */
static uint8_t  fx_cmd;                                 /* 已收到的命令字 */
static uint8_t  fx_chk;                                 /* 运行中的异或值 */
static uint8_t  fx_idx;                                 /* 数据接收下标 */
static uint8_t  fx_dlen;                                /* 本帧数据长度 */
#if SYS_FRAME_WITH_LEN
static uint8_t  fx_data[SYS_FRAME_MAX_PAYLOAD];         /* 正在收的数据区 */
#endif

/* —— 已就绪帧（最新一帧;取走即清） —— */
static uint8_t  fx_ready;                               /* 1 = 有帧可取 */
static uint8_t  fx_rcmd;                                /* 就绪帧的命令字 */
static uint16_t fx_rlen;                                /* 就绪帧的数据长度 */
static uint8_t  fx_rbuf[SYS_FRAME_MAX_PAYLOAD];         /* 就绪帧的数据区 */

/* —— 统计 —— */
static uint16_t fx_err;                                 /* 出错计数 */


/* ================================================================
 *                    内部辅助
 * ================================================================ */
/* 出错后重启状态机:失败字节恰是帧头就直接当新帧开始（帧同步技巧） */
static void fx_restart(uint8_t byte)
{
    if (byte == (uint8_t)SYS_FRAME_HEAD) {
        fx_chk   = byte;
        fx_state = FX_ST_CMD;
    } else {
        fx_state = FX_ST_HEAD;
    }
}


/* ================================================================
 *                    基础功能
 * ================================================================ */
void SYS_FRAME_Send(SysUsartId_t uart, uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    uint16_t i;
    uint8_t  chk;

    if (uart >= SYS_USART_COUNT) return;

#if SYS_FRAME_WITH_LEN
    /* 通用帧: HEAD + CMD + LEN + 数据 + 校验 + TAIL */
    if (len > SYS_FRAME_MAX_PAYLOAD) return;
    if ((payload == 0) && (len != 0U)) return;

    chk = (uint8_t)(SYS_FRAME_HEAD ^ cmd ^ (uint8_t)len);
    for (i = 0U; i < len; i++) chk ^= payload[i];

    SYS_USART_SendByte(uart, (uint8_t)SYS_FRAME_HEAD);
    SYS_USART_SendByte(uart, cmd);
    SYS_USART_SendByte(uart, (uint8_t)len);
    for (i = 0U; i < len; i++) SYS_USART_SendByte(uart, payload[i]);
    SYS_USART_SendByte(uart, chk);
    SYS_USART_SendByte(uart, (uint8_t)SYS_FRAME_TAIL);
#else
    /* 简化帧: HEAD + 数据(即 cmd) + 校验 + TAIL（与教材四字节帧一致） */
    (void)payload;
    (void)len;

    chk = (uint8_t)(SYS_FRAME_HEAD ^ cmd);

    SYS_USART_SendByte(uart, (uint8_t)SYS_FRAME_HEAD);
    SYS_USART_SendByte(uart, cmd);
    SYS_USART_SendByte(uart, chk);
    SYS_USART_SendByte(uart, (uint8_t)SYS_FRAME_TAIL);
#endif
}

void SYS_FRAME_SendShort(SysUsartId_t uart, uint8_t data)
{
    SYS_FRAME_Send(uart, data, 0, 0U);
}

uint8_t SYS_FRAME_Poll(SysUsartId_t uart)
{
    uint8_t n = 0U;
    int     c;

    if (uart >= SYS_USART_COUNT) return 0U;

    /* 把环形缓冲里的字节一次喂完（每喂一字节都可能凑成整帧） */
    while ((c = SYS_USART_RxRead(uart)) >= 0) {
        if (SYS_FRAME_Feed((uint8_t)c) != 0U) n++;
    }
    return n;
}

uint8_t SYS_FRAME_Available(void)
{
    return fx_ready;
}

uint8_t SYS_FRAME_Get(uint8_t *cmd, uint8_t *payload, uint16_t *len)
{
    uint16_t i;

    if (fx_ready == 0U) return 1U;                      /* 没有帧可取 */

    if (cmd) *cmd = fx_rcmd;
    if (len) *len = fx_rlen;
    if (payload) {
        for (i = 0U; i < fx_rlen; i++) payload[i] = fx_rbuf[i];
    }

    fx_ready = 0U;                                      /* 取走即清 */
    return 0U;
}


/* ================================================================
 *                    扩展功能
 * ================================================================ */
uint8_t SYS_FRAME_Feed(uint8_t byte)
{
    switch (fx_state) {
    case FX_ST_HEAD:
        /* 找帧头:不是 0xAA 就一直丢（这就是"重新同步"的静止态） */
        if (byte == (uint8_t)SYS_FRAME_HEAD) {
            fx_chk   = byte;                            /* 校验含帧头 */
            fx_state = FX_ST_CMD;
        }
        return 0U;

    case FX_ST_CMD:
        fx_cmd  = byte;
        fx_chk ^= byte;
#if SYS_FRAME_WITH_LEN
        fx_state = FX_ST_LEN;
#else
        fx_state = FX_ST_CHECK;                         /* 简化帧:本字节就是数据 */
#endif
        return 0U;

#if SYS_FRAME_WITH_LEN
    case FX_ST_LEN:
        if (byte > (uint8_t)SYS_FRAME_MAX_PAYLOAD) {    /* 长度超限:丢帧重找 */
            fx_err++;
            fx_restart(byte);
            return 0U;
        }
        fx_dlen  = byte;
        fx_chk  ^= byte;
        fx_idx   = 0U;
        fx_state = (byte == 0U) ? FX_ST_CHECK : FX_ST_DATA;
        return 0U;

    case FX_ST_DATA:
        fx_data[fx_idx] = byte;
        fx_idx++;
        fx_chk ^= byte;
        if (fx_idx >= fx_dlen) fx_state = FX_ST_CHECK;
        return 0U;
#endif

    case FX_ST_CHECK:
        if (byte != fx_chk) {                           /* 校验错:丢帧重找 */
            fx_err++;
            fx_restart(byte);
            return 0U;
        }
        fx_state = FX_ST_TAIL;
        return 0U;

    case FX_ST_TAIL:
        if (byte == (uint8_t)SYS_FRAME_TAIL) {
            /* 完整帧!拷入就绪槽（覆盖未取走的旧帧,只留最新） */
            fx_rcmd = fx_cmd;
#if SYS_FRAME_WITH_LEN
            fx_rlen = fx_dlen;
            for (fx_idx = 0U; fx_idx < fx_dlen; fx_idx++) {
                fx_rbuf[fx_idx] = fx_data[fx_idx];
            }
#else
            fx_rlen = 0U;                               /* 简化帧:数据在 cmd 里 */
#endif
            fx_ready = 1U;
            fx_state = FX_ST_HEAD;
            return 1U;                                  /* 告诉调用者:收到一帧 */
        }
        fx_err++;                                       /* 帧尾错:丢帧重找 */
        fx_restart(byte);
        return 0U;

    default:
        fx_state = FX_ST_HEAD;                          /* 理论到不了,保底复位 */
        return 0U;
    }
}

void SYS_FRAME_Reset(void)
{
    fx_state = FX_ST_HEAD;
    fx_idx   = 0U;
    fx_ready = 0U;
    fx_rlen  = 0U;
}

uint16_t SYS_FRAME_ErrCount(void)
{
    return fx_err;
}
