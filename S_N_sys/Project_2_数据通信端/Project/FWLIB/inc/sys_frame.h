#ifndef __FWLIB_SYS_FRAME_H
#define __FWLIB_SYS_FRAME_H

#include "stm32f4xx.h"
#include "sys_usart.h"

/* 串口自定义帧协议（组帧 / 校验 / 收帧）头文件,字节收发走 sys_usart
 *
 * 线上帧格式,收发两端必须一致,改下方的宏即改协议:
 *   [0][1]          0xAA 0x55      帧头,双字节
 *   [2]             CMD            命令字,环境数据帧 = 0x01
 *   [3]             LEN            数据字节数,0 ~ SYS_FRAME_MAX_PAYLOAD
 *   [4 .. 3+LEN]    DATA           LEN 字节数据
 *   [4+LEN][5+LEN]  CRC16-MODBUS   低字节在前
 *   [6+LEN][7+LEN]  0x55 0xAA      帧尾,双字节
 *
 * 整帧长度 = 8 + LEN 字节（SYS_FRAME_OVERHEAD + LEN,LEN = 0 时最小 8 字节）
 *
 * CRC 计算范围 = 从 [2] 起共 (2+LEN) 字节,即 CMD + LEN + DATA,
 * 不含帧头、CRC 自身和帧尾。算法 CRC16-MODBUS:初值 0xFFFF,
 * 多项式 0x8005 反射为 0xA001,结果不再异或。
 *
 * 帧同步必须三重确认,只比对裸 0xAA55 不足以定帧:帧尾 0x55AA 与下一帧
 * 帧头 0xAA55 相连时字节流为 ...55 AA AA 55...,接缝处多出 AA AA,
 * 数据段内也可能出现 AA 55。判据:1) 帧头连续两字节 0xAA 0x55;
 * 2) LEN + 8 == 实际字节数;3) 从 CMD 起重算 CRC16 与帧内 CRC 相等。
 *
 * 收帧方式二选一,不可同时使用:SYS_USART_InitRxIT + SYS_FRAME_Poll,
 * 或自写 USARTx_IRQHandler 后逐字节调用 SYS_FRAME_Feed。
 * 数据段允许出现 0xAA / 0x55;每路串口只保留最新一帧;
 * 多帧粘连与半包由状态机跨调用累积处理,丢字节或误码由长度 + CRC 拦下后
 * 重找帧头,两帧粘成一帧时 Verify 报 ERR_LEN,帧内比特错报 ERR_CRC。 */


/* 帧头 / 帧尾各两字节;改这里即改协议,收发两端必须一致。
 * 帧尾与帧头相反:帧头 0xAA 0x55,帧尾 0x55 0xAA,接缝处形成 55 AA AA 55。 */
#define SYS_FRAME_HEAD_HI       0xAAU   /* 帧头第 1 字节（线上先发） */
#define SYS_FRAME_HEAD_LO       0x55U   /* 帧头第 2 字节 */
#define SYS_FRAME_TAIL_HI       0x55U   /* 帧尾第 1 字节（线上先发） */
#define SYS_FRAME_TAIL_LO       0xAAU   /* 帧尾第 2 字节 */

/* 帧固定开销 = 2 字节帧头 + CMD + LEN + 2 字节 CRC + 2 字节帧尾 = 8 字节
 * 整帧长度 = SYS_FRAME_OVERHEAD + 数据字节数 */
#define SYS_FRAME_OVERHEAD      8U

/* 数据段最大字节数,也是本模块内部收帧缓冲大小;受 1 字节 LEN 限制,上限 255
 * 环境数据帧 LEN = 12,这里取 64;调大只增加 RAM 占用,每路串口两份缓冲 */
#define SYS_FRAME_MAX_PAYLOAD   64U

