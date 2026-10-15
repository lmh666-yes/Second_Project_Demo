#ifndef __FWLIB_SYS_CLOCK_H
#define __FWLIB_SYS_CLOCK_H

#include "stm32f4xx.h"
#include <stddef.h>     /* NULL */

/* sys_clock.h:时钟源切换模块头文件
 *
 * 功能：运行时切换 SYSCLK 时钟源，三档：
 *     SYS_CLK_PLL  168MHz  PLL，默认档，上电时 SystemInit 已配好
 *     SYS_CLK_HSI   16MHz  内部 RC，无需外部晶振
 *     SYS_CLK_HSE    8MHz  外部晶振直连
 * 约束：切换改变整芯片运行速度，切换后以下模块必须重新 Init：
 *     sys_usart  波特率按新 PCLK 重算
 *     sys_tim    频率按新总线时钟重算，占空比先 SYS_TIM_PwmGetDuty 存，重 Init 后 SetDuty
 *     sys_i2c / sys_spi  内部速率按新 PCLK 换算
 *     sys_adc    ADCCLK 与采样时序变化，DMA 采集先停
 *     sys_wdg    仅 WWDG 受影响，按 PCLK1 换算；IWDG 用 LSI，不受影响
 *     delay.h 的 delay_ms  粗延时数值不再代表毫秒
 *     lcd        FSMC 时序按 HCLK 周期计，实际时间变化
 *     不受影响：sys_exti / sys_nvic / sys_dma / sys_fault / sys_flash
 * 依据：切换时内部先跳 HSI 过渡（SYS_CLK_SAFE_TRANSITION），再改 Flash latency
 *      与总线分频，避免高速运行中直接改分频导致总线异常；
 *      SysTick 重装已由 SYS_CLK_Switch() 内部完成，参照 sys_rtos.h 的
 *      SYS_RTOS_SYSTICK_RELOAD()（FreeRTOS 走 vPortSetupTimerInterrupt()，
 *      裸机走 SYS_TICK_Init()），调用方不要再调，否则 FreeRTOS 下会与内核抢 SysTick */


/* HSE_VALUE 由 Keil 工程宏定义（当前 8MHz）；HSI_VALUE 为芯片内部 RC 固定频率 */
#define SYS_CLK_FREQ_HSE   (HSE_VALUE)
#define SYS_CLK_FREQ_HSI   (HSI_VALUE)

/* 数值来源：STM32F407 数据手册；换其它型号先核对手册再改 */
#define SYS_CLK_MAX_HCLK   168000000UL
#define SYS_CLK_MAX_PCLK1   42000000UL
#define SYS_CLK_MAX_PCLK2   84000000UL

/* 1 = 先跳 HSI 低速源过渡（推荐保持）
 * 0 = 直接切，高速档之间切换存在总线风险 */
#define SYS_CLK_SAFE_TRANSITION  1

/* 等待 SWS 状态位确认目标源的最大循环次数（防硬件异常时死等） */
#define SYS_CLK_SWITCH_TIMEOUT   1000000UL

/* 等待 HSIRDY / PLLRDY 置位的最大循环次数。
 * 依据：RCC 外设时钟未开（漏调 RCC_AHB1PeriphClockCmd 或时钟树配错）时 HSIRDY
 *       永不置位，PLL 输入源 HSE 掉电或 PLL 配置寄存器值非法时 PLLRDY 永不置位，
 *       裸 while 会永久死循环在时钟初始化里；
 *       HSI 稳定约 2~5us、PLL 锁定约 100~200us，1000000 次循环
 *       （每次约 5 个周期 @16MHz，约 0.3us）余量足够 */
#define SYS_CLK_READY_TIMEOUT    1000000UL

/* 作为函数参数/返回值统一使用的时钟源编号 */
typedef enum {
    SYS_CLK_HSI = 0,        /* 16MHz 内部 RC */
    SYS_CLK_HSE = 1,        /* 8MHz 外部晶振 */
    SYS_CLK_PLL = 2,        /* 168MHz（PLL） */
} SysClkSrc_t;

#define SYS_CLK_OK           0      /* 切换成功 */
#define SYS_CLK_ERR_UNKNOWN  1      /* 目标时钟源非法 */
#define SYS_CLK_ERR_HSE      2      /* HSE 未起振（检查晶振电路） */
#define SYS_CLK_ERR_SW       3      /* 切换超时未完成 */
#define SYS_CLK_ERR_DIV      4      /* 分频值非法（不是 SPL 分频常量） */
#define SYS_CLK_ERR_RANGE    5      /* 预演频率超出总线上限（拒绝执行） */
#define SYS_CLK_ERR_LSI      6      /* LSI 未就绪（超时） */
#define SYS_CLK_ERR_LSE      7      /* LSE 未起振（查晶振/负载电容） */


