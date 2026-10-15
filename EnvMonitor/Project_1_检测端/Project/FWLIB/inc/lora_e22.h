#ifndef __LORA_E22_H
#define __LORA_E22_H

/* ================================================================
 *  lora_e22.h  亿佰特 E22-400T22S LoRa 驱动头文件
 *  型号 : E22-400T22S（433MHz，22dBm，UART 接口）
 *  链路 : MCU 与模块之间一路 UART，透传模式原样转发，无包格式，
 *         空中协议由应用层定。本项目定案 20 字节帧
 *         0xAA55 + 14 + CRC16-MODBUS + 55AA，见《检测数据端设计.md》§5。
 *
 *  接线约束（模块 TXD→MCU RX，模块 RXD←MCU TX）:
 *    TXD  板1 PB11 (USART3_RX)   板2 PC6 (USART6_TX)
 *    RXD  板1 PB10 (USART3_TX)   板2 PC7 (USART6_RX)
 *    M0   PB0 / PE5   推挽输出      M1   PB1 / PE6   推挽输出
 *    AUX  PB2 / PE7   输入上拉      VCC  3.3V (就近去耦)   GND  共地
 *    板1 用原 ESP8266 座（USART3）；板2 的 USART1/2/3 已分别被
 *    CH340C、P5 跳线(SP3485/SP3232)、P10 跳线(ESP8266) 占用。
 *    须先接天线再上电（天线缺失时发射功率反射回功放会损坏模块）。
 *    M0/M1 不得悬空（芯片内部无上拉），驱动将两脚推挽输出到 00。
 *    发射瞬时电流 100~120mA，就近去耦 100µF 电解并 100nF 陶瓷。
 *    AUX 接上时表示模块空闲与初始化完成；缺省时驱动退化为固定延时
 *    等待（.c: lora_wait_idle），连续收发可能丢字节。
 *
 *  工作模式 M1M0 : 00 透传（串口收到什么发什么）
 *                 01 唤醒 WOR（发送前加前导码）
 *                 10 配置（AT 指令，本端串口 115200 8N1）
 *                 11 深睡（最低功耗，参数保持）
 *  切模式延时 : 透传/唤醒 → 配置/休眠须等当前包发完（典型 <5ms）；
 *    配置/休眠 → 透传须重新初始化射频（典型 <15ms，最坏 1s）。
 *    驱动以 AUX 等待为准，缺省 AUX 时固定 2ms 兜底。
 *  未接的脚把 PORT/PIN 两行宏一起注释掉。缺省 AUX 时不再等 AUX，
 *    改用固定 2ms 延时；缺省 M0/M1 时模块须由硬件拨到透传 00，否则
 *    SendAT/ReadConfig/SetBaud 全不可用。不可改成 0 : SPL 的 GPIOB、
 *    GPIO_Pin_0 是带强制类型转换的表达式，进 `#if` 算术表达式会报
 *    #29/#59/#18；驱动用 `#if defined(宏名)` 只判断有无定义。
 *
 *  AT 指令（配置模式，均以 \r\n 结尾）:
 *    AT / AT+VER / AT+RESET；AT+UART / AT+CFG / AT+WOR
 *    AT+ADDR=?  查询本机地址（0~65535）;  AT+ADDR=1234 设置
 *    AT+NETID=18  网络 ID（NETID+信道+地址 相同才互通）
 *    AT+REG0=18,0,0,433000000,22,1,1,0,0,1,0,0
 *      字段顺序 : 网络ID,本机地址,信道,频率,发射功率,带宽,扩频因子,编码率,
 *                 前导码,是否固定传输,唤醒时间,保留
 *    AT+UART=115200,8,1,NON  设置串口（LORA_E22_SetBaud 用此条）
 *    参数改完须 AT+RESET 或断电重启才在透传模式下生效；部分固件
 *    AT+UART 立即生效并切到新波特率，LORA_E22_SetBaud 已同步本端。
 *
 *  故障对照 : Init 返回 ERR_AUX 多为 AUX 常低（未上电/M0M1 接错/未共地）；
 *    SendAT 常 ERR_NO_MODULE 为未接 M0M1、本端波特率不符或 TX RX 接反；
 *    ERR_AT_ERROR 为指令有误（参数逗号、结尾 \r\n）；收不到对端为两端
 *    地址/网络ID/信道 不同；近距离丢包为未接天线或去耦不足。
 * ================================================================ */

