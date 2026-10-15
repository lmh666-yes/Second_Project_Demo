#include "uln2003.h"
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_us 等）独立文件 */

/* 步进电机驱动（ULN2003D）实现
 * MCU 引脚 3.3V 小电流经 ULN2003 达林顿管驱动线圈，输出为漏极开路灌电流
 * 引脚高电平该相通电，由 ULN2003_ACTIVE_HIGH 选择
 * COM 脚必须接 5V，线圈续流经内部二极管回 5V；COM 悬空则电机不转且易损坏管子 */


/* 内部状态 */
static uint8_t  motor_phase = 0U;
static uint32_t motor_delay = ULN2003_STEP_DELAY_US;

/* 4 相控制脚的端口/引脚表，顺序与 CN2 的 T1~T4 一一对应
 * 表长固定 4，长度不符由下面的编译期检查拦截 */
static GPIO_TypeDef *const motor_port[4] = {
    MOTOR_IN1_PORT, MOTOR_IN2_PORT, MOTOR_IN3_PORT, MOTOR_IN4_PORT
};
static const uint16_t motor_pin[4] = {
    MOTOR_IN1_PIN, MOTOR_IN2_PIN, MOTOR_IN3_PIN, MOTOR_IN4_PIN
};

/* 编译期检查：端口表与引脚表都必须为 4 相，长度不符则编译失败 */
typedef char uln2003_port_table_check[(sizeof(motor_port) / sizeof(motor_port[0]) == 4U) ? 1 : -1];
typedef char uln2003_pin_table_check [(sizeof(motor_pin ) / sizeof(motor_pin [0]) == 4U) ? 1 : -1];

/* 八拍序列，每一位表示哪几相通电（bit0=A / bit1=B / bit2=C / bit3=D）
 * A → AB → B → BC → C → CD → D → DA
 * 相邻两步只切换一相，比四拍平滑、抖动小 */
static const uint8_t motor_seq8[8] = {
    0x01U, 0x03U, 0x02U, 0x06U, 0x04U, 0x0CU, 0x08U, 0x09U
};

#if (ULN2003_MODE_8BEAT == 0)
/* 四拍序列：A → B → C → D */
static const uint8_t motor_seq4[4] = {
    0x01U, 0x02U, 0x04U, 0x08U
};
#endif


/* 内部小工具 */

/* 按掩码给 4 相通/断电（掩码 bit0~bit3 对应相 A~D） */
static void motor_write(uint8_t mask)
{
    uint8_t i;

    for (i = 0U; i < 4U; i++) {
        uint8_t on = (uint8_t)((mask >> i) & 0x01U);

#if (ULN2003_ACTIVE_HIGH == 0)
        on = (uint8_t)(on ^ 0x01U);         /* 低电平有效：整体取反 */
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


/* 基础功能 */
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

    ULN2003_Stop();     /* 上电先断电，防止电机上电自行转动 */
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
    motor_write(0x0FU);         /* 四相全通，力矩最大，发热也最大 */
}


/* 扩展功能 */
void ULN2003_OneStep(uint8_t dir)
{
    motor_phase = (dir != 0U) ? (uint8_t)(motor_phase + 1U)
                              : (uint8_t)(motor_phase + 7U);   /* +7 即 -1，无符号回绕正确 */

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

/* 设置每步之间的延时，单位 us；入参限幅 100~20000 */
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
