#include "sys_dht11.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */

#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_us 等）独立文件 */

/* ================================================================
 *  sys_dht11.c —— 【板载】温湿度传感器 DHT11  实现文件
 * ================================================================
 *  时序全景(一次读 = 约 5ms;数值单位: us):
 *    主机: 拉低 18ms → 释放 → 转输入等应答
 *    从机: 拉低 80 → 拉高 80 → 然后 40 位数据
 *    每位: 低 50 + 高 26~28(表示 0) / 高 70(表示 1)
 *    数据: 湿度整数/小数 + 温度整数/小数 + 校验和(= 前 4 字节之和)
 *
 *  实现要点 :
 *    ① 电平变化用 DWT 周期计数器"掐表"(delay_us 同一套内核硬件),
 *       26us 与 70us 差距巨大,50us 阈值判定稳;
 *    ② 起始 18ms 延时不用关中断;真正关中断的只有 应答+40 位 的
 *       ~4.5ms 窗口(宏 SYS_DHT11_LOCK_IRQ 可关掉该行为);
 *    ③ 空闲态 = 输入+上拉(总线释放);只有"拉低起始信号"才切成
 *       开漏输出(GPIO_OType_OD)——这样板载不带外部上拉也能工作
 * ================================================================ */


/* ================================================================
 *                    内部状态
 * ================================================================ */
static GPIO_TypeDef *s_port = 0;    /* 0 = 未初始化 */
static uint16_t      s_pin  = 0;

/* 超时哨兵(周期数不可能是这个值) */
#define DHT11_TIMEOUT   0xFFFFFFFFUL


/* ================================================================
 *                    基础功能
 * ================================================================ */
void SYS_DHT11_Init(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == 0) return;

    s_port = port;
    s_pin  = pin;

    /* 空闲态:输入 + 上拉(总线释放,等待器件与主机发声) */
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

    delay_us(1);                        /* 顺带确保 DWT 计时已使能(首次自动开) */
    thresh = (SystemCoreClock / 1000000U) * 50U;        /* 50us 换算成周期数 */

    /* ① 起始信号:拉低 18ms 后释放(此段不关中断) */
    GPIO_OutInitOD(s_port, s_pin);
    GPIO_OutWrite(s_port, s_pin, 0U);
    delay_ms(18);
    GPIO_OutWrite(s_port, s_pin, 1U);   /* 开漏输出(GPIO_OType_OD)高 = 释放总线 */

    /* ② 进入微秒时序窗口(约 4.5ms):默认关中断保时序 */
#if SYS_DHT11_LOCK_IRQ
    irq_save = __get_PRIMASK();
    __disable_irq();
#endif

    GPIO_InInit(s_port, s_pin, GPIO_PuPd_UP);      /* 转输入,等器件应答 */

    /* ③ 应答:先低 80us、再高 80us */
    if (dht11_wait(0U, 200U) == DHT11_TIMEOUT)      ret = -1;
    else if (dht11_wait(1U, 200U) == DHT11_TIMEOUT) ret = -1;

    /* ④ 40 位数据:每位"低 50us + 高 26/70us",量高电平长度判 0/1 */
    if (ret == 0) {
        for (i = 0U; i < 40U; i++) {
            uint32_t high_cycles;

            if (dht11_wait(1U, 200U) == DHT11_TIMEOUT) { ret = -1; break; } /* 等上升沿 */
            high_cycles = dht11_wait(0U, 200U);                             /* 量高电平 */
            if (high_cycles == DHT11_TIMEOUT) { ret = -1; break; }

            data[i >> 3] = (uint8_t)(data[i >> 3] << 1);
            if (high_cycles > thresh) data[i >> 3] |= 1U;
        }
    }

#if SYS_DHT11_LOCK_IRQ
    __set_PRIMASK(irq_save);
#endif
    GPIO_InInit(s_port, s_pin, GPIO_PuPd_UP);      /* 恢复空闲态 */

    /* ⑤ 校验和 + 输出换算(整数 + 小数/10) */
    if (ret == 0) {
        sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
        if (sum != data[4]) ret = -2;
    }
    if (ret == 0) {
        if (humi_rh) *humi_rh = (float)data[0] + (float)data[1] * 0.1f;
        if (temp_c)  *temp_c  = (float)data[2] + (float)data[3] * 0.1f;
    }
    return ret;
}
