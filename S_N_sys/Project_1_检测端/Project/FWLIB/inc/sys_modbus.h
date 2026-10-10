#ifndef __FWLIB_SYS_MODBUS_H
#define __FWLIB_SYS_MODBUS_H

#include "stm32f4xx.h"
#include "sys_usart.h"

/* sys_modbus.h — Modbus-RTU 从机协议模块头文件;详细设计见 检测数据端设计.md
 * 收帧、CRC 校验、功能码分发、应答组装在本模块内;应用提供寄存器数组,主循环调用 SYS_MODBUS_Poll()
 * 功能码 0x03 读保持寄存器 / 0x04 读输入寄存器 / 0x06 写单个保持寄存器 / 0x10 写多个保持寄存器
 * 不支持地址 0 广播与 0x01/0x05/0x0F 线圈类功能码;异常应答 = 功能码 | 0x80 + 异常码 01/02/03 */

/* 帧缓冲大小(字节):最长帧 = 1+1+1+250+2 = 255,留 1 字节余量 */
#define SYS_MODBUS_BUF_SIZE 256


/* 计算 Modbus-RTU CRC16(多项式 0xA001,初值 0xFFFF),用于收帧校验、应答组装与主机侧请求帧
 * 参数 : buf 帧首地址;len 参与计算的字节数
 * 返回 : 16 位校验值;发送顺序为低字节在前、高字节在后
 * 依据 : 含 CRC 的完整帧再算一遍,结果为 0 即校验通过 */
uint16_t SYS_MODBUS_Crc16(const uint8_t *buf, uint16_t len);

/* 初始化: 绑定串口、从机地址、波特率、两个寄存器数组
 * 参数 : uart 串口编号,须已 SYS_USART_InitRxIT,且该串口未用于 DMA 接收(RecvDMA);
 *        addr 从机地址,协议范围 1~247(248~255 保留);传 0 或 uart 越界时直接返回,不做初始化;
 *        baud 波特率,仅用于计算帧间隔,须与主机一致,传 0 按 9600 处理;
 *        hold/hold_n 保持寄存器数组,0x03/0x06/0x10 的目标,可传 0/0 不用;
 *        input/input_n 输入寄存器数组,0x04 的目标,可传 0/0 不用
 * 范围 : 0x03/0x04 单次读 1~125 个寄存器,0x10 单次写 1~123 个,越界回异常码 02;
 * 地址 : 寄存器地址 = 数组下标,40001 ↔ hold[0]、30001 ↔ input[0];数组读改写集中在同一任务
 * 生命周期 : 两个数组须在运行期持续有效(静态或全局),本模块只保存指针 */
void SYS_MODBUS_Init(SysUsartId_t uart, uint8_t addr, uint32_t baud,
                     uint16_t *hold, uint16_t hold_n,
                     uint16_t *input, uint16_t input_n);

/* 协议轮询(非阻塞): 搬运串口字节,帧间隔到点后处理并应答
 * 返回 : 本次处理的帧数,0 = 无帧,1 = 处理一帧
 * 调用 : 主循环反复调用,单次开销小;须先调用 SYS_MODBUS_Init,未初始化时直接返回 0
 * 时序 : 帧间隔 3.5 字符时间 = 38500000 / 波特率 us(每字符 11 位),
 *        波特率 > 19200 时固定 1750us(依 Modbus over Serial Line V1.02 的 RTU 帧间隔规定)
 * 缓冲 : 单帧超过 SYS_MODBUS_BUF_SIZE 时整帧丢弃,并计入出错帧数 */
uint8_t SYS_MODBUS_Poll(void);


/* 应答发送函数原型,用于 RS485 等自定义发送路径 */
typedef void (*SYS_MODBUS_TxFn_t)(const uint8_t *buf, uint16_t len);

/* 注册应答发送钩子,RS485 场景用于接管收发方向控制
 * 参数 : fn 发送函数指针;注册后所有应答走该函数,不注册则用 SYS_USART_SendBuf
 * 接线 : 方向切换由钩子内的发送函数完成,如包装 SYS_RS485_Send
 * 顺序 : 须在 SYS_MODBUS_Init 之后调用,Init 会把钩子清 0 */
void SYS_MODBUS_SetTxHook(SYS_MODBUS_TxFn_t fn);

/* 读统计: 成功处理帧数 / 出错帧数(CRC 错、参数非法等)
 * 参数 : ok / err 为输出指针,统计值写回调用方;不需要的项可传 NULL
 * 范围 : 计数为 16 位,回绕后从 0 继续 */
void SYS_MODBUS_Counters(uint16_t *ok, uint16_t *err);

#endif /* __FWLIB_SYS_MODBUS_H */
