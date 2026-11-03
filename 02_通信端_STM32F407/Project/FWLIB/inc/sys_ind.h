#ifndef __FWLIB_SYS_IND_H
#define __FWLIB_SYS_IND_H

#include <stdint.h>

/*==================================================================
 * 模块：sys_ind   板2 指示灯状态机
 * 依赖：led.h
 *
 * 板2 只有两颗板载 LED（LED0=PF9、LED1=PF10），
 * 详细数据交给 LCD，这两颗灯只表达"系统有没有活、网络通不通"。
 *
 *   LED0  心跳：RUN 时 1 秒闪一次（亮 100ms）
 *               BOOT 时 100ms 快闪（表示还在初始化，别急着断电）
 *               FAULT 时与 LED1 一起 200ms 快闪（很扎眼）
 *   LED1  网络：RUN 时常亮 = MQTT 在线，灭 = 离线
 *
 * 调用顺序：
 *   LED_Init() 之后       SYS_IND_Init()
 *   启动调度器之前        SYS_IND_EnterRun()
 *   监控任务每 100ms      SYS_IND_Heartbeat()   //2 秒不调 = 卡死，自动进 FAULT
 *   vApplicationTickHook  SYS_IND_Tick()        //每 1ms
 *   网络任务链路变化      SYS_IND_NetState(0/1)
 *   栈溢出/硬件错误钩子   SYS_IND_Fault()
 *==================================================================*/

#define SYS_IND_BOOT_TOGGLE_MS   100U   /*BOOT 阶段翻转周期*/
#define SYS_IND_HB_PERIOD_MS    1000U   /*心跳周期*/
#define SYS_IND_HB_ON_MS         100U   /*心跳每次亮多久*/
#define SYS_IND_FAULT_PERIOD_MS  200U   /*FAULT 快闪周期*/
#define SYS_IND_HB_TIMEOUT_MS   2000U   /*多久没心跳算卡死*/

/*运行状态：
 *  BOOT  = 初始化中，灯快闪
 *  RUN   = 调度器跑起来了，心跳灯慢闪
 *  FAULT = 心跳超时/栈溢出/硬件错误，两灯同快闪
 *------------------------------------------------------------------*/
typedef enum
{
    SYS_IND_BOOT  = 0,
    SYS_IND_RUN   = 1,
    SYS_IND_FAULT = 2
} SysIndState_t;

void    SYS_IND_Init(void);              /*初始化，从 BOOT 开始*/
void    SYS_IND_EnterRun(void);          /*调度器启动前调，进 RUN*/
void    SYS_IND_Heartbeat(void);         /*被监控的任务活着就调一次*/
void    SYS_IND_Busy(uint8_t busy);      /*1=正在长阻塞(如连WiFi)，暂停心跳判定*/
void    SYS_IND_Tick(void);              /*1ms 节拍，钩子里调*/
void    SYS_IND_NetState(uint8_t online);/*1=MQTT 在线*/
void    SYS_IND_Fault(void);             /*锁存故障，灯转快闪*/
uint8_t SYS_IND_State(void);             /*当前状态 SYS_IND_xxx*/
uint8_t SYS_IND_Net(void);               /*网络是否在线*/

#endif /* __FWLIB_SYS_IND_H */
