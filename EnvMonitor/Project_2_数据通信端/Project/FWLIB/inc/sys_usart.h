#ifndef __FWLIB_SYS_USART_H
#define __FWLIB_SYS_USART_H

#include "stm32f4xx.h"

/* sys_usart.h: 串口(USART)模块头文件
 *
 * 覆盖 F407 全部 6 路串口: USART1 / USART2 / USART3 / UART4 / UART5 / USART6。
 * 初始化一次完成时钟、引脚复用、参数配置。引脚宏见"区块 1"。
 *
 * 本板接线（普中-天马 F407 开发板原理图；换板子改"区块 1"）:
 *      USART1 : TX=PA9  RX=PA10 : 接 CH340C（USB2 口，下载/串口调试）
 *      USART2 : TX=PA2  RX=PA3  : 跳线 P6 选：SP3485(RS485 端子) 或 SP3232(RS232→DB9)
 *      USART3 : TX=PB10 RX=PB11 : 跳线 P10 选：ESP8266(WIFI, 网名 W_TX/W_RX)
 *                                  或 绿色 3P 端子(TXD3/RXD3) / RS232
 *      UART4  : TX=PC10 RX=PC11 : 板上无专用座子，走排针引出
 *      UART5  : TX=PC12 RX=PD2  : 板上无专用座子，走排针引出
 *      USART6 : TX=PC6  RX=PC7  : 芯片默认映射，部分 F407 板子上这两脚是晶振/复用区，
 *                                  上板前核对自己的板（引脚在第 4~6 路宏里改）
 *
 * 前三路串口要靠跳线帽才能通到对外接口:
 *      1) USART1 ↔ CH340C：板上丝印 PA9T↔URXD、PA10R↔UTXD 两组，需插 2 枚跳线帽，
 *         否则 USB 串口助手收不到数据；
 *      2) USART2 ↔ SP3485/SP3232：跳线 P6（485R/485T、PA3/PA2T、232R/232T）；
 *      3) USART3 ↔ WIFI/上位机：跳线 P10（TXD3/RXD3、PB10/PB11、H_TX/H_RX）。
 * 本板无以太网 PHY：原理图标题为"以太网模块接口(CN4)"的块实为 NRF24L01 插座，
 *    网络名是 NRF_CS/SPI1_SCK/NRF_CE/SPI1_MOSI/NRF_IRQ/SPI1_MISO。
 * DMA 映射第 4~6 路通道号与前三路不同（表见 SYS_USART_COUNT 声明处）：
 *    UART4/UART5 = DMA1 通道 4，USART6 = DMA2 通道 5。 */


/* 区块 1：定义与宏定义区（换板子只改这里） */
/* -------------------- 串口引脚 -------------------- */
/* 均为复用模式：TX 为复用推挽输出（GPIO_Mode_AF + GPIO_OType_PP），RX 为复用输入；
 * 两脚都开内部上拉 GPIO_PuPd_UP */
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
 * 本板这三路无专用座子，都走排针引出；下面填的是芯片默认映射，
 * 换板子只改这几个宏（SYS_USART_Init 按宏配置）。 */
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
/* 等 TXE / TC 标志的最大循环次数，跑满即放弃本次发送，防止死等卡死任务
 * 按 168MHz 每循环约 3~6 拍估算，20 万次 ≈ 3.6~7ms；
 * 115200bps 下发一个字节约 87us，余量足够。
 * 跑满即放弃本次发送并返回"未发出"（FlushTx 无返回值，超时直接返回）。 */
#define SYS_USART_TX_TIMEOUT       200000UL

/* -------------------- 中断优先级 -------------------- */
/* 数值越小优先级越高；可用范围随 sys_nvic 的分组变化:
 *   NVIC_PriorityGroup_4（本库默认）: 抢占 0~15、子固定 0
 *   NVIC_PriorityGroup_2（纯裸机）  : 抢占 0~3、子 0~3
 *
 * 用 FreeRTOS 时必须 ≥5（默认已给 5）:
 *    FreeRTOS 用 BASEPRI = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY<<4
 *    （本工程 = 5<<4 = 0x50）屏蔽"允许调 FromISR 的中断"。
 *    抢占数值 < 5 的中断能打断内核临界区，禁止在其中调用任何
 *    ...FromISR() 接口；开了 configASSERT 会断言失败。
 *    更急只能取 5，更缓可取 6~15。 */
#define SYS_USART_IRQ_PRE_PRIO     5
#define SYS_USART_IRQ_SUB_PRIO     0

/* -------------------- printf 集成 -------------------- */
/* 1 = 本模块提供 fputc 重定向：printf() 输出到 SYS_USART_PRINTF_ID 串口
 * 0 = 关闭（自己已实现 fputc 时置 0，避免符号重复定义） */
#define SYS_USART_FPUTC_ENABLE     1
/* 对应区块 2 枚举里的 SYS_USART_1（值为 0） */
#define SYS_USART_PRINTF_ID        SYS_USART_1

