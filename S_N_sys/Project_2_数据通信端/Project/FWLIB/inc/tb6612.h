#ifndef __FWLIB_TB6612_H
#define __FWLIB_TB6612_H

#include "stm32f4xx.h"
#include "sys_tim.h"

/* tb6612.h: TB6612FNG 双路直流电机驱动（4WD 小车底盘）
 *
 * 一片 TB6612 有 A / B 两个通道，每通道含两个方向脚（AIN1/AIN2、BIN1/BIN2）
 * 和一路 PWM（PWMA、PWMB）。4WD 底盘把左侧两个马达并联接 A 通道、右侧并联接 B 通道，
 * 因此左轮速度与右轮速度两个量即可表示全部动作：左=右 直行、左>右 右转、
 * 左<右 左转、左=-右 原地转圈。四轮独立需两片 TB6612，共 4 通道。
 *
 * AIN1  AIN2   PWM     结果
 *  1     0     PWM   正转（占空比 = 速度）
 *  0     1     PWM   反转
 *  0     0     任意  停止（滑行，电机两端悬空）
 *  1     1     任意  刹车（短路制动，长时间通电会发热）
 * STBY = 0 整片待机，两路都停；正常驱动需拉高。
 *
 * 接线（P2 排针，以下引脚在不用彩屏、不用外扩 SRAM 时空闲）：
 *      PWMA -> PB6 (TIM4_CH1) P2-3      AIN1 -> PG2  P2-41
 *      PWMB -> PB7 (TIM4_CH2) P2-2      AIN2 -> PG3  P2-40
 *      BIN1 -> PG4 P2-39                BIN2 -> PG5  P2-38
 *      STBY -> PG13 P2-9
 *      VCC -> 3.3V（逻辑电源）  VM -> 7.4V（电池正极，接 5V 扭矩不足）
 *      GND -> 与电池、开发板共地
 *
 * 依赖 : sys_tim.h（PWM 调速）、gpio_core.h（方向脚） */


/* 区块 1：定义与宏，换接线只改这里 */

/* 通道 A（左轮）：PWM 脚 */
#define TB6612_PWMA_PORT    GPIOB
#define TB6612_PWMA_PIN     GPIO_Pin_6      /* P2-3 */
#define TB6612_PWMA_TIM     SYS_TIM_4
#define TB6612_PWMA_CH      1
#define TB6612_PWMA_AF      GPIO_AF_TIM4

/* 通道 A（左轮）：方向脚 */
#define TB6612_AIN1_PORT    GPIOG
#define TB6612_AIN1_PIN     GPIO_Pin_2      /* P2-41 */
#define TB6612_AIN2_PORT    GPIOG
#define TB6612_AIN2_PIN     GPIO_Pin_3      /* P2-40 */

/* 通道 B（右轮）：PWM 脚 */
#define TB6612_PWMB_PORT    GPIOB
#define TB6612_PWMB_PIN     GPIO_Pin_7      /* P2-2 */
#define TB6612_PWMB_TIM     SYS_TIM_4
#define TB6612_PWMB_CH      2
#define TB6612_PWMB_AF      GPIO_AF_TIM4

/* 通道 B（右轮）：方向脚 */
#define TB6612_BIN1_PORT    GPIOG
#define TB6612_BIN1_PIN     GPIO_Pin_4      /* P2-39 */
#define TB6612_BIN2_PORT    GPIOG
#define TB6612_BIN2_PIN     GPIO_Pin_5      /* P2-38 */

/* 待机脚：高 = 工作，低 = 整片待机 */
#define TB6612_STBY_PORT    GPIOG
#define TB6612_STBY_PIN     GPIO_Pin_13     /* P2-9 */

/* 调速参数 */
/* PWM 频率：TT 马达适用 1k~5kHz。低于 500Hz 电机鸣叫、低速抖动；
 * 高于 10kHz 铁损增大、低速扭矩下降。
 * 两个通道须用同一频率，同一定时器天然满足 */
#define TB6612_PWM_FREQ_HZ  2000UL

/* 速度上限（千分比），防止开满。
 * TT 马达 7.4V 满占空比转速偏高，建议先用 700 */
#define TB6612_SPEED_MAX    1000U

/* 左转/右转时内侧轮保留的速度千分比，越小转得越急。
 * 例: 500 → 左转时内轮 500‰、外轮 1000‰，按传入速度等比缩放 */
