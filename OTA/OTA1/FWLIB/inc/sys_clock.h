#ifndef __FWLIB_SYS_CLOCK_H
#define __FWLIB_SYS_CLOCK_H

#include "stm32f4xx.h"
#include <stddef.h>     /* NULL（配置表查找失败返回空指针，sys_clock.c 使用） */

/* ================================================================
 *  sys_clock.h — 【系统】时钟源切换模块  头文件
 *  运行时切换 SYSCLK，三档：SYS_CLK_PLL 168MHz（默认档，上电 SystemInit
 *  已配好）/ SYS_CLK_HSI 16MHz（内部 RC）/ SYS_CLK_HSE 8MHz（外部晶振）。
 *  切换约束：
 *    - Flash 等待周期与总线分频按"先降速→改配置→再升速"处理，默认经 HSI
 *      过渡（SYS_CLK_SAFE_TRANSITION）。
 *    - SysTick 已按新频率重装；SystemCoreClock 由 CMSIS 更新。
 *    - 软件空循环延时失效，delay.h 需重新标定。切换后需重新 Init 的模块
 *      为时钟依赖型外设；不受影响：sys_exti / sys_nvic / sys_dma /
 *      sys_fault / sys_flash、用 LSI 的 IWDG。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* -------------------- 时钟频率 -------------------- */
/* HSE_VALUE 在 Keil 工程宏中定义（当前 8MHz），HSI_VALUE 固定 16MHz */
#define SYS_CLK_FREQ_HSE   (HSE_VALUE)
#define SYS_CLK_FREQ_HSI   (HSI_VALUE)

/* -------------------- 总线频率上限 -------------------- */
/* 数值来源：STM32F407 数据手册 */
#define SYS_CLK_MAX_HCLK   168000000UL
#define SYS_CLK_MAX_PCLK1   42000000UL
#define SYS_CLK_MAX_PCLK2   84000000UL

/* -------------------- 切换策略 -------------------- */
/* 1 = 先跳 HSI 过渡（安全）；0 = 直接切（高速↔高速有总线风险） */
#define SYS_CLK_SAFE_TRANSITION  1

/* -------------------- 切换超时 -------------------- */
/* 等待 SWS 状态位确认目标源的最大循环次数（防硬件异常时死等） */
#define SYS_CLK_SWITCH_TIMEOUT   1000000UL

/* 等待"就绪标志置位"（HSIRDY / PLLRDY）的最大循环次数，防止死等。
 * HSIRDY 不置位：RCC 外设时钟未开或时钟树配错，状态位读不回来；
 * PLLRDY 不置位：PLL 输入源（HSE）掉失或 PLL 配置值非法。
 * 正常 HSI 稳定 2~5us、PLL 锁定 100~200us，1000000 次留有余量。 */
#define SYS_CLK_READY_TIMEOUT    1000000UL

/* -------------------- 时钟源枚举：函数参数/返回值编号 -------------------- */
typedef enum {
    SYS_CLK_HSI = 0,        /* 16MHz 内部 RC */
    SYS_CLK_HSE = 1,        /* 8MHz 外部晶振 */
    SYS_CLK_PLL = 2,        /* 168MHz（PLL） */
} SysClkSrc_t;

/* -------------------- 返回值 -------------------- */
#define SYS_CLK_OK           0      /* 成功 */
#define SYS_CLK_ERR_UNKNOWN  1      /* 目标时钟源非法 */
#define SYS_CLK_ERR_HSE      2      /* HSE 未起振（查晶振电路） */
#define SYS_CLK_ERR_SW       3      /* 切换超时未完成 */
#define SYS_CLK_ERR_DIV      4      /* 分频值非法（不是 SPL 分频常量） */
#define SYS_CLK_ERR_RANGE    5      /* 预演频率超总线上限，拒绝执行 */
#define SYS_CLK_ERR_LSI      6      /* LSI 未就绪（超时） */
#define SYS_CLK_ERR_LSE      7      /* LSE 未起振（查晶振/负载电容） */


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 切换到目标时钟源
 * 内部调用链（标准库）: 先切 HSI 并等 HSIRDY，Flash 等待周期与 PCLK1/2 分频
 *   降到最低，再配 HSE/PLL 并升回参数，最后切目标源、轮询 SWS 确认
 * 参数 : target — SYS_CLK_HSI / SYS_CLK_HSE / SYS_CLK_PLL
 * 返回 : SYS_CLK_OK 或错误码（SYS_CLK_ERR_*）
 * 注意 : 切换后软件延时不准；SysTick 已重装，调用方不要再调 SYS_TICK_Init() */
uint8_t     SYS_CLK_Switch  (SysClkSrc_t target);

/* 获取当前时钟源（读 SWS 状态位）
 * 标准库 : RCC_GetSYSCLKSource */
