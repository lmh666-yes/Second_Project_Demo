#include "tb6612.h"
#include "gpio_core.h"

/* TB6612FNG 双路直流电机驱动实现
 * 硬件映射表
 * ---------------------------------------------------------------
 *   功能        资源              板上位置          改这里
 * ---------------------------------------------------------------
 *   PWMA 左轮   TIM4_CH1 / PB6    P2-3              区块1 TB6612_PWMA_*
 *   PWMB 右轮   TIM4_CH2 / PB7    P2-2              区块1 TB6612_PWMB_*
 *   AIN1/AIN2   PG2 / PG3         P2-41 / 40        区块1 TB6612_AIN*
 *   BIN1/BIN2   PG4 / PG5         P2-39 / 38        区块1 TB6612_BIN*
 *   STBY        PG13              P2-9              区块1 TB6612_STBY_*
 * ---------------------------------------------------------------
 * PB6/PB7 为 TIM4 的 CH1/CH2 且未被板载外设占用（I2C1 占 PB8/PB9、
 * SPI1 占 PB3/PB4/PB5、USART1 占 PA9/PA10）；PA0 为板载 KEY_UP 按键，
 * PG6/7/8 为板载 NRF24L01 座，均不可用。
 * 电源接线：VM 接 7.4V 电池正极，VCC 接开发板 3.3V，GND 三者共地；
 * 电池经 LM2596 转 5V 后接开发板 DC_IN，LM2596 输出端并 470µF 电解电容
 * 吸收电机启动电流冲击；电机线 VM/OUT1/OUT2 尽量粗短，不与 I2C 排线捆扎。
 * 上电后 STBY 必须为高电机才动作，本模块 Init 会自动拉高。 */

/* 编译期检查：配置越界或两路 PWM 共用同一通道时编译报错 */
typedef char tb6612_chA_check[(TB6612_PWMA_CH >= 1 && TB6612_PWMA_CH <= 4) ? 1 : -1];
typedef char tb6612_chB_check[(TB6612_PWMB_CH >= 1 && TB6612_PWMB_CH <= 4) ? 1 : -1];
typedef char tb6612_freq_check[(TB6612_PWM_FREQ_HZ >= 100UL) ? 1 : -1];
typedef char tb6612_freq2_check[(TB6612_PWM_FREQ_HZ <= 100000UL) ? 1 : -1];
typedef char tb6612_speed_check[(TB6612_SPEED_MAX <= 1000U) ? 1 : -1];
typedef char tb6612_turn_check[(TB6612_TURN_INNER <= 1000U) ? 1 : -1];
/* 两路 PWM 若在同一路定时器上，通道号必须不同，否则第二个会覆盖第一个 */
typedef char tb6612_dup_check[((TB6612_PWMA_TIM != TB6612_PWMB_TIM) || \
                               (TB6612_PWMA_CH  != TB6612_PWMB_CH)) ? 1 : -1];


/* 模块内部状态 */
typedef struct {
    GPIO_TypeDef *in1_port;
    uint16_t      in1_pin;
    GPIO_TypeDef *in2_port;
    uint16_t      in2_pin;
    SysTimId_t    tim;
    uint8_t       ch;
} TbMotor_t;

/* 下标 = TB6612_A(左轮) / TB6612_B(右轮) */
static const TbMotor_t tb_motor[2] = {
    { TB6612_AIN1_PORT, TB6612_AIN1_PIN, TB6612_AIN2_PORT, TB6612_AIN2_PIN,
      TB6612_PWMA_TIM,  TB6612_PWMA_CH },
    { TB6612_BIN1_PORT, TB6612_BIN1_PIN, TB6612_BIN2_PORT, TB6612_BIN2_PIN,
      TB6612_PWMB_TIM,  TB6612_PWMB_CH },
};

static int16_t s_speed[2] = { 0, 0 };   /* 当前带符号速度，斜坡用 */
static uint8_t s_ready = 0U;


/* 内部小工具 */

static void tb_write_dir(uint8_t ch, uint8_t in1, uint8_t in2)
{
    GPIO_OutWrite(tb_motor[ch].in1_port, tb_motor[ch].in1_pin, in1);
    GPIO_OutWrite(tb_motor[ch].in2_port, tb_motor[ch].in2_pin, in2);
}

