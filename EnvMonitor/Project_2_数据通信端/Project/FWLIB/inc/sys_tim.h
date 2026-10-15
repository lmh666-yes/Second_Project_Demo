#ifndef __FWLIB_SYS_TIM_H
#define __FWLIB_SYS_TIM_H

#include "stm32f4xx.h"

/* sys_tim.h: 通用定时器(TIM)模块头文件
 *
 * 覆盖五类用法:
 *   1) PWM 输出: 直流电机调速 / 舵机 / 无源蜂鸣器 / 调光
 *   2) 固定周期中断: 周期任务、数据采样
 *   3) 外部脉冲计数(ETR): 按键/信号计数、外部时钟源
 *   4) 输入捕获(IC): 测脉冲宽度 / 周期 / 按键按下时长
 *   5) 输出比较(OC): 翻转输出方波 / 冻结模式做比较中断（PWM 是它的两个预设模式）
 * 支持 TIM1 ~ TIM14 全部 14 个定时器。
 *   PWM 输出: TIM1~5 / TIM8~14；TIM6/7 是基本定时器，只能用于定时中断
 *   通道数: TIM1~5/8 四条、TIM9/12 两条、TIM10/11/13/14 一条
 *
 * 引脚不写死: PWM 初始化时由参数指定端口 / 引脚 / 复用号，复用号速查见
 * gpio_core.h 文末附录，权威依据是数据手册复用功能表。只有物理上支持该定时器
 * 通道的引脚才能输出 PWM，例如 PA0 支持 TIM5_CH1（复用号 GPIO_AF_TIM5）。
 * 定时器中断服务函数在本模块 .c 内为弱定义，TIM1/8~14 用共享中断向量。
 * 正常用法是通过 SYS_TIM_InitIT 注册回调；自己手写同名 TIMx_IRQHandler 会顶替
 * 库版，该定时器的库回调随之停用。
 */


/* -------------------- 中断优先级 -------------------- */
/* 数值越小优先级越高；可用范围随 sys_nvic 的分组变化:
 *   NVIC_PriorityGroup_4（本库默认）: 抢占 0~15、子固定 0
 *   NVIC_PriorityGroup_2（纯裸机）  : 抢占 0~3、子 0~3
 *
 * 用 FreeRTOS 时抢占值必须 ≥5（默认已给 5）:
 *    FreeRTOS 用 BASEPRI = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY<<4
 *    （本工程 = 5<<4 = 0x50）屏蔽允许调 FromISR 的中断。
 *    抢占值 < 5 的中断能打断内核临界区，禁止在其中调用任何 ...FromISR() 接口;
 *    开了 configASSERT 会断言失败。想更急只能取 5，想更缓取 6~15 皆可。 */
#define SYS_TIM_IRQ_PRE_PRIO   5
#define SYS_TIM_IRQ_SUB_PRIO   0

/* -------------------- 舵机参数（区块 3 使用） -------------------- */
#define SYS_TIM_SERVO_FREQ_HZ  50       /* 标准舵机: 50Hz（20ms 周期） */
#define SYS_TIM_SERVO_MIN_US   500      /*   0° → 脉宽 0.5ms */
#define SYS_TIM_SERVO_MAX_US   2500     /* 180° → 脉宽 2.5ms */

/* -------------------- ETR 外部脉冲计数（区块 3 使用） -------------------- */
/* ETR 输入滤波强度（0x0 ~ 0xF）：越大滤抖越强
 * 机械按键类信号建议 0x07 ~ 0x0F；干净数字信号可用 0x00（不滤） */
#define SYS_TIM_ETR_FILTER   0x0F

/* ETR 计数时钟走哪条通路（取值 1 或 2，默认 2），两个标准库函数:
 *   TIM_ETRClockMode2Config（库默认）: SMCR 的 ECE 位置 1，ETR 直通计数;
 *   TIM_ETRClockMode1Config: SMCR 的 SMS=111 + TS=ETRF，ETR 经触发控制器进来，
 *                            会占用从模式选择位。
 * 两种都能让 ETR 引脚上的每个脉冲驱动计数，本库场景等价；Mode2 只占 ECE 位。 */
#define SYS_TIM_ETR_CLKMODE   2

