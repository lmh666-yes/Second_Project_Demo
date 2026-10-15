#include "lora_e22.h"
/* 接线 / 模式表 / AT 指令速查见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时与 DWT 微秒计时 */

#include <string.h>

/* ================================================================
 *  lora_e22.c 亿佰特 E22-400T22S LoRa 模块 实现
 * ================================================================
 *  引脚 / 波特率 / 超时取自 lora_e22.h 配置区；帧边界按字节间间隔判定。
 * ================================================================ */


/* ================================================================
 *                    内部配置
 * ================================================================ */
/* 三个脚是否可选只能用 `#if defined(宏名)`：SPL 的 GPIOB 展开为
 * ((GPIO_TypeDef *) GPIOB_BASE)，GPIO_Pin_x 展开为 ((uint16_t)0x0001)，
 * 均为带强制类型转换的表达式；进 `#if` 报 #29 expected an expression。 */
typedef struct {
    GPIO_TypeDef *m0_port;   uint16_t m0_pin;
    GPIO_TypeDef *m1_port;   uint16_t m1_pin;
    GPIO_TypeDef *aux_port;  uint16_t aux_pin;
    SysUsartId_t  uart;
    uint32_t      baud;
} LoraCfg_t;

static const LoraCfg_t lora_cfg = {
#if defined(SYS_LORA_M0_PORT)
    SYS_LORA_M0_PORT,  SYS_LORA_M0_PIN,
#else
    0, 0U,
#endif
#if defined(SYS_LORA_M1_PORT)
    SYS_LORA_M1_PORT,  SYS_LORA_M1_PIN,
#else
    0, 0U,
#endif
#if defined(SYS_LORA_AUX_PORT)
    SYS_LORA_AUX_PORT, SYS_LORA_AUX_PIN,
#else
    0, 0U,
#endif
    SYS_LORA_UART,
    SYS_LORA_BAUD
};

/* 收帧兜底缓冲（调用方缓冲装不下时用） */
static uint8_t lora_rxbuf[SYS_LORA_RX_BUF_SIZE];

/* 收帧状态，本文件私有静态，非重入 */
static uint16_t lora_rxlen;       /* 已攒字节数 */
static uint8_t  lora_rxgarbage;   /* 本帧溢出标志（溢出则整帧丢弃） */


/* ================================================================
 *                    内部辅助
 * ================================================================ */
/* DWT 周期计数 */
static uint32_t lora_now_cycles(void)
{
    return DWT->CYCCNT;
}

static uint32_t lora_ms_to_cycles(uint32_t ms)
{
    return ms * (SystemCoreClock / 1000U);
}

/* 已过去毫秒数（t0 来自 lora_now_cycles） */
static uint32_t lora_elapsed_ms(uint32_t t0)
{
    return (uint32_t)(DWT->CYCCNT - t0) / (SystemCoreClock / 1000U);
}

/* 驱动 M0/M1（端口为 0 的脚跳过） */
static void lora_set_pins(uint8_t m1, uint8_t m0)
{
    if (lora_cfg.m0_port != 0) {
        GPIO_OutWrite(lora_cfg.m0_port, lora_cfg.m0_pin, m0 ? 1U : 0U);
    }
    if (lora_cfg.m1_port != 0) {
        GPIO_OutWrite(lora_cfg.m1_port, lora_cfg.m1_pin, m1 ? 1U : 0U);
    }
}

/* M1/M0 模式编码 */
static void lora_mode_to_pins(LoraE22Mode_t mode, uint8_t *m1, uint8_t *m0)
{
    switch (mode) {
    case LORA_E22_MODE_TRANSPARENT: *m1 = 0U; *m0 = 0U; break;  /* 透传 */
    case LORA_E22_MODE_WOR:         *m1 = 0U; *m0 = 1U; break;  /* 唤醒 */
    case LORA_E22_MODE_CONFIG:      *m1 = 1U; *m0 = 0U; break;  /* 配置 */
    case LORA_E22_MODE_SLEEP:       *m1 = 1U; *m0 = 1U; break;  /* 深睡 */
    default:                        *m1 = 0U; *m0 = 0U; break;
    }
}

/* 等模块空闲：AUX 为高即空闲；未接 AUX 时固定延时兜底 */
static uint8_t lora_wait_idle(uint32_t timeout_ms)
{
    uint32_t t0;

    if (lora_cfg.aux_port == 0) {
        delay_ms_dwt(2);
        return 2U;
    }

    t0 = lora_now_cycles();
    while (GPIO_InRead(lora_cfg.aux_port, lora_cfg.aux_pin) == 0U) {
        if (lora_elapsed_ms(t0) >= timeout_ms) {
            return 0U;              /* 一直忙：多为 M0/M1/AUX 接错 */
        }
    }
    return 1U;
}

