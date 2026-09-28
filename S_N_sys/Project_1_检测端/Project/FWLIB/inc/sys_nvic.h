#ifndef __FWLIB_SYS_NVIC_H
#define __FWLIB_SYS_NVIC_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_nvic.h —— 【系统】中断优先级(NVIC)辅助模块  头文件
 * ================================================================
 *  设计定位 : 把"优先级分组 + 中断优先级设置 + 开关中断"收拢成
 *             统一接口，替代各处手写 NVIC_Init 结构体的做法
 *  标准库关键词 : NVIC_PriorityGroupConfig / NVIC_Init / NVIC_SetPriority
 *                 （其中部分为 CMSIS 内核接口,见各函数注释）
 *
 *  为什么有"优先级分组" :
 *      Cortex-M4 每个中断的优先级寄存器只有 4 个有效位，
 *      这 4 位如何切分为"抢占优先级 / 子优先级"由分组决定。
 *      五组分组的允许值一览（口径同 ST 标准库 misc.c 的对照表）:
 *
 *        分组                 | 抢占(pre) | 子(sub) | 位数构成
 *        ---------------------+-----------+---------+--------------------------
 *        NVIC_PriorityGroup_0 | 0         | 0~15    | 0 位抢占 + 4 位子
 *        NVIC_PriorityGroup_1 | 0~1       | 0~7     | 1 位抢占 + 3 位子
 *        NVIC_PriorityGroup_2 | 0~3       | 0~3     | 2 位抢占 + 2 位子（本库默认）
 *        NVIC_PriorityGroup_3 | 0~7       | 0~1     | 3 位抢占 + 1 位子
 *        NVIC_PriorityGroup_4 | 0~15      | 固定 0  | 4 位抢占 + 0 位子（FreeRTOS 要求）
 *
 *      SYS_NVIC_SetPriority() 的 pre/sub 请按"当前分组"一行的范围来传；
 *      超出范围的值会被按位截断（如 NVIC_PriorityGroup_2 下 pre 传 5，实际生效 5 & 3 = 1）。
 *      抢占优先级数值小的可以打断正在执行的（抢占数值大的）中断；
 *      子优先级只在"多个中断同时挂起"时决定谁先执行，不能互相打断。
 *      分组方式全局一套，必须在开中断之前定好、运行中不要随便改。
 *
 *  使用方式 :
 *      SYS_NVIC_Init();                          // ① main 开头调用一次
 *      SYS_NVIC_SetPriority(USART1_IRQn, 1, 0);  // ② 单独调整某个中断
 *      SYS_NVIC_EnableIRQ(TIM2_IRQn);            // ③ 手动开/关某个中断
 *
 *  与库内其它模块的关系 :
 *      sys_usart / sys_tim / sys_exti 等模块的优先级配置宏
 *      （SYS_XXX_IRQ_PRE_PRIO / SYS_XXX_IRQ_SUB_PRIO）要配合
 *      分组理解：先调用 SYS_NVIC_Init() 定好分组，各模块写入的
 *      优先级才按预期生效。与 FreeRTOS 混用时把分组改为
 *      NVIC_PriorityGroup_4（见区块 1 的宏）。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 中断优先级分组（4 个有效位的切分方式）
 *   NVIC_PriorityGroup_2 —— 2 位抢占 + 2 位子优先级（裸机推荐，默认）
 *   NVIC_PriorityGroup_4 —— 4 位全抢占 0~15（与 FreeRTOS 混用时必须） */
#define SYS_NVIC_PRIORITY_GROUP     NVIC_PriorityGroup_2


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化 NVIC 优先级分组（在 main 开头、开中断之前调用一次即可）
 * 标准库 : NVIC_PriorityGroupConfig（标准外设库 misc.c 提供）*/
void SYS_NVIC_Init(void);

/* 设置某个中断的优先级（只设置，不使能）
 * 参数 : irq —— 中断号，取值:标准库 IRQn_Type 枚举
 *              （外设中断如 EXTI0_IRQn / TIM2_IRQn / USART1_IRQn;
 *               内核异常（编号为负数）同样支持，如 SysTick_IRQn=-1、
 *               PendSV_IRQn=-2、SVC_IRQn=-5;
 *               NMI / HardFault 由硬件固定，不可设置。
 *               完整对照见 README 5.22）
 *        pre —— 抢占优先级（NVIC_PriorityGroup_2 下: 0~3；NVIC_PriorityGroup_4 下: 0~15）
 *        sub —— 子优先级  （NVIC_PriorityGroup_2 下: 0~3；NVIC_PriorityGroup_4 下恒为 0）
 * 说明 : 数值越小优先级越高；越界的值会被硬件按位截断，注意传参范围
 * 标准库 : NVIC_EncodePriority + NVIC_SetPriority（CMSIS 内核接口;
 *          与标准库 NVIC_Init 结构体写法等效,写的是同一个 IPR 寄存器）
 * 示例 : SYS_NVIC_SetPriority(USART1_IRQn, 1, 0);  // 串口设为较急
 *        SYS_NVIC_SetPriority(SysTick_IRQn, 3, 0); // 内核异常同样可设 */
void SYS_NVIC_SetPriority(IRQn_Type irq, uint8_t pre, uint8_t sub);

/* 使能 / 关闭某个中断（只管 NVIC 开关，不动外设内部的使能位）
 * 标准库 : NVIC_EnableIRQ / NVIC_DisableIRQ（CMSIS 接口,写 ISER/ICER）
 * 示例 : SYS_NVIC_EnableIRQ(TIM2_IRQn);      // 手动打开 TIM2 中断
 *        SYS_NVIC_DisableIRQ(TIM2_IRQn);     // 手动关闭 */
void SYS_NVIC_EnableIRQ (IRQn_Type irq);
void SYS_NVIC_DisableIRQ(IRQn_Type irq);


/* ================================================================
 *                    区块 3：扩展功能（观察与自测）
 * ================================================================ */
/* 查询中断是否正在执行：1 = 已进入 ISR 且尚未返回
 * 标准库 : NVIC_GetActive（CMSIS,读 IABR）
 * 示例 : if (SYS_NVIC_IsActive(TIM2_IRQn)) { ... }   // 它的 ISR 正在执行 */
uint8_t SYS_NVIC_IsActive(IRQn_Type irq);

/* 查询中断是否"挂起"（已触发、等待被执行）：1 = 挂起中
 * 标准库 : NVIC_GetPendingIRQ（CMSIS,读 ISPR）
 * 示例 : if (SYS_NVIC_IsPending(TIM2_IRQn)) { ... }  // 已触发、还没执行 */
uint8_t SYS_NVIC_IsPending(IRQn_Type irq);

/* 软件置"挂起位"：让中断像被真实触发一样去执行一次
 * 用途 : 调试/教学——不接外设也能验证 ISR 与向量表配置是否正确
 * 标准库 : NVIC_SetPendingIRQ（CMSIS,写 ISPR）
 * 示例 : SYS_NVIC_SetPending(TIM2_IRQn);   // 软件触发 TIM2 中断一次 */
void SYS_NVIC_SetPending(IRQn_Type irq);

/* 读当前生效的分组编码（0~7，对应内核 PRIGROUP 值）
 * 标准库 : NVIC_GetPriorityGrouping（CMSIS,读 SCB->AIRCR）*/
uint32_t SYS_NVIC_GetGroup(void);

#endif /* __FWLIB_SYS_NVIC_H */
