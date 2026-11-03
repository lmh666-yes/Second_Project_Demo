#include "sys_dht11.h"
/* 接口说明见 sys_dht11.h;本文件为实现层 */

#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_us 等）独立文件 */

/* ================================================================
 *  sys_dht11.c: 板载 DHT11 温湿度传感器实现
 *  时序(us): 主机拉低 18ms 后释放并转输入;
 *    从机拉低 80 → 拉高 80 → 40 位数据;
 *    每位 = 低 50 + 高 26~28(表示 0)/70(表示 1);
 *    数据 = 湿度整数/小数 + 温度整数/小数 + 校验和(前 4 字节之和)。
 *  电平计时用 DWT 周期计数器, 50us 阈值区分 26us 与 70us。
 *  关中断只覆盖应答 + 40 位窗口(宏 SYS_DHT11_LOCK_IRQ 可关闭);
 *  该窗口最坏为 41 次电平等待, 每次超时上限 100us, 约 4.1ms。
 *  空闲态 = 输入 + 上拉; 仅起始信号期间为开漏输出 GPIO_OType_OD。
 * ================================================================ */


/* ==================== 内部状态 ==================== */
static GPIO_TypeDef *s_port = 0;    /* 0 = 未初始化 */
static uint16_t      s_pin  = 0;

/* 最近一次读到的 5 个原始字节, 见 SYS_DHT11_GetRaw */
static uint8_t       s_raw[5];

/* 超时哨兵(周期数不可能是这个值) */
#define DHT11_TIMEOUT   0xFFFFFFFFUL


/* ==================== 基础功能 ==================== */
void SYS_DHT11_Init(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == 0) return;

    s_port = port;
    s_pin  = pin;

    /* 空闲态: 输入 + 上拉(释放总线) */
    GPIO_InInit(port, pin, GPIO_PuPd_UP);
}

/* 等引脚变成指定电平;返回等待用掉的 CPU 周期数,超时返回 DHT11_TIMEOUT */
static uint32_t dht11_wait(uint8_t level, uint32_t timeout_us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t limit = timeout_us * (SystemCoreClock / 1000000U);

    while (GPIO_InRead(s_port, s_pin) != level) {
        if ((DWT->CYCCNT - start) > limit) return DHT11_TIMEOUT;
    }
    return (DWT->CYCCNT - start);
}