/* 从串口环形缓冲读一行（到 '\n' 或超时） */
static uint16_t lora_read_line(char *out, uint16_t max, uint32_t timeout_ms)
{
    uint16_t n = 0U;
    uint32_t t0 = lora_now_cycles();
    uint32_t limit = lora_ms_to_cycles(timeout_ms);
    int c;

    if (out == 0 || max < 2U) return 0U;
    out[0] = '\0';

    for (;;) {
        c = SYS_USART_RxRead(lora_cfg.uart);
        if (c >= 0) {
            if (n < (uint16_t)(max - 1U)) {
                out[n] = (char)c;
                n++;
                out[n] = '\0';
            }
            if ((char)c == '\n') {
                return n;           /* 一行读完 */
            }
        } else {
            if ((uint32_t)(lora_now_cycles() - t0) >= limit) {
                return n;           /* 超时：返回已收到的（可能半行） */
            }
        }
    }
}

/* 判断一行是否含子串（大小写敏感）
 * 返回 : 1 = 含 / 0 = 不含 */
static uint8_t lora_line_has(const char *line, const char *key)
{
    return (strstr(line, key) != 0) ? 1U : 0U;
}


/* ================================================================
 *  初始化 / 模式 / 发送 / 接收 / 配置（接口说明见 .h）
 * ================================================================ */
uint8_t LORA_E22_Init(void)
{
    uint8_t r;

    /* M0/M1 先置透传(0,0)，须在开串口前：模块停在配置模式会吃掉数据帧 */
    if (lora_cfg.m0_port != 0) {
        GPIO_OutInit(lora_cfg.m0_port, lora_cfg.m0_pin);
    }
    if (lora_cfg.m1_port != 0) {
        GPIO_OutInit(lora_cfg.m1_port, lora_cfg.m1_pin);
    }
    lora_set_pins(0U, 0U);

    /* AUX 为模块输出，MCU 侧配输入上拉（未接时读高） */
    if (lora_cfg.aux_port != 0) {
        GPIO_InInit(lora_cfg.aux_port, lora_cfg.aux_pin, GPIO_PuPd_UP);
    }

    SYS_USART_InitRxIT(lora_cfg.uart, lora_cfg.baud);
    SYS_USART_RxFlush(lora_cfg.uart);

    lora_rxlen = 0U;
    lora_rxgarbage = 0U;
    r = lora_wait_idle(SYS_LORA_AUX_TIMEOUT_MS);
    if (r == 0U) {
        return LORA_E22_ERR_AUX;    /* AUX 一直低 = 模块未接好 / 未上电 */
    }
    return LORA_E22_OK;
}

uint8_t LORA_E22_SetMode(LoraE22Mode_t mode)
{
    uint8_t m1, m0;

    if ((uint8_t)mode > (uint8_t)LORA_E22_MODE_SLEEP) {
        return LORA_E22_ERR_PARAM;
    }

    lora_mode_to_pins(mode, &m1, &m0);
    lora_set_pins(m1, m0);

    /* 数据手册时序：切进配置/休眠须等当前包发完（典型 <5ms），
     * 切回透传须重新初始化射频（典型 <15ms） */
    if (lora_wait_idle(SYS_LORA_AUX_TIMEOUT_MS) == 0U) {
        return LORA_E22_ERR_AUX;
    }
    return LORA_E22_OK;
}

uint8_t LORA_E22_IsBusy(void)
{
    if (lora_cfg.aux_port == 0) return 0U;      /* 未接 AUX：恒认为空闲 */
    return (GPIO_InRead(lora_cfg.aux_port, lora_cfg.aux_pin) == 0U) ? 1U : 0U;
}

uint8_t LORA_E22_WaitReady(uint32_t timeout_ms)
{
    return (lora_wait_idle(timeout_ms) != 0U) ? 1U : 0U;
}


/* ================================================================
 *                    发送 / 接收
 * ================================================================ */
uint8_t LORA_E22_Send(const uint8_t *data, uint16_t len)
{
    if (data == 0 || len == 0U) return LORA_E22_ERR_PARAM;

    /* 等上一包发完再灌数据（E22 的 UART 接收缓冲有限） */
    (void)lora_wait_idle(SYS_LORA_AUX_TIMEOUT_MS);

    SYS_USART_SendBuf(lora_cfg.uart, data, len);
    SYS_USART_FlushTx(lora_cfg.uart);   /* 等最后一位移出再返回 */

    return LORA_E22_OK;
}

