#include "sys_exti.h"
/* 配套指引 : 标准库对照 / 示例 / 扩展提示见同名 .h */
#include "gpio_core.h"

/* =======================================================
 *  sys_exti.c — 【系统】外部中断(EXTI)模块  实现文件
 *
 *  SYSCFG 完成引脚到中断线的绑定。7 个中断向量承载 16 条线 : 线 0/1/2/3/4 独立，
 *  线 5~9 共用 EXTI9_5，线 10~15 共用 EXTI15_10，统一分发到回调表。
 *  7 个 EXTIx_IRQHandler 在此定义，应用重复定义会链接报重复符号。 */


/* 回调表 : 下标 = 中断线号 0~15；0 = 未注册 */
static void (*exti_callback[16])(void);


/* 内部辅助 */
/* 端口指针 → SYSCFG 端口源编号；不支持的端口返回 0xFF */
static uint8_t exti_port_source(GPIO_TypeDef *port)
{
    if (port == GPIOA) return EXTI_PortSourceGPIOA;
    if (port == GPIOB) return EXTI_PortSourceGPIOB;
    if (port == GPIOC) return EXTI_PortSourceGPIOC;
    if (port == GPIOD) return EXTI_PortSourceGPIOD;
    if (port == GPIOE) return EXTI_PortSourceGPIOE;
    if (port == GPIOF) return EXTI_PortSourceGPIOF;
    if (port == GPIOG) return EXTI_PortSourceGPIOG;
    return 0xFF;    /* F407ZE 只有 GPIOA ~ GPIOG */
}

/* 线号 → 所属中断向量 : 0~4 独立；5~9、10~15 共用 */
static IRQn_Type exti_irqn(uint8_t line)
{
    switch (line) {
        case 0:  return EXTI0_IRQn;
        case 1:  return EXTI1_IRQn;
        case 2:  return EXTI2_IRQn;
        case 3:  return EXTI3_IRQn;
        case 4:  return EXTI4_IRQn;
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:  return EXTI9_5_IRQn;
        default: return EXTI15_10_IRQn;
    }
}

/* 中断统一分发 : 查到挂起标志 → 清标志 → 执行回调 */
static void exti_dispatch(uint8_t line)
{
    uint32_t mask = (uint32_t)(1UL << line);

    if (EXTI_GetITStatus(mask) != RESET) {
        EXTI_ClearITPendingBit(mask);

        if (exti_callback[line] != 0) exti_callback[line]();
    }
}


/* 基础功能 */
/* 初始化中断线 : SYSCFG 映射、EXTI 配置、NVIC 使能、回调注册 */
uint8_t SYS_EXTI_InitLine(uint8_t line, GPIO_TypeDef *port, uint16_t pin,
                          SysExtiTrigger_t trigger, void (*callback)(void))
{
    EXTI_InitTypeDef  ei;
    NVIC_InitTypeDef  ni;
    EXTITrigger_TypeDef trig;
    uint8_t           src;
    uint32_t          mask;

    if (line > 15 || callback == 0 || port == 0) return 0;

    /* 约束 : pin 必须等于 1UL << line，否则返回 0。
     * SYSCFG 只按 line 选端口，接入 EXTI 的是引脚号；两者不一致时中断
     * 永不触发。例 : (5, GPIOB, GPIO_Pin_3) 走线 3，不是线 5。 */
    if ((uint32_t)pin != (1UL << line)) return 0;

    src = exti_port_source(port);
    if (src == 0xFF) return 0;

    /* 开引脚端口时钟 : GPIO_ClockEnable（内部 RCC_AHB1PeriphClockCmd） */
    GPIO_ClockEnable(port);

    /* 开 SYSCFG 时钟 : RCC_APB2PeriphClockCmd；映射 : SYSCFG_EXTILineConfig
     * SYSCFG 挂 APB2，时钟未使能时映射写入被忽略，中断不触发。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    SYSCFG_EXTILineConfig(src, line);

    /* 触发方式换算 : SYS_EXTI_* → EXTI_Trigger_* */
    trig = EXTI_Trigger_Rising;
    switch (trigger) {
        case SYS_EXTI_FALLING: trig = EXTI_Trigger_Falling;          break;
        case SYS_EXTI_BOTH:    trig = EXTI_Trigger_Rising_Falling;   break;
        default:               trig = EXTI_Trigger_Rising;           break;
    }

    /* EXTI 配置 : EXTI_Init；先清一次挂起标志，避免残留误触发 */
    mask = (uint32_t)(1UL << line);
    EXTI_ClearITPendingBit(mask);

    EXTI_StructInit(&ei);
    ei.EXTI_Line    = mask;
    ei.EXTI_Mode    = EXTI_Mode_Interrupt;
    ei.EXTI_Trigger = trig;
    ei.EXTI_LineCmd = ENABLE;
    EXTI_Init(&ei);

    /* 注册回调 + NVIC 使能 : NVIC_Init */
    exti_callback[line] = callback;

    ni.NVIC_IRQChannel                   = exti_irqn(line);
    ni.NVIC_IRQChannelPreemptionPriority = SYS_EXTI_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_EXTI_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);

    return 1;
}

