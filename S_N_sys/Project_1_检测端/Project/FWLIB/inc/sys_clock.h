#ifndef __FWLIB_SYS_CLOCK_H
#define __FWLIB_SYS_CLOCK_H

#include "stm32f4xx.h"
#include <stddef.h>     /* NULL（配置表查找失败返回空指针，sys_clock.c 使用） */

/* ================================================================
 *  sys_clock.h —— 【系统】时钟源切换模块  头文件
 * ================================================================
 *  设计定位 : 运行时动态切换 SYSCLK 时钟源（性能 ↔ 功耗）
 *             高负载时用 PLL 168MHz，低功耗/低要求时切 16MHz 或 8MHz
 *  标准库关键词 : RCC_HSEConfig / RCC_PLLCmd / RCC_SYSCLKConfig / RCC_PCLK1Config /
 *                 RCC_PCLK2Config / FLASH_SetLatency / RCC_GetClocksFreq
 *
 *  三档时钟源 :
 *      SYS_CLK_PLL —— 168MHz（默认档：上电时 SystemInit 已配好）
 *      SYS_CLK_HSI —— 16MHz （内部 RC，无需外部晶振，永远可用）
 *      SYS_CLK_HSE —— 8MHz  （外部晶振直连）
 *
 *  重要注意事项 :
 *   ① 切换会改变整个芯片运行速度：
 *      - gpio_core 的软件空循环延时随之变化（需要重新标定）；
 *      - 已配置的 SysTick 需重新调用 SYS_TICK_Init() 校准；
 *   ② 切换涉及 Flash 等待周期（latency）与总线分频，
 *      本模块内部自动处理（先降速→改配置→再升速 的安全流程）；
 *   ③ 为安全起见，切换时先跳 HSI 作过渡源（见 SYS_CLK_SAFE_TRANSITION），
 *      避免高速运行中直接改分频造成总线异常；
 *   ④ 切换成功后 SystemCoreClock（CMSIS 标准变量）会自动更新；
 *   ⑤ ⚠ 切换后"必须重做"清单（按模块，切记！）:
 *      最简写法：
 *          if (SYS_CLK_Switch(target) == SYS_CLK_OK) { SYS_TICK_Init(); }
 *      · sys_tick  —— 必须重调 SYS_TICK_Init()（LOAD 还是旧主频的值）
 *      · sys_usart —— 重新 SYS_USART_Init()（波特率按新 PCLK 重算）
 *      · sys_tim   —— 重新 Init（频率按新总线时钟重算；想保留占空比：
 *                     先 SYS_TIM_PwmGetDuty 存着，重 Init 后 SetDuty）
 *      · sys_i2c / sys_spi —— 重新 Init（内部速率按新 PCLK 换算）
 *      · sys_adc   —— 重新 Init（ADCCLK/采样时序变化；DMA 采集先停）
 *      · sys_wdg   —— 仅 WWDG 受影响（按 PCLK1 换算，需重 WwdgInit）；
 *                     IWDG 用 LSI，不受切换影响
 *      · gpio_core 的 Delay_ms —— 粗延时数值不再代表毫秒（直接失效）
 *      · lcd —— FSMC 时序按 HCLK 周期计，实际时间变（屏异常先重评估宏）
 *      不受影响：sys_exti / sys_nvic / sys_dma / sys_fault / sys_flash
 *      已上 FreeRTOS：SysTick 归 RTOS——跳过 sys_tick 一项，用 vTaskDelay
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* -------------------- 时钟频率 -------------------- */
/* HSE_VALUE 在 Keil 工程宏中定义（当前 8MHz）；
 * HSI_VALUE 是芯片内部 RC 的固定频率（16MHz） */
#define SYS_CLK_FREQ_HSE   (HSE_VALUE)
#define SYS_CLK_FREQ_HSI   (HSI_VALUE)

/* -------------------- 总线频率上限 -------------------- */
/* 数值来源：STM32F407 数据手册；换其它型号先核对手册再改 */
#define SYS_CLK_MAX_HCLK   168000000UL
#define SYS_CLK_MAX_PCLK1   42000000UL
#define SYS_CLK_MAX_PCLK2   84000000UL

