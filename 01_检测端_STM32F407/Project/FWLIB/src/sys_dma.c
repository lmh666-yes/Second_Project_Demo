#include "sys_dma.h"
/* 接口与用法注记见 sys_dma.h */

/* sys_dma.c — 【系统】DMA 通用搬运模块  实现文件
 * dma_cb[16] 回调表：下标 0~7 = DMA1_Stream0~7，8~15 = DMA2_Stream0~7
 * ISR：DMA_STREAM_LIST(X-Macro) 生成 DMA1/DMA2 各 Stream0~7 共 16 个，
 *      在本文件定义，应用代码不要重复定义。 */


/* 内部辅助 */
/* 回调表：16 个数据流通用一张表 */
static SysDmaCallback_t dma_cb[16];

/*  传输错误账本（TE / FE / DME）
 * 约束：TC/HT/TE/FE/DME 同占 LISR/HISR 中一个 6 位组，清除按组进行，
 * 故 ISR 必须先读标志（dma_read_flags）再清，否则 TE/FE/DME 会被当作
 * 传输完成处理，上层回调在数据未搬完时被调用。
 * TE 常见来源：外设被复位、总线/SDRAM 访问越界、流被其他代码抢占。 */
static volatile uint32_t s_dma_err_cnt     = 0U;   /* 累计出错次数 */
static volatile uint8_t  s_dma_last_kind   = 0U;   /* 1=TE 2=FE 3=DME */
static volatile uint8_t  s_dma_last_stream = 0U;   /* 最近出错的流号 0~15 */

/* 数据流指针 → 全局编号：0~7 = DMA1_Stream0~7，8~15 = DMA2_Stream0~7
 * 返回 -1：不是合法的数据流指针
 * 0x18：每个 Stream 寄存器块的字节间距（参考手册寄存器映射） */
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

/* 全局编号 0~15 → NVIC 中断号；DMA1_Stream7 为 47，与其他流不连续 */
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

/* 清某个数据流的全部中断标志（TC/HT/TE/FE/DME）
 * 每个流占 LISR/HISR 中连续 6 位：流 0~3 在 LISR，4~7 在 HISR，
 * 对 LIFCR/HIFCR 对应位写 1 即按组清除这 6 位。
 * 约束：清之前必须先读（见 dma_read_flags），否则错误标志被吞。 */
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

/* 读某个数据流的 6 位标志组（不清除），位序同参考手册：
 *   bit0 = FEIF   FIFO 错误（FIFO 模式下读/写指针越界）
 *   bit1 = DMEIF  直接模式错误（禁止 FIFO 时外设/内存宽度不一致）
 *   bit2 = TEIF   传输错误（总线读写失败）
 *   bit3 = HTIF   半传输
 *   bit4 = TCIF   传输完成
 *   bit5 = 保留
 * 返回低 6 位有效。 */
static uint32_t dma_read_flags(int gi)
{
    DMA_TypeDef *D;
    uint8_t      idx;
    uint32_t     v;

    if (gi < 8) {
        idx = (uint8_t)gi;
        D   = DMA1;
    } else {
        idx = (uint8_t)(gi - 8);
        D   = DMA2;
    }

    v = (idx < 4U) ? D->LISR : D->HISR;
    return (v >> ((idx & 3U) * 6U)) & 0x3FUL;
}

/* 两种方向共用的初始化主体
 * 外设地址不递增、内存地址递增，外设与内存数据宽度相同
 * item_size：1=字节，2=半字 */
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

    /* 开传输完成中断（回调在 DMAx_Streamy_IRQHandler 里转发）：
     * DMA 侧 TC 中断源与 NVIC 对应中断缺一不可，否则回调不执行。
     * 仅当该流已注册回调（dma_cb[gi] != 0）时使能，未注册回调的流
     * 不产生 TC 中断。 */
    dma_clear_flags(gi);
    DMA_ITConfig(stream, DMA_IT_TC, (dma_cb[gi] == 0) ? DISABLE : ENABLE);

    ni.NVIC_IRQChannel                   = dma_irqn(gi);
    ni.NVIC_IRQChannelPreemptionPriority = SYS_DMA_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_DMA_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = (dma_cb[gi] == 0) ? DISABLE : ENABLE;
    NVIC_Init(&ni);

    DMA_Cmd(stream, ENABLE);
}


/* 区块 2：基础功能 */
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


