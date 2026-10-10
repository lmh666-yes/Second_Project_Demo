#include "sys_exti.h"
#include "gpio_core.h"

/* sys_exti.c: 外部中断（EXTI）模块实现
 * 引脚到中断线的绑定由 SYSCFG 完成，每线绑一个端口；线 0~4 各有独立中断向量，
 * 线 5~9 共用 EXTI9_5，线 10~15 共用 EXTI15_10，本文件统一分发到回调表。
 * EXTI0/1/2/3/4/9_5/15_10_IRQHandler 在本文件定义，重复定义会链接报重复符号。 */


/* 回调表：下标 = 中断线号（0~15），0 = 未注册 */
static void (*exti_callback[16])(void);


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
    return 0xFF;    /* F407ZG 只有 GPIOA ~ GPIOG */
}

/* 线号 → 所属中断向量（0~4 独立；5~9、10~15 各自共用） */
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

/* 中断统一分发：有挂起标志则清标志并执行回调 */
static void exti_dispatch(uint8_t line)
{
    uint32_t mask = (uint32_t)(1UL << line);

    if (EXTI_GetITStatus(mask) != RESET) {
        EXTI_ClearITPendingBit(mask);

        if (exti_callback[line] != 0) exti_callback[line]();
    }
}


/* 初始化一条中断线：SYSCFG 映射、EXTI 配置、NVIC 使能、注册回调 */
uint8_t SYS_EXTI_InitLine(uint8_t line, GPIO_TypeDef *port, uint16_t pin,
                          SysExtiTrigger_t trigger, void (*callback)(void))
{
    EXTI_InitTypeDef  ei;
    NVIC_InitTypeDef  ni;
    EXTITrigger_TypeDef trig;
    uint8_t           src;
    uint32_t          mask;

    if (line > 15 || callback == 0 || port == 0) return 0;

    /* line 必须与引脚掩码一致（pin == 1 << line），否则返回 0。
     * line 只决定该线挂哪个端口，真正接到 EXTI 的是引脚号，不一致时中断不触发。 */
    if ((uint32_t)pin != (1UL << line)) return 0;

    src = exti_port_source(port);
    if (src == 0xFF) return 0;

    /* 1) 开引脚端口时钟：GPIO_ClockEnable，内部为 RCC_AHB1PeriphClockCmd */
    GPIO_ClockEnable(port);

    /* 2) 开 SYSCFG 时钟（APB2）并做线映射：RCC_APB2PeriphClockCmd + SYSCFG_EXTILineConfig；
     * 时钟未开时映射写入被硬件忽略，中断不会触发 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    SYSCFG_EXTILineConfig(src, line);

    /* 3) 触发方式换算：SYS_EXTI_* 转 EXTI_Trigger_* */
    trig = EXTI_Trigger_Rising;
    switch (trigger) {
        case SYS_EXTI_FALLING: trig = EXTI_Trigger_Falling;          break;
        case SYS_EXTI_BOTH:    trig = EXTI_Trigger_Rising_Falling;   break;
        default:               trig = EXTI_Trigger_Rising;           break;
    }

    /* 4) EXTI 配置：EXTI_Init；先清一次挂起标志，避免残留误触发 */
    mask = (uint32_t)(1UL << line);
    EXTI_ClearITPendingBit(mask);

    EXTI_StructInit(&ei);
    ei.EXTI_Line    = mask;
    ei.EXTI_Mode    = EXTI_Mode_Interrupt;
    ei.EXTI_Trigger = trig;
    ei.EXTI_LineCmd = ENABLE;
    EXTI_Init(&ei);

    /* 5) 注册回调并做 NVIC 使能：NVIC_Init */
    exti_callback[line] = callback;

    ni.NVIC_IRQChannel                   = exti_irqn(line);
    ni.NVIC_IRQChannelPreemptionPriority = SYS_EXTI_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_EXTI_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);

    return 1;
}

/* 关闭中断线：清标志、关 EXTI、注销回调；本通道无其它已注册线时关 NVIC */
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

    /* 只关 EXTI 线不够，NVIC 通道也要关：不关则中断仍进向量表，每次在 ISR 里查一遍
     * EXTI_GetITStatus 后空返回，浪费 CPU；引脚电平抖动时会形成中断风暴。
     * EXTI 多条线共用一个 IRQ 通道（线 5~9 用 EXTI9_5_IRQn，线 10~15 用
     * EXTI15_10_IRQn，线 0~4 各自独立），直接关会一并废掉同组其它在用的线，先扫描确认。 */
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


/* 软件触发（用于不接硬件时验证回调逻辑） */
void SYS_EXTI_Trigger(uint8_t line)
{
    if (line > 15) return;

    EXTI_GenerateSWInterrupt((uint32_t)(1UL << line));
}

/* 运行中更换触发方式：只改 RTSR/FTSR，不碰回调与 NVIC */
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

    EXTI->IMR  &= ~mask;        /* 改配置期间先屏蔽该线，避免误触发 */
    EXTI->RTSR  = rising  ? (EXTI->RTSR | mask) : (EXTI->RTSR & ~mask);
    EXTI->FTSR  = falling ? (EXTI->FTSR | mask) : (EXTI->FTSR & ~mask);
    EXTI->PR    = mask;         /* 清除旧边沿的挂起标志（写 1 清）   */
    EXTI->IMR  |= mask;         /* 恢复该线的中断屏蔽状态            */

    return 1;
}


/* 中断服务函数：全部为弱定义（__weak），手写同名 EXTIx_IRQHandler 会顶替库版，
 * 顶替后该线的库回调随之停用；弱定义机制见 sys_tim.c 同段注释 */
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
