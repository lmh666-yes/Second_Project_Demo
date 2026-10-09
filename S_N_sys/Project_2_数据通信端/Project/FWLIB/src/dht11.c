#include "dht11.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_us 等）独立文件 */

/* ================================================================
 *  dht11.c —— DHT11 温湿度传感器（单总线）  实现文件
 * ================================================================
 *  协议速览（一次完整通信约 22ms）:
 *      ① 主机拉低 ≥18ms  → 释放
 *      ② 从机应答：80µs 低 + 80µs 高
 *      ③ 从机发 40 位：每位数 = 50µs 低 + 高电平（26~28µs=0 / 70µs=1）
 *      ④ 5 字节 = 湿度整数,湿度小数,温度整数,温度小数,校验和
 *
 *  实现要点 :
 *    ① 位宽测量用 DWT 微秒时间戳（delay.h 的 DWT_GetUs/ElapsedUs）——
 *       比"数循环次数"更抗主频变化，换主频/开优化都不用重标定；
 *    ② 每一步都带超时，传感器没插时**立刻返回**而不是死等；
 *    ③ 用"引脚电平"判断协议状态：开漏输出释放后由板上 10K 上拉拉高。
 * ================================================================ */


/* 上一次成功读取的时刻（ms 计数，用 DWT 微秒时间戳折算），0 = 从未读过 */
static uint32_t dht_last_us  = 0;
static uint8_t  dht_never    = 1U;


/* ================================================================
 *                    底层：引脚与等待
 * ================================================================ */
static void dq_low(void)  { GPIO_OutReset(DHT11_PORT, DHT11_PIN); }
static void dq_high(void) { GPIO_OutSet  (DHT11_PORT, DHT11_PIN); }  /* 释放总线 */
static uint8_t dq_read(void) { return GPIO_InRead(DHT11_PORT, DHT11_PIN); }

/* 等待引脚变成 level；返回等待耗时（µs）；超时返回 0xFFFFFFFF */
static uint32_t dht_wait_level(uint8_t level, uint32_t timeout_us)
{
    uint32_t t0 = DWT_GetUs();
    uint32_t dt;

    while (dq_read() != level) {
        dt = DWT_ElapsedUs(t0);
        if (dt > timeout_us) return 0xFFFFFFFFUL;
    }
    return DWT_ElapsedUs(t0);
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
void DHT11_Init(void)
{
    /* 开漏输出（GPIO_OType_OD，内部自动开时钟）；空闲释放，由板上 10K 上拉拉高 */
    GPIO_OutInitOD(DHT11_PORT, DHT11_PIN);
    dq_high();
    DHT11_ResetInterval();
}

void DHT11_ResetInterval(void)
{
    dht_never   = 1U;
    dht_last_us = 0;
}

uint16_t DHT11_TimeToNextRead(void)
{
    uint32_t dt_ms;

    if (dht_never) return 0U;

    dt_ms = DWT_ElapsedUs(dht_last_us) / 1000U;
    if (dt_ms >= DHT11_MIN_INTERVAL_MS) return 0U;

    return (uint16_t)(DHT11_MIN_INTERVAL_MS - dt_ms);
}

uint8_t DHT11_CheckSum(const uint8_t buf[5])
{
    uint8_t sum;

    if (buf == 0) return 0U;

    sum = (uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]);
    return (sum == buf[4]) ? 1U : 0U;
}