/* 收帧：调用方缓冲、字节间间隔截帧，须周期调用 */
uint8_t LORA_E22_Recv(uint8_t *buf, uint16_t max, uint16_t *out_len)
{
    static uint32_t last_cycle = 0U;    /* 上一个字节到达时刻 */
    int c;

    if (buf == 0 || max == 0U || out_len == 0) return LORA_E22_ERR_PARAM;
    *out_len = 0U;

    for (;;) {
        c = SYS_USART_RxRead(lora_cfg.uart);

        if (c >= 0) {
            if (lora_rxlen < max) {
                buf[lora_rxlen] = (uint8_t)c;
                lora_rxlen++;
            } else {
                lora_rxgarbage = 1U;    /* 装不下：标记本帧作废 */
            }
            last_cycle = lora_now_cycles();
            continue;                   /* 连续到达就一起收 */
        }

        /* 缓冲空了：已收过字节且距上一字节超过帧间隔即本帧结束 */
        if (lora_rxlen > 0U || lora_rxgarbage != 0U) {
            uint32_t gap = (uint32_t)(lora_now_cycles() - last_cycle);
            if (gap >= lora_ms_to_cycles(SYS_LORA_FRAME_GAP_MS)) {
                uint16_t n = lora_rxlen;
                uint8_t  bad = lora_rxgarbage;

                lora_rxlen = 0U;
                lora_rxgarbage = 0U;
                if (bad != 0U) {
                    return LORA_E22_ERR_OVERFLOW;   /* 本帧丢弃 */
                }
                *out_len = n;
                return LORA_E22_OK;
            }
        }
        break;                          /* 未到帧间隔：下轮再看 */
    }

    return LORA_E22_ERR_NO_DATA;        /* 暂无完整帧 */
}

void LORA_E22_Flush(void)
{
    SYS_USART_RxFlush(lora_cfg.uart);
    lora_rxlen = 0U;
    lora_rxgarbage = 0U;
    (void)lora_rxbuf[0];
}


/* ================================================================
 *                    配置（AT 指令）
 * ================================================================ */
uint8_t LORA_E22_SendAT(const char *cmd)
{
    char line[48];
    uint8_t got_ok = 0U;
    uint8_t got_err = 0U;
    uint32_t t0;

    if (cmd == 0) return LORA_E22_ERR_PARAM;

    if (LORA_E22_SetMode(LORA_E22_MODE_CONFIG) != LORA_E22_OK) {
        return LORA_E22_ERR_AUX;
    }
    /* 丢弃切模式期间的回显 */
    SYS_USART_RxFlush(lora_cfg.uart);
    SYS_USART_SendString(lora_cfg.uart, cmd);
    SYS_USART_SendString(lora_cfg.uart, "\r\n");
    SYS_USART_FlushTx(lora_cfg.uart);

    /* 在超时窗口内读行，直到看见 OK / ERROR */
    t0 = lora_now_cycles();
    while (lora_elapsed_ms(t0) < SYS_LORA_AT_TIMEOUT_MS) {
        if (lora_read_line(line, sizeof(line), 50U) > 0U) {
            if (lora_line_has(line, "OK") != 0U)     { got_ok = 1U;  break; }
            if (lora_line_has(line, "ERROR") != 0U)  { got_err = 1U; break; }
        }
    }

    (void)LORA_E22_SetMode(LORA_E22_MODE_TRANSPARENT);
    SYS_USART_RxFlush(lora_cfg.uart);

    if (got_ok  != 0U) return LORA_E22_OK;
    if (got_err != 0U) return LORA_E22_ERR_AT_ERROR;
    return LORA_E22_ERR_NO_MODULE;
}

