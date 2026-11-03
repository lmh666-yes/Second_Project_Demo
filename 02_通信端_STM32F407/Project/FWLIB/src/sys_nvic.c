#include "sys_nvic.h"
/* 本文件为 NVIC 辅助模块实现层；标准库对照与示例注记见同名 .h */

/* 本模块基于 CMSIS 内核接口（NVIC_* 系列），写到底层为 IPR 寄存器，
 * 与标准外设库 NVIC_Init() 操作同一套硬件，两者可混用 */

/* 设置优先级分组：写 SCB->AIRCR 的 PRIGROUP 位
 * 全局一套；分组取值见 sys_nvic.h 的 SYS_NVIC_PRIORITY_GROUP */
void SYS_NVIC_Init(void)
{
    NVIC_PriorityGroupConfig(SYS_NVIC_PRIORITY_GROUP);
}

/* 设置单个中断的优先级
 * irq 为中断号；pre 为抢占优先级，sub 为子优先级，合法取值由当前分组决定
 * 步骤：1) 读当前分组；2) 把抢占/子两个数编码成 8 位优先级；3) 写 NVIC_SetPriority（内部写对应的 IPR 字节） */
void SYS_NVIC_SetPriority(IRQn_Type irq, uint8_t pre, uint8_t sub)
{
    uint32_t encoded = NVIC_EncodePriority(NVIC_GetPriorityGrouping(), pre, sub);

    NVIC_SetPriority(irq, encoded);
}

/* 使能 / 关闭中断：写 NVIC 的 ISER/ICER 寄存器 */
void SYS_NVIC_EnableIRQ (IRQn_Type irq) { NVIC_EnableIRQ(irq);  }
void SYS_NVIC_DisableIRQ(IRQn_Type irq) { NVIC_DisableIRQ(irq); }


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
