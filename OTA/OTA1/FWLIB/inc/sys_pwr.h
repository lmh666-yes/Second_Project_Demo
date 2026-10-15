#ifndef __FWLIB_SYS_PWR_H
#define __FWLIB_SYS_PWR_H

#include "stm32f4xx.h"

/* sys_pwr.h - 电源管理 / 低功耗(PWR)模块头文件
 * 接口: Sleep/Stop/Standby 进入、WKUP(PA0)唤醒使能、唤醒标志查询、停止唤醒回调
 * 依据: RM0090 5.1 低功耗模式; Stop 唤醒后系统时钟回到 HSI(16MHz), PLL 与依赖时钟的外设需重新配置 */


/* 区块 1: 定义与宏定义区 */
/* Stop 唤醒后是否自动恢复系统时钟(PLL 168MHz)
 * 1 = 调用 SystemInit() 恢复 168MHz; 0 = 保持唤醒后的 HSI 16MHz */
#define SYS_PWR_STOP_RESTORE_CLK   1


/* 区块 2: 基础功能 */
/* 睡眠模式: 执行内核 WFI 指令进入, 无标准库函数
 * 唤醒源: 任意中断; 唤醒后从下一条指令继续, 时钟不变
 * 参数: 无; 返回: 无 */
void SYS_PWR_Sleep(void);

/* 停机模式: 停 CPU 与大部分时钟, SRAM 内容保留
 * 唤醒源: EXTI(如按键) / RTC 等中断
 * 唤醒后: 按 SYS_PWR_STOP_RESTORE_CLK 恢复时钟; SysTick / 串口 / 定时器需应用重新初始化
 * 进入前关闭不需要的外设时钟, 唤醒后由应用的初始化代码恢复
 * 参数: 无; 返回: 无
 * 库函数: PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI) */
void SYS_PWR_Stop(void);

/* 待机模式: 整芯片最低功耗
 * 唤醒源: WKUP(PA0) 上升沿 / RTC / 复位
 * 唤醒等同于芯片复位, 程序从 main 重新开始
 * 参数: 无; 返回: 无
 * 依据: RM0090 5.1.3; WKUP 引脚使能需在进入待机前完成
 * 库函数: PWR_ClearFlag(清唤醒标志) + PWR_EnterSTANDBYMode */
void SYS_PWR_Standby(void);

/* 使能 / 失能 WKUP 引脚(PA0)的唤醒功能
 * enable: 非 0 使能, 0 失能
 * 库函数: PWR_WakeUpPinCmd(ENABLE/DISABLE), 本裁剪版引脚固定为 WKUP(PA0)
 * 约束: 使能前需将 PA0 配置为输入; 本板 WKUP = PA0, 与按键 KEY1 共用 */
void SYS_PWR_SetWakeupPin(uint8_t enable);


/* 区块 3: 扩展功能 */
/* 查询并清除唤醒事件标志, 用于判断是否刚从 Stop/Standby 醒来
 * 返回: 1 = 发生过唤醒事件(标志已清除); 0 = 无
 * 依据: PWR_CSR 的 WUF 位, 由 PWR_ClearFlag 清除
 * 库函数: PWR_GetFlagStatus + PWR_ClearFlag */
uint8_t SYS_PWR_GetWakeupFlag(void);

/* 注册 Stop 唤醒回调, SYS_PWR_Stop 醒来且时钟恢复后执行一次
 * 参数: callback 传 0 取消; 在主上下文(非中断)执行
 * 用途: 重装 SysTick / 重配串口、定时器 */
void SYS_PWR_SetWakeCallback(void (*callback)(void));

#endif /* __FWLIB_SYS_PWR_H */
