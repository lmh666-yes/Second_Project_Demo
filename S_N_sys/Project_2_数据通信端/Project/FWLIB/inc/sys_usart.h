#ifndef __FWLIB_SYS_USART_H
#define __FWLIB_SYS_USART_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_usart.h —— 【系统】串口(USART)模块  头文件
 * ================================================================
 *  设计定位 : 标准外设库 USART 的"薄封装"串口服务
 *             —— 初始化一次到位（时钟 / 引脚复用 / 参数全自动）
 *             —— 覆盖本板 3 路串口: USART1 / USART2 / USART3
 *  标准库关键词 : USART_Init / USART_Cmd / USART_SendData / USART_ReceiveData /
 *                 USART_ITConfig / USART_DMACmd / USART_GetFlagStatus / GPIO_PinAFConfig
 *
 *  本板接线（普中-天马 F407开发板原理图；换板子改"区块 1"即可）:
 *      USART1 : TX=PA9  RX=PA10 —— 接 CH340C（USB2 口，下载/串口调试）
 *      USART2 : TX=PA2  RX=PA3  —— 跳线 P5 选：SP3485(RS485 端子) 或 SP3232(RS232→DB9)
 *      USART3 : TX=PB10 RX=PB11 —— 跳线 P10 选：ESP8266(WIFI, 网名 W_TX/W_RX)
 *                                  或 绿色 3P 端子(TXD3/RXD3) / RS232
 *
 *  ⚠⚠ 三路串口在本板上**都要靠跳线帽才能通到对外接口**，最容易踩的坑：
 *      ① USART1 ↔ CH340C：板上丝印 `PA9T`↔`URXD`、`PA10R`↔`UTXD` 两组，
 *         **必须插 2 枚跳线帽**，否则 USB 串口助手完全没反应（引脚全对也收不到）；
 *      ② USART2 ↔ SP3485/SP3232：跳线 P5（`485R/485T`、`PA3/PA2T`、`232R/232T`）；
 *      ③ USART3 ↔ WIFI/上位机：跳线 P10（`TXD3/RXD3`、`PB10/PB11`、`H_TX/H_RX`）。
 *  ⚠ 本板**没有以太网 PHY**：原理图上那个标题叫"以太网模块接口(CN4)"的块，
 *    实际网络是 NRF_CS/SPI1_SCK/NRF_CE/SPI1_MOSI/NRF_IRQ/SPI1_MISO
 *    —— 它就是 NRF24L01 插座，块标题是原作者笔误；不要按"和以太网二选一"来理解。
 *
 *  使用方式 :
 *      SYS_USART_Init(SYS_USART_1, 115200);            // ① 初始化
 *      SYS_USART_SendLine(SYS_USART_1, "hello world"); // ② 发送(带回车换行)
 *      if (SYS_USART_DataReady(SYS_USART_1)) {         // ③ 轮询接收
 *          int c = SYS_USART_ReadByte(SYS_USART_1);
 *      }
 *      // 或：中断接收（数据自动进缓冲区，随时来取）
 *      SYS_USART_InitRxIT(SYS_USART_1, 115200);
 *      if (SYS_USART_Available(SYS_USART_1)) { ... }
 *      // 或：按结束符收"整条命令字符串"（不定长、自带超时）
 *      char line[64];
 *      if (SYS_USART_ReadUntil(SYS_USART_1, line, sizeof(line), '#', 1000) >= 0) { ... }
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* -------------------- 串口引脚 -------------------- */
/* 均为复用模式：TX 复用推挽输出（GPIO_Mode_AF + GPIO_OType_PP）；RX 复用输入（GPIO_Mode_AF）；
 * 两者都开内部上拉 GPIO_PuPd_UP（防悬空；对推挽输出脚而言上拉无副作用） */
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

/* -------------------- 默认波特率 -------------------- */
/* SYS_USART_Init 传入 0 时使用该默认值 */
#define SYS_USART_DEFAULT_BAUD     115200

/* -------------------- 中断接收缓冲 -------------------- */
/* 每路串口的接收环形缓冲大小（字节），仅"中断接收"模式使用 */
#define SYS_USART_RX_BUF_SIZE      64

/* -------------------- 中断优先级 -------------------- */
/* 数值越小优先级越高；可用范围随 sys_nvic 的分组变化——
 *   NVIC_PriorityGroup_2（库默认）: 抢占 0~3、子 0~3（这里的 2/0 = 与全库同级;
 *     想更急改 1 或 0，想更缓改 3）
 *   NVIC_PriorityGroup_4（上 RTOS）: 抢占 0~15、子固定 0 */
#define SYS_USART_IRQ_PRE_PRIO     2
#define SYS_USART_IRQ_SUB_PRIO     0

