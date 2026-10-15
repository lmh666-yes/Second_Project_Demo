#ifndef __FWLIB_SYS_TIM_H
#define __FWLIB_SYS_TIM_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_tim.h — 通用定时器(TIM)模块 头文件
 * ================================================================
 *  功能: 标准外设库 TIM 封装,覆盖 PWM 输出 / 固定周期中断 / 外部脉冲计数
 *        (ETR) / 输入捕获(IC) / 输出比较(OC);支持 TIM1~14。
 *  边界: PWM 限 TIM1~5 / TIM8~14(TIM6/7 无输出通道);通道数
 *        TIM1~5/8=4、TIM9/12=2、TIM10/11/13/14=1。
 *  依据: 引脚由参数指定端口/引脚/复用号,复用功能见数据手册及 gpio_core.h
 *        附录"附:GPIO 复用功能(AF)";仅物理支持该定时器通道的引脚可输出
 *        PWM(如 PA0 = TIM5_CH1 / GPIO_AF_TIM5)。TIMx_IRQHandler 在本模块
 *        .c 内弱定义(TIM1/8~14 用共享中断向量,见 .c 文件头),自定义同名
 *        ISR 会顶替库版并停用库回调。
 * ================================================================ */


/* -------------------- 中断优先级 -------------------- */
/* 数值越小优先级越高,范围随 sys_nvic 分组:PriorityGroup_4(本库默认)
 * 抢占 0~15、子 0;PriorityGroup_2(纯裸机)抢占 0~3、子 0~3。
 * FreeRTOS 下必须 ≥5(默认 5):BASEPRI = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY<<4
 * (= 5<<4 = 0x50);抢占值 <5 可打断内核临界区,其中禁止调用 ...FromISR()。
 * 更急最小取 5,更缓取 6~15。 */
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

/* ETR 计数时钟通路（取值 1 或 2，默认 2）:
 *   2 = TIM_ETRClockMode2Config: SMCR 的 ECE 置 1,ETR 直通,只占 ECE 位;
 *   1 = TIM_ETRClockMode1Config: SMCR 的 SMS=111 + TS=ETRF,经触发控制器进入,
 *       占用 SMS/TS 位。两者均以 ETR 引脚上每个脉冲驱动计数,本库场景等价。 */
#define SYS_TIM_ETR_CLKMODE   2

/* -------------------- 输入捕获（区块 3 使用） -------------------- */
/* 捕获输入的数字滤波强度（0x0 ~ 0xF）：连续采样一致才认边沿,越大抗抖越强
 * 机械按键/慢信号建议 0x07 ~ 0x0F；干净数字信号可用 0x00（不滤） */
#define SYS_TIM_IC_FILTER   0x0F


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
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
 * 参数: id — SYS_TIM_1 ~ SYS_TIM_14(TIM6/7 无输出通道,直接返回);ch — 1 ~ 4
 *       port/pin/af — 输出引脚,af 取同号 GPIO_AF_TIMx;freq_hz — 建议 1Hz ~ 1MHz
 * 依据: TIM_TimeBaseInit(PSC/ARR 自动换算) → TIM_OC1~4Init(PWM1)+预装载
 *         → TIM_CtrlPWMOutputs(仅 TIM1) → TIM_Cmd */
void SYS_TIM_PwmInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                     uint8_t af, uint32_t freq_hz);

/* 设置占空比：permille 千分比 0 ~ 1000（500 = 50%），超范围截断
 * 依据 : TIM_SetCompare1~4 写 CCRx（按通道号自动选） */
void SYS_TIM_PwmSetDuty(SysTimId_t id, uint8_t ch, uint16_t permille);

/* 停止该通道 PWM（占空比归零；同一定时器其它通道不受影响）
 * 依据 : 与 PwmSetDuty 同路径,TIM_SetCompareX 写 0 */
void SYS_TIM_PwmStop(SysTimId_t id, uint8_t ch);