/* 格式化输出缓冲大小（字节），超出自动截断 */
#define SYS_USART_PRINTF_BUF_SIZE  128


/* 区块 2：基础功能 */
/* 串口编号（与内部配置表一一对应）
 * F407 共 6 路: USART1/2/3/6 与 UART4/5。
 * 引脚在"区块 1"宏里改；DMA 映射由芯片固定，见下表。 */
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
 *   USART6: TX → DMA2_Stream6(通道5)   RX → DMA2_Stream1(通道5)，通道号是 5 */

/* 初始化串口：完成时钟、引脚复用、8N1 参数配置
 * （8N1 = USART_WordLength_8b + USART_Parity_No + USART_StopBits_1）
 *
 * 参数 : id: 串口编号，SYS_USART_1 ~ SYS_USART_6（引脚对照见文件头"本板接线"）
 *        baudrate: 波特率（0 = 默认 115200）；常用 9600 / 115200 / 460800
 * 说明 : 可重复调用重新配置；切换系统主频后调用一次即可让波特率恢复准确
 * 标准库 : RCC_APB1/2PeriphClockCmd（USART1 在 APB2，其余 APB1）+ GPIO_PinAFConfig
 *          + GPIO_Init + USART_StructInit + USART_Init + USART_Cmd
 *          中断接收另加 USART_ITConfig(USART_IT_RXNE) + NVIC_Init，见 InitRxIT */
void SYS_USART_Init(SysUsartId_t id, uint32_t baudrate);

/* 发送一个字节（阻塞等待发送寄存器变空）
 * 标准库 : USART_GetFlagStatus(USART_FLAG_TXE 查空) + USART_SendData */
void SYS_USART_SendByte(SysUsartId_t id, uint8_t byte);

/* 发送字符串（发到 '\0' 之前，不含结尾符） */
void SYS_USART_SendString(SysUsartId_t id, const char *str);

/* 发送字符串并自动追加 "\r\n"（串口助手按行显示） */
void SYS_USART_SendLine(SysUsartId_t id, const char *str);

/* 发送一段数据（len 字节，阻塞直到全部发出）
 * 用途 : 发二进制包 / 缓冲区内容；buf 为 NULL 或 len=0 直接返回 */
void SYS_USART_SendBuf(SysUsartId_t id, const uint8_t *buf, uint16_t len);

/* 等待发送彻底完成（TC 标志：最后一位已移出移位寄存器）
 * 用途 : RS485 半双工发完才能松开方向脚（见 sys_rs485 的 Send）；
 *        发完再切串口 / 进睡眠等场合也用
 * 标准库 : USART_GetFlagStatus(USART_FLAG_TC) */
void SYS_USART_FlushTx(SysUsartId_t id);

/* 查询是否有数据可读（轮询方式）：1 = 有，0 = 无
 * 标准库 : USART_GetFlagStatus(USART_FLAG_RXNE) */
uint8_t SYS_USART_DataReady(SysUsartId_t id);

/* 轮询读取一个字节（非阻塞）
 * 返回 : 0~255 = 读到的字节；-1 = 当前无数据 / 参数非法
 * 标准库 : USART_GetFlagStatus(USART_FLAG_RXNE) + USART_ReceiveData */
int SYS_USART_ReadByte(SysUsartId_t id);


/* 区块 3：扩展功能 */
/* ---- 中断接收（环形缓冲，数据自动入库） ---- */

/* 初始化并开启"中断接收"：收到的字节自动存入内部环形缓冲
 * 说明 : 与 SYS_USART_Init 的区别是额外打开 RXNE 中断；
 *        同一串口的"轮询 / 中断"两种方式只用一种
 * 标准库 : USART_ITConfig(USART_IT_RXNE) + NVIC_Init（中断内 USART_GetITStatus +
 *          USART_ReceiveData，ISR 在库内实现）
 * 中断缓冲大小 : SYS_USART_RX_BUF_SIZE */
void SYS_USART_InitRxIT(SysUsartId_t id, uint32_t baudrate);

/* 缓冲区中还有多少字节未读 */
uint16_t SYS_USART_Available(SysUsartId_t id);

/* 从缓冲区读一个字节（非阻塞）
 * 返回 : 0~255 = 读到的字节；-1 = 缓冲区为空 / 参数非法 */
int SYS_USART_RxRead(SysUsartId_t id);

/* 清空接收缓冲区，丢弃积压数据 */
void SYS_USART_RxFlush(SysUsartId_t id);

/* ---- 按结束符接收（不定长字符串 / 命令帧） ---- */
/* 收以 end_ch 结尾的不定长字符串，收帧、拼接、超时、越界保护都在库内
 * 前提 : 该串口已 SYS_USART_InitRxIT（字节靠中断进环形缓冲）
 * 参数 : buf: 输出缓冲（函数会补 '\0'）；
 *        max: 缓冲总大小（含结尾符，至少 2）；
 *        end_ch: 帧结束字符（如 '#'、'\n'）；
 *        timeout_ms: 超时毫秒；0 = 不等待（先把缓冲里已有的收完）
 * 返回 : >=0 = 帧长度（结束符不存入 buf）；-1 = 超时（半帧作废）；
 *        -2 = 缓冲放不下 或 参数/串口非法
 * 说明 : 字节与字节之间也按 timeout_ms 计时，发送方中途停顿超过它就判超时；
 *        计时用 DWT 硬件周期（无需 SysTick），最长约 25 秒
 * 扩展 : 不用结束符、按总线空闲判帧，用 RecvDMA（空闲中断版） */