/* -------------------- printf 集成 -------------------- */
/* 1 = 本模块提供 fputc 重定向：printf() 输出到 SYS_USART_PRINTF_ID 串口
 * 0 = 关闭（若你自己已实现 fputc，请改为 0 以免符号重复定义） */
#define SYS_USART_FPUTC_ENABLE     1
/* 对应区块 2 枚举里的 SYS_USART_1（值为 0） */
#define SYS_USART_PRINTF_ID        SYS_USART_1

/* 格式化输出缓冲大小（字节），超出自动截断 */
#define SYS_USART_PRINTF_BUF_SIZE  128


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 串口编号（与内部配置表一一对应） */
typedef enum {
    SYS_USART_1 = 0,          /* USART1 → PA9  / PA10 */
    SYS_USART_2 = 1,          /* USART2 → PA2  / PA3  */
    SYS_USART_3 = 2,          /* USART3 → PB10 / PB11 */
    SYS_USART_COUNT = 3
} SysUsartId_t;

/* 初始化串口：自动完成 时钟 / 引脚复用 / 8N1 参数配置
 * （8N1 = USART_WordLength_8b + USART_Parity_No + USART_StopBits_1）
 *
 * 标准库调用链（库内部依次调用，可对照学习）:
 *   ① RCC_APB1/2PeriphClockCmd  开串口总线时钟（USART1 在 APB2，其余 APB1）
 *   ② GPIO_PinAFConfig + GPIO_Init  TX/RX 引脚复用为串口（GPIO_Mode_AF + GPIO_OType_PP）
 *   ③ USART_StructInit + USART_Init  波特率 / 8 位字长（USART_WordLength_8b）/ 无校验（USART_Parity_No）
 *   ④ USART_Cmd                 使能串口
 *   （中断接收另加 USART_ITConfig(USART_IT_RXNE) + NVIC_Init,见 InitRxIT）
 *
 * 参数 : id —— 串口编号，三选一: SYS_USART_1 / SYS_USART_2 / SYS_USART_3
 *              （三者引脚对照见文件头"本板接线"）
 *        baudrate —— 波特率（0 = 默认 115200）;常用:9600 / 115200 / 460800
 * 说明 : 可重复调用（重新配置）；
 *        切换系统主频后调用一次即可让波特率恢复准确
 * 示例 : SYS_USART_Init(SYS_USART_1, 115200);   // 调试串口 115200
 *        SYS_USART_Init(SYS_USART_1, 0);        // 0 = 用默认 115200
 * 扩展提示 : 想"奇偶校验/数据位可调"——复制本函数,改 USART_StructInit
 *            之后的 ui.USART_Parity / ui.USART_WordLength 两行即可
 *            （SPL 字段表见 stm32f4xx_usart.h 的 USART_InitTypeDef）*/
void SYS_USART_Init(SysUsartId_t id, uint32_t baudrate);

/* 发送一个字节（阻塞等待发送寄存器变空）
 * 标准库 : USART_GetFlagStatus(USART_FLAG_TXE 查空) + USART_SendData
 * 示例 : SYS_USART_SendByte(SYS_USART_1, 'A');
 *        SYS_USART_SendLine(SYS_USART_1, "hello");   // 输出 hello + 回车换行 */
void SYS_USART_SendByte(SysUsartId_t id, uint8_t byte);

/* 发送字符串（发到 '\0' 之前，不含结尾符）
 * 示例 : SYS_USART_SendString(SYS_USART_1, "temp=25"); */
void SYS_USART_SendString(SysUsartId_t id, const char *str);

/* 发送字符串并自动追加 "\r\n"（串口助手按行显示）
 * 示例 : SYS_USART_SendLine(SYS_USART_1, "hello");   // 输出 hello + 回车换行 */
void SYS_USART_SendLine(SysUsartId_t id, const char *str);

/* 发送一段数据（len 字节，阻塞直到全部发出）
 * 用途 : 发二进制包 / 缓冲区内容；buf 为 NULL 或 len=0 直接返回
 * 示例 : SYS_USART_SendBuf(SYS_USART_1, buf, 16);   // 发 16 字节原始数据 */
void SYS_USART_SendBuf(SysUsartId_t id, const uint8_t *buf, uint16_t len);

/* 等待发送彻底完成（TC 标志：最后一位已移出移位寄存器）
 * 用途 : RS485 半双工"发完才能松开方向脚"（先例:sys_rs485 的 Send）;
 *        发完再切串口/进睡眠等场合也用
 * 标准库 : USART_GetFlagStatus(USART_FLAG_TC)
 * 示例 : SYS_USART_SendBuf(SYS_USART_2, buf, n);
 *        SYS_USART_FlushTx(SYS_USART_2);      // 等最后一位发完再继续 */
void SYS_USART_FlushTx(SysUsartId_t id);

