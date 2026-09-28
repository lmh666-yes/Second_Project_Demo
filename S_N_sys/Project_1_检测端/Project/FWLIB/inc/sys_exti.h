#ifndef __FWLIB_SYS_EXTI_H
#define __FWLIB_SYS_EXTI_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_exti.h —— 【系统】外部中断(EXTI)模块  头文件
 * ================================================================
 *  设计定位 : 把"引脚电平跳变 → 执行一段代码"变简单
 *             —— 支持 EXTI0 ~ EXTI15 全部 16 条外部中断线
 *             —— 触发方式：上升沿 / 下降沿 / 双边沿（EXTI_Trigger_Rising / _Falling / _Rising_Falling）
 *             —— 回调注册机制：不用自己写 IRQHandler、不用查标志位
 *                （ISR 为弱定义——想自己手写 EXTIx_IRQHandler 也会自动顶替库版）
 *  标准库关键词 : GPIO_ClockEnable / RCC_APB2PeriphClockCmd / SYSCFG_EXTILineConfig /
 *                 EXTI_Init / NVIC_Init / EXTI_ClearITPendingBit
 *
 *  重要说明 :
 *   ① "线号 = 引脚号"：PA5 → 线 5、PC13 → 线 13 ……
 *      但同一时刻"每条线只能绑定一个引脚"——
 *      例如 PA0 与 PB0 都是线 0，不能同时开中断；
 *   ② 回调函数运行在"中断上下文"，原则是短小快速返回
 *      （工程做法：回调只置标志，耗时处理交给主循环）；
 *      —— 实验/演示若要在中断里"停住看效果"而需要延时：用
 *      gpio_core 的 Delay_ms()（纯忙等、不依赖任何中断，ISR 内安全）；
 *      ⚠ 不要用 SYS_TICK_Delay_ms()——它靠 SysTick 中断继续累加
 *      时基，优先级配置不当时会在 ISR 里永久死等；
 *   ③ EXTI5~9 共用一个中断向量、EXTI10~15 共用一个，
 *      本模块已自动分发到各自回调，应用层只管注册；
 *   ④ 引脚的方向 / 上下拉需自行配置
 *      （例如用按键时，key 模块的 KEY_Init() 已配好）。
 *
 *  使用方式（板载 KEY1/PA0 下降沿中断示例）:
 *      KEY_Init();                                 // ① 引脚先配好(输入+上拉)
 *      SYS_EXTI_InitLine(0, GPIOA, GPIO_Pin_0,     // ② 线 0 = PA0
 *                        SYS_EXTI_FALLING, MyKeyIsr);  // 按下(下降沿)→回调
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* -------------------- 中断优先级 -------------------- */
/* 数值越小优先级越高；可用范围随 sys_nvic 的分组变化——
 *   NVIC_PriorityGroup_2（库默认）: 抢占 0~3、子 0~3（这里的 2/0 = 与全库同级;
 *     想更急改 1 或 0，想更缓改 3）
 *   NVIC_PriorityGroup_4（上 RTOS）: 抢占 0~15、子固定 0 */
#define SYS_EXTI_IRQ_PRE_PRIO   2
#define SYS_EXTI_IRQ_SUB_PRIO   0


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 触发方式 */
typedef enum {
    SYS_EXTI_RISING  = 0,     /* 上升沿触发（标准库:EXTI_Trigger_Rising） */
    SYS_EXTI_FALLING = 1,     /* 下降沿触发（标准库:EXTI_Trigger_Falling） */
    SYS_EXTI_BOTH    = 2,     /* 双边沿触发（标准库:EXTI_Trigger_Rising_Falling） */
} SysExtiTrigger_t;

/* 初始化一条外部中断线并注册回调
 *
 * 标准库调用链（库内部依次调用，可对照学习）:
 *   ① GPIO_ClockEnable          开引脚端口时钟（gpio_core 工具）
 *   ② RCC_APB2PeriphClockCmd    开 SYSCFG 时钟（映射生效的前提）
 *   ③ SYSCFG_EXTILineConfig     引脚 → 中断线 映射（选端口源）
 *   ④ EXTI_Init                 边沿/模式/屏蔽配置（含触发方式换算）
 *   ⑤ NVIC_Init                 使能中断向量 + 优先级（宏见区块 1）
 *   （中断内自动完成: EXTI_GetITStatus → EXTI_ClearITPendingBit → 回调）
 *
 * 参数 : line     —— 中断线号 0 ~ 15（与引脚号一致：PA0→0, PC13→13）
 *        port/pin —— 绑定的引脚（如 GPIOA, GPIO_Pin_0）
 *        trigger  —— 触发方式，三选一:
 *                    SYS_EXTI_RISING  上升沿（低 → 高）
 *                    SYS_EXTI_FALLING 下降沿（高 → 低）
 *                    SYS_EXTI_BOTH    双边沿
 *        callback —— 中断回调函数（中断上下文执行，保持短小）
 * 返回 : 1 = 成功；0 = 参数非法
 * 示例 : SYS_EXTI_InitLine(13, GPIOC, GPIO_Pin_13,      // PC13 → 线 13
 *                          SYS_EXTI_FALLING, OnTouch);  // 下降沿→回调
 * 扩展提示 : ① 多个引脚共用一个回调 —— 每条线各调一次、回调传同一个;
 *            ② 运行中换边沿 —— 用 SYS_EXTI_SetTrigger,无需重绑;
 *            ③ 不接硬件先测 —— SYS_EXTI_Trigger(line) 软件触发一次
 * ⚠ 同线互斥 : 每个线号全局只能有一个使用者——如 PA0 已占线 0，
 *    再给 PB0/PC0（同为线 0）调用本函数会"抢占"：SYSCFG 改选
 *    端口源后，原引脚不再触发、原回调也被替换（后绑覆盖先绑）。
 *    绑定前先确认该线空闲（按键已占线 0/2/3/4，占用表见 README 5.10） */
