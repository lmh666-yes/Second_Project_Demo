#ifndef __FWLIB_MODBUS_H
#define __FWLIB_MODBUS_H

#include "stm32f4xx.h"
#include "sys_rs485.h"

/* ================================================================
 *  modbus.h —— 【协议】Modbus-RTU 从机（Slave）  头文件
 * ================================================================
 *  设计定位 : 工业现场最常见的串行协议，做成"寄存器映射式"从机——
 *             应用层只把变量数组"绑定"进来，收帧/解帧/回帧/CRC 全部由本模块做
 *  依赖     : sys_rs485.h（RS485 物理层 + 中断接收缓冲）
 *  标准库关键词 : 无——纯软件协议；只用到 sys_rs485 的收发与 gpio_core 的 DWT 计时
 *
 *  【协议要点（面试常问）】
 *    · 帧格式：从机地址(1) + 功能码(1) + 数据(N) + CRC16(2，**低字节在前**)
 *    · 帧间隔：帧与帧之间静默 ≥3.5 个字符时间 → 本驱动用它做"帧结束"判断，
 *              因此**不需要额外定时器**，主循环轮询 MODBUS_Poll() 即可
 *    · 广播帧（地址 0）从机执行但**不回**；地址不匹配直接丢弃
 *    · 异常响应：功能码 | 0x80 + 异常码（01 非法功能 / 02 非法地址 / 03 非法数据）
 *    · CRC16：多项式 0xA001（反射），初值 0xFFFF，校验范围 = 地址~数据末尾
 *
 *  【支持的功能码】
 *      01 读线圈(可读写位)      02 读离散输入(只读位)
 *      03 读保持寄存器(可读写)  04 读输入寄存器(只读)
 *      05 写单个线圈            06 写单个保持寄存器
 *      0F 写多个线圈            10 写多个保持寄存器
 *
 *  【本板接线】RS485 = USART2(PA2/PA3) + 方向控制 PG8（详见 sys_rs485.h）
 *
 *  使用方式（从机，5 步）:
 *      static uint16_t hold[10];          // 保持寄存器（上位机可读可写）
 *      static uint16_t inreg[4];          // 输入寄存器（上位机只读，放采集值）
 *
 *      MODBUS_Init(0x01, 9600);           // ① 从机地址 1，9600 8N1
 *      MODBUS_BindHolding(hold, 10);      // ② 绑定寄存器区
 *      MODBUS_BindInput(inreg, 4);
 *      for (;;) {
 *          inreg[0] = SYS_ADC_Read(...);  // ③ 刷新采集数据
 *          MODBUS_Poll();                 // ④ 轮询（收帧→解析→回帧）
 *      }
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 从机地址（1 ~ 247；0 = 广播，不能做从机地址） */
#define MODBUS_DEFAULT_ADDR     0x01
/* 默认波特率（Modbus-RTU 常规：9600 / 19200；8 数据位、无校验、1 停止位 = 8N1） */
#define MODBUS_DEFAULT_BAUD     9600

/* 收发缓冲区大小：0x03 读 125 个寄存器时响应帧 = 1+1+1+250+2 = 255 字节 */
#define MODBUS_BUF_SIZE         256

/* 帧间隔（3.5 字符时间）换算成微秒的系数：
 *   一个字符按 11 位算（起始1 + 数据8 + 校验1 + 停止1），3.5 × 11 = 38.5
 *   gap_us = 38500000 / 波特率  （9600 → 约 4.0ms） */
#define MODBUS_GAP_US(baud)     (38500000UL / (baud))

/* 单帧最大处理长度（超过直接丢弃，防脏数据撑爆缓冲） */
#define MODBUS_MAX_FRAME        200

/* 各功能码允许的最大数量（协议规定） */
#define MODBUS_MAX_READ_BITS    2000
#define MODBUS_MAX_READ_REGS    125
#define MODBUS_MAX_WRITE_BITS   1968
#define MODBUS_MAX_WRITE_REGS   123

