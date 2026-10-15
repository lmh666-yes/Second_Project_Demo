#ifndef __FWLIB_SYS_FRAME_H
#define __FWLIB_SYS_FRAME_H

#include "stm32f4xx.h"
#include "sys_usart.h"

/* ================================================================
 *  sys_frame.h — 【系统】串口自定义帧协议模块（组帧 / 校验 / 收帧）  头文件
 *  实现见 sys_frame.c
 * ================================================================
 *  线上帧格式（两板必须一致;改区块 1 宏即改协议）:
 *    [0..1] 0xAA 0x55 | [2] CMD | [3] LEN | [4..3+LEN] DATA |
 *    [4+LEN..5+LEN] CRC16-MODBUS 低字节在前 | [6+LEN..7+LEN] 0x55 0xAA |
 *    整帧长度 = OVERHEAD + LEN = 8 + LEN 字节。
 *  CRC 范围 = 帧内 [2] 起 (2+LEN) 字节（CMD + LEN + DATA）,不含帧头、CRC 与
 *  帧尾;算法 CRC16-MODBUS,初值 0xFFFF,多项式 0x8005,反射 0xA001,结果不异或。
 *  帧合法判据:(1) 帧头连续两字节 0xAA 0x55;(2) LEN + 8 == 实际字节数;
 *  (3) 从 CMD 起重算 CRC16 == 帧内 CRC。Poll/Feed 收帧时 (1)(3) 由状态机
 *  保证,Verify 校验整帧时三项全查。帧尾与下一帧帧头相接或数据段本身含
 *  AA 55 时都会出现裸 0xAA55,不可据此开帧。
 *  用法:发用 SYS_FRAME_Send / SendShort;收先 SYS_USART_InitRxIT(uart, baud)
 *  再轮询 Poll(uart) 后 Get。
 *  约束:InitRxIT + Poll 与自写 USARTx_IRQHandler 中逐字节调用
 *  SYS_FRAME_Feed 二选一,不可同时用;帧头不齐、长度超限、CRC 错、帧尾错
 *  自动丢弃重同步;每路只留最新一帧;出错计数见 SYS_FRAME_ErrCount;
 *  数据段允许 0xAA / 0x55,定界靠 LEN + CRC。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换协议只改这里）
 * ================================================================ */
/* 帧头 / 帧尾（各两字节;改这里即改协议,收发两端必须一致）
 * 帧尾为帧头反序:帧头 0xAA55,帧尾 0x55AA;两帧相接时线上为 55 AA AA 55。 */
#define SYS_FRAME_HEAD_HI       0xAAU   /* 帧头第 1 字节（线上先发） */
#define SYS_FRAME_HEAD_LO       0x55U   /* 帧头第 2 字节 */
#define SYS_FRAME_TAIL_HI       0x55U   /* 帧尾第 1 字节（线上先发） */
#define SYS_FRAME_TAIL_LO       0xAAU   /* 帧尾第 2 字节 */

/* 帧固定开销 = 2 字节帧头 + CMD + LEN + 2 字节 CRC + 2 字节帧尾 = 8 字节
 * 整帧长度 = SYS_FRAME_OVERHEAD + 数据字节数 */
#define SYS_FRAME_OVERHEAD      8U

/* 数据段最大字节数（= 内部收帧缓冲大小;受 1 字节 LEN 限制,上限 255）
 * 环境数据帧 LEN = 12,取 64;调大只增加 RAM 占用（每路串口两份缓冲） */
#define SYS_FRAME_MAX_PAYLOAD   64U

/* ---- 整帧校验（SYS_FRAME_Verify）返回码 ----
 * SYS_FRAME_ERR_CHECK 为旧名别名,值同 SYS_FRAME_ERR_CRC（4）。 */
#define SYS_FRAME_OK         0U   /* 校验通过 */
#define SYS_FRAME_ERR_HEAD   1U   /* 帧头不对（不是 0xAA 0x55） */
#define SYS_FRAME_ERR_TAIL   2U   /* 帧尾不对（不是 0x55 0xAA） */
#define SYS_FRAME_ERR_LEN    3U   /* 长度字段与实际帧长不符（典型:两帧粘接） */
#define SYS_FRAME_ERR_CRC    4U   /* CRC16 校验错（旧名 SYS_FRAME_ERR_CHECK） */
#define SYS_FRAME_ERR_PARAM  5U   /* 参数非法（指针空/长度不够） */
#define SYS_FRAME_ERR_CHECK  SYS_FRAME_ERR_CRC   /* 旧名别名:异或校验时代的名字 */

