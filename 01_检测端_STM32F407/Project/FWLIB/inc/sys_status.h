#ifndef __FWLIB_SYS_STATUS_H
#define __FWLIB_SYS_STATUS_H

#include "stm32f4xx.h"

/* ===================
 *  sys_status.h — 板载指示灯状态机（头文件）
 * ===================
 *  作用 : 用 4 颗 LED 显示"程序走到哪一步 / 是否在跑 / 有没有卡死",
 *         不需要串口也能一眼判断板子活着还是死了。
 *
 *  三种状态 :
 *    BOOT  启动中  —— 由 main() 在初始化各阶段调 SYS_STATUS_BootStep(n),
 *                    每步点亮第 n 颗灯并停 500ms（流水灯=进度条）。
 *                    灯停在哪颗,就说明卡在哪一步。
 *    RUN   运行中  —— LED3 每 1 秒闪一次（亮 900ms / 灭 100ms）。
 *                    灯在闪 = 心跳在跳。
 *    FAULT 故障    —— LED0 常亮、其余全灭。由两种途径触发:
 *                    ① 心跳超时 2 秒（任务卡死但中断还活着）;
 *                    ② SYS_FAULT 回调 / 栈溢出 / 堆耗尽（硬件异常）。
 *
 *  边界 : BOOT 阶段的延时用 DWT 忙等（不依赖 SysTick）;
 *         RUN/FAULT 阶段的计时靠 1ms 节拍钩子 vApplicationTickHook()。
 * =================== */


/* ===================
 *                    区块 1：状态定义
 * =================== */
typedef enum
{
    SYS_STATUS_BOOT  = 0,   /* 启动中（流水灯进度条） */
    SYS_STATUS_RUN   = 1,   /* 正常运行（LED3 慢闪） */
    SYS_STATUS_FAULT = 2    /* 故障（LED0 常亮） */
} SysStatusState_t;

/* 启动流水灯每步停留时间（ms）。觉得开机慢就把它改小，如 100 */
#define SYS_STATUS_BOOT_MS        200U
/* 心跳超时（ms）：超过这么久没有 SYS_STATUS_Heartbeat() 就判故障 */
#define SYS_STATUS_HB_TIMEOUT_MS  2000U
/* 运行态 LED3 闪烁周期与点亮时长（ms） */
#define SYS_STATUS_RUN_PERIOD_MS  1000U
#define SYS_STATUS_RUN_ON_MS      900U


/* ===================
 *                    区块 2：功能接口
 * =================== */
/* ----------------------------------------------------------------
 *  初始化状态机
 * ----------------------------------------------------------------
 * 说明 : LED_Init() 之后调用;内部读复位原因,若上次是 IWDG 看门狗
 *        复位,先点亮 LED0 并停 1.5 秒,提示"上次是被狗救回来的"。
 * 参数 : 无
 * 示例 : LED_Init(); SYS_STATUS_Init(); */
void SYS_STATUS_Init(void);

/* ----------------------------------------------------------------
 *  启动阶段推进流水灯
 * ----------------------------------------------------------------
 * 说明 : 在 main() 各初始化阶段之间调用,step = 0~3 对应 LED0~LED3。
 *        每调一次点亮该灯并延时 SYS_STATUS_BOOT_MS。
 * 参数 : step 0~3（超出按 3 处理）
 * 示例 : SYS_STATUS_BootStep(0); ... 时钟/NVIC/串口 ... SYS_STATUS_BootStep(1); */
void SYS_STATUS_BootStep(uint8_t step);

/* ----------------------------------------------------------------
 *  进入运行态
 * ----------------------------------------------------------------
 * 说明 : vTaskStartScheduler() 之前调用,切到 RUN 并点亮 LED3。
 *        之后由 1ms 节拍钩子驱动 LED3 慢闪与心跳超时判断。
 * 参数 : 无
 * 示例 : SYS_STATUS_EnterRun(); SYS_WDG_Init(4000); vTaskStartScheduler(); */
void SYS_STATUS_EnterRun(void);

/* ----------------------------------------------------------------
 *  心跳上报
 * ----------------------------------------------------------------
 * 说明 : 由 Monitor 任务每 100ms 调一次;超过 2 秒没报到 -> 进 FAULT。
 *        放在"一定会跑"的任务里,别的任务卡死才能被抓到。
 * 参数 : 无
 * 示例 : SYS_STATUS_Heartbeat(); */
void SYS_STATUS_Heartbeat(void);

/* ----------------------------------------------------------------
 *  1ms 节拍处理
 * ----------------------------------------------------------------
 * 说明 : 只在 vApplicationTickHook() 里调一次,不要放别处。
 * 参数 : 无
 * 示例 : void vApplicationTickHook(void){ LED_BlinkUpdate(); SYS_STATUS_Tick(); } */
void SYS_STATUS_Tick(void);

/* ----------------------------------------------------------------
 *  故障指示（锁存）
 * ----------------------------------------------------------------
 * 说明 : 进 FAULT 并点亮 LED0,之后不会被心跳恢复（要复位才清）。
 *        挂到 SYS_FAULT_SetCallback(),也用于栈溢出/堆耗尽钩子。
 * 参数 : 无
 * 示例 : SYS_FAULT_SetCallback(SYS_STATUS_Fault); */
void SYS_STATUS_Fault(void);

/* ----------------------------------------------------------------
 *  查询当前状态
 * ----------------------------------------------------------------
 * 返回 : SysStatusState_t
 * 示例 : if (SYS_STATUS_State() == SYS_STATUS_FAULT) ... */
uint8_t SYS_STATUS_State(void);

#endif /* __FWLIB_SYS_STATUS_H */