int SYS_USART_ReadUntil(SysUsartId_t id, char *buf, uint16_t max, char end_ch, uint32_t timeout_ms);

/* 取走接收缓冲中当前已有的字节（不定长，不等待，非阻塞）
 * 返回 : 实际取走的字节数（0 = 缓冲里暂时没有数据） */
uint16_t SYS_USART_ReadBuf(SysUsartId_t id, uint8_t *buf, uint16_t max);

/* ---- DMA 收发（数据由 DMA 搬运，CPU 不参与） ----
 * 前提 : 先调用过 SYS_USART_Init(id, baud)；
 *       DMA 控制器时钟与数据流由 sys_dma 层配置
 * 数据流占用（芯片固定映射；自己调 SYS_DMA_* 时需避开）:
 *   USART1: TX → DMA2_Stream7(通道4)   RX → DMA2_Stream5(通道4)
 *   USART2: TX → DMA1_Stream6(通道4)   RX → DMA1_Stream5(通道4)
 *   USART3: TX → DMA1_Stream3(通道4)   RX → DMA1_Stream1(通道4) */

/* DMA 发送（非阻塞）：len 字节交给 DMA 后台搬走，函数立刻返回
 * 返回 : 0 = 已启动；1 = 拒绝（上一笔还没发完 / 参数错）
 * 注意 : 发送期间 buf 内容不能改；先 TxDmaBusy 查空闲再复用
 * 标准库 : USART_DMACmd(Tx) + 经 sys_dma → DMA_Init 系列 */
uint8_t SYS_USART_SendDMA(SysUsartId_t id, const uint8_t *buf, uint16_t len);

/* DMA 发送是否仍在进行：1 = 还没发完 */
uint8_t SYS_USART_TxDmaBusy(SysUsartId_t id);

/* DMA 接收（非阻塞）：空闲中断 + DMA 接收不定长数据帧
 * 原理 : 数据来一个、DMA 存一个；总线空闲（一帧结束）触发中断，
 *        中断里记下本帧字节数
 * 返回 : 0 = 已启动；1 = 拒绝（参数错 / 该路已在收）
 * 注意 : 与 InitRxIT 的中断接收模式二选一
 * 标准库 : USART_DMACmd(USART_DMAReq_Rx) + USART_ITConfig(USART_IT_IDLE) + NVIC_Init
 *          + 经 sys_dma → DMA_Init 系列 */
uint8_t SYS_USART_RecvDMA(SysUsartId_t id, uint8_t *buf, uint16_t maxlen);

/* 已收到字节数（收的过程中实时变化；检测到空闲后定格整帧长度） */
uint16_t SYS_USART_DmaRxLen(SysUsartId_t id);

/* 一帧是否收完（检测到空闲 / 缓冲收满均算收完）：1 = 收完 */
uint8_t SYS_USART_DmaRxDone(SysUsartId_t id);

/* ---- 格式化输出 ---- */

/* 类 printf 的格式化发送（内部缓冲 SYS_USART_PRINTF_BUF_SIZE，超长截断） */
int SYS_USART_Printf(SysUsartId_t id, const char *format, ...);

/* 格式化到调用方缓冲并发送
 * 区别 : Printf 用库内部缓冲；本函数用传入的 buf（格式化结果可二次使用）
 * 参数 : buf/size: 缓冲及大小；format...: 与 printf 相同
 * 返回 : 实际发送的字符数（超长按 size 截断） */
int SYS_USART_SendFormat(SysUsartId_t id, char *buf, uint16_t size, const char *format, ...);


/* 附: USART_TypeDef 成员速查（定义在 stm32f4xx.h）
 *    SR    USART_FLAG_TXE（发送寄存器空）/ _RXNE（收到数据）/
 *          _TC（发送完成）/ _IDLE（总线空闲）：收发前等状态轮询它
 *    DR    写入 = 发送、读出 = 接收（USART_SendData / USART_ReceiveData）
 *    BRR   波特率分频，按 PCLK 与目标波特率算出（USART_Init 写它）
 *    CR1   USART_CR1_UE 使能 / _RE、_TE 收发开关 / USART_WordLength_8b 字长 /
 *          _RXNEIE 接收中断使能（USART_Cmd、USART_ITConfig 写它）
 *    CR2   停止位（USART_StopBits_1 等，USART_Init 写它）
 *    CR3   DMA 收发请求使能（USART_DMAReq_Tx / _Rx）
 *    GTPR  保护时间与预分频，智能卡/单线模式用，库未用 */

#endif /* __FWLIB_SYS_USART_H */