/* 读取当前占空比（千分比 0~1000）；PwmStop 会归零,需恢复时先读出暂存
 * 依据 : TIM_GetCapture1~4 读 CCR 寄存器值 */
uint16_t SYS_TIM_PwmGetDuty(SysTimId_t id, uint8_t ch);

/* 运行中改 PWM 频率：各通道 CCR 等比缩放，占空比保持不变
 * 约束: 需先 PwmInit 且已启动,TIM6/7 或未启动时直接返回;PSC/ARR 重算后立即
 *       生效;同一定时器有通道正作输入捕获时不可调用(会读 CCR 并清捕获标志);
 *       freq_hz = 0 直接返回,停止输出用 PwmStop
 * 依据: PSC/ARR 直写 + TIM_GenerateEvent(TIM_EventSource_Update) */
void SYS_TIM_PwmSetFreq(SysTimId_t id, uint32_t freq_hz);

/* 定时中断初始化：每 1/freq_hz 秒执行一次 callback
 * 参数: id — SYS_TIM_1 ~ SYS_TIM_14 全部支持;freq_hz — 1 = 每秒 1 次,1000 = 每毫秒 1 次;
 *       callback — 无参数,中断上下文执行,保持短小,多用途各写一个
 * 依据: TIM_TimeBaseInit → TIM_ITConfig(TIM_IT_Update) → NVIC_Init(优先级宏见上) →
 *       TIM_Cmd;中断内 TIM_GetITStatus → TIM_ClearITPendingBit → callback */
void SYS_TIM_InitIT(SysTimId_t id, uint32_t freq_hz, void (*callback)(void));

/* 停止定时器（停止计数，中断模式与 PWM 模式都可用）
 * 依据 : TIM_Cmd(DISABLE) + TIM_ITConfig(TIM_IT_Update, DISABLE) */
void SYS_TIM_Stop(SysTimId_t id);

/* ---- ADC 采样节拍（TRGO 定时触发输出）---- */

/* 把定时器配成 ADC 触发源：每个周期输出一次 TRGO(更新事件)
 * 约束: 不占引脚、不进中断;仅 TIM2 / TIM3 / TIM8 的 TRGO 接入 ADC 触发选择器,
 *       其它定时器直接返回;本函数接管该定时器(重配时基并启动),不可与 PWM /
 *       中断 / 捕获共用同一定时器
 * 参数: freq_hz — 触发频率(Hz),即 ADC 采样率,建议 1Hz ~ 1MHz
 * 依据: PSC/ARR 直写 → TIM_SelectOutputTrigger(TIM_TRGOSource_Update) → TIM_Cmd */
void SYS_TIM_TrgoInit(SysTimId_t id, uint32_t freq_hz);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* ---- 舵机（直接按角度控制） ---- */

/* 初始化舵机通道：50Hz，上电自动停在 90° 位置 */
void SYS_TIM_ServoInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin, uint8_t af);

/* 设置舵机角度：angle 0 ~ 180（度），超范围截断 */
void SYS_TIM_ServoSetAngle(SysTimId_t id, uint8_t ch, uint16_t angle);

/* ---- 无源蜂鸣器 / 方波发声 ---- */

/* 初始化发声通道（上电静音，占空比 0） */
void SYS_TIM_ToneInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin, uint8_t af);

/* 播放指定频率方波（50% 占空比）：freq_hz = 0 等效停止；262Hz = 中音 Do */
void SYS_TIM_TonePlay(SysTimId_t id, uint8_t ch, uint32_t freq_hz);

/* 停止发声 */
void SYS_TIM_ToneStop(SysTimId_t id, uint8_t ch);

/* 播放指定频率 + 占空比（‰），无源蜂鸣器调音量/音色用
 * 说明: 频率与占空比一次设置(= PwmSetFreq + PwmSetDuty);duty_permille 0~1000,
 *       常用 500(50%) / 100(轻) / 900(重);freq_hz = 0 为停止
 * 依据: TIM_SetAutoreload + TIM_SetCompareX */
