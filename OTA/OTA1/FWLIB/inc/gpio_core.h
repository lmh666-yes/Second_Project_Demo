#ifndef __FWLIB_GPIO_CORE_H
#define __FWLIB_GPIO_CORE_H

#include "stm32f4xx.h"
#include "delay.h"    /* 兼容入口，延时接口见 delay.h */

/* ================================================================
 *  gpio_core.h — 通用 GPIO 底层工具库
 * ================================================================
 *  定位 : 标准外设库之上的薄封装，只做配置引脚与读写电平，无外设语义；
 *         换板子无需改动本文件（不含硬件映射）。
 *  调用方 : led.c / key.c / beep.c / ext_io.c
 *  命名 : GPIO_OutXxx 输出方向（推挽 GPIO_OType_PP / 开漏 GPIO_OType_OD）;
 *         GPIO_InXxx 输入方向; 位带宏已拆到 sys_bitband.h。
 *  说明 : 业务代码优先用 led / key / beep / ext_io 的语义化函数，
 *         仅当操作库内未封装的引脚时才直接调用本层。
 * ================================================================ */
/* 区块 2 基础功能：端口时钟 / 输出 / 输入（延时见 delay.h） */

/* 使能指定 GPIO 端口的 AHB1 时钟（幂等）
 *
 * 约束 : 操作任何 GPIO 寄存器前必须先开该端口时钟，否则寄存器写入被硬件忽略。
 * 调用 : GPIO_OutInit / GPIO_InInit 内部已调用; 直接用标准库 API 操作引脚时需手动调用。
 * 实现 : 查表换算端口指针到时钟位，支持 GPIOA ~ GPIOI; 未知端口不做任何事。
 * 标准库 : RCC_AHB1PeriphClockCmd
 * 示例 : GPIO_ClockEnable(GPIOB); */
void GPIO_ClockEnable(GPIO_TypeDef *port);


/* 通用 GPIO 输出操作：推挽/开漏配置（GPIOA~GPIOI，自动使能端口时钟）
 * 配推挽 : GPIO_OType_PP / GPIO_Speed_100MHz / GPIO_PuPd_NOPULL，初始电平 0，
 *          需上电即高时在 Init 后调 GPIO_OutSet；开漏用 GPIO_OutInitOD。
 * 配开漏 : GPIO_OType_OD / GPIO_Speed_100MHz / GPIO_PuPd_NOPULL；用于软件模拟 I2C
 *          （配 GPIO_PuPd_UP）、开漏加上拉电平转换、多设备线与。
 *          开漏输出高时为高阻态，必须外接上拉才呈现高电平，否则测到的是悬空电压。
 * 参数   : port 端口指针（如 GPIOF）/ pin 引脚掩码（如 GPIO_Pin_9，可用 | 组合）
 * 标准库 : RCC_AHB1PeriphClockCmd（经 GPIO_ClockEnable）+ GPIO_Init
 * 示例   : GPIO_OutInit(GPIOF, GPIO_Pin_8);   GPIO_OutInitOD(GPIOB, GPIO_Pin_7); */
void    GPIO_OutInit  (GPIO_TypeDef *port, uint16_t pin);
void    GPIO_OutInitOD(GPIO_TypeDef *port, uint16_t pin);

/* 输出高电平 / 输出低电平 / 电平翻转
 * 前提 : 引脚已用 GPIO_OutInit 配置为输出（本组只写 BSRR，不开时钟、不改模式）
 * 标准库 : GPIO_SetBits / GPIO_ResetBits / GPIO_ToggleBits
 * 示例 : GPIO_OutSet(GPIOF, GPIO_Pin_9);   GPIO_OutToggle(GPIOF, GPIO_Pin_9); */
void    GPIO_OutSet   (GPIO_TypeDef *port, uint16_t pin);
void    GPIO_OutReset (GPIO_TypeDef *port, uint16_t pin);
void    GPIO_OutToggle(GPIO_TypeDef *port, uint16_t pin);

/* 按参数写电平：level 非 0 → 输出高；level = 0 → 输出低
 * 用途 : 电平值来自变量或数组时的统一出口（等价 Set / Reset 二选一）
 * 标准库 : GPIO_SetBits / GPIO_ResetBits
 * 示例 : GPIO_OutWrite(GPIOF, GPIO_Pin_9, 0); */
void    GPIO_OutWrite (GPIO_TypeDef *port, uint16_t pin, uint8_t level);

/* 读输出数据寄存器 ODR：1 = 软件设置为输出高，0 = 输出低
 * 约束 : 读的是软件写入值，不是引脚真实电平；引脚被外部电路拉动时本函数不反映。
 * 标准库 : GPIO_ReadOutputDataBit
 * 示例 : uint8_t lv = GPIO_OutRead(GPIOF, GPIO_Pin_9); */
