#ifndef __FWLIB_SYS_DMA_H
#define __FWLIB_SYS_DMA_H

#include "stm32f4xx.h"

/* sys_dma.h : DMA 通用搬运模块
 *
 * 结构(STM32F407):
 *      DMA1 / DMA2 各有 8 个数据流 Stream0~Stream7,每个流可选
 *      16 个通道 Channel0~15 中的一个;通道是固定硬件连线
 *      (如 USART1_TX 固定走 DMA2_Stream7 的通道 4),选错搬不动数据
 *      典型分工:DMA1 给 APB1 外设,DMA2 给 APB2/内存外设
 *
 * 中断:DMA1_Stream0~7、DMA2_Stream0~7 共 16 个中断服务函数全部在本模块
 *      定义并转发给注册的回调;数据流启动时自动使能对应 NVIC 中断,
 *      应用代码不要再重复定义同名函数
 */


/* SYS_DMA_WaitDone 的默认等待上限(循环次数,按主频粗略估算)
 * 168MHz 下 1 个循环约 3~6 个时钟,1 千万次约 0.2~0.4 秒 */
#define SYS_DMA_WAIT_LOOPS     10000000UL

/* TC(传输完成)中断的 NVIC 优先级;SYS_DMA_SetCallback 的回调运行在其中断里
 * 数值越小优先级越高;可用范围随 sys_nvic 的分组变化:
 *   NVIC_PriorityGroup_4(本库默认): 抢占 0~15、子固定 0
 *   NVIC_PriorityGroup_2(纯裸机)  : 抢占 0~3、子 0~3
 * 用 FreeRTOS 时必须 ≥5(默认已给 5):FreeRTOS 用 BASEPRI =
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY<<4(本工程 5<<4 = 0x50)屏蔽可调
 * FromISR 的中断;抢占值 < 5 的中断能打断内核临界区,其中禁止调用任何
 * ...FromISR() 接口,开 configASSERT 会断言失败。 */
#define SYS_DMA_IRQ_PRE_PRIO   5
#define SYS_DMA_IRQ_SUB_PRIO   0


/* 传输完成(TC)回调类型,在 DMA 中断里被调用
 * 说明 : 本模块固定使用整笔完成中断,不使用半传输中断;
 *        回调运行在中断上下文,只做标记/搬数据这类快操作,
 *        不要调用耗时函数,也不要在裸机回调里用 FreeRTOS 的非 FromISR 接口 */
typedef void (*SysDmaCallback_t)(void);

/* 注册某数据流的传输完成回调
 * 参数 : stream:数据流指针(DMA1_Stream0 ~ DMA2_Stream7)
 * 说明 : 必须在 SYS_DMA_PeriphToMem / SYS_DMA_MemToPeriph 之前注册,
 *        这两个函数按此刻有没有回调决定是否开 TC 中断与 NVIC;
 *        不注册回调时该流纯搬运、不进中断 */
void SYS_DMA_SetCallback(DMA_Stream_TypeDef *stream, SysDmaCallback_t cb);

/* 启动外设 → 内存搬运(如串口收数据、ADC 采集)
 * 参数 : stream:数据流
 *        channel:通道号,取值 DMA_Channel_0 ~ DMA_Channel_7;
 *                外设到流/通道是芯片固定映射,需查数据手册的 DMA 请求映射表,
 *                常用例:USART1_RX = DMA2_Stream5 / DMA_Channel_4
 *        periph_addr:外设数据寄存器地址,如 (uint32_t)&USART1->DR
 *        mem:内存缓冲首地址
 *        len:搬运个数,按 item_size 计数
 *        item_size:每次搬运字节数,1 = 字节(DMA_PeripheralDataSize_Byte /
 *                 DMA_MemoryDataSize_Byte),2 = 半字(对应的 _HalfWord 两个宏)
 *        circular:0 = 单次(DMA_Mode_Normal),1 = 循环(DMA_Mode_Circular)
 * 说明 : 重复调用同一数据流会先停止旧配置再重配 */
void SYS_DMA_PeriphToMem(DMA_Stream_TypeDef *stream, uint32_t channel,
                         uint32_t periph_addr, void *mem,
                         uint16_t len, uint8_t item_size, uint8_t circular);

