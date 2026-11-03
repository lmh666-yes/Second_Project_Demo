#ifndef __FWLIB_ULN2003_H
#define __FWLIB_ULN2003_H

#include "stm32f4xx.h"

/* uln2003.h: 步进电机驱动（ULN2003D + 5 线减速步进）头文件
 *
 * 功能: 提供正转 / 反转 / 走 N 步 / 停 的接口；引脚操作只用 gpio_core 的普通
 *       GPIO 推挽输出，对应标准库 GPIO_Init / GPIO_SetBits / GPIO_ResetBits，
 *       不占用定时器
 *
 * 硬件接线（依原理图核对，U10 = ULN2003D，7 路达林顿驱动阵列，不是 H 桥）:
 *   U10 IN1~IN4 ← 网络 MOTO_IN1~MOTO_IN4；OUT1~OUT4 → 网络 OUT1~OUT4
 *   U10 COM(9 脚) 接 5V，步进电机公共端同样接 5V
 *   J11（5 脚，丝印 5V / 01 / 02 / 03 / 04）插电机: 5V 接 28BYJ-48 红线，
 *       01~04 接其余四根相线
 *   CN2（4 脚，丝印 T1 / T2 / T3 / T4）为控制输入，即 MOTO_IN1~MOTO_IN4
 * 约束: CN2 未连到 MCU 引脚，需用杜邦线从扩展排针 P9 取 4 个空闲 IO 接到 T1~T4；
 *       下面 4 个引脚宏按实际接线填写，默认 PC1~PC4；
 *       本板未占用的脚: PA1 / PA7 / PB5 / PB12 / PB13 / PC1~PC5 / PC12 /
 *                       PD3 / PG11 / PG13 / PG14
 * 驱动方式: 四相八拍 A-AB-B-BC-C-CD-D-DA，相邻两步只切一相，比四相四拍平滑；
 *       两相同时通电时力矩最大
 * 换电机: 按步距角与减速比算步/圈，改 ULN2003_STEPS_PER_REV 与 4 个引脚宏
 */


/* 区块 1: 定义与宏定义 */
/* 0 = 不编译本模块 */
#ifndef ULN2003_ENABLE
#define ULN2003_ENABLE      1
#endif

/* 控制引脚：按实际接到 CN2 的线修改 */
/* 顺序必须与 CN2 的 T1/T2/T3/T4 一一对应 */
#define MOTOR_IN1_PORT      GPIOC
#define MOTOR_IN1_PIN       GPIO_Pin_1
#define MOTOR_IN2_PORT      GPIOC
#define MOTOR_IN2_PIN       GPIO_Pin_2
#define MOTOR_IN3_PORT      GPIOC
#define MOTOR_IN3_PIN       GPIO_Pin_3
#define MOTOR_IN4_PORT      GPIOC
#define MOTOR_IN4_PIN       GPIO_Pin_4

/* 驱动方式 */
/* 1 = 四相八拍，序列 A-AB-B-BC-C-CD-D-DA，相邻两步只切一相，步距减半、运转平滑
 * 0 = 四相四拍，序列 A-B-C-D，力矩略小 */
#define ULN2003_MODE_8BEAT  1

/* 每转步数（28BYJ-48 减速步进） */
/* 28BYJ-48 步距角 5.625°/64，内置 1:64 减速
 * 八拍步数 = 360 / (5.625/64) / 2 = 2048 步/圈，常用取值 2038，差值来自齿轮间隙
 * 换电机: 步/圈 = 360 / 步距角 * 减速比 */
#define ULN2003_STEPS_PER_REV   2038U

/* 每步间隔（微秒） */
/* 一步走完后等待的时间；过小电机来不及响应会丢步、发出嗡声，过大转速下降
 * 常用 1000~3000us，约 300~1000 步/秒
 * ULN2003_Step 在此阻塞等待；需边转边做别的事时改用 ULN2003_OneStep 自行定时调用 */
#define ULN2003_STEP_DELAY_US   1500U

/* 通电/断电的极性 */
/* ULN2003 输入高时对应输出拉到 GND，为灌电流驱动
 * 本模块按通电 = 对应 IN 脚给高电平 1 设计；接线相反时改成 0 */
#define ULN2003_ACTIVE_HIGH     1


/* 区块 2: 基础功能 */
/* 初始化 4 个控制脚（推挽输出 GPIO_OType_PP），并把四相全部断电
 * 经 gpio_core 的 GPIO_ClockEnable / GPIO_OutInit，对应标准库
 * RCC_AHB1PeriphClockCmd + GPIO_Init */
void ULN2003_Init(void);

/* 走指定步数：阻塞，整段走完才返回
 * 参数: steps 步数，1 ~ 约 4 亿，传 0 直接返回；dir 1 = 正转(CCW)，0 = 反转(CW)
 * 每步间隔取运行时值，初始为 ULN2003_STEP_DELAY_US
 * 返回前不自动断电，保持最后一相通电与保持力矩；需断电再调 ULN2003_Stop */
void ULN2003_Step(uint32_t steps, uint8_t dir);

/* 断电停车：四相全部断电，转子失去保持力矩，可手动转动 */
void ULN2003_Stop(void);

/* 四相全部通电：保持力矩最大，发热也最大，不宜长时间使用 */
void ULN2003_Hold(void);


/* 区块 3: 扩展功能 */
/* 只走一步后立即返回，不含延时
 * 参数: dir 1 = 正转，0 = 反转
 * 非阻塞用法: 放进 sys_tick / sys_tim 周期回调，按 ULN2003_STEP_DELAY_US 间隔调用 */
void ULN2003_OneStep(uint8_t dir);

/* 返回当前相位序号：八拍模式 0 ~ 7，四拍模式 0 ~ 3 */
uint8_t ULN2003_GetPhase(void);

/* 相位计数清零：不改变线圈通断，只让软件序号回到 0
 * 换向或重新起转前调用，避免第一步方向异常 */
void ULN2003_ResetPhase(void);

/* 设定每步延时（运行时值，替换默认宏 ULN2003_STEP_DELAY_US）
 * 参数: us 单位微秒，范围 100 ~ 20000，超出按上下限截断 */
void ULN2003_SetStepDelay(uint32_t us);
uint32_t ULN2003_GetStepDelay(void);

#endif /* __FWLIB_ULN2003_H */
