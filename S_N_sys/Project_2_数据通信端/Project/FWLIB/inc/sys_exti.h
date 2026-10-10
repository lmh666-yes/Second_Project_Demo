#ifndef __FWLIB_SYS_EXTI_H
#define __FWLIB_SYS_EXTI_H

#include "stm32f4xx.h"

/* 外部中断(EXTI)模块，支持 EXTI0 ~ EXTI15 共 16 条中断线
 * 触发方式：上升沿 / 下降沿 / 双边沿
 * 调用 SYS_EXTI_InitLine 注册回调，不用自己写 IRQHandler、不用查标志位
 * 回调运行在中断上下文，需短小快速返回；引脚方向与上下拉由调用方配置
 * line 与 pin 的对应关系及同线互斥约束见 SYS_EXTI_InitLine */


/* -------------------- 中断优先级 -------------------- */
/* 数值越小优先级越高；可用范围随 sys_nvic 的分组变化：
 *   NVIC_PriorityGroup_4（本库默认）: 抢占 0~15、子固定 0
 *   NVIC_PriorityGroup_2（纯裸机）  : 抢占 0~3、子 0~3
 *
 * 用 FreeRTOS 时必须 ≥5（默认已给 5）：
 *    FreeRTOS 用 BASEPRI = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY<<4
 *    （本工程 = 5<<4 = 0x50）屏蔽允许调 FromISR 的中断。
 *    抢占数值 < 5 的中断能打断内核临界区，其中禁止调用任何
 *    ...FromISR() 接口；开了 configASSERT 会断言失败。 */
#define SYS_EXTI_IRQ_PRE_PRIO   5
#define SYS_EXTI_IRQ_SUB_PRIO   0


/* 触发方式 */
typedef enum {
    SYS_EXTI_RISING  = 0,     /* 上升沿触发（EXTI_Trigger_Rising） */
    SYS_EXTI_FALLING = 1,     /* 下降沿触发（EXTI_Trigger_Falling） */
    SYS_EXTI_BOTH    = 2,     /* 双边沿触发（EXTI_Trigger_Rising_Falling） */
} SysExtiTrigger_t;

/* 初始化一条外部中断线并注册回调
 *
 * 库内部依次调用：GPIO_ClockEnable 开引脚端口时钟；
 *   RCC_APB2PeriphClockCmd 开 SYSCFG 时钟（映射生效的前提）；
 *   SYSCFG_EXTILineConfig 做引脚到中断线映射；EXTI_Init 配置边沿/模式/屏蔽；
 *   NVIC_Init 使能中断向量和优先级。
 *
 * 参数 : line，中断线号 0 ~ 15（与引脚号一致：PA0→0, PC13→13）
 *        port/pin，绑定的引脚（如 GPIOA, GPIO_Pin_0）
 *        trigger，触发方式，取值见 SysExtiTrigger_t
 *        callback，中断回调函数（中断上下文执行，保持短小）
 * 返回 : 1 = 成功；0 = 参数非法，line > 15 或 pin != (1 << line)
 * 约束 : line 必须与引脚掩码对应（pin == 1 << line），例如
 *        (13, GPIOC, GPIO_Pin_13) 合法、(5, GPIOB, GPIO_Pin_3) 非法。
 *        真正接到 EXTI 的是引脚号，line 只用于告诉 SYSCFG 该线挂在哪个端口。
 *        同线互斥：每个线号全局只能绑定一个引脚，PA0 占用线 0 后，
 *        PB0/PC0 再绑会改选端口源，原引脚不再触发、原回调被替换。
 *        按键已占线 0/2/3/4。 */
uint8_t SYS_EXTI_InitLine(uint8_t line, GPIO_TypeDef *port, uint16_t pin,
                          SysExtiTrigger_t trigger, void (*callback)(void));

/* 关闭指定中断线：清挂起标志、关 EXTI、注销回调，按需关 NVIC 通道
 * 标准库 : EXTI_ClearITPendingBit + EXTI_Init(LineCmd = DISABLE) + NVIC_Init
 * 说明 : 只关 EXTI 线不够，NVIC 通道还开着时中断仍会进向量表，
 *        每次都在 ISR 里查一遍状态然后空手返回。
 *        本函数在该 IRQ 通道上再没有别的已注册线时才关 NVIC 通道，
 *        线 5~9 共用 EXTI9_5、线 10~15 共用 EXTI15_10，不能直接关。 */
void SYS_EXTI_Disable(uint8_t line);

/* 清除指定中断线的挂起标志
 * 标准库 : EXTI_ClearITPendingBit */
void SYS_EXTI_ClearFlag(uint8_t line);

/* 查询指定中断线是否有挂起标志：1 = 有，0 = 无
 * 标准库 : EXTI_GetFlagStatus */
uint8_t SYS_EXTI_GetFlag(uint8_t line);


/* 软件触发一次指定中断线，无需外部电平变化即可测试回调逻辑
 * 标准库 : EXTI_GenerateSWInterrupt */
void SYS_EXTI_Trigger(uint8_t line);

/* 运行中更换触发方式，不改回调、不动 NVIC
 * 参数 : trigger，触发方式，取值同 SYS_EXTI_InitLine
 * 返回 : 1 = 成功；0 = 参数非法
 * 说明 : 切换时自动清一次挂起标志，避免旧边沿残留误触发。
 *        标准库没有只改边沿的单函数（EXTI_Init 需整包重填），
 *        本函数直接操作 EXTI->RTSR / FTSR 寄存器，不动其它位。 */
uint8_t SYS_EXTI_SetTrigger(uint8_t line, SysExtiTrigger_t trigger);


/* 附：EXTI_TypeDef 成员（定义在 stm32f4xx.h），16 条线共用这组寄存器
 *    IMR    中断屏蔽:1 = 允许该线产生中断
 *    EMR    事件屏蔽:1 = 只产生事件不触发中断（库未用）
 *    RTSR   上升沿触发选择:1 = 该线上升沿触发
 *    FTSR   下降沿触发选择:1 = 该线下降沿触发
 *    SWIER  软件中断事件:写 1 = 模拟一次触发
 *    PR     挂起标志:1 = 有触发待处理,写 1 清除
 *
 * 附：SYSCFG_TypeDef 成员（定义在 stm32f4xx.h）
 *    MEMRMP    内存重映射:位 26:24 SWJ_CFG 选择调试口占用，
 *              置 0b010 = 只关 JTAG 保留 SWD（SYS_SPI_Init 释放 PB3/PB4 时写它）
 *    PMC       外设模式:位 23 = RMII 模式（sys_eth.c 写它）
 *    EXTICR[4] 外部中断配置:每线 4 位,选择哪组端口映射到该线，
 *              同线互斥的根源
 *    CMPCR     补偿单元控制:库未使用 */

#endif /* __FWLIB_SYS_EXTI_H */