/* -------------------- 输入捕获（区块 3 使用） -------------------- */
/* 捕获输入的数字滤波强度（0x0 ~ 0xF）：连续采样一致才认边沿,越大抗抖越强
 * 机械按键/慢信号建议 0x07 ~ 0x0F；干净数字信号可用 0x00（不滤） */
#define SYS_TIM_IC_FILTER   0x0F


/* -------------------- 区块 2: 基础功能 -------------------- */
/* 定时器编号 */
typedef enum {
    SYS_TIM_1  = 0,
    SYS_TIM_2  = 1,
    SYS_TIM_3  = 2,
    SYS_TIM_4  = 3,
    SYS_TIM_5  = 4,
    SYS_TIM_6  = 5,
    SYS_TIM_7  = 6,
    SYS_TIM_8  = 7,
    SYS_TIM_9  = 8,
    SYS_TIM_10 = 9,
    SYS_TIM_11 = 10,
    SYS_TIM_12 = 11,
    SYS_TIM_13 = 12,
    SYS_TIM_14 = 13,
    SYS_TIM_COUNT = 14
} SysTimId_t;

/* PWM 初始化（初始占空比 0）
 *
 * 库内部依次调用:
 *   1) RCC_APB1/2PeriphClockCmd  开定时器时钟（TIM1/9 在 APB2，否则 APB1）
 *   2) GPIO_PinAFConfig + GPIO_Init  引脚复用为定时器通道输出（GPIO_Mode_AF + GPIO_OType_PP）
 *   3) TIM_TimeBaseInit          时基（自动换算 PSC / ARR）
 *   4) TIM_OC1~4Init + PreloadConfig  PWM1 模式（TIM_OCMode_PWM1）+ 预装载
 *   5) TIM_CtrlPWMOutputs        开主输出（仅 TIM1 需要）
 *   6) TIM_Cmd                   启动计数
 *
 * 参数 : id      : 定时器编号，SYS_TIM_1 ~ SYS_TIM_14 任一
 *                   （TIM6/TIM7 是基本定时器、没有输出通道，会直接返回）
 *        ch      : 通道号 1 ~ 4（物理上限: TIM1~5/8 四条、TIM9/12 两条、
 *                   TIM10/11/13/14 一条）
 *        port/pin/af : PWM 输出引脚;af(复用号)按定时器选:
 *                       TIM1→GPIO_AF_TIM1    TIM2→GPIO_AF_TIM2
 *                       TIM3→GPIO_AF_TIM3    TIM4→GPIO_AF_TIM4
 *                       TIM5→GPIO_AF_TIM5    TIM8→GPIO_AF_TIM8
 *                       TIM9→GPIO_AF_TIM9    TIM10→GPIO_AF_TIM10
 *                       TIM11→GPIO_AF_TIM11  TIM12→GPIO_AF_TIM12
 *                       TIM13→GPIO_AF_TIM13  TIM14→GPIO_AF_TIM14
 *        freq_hz : PWM 频率（Hz），建议 1Hz ~ 1MHz */
void SYS_TIM_PwmInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                     uint8_t af, uint32_t freq_hz);

/* 设置占空比：permille 为千分比 0 ~ 1000（500 = 50%），超范围自动截断
 * 标准库 : TIM_SetCompare1~4（按通道号自动选） */
void SYS_TIM_PwmSetDuty(SysTimId_t id, uint8_t ch, uint16_t permille);

/* 停止该通道 PWM（占空比归零；同一定时器其它通道不受影响）
 * 标准库 : 与 PwmSetDuty 同路径，TIM_SetCompareX 写 0 */
void SYS_TIM_PwmStop(SysTimId_t id, uint8_t ch);

/* 读取当前占空比（千分比 0~1000）
 * 用途 : PwmStop 会把占空比归零，想之后恢复就先读出来存着
 * 标准库 : TIM_GetCapture1~4（读 CCR 寄存器值） */
uint16_t SYS_TIM_PwmGetDuty(SysTimId_t id, uint8_t ch);

