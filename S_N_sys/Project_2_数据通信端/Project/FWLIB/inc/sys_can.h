#ifndef __FWLIB_SYS_CAN_H
#define __FWLIB_SYS_CAN_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_can.h —— 【板载】CAN 总线（CAN1 + TJA1050 收发器）  头文件
 * ================================================================
 *  设计定位 : 标准外设库 CAN 的"薄封装"——初始化 + 收发 + 过滤器
 *             —— 控制器：STM32 内置 bxCAN（CAN1）
 *             —— 收发器：板载 TJA1050/TJA1040（5V 供电，带 120Ω 终端）
 *  标准库关键词 : RCC_APB1PeriphClockCmd(CAN1) / CAN_Init / CAN_FilterInit /
 *                 CAN_Transmit / CAN_TransmitStatus / CAN_Receive /
 *                 CAN_MessagePending / CAN_OperatingModeRequest
 *
 *  【CAN 是什么（面试点）】
 *    不是"点对点串口"，而是一条**总线**：所有节点并联在 CAN_H / CAN_L 两根
 *    线上（本板用黑色 2P 端子引出），靠**报文 ID**区分内容而不是靠地址，
 *    所以"多机通信不用改代码"。它的抗干扰和错误检测（CRC + 重发 + 错误计数）
 *    是汽车电子的基础，核心概念就三个：**ID + DLC（数据长度）+ 8 字节数据**。
 *
 *  【本板接线（普中-天马 F407 开发板 · 原理图逐脚核对）】
 *      CAN1_TX = PA12   CAN1_RX = PA11   （AF9）
 *      收发器   = U8（TJA1050/TJA1040），板载 120Ω 终端 + 30pF
 *      对外端子 = 板上黑色 2P 端子，丝印 `B`(=CAN_H) / `A`(=CAN_L)
 *
 *  ⚠⚠ **必须先插跳线，否则一根线都发不出去**（这是本板最坑的地方）:
 *      PA11/PA12 同时是 **USB OTG_FS 的 D-/D+**，板上用 `P9` 排针最上面
 *      三行做选择（丝印：左列 `CRX / PA11 / D-`，右列 `CTX / PA12 / D+`）：
 *          · 跳 "上排↔中排"  → 用 **CAN**        （CRX↔PA11、CTX↔PA12）
 *          · 跳 "中排↔下排"  → 用 **USB OTG**    （PA11↔D-、PA12↔D+）
 *      换句话说：**CAN 和 USB 从机不能同时用**。
 *
 *  使用方式 :
 *      SysCanFrame_t f;
 *      SYS_CAN_Init(SYS_CAN_500K);          // ① 初始化（默认收全部 ID）
 *      f.id = 0x123; f.ext = 0; f.dlc = 2; f.data[0] = 0x11; f.data[1] = 0x22;
 *      SYS_CAN_Transmit(&f);                // ② 发送
 *      if (SYS_CAN_Receive(&f) == 0) {      // ③ 轮询接收
 *          printf("id=%X dlc=%d\r\n", f.id, f.dlc);
 *      }
 *
 *  移植指引 : 换引脚改"区块 1"的宏；换波特率用 SYS_CAN_Init 的枚举；
 *             换收发器/终端电阻是硬件活，固件不用改。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 0 = 不编译本模块（RTE 里没勾 CAN 组件时也自然为空，省 Flash） */
#ifndef SYS_CAN_ENABLE
#define SYS_CAN_ENABLE      1
#endif

/* 使用哪个 CAN 控制器：本板只有 CAN1（CAN2 需要额外勾组件且引脚冲突） */
#define SYS_CAN_INSTANCE    CAN1

/* -------------------- 引脚 -------------------- */
#define SYS_CAN_TX_PORT     GPIOA
#define SYS_CAN_TX_PIN      GPIO_Pin_12     /* CAN1_TX = PA12 */
#define SYS_CAN_RX_PORT     GPIOA
#define SYS_CAN_RX_PIN      GPIO_Pin_11     /* CAN1_RX = PA11 */

/* -------------------- APB1 时钟（用于算波特率） -------------------- */
/* CAN 挂在 APB1 上；本板 SystemInit 后 APB1 = 42MHz。
 * ⚠ 用 sys_clock 改了系统时钟后，必须同步改这里并重新 SYS_CAN_Init，
 *   否则实际波特率会跟着偏，总线上别人收不到 */
