#ifndef __FWLIB_SYS_USART_H
#define __FWLIB_SYS_USART_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_usart.h — 【系统】串口(USART)模块  头文件
 * ================================================================
 *  功能 : 标准外设库 USART 薄封装,覆盖本板 USART1 / USART2 / USART3。
 *  接线（依据 GEC-M4 原理图,换板只改区块 1 引脚宏）:
 *      USART1 : TX=PA9  RX=PA10 — 接 CH340G（下载/串口调试），跳线选通
 *      USART2 : TX=PA2  RX=PA3  — 接 RS485（SP3485），跳线选通
 *      USART3 : TX=PB10 RX=PB11 — 接 ESP8266（WiFi 模块），跳线选通
 *  约束 : PA2/PA3 与以太网 ETH_MDIO 等信号复用,两者只能取其一。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* -------------------- 串口引脚 -------------------- */
/* 复用模式(GPIO_Mode_AF):TX 复用推挽输出(GPIO_OType_PP),RX 复用输入,均开内部上拉(GPIO_PuPd_UP)防悬空。 */
#define SYS_USART1_TX_PORT  GPIOA
#define SYS_USART1_TX_PIN   GPIO_Pin_9
#define SYS_USART1_RX_PORT  GPIOA
#define SYS_USART1_RX_PIN   GPIO_Pin_10

#define SYS_USART2_TX_PORT  GPIOA
#define SYS_USART2_TX_PIN   GPIO_Pin_2
#define SYS_USART2_RX_PORT  GPIOA
#define SYS_USART2_RX_PIN   GPIO_Pin_3

#define SYS_USART3_TX_PORT  GPIOB
#define SYS_USART3_TX_PIN   GPIO_Pin_10
#define SYS_USART3_RX_PORT  GPIOB
#define SYS_USART3_RX_PIN   GPIO_Pin_11

/* ---- 第 4~6 路（F407 另有 UART4 / UART5 / USART6，共 6 路串口）----
 * 本板无专用座子,走排针;以下为芯片默认映射,换板只改这几个宏。
 * 需加串口时优先用第 4~6 路（USART2 与 ETH_MDIO 复用,USART3 接 ESP8266）。 */
#define SYS_UART4_TX_PORT   GPIOC
#define SYS_UART4_TX_PIN    GPIO_Pin_10
#define SYS_UART4_RX_PORT   GPIOC
#define SYS_UART4_RX_PIN    GPIO_Pin_11

#define SYS_UART5_TX_PORT   GPIOC
#define SYS_UART5_TX_PIN    GPIO_Pin_12
#define SYS_UART5_RX_PORT   GPIOD
#define SYS_UART5_RX_PIN    GPIO_Pin_2

#define SYS_USART6_TX_PORT  GPIOC
#define SYS_USART6_TX_PIN   GPIO_Pin_6
#define SYS_USART6_RX_PORT  GPIOC
#define SYS_USART6_RX_PIN   GPIO_Pin_7

/* -------------------- 默认波特率 -------------------- */
/* SYS_USART_Init 传入 0 时使用该默认值 */
#define SYS_USART_DEFAULT_BAUD     115200

/* -------------------- 中断接收缓冲 -------------------- */
/* 每路串口的接收环形缓冲大小（字节），仅"中断接收"模式使用 */
#define SYS_USART_RX_BUF_SIZE      64

/* -------------------- 发送等待超时 -------------------- */
/* 等 TXE / TC 标志的最大循环次数,跑满即放弃本次发送(返回 0,可据此统计丢字节)。
 * 数值出处:按 168MHz 每循环 3~6 拍估算,20 万次 ≈ 3.6~7ms,而 115200bps
 * 发一字节约 87us;跑满仅见于串口被硬件卡住或时钟被关闭。 */
#define SYS_USART_TX_TIMEOUT       200000UL

/* -------------------- 中断优先级 -------------------- */
/* 数值越小优先级越高;范围随 sys_nvic 分组:Group_2 抢占 0~3/子 0~3,
 *   Group_4（上 RTOS）抢占 0~15/子固定 0。
 * 约束:用 FreeRTOS 时抢占必须 ≥5（BASEPRI = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY<<4）;
 *   本工程该值 = 5<<4 = 0x50,抢占 <5 可打断内核临界区,其中禁止调 ...FromISR(),
 *   开 configASSERT 会断言失败;更缓取 6~15。 */
#define SYS_USART_IRQ_PRE_PRIO     5
#define SYS_USART_IRQ_SUB_PRIO     0

/* -------------------- printf 集成 -------------------- */
/* 1 = 本模块提供 fputc 重定向,printf() 输出到 SYS_USART_PRINTF_ID 串口;
 * 0 = 关闭,已自行实现 fputc 时须置 0 以免符号重复定义。 */