/* 运行中改 PWM 频率：各通道 CCR 等比缩放，占空比保持不变
 * 说明 : 1) 需先 PwmInit 且定时器已启动;TIM6/7 或未启动时直接返回;
 *        2) PSC/ARR 重算后立即生效（不等下一个周期）;
 *        3) 该定时器若另有通道正作输入捕获，不要调用（缩放会读一遍
 *           CCR、顺带清掉捕获标志）;freq_hz = 0 直接返回（停输出用 PwmStop）
 * 标准库 : PSC/ARR 直写 + TIM_GenerateEvent（TIM_EventSource_Update） */
void SYS_TIM_PwmSetFreq(SysTimId_t id, uint32_t freq_hz);

/* 定时中断初始化：每 1/freq_hz 秒自动执行一次 callback
 *
 * 库内部依次调用:
 *   1) RCC_APB1/2PeriphClockCmd   开定时器时钟
 *   2) TIM_TimeBaseInit           时基（自动换算 PSC / ARR）
 *   3) TIM_ITConfig               开更新中断（TIM_IT_Update）
 *   4) NVIC_Init                  使能中断向量 + 优先级（宏见区块 1）
 *   5) TIM_Cmd                    启动计数
 *   中断内自动完成: TIM_GetITStatus → TIM_ClearITPendingBit → callback
 *
 * 参数 : id      : 定时器编号，SYS_TIM_1 ~ SYS_TIM_14 任一（全部 14 个都支持）
 *        freq_hz : 中断频率(Hz): 1 = 每秒 1 次, 2 = 每秒 2 次, 1000 = 每毫秒 1 次
 *        callback : 中断回调（中断上下文执行，保持短小）
 * 注意 : 回调无参数，要区分来源就每个用途单独写一个回调 */
void SYS_TIM_InitIT(SysTimId_t id, uint32_t freq_hz, void (*callback)(void));

/* 停止定时器（停止计数，中断模式与 PWM 模式都可用）
 * 标准库 : TIM_Cmd(DISABLE) + TIM_ITConfig(TIM_IT_Update, DISABLE) */
void SYS_TIM_Stop(SysTimId_t id);

/* ---- ADC 采样节拍（TRGO 定时触发输出）---- */

/* 把定时器配成"ADC 触发源"：每个周期输出一次 TRGO(更新事件)
 * 特点 : 不占引脚、不进中断，给"定时器触发 ADC"当节拍器
 *        （配 sys_adc 的 SYS_ADC_ExtTrigScanInit 使用）
 * 说明 : 只有 TIM2 / TIM3 / TIM8 的 TRGO 接到了 ADC 触发选择器,
 *        其它定时器调用本函数会直接返回（不做任何事）;
 *        本函数会接管该定时器（重配时基并启动计数）,
 *        不要再与 PWM / 中断 / 捕获等用法共用同一个定时器
 * 标准库调用链 : 时基(PSC/ARR 直写) → TIM_SelectOutputTrigger(TIM_TRGOSource_Update)
 *                → TIM_Cmd 启动（详细见 .c）
 * 参数 : freq_hz : 触发频率(Hz),即 ADC 的采样率;建议 1Hz ~ 1MHz */
void SYS_TIM_TrgoInit(SysTimId_t id, uint32_t freq_hz);


/* -------------------- 区块 3: 扩展功能 -------------------- */
/* ---- 舵机（直接按角度控制） ---- */

/* 初始化舵机通道：50Hz，上电自动停在 90° 位置 */
void SYS_TIM_ServoInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin, uint8_t af);

/* 设置舵机角度：angle 为 0 ~ 180（度），超范围自动截断 */
void SYS_TIM_ServoSetAngle(SysTimId_t id, uint8_t ch, uint16_t angle);

/* ---- 无源蜂鸣器 / 方波发声 ---- */

/* 初始化发声通道（上电静音，占空比 0） */
void SYS_TIM_ToneInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin, uint8_t af);

/* 播放指定频率方波（50% 占空比）：freq_hz = 0 等效停止 */
void SYS_TIM_TonePlay(SysTimId_t id, uint8_t ch, uint32_t freq_hz);

/* 停止发声 */
void SYS_TIM_ToneStop(SysTimId_t id, uint8_t ch);