/* 启动内存 → 外设搬运(如串口发数据、DAC 输出)
 * 参数同 SYS_DMA_PeriphToMem,periph 与 mem 的数据方向相反 */
void SYS_DMA_MemToPeriph(DMA_Stream_TypeDef *stream, uint32_t channel,
                         uint32_t periph_addr, const void *mem,
                         uint16_t len, uint8_t item_size, uint8_t circular);

/* 停止某数据流,关闭使能位;剩余计数保留,可用 SYS_DMA_Remain 查看 */
void SYS_DMA_Stop(DMA_Stream_TypeDef *stream);

/* 查询还剩多少个数据未搬,传输中实时递减,减到 0 即完成(读 NDTR 寄存器) */
uint16_t SYS_DMA_Remain(DMA_Stream_TypeDef *stream);

/* 查询数据流是否还在工作:1 = 使能中(传输未结束),0 = 已停 */
uint8_t SYS_DMA_Busy(DMA_Stream_TypeDef *stream);


/* 阻塞等待搬运结束,带等待上限防止死等
 * 参数 : stream:数据流;loops:等待上限循环次数(0 = 用 SYS_DMA_WAIT_LOOPS)
 * 返回 : 1 = 已完成,0 = 等待超时(数据流仍在工作) */
uint8_t SYS_DMA_WaitDone(DMA_Stream_TypeDef *stream, uint32_t loops);

/* 查询某数据流有没有搬出错(TE / FE / DME)
 * 返回 : 0 = 没有错误;1 = TE 传输错误;2 = FE FIFO 错误;3 = DME 直接模式错误
 * 说明 : 本函数只读不清,清标志由 WaitDone / 中断负责,可以随时调用;
 *        调用 SYS_DMA_WaitDone 判超时后,再查本函数,非 0 就是本次搬挂了
 * 标准库 : 直读 LISR / HISR(DMA_GetFlagStatus 一次只能查一个流,
 *          还要按流号选不同的宏,本模块统一按 6 位标志组读) */
uint8_t SYS_DMA_GetError(DMA_Stream_TypeDef *stream);

/* DMA 传输错误累计次数,全局,跨所有 16 条流
 * 用途 : 偶发错误看单次返回值;持续增长说明是系统性问题
 *        (外设时钟没开、地址算错、宽度配错) */
uint32_t SYS_DMA_ErrCount(void);

/* 最近一次 DMA 错误的类型与流号
 * 返回 : LastErrKind:1 = TE / 2 = FE / 3 = DME(0 = 还没出过错)
 *        LastErrStream:出错流号(0~7 = DMA1_Stream0~7,8~15 = DMA2_Stream0~7) */
uint8_t SYS_DMA_LastErrKind(void);
uint8_t SYS_DMA_LastErrStream(void);

/* 清空 DMA 错误计数与最近错误记录 */
void SYS_DMA_ErrClear(void);


/* 附:标准库结构体速查, DMA_Stream_TypeDef(stm32f4xx.h;一条数据流)
 *  成员一览(含库中用法):
 *    CR      配置:通道选择/方向(DMA_DIR_PeripheralToMemory / _MemoryToPeripheral)/
 *            地址增量/数据宽度/循环模式(DMA_Mode_Normal / _Circular)/优先级/
 *            中断使能位(DMA_Init、DMA_ITConfig、DMA_Cmd 全写它)
 *    NDTR    数据量:还剩多少没搬、递减到 0 完成
 *            (DMA_GetCurrDataCounter 读它,对应 SYS_DMA_Remain)
 *    PAR     外设地址:外设侧数据口(如 &USART2->DR)
 *    M0AR    内存地址 0:内存侧缓冲(双缓冲第二个用 M1AR)
 *    M1AR    内存地址 1:双缓冲用,库未使用
 *    FCR     FIFO 控制:直接模式即可,库保持默认
 *
 *  附:标准库结构体速查, DMA_TypeDef(DMA1/DMA2 各一份,流的容器)
 *    LISR / HISR      低/高 4 条流的中断状态:传输完成等标志
 *                     (中断内 DMA_GetITStatus / ClearITPendingBit 读写)
 *    LIFCR / HIFCR    低/高 4 条流的中断标志清除(写 1 清对应位) */

#endif /* __FWLIB_SYS_DMA_H */