/* ---- 整帧校验（SYS_FRAME_Verify）返回码 ---- */
#define SYS_FRAME_OK         0U   /* 校验通过 */
#define SYS_FRAME_ERR_HEAD   1U   /* 帧头不对（不是 0xAA 0x55） */
#define SYS_FRAME_ERR_TAIL   2U   /* 帧尾不对（不是 0x55 0xAA） */
#define SYS_FRAME_ERR_LEN    3U   /* 长度字段与实际帧长不符（典型:两帧粘接） */
#define SYS_FRAME_ERR_CRC    4U   /* CRC16 校验错（旧名 SYS_FRAME_ERR_CHECK） */
#define SYS_FRAME_ERR_PARAM  5U   /* 参数非法（指针空/长度不够） */
#define SYS_FRAME_ERR_CHECK  SYS_FRAME_ERR_CRC   /* 旧名别名,值同 CRC 错误码 4 */

/* ---- 环境数据帧（CMD = 0x01,LEN = 0x0C）字段表 ----
 * 板1 上报,板2 解析后转发/上传;数据段 12 字节。
 * 所有多字节字段大端（高字节在前）,与帧内 CRC 的小端相反。
 *
 *  帧内偏移  数据段偏移  字段    类型     单位 / 换算
 *  ------------------------------------------------------------------
 *     4          0      TEMP    int16    温度 ℃ × 100（负数补码,-5.00℃ = 0xFE0C）
 *     6          2      HUMI    int16    湿度 %RH × 100
 *     8          4      PRESS   int16    气压 hPa × 10
 *    10          6      LIGHT   uint16   光照 lx 原始值
 *    12          8      TVOC    uint16   TVOC ppb
 *    14         10      MQ135   uint16   MQ135 原始 ADC
 *   16 / 17      无     CRC16   uint16   低字节在前,范围 = 帧内 [2 .. 15] 共 14 字节
 *
 *  帧内偏移 = 4 + 数据段偏移（4 = 帧头 2 字节 + CMD + LEN）。
 *
 *  气压换算取 ×10 不取 ×100:int16 上限 32767 与 uint16 上限 65535,
 *  1013.25 hPa × 100 = 101325 均溢出,× 10 = 10132 可表示,分辨率 0.1 hPa。
 *  湿度用 int16:湿度非负,但传感器读失败时调用方送负值作无效标记。
 */
#define SYS_FRAME_CMD_ENV       0x01U   /* 命令字:环境数据上报（板1 → 板2） */
#define SYS_FRAME_ENV_LEN       0x0CU   /* 环境帧数据段长度 12 字节 */

/* 环境帧字段在“数据段”内的偏移（不含帧头/CMD/LEN,从 0 数起） */
#define SYS_FRAME_ENV_TEMP_OFF   0U
#define SYS_FRAME_ENV_HUMI_OFF   2U
#define SYS_FRAME_ENV_PRESS_OFF  4U
#define SYS_FRAME_ENV_LIGHT_OFF  6U
#define SYS_FRAME_ENV_TVOC_OFF   8U
#define SYS_FRAME_ENV_MQ135_OFF 10U


/* ---- 控制帧（Qt 上位机 → 板2）----
 * 方向:上位机 → 板2（USART2）,应答方向相反,CMD 最高位置 1。
 * 约定:
 *   1) 请求 CMD 0x10 ~ 0x7F;应答 CMD = 请求 | SYS_FRAME_ACK_FLAG（0x80）。
 *   2) 应答数据段第 1 字节恒为状态码 SYS_FRAME_ST_*,后面才是本命令的数据。
 *   3) 未知 CMD 回 SYS_FRAME_ST_UNSUPPORTED;长度/取值不对回 ST_PARAM。
 *   4) 整帧走同一套 CRC16;上位机超时 600 ms 重发,3 次不回判离线。
 * 串口异步,无应答时无法区分命令已生效与链路未通/板2 未运行。 */
#define SYS_FRAME_ACK_FLAG      0x80U   /* 应答位:应答 CMD = 请求 CMD | 本位 */