/* 播放指定频率 + 占空比（‰），无源蜂鸣器调音量/音色用
 * 说明 : 频率与占空比一次设置（PwmSetFreq + PwmSetDuty 的组合调用）;
 *        duty_permille 0~1000,常用 500(50%) / 100(轻) / 900(重)
 * 参数 : freq_hz : 目标频率(0 = 停止);duty_permille : 占空比千分比
 * 标准库 : TIM_SetAutoreload + TIM_SetCompareX（经 PwmSetFreq/SetDuty） */
void SYS_TIM_TonePlayDuty(SysTimId_t id, uint8_t ch, uint32_t freq_hz, uint16_t duty_permille);

/* ---- 外部脉冲计数（ETR 外部时钟,通路由宏选）----
 * 定时器把指定引脚上来的每个脉冲当作计数时钟。
 * 用途:按键次数计数 / 外部信号计数 / 低频脉冲计量
 * 计数时钟通路有 TIM_ETRClockMode1Config / TIM_ETRClockMode2Config 两种,
 * 由区块 1 的 SYS_TIM_ETR_CLKMODE 选 */

/* 外部脉冲计数初始化（纯计数，无中断）
 * 库内部依次调用:
 *   1) RCC_APB1/2PeriphClockCmd  开定时器时钟
 *   2) GPIO_PinAFConfig + GPIO_Init  引脚复用为 TIMx_ETR（GPIO_Mode_AF + 上拉 GPIO_PuPd_UP）
 *   3) TIM_TimeBaseInit          时基（ARR = period_n-1,数满一轮归零）
 *   4) TIM_ETRClockMode1Config / TIM_ETRClockMode2Config  ETR 外部时钟模式
 *      （由区块 1 宏 SYS_TIM_ETR_CLKMODE 选,默认 Mode2）+ 滤波
 *   5) TIM_Cmd                   启动
 * 参数 : id       : 定时器编号（ETR 引脚是芯片固定映射,先查数据手册）
 *        port/pin : ETR 输入引脚（如 TIM2 用 GPIOA/GPIO_Pin_0）
 *        af       : 复用号（TIM2→GPIO_AF_TIM2）
 *        period_n : 每收满多少个脉冲算"一轮"（1 ~ 65536）
 * 提醒 : PA0 是本板 KEY1,用它做 ETR 时按键的轮询/EXTI 方式二选一;
 *        滤波强度由区块 1 的 SYS_TIM_ETR_FILTER 决定 */
void SYS_TIM_EtrInit(SysTimId_t id, GPIO_TypeDef *port, uint16_t pin,
                     uint8_t af, uint32_t period_n);

/* 外部脉冲计数初始化 + 中断回调：每收满 period_n 个脉冲自动执行一次 callback
 * 说明 : 与 SYS_TIM_EtrInit 唯一区别是多开"更新中断"并注册回调 */
void SYS_TIM_EtrInitIT(SysTimId_t id, GPIO_TypeDef *port, uint16_t pin,
                       uint8_t af, uint32_t period_n, void (*callback)(void));

/* 读"本轮已收到的脉冲数"（0 ~ period_n-1；数满归零重数）
 * 标准库 : TIM_GetCounter（读 CNT 寄存器） */
uint32_t SYS_TIM_EtrCount(SysTimId_t id);

/* 清零脉冲计数
 * 标准库 : TIM_SetCounter（写 CNT 寄存器） */
void SYS_TIM_EtrReset(SysTimId_t id);


/* ---- 输入捕获（Input Capture，信道作输入用）----
 * 原理 : 边沿到来瞬间硬件把当前 CNT 复制进 CCRx 并置"捕获到"标志,
 *        两次捕获值之差 ÷ tick_hz 就是两点之间的时间。
 * 用途 : 测脉冲宽度（按键按下时长、红外解码）/ 测周期与频率 / 配合换沿测占空比 */