#define SYS_USART_FPUTC_ENABLE     1
/* 对应区块 2 枚举里的 SYS_USART_1（值为 0） */
#define SYS_USART_PRINTF_ID        SYS_USART_1

/* 格式化输出缓冲大小（字节），超出自动截断 */
#define SYS_USART_PRINTF_BUF_SIZE  128


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 串口编号（与内部配置表一一对应）
 * F407 共 6 路: USART1/2/3/6 与 UART4/5（UART 不带同步）。
 * 引脚在区块 1 宏里改;DMA 映射芯片固定,见下表。 */
typedef enum {
    SYS_USART_1 = 0,          /* USART1 → PA9   / PA10  */
    SYS_USART_2 = 1,          /* USART2 → PA2   / PA3   */
    SYS_USART_3 = 2,          /* USART3 → PB10  / PB11  */
    SYS_UART_4  = 3,          /* UART4  → PC10  / PC11  */
    SYS_UART_5  = 4,          /* UART5  → PC12  / PD2   */
    SYS_USART_6 = 5,          /* USART6 → PC6   / PC7   */
    SYS_USART_COUNT = 6
} SysUsartId_t;

/* DMA 数据流映射（芯片固定）:
 *   USART1: TX → DMA2_Stream7(通道4)   RX → DMA2_Stream5(通道4)
 *   USART2: TX → DMA1_Stream6(通道4)   RX → DMA1_Stream5(通道4)
 *   USART3: TX → DMA1_Stream3(通道4)   RX → DMA1_Stream1(通道4)
 *   UART4 : TX → DMA1_Stream4(通道4)   RX → DMA1_Stream2(通道4)
 *   UART5 : TX → DMA1_Stream7(通道4)   RX → DMA1_Stream0(通道4)
 *   USART6: TX → DMA2_Stream6(通道5)   RX → DMA2_Stream1(通道5)   ← 通道号是 5。 */

/* 初始化串口:完成时钟 / 引脚复用 / 8N1 参数配置。
 * 库内调用链: RCC_APB1/2PeriphClockCmd 开时钟（USART1 在 APB2,其余 APB1）、
 *   GPIO_PinAFConfig + GPIO_Init 引脚复用、USART_StructInit + USART_Init
 *   （USART_WordLength_8b、USART_Parity_No）、USART_Cmd 使能。
 * 参数 : id — SYS_USART_1/2/3,引脚见文件头接线;baudrate — 0 = 默认 115200。
 * 说明 : 可重复调用;切换系统主频后需再次调用以恢复波特率精度。 */
void SYS_USART_Init(SysUsartId_t id, uint32_t baudrate);

/* 发送一个字节,阻塞等待发送寄存器变空(TXE)。 */
void SYS_USART_SendByte(SysUsartId_t id, uint8_t byte);

/* 发送字符串,发到 '\0' 之前,不含结尾符。 */
void SYS_USART_SendString(SysUsartId_t id, const char *str);

/* 发送字符串并追加 "\r\n"。 */
void SYS_USART_SendLine(SysUsartId_t id, const char *str);

/* 发送 len 字节,阻塞至全部发出。
 * 约束 : buf 为 NULL 或 len=0 时直接返回。 */
void SYS_USART_SendBuf(SysUsartId_t id, const uint8_t *buf, uint16_t len);

/* 等待发送完成(TC:最后一位已移出移位寄存器后本函数才返回)。
 * 用途 : RS485 半双工方向脚翻转前须调用（见 sys_rs485 的 Send）。 */
void SYS_USART_FlushTx(SysUsartId_t id);

/* 查询是否有数据可读(轮询):1 = 有,0 = 无。 */
uint8_t SYS_USART_DataReady(SysUsartId_t id);

/* 轮询读取一个字节(非阻塞)。
 * 返回 : 0~255 = 读到的字节;-1 = 当前无数据或参数非法。 */
int SYS_USART_ReadByte(SysUsartId_t id);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* ---- 中断接收（环形缓冲，数据自动入库） ---- */

/* 初始化并开启中断接收,收到的字节自动存入内部环形缓冲。
 * 说明 : 比 SYS_USART_Init 多开 RXNE 中断;同一串口轮询与中断两种方式只取其一。 */
void SYS_USART_InitRxIT(SysUsartId_t id, uint32_t baudrate);

/* 缓冲区中未读字节数。 */
uint16_t SYS_USART_Available(SysUsartId_t id);

/* 从环形缓冲区读一个字节(非阻塞)。
 * 返回 : 0~255 = 读到的字节;-1 = 缓冲区为空或参数非法。 */
int SYS_USART_RxRead(SysUsartId_t id);

/* 清空接收缓冲区,丢弃积压数据。 */
void SYS_USART_RxFlush(SysUsartId_t id);

