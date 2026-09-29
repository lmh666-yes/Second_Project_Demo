#ifndef __FWLIB_SYS_DMA_H
#define __FWLIB_SYS_DMA_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_dma.h —— 【系统】DMA 通用搬运模块  头文件
 * ================================================================
 *  设计定位 : 标准外设库 DMA 的"薄封装"—— 以"数据流(Stream)"为单位
 *             提供"外设↔内存"两种方向的搬运，并统一管理完成回调
 *  标准库关键词 : DMA_Init / DMA_Cmd / DMA_ITConfig / DMA_GetCurrDataCounter /
 *                 DMA_GetCmdStatus / DMA_DeInit / RCC_AHB1PeriphClockCmd
 *
 *  STM32F407 的 DMA 结构（理解这一点，整个模块就通了）:
 *      DMA1 / DMA2 各有 8 个"数据流"(Stream0 ~ Stream7)，
 *      每个流有 16 个"通道"(Channel0~15)可选——但一条流同一时刻
 *      只能绑定一个通道！通道 = 固定硬件连线（如 USART1_TX 固定
 *      走 DMA2_Stream7 的通道 4），选错了数据就搬不动。
 *      典型分工 : DMA1 给 APB1 外设，DMA2 给 APB2/内存外设。
 *
 *  使用方式（以"内存 → 外设"为例）:
 *      SYS_DMA_MemToPeriph(DMA1_Stream6, DMA_Channel_4,
 *                          (uint32_t)&USART2->DR,  // 外设数据口
 *                          buf, len,               // 内存源头
 *                          1, 0);                  // 字节宽/单次模式
 *
 *  与库内模块的关系 :
 *      sys_usart 的 DMA 收发、sys_adc 的 DMA 采集都建立在
 *      本模块之上；同一数据流不可被两个用途同时占用（冲突表
 *      见 README 的 sys_usart / sys_adc 章节）。
 *
 *  中断说明 : DMA1_Stream0~7、DMA2_Stream0~7 共 16 个中断
 *      服务函数全部在本模块定义（转发给注册的回调）；
 *      数据流启动时自动使能对应 NVIC 中断（优先级宏见区块 1），
 *      应用代码不要再重复定义同名函数。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* SYS_DMA_WaitDone 的默认等待上限（循环次数，按主频粗略估算）
 * 168MHz 下 1 个循环约 3~6 个时钟，1 千万次 ≈ 0.2~0.4 秒——
 * 只是"防死等"的兜底上限，正常传输远快于此 */
#define SYS_DMA_WAIT_LOOPS     10000000UL

/* -------------------- 中断优先级 -------------------- */
/* TC(传输完成)中断的 NVIC 优先级——SYS_DMA_SetCallback 的回调运行在其中断里。
 * 数值越小优先级越高；可用范围随 sys_nvic 的分组变化——
 *   NVIC_PriorityGroup_2（库默认）: 抢占 0~3、子 0~3（这里 2/0 = 与全库同级）
 *   NVIC_PriorityGroup_4（上 RTOS）: 抢占 0~15、子固定 0 */
#define SYS_DMA_IRQ_PRE_PRIO   2
#define SYS_DMA_IRQ_SUB_PRIO   0


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 传输完成(TC)回调类型：在 DMA 中断里被调用（"半传输/完成"各回调一次
 * 的话只上报完成——本模块固定使用"整笔完成"中断）
 * 注意 : 回调运行在中断上下文——只做标记/搬数据这类快操作，
 *        不要调用耗时函数（更不要在裸机回调里用 FreeRTOS 的
 *        非 FromISR 接口） */
typedef void (*SysDmaCallback_t)(void);

/* 注册某数据流的"传输完成回调"（在启动搬运之前注册）
 * 参数 : stream —— 数据流指针（DMA1_Stream0 ~ DMA2_Stream7）
 * 标准库 : 无直接调用——只登记回调表;对应中断在启动搬运时自动使能
 *          （中断内: DMA_GetITStatus → DMA_ClearITPendingBit → 回调）
 * 示例 : SYS_DMA_SetCallback(DMA1_Stream1, OnRxDone);   // 传你自己写的无参函数名 */
void SYS_DMA_SetCallback(DMA_Stream_TypeDef *stream, SysDmaCallback_t cb);

