#ifndef __FWLIB_SYS_ENCODER_H
#define __FWLIB_SYS_ENCODER_H

#include "stm32f4xx.h"
#include "sys_tim.h"

/* ================================================================
 *  sys_encoder.h —— 【外接】正交编码器接口（TIM 编码器模式 TIM_EncoderMode_TI12 / _TI1）  头文件
 * ================================================================
 *  设计定位 : 把"增量式正交编码器"接到定时器的 CH1/CH2 上，
 *             由**硬件**自动正/反计数——CPU 完全不用管，读寄存器就行
 *  依赖     : sys_tim.h（SysTimId_t 枚举）、gpio_core.h
 *  标准库关键词 : GPIO_PinAFConfig / GPIO_Init(复用输入) /
 *                 TIM_DeInit / TIM_TimeBaseInit / TIM_EncoderInterfaceConfig /
 *                 TIM_Cmd / TIM_GetCounter / TIM_GetDirection
 *
 *  【原理（面试常问）】
 *      编码器输出 A/B 两路方波，相位差 90°：
 *        正转：A 超前 B 90°；反转：B 超前 A 90°
 *      TIM 的编码器接口同时把 A、B 接到 CH1、CH2，硬件根据"谁先跳变"
 *      自动 加/减 计数，所以：
 *        · 计数值 = 位置（增量）
 *        · 两次读数之差 / 时间 = 转速
 *      TIM_EncoderMode_TI12 = A、B 双边沿都计数（4 倍频，分辨率最高）
 *      本模块默认 TI12（4 倍频）；若你的编码器线数少、想少占计数范围，
 *      可改成 TIM_EncoderMode_TI1（只数 A 的双边沿 = 2 倍频）。
 *
 *  【接线（普中-天马 F407开发板）】
 *      板上没有焊死的编码器，电机/编码器通过排针跳线接入，
 *      所以**端口/引脚/AF 全部由参数传入**（和 sys_tim 的 PWM 一样通用）。
 *      例：编码器 A→PA0(TIM2_CH1, AF1)，B→PA1(TIM2_CH2, AF1)
 *      ⚠ 选定的定时器会被本模块独占，不能再拿去 PWM / 定时中断
 *
 *  使用方式（10ms 读一次速度）:
 *      static uint32_t t;
 *      SYS_ENCODER_Init(SYS_ENCODER_1, SYS_TIM_2,
 *                       GPIOA, GPIO_Pin_0, GPIO_AF_TIM2,
 *                       GPIOA, GPIO_Pin_1, GPIO_AF_TIM2, 0);
 *      for (;;) {
 *          if (SYS_TICK_Timeout(t, 10)) {
 *              t = SYS_TICK_GetTick();
 *              int16_t d = SYS_ENCODER_GetDelta(SYS_ENCODER_1);   // 10ms 内脉冲数
 *              // 转速(rpm) = d / (线数×4) / 0.01 × 60
 *          }
 *      }
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 支持几路编码器（两轮小车 = 2） */
#define SYS_ENCODER_MAX     2

/* 计数模式：TIM_EncoderMode_TI1 / TIM_EncoderMode_TI2 / TIM_EncoderMode_TI12
 * TIM_EncoderMode_TI12 = A/B 双边沿都计数（4 倍频，默认）；TIM_EncoderMode_TI1 = 只数 A（2 倍频） */
#define SYS_ENCODER_MODE    TIM_EncoderMode_TI12


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
typedef enum {
    SYS_ENCODER_1 = 0,
    SYS_ENCODER_2 = 1,
    SYS_ENCODER_COUNT = SYS_ENCODER_MAX
} SysEncoderId_t;

/* 初始化一路编码器
 * 参数 : id       —— SYS_ENCODER_1 / SYS_ENCODER_2
 *        tim      —— 用哪个定时器（TIM2/3/4/5/8 等带编码器接口的通用/高级定时器）
 *        ch1_port/pin/af —— A 相引脚（接定时器 CH1）
 *        ch2_port/pin/af —— B 相引脚（接定时器 CH2）
 *        invert   —— 1 = 反向计数（接线相反、或想翻转正负时用）
 * 返回 : 0 = 成功；1 = 参数非法（id 越界 / 空指针 / 定时器为 0）
 * 说明 : 计数器自动重装值设为 65535；初始化后计数清零
 * 示例 : SYS_ENCODER_Init(SYS_ENCODER_1, SYS_TIM_2,
 *                         GPIOA, GPIO_Pin_0, GPIO_AF_TIM2,
 *                         GPIOA, GPIO_Pin_1, GPIO_AF_TIM2, 0); */
uint8_t SYS_ENCODER_Init(SysEncoderId_t id, SysTimId_t tim,
                         GPIO_TypeDef *ch1_port, uint16_t ch1_pin, uint8_t ch1_af,
                         GPIO_TypeDef *ch2_port, uint16_t ch2_pin, uint8_t ch2_af,
                         uint8_t invert);

/* 读当前计数（16 位有符号扩展：正转增、反转减，范围 -32768~32767）
 * 说明 : 定时器是 16 位，本函数已处理"过零"的符号问题 */
int32_t SYS_ENCODER_GetCount(SysEncoderId_t id);

/* 读"自上次调用以来"的增量（用于测速：调用即清零，下次从零开始）
 * 返回 : 本次与上次调用之间的脉冲数（有符号）
 * 说明 : 内部维护 32 位累计，自动处理 16 位计数器回绕，**不会漏数**
 * 示例 : int16_t d = SYS_ENCODER_GetDelta(SYS_ENCODER_1);   // 本次周期内的脉冲 */
int32_t SYS_ENCODER_GetDelta(SysEncoderId_t id);

/* 读"上电以来"的累计脉冲数（32 位，带符号）
 * 用途 : 里程/位置计算（不受 GetDelta 清零点影响） */
int32_t SYS_ENCODER_GetTotal(SysEncoderId_t id);

/* 旋转方向：1 = 正转（计数递增）；0 = 反转 */
uint8_t SYS_ENCODER_GetDir(SysEncoderId_t id);

/* 计数清零（位置与累计值一起清） */
void SYS_ENCODER_Reset(SysEncoderId_t id);

/* 临时停止 / 恢复计数（做参数保存等长耗时时可先停，避免占用计数） */
void    SYS_ENCODER_Enable(SysEncoderId_t id, uint8_t enable);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 由脉冲增量换算转速（rpm，转/分钟），**不用浮点**
 * 参数 : delta      —— 一个采样周期内的脉冲数（SYS_ENCODER_GetDelta 的返回值）
 *        lines      —— 编码器线数（每转脉冲数，如 13 / 20 / 500）
 *        multiple   —— 倍频数（TIM_EncoderMode_TI12 填 4，TIM_EncoderMode_TI1 填 2）
 *        period_ms  —— 采样周期（ms）
 * 返回 : 转速 rpm（带符号：正 = 正转）
 * 示例 : int32_t rpm = SYS_ENCODER_DeltaToRpm(d, 13, 4, 10); */
int32_t SYS_ENCODER_DeltaToRpm(int32_t delta, uint16_t lines, uint8_t multiple,
                               uint16_t period_ms);

/* 由脉冲增量换算"每秒脉冲数"（做 PID 速度环时直接拿来当反馈量更省事） */
int32_t SYS_ENCODER_DeltaToCps(int32_t delta, uint16_t period_ms);

#endif /* __FWLIB_SYS_ENCODER_H */