/* ---- 按结束符接收（不定长字符串 / 命令帧） ---- */

/* 按结束符收一帧不定长字符串。
 * 前提 : 该串口已 SYS_USART_InitRxIT（字节经中断进环形缓冲）。
 * 参数 : buf — 输出缓冲,函数补 '\0';max — 缓冲总大小,含结尾符,至少 2;
 *        end_ch — 帧结束字符（如 '#'、'\n'）;timeout_ms — 超时毫秒,
 *        0 = 不等待,先取走缓冲已有数据;字节间同按 timeout_ms 计时。
 * 返回 : >=0 = 帧长度,结束符不存入 buf;-1 = 超时,半帧作废;-2 = 缓冲放不下或参数非法。
 * 计时 : DWT 硬件周期,不占用 SysTick,最长约 25 秒。 */
int SYS_USART_ReadUntil(SysUsartId_t id, char *buf, uint16_t max, char end_ch, uint32_t timeout_ms);

/* 取走接收缓冲中当前已有的字节(不定长,不等待,非阻塞)。
 * 返回 : 实际取走的字节数,0 = 缓冲暂时无数据。 */
uint16_t SYS_USART_ReadBuf(SysUsartId_t id, uint8_t *buf, uint16_t max);

/* ---- DMA 收发（数据由 DMA 搬运,CPU 不参与） ----
 * 前提 : 先调用 SYS_USART_Init(id, baud);DMA 控制器时钟与数据流由 sys_dma 配置。
 * 数据流占用（芯片固定映射,自行调用 SYS_DMA_* 时需避开）见上文"DMA 数据流映射"。 */

/* DMA 发送(非阻塞):len 字节交 DMA 搬运,函数立即返回。
 * 返回 : 0 = 已启动;1 = 拒绝（上一笔未完成或参数错）。
 * 约束 : 发送期间 buf 内容不可修改;复用前先用 TxDmaBusy 查空闲。 */
uint8_t SYS_USART_SendDMA(SysUsartId_t id, const uint8_t *buf, uint16_t len);

/* DMA 发送是否进行中:1 = 未发完。 */
uint8_t SYS_USART_TxDmaBusy(SysUsartId_t id);

/* DMA 接收(非阻塞):空闲中断 + DMA 接收不定长帧。
 * 原理 : 每收一个字节 DMA 存一个;总线空闲(一帧结束)触发 IDLE 中断,
 *        中断内记下本帧字节数。
 * 返回 : 0 = 已启动;1 = 拒绝（参数错或该路已在接收）。
 * 约束 : 不可与 InitRxIT 的中断接收模式同时使用。 */
uint8_t SYS_USART_RecvDMA(SysUsartId_t id, uint8_t *buf, uint16_t maxlen);

/* 已收字节数:接收中实时变化,检测到空闲后定格为整帧长度。 */
uint16_t SYS_USART_DmaRxLen(SysUsartId_t id);

/* 一帧是否收完(检测到空闲或缓冲收满均算):1 = 收完。 */
uint8_t SYS_USART_DmaRxDone(SysUsartId_t id);

/* ---- 格式化输出 ---- */

/* 类 printf 的格式化发送,内部缓冲 SYS_USART_PRINTF_BUF_SIZE,超长截断。 */
int SYS_USART_Printf(SysUsartId_t id, const char *format, ...);

/* 格式化到调用方缓冲并发送。
 * 区别 : Printf 用库内部缓冲;本函数用传入 buf,格式化结果可二次使用。
 * 参数 : buf/size — 调用方缓冲及大小;format... — 同 printf。
 * 返回 : 实际发送字符数,超长按 size 截断。 */
int SYS_USART_SendFormat(SysUsartId_t id, char *buf, uint16_t size, const char *format, ...);


/* ================================================================
 *  附:USART_TypeDef 成员（定义在 stm32f4xx.h）
 * ================================================================
 *  SR — TXE 发送寄存器空 / RXNE 收到数据 / TC 发送完成 / IDLE 总线空闲
 *       （标志宏 USART_FLAG_TXE/_RXNE/_TC/_IDLE）;
 *  DR — 写入发送、读出接收（SendData / ReceiveData）;
 *  BRR — 波特率分频,按 PCLK 与目标波特率计算（USART_Init 写入）;
 *  CR1 — UE(USART_CR1_UE)、RE/TE、字长(USART_WordLength_8b)、RXNEIE(USART_CR1_RXNEIE);
 *  CR2 — 停止位(USART_StopBits_1);CR3 — DMA 收发请求(USART_DMAReq_Tx / _Rx);
 *  GTPR — 保护时间与预分频,智能卡/单线模式用,库未使用。
 * ================================================================ */

#endif /* __FWLIB_SYS_USART_H */
