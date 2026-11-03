#include "sys_can.h"
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_us 等）独立文件 */

/* CAN 总线（CAN1 + TJA1050）实现文件
 * PA11/PA12 由 P9 跳线选择接 CAN 收发器还是 USB */


/* 内部状态 */
static uint8_t  can_inited = 0U;
static uint32_t can_baud   = 0U;
static SysCanBaud_t can_baud_sel = SYS_CAN_500K;   /* 当前波特率，切模式时使用 */

/* 位时序表：{预分频, BS1, BS2}，按 APB1=42MHz、tq 共 14 个设计
 * 位速率 = 42MHz / 预分频 / (1 + BS1 + BS2)
 * 采样点 = (1 + BS1) / (1 + BS1 + BS2) = 12/14 ≈ 85.7% */
static const uint8_t can_timing[SYS_CAN_BAUD_COUNT][3] = {
    { 30U, 11U, 2U },   /* 100k */
    { 24U, 11U, 2U },   /* 125k */
    { 12U, 11U, 2U },   /* 250k */
    {  6U, 11U, 2U },   /* 500k */
    {  3U, 11U, 2U }    /* 1M   */
};

/* 中断接收用的环形队列 */
static volatile SysCanFrame_t can_q[SYS_CAN_RX_QUEUE_SIZE];
static volatile uint8_t can_q_head = 0U;
static volatile uint8_t can_q_tail = 0U;
static volatile uint8_t can_q_cnt  = 0U;
static SysCanRxCallback_t can_cb  = 0;

/* 队列溢出计数：队列满时 can_push() 丢最旧的一帧并累加本计数。
 * 计数不涨表示取帧够快；持续增长表示取帧太慢或总线负载过高，
 * 帧在中断中丢失。读取见 SYS_CAN_DropCount()。
 * SYS_CAN_RX_QUEUE_SIZE 调大可减少丢弃，每帧 SysCanFrame_t 约十几字节。 */
static volatile uint32_t s_can_drop_cnt = 0U;

/* CAN_DeInit() 复位 CAN 外设全部寄存器，包括过滤器组（FMR/FiR1/FiR2）
 * 与中断使能（IER）。SYS_CAN_SetSilentMode() / SYS_CAN_LoopbackTest()
 * 走 CAN_DeInit + CAN_Init 切模式，切完必须按下面记录原样重装过滤器，
 * 否则过滤器失效、FMP0 被清零，FIFO0 收不到帧 */
static uint8_t  can_flt_is_id = 0U;     /* 0 = 全通过滤器；1 = 按 ID 过滤 */
static uint16_t can_flt_id    = 0U;     /* 按 ID 过滤时使用的 ID */

/* 前置声明：定义在文件后半部分，can_apply_mode() 要用（AC5 不允许隐式声明） */
void SYS_CAN_FilterAcceptAll(void);
void SYS_CAN_FilterById(uint16_t id);


/* 内部小工具 */

/* 引脚复用为 CAN1（AF9）：TX 复用推挽（GPIO_OType_PP）、RX 复用带上拉（GPIO_PuPd_UP） */
static void can_gpio_init(void)
{
    GPIO_InitTypeDef gi;

    GPIO_ClockEnable(SYS_CAN_TX_PORT);
    GPIO_ClockEnable(SYS_CAN_RX_PORT);

    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;       /* 总线未接时引脚不悬空 */

    gi.GPIO_Pin = SYS_CAN_TX_PIN;
    GPIO_PinAFConfig(SYS_CAN_TX_PORT, GPIO_PinSource(SYS_CAN_TX_PIN), GPIO_AF_CAN1);
    GPIO_Init(SYS_CAN_TX_PORT, &gi);

    gi.GPIO_Pin = SYS_CAN_RX_PIN;
    GPIO_PinAFConfig(SYS_CAN_RX_PORT, GPIO_PinSource(SYS_CAN_RX_PIN), GPIO_AF_CAN1);
    GPIO_Init(SYS_CAN_RX_PORT, &gi);
}

/* 标准库 CanTxMsg → 本库 SysCanFrame_t */
static void can_to_txmsg(const SysCanFrame_t *f, CanTxMsg *tx)
{
    uint8_t i;

    if (f->ext) {
        tx->IDE = CAN_Id_Extended;
    } else {
        tx->IDE = CAN_Id_Standard;
    }
    tx->RTR = (f->rtr != 0U) ? CAN_RTR_Remote : CAN_RTR_Data;
    tx->StdId = (uint16_t)(f->id & 0x7FFU);
    tx->ExtId = f->id & 0x1FFFFFFFU;

    if (f->dlc > 8U) tx->DLC = 8U;
    else             tx->DLC = f->dlc;

    for (i = 0U; i < 8U; i++) {
        tx->Data[i] = (i < tx->DLC) ? f->data[i] : 0U;
    }
}