/* -------------------- 切换策略 -------------------- */
/* 1 = 先跳 HSI 低速源过渡（安全，推荐保持）
 * 0 = 直接切（步骤少，但高速↔高速切换存在总线风险，不推荐） */
#define SYS_CLK_SAFE_TRANSITION  1

/* -------------------- 切换超时 -------------------- */
/* 等待 SWS 状态位确认目标源的最大循环次数（防硬件异常时死等） */
#define SYS_CLK_SWITCH_TIMEOUT   1000000UL

/* -------------------- 时钟源枚举 -------------------- */
/* 作为函数参数/返回值统一使用的时钟源编号 */
typedef enum {
    SYS_CLK_HSI = 0,        /* 16MHz 内部 RC */
    SYS_CLK_HSE = 1,        /* 8MHz 外部晶振 */
    SYS_CLK_PLL = 2,        /* 168MHz（PLL） */
} SysClkSrc_t;

/* -------------------- 返回值 -------------------- */
#define SYS_CLK_OK           0      /* 切换成功 */
#define SYS_CLK_ERR_UNKNOWN  1      /* 目标时钟源非法 */
#define SYS_CLK_ERR_HSE      2      /* HSE 未起振（检查晶振电路） */
#define SYS_CLK_ERR_SW       3      /* 切换超时未完成 */
#define SYS_CLK_ERR_DIV      4      /* 分频值非法（不是 SPL 分频常量） */
#define SYS_CLK_ERR_RANGE    5      /* 预演频率超出总线上限（拒绝执行） */


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 切换到目标时钟源
 *
 * 标准库调用链（库内部依次调用，可对照学习）:
 *   ① RCC_HSICmd + 等 HSIRDY        确保过渡源可用
 *   ② RCC_GetSYSCLKSource + RCC_SYSCLKConfig(HSI)  先降速到 HSI
 *   ③ FLASH_SetLatency(0) + RCC_PCLK1/2Config(Div1) 降到最低档
 *   ④ RCC_HSEConfig + RCC_WaitForHSEStartUp + RCC_PLLCmd  按目标档配置
 *   ⑤ FLASH_SetLatency + RCC_PCLK1/2Config  升到目标参数
 *   ⑥ RCC_SYSCLKConfig(目标) + 轮询 SWS 确认切换到位
 *
 * 参数 : target —— SYS_CLK_HSI / SYS_CLK_HSE / SYS_CLK_PLL
 * 返回 : SYS_CLK_OK 或错误码（SYS_CLK_ERR_*）
 * 注意 : 切换后软件延时不准、SysTick 需重新 Init（见文件头说明）
 * 示例 : if (SYS_CLK_Switch(SYS_CLK_HSI) == SYS_CLK_OK) {
 *            SYS_TICK_Init();      // 切完必须重新校准毫秒时基
 *        } */
uint8_t     SYS_CLK_Switch  (SysClkSrc_t target);

/* 获取当前时钟源（读硬件 SWS 状态位，真实反映现状）
 * 标准库 : RCC_GetSYSCLKSource */
SysClkSrc_t SYS_CLK_GetSource(void);

/* 获取当前 SYSCLK 频率（Hz），如 168000000 / 16000000 / 8000000 */
uint32_t    SYS_CLK_GetFreq (void);

/* 获取 PLL 档频率记录值（Hz）
 * 说明 : 首次切换前从 SystemCoreClock 捕获并保存，
 *       之后即使切到其它时钟源，仍可查询"PLL 档是多少" */
uint32_t    SYS_CLK_GetPllFreq(void);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 便捷封装：切到高性能档（PLL 168MHz）
 * 等价 SYS_CLK_Switch(SYS_CLK_PLL)；返回 SYS_CLK_OK 或错误码 */
uint8_t SYS_CLK_ToHighSpeed(void);

/* 便捷封装：切到低功耗档（HSI 16MHz）
 * 等价 SYS_CLK_Switch(SYS_CLK_HSI)
 * 提醒 : 切换后软件延时不再准确、SysTick 需重新 SYS_TICK_Init()
 * 示例 : if (SYS_CLK_ToHighSpeed() == SYS_CLK_OK) { SYS_TICK_Init(); } */