/* 启动"外设 → 内存"搬运（如：串口收数据、ADC 采集）
 * 参数 : stream      —— 数据流
 *        channel     —— 通道号，取值:标准库 DMA_Channel_0 ~ DMA_Channel_7
 *                       （外设→流/通道是芯片固定映射，查数据手册"DMA 请求
 *                       映射表"；常用例:USART1_RX = DMA2_Stream5 / DMA_Channel_4，
 *                       完整占用表见 README 5.8 / 5.13）
 *        periph_addr —— 外设数据寄存器地址，如 (uint32_t)&USART1->DR
 *        mem         —— 内存缓冲首地址
 *        len         —— 搬运个数（按 item_size 计数）
 *        item_size   —— 每次搬运字节数：1 = 字节（DMA_PeripheralDataSize_Byte /
 *                       DMA_MemoryDataSize_Byte），2 = 半字（对应 _HalfWord 两个宏）
 *        circular    —— 0 = 单次（搬完停，DMA_Mode_Normal），
 *                       1 = 循环（搬到尾回开头，DMA_Mode_Circular）
 * 说明 : 重复调用同一数据流会先停止旧的配置再重配
 *
 * 标准库调用链（库内部依次调用，可对照学习）:
 *   ① RCC_AHB1PeriphClockCmd     开 DMA1/DMA2 时钟（按数据流自动判断）
 *   ② DMA_DeInit + DMA_StructInit  复位数据流并填默认值
 *   ③ DMA_Init                   方向(DMA_DIR_PeripheralToMemory / _MemoryToPeripheral)/通道/地址/数量/优先级
 *   ④ DMA_ITConfig(TC)           开"传输完成"中断
 *   ⑤ NVIC_Init                  使能该数据流的中断向量
 *   ⑥ DMA_Cmd                    启动搬运
 * 示例 : uint8_t rxbuf[64];
 *        SYS_DMA_PeriphToMem(DMA1_Stream1, DMA_Channel_4,    // USART3_RX 固定映射
 *                            (uint32_t)&USART3->DR, rxbuf, 64,
 *                            1, 0);              // 字节宽 / 单次模式
 * 扩展提示 : 想加"半传输(HT)中断"——在 .c 的 dma_start 里给 DMA_ITConfig
 *            多开 DMA_IT_HT 并在中断分发里处理;反向搬运无需重写
 *            （先例:MemToPeriph 就是本函数的现成变体）*/
void SYS_DMA_PeriphToMem(DMA_Stream_TypeDef *stream, uint32_t channel,
                         uint32_t periph_addr, void *mem,
                         uint16_t len, uint8_t item_size, uint8_t circular);

/* 启动"内存 → 外设"搬运（如：串口发数据、DAC 输出）
 * 参数同 SYS_DMA_PeriphToMem（periph 与 mem 的数据方向相反）
 * 标准库 : 与上面同一套调用链（①~⑥ 同）
 * 示例 : SYS_DMA_MemToPeriph(DMA1_Stream6, DMA_Channel_4,             // USART2_TX 固定映射
 *                            (uint32_t)&USART2->DR, buf, len, 1, 0); */
void SYS_DMA_MemToPeriph(DMA_Stream_TypeDef *stream, uint32_t channel,
                         uint32_t periph_addr, const void *mem,
                         uint16_t len, uint8_t item_size, uint8_t circular);

/* 停止某数据流（关闭使能位；剩余标志保留，可用 Remain 查看）
 * 标准库 : DMA_Cmd(DISABLE)
 * 示例 : SYS_DMA_Stop(DMA1_Stream1);   // 立刻停搬(剩余数据保留待查) */
void SYS_DMA_Stop(DMA_Stream_TypeDef *stream);

/* 查询还剩多少个数据未搬（传输中实时递减；减到 0 即完成）
 * 标准库 : DMA_GetCurrDataCounter（读 NDTR 寄存器）
 * 示例 : uint16_t left = SYS_DMA_Remain(DMA1_Stream1);   // 还剩多少没搬 */
uint16_t SYS_DMA_Remain(DMA_Stream_TypeDef *stream);

/* 查询数据流是否还在工作：1 = 使能中（传输未结束），0 = 已停
 * 标准库 : DMA_GetCmdStatus
 * 示例 : while (SYS_DMA_Busy(DMA1_Stream6)) { ... }   // 1 = 还在搬 */
uint8_t SYS_DMA_Busy(DMA_Stream_TypeDef *stream);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 阻塞等待搬运结束（带等待上限，防止死等）
 * 参数 : stream —— 数据流；loops —— 等待上限循环次数（0 = 默认值）
 * 返回 : 1 = 已完成，0 = 等待超时（数据流仍在工作）
 * 示例 : SYS_DMA_MemToPeriph(...);
 *        SYS_DMA_WaitDone(DMA1_Stream6, 0);   // 等这批发完再往下走 */
uint8_t SYS_DMA_WaitDone(DMA_Stream_TypeDef *stream, uint32_t loops);


/* ================================================================
 *  附:标准库结构体速查 —— DMA_Stream_TypeDef（stm32f4xx.h;一条数据流）
 * ================================================================
 *  成员一览（含库中用法）:
 *    CR      配置:通道选择/方向(DMA_DIR_PeripheralToMemory / _MemoryToPeripheral)/
 *            地址增量/数据宽度/循环模式(DMA_Mode_Normal / _Circular)/优先级/
 *            中断使能位（DMA_Init、DMA_ITConfig、DMA_Cmd 全写它）
 *    NDTR    数据量:还剩多少没搬、递减到 0 完成
 *            （DMA_GetCurrDataCounter 读它——SYS_DMA_Remain 的"剩余"）
 *    PAR     外设地址:外设侧数据口（如 &USART2->DR）
 *    M0AR    内存地址 0:内存侧缓冲（双缓冲第二个用 M1AR）
 *    M1AR    内存地址 1:双缓冲用,库未使用
 *    FCR     FIFO 控制:直接模式即可,库保持默认
 *
 *  附:标准库结构体速查 —— DMA_TypeDef（DMA1/DMA2 各一份,流的容器）
 *    LISR / HISR      低/高 4 条流的中断状态:传输完成等标志
 *                     （中断内 DMA_GetITStatus / ClearITPendingBit 读写）
 *    LIFCR / HIFCR    低/高 4 条流的中断标志清除（写 1 清对应位）
 * ================================================================ */

#endif /* __FWLIB_SYS_DMA_H */