/* 切换到目标时钟源
 * 参数 : target = SYS_CLK_HSI / SYS_CLK_HSE / SYS_CLK_PLL
 * 返回 : SYS_CLK_OK 或错误码 SYS_CLK_ERR_*
 * 注意 : 切换后软件延时不准；SysTick 已由本函数按新频率重装，
 *        调用方不要再调 SYS_TICK_Init() */
uint8_t     SYS_CLK_Switch  (SysClkSrc_t target);

/* 获取当前时钟源，读 SWS 状态位
 * 标准库 : RCC_GetSYSCLKSource */
SysClkSrc_t SYS_CLK_GetSource(void);

/* 获取当前 SYSCLK 频率（Hz），如 168000000 / 16000000 / 8000000 */
uint32_t    SYS_CLK_GetFreq (void);

/* 获取 PLL 档频率记录值（Hz）
 * 说明 : 首次切换前从 SystemCoreClock 捕获保存，切到其它时钟源后仍可查询 */
uint32_t    SYS_CLK_GetPllFreq(void);


/* 切到 PLL 168MHz；等价 SYS_CLK_Switch(SYS_CLK_PLL)，返回 SYS_CLK_OK 或错误码 */
uint8_t SYS_CLK_ToHighSpeed(void);

/* 切到 HSI 16MHz 低功耗档；等价 SYS_CLK_Switch(SYS_CLK_HSI)
 * 提醒 : 切换后软件延时不再准确；SysTick 已由内部重装，无需再 Init */
uint8_t SYS_CLK_ToLowPower(void);

/* 读回 HCLK / PCLK1 / PCLK2 频率（Hz），不需要的参数传 NULL
 * 用途 : 填时钟分配表、给 TIM/SPI/ADC 等外设分频换算做参考
 * 标准库 : RCC_GetClocksFreq（一次调用填满整个结构体） */
void SYS_CLK_GetBusFreq(uint32_t *hclk, uint32_t *pclk1, uint32_t *pclk2);

/* 自定义总线分频（AHB / APB1 / APB2），常量用 SPL 定义：
 *   hclk_div              = RCC_SYSCLK_Div1 / _Div2 / _Div4 / _Div8 / _Div16 /
 *                           _Div64 / _Div128 / _Div256 / _Div512
 *   pclk1_div / pclk2_div = RCC_HCLK_Div1 / _Div2 / _Div4 / _Div8 / _Div16（TIM 倍频自动生效）
 * 返回 : SYS_CLK_OK / ERR_DIV / ERR_RANGE / ERR_SW
 * 流程 : 按当前主频预演目标频率，超上限直接拒绝；通过后自动降 HSI、改分频、切回原源
 * 约束 : 之后再调用 SYS_CLK_Switch() 会按档位重置分频
 * 标准库 : RCC_HCLKConfig / RCC_PCLK1Config / RCC_PCLK2Config */
uint8_t SYS_CLK_SetBusDiv(uint32_t hclk_div, uint32_t pclk1_div, uint32_t pclk2_div);

/* LSE / LSI / RTC 时钟源，sys_rtc 依赖 */
/* 启动 LSE（外部 32.768kHz 晶振）并等起振
 * 返回 : SYS_CLK_OK / SYS_CLK_ERR_LSE（超时查晶振、负载电容、焊点）
 * 标准库 : RCC_LSEConfig(RCC_LSE_ON) + RCC_GetFlagStatus(RCC_FLAG_LSERDY) */
uint8_t SYS_CLK_LseOn(void);

/* LSE / LSI 就绪查询（1 = 已就绪） */
uint8_t SYS_CLK_LseReady(void);
uint8_t SYS_CLK_LsiReady(void);

/* 启动 LSI（内部低速 RC）并等就绪，带超时
 * 说明 : IWDG 可能已开 LSI，重复使能幂等
 * 标准库 : RCC_LSICmd + RCC_GetFlagStatus(RCC_FLAG_LSIRDY) */
uint8_t SYS_CLK_LsiOn(void);

/* 选择并启用 RTC 时钟，由 sys_rtc 的 SYS_RTC_Init 内部调用
 * 参数 : src = RCC_RTCCLKSource_LSE / _LSI / _HSE（标准库常量）
 * 标准库 : RCC_RTCCLKConfig + RCC_RTCCLKCmd(ENABLE) */
void SYS_CLK_RtcClkSelect(uint32_t src);


#endif /* __FWLIB_SYS_CLOCK_H */