/* 输入捕获初始化（纯轮询,无中断）
 * 库内部依次调用:
 *   1) RCC_APB1/2PeriphClockCmd  开定时器时钟（+ 经 gpio_core 开引脚时钟）
 *   2) GPIO_PinAFConfig + GPIO_Init  引脚复用为 TIMx_CHx 输入（GPIO_Mode_AF + 上拉 GPIO_PuPd_UP）
 *   3) TIM_TimeBaseInit          时基（PSC 按 tick_hz 换算;ARR = 0xFFFF 满量程）
 *   4) TIM_ICInit                信道配成"输入捕获"（TIM_ICSelection_DirectTI / TIM_ICPSC_DIV1 + 滤波宏）
 *   5) TIM_Cmd                   启动
 * 参数 : id   : 定时器编号（TIM6/7 没有信道,会直接返回）
 *        ch   : 信道号 1 ~ 4
 *        port/pin/af : 捕获输入引脚(如 TIM5_CH1 用 GPIOA / GPIO_Pin_0 / GPIO_AF_TIM5)
 *        polarity : 捕哪个边沿: TIM_ICPolarity_Rising / _Falling / _BothEdge
 *        tick_hz  : 计数频率(Hz):捕获值 ÷ tick_hz = 秒（例 10000 → 1 个计数 = 0.1ms）
 * 提醒 : tick_hz 有效范围 ≥ 定时器时钟/65536（84MHz 时约 1282Hz）;
 *        满量程 = 65536 ÷ tick_hz 秒,计满从 0 重数;
 *        滤波强度由区块 1 的 SYS_TIM_IC_FILTER 决定 */
void SYS_TIM_CaptureInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                         uint8_t af, uint16_t polarity, uint32_t tick_hz);

/* 输入捕获初始化 + 捕获中断回调：每捕到一个边沿 → callback(捕获值)
 * 说明 : 与 SYS_TIM_CaptureInit 唯一区别是多开"捕获中断"并注册回调;
 *        回调参数就是本次捕获到的计数值（中断上下文,保持短小）;
 *        TIM1/TIM8 的捕获中断在单独的 _CC 向量上（库已处理,无需关心） */
void SYS_TIM_CaptureInitIT(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                           uint8_t af, uint16_t polarity, uint32_t tick_hz,
                           void (*callback)(uint32_t value));

/* 查"捕获到了吗"（CCxIF 标志）:1 = 有新捕获值
 * 标准库 : TIM_GetFlagStatus（查 TIM_FLAG_CC1~4） */
uint8_t SYS_TIM_CaptureFlag(SysTimId_t id, uint8_t ch);

/* 读捕获值（CCRx）
 * 标准库 : TIM_GetCapture1~4
 * 注意 : 读 CCR 会顺带清掉"捕获到"标志（硬件行为）,
 *        轮询流程 = CaptureFlag 判断 → CaptureGet 取值(并自动清零) */
uint32_t SYS_TIM_CaptureGet(SysTimId_t id, uint8_t ch);

/* 清"捕获到"标志（不读值时单独清,如丢弃一次干扰）
 * 标准库 : TIM_ClearFlag */
void SYS_TIM_CaptureClear(SysTimId_t id, uint8_t ch);

/* 动态切换捕获边沿（Rising ↔ Falling,常用于"降沿开始/升沿结束"测脉宽）
 * 标准库 : TIM_OC1~4PolarityConfig（写 CCER 的 CCxP 位,捕获/比较共用）
 * 注意 : 只能填 TIM_ICPolarity_Rising / _Falling;BothEdge 请在 CaptureInit 时配 */
void SYS_TIM_CaptureSetPolarity(SysTimId_t id, uint8_t ch, uint16_t polarity);


/* ---- 输出比较（Output Compare，信道作输出/比较用）----
 * 原理 : CNT 数到与 CCR 相同那一刻是"匹配事件",六种模式决定匹配时干什么:
 *        TIM_OCMode_Timing(冻结,只置标志/中断) / _Active(强置高) /
 *        _Inactive(强置低) / _Toggle(翻转) / _PWM1 / _PWM2。
 * 用途 : 翻转模式输出精确方波 / 冻结模式当"比较中断软定时器" /
 *        其它引脚波形实验（PWM 只是六种模式里的两种,要调占空比请用 PwmInit） */