void SYS_TIM_TonePlayDuty(SysTimId_t id, uint8_t ch, uint32_t freq_hz, uint16_t duty_permille);

/* ---- 外部脉冲计数（ETR 外部时钟,通路由宏选）----
 * 让定时器把指定引脚上的每个脉冲当作计数时钟;用途:按键次数计数 /
 * 外部信号计数 / 低频脉冲计量。通路有 TIM_ETRClockMode1Config /
 * TIM_ETRClockMode2Config 两种,由 SYS_TIM_ETR_CLKMODE 选。 */

/* 外部脉冲计数初始化（纯计数，无中断）
 * 参数: id — 定时器编号(ETR 引脚为芯片固定映射,查数据手册);port/pin — ETR
 *       输入引脚(如 TIM2 用 GPIOA / GPIO_Pin_0);af — 复用号(如 GPIO_AF_TIM2);
 *       period_n — 每收满多少脉冲算一轮(1 ~ 65536)
 * 依据: TIM_TimeBaseInit(ARR = period_n-1) → TIM_ETRClockMode1/2Config
 *         (SYS_TIM_ETR_CLKMODE 选,默认 2)+ 滤波 → TIM_Cmd;引脚配 AF 上拉
 * 约束: 滤波强度由 SYS_TIM_ETR_FILTER 定;PA0 为本板 KEY1,用作 ETR 时按键的
 *       轮询/EXTI 方式二选一 */
void SYS_TIM_EtrInit(SysTimId_t id, GPIO_TypeDef *port, uint16_t pin,
                     uint8_t af, uint32_t period_n);

/* 外部脉冲计数初始化 + 中断回调：每收满 period_n 个脉冲执行一次 callback
 * 说明: 与 SYS_TIM_EtrInit 的区别是多开更新中断并注册回调;回调无参数,中断上下文 */
void SYS_TIM_EtrInitIT(SysTimId_t id, GPIO_TypeDef *port, uint16_t pin,
                       uint8_t af, uint32_t period_n, void (*callback)(void));

/* 读本轮已收到的脉冲数（0 ~ period_n-1；数满归零重数）
 * 依据 : TIM_GetCounter 读 CNT 寄存器 */
uint32_t SYS_TIM_EtrCount(SysTimId_t id);

/* 清零脉冲计数
 * 依据 : TIM_SetCounter 写 CNT 寄存器 */
void SYS_TIM_EtrReset(SysTimId_t id);


/* ---- 输入捕获（Input Capture，信道作输入用）----
 * 原理 : 边沿到来瞬间,硬件把当前 CNT 复制进 CCRx 并置捕获标志;
 *        两次捕获值之差 ÷ tick_hz 即两点之间的时间。
 * 用途 : 测脉冲宽度（按键按下时长、红外解码）/ 测周期与频率 / 配合换沿测占空比 */

/* 输入捕获初始化（纯轮询,无中断）
 * 参数: id — 定时器编号(TIM6/7 无信道,直接返回);ch — 信道号 1 ~ 4;
 *       port/pin/af — 捕获输入引脚(TIM5_CH1 用 GPIOA / GPIO_Pin_0 / GPIO_AF_TIM5);
 *       polarity — TIM_ICPolarity_Rising / _Falling / _BothEdge;
 *       tick_hz — 计数频率(Hz),捕获值 ÷ tick_hz = 秒(10000 → 1 计数 = 0.1ms)
 * 依据: TIM_TimeBaseInit(PSC 按 tick_hz,ARR = 0xFFFF) →
 *         TIM_ICInit(DirectTI / ICPSC_DIV1 + 滤波宏) → TIM_Cmd;引脚配 AF 上拉
 * 约束: tick_hz ≥ 定时器时钟/65536(84MHz 约 1282Hz);满量程 = 65536 ÷ tick_hz 秒;
 *       滤波强度由 SYS_TIM_IC_FILTER 定 */
