#ifndef __FWLIB_SYS_EXTI_H
#define __FWLIB_SYS_EXTI_H

#include "stm32f4xx.h"

/* sys_exti.h - 【系统】外部中断(EXTI)模块 头文件
 * 功能 : 引脚电平跳变时执行注册的回调，支持 EXTI0~EXTI15，触发方式为
 *        上升沿 / 下降沿 / 双边沿（EXTI_Trigger_Rising / _Falling /
 *        _Rising_Falling），无需自写 IRQHandler 与查标志。
 * 约束 : 线号 = 引脚号（PA5→线 5、PC13→线 13），每条线同一时刻只能绑定一个
 *        引脚（PA0 与 PB0 同为线 0）;引脚方向 / 上下拉自行配置（按键引脚由
 *        KEY_Init() 配好）;回调运行于中断上下文，须短小快速返回;ISR 内延时用
 *        delay_ms()（纯忙等），不用依赖 SysTick 中断的 SYS_TICK_Delay_ms()。
 *        EXTI5~9、EXTI10~15 各共用一向量，由本模块分发;ISR 为弱定义，可自写
 *        EXTIx_IRQHandler 顶替。 */


/* 中断优先级（数值越小越高）;范围随 sys_nvic 分组:
 *   NVIC_PriorityGroup_4（本库默认）: 抢占 0~15、子固定 0
 *   NVIC_PriorityGroup_2（纯裸机）  : 抢占 0~3、子 0~3
 * 用 FreeRTOS 时必须 ≥5（默认已给 5）: BASEPRI =
 *   configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY<<4（本工程 = 5<<4 = 0x50）
 *   屏蔽允许调 FromISR 的中断;抢占数值 < 5 的中断可打断内核临界区，
 *   禁止在其中调用 ...FromISR()，开 configASSERT 会断言失败。
 *   最急只能取 5，更缓取 6~15。 */
#define SYS_EXTI_IRQ_PRE_PRIO   5
#define SYS_EXTI_IRQ_SUB_PRIO   0


/* -------------------- 基础功能 -------------------- */
/* 触发方式 */
typedef enum {
    SYS_EXTI_RISING  = 0,     /* 上升沿触发（标准库:EXTI_Trigger_Rising） */
    SYS_EXTI_FALLING = 1,     /* 下降沿触发（标准库:EXTI_Trigger_Falling） */
    SYS_EXTI_BOTH    = 2,     /* 双边沿触发（标准库:EXTI_Trigger_Rising_Falling） */
} SysExtiTrigger_t;

/* 初始化一条外部中断线并注册回调
 *
 * 标准库 : GPIO_ClockEnable → RCC_APB2PeriphClockCmd → SYSCFG_EXTILineConfig
 *   → EXTI_Init → NVIC_Init;中断内 EXTI_GetITStatus → EXTI_ClearITPendingBit
 *   → 回调。
 *
 * 参数 : line     - 中断线号 0 ~ 15（与引脚号一致：PA0→0, PC13→13）
 *        port/pin - 绑定的引脚（如 GPIOA, GPIO_Pin_0）
 *        trigger  - SYS_EXTI_RISING（低 → 高）/ SYS_EXTI_FALLING（高 → 低）/
 *                   SYS_EXTI_BOTH（双边沿），三选一
 *        callback - 回调函数（中断上下文执行，保持短小）
 * 返回 : 1 = 成功；0 = 参数非法
 * 约束 : 须满足 pin == 1 << line，如 (13, GPIOC, GPIO_Pin_13) 合法、
 *    (5, GPIOB, GPIO_Pin_3) 非法（返回 0）;line 仅用于告知 SYSCFG 端口源，
 *    可用 GPIO_PinSource(引脚掩码) 求取（见 key.c）。每个线号全局只能有一个
 *    使用者，同线重复绑定会抢占（原引脚不再触发、原回调被替换）;按键占线
 *    0/2/3/4（占用表见 README 5.10）。 */
uint8_t SYS_EXTI_InitLine(uint8_t line, GPIO_TypeDef *port, uint16_t pin,
                          SysExtiTrigger_t trigger, void (*callback)(void));

/* 关闭指定中断线（清挂起标志 + 关 EXTI + 注销回调 + 按需关 NVIC 通道）
 * 标准库 : EXTI_ClearITPendingBit + EXTI_Init(LineCmd = DISABLE) + NVIC_Init
 * 说明 : 只关 EXTI 线不够，NVIC 通道仍会使中断进向量表（ISR 空返回，白耗 CPU）。
 *        线 5~9 共用 EXTI9_5、线 10~15 共用 EXTI15_10，故仅在该 IRQ 通道上再无
 *        其他已注册线时才关 NVIC。 */
void SYS_EXTI_Disable(uint8_t line);

/* 清除指定中断线的挂起标志
 * 标准库 : EXTI_ClearITPendingBit */
void SYS_EXTI_ClearFlag(uint8_t line);

/* 查询指定中断线是否有挂起标志：1 = 有，0 = 无
 * 标准库 : EXTI_GetFlagStatus */
uint8_t SYS_EXTI_GetFlag(uint8_t line);


/* -------------------- 扩展功能 -------------------- */
/* 软件触发一次指定中断线（无需外部电平变化即可测试回调逻辑）
 * 标准库 : EXTI_GenerateSWInterrupt */
void SYS_EXTI_Trigger(uint8_t line);

/* 运行中更换触发方式（不改回调、不动 NVIC）
 * 参数 : trigger - 触发方式，取值同 SYS_EXTI_InitLine:
 *                   SYS_EXTI_RISING / SYS_EXTI_FALLING / SYS_EXTI_BOTH
 * 返回 : 1 = 成功；0 = 参数非法（line > 15）
 * 说明 : 切换时自动清一次挂起标志，避免旧边沿残留误触发。标准库无只改边沿的
 *        单函数（EXTI_Init 需整包重填），故直接写 EXTI->RTSR / FTSR 寄存器。 */
uint8_t SYS_EXTI_SetTrigger(uint8_t line, SysExtiTrigger_t trigger);


/* 附:标准库结构体速查（定义在 stm32f4xx.h）
 * EXTI_TypeDef（16 条线共用）:
 *   IMR    中断屏蔽:1 = 允许该线中断（EXTI_Init 写）
 *   EMR    事件屏蔽:1 = 只产生事件（库未用）
 *   RTSR / FTSR 上升 / 下降沿触发选择:1 = 对应边沿触发（SetTrigger 写）
 *   SWIER  写 1 = 模拟一次触发（SYS_EXTI_Trigger 用）
 *   PR     挂起标志:写 1 清除（清 / 查标志读写它）
 * SYSCFG_TypeDef:
 *   MEMRMP  SWJ_CFG 位选调试口占用,0b010 = 只关 JTAG 保留 SWD
 *           （SYS_SPI_Init 释放 PB3/PB4 写;sys_eth 位 23 切 RMII）
 *   PMC     位 23 = RMII 模式（sys_eth.c 写）
 *   EXTICR[4] 每线 4 位,选映射到该线的端口组（SYSCFG_EXTILineConfig 写）
 *   CMPCR   库未使用 */

#endif /* __FWLIB_SYS_EXTI_H */