/* 请求命令字（上位机 → 板2） */
#define SYS_FRAME_CMD_SET_FWD   0x10U   /* 设转发开关:DATA[0] = 0 关 / 1 开 */
#define SYS_FRAME_CMD_SET_CACHE 0x11U   /* 设断网缓存开关:DATA[0] = 0 / 1 */
#define SYS_FRAME_CMD_QUERY     0x12U   /* 查板2 状态:LEN = 0 */
#define SYS_FRAME_CMD_CLR_CACHE 0x13U   /* 清空 Flash 缓存:LEN = 0 */
#define SYS_FRAME_CMD_REBOOT    0x14U   /* 重启板2:LEN = 0（先回应答再复位） */
/* 历史帧与补传:0x15 是板2 -> 上位机,0x16 是上位机 -> 板2 */
#define SYS_FRAME_CMD_HISTORY   0x15U   /* 历史环境帧（板2 → 上位机）:DATA = 4 字节原始时间戳(大端) + 20 字节原始环境帧,LEN = 24 */
#define SYS_FRAME_CMD_REPLAY    0x16U   /* 补传历史缓存（上位机 → 板2）:DATA[0] = 0 停止 / 1 开始,LEN = 0 视为开始 */

/* HISTORY 的数据段布局（环境帧恒为 LEN = SYS_FRAME_HISTORY_LEN = 24）
 * 时间戳是板2 收到该帧时的时间戳,约定 hh*10000 + mm*100 + ss,原样搬过来;
 * 后面 20 字节是当时存进 Flash 的整帧（含帧头、CRC 与帧尾）,补传不重新组帧 */
#define SYS_FRAME_HISTORY_LEN       24U
#define SYS_FRAME_HISTORY_TS_OFF    0U  /* [0..3]  原始时间戳,大端 */
#define SYS_FRAME_HISTORY_FRAME_OFF 4U  /* [4..23] 原始 20 字节环境帧 */

/* REPLAY 应答（CMD = SYS_FRAME_CMD_REPLAY | SYS_FRAME_ACK_FLAG = 0x96）
 * 数据段固定 4 字节,第 1 字节是本命令自己的补传状态（与 SYS_FRAME_ST_* 数值
 * 重叠但语义不同）:
 *   0 = 空闲（收到停止命令,或本来就没在补传）
 *   1 = 补传中（收到开始命令,应答后由补传任务每 50ms 发一条 0x15）
 *   2 = 已完成（整段发完并清空缓存）
 *   3 = 出错（命令参数非法,或整段发完后清空失败）
 * 补传收尾时板2 主动再发一帧本应答。已补传条数只有低两字节:一轮补传最多
 * 2048 条,够用 */
#define SYS_FRAME_REPLAY_ACK_CMD (SYS_FRAME_CMD_REPLAY | SYS_FRAME_ACK_FLAG)   /* = 0x96 */
#define SYS_FRAME_REPLAY_ACK_LEN  4U
#define SYS_FRAME_RA_ST           0U    /* [0]   补传状态 SYS_FRAME_REPLAY_ST_* */
#define SYS_FRAME_RA_SENT_LO      1U    /* [1]   已补传条数低字节 */
#define SYS_FRAME_RA_SENT_HI      2U    /* [2]   已补传条数高字节 */
#define SYS_FRAME_RA_REMAIN_LO    3U    /* [3]   剩余条数低字节 */

/* 补传状态（REPLAY 应答第 1 个数据字节） */
#define SYS_FRAME_REPLAY_ST_IDLE  0U
#define SYS_FRAME_REPLAY_ST_REPLAYING 1U
#define SYS_FRAME_REPLAY_ST_DONE  2U
#define SYS_FRAME_REPLAY_ST_ERR   3U


/* 应答状态码（应答数据段第 1 字节） */
#define SYS_FRAME_ST_OK          0U     /* 执行成功 */
#define SYS_FRAME_ST_PARAM       1U     /* 参数非法（长度/取值不对） */
#define SYS_FRAME_ST_UNSUPPORTED 2U     /* 不认识的命令字 */
#define SYS_FRAME_ST_BUSY        3U     /* 忙（例如 Flash 正在擦除） */

/* QUERY 应答的数据段布局（LEN = SYS_FRAME_QUERY_ACK_LEN = 16）
 * 多字节字段大端（高字节在前）,与环境帧同一约定 */
