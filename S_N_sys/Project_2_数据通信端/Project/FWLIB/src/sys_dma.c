#include "sys_dma.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */

/* ================================================================
 *  sys_dma.c —— 【系统】DMA 通用搬运模块  实现文件
 * ================================================================
 *  内部结构 :
 *    dma_cb[16]  —— 回调表（下标 0~7 = DMA1_Stream0~7，
 *                   8~15 = DMA2_Stream0~7）
 *    X-Macro     —— 用一张"数据流清单"宏批量生成 16 个中断服务函数，
 *                   避免手写 16 段几乎一样的代码（改一处=全改）
 *
 *  中断服务函数 : DMA1_Stream0~7 / DMA2_Stream0~7 共 16 个
 *  在本文件定义，应用代码不要再重复定义。
 * ================================================================ */


/* ================================================================
 *                    内部辅助
 * ================================================================ */
/* 回调表：16 个数据流通用一张表 */
static SysDmaCallback_t dma_cb[16];

/* 数据流指针 → 全局编号（0~7 = DMA1_Stream0~7，8~15 = DMA2_Stream0~7）
 * 返回 -1 = 不是合法的数据流指针（防御性检查，避免误写寄存器）
 * 注：0x18 = 每个 Stream 寄存器块的字节间距（数据手册寄存器映射） */
static int dma_index(DMA_Stream_TypeDef *stream)
{
    uint32_t s = (uint32_t)stream;

    if ((s >= (uint32_t)DMA2_Stream0) && (s <= (uint32_t)DMA2_Stream7)) {
        return 8 + (int)((s - (uint32_t)DMA2_Stream0) / 0x18U);
    }
    if ((s >= (uint32_t)DMA1_Stream0) && (s <= (uint32_t)DMA1_Stream7)) {
        return (int)((s - (uint32_t)DMA1_Stream0) / 0x18U);
    }
    return -1;
}

/* 打开数据流所在 DMA 控制器的 AHB1 时钟 */
static void dma_clock_enable(int gi)
{
    if (gi < 8) RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
    else        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
}

/* 全局编号(0~15) → 对应 NVIC 中断号
 * 注意 : DMA1_Stream7 的中断号是 47（不与 11~17 连续），别找错 */
static IRQn_Type dma_irqn(int gi)
{
    static const IRQn_Type tab[16] = {
        DMA1_Stream0_IRQn, DMA1_Stream1_IRQn, DMA1_Stream2_IRQn, DMA1_Stream3_IRQn,
        DMA1_Stream4_IRQn, DMA1_Stream5_IRQn, DMA1_Stream6_IRQn, DMA1_Stream7_IRQn,
        DMA2_Stream0_IRQn, DMA2_Stream1_IRQn, DMA2_Stream2_IRQn, DMA2_Stream3_IRQn,
        DMA2_Stream4_IRQn, DMA2_Stream5_IRQn, DMA2_Stream6_IRQn, DMA2_Stream7_IRQn
    };
    return tab[gi];
}

/* 清掉某个数据流的全部中断标志（TC/HT/TE/FE/DME）
 * 原理 : 每个流占 LISR/HISR 里连续的 6 个位（0~3 号流在 LISR，
 *       4~7 号流在 HISR），对清除寄存器 LIFCR/HIFCR 对应位写 1
 *       即清除——一次性清 6 位，不用管具体是哪种事件 */
static void dma_clear_flags(int gi)
{
    DMA_TypeDef        *D;
    uint8_t             idx;
    uint32_t            mask;

    /* 找出该流在所属 DMA 控制器内的序号(0~7) 并选定控制器 */
    if (gi < 8) {
        idx = (uint8_t)gi;
        D   = DMA1;
    } else {
        idx = (uint8_t)(gi - 8);
        D   = DMA2;
    }

    mask = 0x3FUL << ((idx & 3U) * 6U);     /* 一个流占 6 个标志位 */

    if (idx < 4U) D->LIFCR = mask;          /* 低 4 个流 → LIFCR */
    else          D->HIFCR = mask;          /* 高 4 个流 → HIFCR */
}

/* 两种方向共用的初始化主体
 * 说明 : 外设地址固定不递增、内存地址自动递增（搬运的典型模式），
 *        外设与内存的数据宽度一致（item_size：1=字节，2=半字） */