uint8_t GPIO_OutRead  (GPIO_TypeDef *port, uint16_t pin);


/* 通用 GPIO 输入操作：配置输入（自动使能端口时钟）/ 读引脚电平 / 掩码转位号 */
/* 参数 : pull 上下拉选择，直接填标准库宏名，其它值等同 GPIO_PuPd_NOPULL:
 *          GPIO_PuPd_NOPULL = 0 浮空 / GPIO_PuPd_UP = 1 上拉 / GPIO_PuPd_DOWN = 2 下拉
 * 约束 : 数字输入引脚必须有确定电平，浮空时电平不定、易误触发；低电平按下的按键
 *        配 GPIO_PuPd_UP；不同脚不同上下拉时按脚分多次调用。
 * 标准库 : RCC_AHB1PeriphClockCmd（经 GPIO_ClockEnable）+ GPIO_Init
 * 示例 : GPIO_InInit(GPIOA, GPIO_Pin_0, GPIO_PuPd_UP); */
void    GPIO_InInit(GPIO_TypeDef *port, uint16_t pin, uint8_t pull);

/* 读输入数据寄存器 IDR：引脚真实电平，1 = 高，0 = 低
 * 约束 : 即时读取，不含去抖；抖动信号（如机械按键）在上层滤波。
 * 标准库 : GPIO_ReadInputDataBit
 * 示例 : if (GPIO_InRead(GPIOA, GPIO_Pin_0) == 0) { ... }   // PA0 被按下 */
uint8_t GPIO_InRead(GPIO_TypeDef *port, uint16_t pin);

/* 单引脚掩码 → 位号（0~15，即 GPIO_PinSourcex）
 * 用途 : GPIO_PinAFConfig 等需要引脚序号而非掩码的场合
 * 返回 : 0~15 = 对应位号；多引脚组合或 0 等非法输入返回 0xFF
 * 标准库 : 无，用位运算替代 GPIO_PinSource0~15 宏组
 * 示例 : uint8_t s = GPIO_PinSource(GPIO_Pin_9);   // s = 9 */
uint8_t GPIO_PinSource(uint16_t pin);


/* 引脚占用登记表（GPIO_Claim）— 检测多个模块配置同一引脚，实现见 gpio_core.c 同名区块
 *
 * 用途 : 各模块 Init 只配自己的引脚，无中心分配表；同一 (端口,位号) 被两个文件
 *        配成输出时，后初始化者会改写前者的模式/上下拉。离线工具
 *        本地工具\仓库巡检\pin_audit.py 只查头文件声明的脚位，运行期实际占用由本表记录。
 * 登记 : GPIO_Init / GPIO_OutInit / GPIO_OutInitOD / GPIO_InInit 由宏在调用点取
 *        __FILE__/__LINE__ 自动登记，调用方无需改动，复用脚同样覆盖。
 * 冲突 : 同一 (端口,位号) 被两个不同源文件登记 ⇒ 冲突 +1 并记录两侧位置；
 *        同一文件内配两次不计（模块切换方向，如单总线先输出后输入）。
 * 查询 : GPIO_ClaimDump() 打印整表; GPIO_ClaimCount() / GPIO_ClaimAt(i) 遍历;
 *        GPIO_ClaimConflictCount() 为目标为 0 的冲突脚数。
 * 开销 : 每个调用点一个字符串常量（文件:行号，约 20~40 字节 Flash）;
 *        RAM 为静态数组 96 条 × 20 字节 ≈ 1920 字节，不做堆分配;
 *        表满只累加 GPIO_ClaimLost()，不越界。
 * 约束 : 登记只在初始化阶段写，单线程，无锁；运行期多任务动态配脚需自行串行化，
 *        或先调 GPIO_Claim() 占位。 */
#define GPIO_CLAIM_WHO_MAX  2U    /* 每个脚最多记 2 处不同文件的来源 */

typedef struct {
    GPIO_TypeDef *port;                  /* 端口 */
    uint16_t      pin;                   /* 引脚掩码（单脚；组合掩码原样记） */
    uint16_t      hits;                  /* 登记总次数（含同文件重复） */
    uint8_t       nwho;                  /* who[] 有效项数 */
    uint8_t       conflict;              /* 1 = 被两个不同文件登记过 */
    const char   *who[GPIO_CLAIM_WHO_MAX];   /* 来源，形如 "..\src\lora_e22.c:180" */
} GpioClaim_t;

