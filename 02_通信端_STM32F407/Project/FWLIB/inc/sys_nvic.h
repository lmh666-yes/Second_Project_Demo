#ifndef __FWLIB_SYS_NVIC_H
#define __FWLIB_SYS_NVIC_H

#include "stm32f4xx.h"

/* sys_nvic.h: 系统中断优先级(NVIC)辅助模块，含分组配置、优先级设置与中断开关
 * Cortex-M4 每个中断的优先级寄存器只有 4 个有效位，这 4 位如何切分为抢占/子优先级由分组决定。
 * 各分组的允许值（口径同 ST 标准库 misc.c 的对照表）:
 *
 *   分组                 | 抢占(pre) | 子(sub) | 位数构成
 *   ---------------------+-----------+---------+--------------------------
 *   NVIC_PriorityGroup_0 | 0         | 0~15    | 0 位抢占 + 4 位子
 *   NVIC_PriorityGroup_1 | 0~1       | 0~7     | 1 位抢占 + 3 位子
 *   NVIC_PriorityGroup_2 | 0~3       | 0~3     | 2 位抢占 + 2 位子
 *   NVIC_PriorityGroup_3 | 0~7       | 0~1     | 3 位抢占 + 1 位子
 *   NVIC_PriorityGroup_4 | 0~15      | 固定 0  | 4 位抢占 + 0 位子（本库默认）
 *
 * SYS_NVIC_SetPriority() 的 pre/sub 按当前分组一行的范围传入；超范围的值被按位截断
 * （如 NVIC_PriorityGroup_2 下 pre 传 5，实际生效 5 & 3 = 1）。
 * 抢占优先级数值小的可打断数值大的中断；子优先级只在多个中断同时挂起时决定谁先执行，不能互相打断。
 * 分组方式全局一套，必须在开中断之前定好，运行中不修改。
 * sys_usart / sys_tim / sys_exti 等模块的优先级配置宏
 * （SYS_XXX_IRQ_PRE_PRIO / SYS_XXX_IRQ_SUB_PRIO）要先由本模块定好分组才按预期生效。 */


/* 定义与宏定义区 */
/* 中断优先级分组（4 个有效位的切分方式）
 * NVIC_PriorityGroup_4: 4 位全抢占，取值 0~15，本库默认，用于配合 FreeRTOS
 *   FreeRTOS 以 BASEPRI = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << 4 屏蔽临界区；
 *   只有 Group_4 下抢占优先级数值才与寄存器高 4 位一一对应，数值 ≥5 的中断才允许调 ...FromISR()
 * NVIC_PriorityGroup_2: 2 位抢占 + 2 位子优先级，纯裸机项目可改回此值
 *   Group_2 下 pre 被编码到 bit[7:6]，越界优先级看似合法但仍在破坏内核临界区 */
#define SYS_NVIC_PRIORITY_GROUP     NVIC_PriorityGroup_4


/* 基础功能 */
/* 初始化 NVIC 优先级分组，在 main 开头、开中断之前调用一次
 * 标准库: NVIC_PriorityGroupConfig（标准外设库 misc.c）*/
void SYS_NVIC_Init(void);

/* 设置某个中断的优先级，只设置不使能
 * irq: IRQn_Type 枚举；外设中断如 EXTI0_IRQn / TIM2_IRQn / USART1_IRQn，内核异常编号为负
 *      （SysTick_IRQn=-1、PendSV_IRQn=-2、SVC_IRQn=-5）；NMI / HardFault 由硬件固定，不可设置
 * pre: Group_2 下 0~3、Group_4 下 0~15；sub: Group_2 下 0~3、Group_4 下恒为 0；数值越小优先级越高
 * 越界值被硬件按位截断。标准库: NVIC_EncodePriority + NVIC_SetPriority（CMSIS 内核接口） */
void SYS_NVIC_SetPriority(IRQn_Type irq, uint8_t pre, uint8_t sub);

/* 使能 / 关闭某个中断，只管 NVIC 开关，不动外设内部的使能位
 * 标准库: NVIC_EnableIRQ / NVIC_DisableIRQ（CMSIS 接口，写 ISER/ICER）*/
void SYS_NVIC_EnableIRQ (IRQn_Type irq);
void SYS_NVIC_DisableIRQ(IRQn_Type irq);


/* 扩展功能：中断状态查询与软件触发 */
/* 查询中断是否正在执行：1 = 已进入 ISR 且尚未返回
 * 标准库: NVIC_GetActive（CMSIS，读 IABR）*/
uint8_t SYS_NVIC_IsActive(IRQn_Type irq);

/* 查询中断是否挂起（已触发、等待被执行）：1 = 挂起中
 * 标准库: NVIC_GetPendingIRQ（CMSIS，读 ISPR）*/
uint8_t SYS_NVIC_IsPending(IRQn_Type irq);

/* 软件置挂起位：使中断像被真实触发一样执行一次，不接外设也能验证 ISR 与向量表
 * 标准库: NVIC_SetPendingIRQ（CMSIS，写 ISPR）*/
void SYS_NVIC_SetPending(IRQn_Type irq);

/* 读当前生效的分组编码（0~7，对应内核 PRIGROUP 值）
 * 标准库 : NVIC_GetPriorityGrouping（CMSIS,读 SCB->AIRCR）*/
uint32_t SYS_NVIC_GetGroup(void);

#endif /* __FWLIB_SYS_NVIC_H */