uint8_t SYS_CLK_ToLowPower(void);

/* 读回当前三条总线频率（Hz）；不关心的参数可传 NULL（如只要 PCLK1）
 * 用途 : 填“时钟分配表”、给外设分频换算做参考（TIM/SPI/ADC 等）
 * 标准库 : RCC_GetClocksFreq（一次调用填满整个结构体）
 * 示例 : uint32_t hclk, p1, p2;
 *        SYS_CLK_GetBusFreq(&hclk, &p1, &p2);   // 三条总线频率(Hz)
 *        SYS_CLK_GetBusFreq(0, &p1, 0);         // 只要 PCLK1(不需要的传 0) */
void SYS_CLK_GetBusFreq(uint32_t *hclk, uint32_t *pclk1, uint32_t *pclk2);

/* 自定义总线分频（AHB / APB1 / APB2），常量用 SPL 定义：
 *   hclk_div              —— RCC_SYSCLK_Div1 / _Div2 / _Div4 / _Div8 /
 *                            _Div16 / _Div64 / _Div128 / _Div256 / _Div512
 *   pclk1_div / pclk2_div —— RCC_HCLK_Div1 / _Div2 / _Div4 / _Div8 / _Div16
 *                            （TIM 倍频自动生效）
 * 流程：按当前主频“预演”目标频率，超上限直接拒绝；通过后自动
 *       “降 HSI → 改分频 → 切回原源”，全程安全
 * 返回 : SYS_CLK_OK / ERR_DIV / ERR_RANGE / ERR_SW
 * ⚠ 之后再调用 SYS_CLK_Switch() 会按档位重置分频（需重新设置）
 * 标准库 : RCC_HCLKConfig / RCC_PCLK1Config / RCC_PCLK2Config
 * 示例 : SYS_CLK_SetBusDiv(RCC_SYSCLK_Div1, RCC_HCLK_Div8, RCC_HCLK_Div2);   // 降 PCLK1 */
uint8_t SYS_CLK_SetBusDiv(uint32_t hclk_div, uint32_t pclk1_div, uint32_t pclk2_div);


/* ================================================================
 *  附:标准库结构体速查 —— RCC_TypeDef（stm32f4xx.h;时钟总管）
 * ================================================================
 *  库相关的成员（含库中用法）:
 *    CR        时钟控制:HSION/HSEON/PLLON 开关位、HSIRDY/PLLRDY 就绪
 *              标志（RCC_HSICmd、RCC_HSEConfig、RCC_PLLCmd、
 *              RCC_GetFlagStatus 都动它——切频时"等就绪"就在轮询）
 *    PLLCFGR   PLL 配置:M/N/P/Q 参数（SystemInit 已配好 168MHz;
 *              sys_clock 只切换不重配）
 *    CFGR      时钟配置:SWS 当前时钟源 / PPRE 总线分频 / HPRE
 *              （RCC_SYSCLKConfig、RCC_PCLK1/2Config 写它;定时器
 *                判断"APB 分频≠1 时时钟×2"读的也是它的 PPRE 位）
 *    CIR       时钟中断:就绪中断标志,库未用
 *    AHB1/2/3ENR  外设时钟使能:GPIO/DMA/ETH 等在 AHB1ENR
 *                 （RCC_AHB1PeriphClockCmd 写它——本库开时钟全走这里）
 *    APB1/2ENR    APB 外设时钟使能:TIM/USART/I2C/SPI/ADC/PWR/WWDG
 *                 （RCC_APB1/2PeriphClockCmd 写它）
 *    AHB/APB RSTR 系列   外设复位控制,库未用
 *    LPENR 系列         低功耗模式外设时钟,库未用
 *    BDCR      备份域:RTC/LSE 时钟,库未用
 *    CSR       控制状态:复位原因标志!（SYS_WDG_ResetCause 读它、
 *              RCC_ClearFlag 清它——判断"上次怎么重启的"在这）
 *    SSCGR / PLLI2SCFGR / PLLSAICFGR / DCKCFGR  扩频时钟与音频
 *              PLL,库未用
 * ================================================================ */

#endif /* __FWLIB_SYS_CLOCK_H */

