#ifndef __FWLIB_SYS_DMA_H
#define __FWLIB_SYS_DMA_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_dma.h — 【系统】DMA 通用搬运模块 头文件
 * ================================================================
 *  以数据流(Stream)为单位，提供外设↔内存两个方向的搬运与完成回调。
 *  DMA1/DMA2 各 8 条流(Stream0~7)，每流同一时刻只绑定 1 个通道(Channel0~15)；
 *  外设到流/通道是芯片固定映射（如 USART1_TX = DMA2_Stream7/Channel4），请求
 *  映射表与占用表见 README 5.8/5.13。
 *  16 个流中断(DMA1_Stream0~7、DMA2_Stream0~7)在本模块定义并转发给注册回调，
 *  启动流时按有无回调自动使能 NVIC，应用不要重复定义同名函数。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* SYS_DMA_WaitDone 的默认等待上限（循环次数）
 * 168MHz 下 1 次循环约 3~6 个时钟 */
#define SYS_DMA_WAIT_LOOPS     10000000UL

/* -------------------- 中断优先级 -------------------- */
/* TC(传输完成)中断的 NVIC 优先级，SYS_DMA_SetCallback 的回调运行在其中断里；
 * 数值越小优先级越高，可用范围随 sys_nvic 分组: NVIC_PriorityGroup_4（本库
 * 默认）抢占 0~15；NVIC_PriorityGroup_2 抢占 0~3、子 0~3。
 * FreeRTOS 下须 ≥5（本工程默认 5）: BASEPRI =
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY<<4 = 5<<4 = 0x50 屏蔽可调
 * ...FromISR() 的中断；抢占 < 5 会打断内核临界区，开 configASSERT 时断言失败。 */
#define SYS_DMA_IRQ_PRE_PRIO   5
#define SYS_DMA_IRQ_SUB_PRIO   0


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 传输完成(TC)回调类型，在 DMA 中断里调用；只用整笔完成中断，不用半传输(HT)。
 * 回调在中断上下文，只做置标志、搬数据等快操作；不得在其中调 FreeRTOS 的
 * 非 FromISR 接口 */
typedef void (*SysDmaCallback_t)(void);

/* 注册某数据流的传输完成回调
 * 参数 : stream — 数据流指针（DMA1_Stream0 ~ DMA2_Stream7）
 * 标准库 : 无直接调用，只登记回调表；中断内依次执行
 *          DMA_GetITStatus → DMA_ClearITPendingBit → 回调
 * 约束 : 须在启动函数之前注册；启动函数按注册时有无回调决定是否开 TC 中断与
 *        NVIC，未注册的流只搬运不进中断
 * 示例 : SYS_DMA_SetCallback(DMA1_Stream1, OnRxDone); */
void SYS_DMA_SetCallback(DMA_Stream_TypeDef *stream, SysDmaCallback_t cb);

/* 启动"外设 → 内存"搬运（如串口收数据、ADC 采集）
 * 参数 : stream — 数据流；channel — DMA_Channel_0 ~ DMA_Channel_7，外设到
 *          流/通道是芯片固定映射，查数据手册"DMA 请求映射表"，例:
 *          USART1_RX = DMA2_Stream5/DMA_Channel_4，占用表见 README 5.8/5.13
 *        periph_addr — 外设数据寄存器地址，如 (uint32_t)&USART1->DR
 *        mem — 内存缓冲首地址；len — 搬运个数，0 < len ≤ 65535（按 item_size 计）
 *        item_size — 1 = 字节（_Byte 两个宏），2 = 半字（_HalfWord 两个宏）
 *        circular — 0 = 单次（DMA_Mode_Normal），1 = 循环（DMA_Mode_Circular）
 * 说明 : 重复调用同一数据流会先停旧配置再重配
 * 标准库调用链: RCC_AHB1PeriphClockCmd → DMA_DeInit + DMA_StructInit →
 *    DMA_Init → DMA_ITConfig(TC) → NVIC_Init → DMA_Cmd
 * 示例 : SYS_DMA_PeriphToMem(DMA1_Stream1, DMA_Channel_4,    // USART3_RX
 *                            (uint32_t)&USART3->DR, buf, 64, 1, 0); */