/* 登记一个脚（四个入口已自动登记，一般无需手动调用）。
 * 返回 : 0 = 首次登记
 *        1 = 本文件重复登记（幂等，如重复 Init）或端口 = 0 表示没接
 *        2 = 已被另一个文件占用（冲突已计入，不阻断硬件配置）
 *        3 = 登记表满（已计入 GPIO_ClaimLost） */
uint8_t GPIO_Claim(GPIO_TypeDef *port, uint16_t pin, const char *who);

/* 整表查询 */
uint8_t            GPIO_ClaimCount(void);          /* 已登记条目数 */
const GpioClaim_t *GPIO_ClaimAt(uint8_t index);    /* 越界返回 0 */
uint8_t            GPIO_ClaimConflictCount(void);  /* 有冲突的脚数（目标 0） */
uint16_t           GPIO_ClaimLost(void);           /* 表满后未登记的调用次数 */
void               GPIO_ClaimDump(void);           /* 逐条打印（printf，需串口已重定向） */
char               GPIO_PortName(GPIO_TypeDef *port);   /* GPIOA→'A'…GPIOI→'I'，未知→'?' */

/* 四个入口的自登记包装
 *  只对包含本头文件的调用方生效；gpio_core.c 先定义 GPIO_CORE_IMPL 关闭这些宏，
 *  否则函数定义本身会被改写。上面的 GPIO_OutInit 等声明只表示函数形状，
 *  实际符号是带 Ex 的三个；不要 #undef 这些宏，否则链接期找不到符号。 */
#define GPIO_STR_(x)   #x
#define GPIO_STR(x)    GPIO_STR_(x)
#define GPIO_WHO       __FILE__ ":" GPIO_STR(__LINE__)

void GPIO_InitEx    (GPIO_TypeDef *port, GPIO_InitTypeDef *init, const char *who);
void GPIO_OutInitEx (GPIO_TypeDef *port, uint16_t pin, const char *who);
void GPIO_OutInitODEx(GPIO_TypeDef *port, uint16_t pin, const char *who);
void GPIO_InInitEx  (GPIO_TypeDef *port, uint16_t pin, uint8_t pull, const char *who);

#ifndef GPIO_CORE_IMPL
#define GPIO_Init(port, init)         GPIO_InitEx((port), (init), GPIO_WHO)
#define GPIO_OutInit(port, pin)       GPIO_OutInitEx((port), (pin), GPIO_WHO)
#define GPIO_OutInitOD(port, pin)     GPIO_OutInitODEx((port), (pin), GPIO_WHO)
#define GPIO_InInit(port, pin, pull)  GPIO_InInitEx((port), (pin), (pull), GPIO_WHO)
#endif


/* 位带操作已拆分为独立文件 sys_bitband.h
 * 内容 : BITBAND_PERIPH / BITBAND_SRAM / GPIO_BB_OUT / _IN / GPIO_BB_*_ADDR /
 *        GPIO_PIN_NUM / Pxout(n)~PIin(n); 用法 : #include "sys_bitband.h"（纯宏，无编译成本） */


/* 延时函数已拆分为独立文件 delay.h / delay.c
 * 内容 : delay_ms / delay_loop（粗延时）;
 *        delay_cycles / delay_us / delay_ns / delay_ms_dwt（DWT 精准延时）。
 * 说明 : 老代码继续 #include "gpio_core.h" 即可（本文件已含 delay.h）;
 *        新代码直接 #include "delay.h"，旧大写名 Delay_xxx 已取消。 */


/* 附:标准库结构体 GPIO_TypeDef 速查（定义在 stm32f4xx.h）
 * 一律写标准库宏名；库通过指针（如 GPIOA->MODER）操作寄存器。
 *    MODER     模式:GPIO_Mode_IN / GPIO_Mode_OUT / GPIO_Mode_AF / GPIO_Mode_AN
 *              （GPIO_Init 的方向;寄存器编码 00/01/10/11）
 *    OTYPER    输出类型:GPIO_OType_PP（推挽） / GPIO_OType_OD（开漏）
 *    OSPEEDR   输出速度:GPIO_Speed_2MHz / 25MHz / 50MHz / 100MHz（GPIO_Init 的 Speed）
 *    PUPDR     上下拉:GPIO_PuPd_NOPULL / GPIO_PuPd_UP / GPIO_PuPd_DOWN
 *              （GPIO_InInit 的 pull 参数）
 *    IDR       输入数据:引脚真实电平（GPIO_InRead / 位带读）
 *    ODR       输出数据:软件写入的电平（GPIO_OutRead / 位带直写）
 *    BSRRL/H   置位/复位:F4 拆成两个 16 位半字，BSRRL 写 1 置位、BSRRH 写 1 复位
 *              （GPIO_SetBits / ResetBits / ToggleBits 写它）
 *    LCKR      配置锁定:锁住引脚配置防误改，库未使用
 *    AFR[2]    复用功能:每脚 4 位，存复用号 0~15（GPIO_PinAFConfig 写） */