uint8_t SYS_EXTI_InitLine(uint8_t line, GPIO_TypeDef *port, uint16_t pin,
                          SysExtiTrigger_t trigger, void (*callback)(void));

/* 关闭指定中断线（清除挂起标志并注销回调）
 * 标准库 : EXTI_ClearITPendingBit + EXTI_Init(LineCmd = DISABLE)
 * 示例 : SYS_EXTI_Disable(13);   // 关闭线 13 的中断 */
void SYS_EXTI_Disable(uint8_t line);

/* 清除指定中断线的挂起标志
 * 标准库 : EXTI_ClearITPendingBit
 * 示例 : SYS_EXTI_ClearFlag(0);   // 清线 0 的挂起标志 */
void SYS_EXTI_ClearFlag(uint8_t line);

/* 查询指定中断线是否有挂起标志：1 = 有，0 = 无
 * 标准库 : EXTI_GetFlagStatus
 * 示例 : if (SYS_EXTI_GetFlag(0)) { ... }   // 线 0 有标志未清 */
uint8_t SYS_EXTI_GetFlag(uint8_t line);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 软件触发一次指定中断线（无需外部电平变化即可测试回调逻辑）
 * 标准库 : EXTI_GenerateSWInterrupt
 * 示例 : SYS_EXTI_Trigger(0);      // 手动触发线 0,测试回调逻辑 */
void SYS_EXTI_Trigger(uint8_t line);

/* 运行中更换触发方式（不改回调、不动 NVIC）
 * 参数 : trigger —— 触发方式，取值同 SYS_EXTI_InitLine:
 *                   SYS_EXTI_RISING / SYS_EXTI_FALLING / SYS_EXTI_BOTH
 * 用途 : 同一引脚在不同阶段需要不同边沿（如下降沿唤醒→双边沿计数）
 * 返回 : 1 = 成功；0 = 参数非法（line > 15）
 * 示例 : SYS_EXTI_SetTrigger(0, SYS_EXTI_BOTH);   // 线 0 改成双边沿
 * 说明 : 切换时自动清一次挂起标志，避免旧边沿残留误触发；
 *        标准库没有"只改边沿"的单函数（EXTI_Init 需整包重填），
 *        故本函数直接操作 EXTI->RTSR / FTSR 寄存器——更轻、不动其它位 */
uint8_t SYS_EXTI_SetTrigger(uint8_t line, SysExtiTrigger_t trigger);


/* ================================================================
 *  附:标准库结构体速查 —— EXTI_TypeDef（定义在 stm32f4xx.h）
 * ================================================================
 *  16 条外部中断线共用这一组寄存器;成员一览（含库中用法）:
 *    IMR    中断屏蔽:1 = 允许该线产生中断（EXTI_Init 的使能/禁止
 *           写它;SYS_EXTI_SetTrigger 改边沿前后会临时清/恢复）
 *    EMR    事件屏蔽:1 = 只产生事件不触发中断（库未用）
 *    RTSR   上升沿触发选择:1 = 该线上升沿触发（SetTrigger 写它）
 *    FTSR   下降沿触发选择:1 = 该线下降沿触发（SetTrigger 写它）
 *    SWIER  软件中断事件:写 1 = 模拟一次触发（SYS_EXTI_Trigger 用）
 *    PR     挂起标志:1 = 有触发待处理,写 1 清除
 *           （清标志/查标志、以及中断里的"清")就是读写它）
 *
 *  附:标准库结构体速查 —— SYSCFG_TypeDef（定义在 stm32f4xx.h）
 *    MEMRMP    内存重映射:SWJ_CFG 位选择调试口占用——置 0b010 =
 *              只关 JTAG 保留 SWD（SYS_SPI_Init 释放 PB3/PB4 时写它;
 *              sys_eth 用位 23 切 RMII 以太网模式）
 *    PMC       外设模式:位 23 = RMII 模式（sys_eth.c 写它）
 *    EXTICR[4] 外部中断配置:每线 4 位,选择"哪组端口"映射到该线
 *              （SYSCFG_EXTILineConfig 写它——PA0/PB0/PC0 选哪个
 *                就在这;同线互斥的根源）
 *    CMPCR     补偿单元控制:库未使用
 * ================================================================ */

#endif /* __FWLIB_SYS_EXTI_H */
