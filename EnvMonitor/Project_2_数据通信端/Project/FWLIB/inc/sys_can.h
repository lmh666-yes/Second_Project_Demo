#ifndef __FWLIB_SYS_CAN_H
#define __FWLIB_SYS_CAN_H

#include "stm32f4xx.h"

/* sys_can.h : CAN1 总线驱动（控制器 STM32 内置 bxCAN + 板载 TJA1050/TJA1040 收发器）
 *
 * 帧格式:报文 ID + DLC(数据长度) + 最多 8 字节数据
 *
 * 本板接线(普中-天马 F407):
 *      CAN1_TX = PA12   CAN1_RX = PA11   (AF9)
 *      收发器   = U8(TJA1050/TJA1040),板载 120Ω 终端 + 30pF
 *      对外端子 = 黑色 2P 端子,丝印 B(=CAN_H) / A(=CAN_L)
 *      PA11/PA12 与 USB OTG_FS 的 D-/D+ 复用,由 P9 排针选择:
 *      跳上排↔中排用 CAN,跳中排↔下排用 USB OTG,两者不能同时使用
 */


/* 0 = 不编译本模块 */
#ifndef SYS_CAN_ENABLE
#define SYS_CAN_ENABLE      1
#endif

/* 使用的 CAN 控制器:本板只有 CAN1 */
#define SYS_CAN_INSTANCE    CAN1

#define SYS_CAN_TX_PORT     GPIOA
#define SYS_CAN_TX_PIN      GPIO_Pin_12     /* CAN1_TX = PA12 */
#define SYS_CAN_RX_PORT     GPIOA
#define SYS_CAN_RX_PIN      GPIO_Pin_11     /* CAN1_RX = PA11 */

/* CAN 挂在 APB1 上,用于算波特率;本板 SystemInit 后 APB1 = 42MHz
 * 改系统时钟后必须同步改本宏并重新 SYS_CAN_Init,否则波特率会跟着偏 */
#define SYS_CAN_APB1_HZ     42000000UL

/* 数值越小优先级越高;可用范围随 sys_nvic 的分组变化:
 *   NVIC_PriorityGroup_4(本库默认): 抢占 0~15、子固定 0
 *   NVIC_PriorityGroup_2(纯裸机)  : 抢占 0~3、子 0~3
 * 用 FreeRTOS 时必须 ≥5(默认已给 5):FreeRTOS 用 BASEPRI =
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY<<4(本工程 5<<4 = 0x50)屏蔽可调
 * FromISR 的中断;抢占值 < 5 的中断能打断内核临界区,其中禁止调用任何
 * ...FromISR() 接口,开 configASSERT 会断言失败。 */
#define SYS_CAN_IRQ_PRE_PRIO    5
#define SYS_CAN_IRQ_SUB_PRIO    0

/* 等邮箱变空的忙等上限(毫秒);无节点应答时 CAN 会自动重发,用上限避免死等 */
#define SYS_CAN_TX_TIMEOUT_MS   100

/* 接收 FIFO 缓冲上限(中断接收模式用的环形缓冲,单位:帧) */
#define SYS_CAN_RX_QUEUE_SIZE   8


/* 常用波特率(APB1 = 42MHz 时段的精确值,采样点约 85.7%) */
typedef enum {
    SYS_CAN_100K = 0,       /* 预分频 30 */
    SYS_CAN_125K = 1,       /* 预分频 24 */
    SYS_CAN_250K = 2,       /* 预分频 12 */
    SYS_CAN_500K = 3,       /* 预分频  6 */
    SYS_CAN_1M   = 4,       /* 预分频  3 */
    SYS_CAN_BAUD_COUNT
} SysCanBaud_t;

/* 一帧 CAN 报文(标准/扩展帧、数据帧/远程帧) */
typedef struct {
    uint32_t id;            /* 报文 ID:标准帧 0~0x7FF,扩展帧 0~0x1FFFFFFF */
    uint8_t  ext;           /* 1 = 扩展帧(29 位 ID);0 = 标准帧(11 位 ID) */
    uint8_t  rtr;           /* 1 = 远程帧(只请求数据,不带数据);0 = 数据帧 */
    uint8_t  dlc;           /* 数据长度 0~8(CAN 一帧最多 8 字节) */
    uint8_t  data[8];       /* 数据(dlc 之外的字节无意义) */
} SysCanFrame_t;

/* 初始化 CAN:引脚复用 + 波特率 + 位时序,并使能全通过滤器
 * 参数 : baud:见 SysCanBaud_t(本板推荐 SYS_CAN_500K)
 * 返回 : 0 = 成功;1 = 参数非法或进入正常模式失败
 * 说明 : 可重复调用以重配波特率;内部含自动总线关闭恢复(ABOM) */
