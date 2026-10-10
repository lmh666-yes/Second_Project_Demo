#include "sys_frame.h"

/* sys_frame.c 系统串口自定义帧协议模块（实现）
 *  线上帧格式（长度可变,两板同源）:
 *    [0][1]         0xAA 0x55     帧头（双字节,固定）
 *    [2]            CMD           命令字
 *    [3]            LEN           数据字节数（0 ~ SYS_FRAME_MAX_PAYLOAD）
 *    [4 .. 3+LEN]   DATA          LEN 字节数据
 *    [4+LEN][5+LEN] CRC16-MODBUS  低字节在前
 *    [6+LEN][7+LEN] 0x55 0xAA     帧尾（双字节,固定）
 *    整帧长度 = LEN + 8 字节
 *
 *  收帧状态机: 帧头高 -> 帧头低 -> CMD -> LEN -> DATA×n -> CRC 低 -> CRC 高 -> 帧尾高 -> 帧尾低
 *  任一步失败计错并回到找帧头；失败字节为 0xAA 时按已收到帧头高字节处理，不丢下一帧
 *  帧头（连续两字节）+ LEN 与整帧长自洽 + 整段重算 CRC16 三者同时成立才把帧交给应用
 *
 *  CRC16-MODBUS: 初值 0xFFFF，反射多项式 0xA001，末尾不异或，
 *  范围 = 从 CMD 起共 LEN+2 字节（不含帧头、CRC 自身和帧尾）
 *  收到完整帧后拷入就绪槽，只留最新一帧，取走即清
 */


/* 内部状态：每路串口一套收帧状态 */
/* 状态机取值：帧头、帧尾各拆成高字节态和低字节态两拍 */
#define FX_ST_HEAD_HI   0U      /* 找帧头高字节 0xAA（静止态） */
#define FX_ST_HEAD_LO   1U      /* 已收到 0xAA,等帧头低字节 0x55 */
#define FX_ST_CMD       2U      /* 收命令字 */
#define FX_ST_LEN       3U      /* 收长度 */
#define FX_ST_DATA      4U      /* 收数据 */
#define FX_ST_CRC_LO    5U      /* 收 CRC 低字节（协议:低字节在前） */
#define FX_ST_CRC_HI    6U      /* 收 CRC 高字节 */
#define FX_ST_TAIL_HI   7U      /* 收帧尾高字节 0x55 */
#define FX_ST_TAIL_LO   8U      /* 收帧尾低字节 0xAA */

/* 编译期护栏:通用帧负载上限须能塞进 1 字节长度字段 */
typedef char fx_max_payload_check[(SYS_FRAME_MAX_PAYLOAD <= 255U) ? 1 : -1];

/* 收帧状态按串口实例化：每个串口一份独立状态，SYS_FRAME_Poll(uart) 只喂本路，
 * 多路同时收帧不会互相打断。组帧(Send/Build)无状态，不需要实例 */
typedef struct {
    /* 收帧进行中 */
    uint8_t  state;                                     /* 当前状态 */
    uint8_t  cmd;                                       /* 已收到的命令字 */
    uint8_t  idx;                                       /* 数据接收下标 */
    uint8_t  dlen;                                      /* 本帧数据长度 */
    uint8_t  data[SYS_FRAME_MAX_PAYLOAD];               /* 正在收的数据区 */
    uint16_t crc;                                       /* 边收边算的 CRC16-MODBUS */
    uint8_t  rcrc_lo;                                   /* 帧内带来的 CRC 低字节 */

    /* 已就绪帧：最新一帧，取走即清 */
    volatile uint8_t  ready;                            /* 1 = 有帧可取 */
    volatile uint8_t  rcmd;                             /* 就绪帧的命令字 */
    volatile uint16_t rlen;                             /* 就绪帧的数据长度 */
    uint8_t  rbuf[SYS_FRAME_MAX_PAYLOAD];               /* 就绪帧的数据区 */

    /* 统计 */
    volatile uint16_t err;                              /* 出错计数 */
} FrameRx_t;

static FrameRx_t fx[SYS_USART_COUNT];

/* 旧的无 uart 形参 API 作用在哪个串口上（默认 0 = SYS_USART_1，即调试口） */
#ifndef SYS_FRAME_DEFAULT_UART
#define SYS_FRAME_DEFAULT_UART  0U
#endif