/* 查询是否有数据可读（轮询方式）：1 = 有，0 = 无
 * 标准库 : USART_GetFlagStatus(USART_FLAG_RXNE)
 * 示例 : if (SYS_USART_DataReady(SYS_USART_1)) {
 *            int c = SYS_USART_ReadByte(SYS_USART_1);   // c = 0~255 或 -1
 *        } */
uint8_t SYS_USART_DataReady(SysUsartId_t id);

/* 轮询读取一个字节（非阻塞）
 * 返回 : 0~255 = 读到的字节；-1 = 当前无数据 / 参数非法
 * 标准库 : USART_GetFlagStatus(USART_FLAG_RXNE) + USART_ReceiveData
 * 示例 : int c = SYS_USART_ReadByte(SYS_USART_1);   // -1 = 当前没数据 */
int SYS_USART_ReadByte(SysUsartId_t id);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* ---- 中断接收（环形缓冲，数据自动入库） ---- */

/* 初始化并开启"中断接收"：收到的字节自动存入内部环形缓冲
 * 说明 : 与 SYS_USART_Init 的唯一区别是额外打开了 RXNE 中断；
 *        同一串口的"轮询 / 中断"两种方式建议只用一种
 * 标准库 : USART_ITConfig(USART_IT_RXNE) + NVIC_Init（中断内: USART_GetITStatus +
 *          USART_ReceiveData,库已代写 ISR）
 * 示例 : SYS_USART_InitRxIT(SYS_USART_1, 115200);   // 收到的字节自动进缓冲 */
void SYS_USART_InitRxIT(SysUsartId_t id, uint32_t baudrate);

/* 缓冲区中还有多少字节未读
 * 示例 : if (SYS_USART_Available(SYS_USART_1) > 0) { ... } */
uint16_t SYS_USART_Available(SysUsartId_t id);

/* 从缓冲区读一个字节（非阻塞）
 * 返回 : 0~255 = 读到的字节；-1 = 缓冲区为空 / 参数非法
 * 示例 : int c = SYS_USART_RxRead(SYS_USART_1);   // 环形缓冲取一个 */
int SYS_USART_RxRead(SysUsartId_t id);

/* 清空接收缓冲区
 * 示例 : SYS_USART_RxFlush(SYS_USART_1);   // 丢弃积压数据 */
void SYS_USART_RxFlush(SysUsartId_t id);

/* ---- 按结束符接收（不定长字符串 / 命令帧） ---- */
/* 收一串"以 end_ch 结尾"的字符串（不定长帧）——对应教材"以 # 为帧
 * 结束标志"的收法,但收帧/拼接/超时/越界保护全在库内
 * 前提 : 该串口已 SYS_USART_InitRxIT（字节靠中断进环形缓冲）
 * 参数 : buf   —— 输出缓冲（函数会补 '\0'）;
 *        max   —— 缓冲总大小（含结尾符,至少 2）;
 *        end_ch —— 帧结束字符（如 '#'、'\n'）;
 *        timeout_ms —— 超时毫秒;0 = 不等待（先把缓冲里已有的收完）
 * 返回 : >=0 = 帧长度（结束符不存入 buf,已去掉）;
 *        -1 = 超时（半帧作废）; -2 = 缓冲放不下 或 参数/串口非法
 * 说明 : 字节与字节之间也按 timeout_ms 计时——发送方中途停顿超过它
 *        就判超时;计时用 DWT 硬件周期(无需 SysTick),最长约 25 秒
 * 示例 : char line[64];
 *        if (SYS_USART_ReadUntil(SYS_USART_1, line, sizeof(line), '#', 1000) >= 0) {
 *            // line 即一条完整命令(不含 '#'),交给 sys_str 解析
 *        }
 * 扩展提示 : 不用结束符、按"总线空闲"判帧 → 用 RecvDMA（空闲中断版） */
int SYS_USART_ReadUntil(SysUsartId_t id, char *buf, uint16_t max, char end_ch, uint32_t timeout_ms);

/* 取走接收缓冲中"当前已有的字节"（不定长,不等待,非阻塞）
 * 返回 : 实际取走的字节数（0 = 缓冲里暂时没有数据）
 * 示例 : uint8_t raw[64];
 *        uint16_t n = SYS_USART_ReadBuf(SYS_USART_1, raw, sizeof(raw)); */
uint16_t SYS_USART_ReadBuf(SysUsartId_t id, uint8_t *buf, uint16_t max);