uint8_t SYS_CAN_Init(SysCanBaud_t baud);

/* 当前实际波特率(按 SYS_CAN_APB1_HZ 重算) */
uint32_t SYS_CAN_GetBaudrate(void);

/* 过滤器:接收所有 ID;SYS_CAN_Init 内部已调用一次,可用来改过滤器后恢复 */
void SYS_CAN_FilterAcceptAll(void);

/* 过滤器:只接收指定标准帧 ID,其余丢弃
 * 参数 : id:标准帧 ID(0~0x7FF) */
void SYS_CAN_FilterById(uint16_t id);

/* 发送一帧,阻塞等待发送完成,最长 SYS_CAN_TX_TIMEOUT_MS
 * 返回 : 0 = 发送成功(已被总线应答);1 = 失败/超时(无 ACK / 被仲裁)
 * 说明 : 总线上至少要有另一个节点才会 ACK,只有一块板时必然超时 */
uint8_t SYS_CAN_Transmit(const SysCanFrame_t *frame);

/* 发送一帧,非阻塞,立刻返回不等结果
 * 返回 : 0 = 已进邮箱排队;1 = 失败(3 个邮箱都占着 / 参数错) */
uint8_t SYS_CAN_TryTransmit(const SysCanFrame_t *frame);

/* 轮询接收一帧,非阻塞
 * 返回 : 0 = 收到(frame 已被填写);1 = 当前没有报文 */
uint8_t SYS_CAN_Receive(SysCanFrame_t *frame);

/* 是否有报文待收,1 = FIFO 里有 */
uint8_t SYS_CAN_MessagePending(void);

/* 收发错误计数,用于排查总线上无人应答
 * 参数 : tec / rec:非 0 则带出发送/接收错误计数,传 0 忽略
 * 说明 : 发送错误计数持续上涨 = 没有节点 ACK(线没接 / 波特率不对 /
 *        P9 没跳到 CAN);稳定在 0~127 属正常 */
void SYS_CAN_GetErrors(uint8_t *tec, uint8_t *rec);


/* 收到报文时的回调,在中断里执行,只做短操作,不要 printf/延时 */
typedef void (*SysCanRxCallback_t)(const SysCanFrame_t *frame);

/* 开启中断接收:用 CAN1_RX0 中断,FIFO0 每收到一帧回调一次
 * 参数 : cb:回调;传 0 表示只入内部队列不回调
 * 返回 : 0 = 成功;1 = 失败
 * 说明 : 与 SYS_CAN_Receive 轮询二选一 */
uint8_t SYS_CAN_InitIT(SysCanRxCallback_t cb);

/* 关闭中断接收 */
void SYS_CAN_StopIT(void);

/* 从内部队列取一帧,中断接收模式下用,非阻塞
 * 返回 : 0 = 取到;1 = 队列空(或 frame 传了空指针)
 * 说明 : 只能在任务上下文调用:函数内部会短暂关中断以保护队列计数,
 *        在中断里调用会提前开中断,破坏外层的临界区 */
uint8_t SYS_CAN_Dequeue(SysCanFrame_t *frame);

/* 队列里积压的帧数,0 = 空 */
uint8_t SYS_CAN_QueueCount(void);

/* 因队列满而丢弃的帧数,累计值
 * 说明 : 队列满时丢弃最旧的一帧,让最新报文进来
 * 判读 : 一直不涨 = 取帧够快;持续增长 = 正在丢帧,需提高取帧任务的
 *        优先级/频率,或调大 SYS_CAN_RX_QUEUE_SIZE(每帧十几个字节) */
uint32_t SYS_CAN_DropCount(void);

/* 清零溢出计数,用于统计某段时间内的丢帧数 */
void SYS_CAN_ClearDropCount(void);

/* 环回自测(Loopback):自己发自己收,不需要外部节点和接线
 * 返回 : 0 = 自测通过(CAN 控制器和位时序正常);1 = 失败
 * 说明 : 测试完恢复到正常模式;它是控制器级环回,不经过 TJA1050 和端子,
 *        只能证明 MCU 侧正常,不能证明收发器/线缆正常 */
uint8_t SYS_CAN_LoopbackTest(void);

/* 静默模式,只收不发
 * 参数 : on:1 = 进静默;0 = 回正常 */
void SYS_CAN_SetSilentMode(uint8_t on);

/* 进 / 出睡眠(低功耗;总线上一有活动就自动唤醒,需配合唤醒中断)
 * 返回 : 0 = 成功;1 = 失败 */
uint8_t SYS_CAN_Sleep(void);
uint8_t SYS_CAN_WakeUp(void);

#endif /* __FWLIB_SYS_CAN_H */