/* 输出比较初始化（六种模式任选）
 * 库内部依次调用:
 *   1) RCC_APB1/2PeriphClockCmd  开定时器时钟（+ 经 gpio_core 开引脚时钟）
 *   2) GPIO_PinAFConfig + GPIO_Init  引脚复用推挽输出（GPIO_Mode_AF + GPIO_OType_PP；不需要引脚时 port 传 0）
 *   3) TIM_TimeBaseInit          时基（PSC/ARR 按 cycle_hz 换算,同 PWM 算法）
 *   4) TIM_OCInit                输出比较:模式/极性/比较值
 *   5) TIM_ARRPreloadConfig + TIM_Cmd  启动（TIM1/8 另开 MOE 主输出）
 * 参数 : id   : 定时器编号（TIM6/7 没有信道,会直接返回）
 *        ch   : 信道号 1 ~ 4
 *        port/pin/af : 输出引脚;纯"比较事件/中断"不用引脚时 port 传 0
 *        oc_mode : 六选一（标准库宏）:
 *                   TIM_OCMode_Timing   冻结:不碰引脚,只置匹配标志/中断
 *                   TIM_OCMode_Active   匹配瞬间置"有效"电平
 *                   TIM_OCMode_Inactive 匹配瞬间置"无效"电平
 *                   TIM_OCMode_Toggle   匹配瞬间翻转,最经典的"输出比较"
 *                   TIM_OCMode_PWM1 / _PWM2   即 PWM（建议改用 PwmInit+SetDuty）
 *        cycle_hz : 计数一轮(ARR+1 个数)的频率:匹配事件每秒发生 cycle_hz 次
 *        ccr  : 匹配点在本轮中的位置(0 ~ 一轮计数次数):决定相位/翻转点
 * 提醒 : 翻转模式输出方波频率 = cycle_hz ÷ 2（每次匹配翻转一次,
 *        两次一个周期,占空比恒 50%）;要调占空比用 PwmInit */
void SYS_TIM_OcInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                    uint8_t af, uint16_t oc_mode, uint32_t cycle_hz, uint32_t ccr);

/* 输出比较初始化 + 比较中断回调：每次"匹配事件"自动执行一次 callback
 * 说明 : 与 SYS_TIM_OcInit 唯一区别是多开"比较中断"并注册回调;
 *        "冻结模式 + 本中断" = 一个不占引脚的软定时器;
 *        TIM1/TIM8 的比较中断在单独的 _CC 向量上（库已处理,无需关心） */
void SYS_TIM_OcInitIT(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                      uint8_t af, uint16_t oc_mode, uint32_t cycle_hz, uint32_t ccr,
                      void (*callback)(void));

/* 改比较值（CCRx,决定相位/翻转点/占空比）
 * 标准库 : TIM_SetCompare1~4 */
void SYS_TIM_OcSetCompare(SysTimId_t id, uint8_t ch, uint32_t ccr);

/* 运行中改"一轮频率"（cycle_hz）：PSC/ARR 立即重算、CCR 等比缩放
 * 说明 : 翻转模式输出方波频率 = cycle_hz ÷ 2（随之改变）;
 *        冻结+IT 用法下 = 直接改比较中断频率;需先 OcInit 且已启动
 * 标准库 : PSC/ARR 直写 + TIM_GenerateEvent（TIM_EventSource_Update） */
void SYS_TIM_OcSetFreq(SysTimId_t id, uint32_t cycle_hz);

/* 停该信道输出（关信道 + 关比较中断;其它信道不受影响）
 * 标准库 : TIM_CCxCmd */
void SYS_TIM_OcStop(SysTimId_t id, uint8_t ch);


/* 标准库结构体速查: TIM_TypeDef（stm32f4xx.h; TIM2~14 共用布局）
 * 成员一览（含库中用法）:
 *    CR1     控制 1:位 0 CEN = 启动计数（TIM_Cmd 写它）
 *    CR2     控制 2:一般不用
 *    SMCR    从模式控制:ETR 外部时钟开关(ECE)、触发选择(TS)、从模式(SMS)/外部分频等,
 *            SYS_TIM_EtrInit 把外部脉冲接成计数时钟就靠它（TIM_ETRClockMode2Config 置 ECE 位;
 *            TIM_ETRClockMode1Config 置 SMS=111 + TS=ETRF）
 *    DIER    中断使能:更新/比较等中断开关（TIM_ITConfig 写它）
 *    SR      状态:位 0 UIF = 更新中断标志（TIM_GetITStatus 查它、TIM_ClearITPendingBit 清它）
 *    EGR     事件生成:软件产生更新事件（TIM_GenerateEvent）
 *    CCMR1/2 捕获/比较模式:PWM 模式/预装载（TIM_OCxInit 写它）
 *    CCER    捕获/比较使能:每通道开关+极性（TIM_OCxInit 开通道）
 *    CNT     计数:当前计数值
 *    PSC     预分频:计数时钟 = 定时器时钟 / (PSC+1)（TimeBaseInit 自动换算）
 *    ARR     自动重装:计满归零 = 一个周期（库自动换算,决定频率）
 *    RCR     重复计数:高级定时器(TIM1/8)用,普通填 0
 *    CCR1~4  比较值:输出高电平持续几个计数 = 占空比
 *            （TIM_SetCompare1~4 / GetCapture1~4 读写它）
 *    BDTR    刹车和死区:高级定时器用,含 MOE 主输出使能
 *            （TIM_CtrlPWMOutputs 写它,TIM1/8 的 PWM 必须开）
 *    DCR / DMAR   DMA 相关:库未使用
 *    OR      选项:位 0 = TIM9/10/11 重映射,库未用
 */