#define TB6612_TURN_INNER   400U


/* 区块 2：基础功能 */

/* 电机通道 */
#define TB6612_A            0U      /* 通道 A：左轮 */
#define TB6612_B            1U      /* 通道 B：右轮 */

/* 单电机方向 */
#define TB6612_DIR_STOP     0U      /* 停止（滑行） */
#define TB6612_DIR_FWD      1U      /* 正转 */
#define TB6612_DIR_BACK     2U      /* 反转 */
#define TB6612_DIR_BRAKE    3U      /* 刹车（短路制动，立刻停） */

/* 小车整体动作（给 TB6612_Car 用） */
#define TB6612_CAR_STOP     0U      /* 停 */
#define TB6612_CAR_FWD      1U      /* 前进 */
#define TB6612_CAR_BACK     2U      /* 后退 */
#define TB6612_CAR_LEFT     3U      /* 左转（左轮慢、右轮快） */
#define TB6612_CAR_RIGHT    4U      /* 右转（左轮快、右轮慢） */
#define TB6612_CAR_SPIN_L   5U      /* 原地左转（左反、右正） */
#define TB6612_CAR_SPIN_R   6U      /* 原地右转（左正、右反） */

/* 返回码（全库统一：0 成功） */
#define TB6612_OK           0U
#define TB6612_ERR_PARAM    1U      /* 通道号 / 动作号越界 */

/* 初始化：配置 2 路 PWM 与 5 个方向脚，并把 STBY 拉高解除待机
 * 返回 : TB6612_OK
 * 说明 : STBY 未拉高时设 PWM 电机也不动 */
uint8_t TB6612_Init(void);

/* 设置某一路电机的方向和速度
 * 参数 : ch    : TB6612_A（左轮）或 TB6612_B（右轮）
 *        dir   : TB6612_DIR_STOP / FWD / BACK / BRAKE
 *        speed : 速度千分比 0~1000（1000 = 100% 占空比）
 * 返回 : TB6612_OK / TB6612_ERR_PARAM */
uint8_t TB6612_SetMotor(uint8_t ch, uint8_t dir, uint16_t speed);

/* 停车：两路都停（滑行），STBY 保持高 */
void TB6612_Stop(void);

/* 刹车：两路短路制动，立刻停住（长时间通电电机会发热） */
void TB6612_Brake(void);

/* 待机控制：on=1 解除待机（正常工作）；on=0 整片待机（两路断电，最省电）
 * 长时间不用调 Standby(0)，比 Stop 更省电 */
void TB6612_Standby(uint8_t on);


/* 区块 3：扩展功能 */

/* 左右轮独立给定
 * 参数 : left / right : 带符号速度千分比，正 = 前进，负 = 后退，0 = 停
 *                        取值范围 -1000 ~ +1000
 * 说明 : 差速转向由这两个量组合：直行 (600, 600)、左转 (400, 800)、
 *        原地左转 (-500, 500) */
void TB6612_CarTank(int16_t left, int16_t right);

/* 按动作开车：内部按 TB6612_TURN_INNER 折算左右轮差速
 * 参数 : action : TB6612_CAR_STOP / FWD / BACK / LEFT / RIGHT / SPIN_L / SPIN_R
 *        speed  : 外侧轮速度千分比 0~1000
 * 返回 : TB6612_OK / TB6612_ERR_PARAM */
uint8_t TB6612_Car(uint8_t action, uint16_t speed);

/* 速度斜坡：从当前速度逐步过渡到目标速度，避免起步打滑、掉电
 * 参数 : target_l / target_r : 目标左右速度（带符号）
 *        step                : 每毫秒变化量（千分比/ms），建议 5~20
 * 返回 : 到位返回 1，仍在过渡返回 0
 * 说明 : 须在循环里反复调用直到返回 1；按 1ms 间隔调用才准，
 *        间隔更长时按实际间隔折算 step */
uint8_t TB6612_CarRamp(int16_t target_l, int16_t target_r, uint16_t step);

/* 读回当前左右速度（调试、上位机显示用）
 * 参数 : left / right : 出参，可传 0 表示不要 */
void TB6612_GetSpeed(int16_t *left, int16_t *right);

/* 返回码转文字（调试打印用） */
const char *TB6612_ErrStr(uint8_t err);

#endif /* __FWLIB_TB6612_H */