/* 读一帧 40 位（内部函数，不做间隔检查） */
static uint8_t dht_read_frame(uint8_t buf[5])
{
    uint8_t  i;
    uint8_t  j;
    uint8_t  byte;
    uint32_t high_us;

    /* ① 起始信号：拉低 20ms 后释放 */
    dq_low();
    delay_ms_dwt(DHT11_START_LOW_MS);
    dq_high();

    /* ② 等从机应答：先 80µs 低，再 80µs 高 */
    if (dht_wait_level(0, DHT11_TIMEOUT_US) == 0xFFFFFFFFUL) return 1U;  /* 等低电平超时 */
    if (dht_wait_level(1, DHT11_TIMEOUT_US) == 0xFFFFFFFFUL) return 1U;  /* 等高电平超时 */

    /* ★★ 关键一步：再等"应答高电平结束"（下降沿）★★
     * 此刻线才刚刚进入**第 0 位的 50µs 低电平**，位循环的第一步
     * "等电平变高" 才对得上。
     *
     * 少了这一等会怎样（这个 bug 曾经真实发生过）：
     *   上一句返回时线还是**高的**（正在应答脉冲中间），
     *   位循环第一次 dht_wait_level(1) 因为"线已经是高的"而**立刻返回**，
     *   紧接着量到的其实是应答高电平的**尾巴**（60~80µs > 45µs 阈值），
     *   于是**凭空多读出一个 '1'** ——
     *   整帧 40 位整体右移一位：buf[0] 最高位被强行置 1（恒 ≥128）、
     *   buf[4] 丢掉真正的最后一位。
     *   现象是：湿度读数恒在 128~255、校验和却"大约 2/3 概率假通过"，
     *   所以主程序看到的多数是"数据超范围"而不是"读取失败"。 */
    if (dht_wait_level(0, DHT11_TIMEOUT_US) == 0xFFFFFFFFUL) return 1U;

    /* ③ 收 40 位，每 8 位组成一个字节（**高位在前**） */
    for (i = 0; i < 5U; i++) {
        byte = 0;
        for (j = 0; j < 8U; j++) {
            /* 每位以 50µs 低电平开始 */
            if (dht_wait_level(1, DHT11_TIMEOUT_US) == 0xFFFFFFFFUL) return 1U;

            /* 高电平宽度决定该位是 0 还是 1 */
            high_us = dht_wait_level(0, DHT11_TIMEOUT_US);
            if (high_us == 0xFFFFFFFFUL) return 1U;

            byte = (uint8_t)(byte << 1);
            if (high_us > DHT11_BIT_THRESHOLD_US) byte |= 0x01U;
        }
        buf[i] = byte;
    }

    /* ④ 校验 */
    return DHT11_CheckSum(buf) ? 0U : 2U;
}

uint8_t DHT11_ReadRaw(uint8_t buf[5])
{
    uint8_t r = 2U;
    uint8_t k;

    if (buf == 0) return 2U;

    /* 间隔检查：DHT11 采样周期 ≥1s，太密直接拒绝（读到的会是脏数据） */
    if (DHT11_TimeToNextRead() != 0U) return 3U;

    for (k = 0; k < DHT11_RETRY; k++) {
        r = dht_read_frame(buf);
        if (r == 0U) {
            /* 记下本次成功读取的时刻，用于下一次的间隔检查 */
            dht_last_us = DWT_GetUs();
            dht_never   = 0U;
            return 0U;
        }
        if (r == 1U) break;          /* 无应答：重试也没用，直接返回 */
        delay_ms_dwt(50);            /* 校验失败：稍等再试 */
    }

    return r;
}

uint8_t DHT11_ReadInt(uint8_t *temp_c, uint8_t *humi_pct)
{
    uint8_t buf[5];
    uint8_t r = DHT11_ReadRaw(buf);

    if (r != 0U) return r;

    if (humi_pct != 0) *humi_pct = buf[0];
    if (temp_c   != 0) *temp_c   = buf[2];
    return 0U;
}

uint8_t DHT11_ReadC10(int16_t *temp_c10, int16_t *humi_c10)
{
    uint8_t buf[5];
    uint8_t r = DHT11_ReadRaw(buf);

    if (r != 0U) return r;

    /* DHT11 只有整数位：小数位固定 0，所以 ×10 即可；
     * 若换 DHT22，这里应改成 (整数 << 8 | 小数) 的解析方式 */
    if (humi_c10 != 0) *humi_c10 = (int16_t)((int16_t)buf[0] * 10);
    if (temp_c10 != 0) *temp_c10 = (int16_t)((int16_t)buf[2] * 10);
    return 0U;
}