void SYS_TIM_CaptureInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                         uint8_t af, uint16_t polarity, uint32_t tick_hz);

/* 输入捕获初始化 + 捕获中断回调：每捕到一个边沿 → callback(捕获值)
 * 说明: 与 SYS_TIM_CaptureInit 的区别是多开捕获中断并注册回调;回调参数为本次
 *       捕获计数值,中断上下文执行;TIM1/TIM8 捕获中断在单独的 _CC 向量(库已处理) */
void SYS_TIM_CaptureInitIT(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                           uint8_t af, uint16_t polarity, uint32_t tick_hz,
                           void (*callback)(uint32_t value));

/* 查捕获标志（CCxIF）:1 = 有新捕获值
 * 依据 : TIM_GetFlagStatus 查 TIM_FLAG_CC1~4 */
uint8_t SYS_TIM_CaptureFlag(SysTimId_t id, uint8_t ch);

/* 读捕获值（CCRx）:读 CCR 会顺带清掉捕获标志(硬件行为)
 * 依据 : TIM_GetCapture1~4；轮询 = CaptureFlag 判断 → CaptureGet 取值 */
uint32_t SYS_TIM_CaptureGet(SysTimId_t id, uint8_t ch);

/* 清捕获标志（不读值时单独清,如丢弃一次干扰）
 * 依据 : TIM_ClearFlag */
void SYS_TIM_CaptureClear(SysTimId_t id, uint8_t ch);

/* 动态切换捕获边沿（Rising ↔ Falling,用于"降沿开始/升沿结束"测脉宽）
 * 依据 : TIM_OC1~4PolarityConfig（写 CCER 的 CCxP 位,捕获/比较共用）
 * 约束: 只能填 TIM_ICPolarity_Rising / _Falling;BothEdge 在 CaptureInit 时配 */
void SYS_TIM_CaptureSetPolarity(SysTimId_t id, uint8_t ch, uint16_t polarity);


/* ---- 输出比较（Output Compare，信道作输出/比较用）----
 * 原理 : CNT 数到与 CCR 相同那一刻是匹配事件;六种模式决定匹配时干什么:
 *        TIM_OCMode_Timing(冻结,只置标志/中断) / _Active(强置高) /
 *        _Inactive(强置低) / _Toggle(翻转) / _PWM1 / _PWM2。
 * 用途 : 翻转模式输出精确方波 / 冻结模式当比较中断软定时器 */

/* 输出比较初始化（六种模式任选）
 * 参数: id — 定时器编号(TIM6/7 无信道,直接返回);ch — 信道号 1 ~ 4;
 *       port/pin/af — 输出引脚,纯比较事件/中断不用引脚时 port 传 0;
 *       oc_mode — 六选一:Timing 冻结(只置匹配标志/中断) / _Active 置有效电平 /
 *                 _Inactive 置无效电平 / _Toggle 翻转 / _PWM1 / _PWM2(即 PWM,
 *                 建议改用 PwmInit+SetDuty);
 *       cycle_hz — 计数一轮(ARR+1 个数)的频率,匹配事件每秒 cycle_hz 次;
 *       ccr — 匹配点在本轮中的位置(0 ~ 一轮计数次数),决定相位/翻转点
 * 依据: TIM_TimeBaseInit(PSC/ARR 按 cycle_hz) → TIM_OCInit → TIM_ARRPreloadConfig
 *         + TIM_Cmd(TIM1/8 另开 MOE);引脚配 AF 推挽
 * 约束: 翻转模式输出方波频率 = cycle_hz ÷ 2,占空比恒 50% */
void SYS_TIM_OcInit(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                    uint8_t af, uint16_t oc_mode, uint32_t cycle_hz, uint32_t ccr);

