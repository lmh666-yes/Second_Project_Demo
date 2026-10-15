#include "lora_e22.h"
#include "gpio_core.h"
#include "delay.h"      /* 延时与 DWT 微秒计时 */

#include <string.h>

/* 亿佰特 E22-400T22S LoRa 模块驱动实现，基于标准外设库。
 * 引脚、波特率、超时来自 lora_e22.h 配置区；接线与 AT 指令见该头文件。
 * 帧边界靠字节间停顿判定，所以 LORA_E22_Recv() 必须周期性调用。 */


/* 引脚配置：把 .h 里的三个宏折成运行期可用的形式。
 * 某个脚没接就把 .h 里对应的 PORT / PIN 两行一起注释掉，
 * 这里折成端口 = 0、引脚 = 0，该功能运行期自动降级。
 * 判据必须用 #if defined(宏名)，不能写 #if (宏 ...) 做算术判定：
 * SPL 里 GPIOB 展开为带强制类型转换的指针常量，GPIO_Pin_x 展开为
 * ((uint16_t)0x0001) 这类带类型转换的表达式，
 * C90 预处理表达式不允许强制类型转换，会报 #29 / #59 / #18 错误。 */
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

/* 收帧缓冲：供分片 / 兜底使用，当前 Recv 直接写调用方缓冲 */
static uint8_t lora_rxbuf[SYS_LORA_RX_BUF_SIZE];

/* 收帧状态，本文件私有静态变量，非重入 */
static uint16_t lora_rxlen;       /* 已攒字节数 */
static uint8_t  lora_rxgarbage;   /* 本帧是否溢出过，溢出则整帧丢弃 */


/* 内部辅助 */
/* DWT 周期计数与毫秒互转，供字节间超时用。
 * SysTick 在 RTOS 下被内核占用，这里统一用 DWT，与 delay.h、
 * sys_usart 的 ReadUntil 同一套计时源 */
static uint32_t lora_now_cycles(void)
{
    return DWT->CYCCNT;
}

static uint32_t lora_ms_to_cycles(uint32_t ms)
{
    return ms * (SystemCoreClock / 1000U);
}

/* 已过去多少毫秒（t0 来自 lora_now_cycles） */
static uint32_t lora_elapsed_ms(uint32_t t0)
{
    return (uint32_t)(DWT->CYCCNT - t0) / (SystemCoreClock / 1000U);
}

/* 驱动 M0/M1 两个模式脚（宏置 0 的脚自动跳过） */
static void lora_set_pins(uint8_t m1, uint8_t m0)
{
    if (lora_cfg.m0_port != 0) {
        GPIO_OutWrite(lora_cfg.m0_port, lora_cfg.m0_pin, m0 ? 1U : 0U);
    }
    if (lora_cfg.m1_port != 0) {
        GPIO_OutWrite(lora_cfg.m1_port, lora_cfg.m1_pin, m1 ? 1U : 0U);
    }
}

/* M1/M0 电平 ↔ 模式号，便于 SetMode 换算 */
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

/* 等模块空闲：AUX 高 = 空闲。
 * AUX 没接时无法判断，退化为固定延时，不返回失败，
 * 否则没接 AUX 的板子整个驱动都用不了。
 * 返回 : 1 = 已空闲 / 2 = 没接 AUX，已用固定延时兜底 / 0 = 超时 */
static uint8_t lora_wait_idle(uint32_t timeout_ms)
{
    uint32_t t0;

    if (lora_cfg.aux_port == 0) {
        delay_ms_dwt(2);            /* 没接 AUX：给模块 2ms 稳定时间 */
        return 2U;
    }

    t0 = lora_now_cycles();
    while (GPIO_InRead(lora_cfg.aux_port, lora_cfg.aux_pin) == 0U) {
        if (lora_elapsed_ms(t0) >= timeout_ms) {
            return 0U;              /* 一直忙：多半 M0/M1/AUX 接错 */
        }
    }
    return 1U;
}