/* Modbus 异常码 */
#define MODBUS_EXC_ILLEGAL_FUNC     0x01
#define MODBUS_EXC_ILLEGAL_ADDR     0x02
#define MODBUS_EXC_ILLEGAL_VALUE    0x03


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：从机地址 + 波特率（内部会调 SYS_RS485_Init 开好 RS485 物理层）
 * 参数 : slave_addr —— 1~247（传 0 = 用 MODBUS_DEFAULT_ADDR）
 *        baud       —— 波特率（传 0 = MODBUS_DEFAULT_BAUD）
 * 示例 : MODBUS_Init(0x01, 9600); */
void MODBUS_Init(uint8_t slave_addr, uint32_t baudrate);

/* 绑定"保持寄存器"区（功能码 03 读 / 06 单写 / 10 多写，上位机可读可写）
 * 说明 : 传进来的必须是**可写**数组（本模块会直接改它）
 * 示例 : static uint16_t hold[10];  MODBUS_BindHolding(hold, 10); */
void MODBUS_BindHolding(uint16_t *regs, uint16_t count);

/* 绑定"输入寄存器"区（功能码 04，上位机只读；通常放采集值） */
void MODBUS_BindInput(const uint16_t *regs, uint16_t count);

/* 绑定"线圈"区（功能码 01 读 / 05 单写 / 0F 多写）
 * 说明 : 一个元素代表 1 个位，值取 0/1；数组内容是"每字节一个位"（非位压缩），
 *        这样应用层可读性最好；打包成 Modbus 位流由本模块负责 */
void MODBUS_BindCoils(uint8_t *coils, uint16_t count);

/* 绑定"离散输入"区（功能码 02，只读） */
void MODBUS_BindDiscrete(const uint8_t *inputs, uint16_t count);

/* 主循环轮询：收帧 → 校验 → 执行 → 回帧（非阻塞，一次调用最多处理一帧）
 * 说明 : 帧结束靠"总线静默 3.5 字符时间"判断，所以要**经常调用**
 *        （建议主循环里每次都调，不要在中间加长延时）
 * 示例 : while (1) { MODBUS_Poll(); ...其它任务... } */
void MODBUS_Poll(void);

/* 计算 Modbus CRC16（多项式 0xA001，初值 0xFFFF）
 * 用途 : 自己拼帧/自测用；也方便你拿上位机报文手动验算
 * 示例 : uint16_t c = MODBUS_CRC16(frame, 6);   // frame[6]=c&0xFF, frame[7]=c>>8 */
uint16_t MODBUS_CRC16(const uint8_t *buf, uint16_t len);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 上位机写数据后的回调（功能码 05/06/0F/10 生效；在 Poll 上下文执行，别长阻塞）
 * 参数 : func —— 触发的功能码；addr —— 起始地址；value —— 写入值（多写时为首个）
 * 典型用途 : 上位机改参数后立刻存 Flash / 重算 PID 参数
 * 示例 : static void on_write(uint8_t func, uint16_t addr, uint16_t value)
 *        { SYS_FLASH_SaveParams(...); }
 *        MODBUS_SetWriteCallback(on_write); */
void MODBUS_SetWriteCallback(void (*callback)(uint8_t func, uint16_t addr, uint16_t value));

/* 统计（调试上位机是否真的在发、从机是否在答）
 * 返回 : 累计的 收帧数 / 回帧数 / 出错帧数（CRC 错、地址不符等） */
uint32_t MODBUS_GetRxFrames(void);
uint32_t MODBUS_GetTxFrames(void);
uint32_t MODBUS_GetErrFrames(void);
void     MODBUS_ResetCounters(void);

/* 从机地址（运行时读取；想动态改地址用 MODBUS_SetAddr）
 * ⚠ 多从机场景常见做法：把地址存在 Flash，开机 MODBUS_SetAddr(...) */
uint8_t  MODBUS_GetAddr(void);
void     MODBUS_SetAddr(uint8_t addr);

/* 让从机"主动回报"一次：把当前保持寄存器内容按 03 响应格式发出去
 * 用途 : 无上位机轮询时，从机也能周期性上报（很多"采集盒"就这么做）
 * 示例 : if (SYS_TICK_Timeout(t, 1000)) { t = SYS_TICK_GetTick(); MODBUS_ReportHolding(0, 10); } */
void MODBUS_ReportHolding(uint16_t start, uint16_t count);

#endif /* __FWLIB_MODBUS_H */
