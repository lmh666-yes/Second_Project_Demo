#ifndef __FWLIB_EXT_IO_H
#define __FWLIB_EXT_IO_H

#include "stm32f4xx.h"

/* 单引脚数字输入模块：红外接收头、电容触摸按键、光敏数字量、WiFi 模块按键
 * 芯片平台 STM32F407ZGT6（标准外设库 StdPeriph），依赖 gpio_core.h
 * 需要协议或时序的器件（DS18B20 单总线、WS2812、OLED、MPU6050 等）另建驱动文件
 *
 * 初始化分两层，顺序不能颠倒：
 *   1) EXT_IO_Init()  打底，所有外接引脚统一设为输入 + EXT_BASE_PULL，引脚不悬空
 *   2) EXT_XXX_Init() 覆盖，只给需要特殊配置的模块做设置，默认态已满足时留空
 *
 * 引脚表在 ext_io.c，改端口或引脚改 .c；本头文件只放数量、极性与打底上下拉
 * J8 为共享模拟输入排针：R_ADC / STM_ADC(PA5) / P_TOUCH / STM_DAC(PA4) / TAD1
 * PA8 与 PA4 分别与摄像头接口 DCMI_XCLK、DCMI_HREF 共用，插上摄像头模块后不可用 */


/* 定义与宏定义区（换板子只改这里） */
/* 触发极性：1 = 低电平有效（检测到 → 引脚为低），0 = 高电平有效
 * 多数模块有信号时输出低；读到的结果与实际相反时翻转对应开关即可 */
#define EXT_IR_ACTIVE_LOW     1   /* 红外接收头 PA8（IRED）：无载波输出高，有载波输出低 */
#define EXT_KEY_ACTIVE_LOW    1   /* WiFi 模块按键 PF6（W_KEY）：按下接地 */
/* 外接红外避障模块：有障碍时输出低；读出结果相反时改为 0 */
#define EXT_OBS_ACTIVE_LOW    1

/* 下面两个宏仅在改用数字读时有意义：光敏 PF7 是模拟量，数字读只能得到粗阈值
 * 触摸 PA5 当数字读无效。保留宏用于外接数字型光敏模块或数字触摸模块的场合 */
#define EXT_LIGHT_ACTIVE_LOW  1   /* 板载光敏 PF7：光越强分压越低 */
#define EXT_TOUCH_ACTIVE_LOW  1   /* 数字型触摸模块：触摸时拉低，本板不适用 */

/* 触摸工作方式：1 = 电容充电时间法，本板 P_TOUCH 配 1M 上拉，只能用这个
 * 0 = 普通数字电平读，仅用于外接数字输出型触摸模块 */
#define EXT_TOUCH_MODE_CHARGE  1

/* 触摸判定阈值（µs）：充电时间超过基准 + 本值判为触摸到
 * 实测参考（3.3V / 1M，无触摸）8~12µs；手指按下 20~40µs */
#define EXT_TOUCH_DELTA_US     15

/* 单次充电测量的超时上限（µs）：引脚短路或损坏时避免死等 */
#define EXT_TOUCH_TIMEOUT_US   2000

/* 打底上下拉：所有外接引脚在打底时统一设置，决定无信号时的静止电平
 * 模块低有效（ACTIVE_LOW=1）配 GPIO_PuPd_UP（1），无信号时引脚拉高，判为未检测到
 * 模块高有效（ACTIVE_LOW=0）配 GPIO_PuPd_DOWN（2）
 * 取值：0 = GPIO_PuPd_NOPULL（浮空）, 1 = GPIO_PuPd_UP（上拉）, 2 = GPIO_PuPd_DOWN（下拉） */
#define EXT_BASE_PULL  1

/* 每类模块的检测路数：合法 id 为 0 ~ 数量-1
 * 改动后须同步 ext_io.c 中对应引脚表的项数 */
#define EXT_IR_COUNT    1
#define EXT_TOUCH_COUNT 1
#define EXT_LIGHT_COUNT 1
#define EXT_KEY_COUNT   1
/* 外接红外避障模块路数（4WD 小车：前左/前右/左/右），引脚见 ext_io.c */
#define EXT_OBS_COUNT   4


/* 基础功能 */
/* 第一层：打底，把四类模块引脚统一配置为输入 + EXT_BASE_PULL
 * 必须在各 EXT_XXX_Init() 之前调用
 * 经 gpio_core 使能时钟后调用 GPIO_Init（输入 + 上下拉） */