/* 从串口环形缓冲读一行，到 '\n' 或超时为止，返回长度，0 表示没读到。
 * 用途 : 解析模块的 "OK" / "ERROR" / AT 回显 */
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
                return n;           /* 超时：把已读到的返回（可能是半行） */
            }
        }
    }
}

/* 判断一行里是否含指定子串（大小写敏感；模块固定回 "OK" / "ERROR"）
 * 返回 : 1 = 含 / 0 = 不含 */
static uint8_t lora_line_has(const char *line, const char *key)
{
    return (strstr(line, key) != 0) ? 1U : 0U;
}


/* 初始化 / 模式 */
uint8_t LORA_E22_Init(void)
{
    uint8_t r;

    /* 先摆模式：透传 M1M0=00，且必须在开串口之前。
     * 否则模块可能停在配置模式，把应用层数据帧当 AT 指令吃掉 */
    if (lora_cfg.m0_port != 0) {
        GPIO_OutInit(lora_cfg.m0_port, lora_cfg.m0_pin);
    }
    if (lora_cfg.m1_port != 0) {
        GPIO_OutInit(lora_cfg.m1_port, lora_cfg.m1_pin);
    }
    lora_set_pins(0U, 0U);

    /* AUX 是模块的输出脚，MCU 侧配成输入，上拉：不接时读高，
     * 等价于空闲状态 */
    if (lora_cfg.aux_port != 0) {
        GPIO_InInit(lora_cfg.aux_port, lora_cfg.aux_pin, GPIO_PuPd_UP);
    }

    /* 串口：中断接收，收到的字节自动进环形缓冲 */
    SYS_USART_InitRxIT(lora_cfg.uart, lora_cfg.baud);
    SYS_USART_RxFlush(lora_cfg.uart);

    /* 等模块就绪：上电后模块自初始化射频，典型十几毫秒 */
    lora_rxlen = 0U;
    lora_rxgarbage = 0U;
    r = lora_wait_idle(SYS_LORA_AUX_TIMEOUT_MS);
    if (r == 0U) {
        return LORA_E22_ERR_AUX;    /* AUX 一直低 = 模块没接好/没上电 */
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

    /* 数据手册时序：透传/唤醒切进配置/休眠要等当前包发完，典型小于 5ms；
     * 切回透传要重新初始化射频，典型小于 15ms。
     * 统一按 AUX 等，AUX 没接时 wait_idle 内部给 2ms 固定延时兜底 */
    if (lora_wait_idle(SYS_LORA_AUX_TIMEOUT_MS) == 0U) {
        return LORA_E22_ERR_AUX;
    }
    return LORA_E22_OK;
}

uint8_t LORA_E22_IsBusy(void)
{
    if (lora_cfg.aux_port == 0) return 0U;      /* 没接 AUX：恒认为空闲 */
    return (GPIO_InRead(lora_cfg.aux_port, lora_cfg.aux_pin) == 0U) ? 1U : 0U;
}

uint8_t LORA_E22_WaitReady(uint32_t timeout_ms)
{
    return (lora_wait_idle(timeout_ms) != 0U) ? 1U : 0U;
}


/* 发送 / 接收 */
uint8_t LORA_E22_Send(const uint8_t *data, uint16_t len)
{
    if (data == 0 || len == 0U) return LORA_E22_ERR_PARAM;

    /* 等模块把上一包发完再灌数据：E22 的 UART 接收缓冲有限，
     * 灌太快会丢字节；没接 AUX 时这里只是 2ms 延时 */
    (void)lora_wait_idle(SYS_LORA_AUX_TIMEOUT_MS);

    SYS_USART_SendBuf(lora_cfg.uart, data, len);
    SYS_USART_FlushTx(lora_cfg.uart);   /* 等最后一位真正移出去再返回 */

    return LORA_E22_OK;
}

/* 收一帧：把环形缓冲里的字节按字节间间隔截成一帧。
 * 必须周期性调用，建议 10~50ms 一次，调用太稀会把两帧粘成一帧 */
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
                lora_rxgarbage = 1U;    /* 装不下，标记本帧作废 */
            }
            last_cycle = lora_now_cycles();
            continue;                   /* 连着来就一起收 */
        }

        /* 缓冲空了：若已经收过字节、且距上一个字节已超过"帧间隔"，
         * 说明这一帧吐完了 */
        if (lora_rxlen > 0U || lora_rxgarbage != 0U) {
            uint32_t gap = (uint32_t)(lora_now_cycles() - last_cycle);
            if (gap >= lora_ms_to_cycles(SYS_LORA_FRAME_GAP_MS)) {
                uint16_t n = lora_rxlen;
                uint8_t  bad = lora_rxgarbage;

                lora_rxlen = 0U;
                lora_rxgarbage = 0U;
                if (bad != 0U) {
                    return LORA_E22_ERR_OVERFLOW;   /* 这一帧丢了，状态已复位 */
                }
                *out_len = n;
                return LORA_E22_OK;
            }
        }
        break;                          /* 还没到帧间隔：下轮再来看 */
    }

    return LORA_E22_ERR_NO_DATA;        /* 暂时没有完整帧（正常） */
}