/* ---- DMA 收发（数据自动搬运，CPU 不参与） ----
 * 前提 : 先调用过 SYS_USART_Init(id, baud)（串口本身要配好；
 *       DMA 控制器时钟与数据流由 sys_dma 层自动配置）
 * 数据流占用（芯片固定映射；你自己调 SYS_DMA_* 时需避开）:
 *   USART1: TX → DMA2_Stream7(通道4)   RX → DMA2_Stream5(通道4)
 *   USART2: TX → DMA1_Stream6(通道4)   RX → DMA1_Stream5(通道4)
 *   USART3: TX → DMA1_Stream3(通道4)   RX → DMA1_Stream1(通道4) */

/* DMA 发送（非阻塞）：len 字节交给 DMA 后台搬走，函数立刻返回
 * 返回 : 0 = 已启动；1 = 拒绝（上一笔还没发完 / 参数错）
 * 注意 : 发送期间 buf 内容不能改；先 TxDmaBusy 查空闲再复用
 * 标准库 : USART_DMACmd(Tx) + 经 sys_dma → DMA_Init 系列 */
uint8_t SYS_USART_SendDMA(SysUsartId_t id, const uint8_t *buf, uint16_t len);

/* DMA 发送是否仍在进行：1 = 还没发完
 * 示例 : if (SYS_USART_TxDmaBusy(SYS_USART_1) == 0) { ... }   // 空闲可发下一笔 */
uint8_t SYS_USART_TxDmaBusy(SysUsartId_t id);

/* DMA 接收（非阻塞）："空闲中断 + DMA"接收不定长数据帧
 * 原理 : 数据来一个、DMA 存一个；总线空闲(一帧结束)触发中断
 *        记下"这帧收了多少字节"
 * 返回 : 0 = 已启动；1 = 拒绝（参数错 / 该路已在收）
 * 注意 : 与 InitRxIT 的中断接收模式不要同时用（二选一）
 * 标准库 : USART_DMACmd(USART_DMAReq_Rx) + USART_ITConfig(USART_IT_IDLE) + NVIC_Init
 *          + 经 sys_dma → DMA_Init 系列 */
uint8_t SYS_USART_RecvDMA(SysUsartId_t id, uint8_t *buf, uint16_t maxlen);

/* 已收到字节数（收的过程中实时变化；检测到空闲后定格整帧长度）
 * 示例 : uint16_t n = SYS_USART_DmaRxLen(SYS_USART_1); */
uint16_t SYS_USART_DmaRxLen(SysUsartId_t id);

/* 一帧是否收完（检测到空闲 / 缓冲收满均算收完）：1 = 收完
 * 示例 : if (SYS_USART_DmaRxDone(SYS_USART_1)) { ... } */
uint8_t SYS_USART_DmaRxDone(SysUsartId_t id);

/* ---- 格式化输出 ---- */

/* 类 printf 的格式化发送（内部缓冲 SYS_USART_PRINTF_BUF_SIZE，超长截断）
 * 示例 : SYS_USART_Printf(SYS_USART_1, "x=%d\r\n", x);   // 格式化输出 */
int SYS_USART_Printf(SysUsartId_t id, const char *format, ...);

/* 格式化到"你的缓冲"并发送——一步到位
 * 区别 : Printf 用库内部缓冲;本函数用你给的 buf（格式化结果还能二次使用）
 * 参数 : buf/size —— 你的缓冲及大小;format... —— 与 printf 相同
 * 返回 : 实际发送的字符数（超长按 size 截断）
 * 示例 : char msg[64];
 *        SYS_USART_SendFormat(SYS_USART_1, msg, sizeof(msg), "TEMP:%d\r\n", t); */
int SYS_USART_SendFormat(SysUsartId_t id, char *buf, uint16_t size, const char *format, ...);


/* ================================================================
 *  附:标准库结构体速查 —— USART_TypeDef（定义在 stm32f4xx.h）
 * ================================================================
 *  成员一览（含库中用法）:
 *    SR      状态:USART_FLAG_TXE（发送寄存器空）/ _RXNE（收到数据）/
 *            _TC（发送完成）/ _IDLE（总线空闲）—— 库的收发前"等状态"都轮询它
 *    DR      数据:写入 = 发送、读出 = 接收（SendData / ReceiveData）
 *    BRR     波特率:按 PCLK 与目标波特率分频（USART_Init 算好写它）
 *    CR1     控制 1:USART_CR1_UE 使能 / _RE、_TE 收发开关 /
 *            USART_WordLength_8b 字长 / _RXNEIE 接收中断使能
 *            （USART_Cmd、USART_ITConfig 写它）
 *    CR2     控制 2:停止位（USART_StopBits_1 等;USART_Init 的停止位字段）
 *    CR3     控制 3:DMA 收发请求使能（USART_DMAReq_Tx / _Rx——
 *            库的 DMA 收发就靠它把串口和 DMA 通道接起来）
 *    GTPR    保护时间和预分频:智能卡/单线模式用,库未用
 * ================================================================ */

#endif /* __FWLIB_SYS_USART_H */