/* 关闭中断线 : 清标志、关 EXTI、注销回调，按需关 NVIC 通道 */
void SYS_EXTI_Disable(uint8_t line)
{
    EXTI_InitTypeDef ei;
    uint32_t         mask;

    if (line > 15) return;

    mask = (uint32_t)(1UL << line);

    EXTI_ClearITPendingBit(mask);

    EXTI_StructInit(&ei);
    ei.EXTI_Line    = mask;
    ei.EXTI_Mode    = EXTI_Mode_Interrupt;
    ei.EXTI_Trigger = EXTI_Trigger_Rising;   /* 禁用时该字段不生效 */
    ei.EXTI_LineCmd = DISABLE;
    EXTI_Init(&ei);

    exti_callback[line] = 0;

    /* 关 EXTI 线后还须关 NVIC 通道，否则中断仍进向量表，ISR 空返回。
     * 约束 : 多条线共用一个 IRQ 通道（线 5~9 → EXTI9_5_IRQn，
     * 线 10~15 → EXTI15_10_IRQn），关通道前须确认同组无其他线在用。 */
    {
        uint8_t          k;
        uint8_t          shared = 0U;
        NVIC_InitTypeDef ni;

        for (k = 0U; k <= 15U; k++) {
            if (k == line) continue;
            if (exti_callback[k] == 0) continue;
            if (exti_irqn(k) == exti_irqn(line)) { shared = 1U; break; }
        }

        if (shared == 0U) {
            ni.NVIC_IRQChannel                   = exti_irqn(line);
            ni.NVIC_IRQChannelPreemptionPriority = SYS_EXTI_IRQ_PRE_PRIO;
            ni.NVIC_IRQChannelSubPriority        = SYS_EXTI_IRQ_SUB_PRIO;
            ni.NVIC_IRQChannelCmd                = DISABLE;
            NVIC_Init(&ni);
        }
    }
}

/* 清除挂起标志 */
void SYS_EXTI_ClearFlag(uint8_t line)
{
    if (line > 15) return;

    EXTI_ClearITPendingBit((uint32_t)(1UL << line));
}

/* 查询挂起标志 */
uint8_t SYS_EXTI_GetFlag(uint8_t line)
{
    if (line > 15) return 0;

    return (EXTI_GetFlagStatus((uint32_t)(1UL << line)) != RESET) ? 1 : 0;
}


/* 扩展功能 */
/* 软件触发，用于不接硬件时验证回调逻辑 */
void SYS_EXTI_Trigger(uint8_t line)
{
    if (line > 15) return;

    EXTI_GenerateSWInterrupt((uint32_t)(1UL << line));
}

/* 运行中更换触发方式 : 只改 RTSR/FTSR，不碰回调与 NVIC */
uint8_t SYS_EXTI_SetTrigger(uint8_t line, SysExtiTrigger_t trigger)
{
    uint32_t mask;
    uint32_t rising;
    uint32_t falling;

    if (line > 15) return 0;

    mask = (uint32_t)(1UL << line);

    /* 触发方式 → 上升/下降沿开关 */
    switch (trigger) {
        case SYS_EXTI_FALLING: rising = 0U; falling = 1U; break;
        case SYS_EXTI_BOTH:    rising = 1U; falling = 1U; break;
        default:               rising = 1U; falling = 0U; break;
    }

    EXTI->IMR  &= ~mask;        /* 改配置期间先屏蔽该线，避免擦边误触发 */
    EXTI->RTSR  = rising  ? (EXTI->RTSR | mask) : (EXTI->RTSR & ~mask);
    EXTI->FTSR  = falling ? (EXTI->FTSR | mask) : (EXTI->FTSR & ~mask);
    EXTI->PR    = mask;         /* 清除旧边沿的挂起标志（写 1 清）   */
    EXTI->IMR  |= mask;         /* 恢复该线的中断屏蔽状态            */

    return 1;
}


/* =======================================================
 *                    中断服务函数（统一分发）
 * =======================================================
 * 7 个 ISR 均为弱定义（__weak）:应用侧手写同名 EXTIx_IRQHandler 会覆盖本实现，
 * 库回调随之停用；弱定义机制见 sys_tim.c 同段注释。 */
__weak void EXTI0_IRQHandler(void)     { exti_dispatch(0); }
__weak void EXTI1_IRQHandler(void)     { exti_dispatch(1); }
__weak void EXTI2_IRQHandler(void)     { exti_dispatch(2); }
__weak void EXTI3_IRQHandler(void)     { exti_dispatch(3); }
__weak void EXTI4_IRQHandler(void)     { exti_dispatch(4); }

__weak void EXTI9_5_IRQHandler(void)
{
    uint8_t line;

    for (line = 5; line <= 9; line++) {
        exti_dispatch(line);
    }
}

__weak void EXTI15_10_IRQHandler(void)
{
    uint8_t line;

    for (line = 10; line <= 15; line++) {
        exti_dispatch(line);
    }
}