void LORA_E22_Flush(void)
{
    SYS_USART_RxFlush(lora_cfg.uart);
    lora_rxlen = 0U;
    lora_rxgarbage = 0U;
    (void)lora_rxbuf[0];                /* 引用一次，避免未使用告警 */
}


/* 配置（AT 指令） */
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
    SYS_USART_RxFlush(lora_cfg.uart);   /* 丢掉切模式期间的回显噪声 */

    /* 指令必须以 \r\n 结尾，模块才认 */
    SYS_USART_SendString(lora_cfg.uart, cmd);
    SYS_USART_SendString(lora_cfg.uart, "\r\n");
    SYS_USART_FlushTx(lora_cfg.uart);

    /* 在超时窗口内反复读行，直到看见 OK / ERROR */
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

        /* 每条指令最多等 SYS_LORA_AT_TIMEOUT_MS，把它回的行拼进 out。
         * read_line 单行等待 50ms，保证窗口内能多次轮询 */
        t0 = lora_now_cycles();
        while (lora_elapsed_ms(t0) < SYS_LORA_AT_TIMEOUT_MS) {
            if (lora_read_line(line, sizeof(line), 50U) > 0U) {
                uint16_t L = (uint16_t)strlen(line);
                if ((uint32_t)used + L + 3U < out_size) {
                    memcpy(&out[used], line, L);
                    used = (uint16_t)(used + L);
                    out[used] = ' ';        /* 把多行折成一行，便于打印 */
                    used++;
                    out[used] = '\0';
                }
            }
            if (used > 0U) break;           /* 本条已拿到回复，问下一条 */
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
        return LORA_E22_ERR_PARAM;      /* 只支持 E22 认的那几档 */
    }

    /* 组 "AT+UART=<baud>,8,1,NON"：8 数据位 / 1 停止位 / 无校验 */
    {
        char num[12];
        uint8_t k = 0U;
        uint32_t v = baud;
        char tmp[12];
        uint8_t t = 0U;

        while (v > 0U && t < sizeof(tmp)) { tmp[t] = (char)('0' + (v % 10U)); v /= 10U; t++; }
        while (t > 0U) { num[k] = tmp[--t]; k++; }
        num[k] = '\0';

        /* 拼字符串（不用 snprintf：部分工程没开 microLIB 的浮点/格式化会很大） */
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

    /* 模块此刻已切到新波特率：本端必须立刻跟上，否则再也收不到它说话 */
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

    /* 复位后模块会自己重启射频，回 OK 后仍要等一会儿 */
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