#define SYS_CAN_APB1_HZ     42000000UL

/* -------------------- 中断优先级 -------------------- */
#define SYS_CAN_IRQ_PRE_PRIO    2
#define SYS_CAN_IRQ_SUB_PRIO    0

/* -------------------- 发送超时 -------------------- */
/* 等邮箱变空的忙等上限（毫秒）。没有任何节点应答时 CAN 会自动重发，
 * 这里给个上限，避免死等把程序卡住 */
#define SYS_CAN_TX_TIMEOUT_MS   100

/* 接收 FIFO 缓冲上限（中断接收模式用的环形缓冲，单位：帧） */
#define SYS_CAN_RX_QUEUE_SIZE   8


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 常用波特率（本板 APB1 = 42MHz 时段的精确值，采样点约 85.7%） */
typedef enum {
    SYS_CAN_100K = 0,       /* 预分频 30  —— 调试/教学常用 */
    SYS_CAN_125K = 1,       /* 预分频 24 */
    SYS_CAN_250K = 2,       /* 预分频 12 */
    SYS_CAN_500K = 3,       /* 预分频  6  —— 工业/汽车最常用 */
    SYS_CAN_1M   = 4,       /* 预分频  3  —— 短距离高速 */
    SYS_CAN_BAUD_COUNT
} SysCanBaud_t;

/* 一帧 CAN 报文（标准/扩展帧、数据帧/远程帧都能表示） */
typedef struct {
    uint32_t id;            /* 报文 ID：标准帧 0~0x7FF，扩展帧 0~0x1FFFFFFF */
    uint8_t  ext;           /* 1 = 扩展帧（29 位 ID）；0 = 标准帧（11 位 ID） */
    uint8_t  rtr;           /* 1 = 远程帧（只请求数据，不带数据）；0 = 数据帧 */
    uint8_t  dlc;           /* 数据长度 0~8（CAN 一帧最多 8 字节） */
    uint8_t  data[8];       /* 数据（dlc 之外的字节无意义） */
} SysCanFrame_t;

/* 初始化 CAN（含引脚复用 / 波特率 / 位时序；默认"收全部 ID"）
 * 参数 : baud —— 见 SysCanBaud_t（本板推荐 SYS_CAN_500K）
 * 返回 : 0 = 成功；1 = 参数非法或进入正常模式失败
 * 标准库 : RCC_APB1PeriphClockCmd(CAN1) → GPIO_PinAFConfig(AF9) +
 *          GPIO_Init → CAN_DeInit → CAN_StructInit → CAN_Init →
 *          CAN_FilterInit（全通过滤器）→ CAN_OperatingModeRequest
 * 说明 : 可重复调用（重新配置波特率）；内部含自动总线关闭恢复(ABOM)
 * 示例 : if (SYS_CAN_Init(SYS_CAN_500K) != 0) printf("CAN init fail\r\n"); */
uint8_t SYS_CAN_Init(SysCanBaud_t baud);

/* 当前实际波特率（按 SYS_CAN_APB1_HZ 重算，用于自查）
 * 示例 : printf("can baud = %u\r\n", SYS_CAN_GetBaudrate()); */
uint32_t SYS_CAN_GetBaudrate(void);

/* 过滤器：接收**所有** ID（最省事的做法，调试阶段就用它）
 * 说明 : SYS_CAN_Init 内部已调用一次，单独写它是为了"改过过滤器后恢复" */
void SYS_CAN_FilterAcceptAll(void);

/* 过滤器：只接收指定标准帧 ID（其余全丢）
 * 参数 : id —— 标准帧 ID（0~0x7FF）
 * 示例 : SYS_CAN_FilterById(0x123);   // 只收 0x123 */
void SYS_CAN_FilterById(uint16_t id);

/* 发送一帧（阻塞等待发送完成，最长 SYS_CAN_TX_TIMEOUT_MS）
 * 返回 : 0 = 发送成功（已被总线应答）；1 = 失败/超时（无 ACK / 被仲裁）
 * 说明 : 总线上**至少要有另一个节点**才会 ACK；只有一个板子时必然超时，
 *        这是正常的（CAN 需要至少两个节点才成网）
 * 示例 : SYS_CAN_Transmit(&frame); */