/* ---- 环境数据帧（CMD = 0x01,LEN = 0x0C）字段表 ----
 * 板1 周期上报,板2 解析后转发/上传。多字节字段大端（高字节在前）,与 CRC 相反。
 *   帧内[4] TEMP int16 ℃×100,负数补码（-5.00℃ = 0xFE0C）
 *   帧内[6] HUMI int16 %RH×100
 *   帧内[8] PRESS int16 hPa×10
 *   帧内[10] LIGHT uint16 lx 原始值
 *   帧内[12] TVOC uint16 ppb
 *   帧内[14] MQ135 uint16 原始 ADC
 *   帧内[16..17] CRC16 uint16 低字节在前,范围 = 帧内 [2..15]
 *  帧内偏移 = 4 + 数据段偏移;PRESS ×10 来源:uint16 上限 65535,
 *   1013.25 hPa × 100 = 101325 溢出,×10 = 10132。HUMI 读失败时送负值作无效标记。 */
#define SYS_FRAME_CMD_ENV       0x01U   /* 命令字:环境数据上报（板1 → 板2） */
#define SYS_FRAME_ENV_LEN       0x0CU   /* 环境帧数据段长度 12 字节 */

/* 环境帧字段在“数据段”内的偏移（不含帧头/CMD/LEN,从 0 数起） */
#define SYS_FRAME_ENV_TEMP_OFF   0U
#define SYS_FRAME_ENV_HUMI_OFF   2U
#define SYS_FRAME_ENV_PRESS_OFF  4U
#define SYS_FRAME_ENV_LIGHT_OFF  6U
#define SYS_FRAME_ENV_TVOC_OFF   8U
#define SYS_FRAME_ENV_MQ135_OFF 10U


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 组帧并发送（阻塞,逐字节发完才返回）
 * 参数 : uart — 串口编号;cmd — 命令字;payload/len — 数据与字节数（0/0 为空）
 * 返回 : 无;len 超上限或指针非法时不发送
 * 标准库 : 经 sys_usart → USART_GetFlagStatus(TXE) + USART_SendData;
 * 非阻塞发送改用 SYS_FRAME_Build */
void SYS_FRAME_Send(SysUsartId_t uart, uint8_t cmd, const uint8_t *payload, uint16_t len);

/* 发数据段为空的短帧:只有命令字,整帧 8 字节,等价 LEN = 0 的 SYS_FRAME_Send
 * 示例 : SYS_FRAME_SendShort(SYS_USART_1, 0x0F);   // 心跳/点名单命令 */
void SYS_FRAME_SendShort(SysUsartId_t uart, uint8_t data);

/* 协议轮询：把串口收到的字节喂进收帧状态机（非阻塞）
 * 返回 : 本次新收完的帧数（0 = 无变化;1 = 收到一帧,可以 Get）
 * 前提 : 该串口已 SYS_USART_InitRxIT（字节靠中断进环形缓冲）
 * 示例 : if (SYS_FRAME_Poll(SYS_USART_1) > 0) { ... } */
uint8_t SYS_FRAME_Poll(SysUsartId_t uart);

/* 以下收帧 API 均带 uart 形参:每路串口一套独立状态机,互不干扰。
 * 按 uart 各存一份收帧状态;板1 = 调试口 + LoRa,板2 = 四路。 */

/* 某路串口有没有"已收到但还没取走"的帧：1 = 有 */
uint8_t SYS_FRAME_Available(SysUsartId_t uart);

/* 取走某路串口的一帧（拷贝到调用方缓冲;任一输出指针可为 0 跳过）
 * 参数 : cap — payload 缓冲容量（字节数）,必填;cap < 帧长时不拷贝
 * 返回 : 0 = 成功;1 = 当前无帧可取;2 = 缓冲不够（帧保留不取走）
 * 说明 : 取走即清标志;每路只保留最新一帧;payload 为数据段（不含
 *        CMD/LEN/CRC）,长度为帧内 LEN
 * 示例 : if (SYS_FRAME_Get(SYS_USART_3, &cmd, data, sizeof(data), &n) == 0) { ... } */
uint8_t SYS_FRAME_Get(SysUsartId_t uart, uint8_t *cmd, uint8_t *payload,
                      uint16_t cap, uint16_t *len);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 单字节喂某路串口的状态机（自写 ISR / 其它数据来源使用）
 * 返回 : 1 = 该字节凑成一帧（帧存入该串口就绪槽）;0 = 仍在收
 * 说明 : 与 Poll 属两条喂字节路径,同一路只喂一边
 * 示例 : void USART1_IRQHandler(void) { ... SYS_FRAME_Feed(SYS_USART_1, ch); ... } */
uint8_t SYS_FRAME_Feed(SysUsartId_t uart, uint8_t byte);

/* 复位某路串口的收帧状态机（丢弃半帧,重新找帧头;上线/复位链路时用） */
void SYS_FRAME_Reset(SysUsartId_t uart);

