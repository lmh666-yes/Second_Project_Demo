#ifndef __FWLIB_TB6612_H
#define __FWLIB_TB6612_H

#include "stm32f4xx.h"
#include "sys_tim.h"

/* ================================================================
 *  tb6612.h —— 【电机】TB6612FNG 双路直流电机驱动（4WD 小车底盘）
 * ================================================================
 *  设计定位 : 把"AIN1/AIN2 真值表 + STBY 使能 + 左右差速"这些硬件细节
 *             收进来，对外只留"往哪走、多快"两件事。
 *             面向【智能避障遥控小车】—— 前进/后退/左转/右转/停止/原地转。
 *  依赖     : sys_tim.h（PWM 调速）、gpio_core.h（方向脚）
 *  标准库关键词 : RCC_AHB1PeriphClockCmd / GPIO_Init / TIM_OCxInit /
 *                 TIM_SetCompareX（后两者经 sys_tim 封装）
 *
 *  【TB6612FNG 是什么】
 *      一片 TB6612 = **2 个通道**（A / B），每个通道：
 *        · AIN1 / AIN2 两个方向脚 —— 决定正转还是反转
 *        · PWMA 一路 PWM        —— 决定转速（占空比）
 *      4WD 小车把【左边两个马达并联接 A 通道、右边两个并联接 B 通道】，
 *      于是"左轮速度 / 右轮速度"两个量就能表示所有动作：
 *        左=右 且 >0  → 直行      左>右 → 右转
 *        左=右 且 <0  → 后退      左<右 → 左转
 *        左=-右      → 原地转圈
 *      ⚠ 所以本模块**做不了四轮独立**（要四轮独立得用两片 TB6612，共 4 通道）。
 *
 *  【真值表（片内逻辑，本模块已封装，了解即可）】
 *      AIN1  AIN2   PWMA        结果
 *       1     0     PWM     正转（占空比 = 速度）
 *       0     1     PWM     反转
 *       0     0     任意    停止（滑行 —— 电机两端悬空，车会滑一段）
 *       1     1     任意    刹车（短路制动 —— 立刻停，别长时间用会发热）
 *      STBY = 0 → 整片待机（两路都停），省电用；正常跑要拉高
 *
 *  【接线（P2 排针；★这些引脚在"不用彩屏/不用外扩SRAM"时空闲）】
 *      TB6612          F407         P2 脚号
 *      ────────────────────────────────────
 *      PWMA  ───────►  PB6 (TIM4_CH1)    3
 *      PWMB  ───────►  PB7 (TIM4_CH2)    2
 *      AIN1  ───────►  PG2              41
 *      AIN2  ───────►  PG3              40
 *      BIN1  ───────►  PG4              39
 *      BIN2  ───────►  PG5              38
 *      STBY  ───────►  PG13              9
 *      VCC   ───────►  3.3V  （逻辑电源，从板子取）
 *      VM    ───────►  7.4V  （电池正极，**别接 5V，扭矩不够**）
 *      GND   ───────►  GND   （★必须与电池、开发板共地）
 *
 *  【使用方式】
 *      TB6612_Init();
 *      TB6612_Car(TB6612_CAR_FWD, 600);      // 前进，60% 速度
 *      delay_ms(1000);
 *      TB6612_CarTank(500, -500);            // 原地转圈（左正右反）
 *      delay_ms(500);
 *      TB6612_Stop();
 *
 *  移植指引 : 换引脚只改下面区块 1（7 个引脚 + 1 个定时器）；
 *             换电机转速手感改 TB6612_PWM_FREQ_HZ 与速度上限宏；
 *             要四路独立 → 自己再包一层，本模块结构照抄即可。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换接线只改这里）
 * ================================================================ */

/* ---- 通道 A（左轮）：PWM 脚 ---- */
#define TB6612_PWMA_PORT    GPIOB
#define TB6612_PWMA_PIN     GPIO_Pin_6      /* P2-3 */
#define TB6612_PWMA_TIM     SYS_TIM_4
#define TB6612_PWMA_CH      1
#define TB6612_PWMA_AF      GPIO_AF_TIM4

/* ---- 通道 A（左轮）：方向脚 ---- */
#define TB6612_AIN1_PORT    GPIOG
#define TB6612_AIN1_PIN     GPIO_Pin_2      /* P2-41 */
#define TB6612_AIN2_PORT    GPIOG
#define TB6612_AIN2_PIN     GPIO_Pin_3      /* P2-40 */

/* ---- 通道 B（右轮）：PWM 脚 ---- */
#define TB6612_PWMB_PORT    GPIOB
#define TB6612_PWMB_PIN     GPIO_Pin_7      /* P2-2 */
#define TB6612_PWMB_TIM     SYS_TIM_4
#define TB6612_PWMB_CH      2
#define TB6612_PWMB_AF      GPIO_AF_TIM4

/* ---- 通道 B（右轮）：方向脚 ---- */
#define TB6612_BIN1_PORT    GPIOG
#define TB6612_BIN1_PIN     GPIO_Pin_4      /* P2-39 */
#define TB6612_BIN2_PORT    GPIOG
#define TB6612_BIN2_PIN     GPIO_Pin_5      /* P2-38 */

/* ---- 待机脚（高 = 工作，低 = 整片待机） ---- */
#define TB6612_STBY_PORT    GPIOG
#define TB6612_STBY_PIN     GPIO_Pin_13     /* P2-9 */

/* ---- 调速参数 ---- */
/* PWM 频率：TT 马达建议 1k~5kHz。
 *   太低（<500Hz）→ 电机会"嗡嗡"叫、低速抖动；
 *   太高（>10kHz）→ 铁损大、低速扭矩变差。
 * ⚠ 两个通道必须用**同一个频率**（同一路定时器时天然满足） */
