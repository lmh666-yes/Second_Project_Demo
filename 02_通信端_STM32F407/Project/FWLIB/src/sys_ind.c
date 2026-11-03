#include "sys_ind.h"
#include "led.h"

/*==================================================================
 * 板2 指示灯状态机：两颗灯只表示"活着 / 网络通"，数据看 LCD
 *==================================================================*/

#define IND_LED_HB       0U     /*LED0 = 心跳*/
#define IND_LED_NET      1U     /*LED1 = 网络*/

static volatile uint8_t  s_state = SYS_IND_BOOT;
static volatile uint8_t  s_net = 0U;
static volatile uint8_t  s_fault_done = 0U;
static volatile uint32_t s_ms = 0U;         /*1ms 节拍计数*/
static volatile uint32_t s_hb_ms = 0U;      /*最近一次心跳的时刻*/
static volatile uint8_t  s_busy = 0U;       /*1=任务正在长阻塞，不判超时*/

/*==================================================================
 * 点亮/熄灭，调用者不用管极性
 *==================================================================*/
static void ind_set(uint8_t id, uint8_t on)
{
    if (on != 0U) {
        LED_On(id);
    } else {
        LED_Off(id);
    }
}

/*==================================================================
 * 初始化：两灯全灭，进 BOOT
 *==================================================================*/
void SYS_IND_Init(void)
{
    s_ms = 0U;
    s_hb_ms = 0U;
    s_net = 0U;
    s_fault_done = 0U;
    s_state = SYS_IND_BOOT;
    LED_AllOff();
}

/*==================================================================
 * 调度器启动前调：进 RUN，心跳计时从此刻起算
 *==================================================================*/
void SYS_IND_EnterRun(void)
{
    s_hb_ms = s_ms;
    s_state = SYS_IND_RUN;
    ind_set(IND_LED_HB, 1U);
    ind_set(IND_LED_NET, s_net);
}

/*==================================================================
 * 被监控任务周期性调用，证明调度器没挂
 *==================================================================*/
void SYS_IND_Heartbeat(void)
{
    s_hb_ms = s_ms;
}

/*==================================================================
 * 长阻塞开关：连 WiFi/建 TCP 会阻塞数秒，此时调度器仍在跑，
 * 其他任务只是拿不到 CPU——这不是卡死，必须暂停心跳判定
 *==================================================================*/
void SYS_IND_Busy(uint8_t busy)
{
    if (busy != 0U) {
        s_busy = 1U;
    } else {
        s_hb_ms = s_ms;             /*恢复时把计时重新对齐，避免误判*/
        s_busy = 0U;
    }
}

/*==================================================================
 * 网络状态变化时调
 *==================================================================*/
void SYS_IND_NetState(uint8_t online)
{
    s_net = (online != 0U) ? 1U : 0U;
    if (s_state == SYS_IND_RUN) {
        ind_set(IND_LED_NET, s_net);
    }
}

/*==================================================================
 * 锁存故障：只进不出，靠复位恢复
 *==================================================================*/
void SYS_IND_Fault(void)
{
    if (s_fault_done == 0U) {
        s_fault_done = 1U;
        s_state = SYS_IND_FAULT;
    }
}

uint8_t SYS_IND_State(void)
{
    return s_state;
}

uint8_t SYS_IND_Net(void)
{
    return s_net;
}

/*==================================================================
 * 1ms 节拍：所有闪烁都在这里算，不在任务里 delay
 *==================================================================*/
void SYS_IND_Tick(void)
{
    s_ms++;

    switch (s_state) {
    case SYS_IND_BOOT:
        /*初始化阶段快地闪，提示"还没起来，别断电"*/
        ind_set(IND_LED_HB, ((s_ms / SYS_IND_BOOT_TOGGLE_MS) & 1U) != 0U ? 1U : 0U);
        ind_set(IND_LED_NET, 0U);
        break;

    case SYS_IND_RUN:
        /*超过 2 秒没有心跳 = 任务卡死或调度器停了*/
        if ((s_busy == 0U) && ((uint32_t)(s_ms - s_hb_ms) > SYS_IND_HB_TIMEOUT_MS)) {
            SYS_IND_Fault();
            break;
        }
        ind_set(IND_LED_HB, ((s_ms % SYS_IND_HB_PERIOD_MS) < SYS_IND_HB_ON_MS) ? 1U : 0U);
        ind_set(IND_LED_NET, s_net);
        break;

    default:
        /*两灯同步快闪，现场一眼能认出来*/
        if ((s_ms % SYS_IND_FAULT_PERIOD_MS) < (SYS_IND_FAULT_PERIOD_MS / 2U)) {
            ind_set(IND_LED_HB, 1U);
            ind_set(IND_LED_NET, 1U);
        } else {
            ind_set(IND_LED_HB, 0U);
            ind_set(IND_LED_NET, 0U);
        }
        break;
    }
}