/* 标准库 CanRxMsg → 本库 SysCanFrame_t */
static void can_from_rxmsg(SysCanFrame_t *f, const CanRxMsg *rx)
{
    uint8_t i;

    if (rx->IDE == CAN_Id_Extended) {
        f->ext = 1U;
        f->id  = rx->ExtId;
    } else {
        f->ext = 0U;
        f->id  = rx->StdId;
    }
    f->rtr = (rx->RTR == CAN_RTR_Remote) ? 1U : 0U;
    f->dlc = (rx->DLC > 8U) ? 8U : rx->DLC;

    for (i = 0U; i < 8U; i++) {
        f->data[i] = (i < f->dlc) ? rx->Data[i] : 0U;
    }
}

/* 按记录的配置重装过滤器（CAN_DeInit 之后必须调用） */
static void can_reapply_filter(void)
{
    if (can_flt_is_id != 0U) {
        SYS_CAN_FilterById(can_flt_id);
    } else {
        SYS_CAN_FilterAcceptAll();
    }
}

/* 用指定模式重新初始化 CAN 控制器（标准库无模式请求 API，
 * Loopback / Silent 只能靠 CAN_Init 的参数切），成功返 0
 * CAN_DeInit() 复位全部寄存器，因此切完模式必须：
 *   1) 按原样重装过滤器（FMR / FiR1 / FiR2 已复位）
 *   2) 之前启用过中断接收则重新使能 FMP0，否则 CAN1_RX0 中断失效 */
static uint8_t can_apply_mode(uint8_t mode, SysCanBaud_t baud)
{
    CAN_InitTypeDef ci;
    uint8_t it_was_on = (can_cb != 0) ? 1U : 0U;

    CAN_StructInit(&ci);
    ci.CAN_Prescaler = can_timing[baud][0];
    ci.CAN_SJW       = CAN_SJW_1tq;
    ci.CAN_BS1       = (uint8_t)(can_timing[baud][1] - 1U);
    ci.CAN_BS2       = (uint8_t)(can_timing[baud][2] - 1U);
    ci.CAN_Mode      = mode;
    ci.CAN_TTCM      = DISABLE;
    ci.CAN_ABOM      = ENABLE;
    ci.CAN_AWUM      = ENABLE;
    ci.CAN_NART      = DISABLE;
    ci.CAN_RFLM      = DISABLE;
    ci.CAN_TXFP      = DISABLE;

    CAN_DeInit(SYS_CAN_INSTANCE);
    if (CAN_Init(SYS_CAN_INSTANCE, &ci) == CAN_InitStatus_Failed) return 1U;

    can_reapply_filter();                    /* 1) 恢复原过滤器配置 */

    if (it_was_on != 0U) {
        CAN_ITConfig(SYS_CAN_INSTANCE, CAN_IT_FMP0, ENABLE);   /* 2) 恢复中断接收 */
    }

    if (CAN_OperatingModeRequest(SYS_CAN_INSTANCE,
                                CAN_OperatingMode_Normal) == CAN_ModeStatus_Failed) {
        return 1U;
    }
    return 0U;
}

/* 把一帧压进中断队列，满则丢最旧的一帧
 * 丢弃会累加 s_can_drop_cnt，用 SYS_CAN_DropCount() 查
 * 本函数在中断上下文运行，不能调用阻塞接口 */
static void can_push(const SysCanFrame_t *f)
{
    if (can_q_cnt >= SYS_CAN_RX_QUEUE_SIZE) {
        can_q_tail = (uint8_t)((can_q_tail + 1U) % SYS_CAN_RX_QUEUE_SIZE);
        can_q_cnt--;
        s_can_drop_cnt++;               /* 丢弃计数 */
    }
    can_q[can_q_head] = *f;
    can_q_head = (uint8_t)((can_q_head + 1U) % SYS_CAN_RX_QUEUE_SIZE);
    can_q_cnt++;
}