void    EXT_IO_Init(void);

/* 红外接收头（id：0 ~ EXT_IR_COUNT-1），引脚 PA8（IRED） */

/* 第二层：覆盖配置，默认态已满足时函数体留空
 * 需要特殊配置时在 ext_io.c 对应函数内追加，在打底之后执行 */
void    EXT_IR_Init   (void);

/* 读检测结果：1 = 检测到，0 = 未检测到或 id 越界
 * 触发极性按 EXT_IR_ACTIVE_LOW 适配；即时读取，不含消抖
 * 解码红外遥控需配合 sys_exti 与定时器实现
 * 经 gpio_core 调用 GPIO_ReadInputDataBit 读 IDR，以下各 Detected 同 */
uint8_t EXT_IR_Detected(uint8_t id);

/* 电容触摸按键（id：0 ~ EXT_TOUCH_COUNT-1），引脚 PA5（STM_ADC） */
void    EXT_TOUCH_Init   (void);
/* 读触摸检测结果：1 = 触摸到，0 = 未触摸或 id 越界 */
uint8_t EXT_TOUCH_Detected(uint8_t id);

#if (EXT_TOUCH_MODE_CHARGE)
/* 单次充电测量的充电时间（µs），用于确定 EXT_TOUCH_DELTA_US 阈值 */
uint32_t EXT_TOUCH_ChargeTimeUs(uint8_t id);

/* 重新校准：把当前状态作为未触摸基准，上电稳定后调用一次
 * 不调用时 EXT_TOUCH_Detected 首帧会自动校准 */
void     EXT_TOUCH_Calibrate(uint8_t id);

/* 取当前基准值（µs） */
uint32_t EXT_TOUCH_GetBaseline(uint8_t id);
#endif

/* 板载光敏（id：0 ~ EXT_LIGHT_COUNT-1），引脚 PF7（LIGHT）
 * PF7 是 47K 上拉与光敏电阻到地的分压点，数字比较输出 LSENS 未引到 MCU
 * 与 sys_adc 的 SYS_ADC_LIGHT_* 是同一根引脚：本模块按数字阈值用，亮暗两态
 * 精确光照值用 sys_adc（PF7 = ADC3_IN5）；两者都会改引脚配置，只能用其中一种 */
void    EXT_LIGHT_Init   (void);
/* 读光敏检测结果：1 = 光照超过阈值，0 = 未超过或 id 越界 */
uint8_t EXT_LIGHT_Detected(uint8_t id);

/* WiFi 模块按键（id：0 ~ EXT_KEY_COUNT-1），引脚 PF6（W_KEY） */
void    EXT_KEY_Init     (void);
/* 读按键检测结果：1 = 按下，0 = 未按下或 id 越界 */
uint8_t EXT_KEY_Detected(uint8_t id);

/* 外接红外避障模块（id：0 ~ EXT_OBS_COUNT-1），与板载红外接收头 EXT_IR 是两种器件
 * 默认引脚（在 ext_io.c 的 ext_obs_list 里改）:
 *   id 0 → PC10（P2-24）  id 1 → PC11（P2-23）
 *   id 2 → PC12（P2-22）  id 3 → PC8 （P2-32）
 * 接线 : 每个模块 VCC→3.3V 或 5V（看模块规格）、GND→GND、OUT→对应引脚
 * 输入已配成上拉（EXT_BASE_PULL），模块未接时读为无障碍
 * 模块电位器需在实际路面上调定：黑色地面吸红外，桌面上调好的阈值在地面可能不触发 */
void    EXT_OBS_Init     (void);
/* 读避障检测结果：1 = 该方向有障碍，0 = 无障碍或 id 越界 */
uint8_t EXT_OBS_Detected(uint8_t id);


/* 扩展功能 */
/* 统计检测到的路数，返回 0 ~ EXT_XXX_COUNT；多路输入时判断有几路同时有效
 * 内部逐路调用对应的 Detected（GPIO_ReadInputDataBit） */
uint8_t EXT_IR_CountDetected   (void);
uint8_t EXT_TOUCH_CountDetected(void);
uint8_t EXT_LIGHT_CountDetected(void);
uint8_t EXT_KEY_CountDetected  (void);
uint8_t EXT_OBS_CountDetected  (void);

#endif /* __FWLIB_EXT_IO_H */
