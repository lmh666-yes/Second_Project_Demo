#include "uln2003.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_us 等）独立文件 */

/* ================================================================
 *  uln2003.c —— 【板载】步进电机驱动（ULN2003D）  实现文件
 * ================================================================
 *  为什么中间要放一颗 ULN2003？
 *
 *      步进电机的线圈是"电感性负载"，而且相电流比 MCU 引脚能给的
 *      （20mA 量级）大得多。ULN2003 是 7 路 **达林顿管**（内部已带
 *      续流二极管），干的就是"小电流控制大电流"这件事：
 *
 *          MCU 引脚(3.3V, 几 mA)  →  ULN2003 输入
 *          电机线圈（几十至几百 mA） ←  ULN2003 输出（漏极开路，灌电流）
 *
 *      ⇒ 所以"给高电平"= 这一相**通电**（ACTIVE_HIGH）。
 *
 *  ⇒ 同时也解释了为什么电机模块的 COM 脚要接 5V：
 *      灌电流回来要经过续流二极管回到 5V，不接 COM 电机不转还容易烧管。
 * ================================================================ */


/* ================================================================
 *                      内部状态
 * ================================================================ */
static uint8_t  motor_phase = 0U;
static uint32_t motor_delay = ULN2003_STEP_DELAY_US;

/* 4 相控制脚的端口/引脚表 —— 顺序与 CN2 的 T1~T4 严格对应
 * （数量由 4 个宏决定，少写/多写都会编译不过，见下面的编译期护栏） */
static GPIO_TypeDef *const motor_port[4] = {
    MOTOR_IN1_PORT, MOTOR_IN2_PORT, MOTOR_IN3_PORT, MOTOR_IN4_PORT
};
static const uint16_t motor_pin[4] = {
    MOTOR_IN1_PIN, MOTOR_IN2_PIN, MOTOR_IN3_PIN, MOTOR_IN4_PIN
};

/* 编译期护栏：这张表和"4 相"必须一致 —— 不一致直接编译不过 */
typedef char uln2003_port_table_check[(sizeof(motor_port) / sizeof(motor_port[0]) == 4U) ? 1 : -1];
typedef char uln2003_pin_table_check [(sizeof(motor_pin ) / sizeof(motor_pin [0]) == 4U) ? 1 : -1];

/* 八拍序列：每一位对应"哪几相通电"（bit0=A / bit1=B / bit2=C / bit3=D）
 *   A → AB → B → BC → C → CD → D → DA
 * 相邻两步只切换一相，这就是"八拍比四拍平滑、抖动小"的原因 */
static const uint8_t motor_seq8[8] = {
    0x01U, 0x03U, 0x02U, 0x06U, 0x04U, 0x0CU, 0x08U, 0x09U
};

#if (ULN2003_MODE_8BEAT == 0)
/* 四拍序列：A → B → C → D */
static const uint8_t motor_seq4[4] = {
    0x01U, 0x02U, 0x04U, 0x08U
};
#endif


/* ================================================================
 *                      内部小工具
 * ================================================================ */

/* 按掩码给 4 相通/断电（掩码 bit0~bit3 ↔ 相 A~D） */
static void motor_write(uint8_t mask)
{
    uint8_t i;

    for (i = 0U; i < 4U; i++) {
        uint8_t on = (uint8_t)((mask >> i) & 0x01U);

#if (ULN2003_ACTIVE_HIGH == 0)
        on = (uint8_t)(on ^ 0x01U);         /* 接线反了：整体取反 */
#endif

        if (on != 0U) GPIO_OutSet  (motor_port[i], motor_pin[i]);
        else          GPIO_OutReset(motor_port[i], motor_pin[i]);
    }
}

/* 相位序号 → 通电掩码 */
static uint8_t motor_mask(uint8_t phase)
{
#if (ULN2003_MODE_8BEAT)
    return motor_seq8[phase & 0x07U];
#else
    return motor_seq4[phase & 0x03U];
#endif
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
void ULN2003_Init(void)
{
#if (ULN2003_ENABLE == 0)
    return;
#else
    uint8_t i;

    for (i = 0U; i < 4U; i++) {
        GPIO_ClockEnable(motor_port[i]);
        GPIO_OutInit(motor_port[i], motor_pin[i]);
    }

    motor_phase = 0U;
    motor_delay = ULN2003_STEP_DELAY_US;

    ULN2003_Stop();     /* 上电先断电，防止一上电电机自己哆嗦 */
#endif
}

void ULN2003_Step(uint32_t steps, uint8_t dir)
{
    uint32_t i;

    if (steps == 0UL) return;

    for (i = 0UL; i < steps; i++) {
        ULN2003_OneStep(dir);
        delay_us(motor_delay);
    }
}

void ULN2003_Stop(void)
{
    motor_write(0x00U);         /* 四相全断 */
}

void ULN2003_Hold(void)
{
    motor_write(0x0FU);         /* 四相全通，力矩最大（发热也最大） */
}


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
void ULN2003_OneStep(uint8_t dir)
{
    motor_phase = (dir != 0U) ? (uint8_t)(motor_phase + 1U)
                              : (uint8_t)(motor_phase + 7U);   /* +7 = -1，且天然回绕 */

#if (ULN2003_MODE_8BEAT)
    motor_phase &= 0x07U;
#else
    motor_phase &= 0x03U;
#endif

    motor_write(motor_mask(motor_phase));
}

uint8_t ULN2003_GetPhase(void)
{
    return motor_phase;
}

void ULN2003_ResetPhase(void)
{
    motor_phase = 0U;
}

void ULN2003_SetStepDelay(uint32_t us)
{
    if (us < 100UL)    us = 100UL;
    if (us > 20000UL)  us = 20000UL;

    motor_delay = us;
}

uint32_t ULN2003_GetStepDelay(void)
{
    return motor_delay;
}

/* 文件结束 */
