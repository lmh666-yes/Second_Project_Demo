#ifndef __FWLIB_SYS_TICK_H
#define __FWLIB_SYS_TICK_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_tick.h —— 【系统】SysTick 硬件定时器模块  头文件
 * ================================================================
 *  设计定位 : 系统级精确定时服务（毫秒时基 + 微秒延时）
 *             基于 Cortex-M4 内核自带的 24 位递减计数器，
 *             不占用任何外设定时器资源。
 *
 *  标准库关键词 : 无 —— 直接操作内核寄存器（SysTick->LOAD/VAL/CTRL,
 *                 CMSIS 头 core_cm4.h 定义;标准外设库不提供 SysTick API）
 *
 *  与 gpio_core 的 Delay_ms 区别 :
 *      gpio_core 的 Delay_ms —— 软件空循环，主频/优化一变就不准；
 *      本模块的 SYS_TICK_*  —— 硬件计数，改主频后重新 Init 依旧精准。
 *      => 对时间有要求的场合（计时、节拍、超时判断）用本模块。
 *
 *  重要说明 :
 *   ① 本模块使用 SysTick 的"中断方式"：每 1ms 触发一次
 *      SysTick_Handler 给毫秒计数器 +1；
 *   ② SysTick 是内核独占资源，全芯片只能有一个使用者——
 *      若其它代码/库（或 RTOS）也配置了 SysTick，会相互冲突；
 *      与 FreeRTOS 混用时：本模块 SysTick_Handler 是"弱定义"，
 *      会被 FreeRTOS 的强定义自动顶替，SysTick 归 RTOS 调度使用，
 *      此时本模块的延时/计时函数失效，请改用 vTaskDelay 系列；
 *   ③ ⚠ 与 sys_clock 联动（重要）：SYS_CLK_Switch() 换主频后，
 *      SysTick 的 LOAD 仍是旧主频的值——必须重新调用
 *      SYS_TICK_Init()，否则毫秒延时/计时全部失准。标准写法：
 *          if (SYS_CLK_Switch(target) == SYS_CLK_OK) {
 *              SYS_TICK_Init();            （重新校准毫秒时基）
 *          }
 *      （完整"切换后必须重做"的模块清单见 sys_clock.h 顶部警告框）
 *   ④ SysTick_Handler 在本模块 .c 内定义（弱定义）;你手写同名函数会
 *      自动顶替库版（顶替后本模块计时/延时全部停用;机制同②的让位）。
 *   ⑤ 本模块不设置 SysTick 的中断优先级（保持内核复位默认值）；
 *      需要统一规划优先级请用 sys_nvic 模块（上 RTOS 后由 RTOS 接管）。
 *
 *  使用方式 :
 *      SYS_TICK_Init();                 // ① 初始化（1ms 中断）
 *      SYS_TICK_Delay_ms(500);          // ② 精确延时 500ms
 *      uint32_t t = SYS_TICK_GetTick(); // ③ 取时间戳
 *      ...;                             //    做一段操作
 *      SYS_TICK_Elapsed(t);             //    → 已过去多少 ms
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* SysTick 中断周期（毫秒）：1 = 每 1ms 中断一次（默认，也是推荐值）
 * 说明 : ms 时基（Delay_ms / GetTick / Elapsed）按该周期工作；
 *       改大周期会降低 ms 计时精度，一般保持 1 即可 */
#define SYS_TICK_PERIOD_MS   1


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：按 SystemCoreClock 计算 1ms 重装值并启动中断计时
 * 调用时机：main 开头；或每次切换系统时钟之后（重算重装值）
 * 标准库 : 无——寄存器直写 SysTick->LOAD / VAL / CTRL（CMSIS 定义）*/
void     SYS_TICK_Init    (void);

/* 精确阻塞延时（ms）：靠毫秒计数器差值判断，与主频无关
 * 说明 : 延时期间 CPU 空转等待（阻塞式）
 * ⚠ 不要在中断/回调里使用本函数：它依赖 SysTick 中断"继续累加
 *   时基"，优先级配置不当时会在 ISR 里永久死等；中断内延时请用
 *   gpio_core 的 Delay_ms()（纯忙等，不依赖任何中断）
 * 示例 : SYS_TICK_Init();
 *        SYS_TICK_Delay_ms(500);        // 精确延时半秒 */
void     SYS_TICK_Delay_ms(uint32_t ms);

/* 取当前毫秒时刻（时间戳）：上电从 0 开始累加，约 49.7 天回绕 */
uint32_t SYS_TICK_GetTick (void);

/* 计算 start_tick 至今过去了多少 ms
 * 示例 : uint32_t t0 = SYS_TICK_GetTick();
 *        ...做一段操作...
 *        uint32_t dt = SYS_TICK_Elapsed(t0);   // dt = 已过去毫秒数
 * 溢出安全：基于无符号减法，计数器回绕瞬间结果依然正确 */
uint32_t SYS_TICK_Elapsed (uint32_t start_tick);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 微秒级阻塞延时（1 ~ 1000us，超出按 1000 处理）
 * 说明 : 直接轮询计数器值实现（1ms 中断的粒度不够微秒用）；
 *       需要先调用 SYS_TICK_Init() 使计数器处于运行状态
 * 标准库 : 无——轮询 SysTick->VAL 寄存器
 * 示例 : SYS_TICK_Delay_us(10);      // 延时 10 微秒 */
void     SYS_TICK_Delay_us(uint32_t us);

/* 秒级阻塞延时（内部按 1 秒步进循环，支持任意 uint32 秒数）
 * 示例 : SYS_TICK_Delay_s(2);    // 精确延时 2 秒 */
void     SYS_TICK_Delay_s (uint32_t s);

/* 获取微秒级时间戳（毫秒部分 + 当前毫秒周期内的微秒数）
 * 说明 : 基于计数器轮询拼合，典型误差几个微秒；
 *       适合测量脉冲宽度 / 短过程耗时（约 49.7 天回绕） */
uint32_t SYS_TICK_GetUs   (void);

/* 超时判断辅助（非阻塞）：
 *   传入起始时间戳与超时时间，返回 1 = 已超时，0 = 未到
 * 典型用法 : 
 *      uint32_t t = SYS_TICK_GetTick();
 *      while (!SYS_TICK_Timeout(t, 500)) { ... }   // 最多等 500ms
 * 溢出安全：内部用 SYS_TICK_Elapsed 差值判断 */
uint8_t  SYS_TICK_Timeout (uint32_t start_tick, uint32_t timeout_ms);

/* 周期节拍辅助（非阻塞）：判断"距上次执行已满 period_ms"
 * 参数 : last_tick —— 保存上次执行时刻的变量（首次传 0 即可）
 *        period_ms —— 周期（ms，0 视为 1）
 * 返回 : 1 = 到点了（内部已把 *last_tick 更新为当前时刻）；0 = 未到
 * 典型用法（每 200ms 执行一次任务，不用阻塞式延时）：
 *      static uint32_t t = 0;
 *      if (SYS_TICK_Every(&t, 200)) { ...每 200ms 做的事... }
 * 说明 : 每处任务各自持有自己的变量 —— 一份时基、多路节拍互不干扰 */
uint8_t  SYS_TICK_Every (uint32_t *last_tick, uint32_t period_ms);

#endif /* __FWLIB_SYS_TICK_H */