#include "sys_usart.h"
#include "stm32f4xx.h"


/* ================================================================
 *                    1. 用户配置区（换板/换线只改这里）
 * ================================================================ */
/* -------------------- 串口与波特率 -------------------- */
/* 本板（板1 检测端）= USART3（原 ESP8266 座 PB10/PB11）。
 * 板2（数据通信端）为 SYS_USART_6（PC6/PC7）；两份 .h 仅此宏与
 * 下面 M0/M1/AUX 脚位不同，lora_e22.c 两板相同。
 * 该路须存在于 SysUsartId_t（板1/板2 的 sys_usart 均扩到 6 路，
 * 见 sys_usart.h）。 */
#define SYS_LORA_UART            SYS_USART_3

/* 模块出厂默认 9600 8N1，须与本端一致。
 * 此处只改本端，模块侧波特率由 LORA_E22_SetBaud() 修改。 */
#define SYS_LORA_BAUD            9600U


/* -------------------- 控制引脚 -------------------- */
/*  M0/M1 为模块输入，MCU 侧推挽输出。
 * 下列为板1 脚位；板2 用 PE5/PE6/PE7。
 * 换板或换接线只改这三个宏。未接的脚把 PORT/PIN 两行一起注释掉。 */
#define SYS_LORA_M0_PORT          GPIOB
#define SYS_LORA_M0_PIN           GPIO_Pin_0
#define SYS_LORA_M1_PORT          GPIOB
#define SYS_LORA_M1_PIN           GPIO_Pin_1
#define SYS_LORA_AUX_PORT         GPIOB
#define SYS_LORA_AUX_PIN          GPIO_Pin_2


/* -------------------- 超时与帧边界 -------------------- */
/* AUX 等待上限：切模式/发完一包最多等这么久 */
#define SYS_LORA_AUX_TIMEOUT_MS   1000U

/* 一条 AT 指令的回复等待上限 */
#define SYS_LORA_AT_TIMEOUT_MS    500U

/* 帧边界判据 : E22 透传模式无包起止标记，按字节流停顿时长切帧。
 * 本项目一帧 20 字节，9600bps 连续发约 21ms，帧内字节间隔 ≈0.2ms
 * （1 字节约 1.04ms），帧间 ≈1s（采集周期）。
 * 取 10ms : 容忍发送方任务被打断数毫秒，且远小于帧间隔。
 * SYS_LORA_BAUD 降到 1200 时 1 字节约 8.3ms，此值须 ≥30ms。 */
#define SYS_LORA_FRAME_GAP_MS     10U

/* 收帧缓冲（.c 内部兜底缓冲，调用方给了缓冲时不用） */
#define SYS_LORA_RX_BUF_SIZE      256U


/* ================================================================
 *                    2. 状态码
 * ================================================================ */
/* 返回码 : 0 = 成功；非 0 均为失败。
 * "暂无完整帧"单列一码，不与超时混用。 */
#define LORA_E22_OK               0U   /* 成功 */
#define LORA_E22_ERR_AUX          1U   /* AUX 一直忙：模块没上电/没接好/M0M1 错 */
#define LORA_E22_ERR_NO_MODULE    2U   /* AT 无回应：没接 M0M1 / 波特率不符 / TXRX 反 */
#define LORA_E22_ERR_AT_ERROR     3U   /* 模块回了 ERROR：指令本身有问题 */
#define LORA_E22_ERR_PARAM        4U   /* 入参非法（空指针/零长度/不支持的波特率） */
#define LORA_E22_ERR_OVERFLOW     5U   /* 调用方缓冲装不下，本帧已丢弃 */
#define LORA_E22_ERR_NO_DATA      6U   /* 暂无完整帧，非错误，下轮再取 */


/* ================================================================
 *                    3. 工作模式
 * ================================================================ */
typedef enum {
    LORA_E22_MODE_TRANSPARENT = 0,   /* M1M0=00 透传 */
    LORA_E22_MODE_WOR         = 1,   /* M1M0=01 唤醒模式 */
    LORA_E22_MODE_CONFIG      = 2,   /* M1M0=10 配置模式（AT 指令） */
    LORA_E22_MODE_SLEEP       = 3    /* M1M0=11 深睡模式 */
} LoraE22Mode_t;


/* ================================================================
 *                    4. 接口
 * ================================================================ */