/* 区块 3：扩展功能 */
uint8_t SYS_DMA_WaitDone(DMA_Stream_TypeDef *stream, uint32_t loops)
{
    volatile uint32_t wait = (loops == 0U) ? SYS_DMA_WAIT_LOOPS : loops;

    while (wait != 0U) {
        if (SYS_DMA_Busy(stream) == 0U) return 1U;   /* 结束了 */
        wait--;
    }
    return 0U;                                       /* 超时（仍在搬） */
}

/* 传输错误查询（TE / FE / DME）
 * 顺序：先 SYS_DMA_WaitDone 判超时（可能流未启动），再 SYS_DMA_GetError
 * 判本次是出错还是完成；SYS_DMA_ErrCount 持续增长说明传输系统性失败
 * （外设时钟未开/地址越界）。
 * 返回 1 = TE 总线读写失败；2 = FE FIFO 错误（本库 FIFO 关闭）；
 *      3 = DME 外设与内存数据宽度不一致（item_size 与实际位宽不符）。 */
uint8_t SYS_DMA_GetError(DMA_Stream_TypeDef *stream)
{
    int      gi = dma_index(stream);
    uint32_t f;

    if (gi < 0) return 0U;

    f = dma_read_flags(gi);
    if ((f & 0x07UL) == 0U) return 0U;

    if ((f & 0x04UL) != 0U)      return 1U;   /* TE  */
    else if ((f & 0x01UL) != 0U) return 2U;   /* FE  */
    else                         return 3U;   /* DME */
}

uint32_t SYS_DMA_ErrCount(void)
{
    return s_dma_err_cnt;
}

uint8_t SYS_DMA_LastErrKind(void)
{
    return s_dma_last_kind;
}

uint8_t SYS_DMA_LastErrStream(void)
{
    return s_dma_last_stream;
}

void SYS_DMA_ErrClear(void)
{
    s_dma_err_cnt     = 0U;
    s_dma_last_kind   = 0U;
    s_dma_last_stream = 0U;
}


/* 中断服务函数：16 个数据流批量生成
 * X-Macro：DMA_STREAM_LIST(X) 列出 16 个数据流，
 * DMA_DEFINE_ISR 以 stream##_IRQHandler 拼接生成 16 个 ISR。 */
static void dma_irq(DMA_Stream_TypeDef *stream)
{
    int      gi = dma_index(stream);
    uint32_t f;

    if (gi < 0) return;

    f = dma_read_flags(gi);                    /*  先读，再清 */

    /* 低 3 位 = FE/DME/TE；任一置位即本次传输失败，只记账 + 清标志，
     * 不调上层完成回调（否则上层会认为数据已搬完）。 */
    if ((f & 0x07UL) != 0U) {
        s_dma_err_cnt++;
        s_dma_last_stream = (uint8_t)gi;
        if ((f & 0x04UL) != 0U)      s_dma_last_kind = 1U;   /* TE  */
        else if ((f & 0x01UL) != 0U) s_dma_last_kind = 2U;   /* FE  */
        else                         s_dma_last_kind = 3U;   /* DME */
        dma_clear_flags(gi);
        return;
    }

    dma_clear_flags(gi);                       /* 清标志（含 TC） */
    if (dma_cb[gi] != 0) dma_cb[gi]();         /* 通知上层 */
}

#define DMA_STREAM_LIST(X) \
    X(DMA1_Stream0) X(DMA1_Stream1) X(DMA1_Stream2) X(DMA1_Stream3) \
    X(DMA1_Stream4) X(DMA1_Stream5) X(DMA1_Stream6) X(DMA1_Stream7) \
    X(DMA2_Stream0) X(DMA2_Stream1) X(DMA2_Stream2) X(DMA2_Stream3) \
    X(DMA2_Stream4) X(DMA2_Stream5) X(DMA2_Stream6) X(DMA2_Stream7)

/* ISR 为弱定义（__weak）：应用自定义同名 DMAx_Streamy_IRQHandler 会顶替库版，
 * 顶替后该流的库回调停用；机制见 sys_tim.c 同段注释 */
#define DMA_DEFINE_ISR(stream) \
    __weak void stream##_IRQHandler(void) { dma_irq(stream); }

DMA_STREAM_LIST(DMA_DEFINE_ISR)

/* 两个宏仅在上方展开一次，之后撤销，避免与外部同名 */
#undef DMA_DEFINE_ISR
#undef DMA_STREAM_LIST
