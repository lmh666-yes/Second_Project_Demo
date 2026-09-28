#ifndef __FWLIB_SYS_FRAME_H
#define __FWLIB_SYS_FRAME_H

#include "stm32f4xx.h"
#include "sys_usart.h"

/* ================================================================
 *  sys_frame.h —— 【系统】串口自定义帧协议模块（组帧 / 校验 / 收帧）  头文件
 * ================================================================
 *  设计定位 : "帧头 + 数据 + 校验 + 帧尾"串口协议的"薄封装"——
 *             组帧、逐字节收帧、帧同步、校验全部在库内,
 *             应用只管"发什么"和"收到后做什么"
 *  标准库关键词 : 无——字节收发走 sys_usart;校验为纯字节异或运算
 *
 *  帧格式（两种,由区块 1 宏 SYS_FRAME_WITH_LEN 选择;收发两端必须一致）:
 *    通用帧（默认）: [0xAA][CMD][长度 n][数据 n 字节][校验][0x55]
 *    简化帧       : [0xAA][数据][校验][0x55]   —— 教材式四字节帧
 *    校验 = 帧内除"校验位与帧尾"外所有字节的逐字节异或（含帧头）
 *    例: 简化帧发数据 0x0F → 0xAA ^ 0x0F = 0xA5 → 线上 AA 0F A5 55
 *        通用帧发 cmd=0x01、数据 {0x0F} → 0xAA^0x01^0x01^0x0F = 0xA4
 *        → 线上 AA 01 01 0F A4 55
 *
 *  使用方式 :
 *    发 : SYS_FRAME_Send(SYS_USART_1, 0x01, &mask, 1);   // 通用帧
 *         SYS_FRAME_SendShort(SYS_USART_1, 0x0F);        // 单字节数据帧
 *    收 : 先 SYS_USART_InitRxIT(uart, baud) 开中断收字节,然后主循环——
 *         if (SYS_FRAME_Poll(SYS_USART_1) > 0) {
 *             uint8_t cmd, data; uint16_t n;
 *             if (SYS_FRAME_Get(&cmd, &data, &n) == 0) { 处理这一帧 }
 *         }
 *  重要说明 :
 *    ① 接收不要自己再写 USARTx_IRQHandler（库方式 = InitRxIT + Poll 喂状态机）;
 *       非要用自己的 ISR,则把每个收到的字节喂 SYS_FRAME_Feed()——
 *       两种喂法二选一,别同时用（会互相抢字节）;
 *    ② 帧同步内建：帧头不对自动丢到下一个 0xAA;帧尾错/校验错也自动
 *       重新找头——教材里"收到异常数据后还能恢复正常收包"的思路,
 *       在 .c 状态机里实现;
 *    ③ 只保留"最新一帧"：新帧会覆盖没取走的旧帧——处理要跟得上;
 *    ④ 出错累计在 SYS_FRAME_ErrCount（联调时看一眼就知道链路质量）;
 *    ⑤ 数据里别使用 0xAA / 0x55（简化帧无转义字节;通用帧虽带长度,
 *       也建议避开——换头尾宏即可绕开）
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换协议只改这里）
 * ================================================================ */
/* 帧头 / 帧尾（改这里即改协议;收发两端必须一致） */
#define SYS_FRAME_HEAD          0xAAU
#define SYS_FRAME_TAIL          0x55U

/* 帧长模式: 1 = 通用帧（带长度字段,数据 0 ~ MAX 字节）
 *           0 = 简化帧（教材式:固定 1 字节数据,共 4 字节） */
#define SYS_FRAME_WITH_LEN      1

/* 通用帧的数据区最大字节数（= 内部收帧缓冲大小;按协议最长数据改） */
#define SYS_FRAME_MAX_PAYLOAD   64U


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 组帧并发送（阻塞,逐字节发完返回;帧格式按区块 1 宏）
 * 参数 : uart —— 串口编号;cmd —— 命令字;
 *        payload/len —— 数据与字节数（可为 0/0 表示无数据）
 * 简化模式说明 : cmd 当作数据字节,payload/len 被忽略——
 *       线上就是 [AA][cmd][XOR][55]（与教材四字节帧一致）
 * 返回 : 无（len 超上限 / 指针非法时直接不发）
 * 标准库 : 经 sys_usart → USART_GetFlagStatus(TXE) + USART_SendData
 * 示例 : uint8_t mask = 0x0F;
 *        SYS_FRAME_Send(SYS_USART_1, 0x01, &mask, 1);   // 通用帧
 * 扩展提示 : 想换更强校验（求和/CRC16）——把本函数与 Feed 里的
 *            异或行换成 SYS_MODBUS_Crc16(帧内字节),帧尾格式同步改 */
void SYS_FRAME_Send(SysUsartId_t uart, uint8_t cmd, const uint8_t *payload, uint16_t len);

/* 发"单字节数据"帧（等价 SYS_FRAME_Send(uart, data, 0, 0)）
 * 说明 : 简化模式下线上 = AA data XOR 55（教材 LED 控制帧就是这个）;
 *        通用模式下 = 长度 0 的通用帧（5 字节）
 * 示例 : SYS_FRAME_SendShort(SYS_USART_1, 0x0F);   // 教材对照:AA0FA555 */
void SYS_FRAME_SendShort(SysUsartId_t uart, uint8_t data);

/* 协议轮询：把串口收到的字节喂进收帧状态机（非阻塞）
 * 返回 : 本次新收完的帧数（0 = 没变化;1 = 收到一帧,可以 Get 了）
 * 前提 : 该串口已 SYS_USART_InitRxIT（字节靠中断进环形缓冲）
 * 示例 : if (SYS_FRAME_Poll(SYS_USART_1) > 0) { ... } */
uint8_t SYS_FRAME_Poll(SysUsartId_t uart);

/* 有没有"已收到但还没取走"的帧：1 = 有 */
uint8_t SYS_FRAME_Available(void);

/* 取走一帧（拷贝到你的缓冲;任一输出指针可为 0 跳过）
 * 返回 : 0 = 成功;1 = 当前没有帧可取
 * 说明 : 取走即清标志;库内只保留最新一帧（处理要跟上）;
 *        简化模式:数据字节从 cmd 取,长度恒为 0
 * 示例 : uint8_t cmd, data[16]; uint16_t n;
 *        if (SYS_FRAME_Get(&cmd, data, &n) == 0) { ... } */
uint8_t SYS_FRAME_Get(uint8_t *cmd, uint8_t *payload, uint16_t *len);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 单字节喂状态机（给"你自己的 ISR / 其它数据来源"用）
 * 返回 : 1 = 这一字节刚好凑成一帧（帧已存入就绪槽）;0 = 还在收
 * 说明 : 与 Poll 属两条喂字节的路径——同一路数据只喂一边
 * 示例 : void USART1_IRQHandler(void) { ... SYS_FRAME_Feed(ch); ... } */
uint8_t SYS_FRAME_Feed(uint8_t byte);

/* 复位收帧状态机（丢弃半帧,重新找帧头;上线/复位链路时用） */
void SYS_FRAME_Reset(void);

/* 收帧出错计数（帧长超限/校验错/帧尾错）——联调排查用 */
uint16_t SYS_FRAME_ErrCount(void);

#endif /* __FWLIB_SYS_FRAME_H */
