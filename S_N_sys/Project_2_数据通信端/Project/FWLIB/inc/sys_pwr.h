#ifndef __FWLIB_SYS_PWR_H
#define __FWLIB_SYS_PWR_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_pwr.h —— 【系统】电源管理 / 低功耗(PWR)模块  头文件
 * ================================================================
 *  设计定位 : 把标准库的三种低功耗模式封装成"一行调用"
 *             —— 睡眠(Sleep)  : 只停 CPU；任意中断唤醒；时钟不变
 *             —— 停机(Stop)   : 停大部分时钟（SRAM 内容保留）；
 *                               EXTI / RTC 等中断唤醒
 *             —— 待机(Standby): 整芯片最低功耗；唤醒 = 芯片复位
 *  标准库关键词 : PWR_EnterSTOPMode / PWR_EnterSTANDBYMode / PWR_WakeUpPinCmd /
 *                 PWR_GetFlagStatus / PWR_ClearFlag
 *
 *  重要说明 :
 *   ① 低功耗的本质是"停时钟"，唤醒后"依赖时钟的外设要重新初始化"：
 *      - Stop 唤醒后系统时钟回到 HSI(16MHz)——本模块会自动调用
 *        SystemInit() 恢复 168MHz（可用"区块 1"的宏关掉该行为）；
 *      - SysTick / 串口 / 定时器 等需由应用重新 Init；
 *   ② 进入 Stop/Standby 前建议关闭不需要的外设，省电更彻底；
 *   ③ 本板 WKUP 引脚 = PA0（与按键 KEY1 共用）——可直接用
 *      KEY1 做待机唤醒（需先使能 WKUP 功能）。
 *
 *  使用方式 :
 *      SYS_PWR_Stop();        // 停机：等外部中断(如按键)唤醒
 *      SYS_PWR_Standby();     // 待机：按键/复位唤醒（唤醒 = 重启）
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* Stop 唤醒后是否自动恢复系统时钟（PLL 168MHz）
 * 1 = 自动调用 SystemInit()（推荐：醒来即可继续全速运行）
 * 0 = 保留唤醒后的 HSI 16MHz（自行决定何时恢复） */
#define SYS_PWR_STOP_RESTORE_CLK   1


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 睡眠模式：立即进入；任意中断唤醒；唤醒后从下一句继续执行
 * 特点：响应最快、最省事的省电方式（空闲时调用即可）
 * 标准库 : 无——执行内核等待指令（WFI）即进入 */
void SYS_PWR_Sleep(void);

/* 停机模式：CPU 与大部分时钟停止（SRAM 内容保留）
 * 唤醒源：EXTI 中断（如按键）、RTC 等
 * 唤醒后：时钟自动恢复 168MHz（见区块 1）；
 *         应用需自行重新初始化 SysTick、串口、定时器等
 * 标准库 : PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI)
 * 示例 : SYS_PWR_Stop();      // 本句执行后 CPU 停住,按键(EXTI)唤醒后继续 */
void SYS_PWR_Stop(void);

/* 待机模式：整芯片最低功耗
 * 唤醒源：WKUP(PA0) 上升沿 / RTC / 复位
 * 唤醒后 = 芯片复位，程序从 main 重新开始
 * 标准库 : PWR_ClearFlag（清唤醒标志）+ PWR_EnterSTANDBYMode
 * 示例 : SYS_PWR_SetWakeupPin(1);   // 先开 PA0 唤醒使能
 *        SYS_PWR_Standby();         // 进入待机（唤醒 = 重启） */
void SYS_PWR_Standby(void);

/* 使能 / 关闭 WKUP 引脚(PA0)的唤醒功能：enable 非 0 = 使能
 * 标准库 : PWR_WakeUpPinCmd(ENABLE/DISABLE)
 *          （本 DFP 的裁剪版函数就一个参数——引脚固定为 WKUP=PA0;
 *            完整版 SPL 的写法是 PWR_WakeUpPinCmd(PWR_WakeUpPin_1, ENABLE)）
 * 示例 : SYS_PWR_SetWakeupPin(1);   // 使能 PA0 唤醒(0 = 关闭) */
void SYS_PWR_SetWakeupPin(uint8_t enable);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 查询并清除"唤醒事件标志"（判断是否刚从 Stop/Standby 醒来）
 * 返回 : 1 = 发生过唤醒事件（标志已清除）；0 = 无 */
uint8_t SYS_PWR_GetWakeupFlag(void);

/* 注册"Stop 唤醒回调"（可选）：SYS_PWR_Stop 醒来、时钟恢复后
 * 自动执行一次——典型内容：重装 SysTick / 重配串口、定时器
 * 参数 : 传 0 取消；回调运行在"主上下文"（非中断），可做普通初始化
 * 示例 : SYS_PWR_SetWakeCallback(AfterWake);   // AfterWake 里 SYS_TICK_Init() 等 */
void SYS_PWR_SetWakeCallback(void (*callback)(void));

#endif /* __FWLIB_SYS_PWR_H */
