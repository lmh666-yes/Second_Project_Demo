#ifndef __FWLIB_DS18B20_H
#define __FWLIB_DS18B20_H

#include "stm32f4xx.h"

/* DS18B20 数字温度传感器（单总线 1-Wire），器件级驱动
 * 依赖 gpio_core.h：开漏输出 + DWT 微秒延时
 * 接线：DQ = PG9（J6 接口，网络名 DS18B20，板上已有 10K 上拉电阻）
 * 该接口与 DHT11 共用同一根线，二选一，不能同时插两个传感器
 * 移植：换引脚只改 DS18B20_PORT / DS18B20_PIN */


#define DS18B20_PORT        GPIOG
#define DS18B20_PIN         GPIO_Pin_9

/* 复位脉冲宽度（µs）：手册要求 >480，取 500 留余量 */
#define DS18B20_RESET_US    500
/* 等待存在脉冲前的释放时间（µs）：手册要求 15~60 */
#define DS18B20_RELEASE_US  30
/* 存在脉冲判定窗口（µs）：器件应在 60~240µs 内把总线拉低 */
#define DS18B20_PRESENCE_US 240

/* 分辨率：9 = 0.5℃/93.75ms；10 = 0.25℃/187.5ms；
 *         11 = 0.125℃/375ms；12 = 0.0625℃/750ms（默认，最准） */
#define DS18B20_RESOLUTION  12

/* 一次"转换 + 读取"的等待毫秒数（按分辨率自动算，留 10% 余量） */
#define DS18B20_CONV_MS_9   100
#define DS18B20_CONV_MS_10  210
#define DS18B20_CONV_MS_11  420
#define DS18B20_CONV_MS_12  800

/* 编译期自检 */
#if (DS18B20_RESOLUTION < 9) || (DS18B20_RESOLUTION > 12)
#error "DS18B20_RESOLUTION must be 9..12"
#endif


/* 初始化：DQ 配成开漏输出（GPIO_OType_OD，空闲释放为高阻，靠板上上拉拉高）
 * 返回：1 = 总线上检测到器件；0 = 未检测到（查接线 / 上拉 / 供电） */
uint8_t DS18B20_Init(void);

/* 复位总线并检测存在脉冲：1 = 有器件应答，0 = 无 */
uint8_t DS18B20_IsPresent(void);

/* 启动一次温度转换（SKIP ROM + CONVERT T），非阻塞
 * 启动后需按转换时间等待，见 DS18B20_CONV_MS_* */
uint8_t DS18B20_StartConvert(void);

/* 读暂存器 9 字节（sp[0..8]；sp[0]/sp[1] 是温度，sp[8] 是 CRC）
 * 返回 : 0 = 成功；1 = 无器件；2 = CRC 校验失败 */
uint8_t DS18B20_ReadScratch(uint8_t sp[9]);

/* 读温度（原始格式：1 个 LSB = 1/16 ℃，有符号）
 * 返回：0 = 成功；1/2 = 同 ReadScratch */
uint8_t DS18B20_ReadTempRaw(int16_t *raw16);

/* 一站式读温度（0.1℃ 单位，已四舍五入）：启动转换 → 按分辨率等待 → 读回
 * 返回：0 = 成功；1 = 无器件；2 = 读失败
 * 阻塞：按 DS18B20_RESOLUTION 阻塞约 100~800ms */
uint8_t DS18B20_ReadTempC10(int16_t *t_c10);

/* 取上次成功读到的温度（0.1℃），不发起新转换（不阻塞） */
int16_t DS18B20_GetLastTempC10(void);


/* 修改分辨率（写暂存器第 5 字节的 bit6:5；掉电不保存）
 * 参数：bits = 9~12；结果需同步改 DS18B20_RESOLUTION 宏，否则等待时间不符 */
uint8_t DS18B20_SetResolution(uint8_t bits);

/* 写暂存器（TH / TL / 分辨率），一般用于设置报警阈值 */
uint8_t DS18B20_WriteScratch(uint8_t th, uint8_t tl);

/* 底层原语，多器件 ROM 搜索等高级用法可直接调用
 * 时序均基于 DWT 微秒级延时，见 delay.h 的 delay_us */
void    DS18B20_WriteByte(uint8_t byte);
uint8_t DS18B20_ReadByte(void);
void    DS18B20_WriteBit(uint8_t bit);
uint8_t DS18B20_ReadBit(void);

#endif /* __FWLIB_DS18B20_H */