#define SYS_FRAME_QUERY_ACK_LEN  16U
#define SYS_FRAME_QA_ST          0U     /* [0]      状态码 */
#define SYS_FRAME_QA_FWD         1U     /* [1]      转发开关 0/1 */
#define SYS_FRAME_QA_CACHE       2U     /* [2]      缓存开关 0/1 */
#define SYS_FRAME_QA_RXOK        4U     /* [4..5]   收帧成功计数 */
#define SYS_FRAME_QA_RXBAD       6U     /* [6..7]   收帧出错计数 */
#define SYS_FRAME_QA_CACHECNT    8U     /* [8..9]   Flash 缓存条数 */
#define SYS_FRAME_QA_LOST       10U     /* [10..11] 缓存溢出丢帧数 */
#define SYS_FRAME_QA_UPTIME     12U     /* [12..15] 开机运行秒数 */

/* SET_FWD / SET_CACHE 应答:DATA[1] = 生效后的新值 */
#define SYS_FRAME_ACK_VAL_OFF    1U
/* CLR_CACHE 应答:DATA[1..2] = 清空后剩余条数（大端,恒 0,给上位机复核） */
#define SYS_FRAME_ACK_CNT_OFF    1U


/* ---- 区块 2：基础功能 ---- */
/* 组帧并发送（阻塞,逐字节发完才返回）
 * 参数 : uart 串口编号;cmd 命令字;payload/len 数据与字节数,0/0 表示数据段为空
 * 返回 : 无;len 超上限或指针非法时不发送,调用方须保证参数合法
 * 线上 : AA 55 cmd len 数据… CRC低 CRC高 55 AA,共 len + 8 字节
 * 实现 : 经 sys_usart 调用 USART_GetFlagStatus(TXE) + USART_SendData;不阻塞组帧见 SYS_FRAME_Build */
void SYS_FRAME_Send(SysUsartId_t uart, uint8_t cmd, const uint8_t *payload, uint16_t len);

/* 发数据段为空的短帧:只有命令字,整帧 8 字节
 * 说明 : 等价 SYS_FRAME_Send(uart, data, 0, 0),线上 AA 55 data 00 CRC2 55 AA
 *        本协议无独立的简化帧,短帧就是 LEN = 0 的通用帧 */
void SYS_FRAME_SendShort(SysUsartId_t uart, uint8_t data);

/* 协议轮询：把串口收到的字节喂进收帧状态机（非阻塞）
 * 返回 : 本次新收完的帧数（0 = 没变化;1 = 收到一帧,可以 Get 了）
 * 前提 : 该串口已调用 SYS_USART_InitRxIT,字节由中断进环形缓冲 */
uint8_t SYS_FRAME_Poll(SysUsartId_t uart);

/* 以下收帧 API 一律带 uart 形参:每路串口一套独立状态机。
 * 文件级共享状态会让多路同时收帧时互相截断,按 uart 各存一份后互不干扰。 */

/* 某路串口有没有已收到但还没取走的帧：1 = 有 */
uint8_t SYS_FRAME_Available(SysUsartId_t uart);

/* 取走某路串口的一帧（拷贝到调用方缓冲;任一输出指针可为 0 跳过）
 * 参数 : cap: payload 缓冲容量（字节数）,必填
 * 返回 : 0 = 成功;1 = 当前没有帧可取;2 = 缓冲不够（cap < 帧长,帧保留不取走）
 * 说明 : 取走即清标志;每路只保留最新一帧;cap 用于防止帧长超出调用方缓冲时越界写;
 *        payload 为数据段（不含 CMD/LEN/CRC）,长度即帧里的 LEN */
uint8_t SYS_FRAME_Get(SysUsartId_t uart, uint8_t *cmd, uint8_t *payload,
                      uint16_t cap, uint16_t *len);


/* ---- 区块 3：扩展功能 ---- */
/* 单字节喂某路串口的状态机（供自写 ISR 或其它数据来源使用）
 * 返回 : 1 = 这一字节刚好凑成一帧（帧已存入该串口的就绪槽）;0 = 还在收
 * 说明 : 与 Poll 是两条喂字节的路径,同一路数据只走一边 */
uint8_t SYS_FRAME_Feed(SysUsartId_t uart, uint8_t byte);

/* 复位某路串口的收帧状态机（丢弃半帧,重新找帧头;上线/复位链路时用） */
void SYS_FRAME_Reset(SysUsartId_t uart);

/* 某路串口的收帧出错计数（长度超限 / CRC 错 / 帧尾错）
 * 说明 : 帧头低字节对不上属正常重新同步,不计入,避免空闲链路的计数
 *        被随机噪声顶满 */
