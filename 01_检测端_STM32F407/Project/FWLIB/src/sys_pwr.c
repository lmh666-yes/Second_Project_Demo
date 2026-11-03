#include "sys_pwr.h"
/* 标准库对照与示例注记见同名 .h;本文件为实现层 */

/* sys_pwr.c — 电源管理 / 低功耗(PWR) 实现
 * 睡眠深度由 SCB->SCR 的 SLEEPDEEP 位选择，__WFI() 等待中断；
 * Stop 模式需先使能 PWR 时钟，选择稳压器与进入方式；
 * Standby 模式先清唤醒标志，再执行 PWR_EnterSTANDBYMode()。
 */


/* 使能 PWR 时钟（幂等，可重复调用） */
static void pwr_clock_enable(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
}

/* Stop 唤醒回调（SYS_PWR_SetWakeCallback 注册，缺省为空） */
static void (*pwr_wake_callback)(void);


/* 睡眠：清除 SLEEPDEEP 位后 __WFI() 等待中断 */
void SYS_PWR_Sleep(void)
{
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;     /* 普通睡眠（只停 CPU） */
    __WFI();
}

/* 停机：低功耗稳压器 + WFI 进入；唤醒后恢复系统时钟 */
void SYS_PWR_Stop(void)
{
    pwr_clock_enable();

    /* 唤醒中断（EXTI 等）必须在进入前配置好，否则芯片会一直保持停机状态 */
    PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);

#if SYS_PWR_STOP_RESTORE_CLK
    /* 唤醒后系统时钟为 HSI，恢复默认 168MHz 配置
     * 外设（串口 / 定时器 / SysTick）需应用层重新初始化 */
    SystemInit();
#endif

    /* 唤醒回调（可选）：时钟已恢复，适合在此重装 SysTick / 重配外设；未注册时为空操作 */
    if (pwr_wake_callback != 0) {
        pwr_wake_callback();
    }
}

/* 待机：不返回，唤醒即复位重启 */
void SYS_PWR_Standby(void)
{
    pwr_clock_enable();

    /* 清一次唤醒标志 PWR_FLAG_WU（便于唤醒复位后判断唤醒来源） */
    PWR_ClearFlag(PWR_FLAG_WU);

    PWR_EnterSTANDBYMode();
}

/* WKUP 引脚(PA0)唤醒功能开关 */
void SYS_PWR_SetWakeupPin(uint8_t enable)
{
    pwr_clock_enable();

    PWR_WakeUpPinCmd(enable ? ENABLE : DISABLE);
}


/* 查询并清除唤醒事件标志 PWR_FLAG_WU；有唤醒事件返回 1，否则返回 0 */
uint8_t SYS_PWR_GetWakeupFlag(void)
{
    pwr_clock_enable();

    if (PWR_GetFlagStatus(PWR_FLAG_WU) != RESET) {
        PWR_ClearFlag(PWR_FLAG_WU);
        return 1;
    }
    return 0;
}

/* 注册 Stop 唤醒回调（传 0 取消） */
void SYS_PWR_SetWakeCallback(void (*callback)(void))
{
    pwr_wake_callback = callback;
}