/* 标准库结构体速查: TIM_TimeBaseInitTypeDef
 *        （stm32f4xx_tim.h; 时基配置结构体, 喂给 TIM_TimeBaseInit）
 * 时基参数即"多久数一次、数多少个算一轮"。TIM_TimeBaseInit 把它写进
 * PSC / ARR / CR1(模式位) / RCR 等寄存器;PWM / 定时中断 / ETR 计数三条
 * 调用链的时基一步填的就是它。该结构体是打包参数,TIM_TypeDef 才是寄存器映射。
 *
 * 字段一览（含可取值与寄存器对应;"库用"= 库内统一填法）:
 *    TIM_Prescaler  分频器:计数时钟 = 定时器时钟 / (本值 + 1)
 *                   取值 0 ~ 0xFFFF（寄存器值 = 分频系数 - 1:
 *                   要 /8400 就填 8400-1 = 8399）   寄存器:PSC
 *                   库用:tim_calc_psc_arr 按目标频率自动换算
 *
 *    TIM_Period     周期:数满这么多次归零 = 一轮
 *                   取值 0 ~ 0xFFFF（寄存器值 = 计数次数 - 1:
 *                   每 10000 个数一轮就填 9999）    寄存器:ARR
 *                   本芯片 TIM2/TIM5 计数器实为 32 位、量程更大,
 *                   标准库按 16 位写注释
 *                   库用:自动换算（PWM/中断）;ETR 版 = period_n - 1
 *
 *    TIM_CounterMode  计数模式（方向与对齐方式）:
 *                   可取值 TIM_CounterMode_Up / _Down /
 *                   _CenterAligned1 / _CenterAligned2 / _CenterAligned3
 *                   （中心对齐用于电机类 PWM,常配互补输出;
 *                     TIM9~14 硬件上只能向上计数）
 *                   寄存器:CR1 的 CMS + DIR 位       库用:固定 Up
 *
 *    TIM_ClockDivision  时钟分割 CKD:不改变计数时钟,只影响数字滤波采样时钟
 *                   与死区时间基准,普通计时 / PWM 无感知
 *                   可取值 TIM_CKD_DIV1 / _DIV2 / _DIV4
 *                   寄存器:CR1 的 CKD 位             库用:固定 DIV1
 *
 *    TIM_RepetitionCounter  重复计数 RCR,仅 TIM1 / TIM8 有效:
 *                   每数满 (本值 + 1) 轮才产生一次更新事件（中断）;
 *                   普通定时器没有此功能,填 0
 *                   取值 0 ~ 0xFF                    库用:固定 0
 *                   寄存器:RCR
 *
 * 计算例（TIM3 每 1 秒一次中断,定时器时钟 84MHz）:
 *    PSC = 8400 - 1（84MHz ÷ 8400 = 10kHz）,ARR = 10000 - 1（10k 个数 × 0.1ms = 1s）
 *    频率公式 : 更新频率 = 定时器时钟 ÷ ((PSC + 1) × (ARR + 1))
 *
 * 库内三处填法:
 *    SYS_TIM_PwmInit  时基 → 输出 freq 方波    (PSC/ARR 按频率换算)
 *    SYS_TIM_InitIT   时基 → 每 1/freq 秒中断  (同上)
 *    SYS_TIM_EtrInit  时基 → PSC=0、ARR=period_n-1（每 N 个脉冲一轮）
 *
 * 库先调 TIM_TimeBaseStructInit(&tb) 填全部字段默认值,再只改要动的,
 * 避免漏赋字段（局部变量未赋值的字段是野值）。
 */


