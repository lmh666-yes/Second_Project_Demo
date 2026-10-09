#include "sys_can.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_us 等）独立文件 */

/* ================================================================
 *  sys_can.c —— 【板载】CAN 总线（CAN1 + TJA1050）  实现文件
 * ================================================================
 *  一帧 CAN 在硬件上是怎么"发出去"的（一句话版）：
 *
 *      你把 ID + 8 字节塞进发送邮箱 → 控制器按"位时序"把 ID/控制/数据/CRC
 *      逐位推到 CAN_TX → TJA1050 把它变成 CAN_H/CAN_L 上的**差分电平**
 *      → 总线上**每个**节点都收到（靠 ID 自行决定要不要），
 *        并且**至少有一个节点回 ACK 位**才算发送成功
 *
 *  ⇒ 所以"发送超时/错误计数猛涨"的第一嫌疑永远是：
 *     ① P9 跳线没跳到 CAN（PA11/PA12 还连着 USB）
 *     ② 总线上只有自己一块板子（没第二个节点 → 没人 ACK）
 *     ③ 波特率和对面不一致
 * ================================================================ */


/* ================================================================
 *                      内部状态
 * ================================================================ */
static uint8_t  can_inited = 0U;
static uint32_t can_baud   = 0U;
static SysCanBaud_t can_baud_sel = SYS_CAN_500K;   /* 记住当前波特率，切模式时要用 */

/* 位时序参数表：{预分频, BS1, BS2} —— 按 APB1=42MHz、tq 数 14 设计
 *   位速率 = 42MHz / 预分频 / (1 + BS1 + BS2)
 *   采样点 = (1 + BS1) / (1 + BS1 + BS2) = 12/14 ≈ 85.7% */
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


/* ================================================================
 *                      内部小工具
 * ================================================================ */