int SYS_DHT11_Read(float *temp_c, float *humi_rh)
{
    uint8_t  data[5];
    uint8_t  i;
    uint8_t  sum;
    uint32_t thresh;                    /* "位=1"的高电平阈值(周期数) */
    int      ret = 0;

#if SYS_DHT11_LOCK_IRQ
    uint32_t irq_save;
#endif

    if (s_port == 0) return -1;

    for (i = 0U; i < 5U; i++) data[i] = 0U;

    delay_us(1);                        /* 确保 DWT 周期计数器已使能 */
    thresh = (SystemCoreClock / 1000000U) * 50U;        /* 50us 换算成周期数 */

    /* 起始信号: 拉低 18ms 后释放(不关中断) */
    GPIO_OutInitOD(s_port, s_pin);
    GPIO_OutWrite(s_port, s_pin, 0U);
    delay_ms(18);
    GPIO_OutWrite(s_port, s_pin, 1U);   /* 开漏输出(GPIO_OType_OD)高 = 释放总线 */

    /* 时序窗口(约 4.5ms): 默认关中断 */
#if SYS_DHT11_LOCK_IRQ
    irq_save = __get_PRIMASK();
    __disable_irq();
#endif

    GPIO_InInit(s_port, s_pin, GPIO_PuPd_UP);      /* 转输入,等器件应答 */

    /* 应答: 先低 80us, 再高 80us。
     * 必须连续三次等待: wait(0) 停在应答低电平起点; 此时引脚已为高,
     * wait(1) 未消耗边沿即返回; 再由 wait(0) 吃掉应答高电平,
     * 停在 bit0 低电平起点。
     * 少一次等待时 bit0 会量到应答高剩余 ~80us(> 50us 阈值)被判为 1,
     * 其后各位错位, 校验和恒不成立, 本函数固定返回 -2。
     * 超时取 100us: 手册中任何一段电平不超过 80us, 尚留 25% 余量;
     * 该段为关中断窗口, 最坏 41 × 100us ≈ 4.1ms。 */
    if (dht11_wait(0U, 100U) == DHT11_TIMEOUT)      ret = -1;   /* 应答低电平起点 */
    else if (dht11_wait(1U, 100U) == DHT11_TIMEOUT) ret = -1;   /* 应答高电平起点 */
    else if (dht11_wait(0U, 100U) == DHT11_TIMEOUT) ret = -1;   /* 吃掉应答高，停在 bit0 低电平起点 */

    /* 40 位数据: 每位 低 50us + 高 26/70us, 量高电平长度判 0/1 */
    if (ret == 0) {
        for (i = 0U; i < 40U; i++) {
            uint32_t high_cycles;

            if (dht11_wait(1U, 100U) == DHT11_TIMEOUT) { ret = -1; break; } /* 等上升沿 */
            high_cycles = dht11_wait(0U, 100U);                             /* 量高电平 */
            if (high_cycles == DHT11_TIMEOUT) { ret = -1; break; }

            data[i >> 3] = (uint8_t)(data[i >> 3] << 1);
            if (high_cycles > thresh) data[i >> 3] |= 1U;
        }
    }

#if SYS_DHT11_LOCK_IRQ
    __set_PRIMASK(irq_save);
#endif
    GPIO_InInit(s_port, s_pin, GPIO_PuPd_UP);      /* 恢复空闲态 */

    /* 校验和 + 输出换算(整数 + 小数/10) */
    if (ret == 0) {
        sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
        if (sum != data[4]) ret = -2;
    }

    /* 保留 5 个原始字节供 SYS_DHT11_GetRaw() 读取。
     * 返回 -2(校验失败)时打印这 5 字节, 可区分"某位采错"与"全无采样"(全 0x00/全 0xFF)。 */
    for (i = 0U; i < 5U; i++) s_raw[i] = data[i];

    if (ret == 0) {
        if (humi_rh) *humi_rh = (float)data[0] + (float)data[1] * 0.1f;
        if (temp_c)  *temp_c  = (float)data[2] + (float)data[3] * 0.1f;
    }
    return ret;
}

/* 取回最近一次读到的 5 个原始字节: 湿度整数/小数, 温度整数/小数, 校验和 */
void SYS_DHT11_GetRaw(uint8_t raw[5])
{
    uint8_t i;

    if (raw == 0) return;
    for (i = 0U; i < 5U; i++) raw[i] = s_raw[i];
}

int SYS_DHT11_ReadRetry(float *temp_c, float *humi_rh, uint8_t retries)
{
    uint8_t  n;
    int      r  = -1;
    uint8_t  saw_any = 0U;      /* 是否有过一次"有响应但校验失败" */

    if (retries == 0U) retries = 1U;

    for (n = 0U; n < retries; n++) {
        r = SYS_DHT11_Read(temp_c, humi_rh);
        if (r == 0) return 0;

        /* -2 = 器件有回应但未过校验, 其重读命中率高于 -1(无回应) */
        if (r == -2) saw_any = 1U;

        /* 器件采样率 1Hz: 两次尝试之间必须留间隔, 连续读只会得到同一份
         * 陈旧数据(DHT11 两次内部转换之间返回上一次结果)。此处退避 20ms。 */
        if ((uint8_t)(n + 1U) < retries) delay_ms(20);
    }

    /* 全失败: 区分"硬件未接"(无任何回应) 与"信号质量差" */
    return (saw_any != 0U) ? -2 : -1;
}