/* 附:GPIO 复用功能(AF)速查表 — GPIO_PinAFConfig 的选号依据
 * 约束 : 使能一个复用功能需两步，缺一不生效，先后顺序不限:
 *          1) GPIO_PinAFConfig(端口, 脚序号, 复用号)  选号
 *          2) GPIO_Init(复用模式)                      切到复用
 *
 * AF 号 → 外设（按 F407ZE 整理;[ ] 内为仅高阶型号有的功能，本芯片没有）:
 *    AF0  系统 : RTC_50Hz / MCO1 / MCO2 / TAMPER / SWJ(复位默认) / TRACE
 *    AF1  TIM1、TIM2
 *    AF2  TIM3、TIM4、TIM5
 *    AF3  TIM8、TIM9、TIM10、TIM11
 *    AF4  I2C1、I2C2、I2C3
 *    AF5  SPI1、SPI2（含 I2S2）            [SPI4/5/6 仅 F42x/43x]
 *    AF6  SPI3（含 I2S3）                  [SAI1     仅 F42x/43x]
 *    AF7  USART1、USART2、USART3、I2S3ext
 *    AF8  UART4、UART5、USART6             [UART7/8  仅 F42x/43x]
 *    AF9  CAN1、CAN2、TIM12、TIM13、TIM14
 *    AF10 OTG_FS、OTG_HS
 *    AF11 ETH
 *    AF12 FSMC、SDIO、OTG_HS_FS            [FMC 仅 F42x/43x(FSMC 改名)]
 *    AF13 DCMI
 *    AF14 （本芯片未用）                    [LTDC     仅 F429/439]
 *    AF15 EVENTOUT（内核事件输出到引脚，调试用）
 *
 * 外设 → AF 号 反查:
 *    USART1/2/3 → AF7      USART6、UART4/5 → AF8
 *    I2C1/2/3   → AF4      SPI1/2 → AF5      SPI3 → AF6
 *    TIM1/2 → AF1   TIM3/4/5 → AF2   TIM8~11 → AF3   TIM12~14 → AF9
 *    CAN1/2 → AF9   ETH → AF11   FSMC/SDIO → AF12   DCMI → AF13
 *    （af 参数填同名宏: TIM5 → GPIO_AF_TIM5）
 *
 * 约束 : AF 号只说明哪套映射，引脚是否支持查数据手册 "Alternate function mapping"
 *        表（以引脚为行）; 同一外设信号可出现在多根引脚且复用号相同
 *        （USART1_TX 在 PA9 或 PB6 都是 AF7，换脚只改 port / pin）;
 *        一根引脚同一时刻只能接一个外设（如 PB10/PB11 给了 I2C2，USART3 不可再选）。
 *
 * 附:GPIO_PinAFConfig 数据布局
 *  原型 : void GPIO_PinAFConfig(GPIOx, GPIO_PinSource, GPIO_AF)
 *  AFR 为 uint32_t[2]: AFR[0] 管 0~7 号脚(AFRL)，AFR[1] 管 8~15 号脚(AFRH);
 *  每脚占 4 位，脚 0 占 bit[3:0] …… 脚 7 占 bit[31:28]。
 *    选寄存器 : AFR[GPIO_PinSource >> 3]
 *    算偏移   : (GPIO_PinSource & 7) * 4
 *    清零     : AFR[..] &= ~(0xF << 偏移);      // 只清该脚 4 位
 *    写入     : AFR[..] |= (GPIO_AF << 偏移);   // 放入复用号
 *  例:PA9 → USART1_TX(AF7): 9>>3 = 1 → AFR[1]; (9&7)*4 = 4 → bit[7:4];
 *     AFR[1] = (AFR[1] & ~0xF0) | 0x70，等价 GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1)。
 *
 * 约束 : 第二参数是脚序号 0~15（GPIO_PinSourcex 宏），不是 GPIO_Pin_x 掩码:
 *        GPIO_PinSource9 = 9，而 GPIO_Pin_9 = 0x0200; 传掩码会索引越界，
 *        写坏内存。转换工具: 运行时 GPIO_PinSource(pin) 函数、编译期
 *        GPIO_PIN_NUM(pin) 宏（见 sys_bitband.h）; 手写标准库时直接写
 *        GPIO_PinSource9 常量即可。
 *        复位后 AFR 全 0，所有脚处于 AF0，SWD 口 (PA13/PA14) 上电即可用。 */

#endif /* __FWLIB_GPIO_CORE_H */