static void tb_write_pwm(uint8_t ch, uint16_t permille)
{
    if (permille == 0U) {
        SYS_TIM_PwmStop(tb_motor[ch].tim, tb_motor[ch].ch);
    } else {
        SYS_TIM_PwmSetDuty(tb_motor[ch].tim, tb_motor[ch].ch, permille);
    }
}

/* 带符号速度：正=正转、负=反转、0=滑行 */
static void tb_set_signed(uint8_t ch, int16_t v)
{
    if (v > 0) {
        tb_write_dir(ch, 1U, 0U);
        tb_write_pwm(ch, (uint16_t)v);
    } else if (v < 0) {
        tb_write_dir(ch, 0U, 1U);
        tb_write_pwm(ch, (uint16_t)(-v));
    } else {
        tb_write_dir(ch, 0U, 0U);
        tb_write_pwm(ch, 0U);
    }
    s_speed[ch] = v;
}

static int16_t tb_clamp(int16_t v)
{
    if (v > 1000) return 1000;
    if (v < -1000) return -1000;
    return v;
}


/* 基础功能 */

uint8_t TB6612_Init(void)
{
    s_ready = 0U;

    /* 5 个方向/待机脚：推挽输出 GPIO_OType_PP，先全部拉低（两路滑行、芯片待机） */
    GPIO_OutInit(TB6612_AIN1_PORT, TB6612_AIN1_PIN);
    GPIO_OutInit(TB6612_AIN2_PORT, TB6612_AIN2_PIN);
    GPIO_OutInit(TB6612_BIN1_PORT, TB6612_BIN1_PIN);
    GPIO_OutInit(TB6612_BIN2_PORT, TB6612_BIN2_PIN);
    GPIO_OutInit(TB6612_STBY_PORT, TB6612_STBY_PIN);

    GPIO_OutReset(TB6612_AIN1_PORT, TB6612_AIN1_PIN);
    GPIO_OutReset(TB6612_AIN2_PORT, TB6612_AIN2_PIN);
    GPIO_OutReset(TB6612_BIN1_PORT, TB6612_BIN1_PIN);
    GPIO_OutReset(TB6612_BIN2_PORT, TB6612_BIN2_PIN);
    GPIO_OutReset(TB6612_STBY_PORT, TB6612_STBY_PIN);

    /* 两路 PWM：同一路定时器的两个通道，频率天然一致。
     * SYS_TIM_PwmInit 里的 TIM_TimeBaseInit 不碰 CCR，
     * 第二次调用不会破坏第一个通道已设的占空比 */
    SYS_TIM_PwmInit(TB6612_PWMA_TIM, TB6612_PWMA_CH,
                    TB6612_PWMA_PORT, TB6612_PWMA_PIN, TB6612_PWMA_AF,
                    TB6612_PWM_FREQ_HZ);
    SYS_TIM_PwmInit(TB6612_PWMB_TIM, TB6612_PWMB_CH,
                    TB6612_PWMB_PORT, TB6612_PWMB_PIN, TB6612_PWMB_AF,
                    TB6612_PWM_FREQ_HZ);

    s_speed[0] = 0;
    s_speed[1] = 0;
    s_ready = 1U;

    /* 解除待机：STBY 置高 */
    TB6612_Standby(1U);

    return TB6612_OK;
}

uint8_t TB6612_SetMotor(uint8_t ch, uint8_t dir, uint16_t speed)
{
    if (ch > 1U) return TB6612_ERR_PARAM;
    if (!s_ready) return TB6612_ERR_PARAM;

    if (speed > TB6612_SPEED_MAX) speed = TB6612_SPEED_MAX;
    if (speed > 1000U) speed = 1000U;

    switch (dir) {
    case TB6612_DIR_FWD:
        tb_write_dir(ch, 1U, 0U);
        tb_write_pwm(ch, speed);
        s_speed[ch] = (int16_t)speed;
        break;

    case TB6612_DIR_BACK:
        tb_write_dir(ch, 0U, 1U);
        tb_write_pwm(ch, speed);
        s_speed[ch] = -(int16_t)speed;
        break;

    case TB6612_DIR_BRAKE:
        /* 两个输入同时为高 = 短路制动，电机绕组被短接 */
        tb_write_dir(ch, 1U, 1U);
        tb_write_pwm(ch, 1000U);
        s_speed[ch] = 0;
        break;

    case TB6612_DIR_STOP:
    default:
        /* 两个输入同时为低 = 滑行，输出悬空 */
        tb_write_dir(ch, 0U, 0U);
        tb_write_pwm(ch, 0U);
        s_speed[ch] = 0;
        break;
    }

    return TB6612_OK;
}