/* 基础功能 */
uint8_t SYS_CAN_Init(SysCanBaud_t baud)
{
#if (SYS_CAN_ENABLE == 0)
    (void)baud;
    return 1U;
#else
    CAN_InitTypeDef        ci;

    if (baud >= SYS_CAN_BAUD_COUNT) return 1U;

    /* 1) 时钟 + 引脚（AF9） */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);
    can_gpio_init();

    /* 2) 位时序（见 can_timing 表） */
    CAN_StructInit(&ci);
    ci.CAN_Prescaler = can_timing[baud][0];
    ci.CAN_SJW       = CAN_SJW_1tq;
    ci.CAN_BS1       = (uint8_t)(can_timing[baud][1] - 1U);  /* 库里的 BS1 是"减 1"编码 */
    ci.CAN_BS2       = (uint8_t)(can_timing[baud][2] - 1U);
    ci.CAN_Mode      = CAN_Mode_Normal;
    ci.CAN_TTCM      = DISABLE;     /* 时间触发模式：不用 */
    ci.CAN_ABOM      = ENABLE;      /* 自动退出总线关闭：总线恢复后自动重新上线 */
    ci.CAN_AWUM      = ENABLE;      /* 自动唤醒：总线上有活动就醒 */
    ci.CAN_NART      = DISABLE;     /* 自动重传：发送失败自动重发 */
    ci.CAN_RFLM      = DISABLE;     /* FIFO 不锁定：满了就覆盖，保证收最新 */
    ci.CAN_TXFP      = DISABLE;     /* 发送优先级按 ID，而非入队先后 */

    CAN_DeInit(SYS_CAN_INSTANCE);
    if (CAN_Init(SYS_CAN_INSTANCE, &ci) == CAN_InitStatus_Failed) return 1U;

    /* 3) 过滤器：默认全通 */
    SYS_CAN_FilterAcceptAll();

    /* 4) 确认已进入正常模式 */
    if (CAN_OperatingModeRequest(SYS_CAN_INSTANCE,
                                 CAN_OperatingMode_Normal) == CAN_ModeStatus_Failed) {
        return 1U;
    }

    can_inited   = 1U;
    can_baud     = SYS_CAN_APB1_HZ / can_timing[baud][0]
                   / (uint32_t)(1U + can_timing[baud][1] + can_timing[baud][2]);
    can_baud_sel = baud;

    return 0U;
#endif
}

uint32_t SYS_CAN_GetBaudrate(void)
{
    return can_baud;
}

void SYS_CAN_FilterAcceptAll(void)
{
    CAN_FilterInitTypeDef fi;

    can_flt_is_id = 0U;             /* 记录配置，切模式后按原样恢复 */
    can_flt_id    = 0U;

    /* 掩码全 0：任意 ID 都命中；32 位掩码模式，挂 FIFO0 */
    fi.CAN_FilterNumber         = 0U;
    fi.CAN_FilterMode           = CAN_FilterMode_IdMask;
    fi.CAN_FilterScale          = CAN_FilterScale_32bit;
    fi.CAN_FilterIdHigh         = 0x0000U;
    fi.CAN_FilterIdLow          = 0x0000U;
    fi.CAN_FilterMaskIdHigh     = 0x0000U;
    fi.CAN_FilterMaskIdLow      = 0x0000U;
    fi.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
    fi.CAN_FilterActivation     = ENABLE;

    CAN_FilterInit(&fi);
}

void SYS_CAN_FilterById(uint16_t id)
{
    CAN_FilterInitTypeDef fi;

    id &= 0x7FFU;                   /* 标准帧 ID 只 11 位 */

    can_flt_is_id = 1U;             /* 记录配置，切模式后按原样恢复 */
    can_flt_id    = id;

    /* 标准帧在 32 位过滤器里要左移 5 位（对齐到 ID[10:0] 位置） */
    fi.CAN_FilterNumber         = 0U;
    fi.CAN_FilterMode           = CAN_FilterMode_IdMask;
    fi.CAN_FilterScale          = CAN_FilterScale_32bit;
    fi.CAN_FilterIdHigh         = (uint16_t)(id << 5);
    fi.CAN_FilterIdLow          = 0x0000U;
    fi.CAN_FilterMaskIdHigh     = (uint16_t)(0x7FFU << 5);   /* 只比 ID 这 11 位 */
    fi.CAN_FilterMaskIdLow      = 0x0000U;
    fi.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
    fi.CAN_FilterActivation     = ENABLE;

    CAN_FilterInit(&fi);
}

