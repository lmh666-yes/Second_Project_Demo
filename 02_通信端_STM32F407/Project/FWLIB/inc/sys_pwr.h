#ifndef __FWLIB_SYS_PWR_H
#define __FWLIB_SYS_PWR_H

#include "stm32f4xx.h"

/* pwr: 电源管理 / 低功耗(PWR)模块
 * 睡眠 Sleep 只停 CPU，任意中断唤醒，时钟不变；停机 Stop 停大部分时钟，SRAM
 * 内容保留，EXTI / RTC 等中断唤醒；待机 Standby 整芯片最低功耗，唤醒即复位。
 * 标准库函数：PWR_EnterSTOPMode / PWR_EnterSTANDBYMode / PWR_WakeUpPinCmd /
 * PWR_GetFlagStatus / PWR_ClearFlag。
 *
 * 低功耗停的是时钟，唤醒后依赖时钟的外设要重新初始化：Stop 唤醒后系统时钟为
 * HSI 16MHz，本模块按 SYS_PWR_STOP_RESTORE_CLK 调用 SystemInit() 恢复 168MHz，
 * SysTick、串口、定时器等由应用重新 Init。本板 WKUP 引脚为 PA0，与按键 KEY1
 * 共用，先用 SYS_PWR_SetWakeupPin 使能 WKUP 再可用于待机唤醒。 */


/* 定义与宏定义区 */
/* Stop 唤醒后是否自动恢复系统时钟（PLL 168MHz）
 * 1 = 自动调用 SystemInit()，醒来即可全速运行
 * 0 = 保留唤醒后的 HSI 16MHz，何时恢复由应用决定 */
#define SYS_PWR_STOP_RESTORE_CLK   1


/* 基础功能 */
/* 睡眠模式：立即进入，任意中断唤醒，唤醒后从下一句继续执行
 * 标准库 : 无，执行内核等待指令 WFI 即进入 */
void SYS_PWR_Sleep(void);

/* 停机模式：CPU 与大部分时钟停止，SRAM 内容保留
 * 唤醒源：EXTI 中断（如按键）、RTC 等
 * 唤醒后：按 SYS_PWR_STOP_RESTORE_CLK 的设置恢复时钟；SysTick、串口、
 * 定时器等由应用重新初始化
 * 标准库 : PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI) */
void SYS_PWR_Stop(void);

/* 待机模式：整芯片最低功耗
 * 唤醒源：WKUP(PA0) 上升沿 / RTC / 复位
 * 唤醒后等于芯片复位，程序从 main 重新开始
 * 标准库 : PWR_ClearFlag（清唤醒标志）+ PWR_EnterSTANDBYMode */
void SYS_PWR_Standby(void);

/* 使能或关闭 WKUP 引脚(PA0)的唤醒功能：enable 非 0 为使能，0 为关闭
 * 标准库 : PWR_WakeUpPinCmd(ENABLE/DISABLE)
 *          本 DFP 的裁剪版函数只有一个参数，引脚固定为 WKUP=PA0；
 *          完整版 SPL 的写法是 PWR_WakeUpPinCmd(PWR_WakeUpPin_1, ENABLE) */
void SYS_PWR_SetWakeupPin(uint8_t enable);


/* 扩展功能 */
/* 查询并清除唤醒事件标志，用于判断是否刚从 Stop/Standby 醒来
 * 返回 : 1 = 发生过唤醒事件（标志已清除）；0 = 无 */
uint8_t SYS_PWR_GetWakeupFlag(void);

/* 注册 Stop 唤醒回调（可选）：SYS_PWR_Stop 醒来且时钟恢复后自动执行一次，
 * 典型内容是重装 SysTick、重配串口与定时器
 * 参数 : 传 0 取消；回调运行在主上下文（非中断），可做普通初始化 */
void SYS_PWR_SetWakeCallback(void (*callback)(void));

#endif /* __FWLIB_SYS_PWR_H */