static void dma_start(DMA_Stream_TypeDef *stream, uint32_t channel,
                      uint32_t periph_addr, uint32_t mem_addr,
                      uint32_t direction, uint16_t len,
                      uint8_t item_size, uint8_t circular)
{
    DMA_InitTypeDef  di;
    NVIC_InitTypeDef ni;
    int gi = dma_index(stream);

    if (gi < 0 || len == 0U) return;

    /* 先停掉本流可能残留的旧配置，再重置为默认值 */
    DMA_Cmd(stream, DISABLE);
    DMA_DeInit(stream);
    dma_clock_enable(gi);

    DMA_StructInit(&di);
    di.DMA_Channel            = channel;
    di.DMA_PeripheralBaseAddr = periph_addr;
    di.DMA_Memory0BaseAddr    = mem_addr;
    di.DMA_DIR                = direction;
    di.DMA_BufferSize         = len;
    di.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;   /* 外设地址不动 */
    di.DMA_MemoryInc          = DMA_MemoryInc_Enable;        /* 内存地址递增 */
    di.DMA_PeripheralDataSize = (item_size == 2U) ? DMA_PeripheralDataSize_HalfWord
                                                  : DMA_PeripheralDataSize_Byte;
    di.DMA_MemoryDataSize     = (item_size == 2U) ? DMA_MemoryDataSize_HalfWord
                                                  : DMA_MemoryDataSize_Byte;
    di.DMA_Mode               = circular ? DMA_Mode_Circular : DMA_Mode_Normal;
    di.DMA_Priority           = DMA_Priority_High;
    di.DMA_FIFOMode           = DMA_FIFOMode_Disable;        /* 直通模式，无需 FIFO */
    di.DMA_FIFOThreshold      = DMA_FIFOThreshold_HalfFull;
    di.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    di.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;
    DMA_Init(stream, &di);

    /* 开"传输完成"中断（回调在 DMAx_Streamy_IRQHandler 里转发）：
     * ① DMA 内部 TC 中断源；② NVIC 对应中断——缺了 ②，
     *    TC 只会把标志挂起，回调永远不会被执行 */
    dma_clear_flags(gi);
    DMA_ITConfig(stream, DMA_IT_TC, ENABLE);

    ni.NVIC_IRQChannel                   = dma_irqn(gi);
    ni.NVIC_IRQChannelPreemptionPriority = SYS_DMA_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_DMA_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);

    DMA_Cmd(stream, ENABLE);
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
void SYS_DMA_SetCallback(DMA_Stream_TypeDef *stream, SysDmaCallback_t cb)
{
    int gi = dma_index(stream);

    if (gi >= 0) dma_cb[gi] = cb;
}

void SYS_DMA_PeriphToMem(DMA_Stream_TypeDef *stream, uint32_t channel,
                         uint32_t periph_addr, void *mem,
                         uint16_t len, uint8_t item_size, uint8_t circular)
{
    dma_start(stream, channel, periph_addr, (uint32_t)mem,
              DMA_DIR_PeripheralToMemory, len, item_size, circular);
}

void SYS_DMA_MemToPeriph(DMA_Stream_TypeDef *stream, uint32_t channel,
                         uint32_t periph_addr, const void *mem,
                         uint16_t len, uint8_t item_size, uint8_t circular)
{
    dma_start(stream, channel, periph_addr, (uint32_t)mem,
              DMA_DIR_MemoryToPeripheral, len, item_size, circular);
}

void SYS_DMA_Stop(DMA_Stream_TypeDef *stream)
{
    DMA_Cmd(stream, DISABLE);
}

uint16_t SYS_DMA_Remain(DMA_Stream_TypeDef *stream)
{
    return DMA_GetCurrDataCounter(stream);
}

uint8_t SYS_DMA_Busy(DMA_Stream_TypeDef *stream)
{
    return (DMA_GetCmdStatus(stream) != DISABLE) ? 1U : 0U;
}


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
uint8_t SYS_DMA_WaitDone(DMA_Stream_TypeDef *stream, uint32_t loops)
{
    volatile uint32_t wait = (loops == 0U) ? SYS_DMA_WAIT_LOOPS : loops;

    while (wait != 0U) {
        if (SYS_DMA_Busy(stream) == 0U) return 1U;   /* 结束了 */
        wait--;
    }
    return 0U;                                       /* 超时（仍在搬） */
}


/* ================================================================
 *                中断服务函数：16 个数据流批量生成
 * ================================================================
 * 宏技巧说明（X-Macro）：
 *   清单宏 DMA_STREAM_LIST(X) 把 16 个数据流"喂"给 X；
 *   DMA_DEFINE_ISR 展开时，stream##_IRQHandler 把数据流名
 *   和 _IRQHandler 拼成函数名——一行代码生成一个 ISR。
 *   以后要加/减流，只动清单宏一行即可。 */
static void dma_irq(DMA_Stream_TypeDef *stream)
{
    int gi = dma_index(stream);

    if (gi < 0) return;

    dma_clear_flags(gi);                       /* 清标志（含 TC） */
    if (dma_cb[gi] != 0) dma_cb[gi]();         /* 通知上层 */
}

#define DMA_STREAM_LIST(X) \
    X(DMA1_Stream0) X(DMA1_Stream1) X(DMA1_Stream2) X(DMA1_Stream3) \
    X(DMA1_Stream4) X(DMA1_Stream5) X(DMA1_Stream6) X(DMA1_Stream7) \
    X(DMA2_Stream0) X(DMA2_Stream1) X(DMA2_Stream2) X(DMA2_Stream3) \
    X(DMA2_Stream4) X(DMA2_Stream5) X(DMA2_Stream6) X(DMA2_Stream7)

/* 每个 ISR 都是弱定义（__weak）:你手写同名 DMAx_Streamy_IRQHandler 时
 * 会自动顶替库版（顶替后该流的库回调随之停用;机制说明见 sys_tim.c 同段注释） */
#define DMA_DEFINE_ISR(stream) \
    __weak void stream##_IRQHandler(void) { dma_irq(stream); }

DMA_STREAM_LIST(DMA_DEFINE_ISR)

/* 用完即撤：这两个宏只在上面一处展开，避免污染后续代码/与外部同名 */
#undef DMA_DEFINE_ISR
#undef DMA_STREAM_LIST