uint8_t SYS_CAN_TryTransmit(const SysCanFrame_t *frame)
{
    CanTxMsg tx;
    uint8_t  mbox;

    if (frame == 0 || can_inited == 0U) return 1U;

    can_to_txmsg(frame, &tx);
    mbox = CAN_Transmit(SYS_CAN_INSTANCE, &tx);

    /* 返回 CAN_TxStatus_NoMailBox 表示没有空闲邮箱 */
    return (mbox == CAN_TxStatus_NoMailBox) ? 1U : 0U;
}

uint8_t SYS_CAN_Transmit(const SysCanFrame_t *frame)
{
    CanTxMsg tx;
    uint8_t  mbox;
    uint8_t  status;
    uint32_t waited = 0U;

    if (frame == 0 || can_inited == 0U) return 1U;

    can_to_txmsg(frame, &tx);
    mbox = CAN_Transmit(SYS_CAN_INSTANCE, &tx);
    if (mbox == CAN_TxStatus_NoMailBox) return 1U;

    /* 轮询本邮箱的发送完成状态 */
    do {
        status = CAN_TransmitStatus(SYS_CAN_INSTANCE, mbox);
        if (status == CAN_TxStatus_Ok) return 0U;

        delay_us(50U);
        waited += 1U;                        /* 每轮约 50us */
    } while (waited < (SYS_CAN_TX_TIMEOUT_MS * 20U));

    /* 超时：撤销本次发送，释放邮箱 */
    CAN_CancelTransmit(SYS_CAN_INSTANCE, mbox);
    return 1U;
}

uint8_t SYS_CAN_MessagePending(void)
{
    return (CAN_MessagePending(SYS_CAN_INSTANCE, CAN_FIFO0) != 0U) ? 1U : 0U;
}

uint8_t SYS_CAN_Receive(SysCanFrame_t *frame)
{
    CanRxMsg rx;

    if (frame == 0 || can_inited == 0U) return 1U;

    if (CAN_MessagePending(SYS_CAN_INSTANCE, CAN_FIFO0) == 0U) return 1U;

    CAN_Receive(SYS_CAN_INSTANCE, CAN_FIFO0, &rx);
    CAN_FIFORelease(SYS_CAN_INSTANCE, CAN_FIFO0);

    can_from_rxmsg(frame, &rx);
    return 0U;
}

void SYS_CAN_GetErrors(uint8_t *tec, uint8_t *rec)
{
    if (tec != 0) *tec = CAN_GetLSBTransmitErrorCounter(SYS_CAN_INSTANCE);
    if (rec != 0) *rec = CAN_GetReceiveErrorCounter(SYS_CAN_INSTANCE);
}


/* 扩展功能 */
uint8_t SYS_CAN_InitIT(SysCanRxCallback_t cb)
{
    NVIC_InitTypeDef ni;

    if (can_inited == 0U) return 1U;

    /* 先关 FIFO0 中断再清队列：中断开着清 can_q_head/tail/cnt 时
     * 若来一帧，can_push() 会改到一半的队列上，头尾指针与计数错乱 */
    CAN_ITConfig(SYS_CAN_INSTANCE, CAN_IT_FMP0, DISABLE);

    can_q_head = 0U;
    can_q_tail = 0U;
    can_q_cnt  = 0U;
    can_cb     = cb;

    /* FIFO0 非空（FMP0）中断 */
    CAN_ITConfig(SYS_CAN_INSTANCE, CAN_IT_FMP0, ENABLE);

    ni.NVIC_IRQChannel                   = CAN1_RX0_IRQn;
    ni.NVIC_IRQChannelPreemptionPriority = SYS_CAN_IRQ_PRE_PRIO;
    ni.NVIC_IRQChannelSubPriority        = SYS_CAN_IRQ_SUB_PRIO;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);

    return 0U;
}

void SYS_CAN_StopIT(void)
{
    CAN_ITConfig(SYS_CAN_INSTANCE, CAN_IT_FMP0, DISABLE);
    can_cb = 0;
}

/* 从队列取一帧，任务上下文调用；0 = 取到，1 = 队列空或 frame 为空
 * 与中断中的 can_push() 共用 can_q_tail / can_q_cnt，交叉读改写会丢更新
 * （计数少减会取到陈旧帧，多减下溢后每帧都被当满丢弃），
 * 因此这段必须关中断；不能在中断上下文调用 */