/* 初始化 : 配 M0/M1/AUX 引脚 → 进透传 → 开中断接收 → 等模块就绪
 * 返回 : LORA_E22_OK / LORA_E22_ERR_AUX
 * 内部调用 SYS_USART_InitRxIT()，不要再对该串口做别的 DMA 配置；
 * 重复调用安全（重开会清缓冲）。 */
uint8_t LORA_E22_Init(void);

/* 切换工作模式（内部等 AUX 空闲）
 * 返回 : LORA_E22_OK / LORA_E22_ERR_PARAM / LORA_E22_ERR_AUX
 * 业务收发前须处于 LORA_E22_MODE_TRANSPARENT。 */
uint8_t LORA_E22_SetMode(LoraE22Mode_t mode);

/* 模块忙不忙（读 AUX；未接 AUX 时恒返回 0）
 * 返回 : 1 = 忙（不能灌数据）/ 0 = 空闲 */
uint8_t LORA_E22_IsBusy(void);

/* 等模块空闲
 * 返回 : 1 = 已空闲（未接 AUX 时用固定延时兜底）/ 0 = 超时仍忙 */
uint8_t LORA_E22_WaitReady(uint32_t timeout_ms);

/* 发一包（透传模式）。内部先等 AUX 空闲，再阻塞发完
 * 返回 : LORA_E22_OK / LORA_E22_ERR_PARAM
 * 驱动无锁，不可在多任务里并发调用；须固定由 CommTask 调用。 */
uint8_t LORA_E22_Send(const uint8_t *data, uint16_t len);

/* 收一帧（透传模式，按字节间间隔切帧）
 * 参数 : buf/max = 调用方缓冲；out_len = 出参，收到字节数
 * 返回 : LORA_E22_OK         收到完整一帧，*out_len 有效
 *        LORA_E22_ERR_NO_DATA 暂无完整帧，下轮再取
 *        LORA_E22_ERR_OVERFLOW 本帧长于 max，已丢弃（状态自动复位）
 *        LORA_E22_ERR_PARAM   入参非法
 * 须周期性调用（10~50ms 一次），间隔过大会把两帧粘成一帧。
 * 缓冲须 ≥ 32 字节（20 字节帧 + 余量）。 */
uint8_t LORA_E22_Recv(uint8_t *buf, uint16_t max, uint16_t *out_len);

/* 清接收状态（丢弃半截帧）。切模式、改波特率之后调用一次 */
void LORA_E22_Flush(void);

/* 发一条 AT 指令（切配置模式 → 发 → 等 OK/ERROR → 切回透传）
 * 参数 : cmd = 不含结尾 \r\n 的指令串，如 "AT+ADDR=1234"
 * 返回 : LORA_E22_OK / LORA_E22_ERR_NO_MODULE / LORA_E22_ERR_AT_ERROR
 *        / LORA_E22_ERR_PARAM / LORA_E22_ERR_AUX
 * 执行期间模块不能收发业务数据（模式被切走），只在开机配置或人为
 * 改参数时调用，不入数据通路。 */
uint8_t LORA_E22_SendAT(const char *cmd);

/* 把模块当前配置读成一行文本（AT+VER / AT+ADDR / AT+NETID / AT+REG0）
 * 参数 : out/out_size = 输出文本缓冲（≥ 96 字节）
 * 返回 : LORA_E22_OK / LORA_E22_ERR_NO_MODULE / LORA_E22_ERR_PARAM / LORA_E22_ERR_AUX
 * 用途 : 开机打印一次，排障时核对 地址/网络ID/信道。 */
uint8_t LORA_E22_ReadConfig(char *out, uint16_t out_size);

/* 改模块串口波特率（AT+UART=<baud>,8,1,NON），并把本端跟着切过去
 * 返回 : LORA_E22_OK / LORA_E22_ERR_PARAM（只支持 1200/2400/4800/9600/19200
 *        /38400/57600/115200）/ LORA_E22_ERR_NO_MODULE / LORA_E22_ERR_AT_ERROR
 * 改完须把 SYS_LORA_BAUD 改成同一值，否则下次上电不一致。 */
uint8_t LORA_E22_SetBaud(uint32_t baud);

/* 复位模块（AT+RESET）并等它重新就绪
 * 返回 : LORA_E22_OK / LORA_E22_ERR_NO_MODULE / LORA_E22_ERR_AUX */
uint8_t LORA_E22_Reset(void);

#endif /* __LORA_E22_H */
