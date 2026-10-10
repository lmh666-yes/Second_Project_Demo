#ifndef __FWLIB_MODBUS_H
#define __FWLIB_MODBUS_H

#include "stm32f4xx.h"
#include "sys_rs485.h"

/* modbus.h: Modbus-RTU 从机头文件，依赖 sys_rs485.h（RS485 物理层 + 中断接收缓冲）
 *
 * 帧格式: 从机地址(1) + 功能码(1) + 数据(N) + CRC16(2，低字节在前)
 * 帧结束判断: 帧间静默 ≥ 3.5 个字符时间，不需要额外定时器，主循环轮询 MODBUS_Poll()
 * 广播帧（地址 0）从机执行但不回；地址不匹配直接丢弃
 * 异常响应: 功能码 | 0x80 + 异常码（01 非法功能 / 02 非法地址 / 03 非法数据）
 * CRC16: 多项式 0xA001（反射），初值 0xFFFF，校验范围 = 地址~数据末尾
 *
 * 功能码: 01 读线圈  02 读离散输入  03 读保持寄存器  04 读输入寄存器
 *         05 写单个线圈  06 写单个保持寄存器  0F 写多个线圈  10 写多个保持寄存器
 * 本板接线: RS485 = USART2(PA2/PA3) + 方向控制 PG8（详见 sys_rs485.h） */


/* 定义与宏定义区（换板子只改这里） */
/* 从机地址（1 ~ 247；0 = 广播，不能做从机地址） */
#define MODBUS_DEFAULT_ADDR     0x01
/* 默认波特率（Modbus-RTU 常规：9600 / 19200；8 数据位、无校验、1 停止位 = 8N1） */
#define MODBUS_DEFAULT_BAUD     9600

/* 收发缓冲区大小：0x03 读 125 个寄存器时响应帧 = 1+1+1+250+2 = 255 字节 */
#define MODBUS_BUF_SIZE         256

/* 帧间隔（3.5 字符时间）换算成微秒：一个字符按 11 位算（起始1 + 数据8 + 校验1 + 停止1），
 * 3.5 × 11 = 38.5，gap_us = 38500000 / 波特率（9600 约 4.0ms）
 * baud 参数必须带括号，否则宏展开后运算优先级改变 */
#define MODBUS_GAP_US(baud)     (38500000UL / ((baud)))

/* 单帧最大处理长度（超过直接丢弃，防脏数据撑爆缓冲）
 * 必须 ≥ 协议理论最大帧长 256：0x03 读 125 个保持寄存器响应帧 = 255 字节；
 * 0x10 写 123 个寄存器请求帧 = 7+246+2 = 255 字节；
 * 0x0F 写 1968 位线圈请求帧 = 7+246+2 = 255 字节 */
#define MODBUS_MAX_FRAME        MODBUS_BUF_SIZE

/* 各功能码允许的最大数量（协议规定） */
#define MODBUS_MAX_READ_BITS    2000
#define MODBUS_MAX_READ_REGS    125
#define MODBUS_MAX_WRITE_BITS   1968
#define MODBUS_MAX_WRITE_REGS   123

/* Modbus 异常码 */
#define MODBUS_EXC_ILLEGAL_FUNC     0x01
#define MODBUS_EXC_ILLEGAL_ADDR     0x02
#define MODBUS_EXC_ILLEGAL_VALUE    0x03


/* 基础功能 */
/* 初始化：从机地址 + 波特率（内部调 SYS_RS485_Init 开好 RS485 物理层）
 * slave_addr : 1~247（传 0 = 用 MODBUS_DEFAULT_ADDR）
 * baudrate   : 波特率（传 0 = MODBUS_DEFAULT_BAUD） */
void MODBUS_Init(uint8_t slave_addr, uint32_t baudrate);

/* 绑定"保持寄存器"区（功能码 03 读 / 06 单写 / 10 多写，上位机可读可写）
 * regs 必须是可写数组，本模块会直接改写它 */
void MODBUS_BindHolding(uint16_t *regs, uint16_t count);

/* 绑定"输入寄存器"区（功能码 04，上位机只读；通常放采集值） */
void MODBUS_BindInput(const uint16_t *regs, uint16_t count);

/* 绑定"线圈"区（功能码 01 读 / 05 单写 / 0F 多写）
 * 一个元素代表 1 个位，值取 0/1；数组按每字节一个位存放（非位压缩），
 * 打包成 Modbus 位流由本模块负责 */
void MODBUS_BindCoils(uint8_t *coils, uint16_t count);

/* 绑定"离散输入"区（功能码 02，只读） */
void MODBUS_BindDiscrete(const uint8_t *inputs, uint16_t count);

/* 主循环轮询：收帧 → 校验 → 执行 → 回帧（非阻塞，一次调用最多处理一帧）
 * 帧结束靠总线静默 3.5 字符时间判断，必须经常调用，调用之间不要加长延时 */
void MODBUS_Poll(void);

/* 计算 Modbus CRC16（多项式 0xA001，初值 0xFFFF），自拼帧与自测用
 * 写帧时低字节在前：frame[6] = c & 0xFF，frame[7] = c >> 8 */
uint16_t MODBUS_CRC16(const uint8_t *buf, uint16_t len);


/* 扩展功能 */
/* 上位机写数据后的回调（功能码 05/06/0F/10 生效；在 Poll 上下文执行，不可长阻塞）
 * func : 触发的功能码；addr : 起始地址；value : 写入值（多写时为首个） */
void MODBUS_SetWriteCallback(void (*callback)(uint8_t func, uint16_t addr, uint16_t value));

/* 统计：收帧数 / 回帧数 / 出错帧数 / 他人帧数
 * 出错帧只统计长度不足与 CRC 校验失败的帧；地址不是本机的帧属于总线旁听，记入他人帧数 */
uint32_t MODBUS_GetRxFrames(void);
uint32_t MODBUS_GetTxFrames(void);
uint32_t MODBUS_GetErrFrames(void);
uint32_t MODBUS_GetOtherFrames(void);
void     MODBUS_ResetCounters(void);

/* 从机地址（运行时读取；动态改地址用 MODBUS_SetAddr）
 * 多从机场景常把地址存在 Flash，开机调 MODBUS_SetAddr 写入 */
uint8_t  MODBUS_GetAddr(void);
void     MODBUS_SetAddr(uint8_t addr);

/* 从机主动上报一次：把当前保持寄存器内容按 03 响应格式发出，无上位机轮询时用
 * start : 起始地址；count : 寄存器个数 */
void MODBUS_ReportHolding(uint16_t start, uint16_t count);

#endif /* __FWLIB_MODBUS_H */