uint8_t SYS_CAN_Transmit(const SysCanFrame_t *frame);

/* 发送一帧（非阻塞）：立刻返回，不等结果
 * 返回 : 0 = 已进邮箱排队；1 = 失败（3 个邮箱都占着 / 参数错）
 * 示例 : uint8_t ok = SYS_CAN_TryTransmit(&f);   // ok == 0 表示已进邮箱排队 */
uint8_t SYS_CAN_TryTransmit(const SysCanFrame_t *frame);

/* 轮询接收一帧（非阻塞）
 * 返回 : 0 = 收到（frame 已被填写）；1 = 当前没有报文
 * 标准库 : CAN_MessagePending + CAN_Receive + CAN_FIFORelease
 * 示例 : if (SYS_CAN_Receive(&f) == 0) { ... } */
uint8_t SYS_CAN_Receive(SysCanFrame_t *frame);

/* 是否有报文待收（1 = FIFO 里有）
 * 示例 : if (SYS_CAN_MessagePending()) { ... } */
uint8_t SYS_CAN_MessagePending(void);

/* 收发错误计数（排查"总线上没人应答"最快的手段）
 * 参数 : tec / rec —— 传非 0 则把发送/接收错误计数带出，可传 0 忽略
 * 说明 : 发送错误计数持续上涨 = 没有节点 ACK（线没接 / 波特率不对 /
 *        跳线 P9 没跳到 CAN）；稳定在 0~127 属正常 */
void SYS_CAN_GetErrors(uint8_t *tec, uint8_t *rec);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* ---- 中断接收（报文自动进队列，主循环随时来取） ---- */
/* 收到报文时回调（在中断里执行，**必须短**：别在里面 printf/延时） */
typedef void (*SysCanRxCallback_t)(const SysCanFrame_t *frame);

/* 开启"中断接收"（用 CAN1_RX0 中断，FIFO0 里每来一帧就回调一次）
 * 参数 : cb —— 回调；传 0 表示只入内部队列不回调
 * 返回 : 0 = 成功；1 = 失败
 * 说明 : 与 SYS_CAN_Receive 轮询二选一；中断里做的只是"搬一帧出来"
 * 示例 : SYS_CAN_InitIT(my_can_rx_cb); */
uint8_t SYS_CAN_InitIT(SysCanRxCallback_t cb);

/* 关掉中断接收 */
void SYS_CAN_StopIT(void);

/* 从内部队列取一帧（中断接收模式下用；非阻塞）
 * 返回 : 0 = 取到；1 = 队列空
 * 示例 : while (SYS_CAN_Dequeue(&f) == 0) { handle(f); } */
uint8_t SYS_CAN_Dequeue(SysCanFrame_t *frame);

/* 队列里积压了多少帧（0 = 空）
 * 示例 : printf("pending=%d\r\n", SYS_CAN_QueueCount()); */
uint8_t SYS_CAN_QueueCount(void);

/* ---- 自测与低功耗 ---- */

/* 环回自测（Loopback）：不需要任何外部节点/接线，自己发自己收
 * 返回 : 0 = 自测通过（CAN 控制器和位时序正常）；1 = 失败
 * 用途 : ① 刚焊好板子怀疑 CAN 不工作；② 验证波特率配置
 * 说明 : 测试完会**恢复到正常模式**，可直接接着用
 *        注意它是"控制器级环回"，**不经过 TJA1050 和端子**，
 *        所以通过它只能证明 MCU 侧正常，不能证明收发器/线缆正常
 * 示例 : if (SYS_CAN_LoopbackTest() == 0) printf("CAN controller OK\r\n"); */
uint8_t SYS_CAN_LoopbackTest(void);

/* 静默模式（只收不发）——接到总线上"只听不打扰"时用
 * 参数 : on —— 1 = 进静默；0 = 回正常 */
void SYS_CAN_SetSilentMode(uint8_t on);

/* 进 / 出睡眠（低功耗；总线上一有活动就会被自动唤醒，需配合唤醒中断）
 * 返回 : 0 = 成功；1 = 失败 */
uint8_t SYS_CAN_Sleep(void);
uint8_t SYS_CAN_WakeUp(void);

#endif /* __FWLIB_SYS_CAN_H */