/* 标准库结构体速查: TIM_ICInitTypeDef
 *        （stm32f4xx_tim.h; 输入捕获配置结构体, 喂给 TIM_ICInit）
 * 信道作输入时的配置包,TIM_ICInit 把它写进 CCMR（选通/滤波/分频）
 * 与 CCER（极性/使能）寄存器。
 *
 * 字段一览（含可取值;"库用"= SYS_TIM_CaptureInit 内填法）:
 *    TIM_Channel      信道: TIM_Channel_1 ~ _4（选哪个信道）  库用:参数 ch
 *
 *    TIM_ICPolarity   捕哪个边沿: TIM_ICPolarity_Rising / _Falling /
 *                     _BothEdge（写 CCER 的 CCxP/CCxNP 位）
 *                     库用:参数 polarity;运行中换沿用 CaptureSetPolarity
 *
 *    TIM_ICSelection  信号怎么接: TIM_ICSelection_DirectTI（引脚
 *                     直连本信道→TIx）/ _IndirectTI（交叉到另一信道）/
 *                     _TRC（触发用）
 *                     测占空比时经 TIM_PWMIConfig 自动配成"一升一降"双信道
 *                     库用:固定 TIM_ICSelection_DirectTI
 *
 *    TIM_ICPrescaler  捕获分频: TIM_ICPSC_DIV1（每个边沿都捕）/
 *                     _DIV2 / _DIV4 / _DIV8（每 N 个边沿才捕一次）
 *                     库用:固定 TIM_ICPSC_DIV1
 *
 *    TIM_ICFilter     数字滤波: 0x0 ~ 0xF,连续采样都相同时才认边沿,
 *                     越大抗抖越强（机械按键/慢信号调大;干净信号可用 0）
 *                     库用:宏 SYS_TIM_IC_FILTER（默认 0xF）
 *
 * 读 CCRx 寄存器会顺带清掉捕获标志(CCxIF),库的 CaptureGet 同理。
 */


/* 标准库结构体速查: TIM_OCInitTypeDef
 *        （stm32f4xx_tim.h; 输出比较配置结构体, 喂给 TIM_OC1~4Init）
 * 信道作输出/比较时的配置包,TIM_OCxInit 把它写进 CCMR（模式/预装载）、
 * CCER（极性/使能）与 CCRx。
 *
 * 字段一览（含可取值;"库用"= PwmInit / OcInit 内填法）:
 *    TIM_OCMode       匹配时干什么（六选一）:
 *                     TIM_OCMode_Timing（冻结:只置标志,不动引脚）/
 *                     _Active（匹配置有效电平）/ _Inactive（置无效电平）/
 *                     _Toggle（匹配翻转）/ _PWM1 / _PWM2
 *                     库用:PwmInit 固定 PWM1;OcInit 由参数传入
 *
 *    TIM_OutputState  主输出开关: TIM_OutputState_Enable / _Disable
 *                     库用:固定 Enable
 *
 *    TIM_OutputNState 互补输出开关（仅 TIM1/8 有,库未用）
 *
 *    TIM_Pulse        比较值 → CCRx（0 ~ ARR）:PWM 的占空比 / 翻转的时刻
 *                     库用:初始 0(PWM) 或参数 ccr
 *
 *    TIM_OCPolarity   有效电平极性: TIM_OCPolarity_High / _Low
 *                     库用:固定 High
 *
 *    TIM_OCNPolarity  互补输出极性（仅 TIM1/8,库未用）
 *    TIM_OCIdleState / TIM_OCNIdleState  刹车空闲电平（仅 TIM1/8,库未用;
 *                     配合 BDTR 的 MOE,PwmInit/OcInit 已自动开 MOE）
 *
 * PWM 是 TIM_OCMode_PWM1 / _PWM2 的预设,所以库把 PWM 单独封装成
 * 频率/占空比两参数,其它模式归 OcInit。
 */

#endif /* __FWLIB_SYS_TIM_H */