/* 引脚复用为 CAN1（AF9）：TX 复用推挽（GPIO_OType_PP）、RX 复用带上拉（GPIO_PuPd_UP） */
static void can_gpio_init(void)
{
    GPIO_InitTypeDef gi;

    GPIO_ClockEnable(SYS_CAN_TX_PORT);
    GPIO_ClockEnable(SYS_CAN_RX_PORT);

    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;       /* 总线没接时别悬空乱收 */

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

/* 用指定模式重新初始化 CAN 控制器（Loopback / Silent 靠 CAN_Init 切，
 * 没有单独的"模式请求"API），成功返 0 */
static uint8_t can_apply_mode(uint8_t mode, SysCanBaud_t baud)
{
    CAN_InitTypeDef ci;

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

    SYS_CAN_FilterAcceptAll();

    if (CAN_OperatingModeRequest(SYS_CAN_INSTANCE,
                                CAN_OperatingMode_Normal) == CAN_ModeStatus_Failed) {
        return 1U;
    }
    return 0U;
}

/* 把一帧压进中断队列（满则丢最旧的，保证"最新的能进来"） */
static void can_push(const SysCanFrame_t *f)
{
    if (can_q_cnt >= SYS_CAN_RX_QUEUE_SIZE) {
        can_q_tail = (uint8_t)((can_q_tail + 1U) % SYS_CAN_RX_QUEUE_SIZE);
        can_q_cnt--;
    }
    can_q[can_q_head] = *f;
    can_q_head = (uint8_t)((can_q_head + 1U) % SYS_CAN_RX_QUEUE_SIZE);
    can_q_cnt++;
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
uint8_t SYS_CAN_Init(SysCanBaud_t baud)
{
#if (SYS_CAN_ENABLE == 0)
    (void)baud;
    return 1U;
#else
    CAN_InitTypeDef        ci;

    if (baud >= SYS_CAN_BAUD_COUNT) return 1U;

    /* ① 时钟 + 引脚（AF9） */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);
    can_gpio_init();

    /* ② 位时序（详见 can_timing 表注释） */
    CAN_StructInit(&ci);
    ci.CAN_Prescaler = can_timing[baud][0];
    ci.CAN_SJW       = CAN_SJW_1tq;
    ci.CAN_BS1       = (uint8_t)(can_timing[baud][1] - 1U);  /* 库里的 BS1 是"减 1"编码 */
    ci.CAN_BS2       = (uint8_t)(can_timing[baud][2] - 1U);
    ci.CAN_Mode      = CAN_Mode_Normal;
    ci.CAN_TTCM      = DISABLE;     /* 时间触发模式：不用 */
    ci.CAN_ABOM      = ENABLE;      /* ★ 自动退出总线关闭——总线插拔后能自愈 */
    ci.CAN_AWUM      = ENABLE;      /* 自动唤醒：总线上有活动就醒 */
    ci.CAN_NART      = DISABLE;     /* 自动重传（CAN 的可靠性核心，别关） */
    ci.CAN_RFLM      = DISABLE;     /* FIFO 不锁定：满了就覆盖，保证收最新 */
    ci.CAN_TXFP      = DISABLE;     /* 发送优先级按 ID 而不是按先后 */

    CAN_DeInit(SYS_CAN_INSTANCE);
    if (CAN_Init(SYS_CAN_INSTANCE, &ci) == CAN_InitStatus_Failed) return 1U;

    /* ③ 过滤器：默认全通 */
    SYS_CAN_FilterAcceptAll();

    /* ④ 确认已进正常模式（硬件初始化后要等它自己切过去） */
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

    /* 掩码全 0 = "任何 ID 都算命中"；32 位掩码模式，挂到 FIFO0 */
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

    /* CAN_Transmit 返回的邮箱号若等于 CAN_TxStatus_NoMailBox 就是"没空邮箱" */
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

    /* 等邮箱发完：等的是"这个邮箱的请求是否已完成"，而不是死循环里干等 */
    do {
        status = CAN_TransmitStatus(SYS_CAN_INSTANCE, mbox);
        if (status == CAN_TxStatus_Ok) return 0U;

        delay_us(50U);
        waited += 1U;                        /* 每轮约 50us */
    } while (waited < (SYS_CAN_TX_TIMEOUT_MS * 20U));

    /* 超时：把这一笔撤掉，别占着邮箱影响下一帧 */
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


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
uint8_t SYS_CAN_InitIT(SysCanRxCallback_t cb)
{
    NVIC_InitTypeDef ni;

    if (can_inited == 0U) return 1U;

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

uint8_t SYS_CAN_Dequeue(SysCanFrame_t *frame)
{
    if (frame == 0 || can_q_cnt == 0U) return 1U;

    *frame     = can_q[can_q_tail];
    can_q_tail = (uint8_t)((can_q_tail + 1U) % SYS_CAN_RX_QUEUE_SIZE);
    can_q_cnt--;

    return 0U;
}

uint8_t SYS_CAN_QueueCount(void)
{
    return can_q_cnt;
}

uint8_t SYS_CAN_LoopbackTest(void)
{
    SysCanFrame_t tx;
    SysCanFrame_t rx;
    uint32_t      waited = 0U;
    uint8_t       got;

    if (can_inited == 0U) return 1U;

    /* 切到"环回模式"：TX 不外发，内部直接回环到自己的 RX，
     * 所以不需要第二个节点、也不需要 120Ω 和跳线
     * ⚠ 标准库没有"模式请求"API，只能用 CAN_Init 的 CAN_Mode_* 参数切 */
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

    /* 恢复普通模式，回复到"可直接使用"的状态 */
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


/* ================================================================
 *           中断服务函数（库风格：ISR 只做搬运，业务走回调/队列）
 * ================================================================ */
/* 本 ISR 为弱定义（__weak）:你手写同名 CAN1_RX0_IRQHandler 会自动顶替库版
 * （靠工程链接器 --muldefweak）；顶替后本模块的回调/队列随之停用 */
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

/* 文件结束 */
