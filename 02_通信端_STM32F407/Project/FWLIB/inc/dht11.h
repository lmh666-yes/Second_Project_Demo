#ifndef __FWLIB_DHT11_H
#define __FWLIB_DHT11_H

#include "stm32f4xx.h"

/* dht11.h: 板载 DHT11 温湿度传感器（单总线）头文件
 * 器件级驱动：读一次温湿度 + 读原始帧
 * 依赖 gpio_core.h + delay.h（开漏输出 + delay_us / DWT_GetUs 测脉宽）
 *
 * 天马 F407 开发板接线（普中-天马 F407开发板原理图）：
 *     DQ = PG9（J6 接口，网络名 DHT11/DS18B20；板上已有 10K 上拉）
 *     与 DS18B20 共用同一根线，二选一，不能同时插两个
 *
 * DHT11 限制：
 *     1) 采样周期 ≥ 1 秒，上电后还要等 1 秒才稳定；驱动用 DHT11_MIN_INTERVAL_MS 拦住过密读取，
 *        过密直接返回间隔不足，不会读到脏数据
 *     2) 精度为整数：小数位字节恒为 0，只用整数位；要小数需换 DHT22/AM2302（16 位编码，解析方式不同）
 *     3) 可靠量程：湿度 20~90%RH、温度 0~50℃（数据手册标称）；超范围读数不可信，
 *        应用层需校验通过 + 量程检查后再使用
 *     4) 时序对中断敏感：读的 4ms 内被高频中断打断可能校验失败，驱动已内置重试
 *
 * 移植：换引脚只改 DHT11_PORT / DHT11_PIN */


/* 定义与宏定义区：换板子只改这里 */
#define DHT11_PORT      GPIOG
#define DHT11_PIN       GPIO_Pin_9

/* 起始信号：拉低 ≥18ms（手册要求），这里给 20ms 留余量 */
#define DHT11_START_LOW_MS  20
/* 读失败自动重试次数 */
#define DHT11_RETRY         3
/* 两次读取的最小间隔（ms）：DHT11 手册要求 ≥1000ms */
#define DHT11_MIN_INTERVAL_MS  1100

/* 时序判定的容差（µs）：位=1 的高电平 ≈70µs，位=0 ≈26µs，取 45µs 做分界 */
#define DHT11_BIT_THRESHOLD_US 45
/* 等待电平翻转的超时（µs）：防止传感器没接时死等 */
#define DHT11_TIMEOUT_US       200


/* 基础功能 */
/* 初始化：DQ 配成开漏输出（GPIO_OType_OD，空闲释放），不发起通信
 * 上电后需 delay_ms(1000) 再读第一次 */
void DHT11_Init(void);

/* 读一次温湿度（整数版）
 * 参数 : temp_c   输出：温度 ℃（0~50），可传 0 表示不关心
 *        humi_pct 输出：相对湿度 %（0~100），可传 0
 * 返回 : 0 = 成功
 *        1 = 传感器无应答（没插 / 接线松 / 上拉缺失）
 *        2 = 校验和错误（时序被干扰，重试通常就好）
 *        3 = 读取间隔不足（< DHT11_MIN_INTERVAL_MS，等一会再来）
 * 阻塞 : 单次约 22ms */
uint8_t DHT11_ReadInt(uint8_t *temp_c, uint8_t *humi_pct);

/* 读原始 5 字节帧：buf[0]=湿度整数 buf[1]=湿度小数 buf[2]=温度整数
 *                  buf[3]=温度小数 buf[4]=校验和
 * 标准 DHT11 的 buf[1]/buf[3] 恒为 0；读到非 0 值可判定为 DHT22(AM2302) 或仿制芯片，解析方式要另换
 * 返回 : 同 DHT11_ReadInt */
uint8_t DHT11_ReadRaw(uint8_t buf[5]);

/* 距上次成功读取过去了多少毫秒后可以再读（>0 = 还需等待，0 = 可以读） */
uint16_t DHT11_TimeToNextRead(void);


/* 扩展功能 */
/* 读温度/湿度，输出 0.1 单位（DHT11 小数位恒为 0） */
uint8_t DHT11_ReadC10(int16_t *temp_c10, int16_t *humi_c10);

/* 校验和自检：把 5 字节帧的第 5 字节与前 4 字节之和比较
 * 返回 : 1 = 校验通过；0 = 不通过（buf 为 0 时返回 0） */
uint8_t DHT11_CheckSum(const uint8_t buf[5]);

/* 清掉"上次读取时间"记录，让下一次读取不必等间隔
 * 上电初始化后想立刻读一次时用（DHT11 本身仍需上电稳定 1s） */
void DHT11_ResetInterval(void);

#endif /* __FWLIB_DHT11_H */
