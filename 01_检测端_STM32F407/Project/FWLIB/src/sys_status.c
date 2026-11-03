#include "sys_status.h"
#include "led.h"
#include "delay.h"
#include "sys_wdg.h"

/* ===================
 *  sys_status.c — 板载指示灯状态机（实现文件）
 * ===================
 *  三种状态: BOOT(流水灯) / RUN(LED3 慢闪) / FAULT(LED0 常亮)
 *  计时来源: BOOT 用 DWT 忙等; RUN/FAULT 用 1ms 节拍钩子累加。
 * =================== */


/* ===================
 *                    区块 1：内部变量
 * =================== */
static volatile uint8_t  s_state      = SYS_STATUS_BOOT;  /* 当前状态 */
static volatile uint32_t s_ms         = 0U;               /* 节拍累加(ms) */
static volatile uint32_t s_hb_ms      = 0U;               /* 最后一次心跳的 s_ms */
static volatile uint8_t  s_fault_done = 0U;               /* 故障灯是否已锁存 */


/* ===================
 *                    区块 2：内部函数
 * =================== */
//按位显示 4 颗灯（位=1 亮）
static void status_show(uint8_t mask)
{
    uint8_t i;

    for (i = 0U; i < LED_COUNT; i++)
    {
        if ((mask & (1U << i)) != 0U) LED_On(i);
        else                          LED_Off(i);
    }
}

//进入故障态并点亮 LED0（只做一次）
static void status_enter_fault(void)
{
    if (s_fault_done != 0U) return;

    s_state      = SYS_STATUS_FAULT;
    s_fault_done = 1U;
    status_show(0x01U);          /* 只亮 LED0 */
}


/* ===================
 *                    区块 3：对外接口
 * =================== */
//初始化状态机
void SYS_STATUS_Init(void)
{
    uint32_t cause;

    /* 先取复位原因,再清标志,否则下次读不到 */
    cause = SYS_WDG_ResetCause();
    SYS_WDG_ClearResetFlags();

    s_state      = SYS_STATUS_BOOT;
    s_ms         = 0U;
    s_hb_ms      = 0U;
    s_fault_done = 0U;

    /* 上次是独立看门狗复位：四灯快闪 3 次,既不误判"卡死",又能看出来 */
    if ((cause & SYS_WDG_RST_IWDG) != 0U)
    {
        uint8_t k;

        for (k = 0U; k < 3U; k++)
        {
            status_show(0x0FU);
            delay_ms_dwt(100U);
            status_show(0x00U);
            delay_ms_dwt(100U);
        }
    }

    status_show(0x01U);          /* 默认停在第一步 */
}

//启动阶段推进流水灯（step = 0~3）
void SYS_STATUS_BootStep(uint8_t step)
{
    if (step > 3U) step = 3U;

    status_show((uint8_t)(1U << step));
    delay_ms_dwt(SYS_STATUS_BOOT_MS);
}

//进入运行态
void SYS_STATUS_EnterRun(void)
{
    if (s_state == SYS_STATUS_FAULT) return;

    s_state = SYS_STATUS_RUN;
    s_hb_ms = s_ms;
    status_show(0x08U);          /* 只亮 LED3 */
}

//心跳上报
void SYS_STATUS_Heartbeat(void)
{
    s_hb_ms = s_ms;
}

//1ms 节拍处理（只在 vApplicationTickHook 里调）
void SYS_STATUS_Tick(void)
{
    s_ms++;

    if (s_state == SYS_STATUS_BOOT)  return;          /* 启动阶段由 main 推进 */

    if (s_state == SYS_STATUS_FAULT)
    {
        status_show(0x01U);
        return;
    }

    /* RUN：LED3 慢闪,亮 SYS_STATUS_RUN_ON_MS / 灭 (周期-ON) */
    if ((s_ms % SYS_STATUS_RUN_PERIOD_MS) < SYS_STATUS_RUN_ON_MS) LED_On(3);
    else                                                          LED_Off(3);

    /* 心跳超时 -> 任务卡死 */
    if ((uint32_t)(s_ms - s_hb_ms) > SYS_STATUS_HB_TIMEOUT_MS)
    {
        status_enter_fault();
    }
}

//故障指示（锁存）
void SYS_STATUS_Fault(void)
{
    status_enter_fault();
}

//查询当前状态
uint8_t SYS_STATUS_State(void)
{
    return s_state;
}