SysClkSrc_t SYS_CLK_GetSource(void);

/* 获取当前 SYSCLK 频率（Hz），如 168000000 / 16000000 / 8000000 */
uint32_t    SYS_CLK_GetFreq (void);

/* 获取 PLL 档频率记录值（Hz）
 * 首次切换前自 SystemCoreClock 捕获，切到其它源后仍可查询 */
uint32_t    SYS_CLK_GetPllFreq(void);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 便捷封装：切到高性能档（PLL 168MHz）
 * 等价 SYS_CLK_Switch(SYS_CLK_PLL)；返回 SYS_CLK_OK 或错误码 */
uint8_t SYS_CLK_ToHighSpeed(void);

/* 便捷封装：切到低功耗档（HSI 16MHz），等价 SYS_CLK_Switch(SYS_CLK_HSI)
 * 切换后软件延时不准；SysTick 已由内部重装，无需再 Init */
uint8_t SYS_CLK_ToLowPower(void);

/* 读回当前三条总线频率（Hz）；不需要的参数传 NULL
 * 用途 : 填时钟分配表、给外设分频换算做参考（TIM/SPI/ADC）
 * 标准库 : RCC_GetClocksFreq（一次调用填满整个结构体）
 * 示例 : SYS_CLK_GetBusFreq(&hclk, &p1, &p2) 三条总线；传 0 表示不要该条 */
void SYS_CLK_GetBusFreq(uint32_t *hclk, uint32_t *pclk1, uint32_t *pclk2);

/* 自定义总线分频（AHB / APB1 / APB2），常量用 SPL 定义：
 *   hclk_div — RCC_SYSCLK_Div1 / _Div2 / _Div4 / _Div8 / _Div16 / _Div64 /
 *              _Div128 / _Div256 / _Div512
 *   pclk1_div / pclk2_div — RCC_HCLK_Div1 / _Div2 / _Div4 / _Div8 / _Div16
 *              （TIM 倍频自动生效）
 * 返回 : SYS_CLK_OK / ERR_DIV / ERR_RANGE / ERR_SW（按当前主频预演，超上限
 *        拒绝；此后调用 SYS_CLK_Switch() 会按档位重置分频）
 * 标准库 : RCC_HCLKConfig / RCC_PCLK1Config / RCC_PCLK2Config */
uint8_t SYS_CLK_SetBusDiv(uint32_t hclk_div, uint32_t pclk1_div, uint32_t pclk2_div);

/* ---- LSE / LSI / RTC 时钟源（sys_rtc 依赖）---- */
/* 启动 LSE（外部 32.768kHz 晶振）并等起振
 * 返回 : SYS_CLK_OK / SYS_CLK_ERR_LSE（超时，查晶振/负载电容/焊点）
 * 标准库 : RCC_LSEConfig(RCC_LSE_ON) + RCC_GetFlagStatus(RCC_FLAG_LSERDY) */
uint8_t SYS_CLK_LseOn(void);

/* LSE / LSI 就绪查询（1 = 已就绪） */
uint8_t SYS_CLK_LseReady(void);
uint8_t SYS_CLK_LsiReady(void);

/* 启动 LSI（内部低速 RC）并等就绪；IWDG 可能已使能 LSI，重复使能幂等
 * 标准库 : RCC_LSICmd + RCC_GetFlagStatus(RCC_FLAG_LSIRDY) */
uint8_t SYS_CLK_LsiOn(void);

/* 选择并启用 RTC 时钟（sys_rtc 的 SYS_RTC_Init 内部调用）
 * 参数 : src — RCC_RTCCLKSource_LSE / _LSI / _HSE（标准库常量）
 * 标准库 : RCC_RTCCLKConfig + RCC_RTCCLKCmd(ENABLE) */
void SYS_CLK_RtcClkSelect(uint32_t src);


/* ================================================================
 *  附：标准库结构体速查 — RCC_TypeDef（stm32f4xx.h）
 * ================================================================
 *    CR       HSION/HSEON/PLLON 开关位、HSIRDY/PLLRDY 就绪标志
 *             （RCC_HSICmd / RCC_HSEConfig / RCC_PLLCmd / RCC_GetFlagStatus）
 *    PLLCFGR  PLL 配置 M/N/P/Q（SystemInit 已配 168MHz，本模块只切换）
 *    CFGR     SWS 当前源、HPRE/PPRE 总线分频（RCC_SYSCLKConfig /
 *             RCC_PCLK1Config / RCC_PCLK2Config）
 *    AHB1/2/3ENR、APB1/2ENR  外设时钟使能（RCC_xxxPeriphClockCmd）
 *    CSR      复位原因标志（SYS_WDG_ResetCause 读、RCC_ClearFlag 清）
 *    其余成员本库未用。
 * ================================================================ */

#endif /* __FWLIB_SYS_CLOCK_H */