uint8_t SYS_CAN_Dequeue(SysCanFrame_t *frame)
{
    uint8_t ok = 1U;

    if (frame == 0) return 1U;

    __disable_irq();
    if (can_q_cnt != 0U) {
        *frame     = can_q[can_q_tail];
        can_q_tail = (uint8_t)((can_q_tail + 1U) % SYS_CAN_RX_QUEUE_SIZE);
        can_q_cnt--;
        ok = 0U;
    }
    __enable_irq();

    return ok;
}

uint8_t SYS_CAN_QueueCount(void)
{
    return can_q_cnt;
}

/* 取队列溢出计数：从上次清零以来因队列满被丢弃的最旧帧数
 * 一直为 0 表示取帧够快；持续增长表示取帧太慢或总线负载过高 */
uint32_t SYS_CAN_DropCount(void)
{
    return s_can_drop_cnt;
}

/* 清零溢出计数，用于统计某段时间内的丢帧数 */
void SYS_CAN_ClearDropCount(void)
{
    s_can_drop_cnt = 0U;
}

uint8_t SYS_CAN_LoopbackTest(void)
{
    SysCanFrame_t tx;
    SysCanFrame_t rx;
    uint32_t      waited = 0U;
    uint8_t       got;

    if (can_inited == 0U) return 1U;

    /* 切到环回模式：TX 不外发，内部回环到自身 RX，不需要第二个节点、
     * 120Ω 终端电阻和跳线。标准库无模式请求 API，只能靠 CAN_Init
     * 的 CAN_Mode_* 参数切 */
    if (can_apply_mode(CAN_Mode_LoopBack, can_baud_sel) != 0U) return 1U;

    tx.id = 0x123U;
    tx.ext = 0U;
    tx.rtr = 0U;
    tx.dlc = 4U;
    tx.data[0] = 0xAAU;
    tx.data[1] = 0x55U;
    tx.data[2] = 0x12U;
    tx.data[3] = 0x34U;

    if (SYS_CAN_TryTransmit(&tx) != 0U) {
        (void)can_apply_mode(CAN_Mode_Normal, can_baud_sel);
        return 1U;
    }

    got = 1U;
    do {
        if (SYS_CAN_Receive(&rx) == 0U) {
            got = ((rx.id == 0x123U) && (rx.dlc == 4U) &&
                   (rx.data[0] == 0xAAU) && (rx.data[1] == 0x55U)) ? 0U : 1U;
            break;
        }
        delay_us(50U);
        waited += 1U;
    } while (waited < 2000U);            /* 上限约 100ms */

    /* 恢复普通模式 */
    (void)can_apply_mode(CAN_Mode_Normal, can_baud_sel);

    return got;
}

void SYS_CAN_SetSilentMode(uint8_t on)
{
    if (can_inited == 0U) return;

    (void)can_apply_mode((on != 0U) ? CAN_Mode_Silent : CAN_Mode_Normal,
                         can_baud_sel);
}

uint8_t SYS_CAN_Sleep(void)
{
    return (CAN_Sleep(SYS_CAN_INSTANCE) == CAN_Sleep_Ok) ? 0U : 1U;
}

uint8_t SYS_CAN_WakeUp(void)
{
    return (CAN_WakeUp(SYS_CAN_INSTANCE) == CAN_WakeUp_Ok) ? 0U : 1U;
}


/* 中断服务函数：ISR 只搬帧，业务走回调或队列 */
/* 本 ISR 为弱定义（__weak）：工程内定义同名 CAN1_RX0_IRQHandler 会顶替本实现
 * （靠链接器 --muldefweak），顶替后本模块的回调与队列停用 */
__weak void CAN1_RX0_IRQHandler(void)
{
    CanRxMsg       rx;
    SysCanFrame_t  f;

    if (CAN_GetITStatus(SYS_CAN_INSTANCE, CAN_IT_FMP0) == RESET) return;

    /* FIFO0 里可能积了不止一帧，一次中断全搬空（FIFO 深度 3） */
    while (CAN_MessagePending(SYS_CAN_INSTANCE, CAN_FIFO0) != 0U) {
        CAN_Receive(SYS_CAN_INSTANCE, CAN_FIFO0, &rx);
        CAN_FIFORelease(SYS_CAN_INSTANCE, CAN_FIFO0);

        can_from_rxmsg(&f, &rx);
        can_push(&f);

        if (can_cb != 0) can_cb(&f);
    }

    CAN_ClearITPendingBit(SYS_CAN_INSTANCE, CAN_IT_FMP0);
}