void TB6612_Stop(void)
{
    (void)TB6612_SetMotor(TB6612_A, TB6612_DIR_STOP, 0U);
    (void)TB6612_SetMotor(TB6612_B, TB6612_DIR_STOP, 0U);
}

void TB6612_Brake(void)
{
    (void)TB6612_SetMotor(TB6612_A, TB6612_DIR_BRAKE, 0U);
    (void)TB6612_SetMotor(TB6612_B, TB6612_DIR_BRAKE, 0U);
}

void TB6612_Standby(uint8_t on)
{
    GPIO_OutWrite(TB6612_STBY_PORT, TB6612_STBY_PIN, (on != 0U) ? 1U : 0U);
}


/* 扩展功能 */

void TB6612_CarTank(int16_t left, int16_t right)
{
    tb_set_signed(TB6612_A, tb_clamp(left));
    tb_set_signed(TB6612_B, tb_clamp(right));
}

uint8_t TB6612_Car(uint8_t action, uint16_t speed)
{
    uint16_t inner;
    int16_t  v;

    if (speed > TB6612_SPEED_MAX) speed = TB6612_SPEED_MAX;
    if (speed > 1000U) speed = 1000U;

    v = (int16_t)speed;
    inner = (uint16_t)(((uint32_t)speed * (uint32_t)TB6612_TURN_INNER) / 1000UL);

    switch (action) {
    case TB6612_CAR_STOP:
        TB6612_Stop();
        break;
    case TB6612_CAR_FWD:
        TB6612_CarTank(v, v);
        break;
    case TB6612_CAR_BACK:
        TB6612_CarTank((int16_t)(-v), (int16_t)(-v));
        break;
    case TB6612_CAR_LEFT:                   /* 左轮慢、右轮快 → 往左拐 */
        TB6612_CarTank((int16_t)inner, v);
        break;
    case TB6612_CAR_RIGHT:                  /* 左轮快、右轮慢 → 往右拐 */
        TB6612_CarTank(v, (int16_t)inner);
        break;
    case TB6612_CAR_SPIN_L:                 /* 左反右正 → 原地逆时针 */
        TB6612_CarTank((int16_t)(-v), v);
        break;
    case TB6612_CAR_SPIN_R:                 /* 左正右反 → 原地顺时针 */
        TB6612_CarTank(v, (int16_t)(-v));
        break;
    default:
        return TB6612_ERR_PARAM;
    }

    return TB6612_OK;
}

/* 单路斜坡：朝 target 走一步 step，返回 1 = 已到位，0 = 未到位 */
static uint8_t tb_ramp_one(uint8_t ch, int16_t target, uint16_t step)
{
    int16_t cur = s_speed[ch];
    int16_t d;
    int16_t want;

    target = tb_clamp(target);

    d = (int16_t)(target - cur);
    if (d > (int16_t)step)       d = (int16_t)step;
    else if (d < -(int16_t)step) d = (int16_t)(-(int16_t)step);

    want = (int16_t)(cur + d);

    if (want != cur) tb_set_signed(ch, want);

    return (want == target) ? 1U : 0U;
}

uint8_t TB6612_CarRamp(int16_t target_l, int16_t target_r, uint16_t step)
{
    uint8_t done = 1U;

    if (step == 0U) step = 1U;

    if (tb_ramp_one(TB6612_A, target_l, step) == 0U) done = 0U;
    if (tb_ramp_one(TB6612_B, target_r, step) == 0U) done = 0U;

    return done;
}

void TB6612_GetSpeed(int16_t *left, int16_t *right)
{
    if (left  != 0) *left  = s_speed[TB6612_A];
    if (right != 0) *right = s_speed[TB6612_B];
}

const char *TB6612_ErrStr(uint8_t err)
{
    switch (err) {
    case TB6612_OK:        return "OK";
    case TB6612_ERR_PARAM: return "ERR: bad channel/action, or TB6612_Init not called";
    default:               return "ERR: unknown";
    }
}

/* tb6612.c end */