/* 输出比较初始化 + 比较中断回调：每次匹配事件执行一次 callback
 * 说明: 与 SYS_TIM_OcInit 的区别是多开比较中断并注册回调;冻结模式 + 本中断 =
 *       不占引脚的软定时器;TIM1/TIM8 比较中断在单独的 _CC 向量(库已处理) */
void SYS_TIM_OcInitIT(SysTimId_t id, uint8_t ch, GPIO_TypeDef *port, uint16_t pin,
                      uint8_t af, uint16_t oc_mode, uint32_t cycle_hz, uint32_t ccr,
                      void (*callback)(void));

/* 改比较值（CCRx,决定相位/翻转点/占空比）
 * 依据 : TIM_SetCompare1~4 */
void SYS_TIM_OcSetCompare(SysTimId_t id, uint8_t ch, uint32_t ccr);

/* 运行中改一轮频率（cycle_hz）：PSC/ARR 立即重算、CCR 等比缩放
 * 说明: 翻转模式输出方波频率 = cycle_hz ÷ 2(随之改变);冻结+中断用法下 = 直接改
 *       比较中断频率;需先 OcInit 且已启动
 * 依据 : PSC/ARR 直写 + TIM_GenerateEvent（TIM_EventSource_Update） */
void SYS_TIM_OcSetFreq(SysTimId_t id, uint32_t cycle_hz);

/* 停该信道输出(关信道 + 关比较中断;其它信道不受影响)
 * 依据 : TIM_CCxCmd */
void SYS_TIM_OcStop(SysTimId_t id, uint8_t ch);


/* ================================================================
 *  附:标准库结构体速查 — TIM_TypeDef（stm32f4xx.h;TIM2~14 共用布局）
 * ================================================================
 *    CR1     位 0 CEN = 启动计数(TIM_Cmd)      CR2    控制 2
 *    SMCR    ECE = ETR 外部时钟(Mode2Config 置 1);TS + SMS = 触发源
 *            (Mode1Config 置 SMS=111 + TS=ETRF)
 *    DIER    中断使能(TIM_ITConfig)             SR     位 0 UIF = 更新中断标志
 *            (TIM_GetITStatus / TIM_ClearITPendingBit)
 *    EGR     软件产生更新事件(TIM_GenerateEvent)
 *    CCMR1/2 捕获/比较模式:模式与预装载(TIM_OCxInit)
 *    CCER    每通道使能与极性(TIM_OCxInit)      CNT    当前计数值
 *    PSC     计数时钟 = 定时器时钟 / (PSC+1)    ARR    自动重装,计满归零 = 一个周期
 *    RCR     重复计数,仅 TIM1/8,普通填 0
 *    CCR1~4  比较值,决定占空比(TIM_SetCompare1~4 / TIM_GetCapture1~4)
 *    BDTR    刹车和死区,含 MOE 主输出使能(TIM1/8 的 PWM 必须开)
 *    DCR / DMAR / OR   DMA 与选项,库未用
 * ================================================================ */


