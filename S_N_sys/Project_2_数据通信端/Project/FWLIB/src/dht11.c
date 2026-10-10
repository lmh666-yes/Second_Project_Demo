#include "dht11.h"
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_us 等）独立文件 */

/* dht11.c: DHT11 温湿度传感器（单总线）实现，一次完整通信约 22ms
 *   1) 主机拉低 ≥18ms 后释放
 *   2) 从机应答：80µs 低 + 80µs 高
 *   3) 从机发 40 位：每位数 = 50µs 低 + 高电平（26~28µs=0 / 70µs=1）
 *   4) 5 字节 = 湿度整数,湿度小数,温度整数,温度小数,校验和
 * 位宽用 DWT 微秒时间戳（delay.h 的 DWT_GetUs/ElapsedUs）测量，换主频不用重标定
 * 每步带超时，传感器未接时立即返回；开漏输出释放后由板上 10K 上拉拉高 */


/* 上一次成功读取的 DWT 微秒时间戳，0 = 从未读过 */
static uint32_t dht_last_us  = 0;
static uint8_t  dht_never    = 1U;


/* 底层：引脚与等待 */
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


/* 基础功能 */
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

    /* 起始信号：拉低 20ms 后释放 */
    dq_low();
    delay_ms_dwt(DHT11_START_LOW_MS);
    dq_high();

    /* 等从机应答：先 80µs 低，再 80µs 高 */
    if (dht_wait_level(0, DHT11_TIMEOUT_US) == 0xFFFFFFFFUL) return 1U;  /* 等低电平超时 */
    if (dht_wait_level(1, DHT11_TIMEOUT_US) == 0xFFFFFFFFUL) return 1U;  /* 等高电平超时 */

    /* 再等应答高电平结束（下降沿）：此后线才进入第 0 位的 50µs 低电平
     * 缺这一步时位循环首个 dht_wait_level(1) 因线已为高而立即返回，量到的是应答高电平尾部
     * （60~80µs > 45µs 阈值），整帧 40 位右移一位：buf[0] 最高位恒为 1，buf[4] 丢最后一位 */
    if (dht_wait_level(0, DHT11_TIMEOUT_US) == 0xFFFFFFFFUL) return 1U;

    /* 收 40 位，每 8 位组成一个字节，高位在前 */
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

    /* 校验和比对 */
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

    /* DHT11 只有整数位，小数位固定 0，×10 即可 */
    if (humi_c10 != 0) *humi_c10 = (int16_t)((int16_t)buf[0] * 10);
    if (temp_c10 != 0) *temp_c10 = (int16_t)((int16_t)buf[2] * 10);
    return 0U;
}