void SYS_DMA_PeriphToMem(DMA_Stream_TypeDef *stream, uint32_t channel,
                         uint32_t periph_addr, void *mem,
                         uint16_t len, uint8_t item_size, uint8_t circular);

/* 启动"内存 → 外设"搬运（如串口发数据、DAC 输出）
 * 参数同 SYS_DMA_PeriphToMem（periph 与 mem 方向相反），标准库调用链相同
 * 示例 : SYS_DMA_MemToPeriph(DMA1_Stream6, DMA_Channel_4,    // USART2_TX
 *                            (uint32_t)&USART2->DR, buf, len, 1, 0); */
void SYS_DMA_MemToPeriph(DMA_Stream_TypeDef *stream, uint32_t channel,
                         uint32_t periph_addr, const void *mem,
                         uint16_t len, uint8_t item_size, uint8_t circular);

/* 停止某数据流（关闭使能位；NDTR 剩余值保留，可用 SYS_DMA_Remain 查看）
 * 标准库 : DMA_Cmd(DISABLE) */
void SYS_DMA_Stop(DMA_Stream_TypeDef *stream);

/* 查询还剩多少个数据未搬（传输中实时递减，减到 0 即完成）
 * 返回 : 剩余个数，0 ~ len
 * 标准库 : DMA_GetCurrDataCounter（读 NDTR 寄存器） */
uint16_t SYS_DMA_Remain(DMA_Stream_TypeDef *stream);

/* 查询数据流是否还在工作：1 = 使能中（传输未结束），0 = 已停
 * 标准库 : DMA_GetCmdStatus */
uint8_t SYS_DMA_Busy(DMA_Stream_TypeDef *stream);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 阻塞等待搬运结束（有等待上限）
 * 参数 : stream — 数据流；loops — 等待上限循环次数（0 = 用 SYS_DMA_WAIT_LOOPS）
 * 返回 : 1 = 已完成，0 = 等待超时（数据流仍在工作） */
uint8_t SYS_DMA_WaitDone(DMA_Stream_TypeDef *stream, uint32_t loops);

/* 查询某数据流是否搬运出错（TE / FE / DME）
 * 返回 : 0 = 无错误；1 = TE 传输错误；2 = FE FIFO 错误；3 = DME 直接模式错误
 * 说明 : TE/FE/DME 与完成标志同在 16 个标志位中，只查完成标志会把搬一半当成功
 *        （数组后半段为旧值仍返回成功）；本函数只读不清，清标志由
 *        SYS_DMA_WaitDone 与中断负责
 * 标准库 : 直读 LISR / HISR（不用 DMA_GetFlagStatus，它一次只能查一条流） */
uint8_t SYS_DMA_GetError(DMA_Stream_TypeDef *stream);

/* DMA 传输错误累计次数（全局，覆盖 16 条流）
 * 用途 : 偶发错误看单次返回值即可；持续增长为系统性问题，数据不可当有效值用 */
uint32_t SYS_DMA_ErrCount(void);

/* 最近一次 DMA 错误的类型与流号
 * 返回 : SYS_DMA_LastErrKind — 0 = 未出错 / 1 = TE / 2 = FE / 3 = DME；
 *        SYS_DMA_LastErrStream — 0~7 = DMA1_Stream0~7，8~15 = DMA2_Stream0~7 */
uint8_t SYS_DMA_LastErrKind(void);
uint8_t SYS_DMA_LastErrStream(void);

/* 清空 DMA 错误累计次数与最近错误记录 */
void SYS_DMA_ErrClear(void);


/* ================================================================
 *  附:标准库结构体速查（stm32f4xx.h）
 * ================================================================
 *  DMA_Stream_TypeDef（一条流）: CR 通道/方向/增量/宽度/循环模式/优先级/中断
 *    使能位（DMA_Init、DMA_ITConfig、DMA_Cmd 都写它）；NDTR 剩余个数，
 *    DMA_GetCurrDataCounter 读；PAR 外设数据口；M0AR 内存缓冲（M1AR 双缓冲本
 *    库未用）；FCR FIFO 控制，库用默认。
 *  DMA_TypeDef: LISR/HISR 低/高 4 条流的中断状态标志（DMA_GetITStatus /
 *    ClearITPendingBit）；LIFCR/HIFCR 写 1 清位。
 * ================================================================ */

#endif /* __FWLIB_SYS_DMA_H */