/* 某路串口的收帧出错计数（长度超限 / CRC 错 / 帧尾错）,联调排查用
 * 帧头低字节对不上属正常重新同步,不计入 */
uint16_t SYS_FRAME_ErrCount(SysUsartId_t uart);

/* ---- 旧签名兼容层（只作用于 SYS_FRAME_DEFAULT_UART,默认 0 = SYS_USART_1）----
 * 无 uart 形参的旧调用改用下面这组 *Legacy 名字:
 *   SYS_FRAME_AvailableLegacy() / FeedLegacy(b) /
 *   GetLegacy(&cmd, buf, cap, &len) / ResetLegacy() / ErrCountLegacy() */
uint8_t  SYS_FRAME_AvailableLegacy(void);
uint8_t  SYS_FRAME_FeedLegacy(uint8_t byte);
uint8_t  SYS_FRAME_GetLegacy(uint8_t *cmd, uint8_t *payload, uint16_t cap, uint16_t *len);
void     SYS_FRAME_ResetLegacy(void);
uint16_t SYS_FRAME_ErrCountLegacy(void);

/* ---- 数据帧的定义与检查（防粘包双保险）---- */

/* 组帧到调用方缓冲区（不发送）:先组后发 / 入队 / 走其它通道
 * 返回 : 帧总字节数（= len + SYS_FRAME_OVERHEAD,> 0）;
 *        0 = 参数非法（cap 不够 / 指针空 / 长度超限）
 * 用途 : 组帧入口;与 SYS_FRAME_Verify 对偶
 * 示例 : n = SYS_FRAME_Build(SYS_FRAME_CMD_ENV, &mask, 1, fbuf, sizeof(fbuf)); */
uint16_t SYS_FRAME_Build(uint8_t cmd, const uint8_t *payload, uint16_t len,
                         uint8_t *out, uint16_t cap);

/* 校验"一整帧"（缓冲区里已是完整帧:自己的 ISR 收、DMA 收、上位机联调）
 * 返回 : SYS_FRAME_OK(0) 合法;1 帧头 / 2 帧尾 / 3 长度不符 / 4 CRC 错 / 5 参数
 * 用途 : 数据帧检查,先 Verify 再剥数据。判据:帧头 0xAA55、LEN 与实际长度
 *        自洽、CRC16 相等。两帧粘接报 ERR_LEN,比特错 / 丢字节报 ERR_CRC;
 *        接缝处 ...55 AA AA 55... 会出现裸 0xAA55,不可只查帧头两字节。
 * 示例 : if (SYS_FRAME_Verify(buf, n) == SYS_FRAME_OK) { 拆 cmd/data; } */
uint8_t SYS_FRAME_Verify(const uint8_t *buf, uint16_t len);

/* ---- 环境数据帧（CMD = 0x01）的组帧 / 拆包助手（可选）----
 * 字段表与换算见区块 1;此处固化 12 字节大端拼装 / 拆解。参数后缀 _x100 /
 *   _x10 为定点整数:线上不传 float（格式与字节序随编译器变）,由调用方先
 *   换算,如 25.60℃ → 2560、60.30 %RH → 6030、1013.2 hPa → 10132。 */

/* 组环境帧（拼 12 字节数据段后套帧）
 * 返回 : 帧总字节数（= SYS_FRAME_ENV_LEN + SYS_FRAME_OVERHEAD = 20）;
 *        0 = 参数非法（out 空 / cap 小于 20）
 * 示例 : n = SYS_FRAME_BuildEnv(f, sizeof(f), 2560, 6030, 10132, 320, 15, 812); */
uint16_t SYS_FRAME_BuildEnv(uint8_t *out, uint16_t cap,
                            int16_t temp_x100, int16_t humi_x100, int16_t press_x10,
                            uint16_t light, uint16_t tvoc, uint16_t mq135);

/* 拆环境帧的数据段（喂 SYS_FRAME_Get 取到的 payload,不是整帧）
 * 参数 : payload/len — 数据段及其长度（len >= 12 才有效）;
 *        输出指针任一给 0 即跳过该项
 * 返回 : SYS_FRAME_OK(0) 成功;SYS_FRAME_ERR_PARAM(5) 参数错（空指针/长度不足）
 * 示例 : SYS_FRAME_UnpackEnv(data, n, &t, &h, &p, &lx, &tv, &mq);
 *        // t/100.0f = ℃, p/10.0f = hPa */
uint8_t SYS_FRAME_UnpackEnv(const uint8_t *payload, uint16_t len,
                            int16_t *temp_x100, int16_t *humi_x100, int16_t *press_x10,
                            uint16_t *light, uint16_t *tvoc, uint16_t *mq135);

#endif /* __FWLIB_SYS_FRAME_H */
