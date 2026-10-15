#include "sys_nvic.h"
/* 实现层;对照说明见 sys_nvic.h */

/* ================================================================
 *  sys_nvic.c:中断优先级(NVIC)辅助模块 实现文件
 *  基于 CMSIS NVIC_* 接口;与标准库 NVIC_Init() 写同一 IPR 寄存器。
 * ================================================================ */


/* 区块 2:基础功能 */
/* 优先级分组:写 SCB->AIRCR.PRIGROUP;全局一套,取值见 sys_nvic.h 的 SYS_NVIC_PRIORITY_GROUP */
void SYS_NVIC_Init(void)
{
    NVIC_PriorityGroupConfig(SYS_NVIC_PRIORITY_GROUP);
}

/* 设置单个中断优先级:读当前分组,抢占/子优先级编码为 8 位后写对应 IPR 字节 */
void SYS_NVIC_SetPriority(IRQn_Type irq, uint8_t pre, uint8_t sub)
{
    uint32_t encoded = NVIC_EncodePriority(NVIC_GetPriorityGrouping(), pre, sub);

    NVIC_SetPriority(irq, encoded);
}

/* 使能 / 关闭中断：写 NVIC 的 ISER/ICER 寄存器 */
void SYS_NVIC_EnableIRQ (IRQn_Type irq) { NVIC_EnableIRQ(irq);  }
void SYS_NVIC_DisableIRQ(IRQn_Type irq) { NVIC_DisableIRQ(irq); }


/* 区块 3:扩展功能 */
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
