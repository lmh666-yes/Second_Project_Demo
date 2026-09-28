#include "sys_nvic.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */

/* ================================================================
 *  sys_nvic.c —— 【系统】中断优先级(NVIC)辅助模块  实现文件
 * ================================================================
 *  实现基于 CMSIS 内核接口（NVIC_* 系列），和标准外设库的
 *  NVIC_Init() 是同一套硬件的两种写法：
 *    NVIC_Init(结构体)     —— 老式风格，需要填 4 个字段
 *    NVIC_SetPriority(...) —— CMSIS 风格，一行完成，本文件采用后者
 *  两者可混用（写到底层都是同一个 IPR 寄存器）。
 * ================================================================ */


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 设置优先级分组：写 SCB->AIRCR 的 PRIGROUP 位
 * 说明 : 全局一套；分组选择见 sys_nvic.h 区块 1 的 SYS_NVIC_PRIORITY_GROUP */
void SYS_NVIC_Init(void)
{
    NVIC_PriorityGroupConfig(SYS_NVIC_PRIORITY_GROUP);
}

/* 设置单个中断的优先级
 * 步骤 : ① 读当前分组；② 把"抢占/子"两个数编码成 8 位优先级；
 *        ③ 写入 NVIC_SetPriority（内部写对应的 IPR 字节） */
void SYS_NVIC_SetPriority(IRQn_Type irq, uint8_t pre, uint8_t sub)
{
    uint32_t encoded = NVIC_EncodePriority(NVIC_GetPriorityGrouping(), pre, sub);

    NVIC_SetPriority(irq, encoded);
}

/* 使能 / 关闭中断：写 NVIC 的 ISER/ICER 寄存器 */
void SYS_NVIC_EnableIRQ (IRQn_Type irq) { NVIC_EnableIRQ(irq);  }
void SYS_NVIC_DisableIRQ(IRQn_Type irq) { NVIC_DisableIRQ(irq); }


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 是否正在执行：读 IABR（Active Bit Register） */
uint8_t SYS_NVIC_IsActive(IRQn_Type irq)
{
    return (NVIC_GetActive(irq) != 0U) ? 1U : 0U;
}

/* 是否挂起：读 ISPR（Pending Bit Register） */
uint8_t SYS_NVIC_IsPending(IRQn_Type irq)
{
    return (NVIC_GetPendingIRQ(irq) != 0U) ? 1U : 0U;
}

/* 软件置挂起：写 ISPR，内核会在当前中断层级允许时执行对应 ISR */
void SYS_NVIC_SetPending(IRQn_Type irq)
{
    NVIC_SetPendingIRQ(irq);
}

/* 读当前分组编码（SCB->AIRCR.PRIGROUP，0~7） */
uint32_t SYS_NVIC_GetGroup(void)
{
    return NVIC_GetPriorityGrouping();
}