/* 内部辅助 */
/* CRC16-MODBUS: 初值 0xFFFF，多项式 0x8005 反射为 0xA001，末尾不再异或
 * 本文件自带一份实现，不 include 任一板的 modbus 头文件，两板源码得以完全相同
 * 附带性质：把 CRC 本身也算进去，结果为 0，可作整帧校验通过的判据 */
static uint16_t frame_crc16_update(uint16_t crc, uint8_t byte)
{
    uint8_t i;

    crc ^= (uint16_t)byte;
    for (i = 0U; i < 8U; i++) {
        if ((crc & 0x0001U) != 0U) {
            crc = (uint16_t)((crc >> 1) ^ 0xA001U);
        } else {
            crc = (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

/* 整段计算:初值 0xFFFF,逐字节更新 */
static uint16_t frame_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;

    if (buf == 0) return crc;                           /* 理论到不了;防野指针 */
    for (i = 0U; i < len; i++) crc = frame_crc16_update(crc, buf[i]);
    return crc;
}

/* 大端存取：环境帧规定所有多字节字段高字节在前 */
static void frame_put_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFU);
}

static uint16_t frame_get_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* 出错后重启状态机：失败字节是帧头高字节 0xAA 时直接进入等帧头低字节状态，
 * 该字节既可能是误码也可能是下一帧开头，不能丢 */
static void fx_restart(FrameRx_t *f, uint8_t byte)
{
    f->crc = 0xFFFFU;
    if (byte == (uint8_t)SYS_FRAME_HEAD_HI) {
        f->state = FX_ST_HEAD_LO;
    } else {
        f->state = FX_ST_HEAD_HI;
    }
}

/* 取某路串口的状态块；uart 越界返回 0（调用方须判空） */
static FrameRx_t *fx_get(SysUsartId_t uart)
{
    if (uart >= SYS_USART_COUNT) return 0;
    return &fx[uart];
}


/* 基础功能 */
void SYS_FRAME_Send(SysUsartId_t uart, uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    uint16_t crc;
    uint16_t i;

    if (uart >= SYS_USART_COUNT) return;
    if (len > SYS_FRAME_MAX_PAYLOAD) return;
    if ((payload == 0) && (len != 0U)) return;

    /* 帧头:高字节在前 */
    SYS_USART_SendByte(uart, (uint8_t)SYS_FRAME_HEAD_HI);
    SYS_USART_SendByte(uart, (uint8_t)SYS_FRAME_HEAD_LO);

    /* CRC 范围 = CMD + LEN + DATA，本函数阻塞逐字节发送，顺序与覆盖范围一致，边发边算 */
    crc = 0xFFFFU;
    crc = frame_crc16_update(crc, cmd);
    crc = frame_crc16_update(crc, (uint8_t)len);

    SYS_USART_SendByte(uart, cmd);
    SYS_USART_SendByte(uart, (uint8_t)len);
    for (i = 0U; i < len; i++) {
        SYS_USART_SendByte(uart, payload[i]);
        crc = frame_crc16_update(crc, payload[i]);
    }

    SYS_USART_SendByte(uart, (uint8_t)(crc & 0xFFU));   /* 低字节在前 */
    SYS_USART_SendByte(uart, (uint8_t)(crc >> 8));

    /* 帧尾:高字节 0x55 在前,低字节 0xAA 在后（协议规定,与帧头相反） */
    SYS_USART_SendByte(uart, (uint8_t)SYS_FRAME_TAIL_HI);
    SYS_USART_SendByte(uart, (uint8_t)SYS_FRAME_TAIL_LO);
}

void SYS_FRAME_SendShort(SysUsartId_t uart, uint8_t data)
{
    /* 简帧:只发命令字、数据段为空，整帧 8 字节 */
    SYS_FRAME_Send(uart, data, 0, 0U);
}

uint8_t SYS_FRAME_Poll(SysUsartId_t uart)
{
    uint8_t n = 0U;
    int     c;

    if (uart >= SYS_USART_COUNT) return 0U;

    /* 把环形缓冲里的字节一次喂完（每喂一字节都可能凑成整帧） */
    while ((c = SYS_USART_RxRead(uart)) >= 0) {
        if (SYS_FRAME_Feed(uart, (uint8_t)c) != 0U) n++;
    }
    return n;
}

uint8_t SYS_FRAME_Available(SysUsartId_t uart)
{
    FrameRx_t *f = fx_get(uart);

    if (f == 0) return 0U;
    return f->ready;
}

uint8_t SYS_FRAME_Get(SysUsartId_t uart, uint8_t *cmd, uint8_t *payload,
                        uint16_t cap, uint16_t *len)
{
    FrameRx_t *f = fx_get(uart);
    uint16_t   i;

    if (f == 0) return 1U;
    if (f->ready == 0U) return 1U;                      /* 没有帧可取 */

    /* cap 为调用方给出的缓冲容量，cap < 帧长时返回 2，避免越界写 */
    if (cap < f->rlen) return 2U;                       /* 缓冲不够:帧保留,不清 ready */

    if (cmd) *cmd = f->rcmd;
    if (len) *len = f->rlen;
    if (payload) {
        for (i = 0U; i < f->rlen; i++) payload[i] = f->rbuf[i];
    }

    f->ready = 0U;                                      /* 取走即清 */
    return 0U;
}


/* 扩展功能 */
uint8_t SYS_FRAME_Feed(SysUsartId_t uart, uint8_t byte)
{
    FrameRx_t *f = fx_get(uart);
    uint8_t    k;

    if (f == 0) return 0U;

    switch (f->state) {
    case FX_ST_HEAD_HI:
        /* 找帧头高字节：非 0xAA 丢弃，即重新同步的静止态 */
        if (byte == (uint8_t)SYS_FRAME_HEAD_HI) {
            f->state = FX_ST_HEAD_LO;
        }
        return 0U;

    case FX_ST_HEAD_LO:
        if (byte == (uint8_t)SYS_FRAME_HEAD_LO) {
            /* 帧头齐了:CRC 从 CMD 起算,此刻复位初值 */
            f->crc   = 0xFFFFU;
            f->state = FX_ST_CMD;
        } else if (byte != (uint8_t)SYS_FRAME_HEAD_HI) {
            f->state = FX_ST_HEAD_HI;                   /* 既非 0x55 也非 0xAA:彻底重找 */
        }
        /* byte == 0xAA:连着的两个 0xAA,把本字节当作新的帧头高字节,状态不动 */
        return 0U;

    case FX_ST_CMD:
        f->cmd = byte;
        f->crc = frame_crc16_update(f->crc, byte);
        f->state = FX_ST_LEN;
        return 0U;

    case FX_ST_LEN:
        if (byte > (uint8_t)SYS_FRAME_MAX_PAYLOAD) {    /* 长度超限:丢帧重找 */
            f->err++;
            fx_restart(f, byte);
            return 0U;
        }
        f->dlen = byte;
        f->crc  = frame_crc16_update(f->crc, byte);
        f->idx  = 0U;
        /* 数据段长度 0 的帧直接进 CRC 态，即 SendShort 发出的 8 字节帧 */
        f->state = (byte == 0U) ? FX_ST_CRC_LO : FX_ST_DATA;
        return 0U;

    case FX_ST_DATA:
        f->data[f->idx] = byte;
        f->idx++;
        f->crc = frame_crc16_update(f->crc, byte);
        if (f->idx >= f->dlen) f->state = FX_ST_CRC_LO;
        return 0U;

    case FX_ST_CRC_LO:
        f->rcrc_lo = byte;                              /* 协议:CRC 低字节在前,先存着 */
        f->state   = FX_ST_CRC_HI;
        return 0U;

    case FX_ST_CRC_HI:
        /* CRC16 与边收边算值不等则丢帧：差错、丢字节、假帧头在此拦下 */
        if ((uint16_t)(((uint16_t)byte << 8) | (uint16_t)f->rcrc_lo) != f->crc) {
            f->err++;
            fx_restart(f, byte);
            return 0U;
        }
        f->state = FX_ST_TAIL_HI;
        return 0U;

    case FX_ST_TAIL_HI:
        if (byte != (uint8_t)SYS_FRAME_TAIL_HI) {       /* 帧尾高字节错:丢帧重找 */
            f->err++;
            fx_restart(f, byte);
            return 0U;
        }
        f->state = FX_ST_TAIL_LO;
        return 0U;

    case FX_ST_TAIL_LO:
        if (byte == (uint8_t)SYS_FRAME_TAIL_LO) {
            /* 完整帧拷入本串口就绪槽；覆盖未取走的旧帧，只留最新 */
            f->rcmd = f->cmd;
            f->rlen = f->dlen;
            for (k = 0U; k < f->dlen; k++) {
                f->rbuf[k] = f->data[k];
            }
            f->ready = 1U;
            f->state = FX_ST_HEAD_HI;
            return 1U;                                  /* 告诉调用者:收到一帧 */
        }
        f->err++;                                       /* 帧尾低字节错:丢帧重找 */
        fx_restart(f, byte);
        return 0U;

    default:
        f->state = FX_ST_HEAD_HI;                       /* 理论到不了,保底复位 */
        return 0U;
    }
}

void SYS_FRAME_Reset(SysUsartId_t uart)
{
    FrameRx_t *f = fx_get(uart);

    if (f == 0) return;

    f->state = FX_ST_HEAD_HI;
    f->idx   = 0U;
    f->crc   = 0xFFFFU;
    f->ready = 0U;
    f->rlen  = 0U;
}

uint16_t SYS_FRAME_ErrCount(SysUsartId_t uart)
{
    FrameRx_t *f = fx_get(uart);

    if (f == 0) return 0U;
    return f->err;
}


/* 兼容层:旧的无 uart 形参 API，一律作用于 SYS_FRAME_DEFAULT_UART（默认 0 = SYS_USART_1） */
uint8_t SYS_FRAME_AvailableLegacy(void)
{
    return SYS_FRAME_Available((SysUsartId_t)SYS_FRAME_DEFAULT_UART);
}

uint8_t SYS_FRAME_FeedLegacy(uint8_t byte)
{
    return SYS_FRAME_Feed((SysUsartId_t)SYS_FRAME_DEFAULT_UART, byte);
}

uint8_t SYS_FRAME_GetLegacy(uint8_t *cmd, uint8_t *payload, uint16_t cap, uint16_t *len)
{
    return SYS_FRAME_Get((SysUsartId_t)SYS_FRAME_DEFAULT_UART, cmd, payload, cap, len);
}

void SYS_FRAME_ResetLegacy(void)
{
    SYS_FRAME_Reset((SysUsartId_t)SYS_FRAME_DEFAULT_UART);
}

uint16_t SYS_FRAME_ErrCountLegacy(void)
{
    return SYS_FRAME_ErrCount((SysUsartId_t)SYS_FRAME_DEFAULT_UART);
}

/* 组帧到缓冲区 / 整帧校验 */
uint16_t SYS_FRAME_Build(uint8_t cmd, const uint8_t *payload, uint16_t len,
                         uint8_t *out, uint16_t cap)
{
    uint16_t crc;
    uint16_t i;

    if (out == 0) return 0U;
    if (len > SYS_FRAME_MAX_PAYLOAD) return 0U;
    if ((payload == 0) && (len != 0U)) return 0U;
    if (cap < (uint16_t)(len + SYS_FRAME_OVERHEAD)) return 0U;

    out[0] = (uint8_t)SYS_FRAME_HEAD_HI;
    out[1] = (uint8_t)SYS_FRAME_HEAD_LO;
    out[2] = cmd;
    out[3] = (uint8_t)len;
    for (i = 0U; i < len; i++) out[4U + i] = payload[i];

    /* CRC 覆盖 CMD+LEN+DATA,即从 out[2] 起共 len+2 字节 */
    crc = frame_crc16(&out[2], (uint16_t)(len + 2U));
    out[4U + len] = (uint8_t)(crc & 0xFFU);             /* 低字节在前 */
    out[5U + len] = (uint8_t)(crc >> 8);

    out[6U + len] = (uint8_t)SYS_FRAME_TAIL_HI;
    out[7U + len] = (uint8_t)SYS_FRAME_TAIL_LO;
    return (uint16_t)(len + SYS_FRAME_OVERHEAD);
}

uint8_t SYS_FRAME_Verify(const uint8_t *buf, uint16_t len)
{
    uint16_t crc;
    uint16_t dlen;

    if (buf == 0) return SYS_FRAME_ERR_PARAM;
    if (len < SYS_FRAME_OVERHEAD) return SYS_FRAME_ERR_PARAM;

    /* 帧头必须连续两字节 0xAA 0x55：帧尾与下一帧帧头接缝处会出现 55 AA AA 55，
     * 只匹配单个字节会认错 */
    if ((buf[0] != (uint8_t)SYS_FRAME_HEAD_HI) ||
        (buf[1] != (uint8_t)SYS_FRAME_HEAD_LO)) return SYS_FRAME_ERR_HEAD;

    if ((buf[len - 2U] != (uint8_t)SYS_FRAME_TAIL_HI) ||
        (buf[len - 1U] != (uint8_t)SYS_FRAME_TAIL_LO)) return SYS_FRAME_ERR_TAIL;

    /* 长度字段须与传入的整帧长度自洽：len = LEN + SYS_FRAME_OVERHEAD，
     * 两帧粘接成的长包在此拦下 */
    dlen = (uint16_t)buf[3];
    if ((dlen + SYS_FRAME_OVERHEAD) != len) return SYS_FRAME_ERR_LEN;

    /* 整段重算 CRC16：覆盖 CMD+LEN+DATA，不含帧头帧尾 */
    crc = frame_crc16(&buf[2], (uint16_t)(dlen + 2U));
    if ((buf[4U + dlen] != (uint8_t)(crc & 0xFFU)) ||
        (buf[5U + dlen] != (uint8_t)(crc >> 8))) return SYS_FRAME_ERR_CRC;

    return SYS_FRAME_OK;
}


/* 环境数据帧（CMD = 0x01）的组帧 / 拆包助手：固定 12 字节大端数据段，
 * 协议本体仍是通用信封 */
uint16_t SYS_FRAME_BuildEnv(uint8_t *out, uint16_t cap,
                            int16_t temp_x100, int16_t humi_x100, int16_t press_x10,
                            uint16_t light, uint16_t tvoc, uint16_t mq135)
{
    uint8_t env[SYS_FRAME_ENV_LEN];

    if (out == 0) return 0U;

    /* 所有多字节字段一律大端（高字节在前）,与帧内 CRC 的小端相反 */
    frame_put_u16_be(&env[SYS_FRAME_ENV_TEMP_OFF],  (uint16_t)temp_x100);
    frame_put_u16_be(&env[SYS_FRAME_ENV_HUMI_OFF],  (uint16_t)humi_x100);
    frame_put_u16_be(&env[SYS_FRAME_ENV_PRESS_OFF], (uint16_t)press_x10);
    frame_put_u16_be(&env[SYS_FRAME_ENV_LIGHT_OFF], light);
    frame_put_u16_be(&env[SYS_FRAME_ENV_TVOC_OFF],  tvoc);
    frame_put_u16_be(&env[SYS_FRAME_ENV_MQ135_OFF], mq135);

    /* 信封交给通用组帧:帧头 + CMD + LEN + 数据段 + CRC + 帧尾 = 20 字节 */
    return SYS_FRAME_Build((uint8_t)SYS_FRAME_CMD_ENV, env,
                           (uint16_t)SYS_FRAME_ENV_LEN, out, cap);
}

uint8_t SYS_FRAME_UnpackEnv(const uint8_t *payload, uint16_t len,
                            int16_t *temp_x100, int16_t *humi_x100, int16_t *press_x10,
                            uint16_t *light, uint16_t *tvoc, uint16_t *mq135)
{
    if (payload == 0) return SYS_FRAME_ERR_PARAM;
    if (len < (uint16_t)SYS_FRAME_ENV_LEN) return SYS_FRAME_ERR_PARAM;

    /* 任一输出指针给 0 就跳过该项（只想取温度时不必声明其它变量） */
    if (temp_x100) *temp_x100 = (int16_t)frame_get_u16_be(&payload[SYS_FRAME_ENV_TEMP_OFF]);
    if (humi_x100) *humi_x100 = (int16_t)frame_get_u16_be(&payload[SYS_FRAME_ENV_HUMI_OFF]);
    if (press_x10) *press_x10 = (int16_t)frame_get_u16_be(&payload[SYS_FRAME_ENV_PRESS_OFF]);
    if (light) *light = frame_get_u16_be(&payload[SYS_FRAME_ENV_LIGHT_OFF]);
    if (tvoc)  *tvoc  = frame_get_u16_be(&payload[SYS_FRAME_ENV_TVOC_OFF]);
    if (mq135) *mq135 = frame_get_u16_be(&payload[SYS_FRAME_ENV_MQ135_OFF]);

    return SYS_FRAME_OK;
}