uint8_t LORA_E22_ReadConfig(char *out, uint16_t out_size)
{
    static const char *const query[] = { "AT+VER", "AT+ADDR", "AT+NETID", "AT+REG0" };
    char line[48];
    uint16_t used = 0U;
    uint8_t  i;
    uint32_t t0;

    if (out == 0 || out_size < 8U) return LORA_E22_ERR_PARAM;
    out[0] = '\0';

    if (LORA_E22_SetMode(LORA_E22_MODE_CONFIG) != LORA_E22_OK) {
        return LORA_E22_ERR_AUX;
    }
    SYS_USART_RxFlush(lora_cfg.uart);

    for (i = 0U; i < 4U; i++) {
        SYS_USART_SendString(lora_cfg.uart, query[i]);
        SYS_USART_SendString(lora_cfg.uart, "\r\n");
        SYS_USART_FlushTx(lora_cfg.uart);

        /* 每条指令最多等 SYS_LORA_AT_TIMEOUT_MS，回行拼进 out */
        t0 = lora_now_cycles();
        while (lora_elapsed_ms(t0) < SYS_LORA_AT_TIMEOUT_MS) {
            if (lora_read_line(line, sizeof(line), 50U) > 0U) {
                uint16_t L = (uint16_t)strlen(line);
                if ((uint32_t)used + L + 3U < out_size) {
                    memcpy(&out[used], line, L);
                    used = (uint16_t)(used + L);
                    out[used] = ' ';        /* 多行折成一行 */
                    used++;
                    out[used] = '\0';
                }
            }
            if (used > 0U) break;           /* 本条已回复，问下一条 */
        }
    }

    (void)LORA_E22_SetMode(LORA_E22_MODE_TRANSPARENT);
    SYS_USART_RxFlush(lora_cfg.uart);

    if (used == 0U) return LORA_E22_ERR_NO_MODULE;
    return LORA_E22_OK;
}

uint8_t LORA_E22_SetBaud(uint32_t baud)
{
    char cmd[32];
    uint8_t r;

    switch (baud) {
    case 1200U: case 2400U: case 4800U: case 9600U:
    case 19200U: case 38400U: case 57600U: case 115200U:
        break;
    default:
        return LORA_E22_ERR_PARAM;      /* 仅 E22 支持的几档 */
    }

    /* 组 "AT+UART=<baud>,8,1,NON"（8 数据位 / 1 停止位 / 无校验） */
    {
        char num[12];
        uint8_t k = 0U;
        uint32_t v = baud;
        char tmp[12];
        uint8_t t = 0U;

        while (v > 0U && t < sizeof(tmp)) { tmp[t] = (char)('0' + (v % 10U)); v /= 10U; t++; }
        while (t > 0U) { num[k] = tmp[--t]; k++; }
        num[k] = '\0';

        /* 手工拼字符串，不用 snprintf：部分工程未开 microLIB，格式化代码体积大 */
        {
            const char *p1 = "AT+UART=";
            const char *p2 = ",8,1,NON";
            uint8_t n = 0U;
            while (*p1 != '\0' && n < (sizeof(cmd) - 1U)) { cmd[n] = *p1; n++; p1++; }
            {
                uint8_t j = 0U;
                while (num[j] != '\0' && n < (sizeof(cmd) - 1U)) { cmd[n] = num[j]; n++; j++; }
            }
            {
                uint8_t j = 0U;
                while (p2[j] != '\0' && n < (sizeof(cmd) - 1U)) { cmd[n] = p2[j]; n++; j++; }
            }
            cmd[n] = '\0';
        }
    }

    r = LORA_E22_SendAT(cmd);
    if (r != LORA_E22_OK) return r;

    /* 模块已切新波特率，本端须立刻跟上，否则收不到它回话 */
    SYS_USART_InitRxIT(lora_cfg.uart, baud);
    SYS_USART_RxFlush(lora_cfg.uart);
    (void)LORA_E22_WaitReady(SYS_LORA_AUX_TIMEOUT_MS);

    return LORA_E22_OK;
}

uint8_t LORA_E22_Reset(void)
{
    uint8_t r;

    if (LORA_E22_SetMode(LORA_E22_MODE_CONFIG) != LORA_E22_OK) {
        return LORA_E22_ERR_AUX;
    }
    SYS_USART_RxFlush(lora_cfg.uart);
    SYS_USART_SendString(lora_cfg.uart, "AT+RESET\r\n");
    SYS_USART_FlushTx(lora_cfg.uart);

    /* 复位后模块重启射频，回 OK 后仍需等待 */
    {
        char line[48];
        uint32_t t0 = lora_now_cycles();
        while (lora_elapsed_ms(t0) < SYS_LORA_AT_TIMEOUT_MS) {
            if (lora_read_line(line, sizeof(line), 50U) > 0U) {
                if (lora_line_has(line, "OK") != 0U) break;
            }
        }
    }

    (void)LORA_E22_SetMode(LORA_E22_MODE_TRANSPARENT);
    SYS_USART_RxFlush(lora_cfg.uart);
    (void)LORA_E22_WaitReady(SYS_LORA_AUX_TIMEOUT_MS);

    r = LORA_E22_OK;
    return r;
}