uint16_t SYS_FRAME_ErrCount(SysUsartId_t uart);

/* 旧签名兼容层（只作用于 SYS_FRAME_DEFAULT_UART,默认 0 = SYS_USART_1）
 * 无 uart 形参的旧调用改用这组 *Legacy 名字:
 *   SYS_FRAME_AvailableLegacy() / FeedLegacy(b) /
 *   GetLegacy(&cmd, buf, cap, &len) / ResetLegacy() / ErrCountLegacy() */
uint8_t  SYS_FRAME_AvailableLegacy(void);
uint8_t  SYS_FRAME_FeedLegacy(uint8_t byte);
uint8_t  SYS_FRAME_GetLegacy(uint8_t *cmd, uint8_t *payload, uint16_t cap, uint16_t *len);
void     SYS_FRAME_ResetLegacy(void);
uint16_t SYS_FRAME_ErrCountLegacy(void);

/* ---- 数据帧的定义与检查 ---- */

/* 组帧到调用方缓冲区（不发送）
 * 返回 : 帧总字节数（= len + SYS_FRAME_OVERHEAD,> 0）;
 *        0 = 参数非法（cap 不够 / 指针空 / 长度超限）
 * 说明 : 组帧的统一入口;与 SYS_FRAME_Verify 对偶,组完可自检 */
uint16_t SYS_FRAME_Build(uint8_t cmd, const uint8_t *payload, uint16_t len,
                         uint8_t *out, uint16_t cap);

/* 校验缓冲区里的完整一帧（自写 ISR 收、DMA 收、上位机联调均可用）
 * 返回 : SYS_FRAME_OK(0) 合法;1 帧头 / 2 帧尾 / 3 长度不符 / 4 CRC 错 / 5 参数
 * 说明 : 三重确认:帧头 0xAA55、LEN 与实际长度自洽、CRC16 相等;
 *        两帧粘接报 ERR_LEN,帧内比特错或丢字节报 ERR_CRC,均挡在解析前;
 *        仅比对前两字节 0xAA55 不足以定帧,接缝处 55 AA AA 55 会频繁命中 */
uint8_t SYS_FRAME_Verify(const uint8_t *buf, uint16_t len);

/* ---- 环境数据帧（CMD = 0x01）的组帧 / 拆包助手（可选,不用也照样收发）----
 * 字段表与换算见上文环境数据帧字段表;这里固化了 12 字节大端的拼装/拆解,
 * 免得每个调用点各写一遍移位。
 * 参数带 _x100 / _x10 后缀:线上为定点整数（float 不能跨平台直传,
 * 4 字节格式与字节序随编译器变）,换算由调用方完成:
 * 25.60℃ → 2560、60.30 %RH → 6030、1013.2 hPa → 10132。 */

/* 组环境帧（等价 BuildEnv 拼数据段 + SYS_FRAME_Build 套信封）
 * 返回 : 帧总字节数（= SYS_FRAME_ENV_LEN + SYS_FRAME_OVERHEAD = 20）;
 *        0 = 参数非法（out 为空 / cap 小于 20） */
uint16_t SYS_FRAME_BuildEnv(uint8_t *out, uint16_t cap,
                            int16_t temp_x100, int16_t humi_x100, int16_t press_x10,
                            uint16_t light, uint16_t tvoc, uint16_t mq135);

/* 拆环境帧的数据段（喂 SYS_FRAME_Get 取到的 payload,不是整帧）
 * 参数 : payload/len: 数据段及其长度（len >= 12 才有效）;
 *        后面的输出指针任一给 0 即跳过该项
 * 返回 : SYS_FRAME_OK(0) 成功;SYS_FRAME_ERR_PARAM(5) 参数错（空指针/长度不足）
 * 换算 : temp_x100/100.0f = ℃,press_x10/10.0f = hPa */
uint8_t SYS_FRAME_UnpackEnv(const uint8_t *payload, uint16_t len,
                            int16_t *temp_x100, int16_t *humi_x100, int16_t *press_x10,
                            uint16_t *light, uint16_t *tvoc, uint16_t *mq135);

#endif /* __FWLIB_SYS_FRAME_H */