#define TB6612_PWM_FREQ_HZ  2000UL

/* 速度上限（千分比）：给"别开满"留个安全垫。
 * TT 马达在 7.4V 满占空比已经很快，建议先用 700 试 */
#define TB6612_SPEED_MAX    1000U

/* 左转/右转时内侧轮保留多少速度（越小转得越急）
 * 例: 500 → 左转时左轮 500‰、右轮 1000‰（按传入速度等比缩） */
#define TB6612_TURN_INNER   400U


/* ================================================================
 *                        区块 2：基础功能
 * ================================================================ */

/* 电机通道 */
#define TB6612_A            0U      /* 通道 A —— 左轮 */
#define TB6612_B            1U      /* 通道 B —— 右轮 */

/* 单电机方向 */
#define TB6612_DIR_STOP     0U      /* 停止（滑行，车会溜一段） */
#define TB6612_DIR_FWD      1U      /* 正转 */
#define TB6612_DIR_BACK     2U      /* 反转 */
#define TB6612_DIR_BRAKE    3U      /* 刹车（短路制动，立刻停；别长期用） */

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

/* 【初始化】配好 2 路 PWM + 5 个方向脚，并解除待机
 * 返回 : TB6612_OK
 * 说明 : 内部会把 STBY 拉高（否则怎么设 PWM 电机都不动 —— 忘了解除待机
 *        是"接线全对但电机纹丝不动"最常见的原因）
 * 示例 : TB6612_Init(); */
uint8_t TB6612_Init(void);

/* 【设某一路电机的方向和速度】★最底层，其余函数都建在它上面
 * 参数 : ch    —— TB6612_A（左轮）或 TB6612_B（右轮）
 *        dir   —— TB6612_DIR_STOP / FWD / BACK / BRAKE
 *        speed —— 速度千分比 0~1000（1000 = 100% 占空比）
 * 返回 : TB6612_OK / TB6612_ERR_PARAM
 * 示例 : TB6612_SetMotor(TB6612_A, TB6612_DIR_FWD, 600);   // 左轮 60% 正转 */
uint8_t TB6612_SetMotor(uint8_t ch, uint8_t dir, uint16_t speed);

/* 【停车】两路都停（滑行），STBY 保持高（下次还能立刻跑）
 * 示例 : TB6612_Stop(); */
void TB6612_Stop(void);

/* 【刹车】两路短路制动，立刻停住（急停用；长时间通电电机会发热）
 * 示例 : TB6612_Brake(); */
void TB6612_Brake(void);

/* 【待机控制】on=1 解除待机（正常工作）；on=0 整片待机（两路断电、最省电）
 * 说明 : 长时间不用就 Standby(0)，比 Stop 更省电（芯片内部也断了）
 * 示例 : TB6612_Standby(0);   // 收工 */
void TB6612_Standby(uint8_t on);


/* ================================================================
 *                        区块 3：扩展功能
 * ================================================================ */

/* 【左右轮独立给定】★小车运动学的核心接口
 * 参数 : left / right —— **带符号**速度，正 = 前进，负 = 后退，0 = 停
 *                        取值范围 -1000 ~ +1000
 * 说明 : 所有动作都能用它表示 —— 这就是"差速转向"：
 *          直行 (600, 600) ｜ 后退 (-600, -600)
 *          右转 (800, 400) ｜ 左转 (400, 800)
 *          原地左转 (-500, 500) ｜ 原地右转 (500, -500)
 * 示例 : TB6612_CarTank(600, 600);   // 直行 */
void TB6612_CarTank(int16_t left, int16_t right);

/* 【按"动作"开车】不想算左右差速就用这个
 * 参数 : action —— TB6612_CAR_STOP / FWD / BACK / LEFT / RIGHT / SPIN_L / SPIN_R
 *        speed  —— 外侧轮速度千分比 0~1000
 * 返回 : TB6612_OK / TB6612_ERR_PARAM
 * 说明 : 左转/右转时内侧轮 = speed × TB6612_TURN_INNER / 1000
 * 示例 : TB6612_Car(TB6612_CAR_LEFT, 700);   // 左转 */
uint8_t TB6612_Car(uint8_t action, uint16_t speed);

/* 【速度斜坡】从"当前速度"平滑过渡到"目标速度"，避免突然启动打滑/掉电
 * 参数 : target_l / target_r —— 目标左右速度（带符号）
 *        step               —— 每毫秒变化多少（千分比/ms），建议 5~20
 * 返回 : 走到位返回 1，还在过渡中返回 0
 * 说明 : 需要在循环里反复调，直到返回 1。典型用法：
 *          while (!TB6612_CarRamp(600, 600, 10)) { delay_ms(5); }
 *        ⚠ 每 1ms 调一次才准；调得比 1ms 慢就按实际间隔折算 step
 * 示例 : TB6612_CarRamp(600, 600, 10);   // 10ms 内从 0 加到 600 */
uint8_t TB6612_CarRamp(int16_t target_l, int16_t target_r, uint16_t step);

/* 【读回当前左右速度】调试 / 上位机显示用
 * 参数 : left / right —— 出参，可传 0 表示不要
 * 示例 : int16_t l, r; TB6612_GetSpeed(&l, &r); */
void TB6612_GetSpeed(int16_t *left, int16_t *right);

/* 【返回码转中文说明】调试打印用 */
const char *TB6612_ErrStr(uint8_t err);

#endif /* __FWLIB_TB6612_H */
