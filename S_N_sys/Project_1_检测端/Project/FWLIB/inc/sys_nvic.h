#ifndef __FWLIB_SYS_NVIC_H
#define __FWLIB_SYS_NVIC_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_nvic.h — 中断优先级(NVIC)辅助模块 头文件
 * ================================================================
 *  功能: 优先级分组、中断优先级设置、中断开关的统一接口。
 *  约束: 分组全局唯一，须在开中断前定好，运行中不可改；SYS_NVIC_Init() 调用一次；
 *        pre/sub 范围随分组(G0 0/0~15、G1 0~1/0~7、G2 0~3/0~3、G3 0~7/0~1、G4 0~15/0)，
 *        sys_usart/sys_tim/sys_exti 的 SYS_XXX_IRQ_PRE_PRIO / SYS_XXX_IRQ_SUB_PRIO 与分组一致。
 *  依据: ST 标准库 misc.c 分组对照表、Cortex-M4 优先级寄存器 4 个有效位、README 5.22。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* 中断优先级分组
 *   NVIC_PriorityGroup_4 — 4 位全抢占 0~15（本库默认）
 *   NVIC_PriorityGroup_2 — 2 位抢占 + 2 位子优先级，pre 编码到 bit[7:6]
 * 依据: FreeRTOS 以 BASEPRI = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY<<4 屏蔽临界区，
 *       Group_4 下抢占优先级数值与 IPR 高 4 位一一对应，数值 ≥5 的中断才允许调 ...FromISR()。 */
#define SYS_NVIC_PRIORITY_GROUP     NVIC_PriorityGroup_4


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化 NVIC 优先级分组（在 main 开头、开中断之前调用一次即可）
 * 标准库 : NVIC_PriorityGroupConfig（标准外设库 misc.c 提供）*/
void SYS_NVIC_Init(void);

/* 设置某个中断的优先级（只设置，不使能）
 * irq — 中断号，IRQn_Type 枚举：外设中断 EXTI0_IRQn / TIM2_IRQn / USART1_IRQn；
 *       内核异常编号为负，如 SysTick_IRQn=-1、PendSV_IRQn=-2、SVC_IRQn=-5；
 *       NMI / HardFault 由硬件固定，不可设置。完整对照见 README 5.22。
 * pre — 抢占优先级（NVIC_PriorityGroup_2 下: 0~3；NVIC_PriorityGroup_4 下: 0~15）
 * sub — 子优先级（NVIC_PriorityGroup_2 下: 0~3；NVIC_PriorityGroup_4 下恒为 0）
 * 说明 : 数值越小优先级越高；越界值被硬件按位截断。
 * 标准库 : NVIC_EncodePriority + NVIC_SetPriority（CMSIS，写 IPR，等效 NVIC_Init 结构体写法）
 * 示例 : SYS_NVIC_SetPriority(USART1_IRQn, 1, 0); */
void SYS_NVIC_SetPriority(IRQn_Type irq, uint8_t pre, uint8_t sub);

/* 使能 / 失能某个中断（只操作 NVIC 开关，不动外设内部使能位）
 * 标准库 : NVIC_EnableIRQ / NVIC_DisableIRQ（CMSIS，写 ISER/ICER）
 * 示例 : SYS_NVIC_EnableIRQ(TIM2_IRQn); SYS_NVIC_DisableIRQ(TIM2_IRQn); */
void SYS_NVIC_EnableIRQ (IRQn_Type irq);
void SYS_NVIC_DisableIRQ(IRQn_Type irq);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 查询中断是否正在执行：1 = 已进入 ISR 且尚未返回
 * 标准库 : NVIC_GetActive（CMSIS，读 IABR）*/
uint8_t SYS_NVIC_IsActive(IRQn_Type irq);

/* 查询中断是否挂起（已触发、等待执行）：1 = 挂起中
 * 标准库 : NVIC_GetPendingIRQ（CMSIS，读 ISPR）*/
uint8_t SYS_NVIC_IsPending(IRQn_Type irq);

/* 软件置挂起位：中断按真实触发方式执行一次
 * 用途 : 不接外设验证 ISR 与向量表配置；标准库 NVIC_SetPendingIRQ（CMSIS，写 ISPR）*/
void SYS_NVIC_SetPending(IRQn_Type irq);

/* 读当前生效的分组编码（0~7，对应内核 PRIGROUP 值）
 * 标准库 : NVIC_GetPriorityGrouping（CMSIS,读 SCB->AIRCR）*/
uint32_t SYS_NVIC_GetGroup(void);

#endif /* __FWLIB_SYS_NVIC_H */