/* ================================================================
 *  附:标准库结构体速查 — TIM_TimeBaseInitTypeDef
 *        （stm32f4xx_tim.h;时基配置结构体,喂给 TIM_TimeBaseInit）
 * ================================================================
 *  写 PSC / ARR / CR1(模式位) / RCR;PWM / 定时中断 / ETR 计数三条调用链的时基
 *  一步均填它(PSC/ARR 由库自动换算)。"库用" = 库内统一填法。
 *    TIM_Prescaler  分频器 0 ~ 0xFFFF,寄存器值 = 分频系数-1(要 /8400 填 8399)
 *                   → PSC;库用:tim_calc_psc_arr 按目标频率自动换算
 *    TIM_Period     周期 0 ~ 0xFFFF,寄存器值 = 计数次数-1(10000 个数填 9999)
 *                   → ARR;TIM2/TIM5 计数器实为 32 位,量程更大;
 *                   库用:自动换算(PWM/中断),ETR 版 = period_n - 1
 *    TIM_CounterMode  TIM_CounterMode_Up / _Down / _CenterAligned1~3(中心对齐用
 *                   于电机类 PWM,常配互补输出;TIM9~14 只能向上计数)
 *                   → CR1 的 CMS + DIR;库用:固定 Up
 *    TIM_ClockDivision  TIM_CKD_DIV1 / _DIV2 / _DIV4:不改计数时钟,只影响数字
 *                   滤波采样时钟与死区时间基准 → CR1 的 CKD;库用:固定 DIV1
 *    TIM_RepetitionCounter  0 ~ 0xFF,仅 TIM1/TIM8,每 (本值+1) 轮一次更新事件;
 *                   普通定时器填 0 → RCR;库用:固定 0
 *  例(TIM3 每 1 秒一次中断,定时器时钟 84MHz):TIM_Prescaler = 8400-1
 *   → 84MHz ÷ 8400 = 10kHz;TIM_Period = 10000-1 → 10k × 0.1ms = 1s;
 *   更新频率 = 定时器时钟 ÷ ((PSC + 1) × (ARR + 1))
 * ================================================================ */


/* ================================================================
 *  附:标准库结构体速查 — TIM_ICInitTypeDef
 *        （stm32f4xx_tim.h;输入捕获配置结构体,喂给 TIM_ICInit）
 * ================================================================
 *  写 CCMR(选通/滤波/分频)与 CCER(极性/使能);库用 = SYS_TIM_CaptureInit 内填法。
 *    TIM_Channel      TIM_Channel_1 ~ _4;库用:参数 ch
 *    TIM_ICPolarity   TIM_ICPolarity_Rising / _Falling / _BothEdge(写 CCER 的
 *                     CCxP/CCxNP 位);库用:参数 polarity,运行中换沿用
 *                     CaptureSetPolarity
 *    TIM_ICSelection  _DirectTI(引脚直连本信道→TIx) / _IndirectTI(交叉到另一信道)
 *                     / _TRC(触发用);库用:固定 DirectTI
 *    TIM_ICPrescaler  _DIV1(每个边沿都捕) / _DIV2 / _DIV4 / _DIV8;库用:固定 DIV1
 *    TIM_ICFilter     0x0 ~ 0xF,连续采样都相同才认边沿,越大抗抖越强;
 *                     库用:宏 SYS_TIM_IC_FILTER(默认 0xF)
 * ================================================================ */


/* ================================================================
 *  附:标准库结构体速查 — TIM_OCInitTypeDef
 *        （stm32f4xx_tim.h;输出比较配置结构体,喂给 TIM_OC1~4Init）
 * ================================================================
 *  写 CCMR(模式/预装载)、CCER(极性/使能)与 CCRx;库用 = PwmInit / OcInit 内填法。
 *    TIM_OCMode       TIM_OCMode_Timing(冻结,只置标志) / _Active(置有效电平) /
 *                     _Inactive(置无效电平) / _Toggle(匹配翻转) / _PWM1 / _PWM2;
 *                     库用:PwmInit 固定 PWM1,OcInit 由参数传入
 *    TIM_OutputState  TIM_OutputState_Enable / _Disable;库用:固定 Enable
 *    TIM_OutputNState 互补输出开关(仅 TIM1/8,库未用)
 *    TIM_Pulse        比较值 → CCRx(0 ~ ARR):占空比 / 翻转时刻;
 *                     库用:初始 0(PWM)或参数 ccr
 *    TIM_OCPolarity   TIM_OCPolarity_High / _Low;库用:固定 High
 *    TIM_OCNPolarity  互补输出极性(仅 TIM1/8,库未用)
 *    TIM_OCIdleState / TIM_OCNIdleState  刹车空闲电平(仅 TIM1/8,库未用;
 *                     配合 BDTR 的 MOE,PwmInit/OcInit 已自动开 MOE)
 * ================================================================ */

#endif /* __FWLIB_SYS_TIM_H */
