#ifndef __FWLIB_GPIO_CORE_H
#define __FWLIB_GPIO_CORE_H

#include "stm32f4xx.h"
#include "delay.h"    /* 兼容:老代码 include gpio_core.h 仍可用,延时函数见 delay.h */

/* GPIO 底层工具：配置引脚、读写电平，不含 LED/按键等外设语义（那些在各自文件里）
 * 参数 : port 端口指针，pin 引脚掩码（可 | 组合）
 * 命名 : GPIO_OutXxx 输出方向，GPIO_InXxx 输入方向；位带宏见 sys_bitband.h
 * 说明 : 换板子无需改动本文件；只有需要操作本层未封装的引脚时才直接调用 */

/* 区块 2：基础功能 */
/* ----------------------------------------------------------------
 *  小节 1：端口时钟辅助
 * ---------------------------------------------------------------- */
/* 使能指定 GPIO 端口的 AHB1 时钟；查表由端口指针换算时钟位，可重复调用
 * 依据 : 标准库写任何 GPIO 寄存器前必须先开该端口时钟，否则写操作被硬件忽略
 * 时机 : GPIO_OutInit / GPIO_InInit 内部已调用；直接使用标准库 API 时才需手动调用
 * 范围 : 支持 GPIOA ~ GPIOI，以芯片实际存在的端口为准；未知端口不做任何事 */
void GPIO_ClockEnable(GPIO_TypeDef *port);


/* 通用 GPIO 输出操作 */
/* 配置为推挽输出：GPIO_OType_PP / GPIO_Speed_100MHz / GPIO_PuPd_NOPULL
 * 参数 : port 端口指针，如 GPIOF；pin 引脚掩码，如 GPIO_Pin_9（可 | 组合多个引脚）
 * 说明 : 配置后引脚为输出模式，初始电平为低；需要上电即高时在 Init 后立即调用 GPIO_OutSet
 * 其它 : 开漏输出改用 GPIO_OutInitOD；其它速度等级用标准库另行配置 */
void    GPIO_OutInit  (GPIO_TypeDef *port, uint16_t pin);

/* 配置为开漏输出：GPIO_OType_OD / GPIO_Speed_100MHz / GPIO_PuPd_NOPULL
 * 用途 : 线与总线（软件模拟 I2C 的 SCL/SDA 用开漏加上拉）、电平转换、多设备占线
 * 约束 : 开漏输出高电平时引脚为高阻态，必须外接上拉电阻才呈现高电平 */
void    GPIO_OutInitOD(GPIO_TypeDef *port, uint16_t pin);

/* 输出高电平 / 输出低电平 / 电平翻转
 * 前提 : 引脚已用 GPIO_OutInit 配置为输出；本组只写 BSRR，不改模式、不开时钟
 * 标准库 : GPIO_SetBits / GPIO_ResetBits / GPIO_ToggleBits */
void    GPIO_OutSet   (GPIO_TypeDef *port, uint16_t pin);   /* 输出高电平 */
void    GPIO_OutReset (GPIO_TypeDef *port, uint16_t pin);   /* 输出低电平 */
void    GPIO_OutToggle(GPIO_TypeDef *port, uint16_t pin);   /* 电平翻转   */

/* 按参数写电平：level 非 0 输出高，level = 0 输出低
 * 用途 : 电平值来自变量或数组时的统一出口（等价 Set / Reset 二选一） */
void    GPIO_OutWrite (GPIO_TypeDef *port, uint16_t pin, uint8_t level);

/* 读输出数据寄存器 ODR：1 = 软件设置为输出高，0 = 输出低
 * 注意 : 读的是软件写入的值，引脚被外部电路拉动时不会反映出来 */
uint8_t GPIO_OutRead  (GPIO_TypeDef *port, uint16_t pin);


/* 通用 GPIO 输入操作 */
/* 配置为输入：内部自动使能端口时钟
 * 参数 : port 端口指针；pin 引脚掩码
 *        pull 上下拉，取值与标准库 GPIO_PuPd_* 编码一致：
 *             GPIO_PuPd_NOPULL = 0 浮空，GPIO_PuPd_UP = 1 上拉，GPIO_PuPd_DOWN = 2 下拉
 *             传入其它值等同 GPIO_PuPd_NOPULL
 * 约束 : 数字输入引脚必须给确定电平，悬空时电平不定会误触发
 *        低电平按下的按键接上拉（GPIO_PuPd_UP）；不同脚可用不同上下拉，按脚分多次调用 */
void    GPIO_InInit(GPIO_TypeDef *port, uint16_t pin, uint8_t pull);

/* 读输入数据寄存器 IDR：引脚上的真实电平
 * 返回 : 1 = 高电平，0 = 低电平
 * 注意 : 即时读取，不含去抖；抖动信号（如机械按键）由上层滤波 */
uint8_t GPIO_InRead(GPIO_TypeDef *port, uint16_t pin);

/* 单引脚掩码转位号（0~15，即 GPIO_PinSourcex）
 * 用途 : GPIO_PinAFConfig 等需要引脚序号而不是掩码的场合
 * 返回 : 0~15 为对应位号；多引脚组合、0 等非法输入返回 0xFF
 * 实现 : 用位运算替代 GPIO_PinSource0~15 宏组 */
uint8_t GPIO_PinSource(uint16_t pin);


/*
 *      引脚占用登记表（GPIO_Claim）：检测多个文件配置同一个脚
 * 问题 : 模块各自 Init 时配自己的脚，没有集中分配表。两个文件把同一
 *   (端口, 位号) 配成输出时，后初始化的会静默改掉前一个的模式与上下拉，
 *   表现为单独测正常、一起跑不对。板2 实例：PB3/PB4/PB5 = W25QXX 的
 *   SPI1 与 NRF24L01 共用；PG9 = DHT11 与 DS18B20；PB10/PB11 = I2C2 与 USART3。
 *   离线工具 本地工具\仓库巡检\pin_audit.py 只能查头文件里声明的脚位，
 *   查不出运行期谁配了谁，本表补这一块。
 *
 * 做法 : GPIO_Init / GPIO_OutInit / GPIO_OutInitOD / GPIO_InInit 四个入口
 *   自动登记（见文件末尾的宏定义）；登记内容为调用点的 __FILE__ 与 __LINE__，
 *   已有调用方无需修改。走标准库 GPIO_Init 的 USART/SPI/I2C/CAN/FSMC 复用脚一并覆盖。
 *
 * 冲突判定 : 同一个 (端口,位号) 被两个不同源文件登记则冲突 +1，两侧位置都记录；
 *   同一文件内配两次不算冲突（常见于模块自己切换方向，如单总线先输出后输入）。
 *
 * 查询 : GPIO_ClaimDump() 打印整张表（带冲突标记与两侧位置）；
 *   GPIO_ClaimCount() / GPIO_ClaimAt(i) 可自行遍历；
 *   GPIO_ClaimConflictCount() 即目标为 0 的那个数。
 *
 * 开销 : 每个调用点多一个字符串常量（文件:行号，约 20~40 字节 Flash）；
 *   RAM 为静态数组（96 条 × 20 字节 ≈ 1920 字节），不做堆分配；
 *   表满后只累加 GPIO_ClaimLost()，不越界。
 *
 * 约束 : 登记只在初始化阶段写，未加锁；运行期多任务动态配脚需自行串行化，
 *   或先调用 GPIO_Claim() 占位。
 */
#define GPIO_CLAIM_WHO_MAX  2U    /* 每个脚最多记 2 处不同文件的来源 */

typedef struct {
    GPIO_TypeDef *port;                  /* 端口 */
    uint16_t      pin;                   /* 引脚掩码（单脚；组合掩码原样记） */
    uint16_t      hits;                  /* 被登记的总次数（含同文件重复） */
    uint8_t       nwho;                  /* who[] 里有效项数 */
    uint8_t       conflict;              /* 1 = 被两个不同文件登记过 */
    const char   *who[GPIO_CLAIM_WHO_MAX];   /* 来源，形如 "..\src\lora_e22.c:180" */
} GpioClaim_t;

/* 登记一个脚（一般不用手动调：上面四个入口已自动登记）
 * 返回 : 0 = 首次登记
 *        1 = 本文件里重复登记（幂等，例如重复 Init）或端口=0 表示没接
 *        2 = 已被另一个文件占了（冲突已计入，但不阻断硬件配置）
 *        3 = 登记表满（已计入 GPIO_ClaimLost） */
uint8_t GPIO_Claim(GPIO_TypeDef *port, uint16_t pin, const char *who);

/* 整表查询 */
uint8_t            GPIO_ClaimCount(void);          /* 已登记条目数 */
const GpioClaim_t *GPIO_ClaimAt(uint8_t index);    /* 越界返回 0 */
uint8_t            GPIO_ClaimConflictCount(void);  /* 有冲突的脚数（目标 0） */
uint16_t           GPIO_ClaimLost(void);           /* 表满后未登记的调用次数 */
void               GPIO_ClaimDump(void);           /* 逐条打印（用 printf，需串口已重定向） */
char               GPIO_PortName(GPIO_TypeDef *port);   /* GPIOA→'A'…GPIOI→'I'，未知→'?' */

/* ---------- 四个入口的自登记包装 ----------
 * 只对包含本头文件的调用方生效。gpio_core.c 自己先定义 GPIO_CORE_IMPL
 * 关掉这几个宏，否则函数定义本身也会被改写。
 * 上面 GPIO_OutInit(...) 等声明用于说明接口形状，实际符号是带 Ex 的三个；
 * 不要 #undef 这些宏，否则链接期找不到符号。 */
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


/*
 *  位带操作（Bit-Band）已拆分为独立文件: sys_bitband.h
 *  内容 : BITBAND_PERIPH / BITBAND_SRAM / GPIO_BB_OUT / _IN /
 *         GPIO_BB_*_ADDR / GPIO_PIN_NUM / Pxout(n)~PIin(n) 等宏。
 *  用法 : 需要时 #include "sys_bitband.h"（纯宏,无编译成本） */


/*
 *  延时函数已拆分为独立文件: delay.h / delay.c
 *  内容 : delay_ms / delay_loop（粗延时）;
 *         delay_cycles / delay_us / delay_ns / delay_ms_dwt（DWT 精准延时）;
 *         DWT_GetCycles / DWT_GetUs / DWT_ElapsedUs（DWT 测时读数）。
 *  说明 : 老代码继续 #include "gpio_core.h" 即可（本文件已转含 delay.h）;
 *         函数名统一小写开头,旧大写名（Delay_xxx）已取消。 */


/*
 *  附:标准库结构体速查, GPIO_TypeDef（定义在 stm32f4xx.h）
 *  库通过指针（如 GPIOA->MODER）操作。下表左列是标准库宏名:
 *    MODER     模式:GPIO_Mode_IN（输入） / GPIO_Mode_OUT（输出） /
 *              GPIO_Mode_AF（复用） / GPIO_Mode_AN（模拟）
 *              （GPIO_Init 配"方向"写的就是它;寄存器编码 00/01/10/11）
 *    OTYPER    输出类型:GPIO_OType_PP（推挽） / GPIO_OType_OD（开漏）
 *              （GPIO_OutInitOD 选的就是 GPIO_OType_OD）
 *    OSPEEDR   输出速度:GPIO_Speed_2MHz / GPIO_Speed_25MHz /
 *              GPIO_Speed_50MHz / GPIO_Speed_100MHz
 *              （GPIO_Init 的 Speed 字段;寄存器编码 00/01/10/11）
 *    PUPDR     上下拉:GPIO_PuPd_NOPULL（无） / GPIO_PuPd_UP（上拉） /
 *              GPIO_PuPd_DOWN（下拉）
 *              （GPIO_InInit 的 pull 参数;决定按键空闲电平）
 *    IDR       输入数据:引脚上的真实电平（GPIO_InRead / 位带读它）
 *    ODR       输出数据:软件写入的电平（位带直写写它的某一位;
 *              GPIO_OutRead 读它）
 *    BSRRL/H   置位/复位:F4 拆成两个 16 位半字,BSRRL 写 1 置位、
 *              BSRRH 写 1 复位（GPIO_SetBits/ResetBits/ToggleBits 写它）
 *    LCKR      配置锁定:锁住引脚配置防误改,库未使用
 *    AFR[2]    复用功能:每个引脚 4 位,存复用号 0~15
 *              （GPIO_PinAFConfig 写它;GPIO_AF_TIM5 等取的就是这的值）
 */


/*
 *  附:GPIO 复用功能(AF)速查表, GPIO_PinAFConfig 的选号依据
 *  AF0 ~ AF15 为 16 套固定映射,写哪个号引脚就接哪套信号。
 *  使能一个复用功能需两步,缺一不生效,先后顺序不限:
 *    1) GPIO_PinAFConfig(端口, 脚序号, 复用号)  选号
 *    2) GPIO_Init(复用模式)                      切到复用
 *
 *  AF 号 → 外设 速查（按 F407ZG 整理,即本模板目标芯片;
 *                [ ] 内为仅高阶型号有的功能,本芯片没有）:
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
 *    AF15 EVENTOUT（内核事件输出到引脚,调试用）
 *
 *  外设 → AF 号 反查:
 *    USART1/2/3 → AF7      USART6、UART4/5 → AF8
 *    I2C1/2/3   → AF4      SPI1/2 → AF5      SPI3 → AF6
 *    TIM1/2 → AF1   TIM3/4/5 → AF2   TIM8~11 → AF3   TIM12~14 → AF9
 *    CAN1/2 → AF9   ETH → AF11   FSMC/SDIO → AF12   DCMI → AF13
 *    （库函数的 af 参数填同名宏: TIM5 → GPIO_AF_TIM5）
 *
 *  约束 :
 *    1) AF 号只说明哪套映射,引脚支不支持要看数据手册的
 *       "Alternate function mapping" 表（以引脚为行）:该脚那行
 *       列出几项,就说明它能干几件事、分别用哪个号。
 *    2) 同一外设信号可出现在多根引脚且复用号相同:USART1_TX 在 PA9 或
 *       PB6 都是 AF7,换引脚时 af 参数照填,只改 port / pin。
 *    3) 一根引脚同一时刻只能接一个外设:PB10/PB11 给了 I2C2,
 *       USART3 就不能再选它。
 *
 *  附:GPIO_PinAFConfig 逐句解析
 *  ----------------------------------------------------------------
 *  函数原型 : void GPIO_PinAFConfig(GPIOx, GPIO_PinSource, GPIO_AF)
 *  AFR 布局 : AFR 是 uint32_t[2], AFR[0] 管 0~7 号脚(AFRL);
 *             AFR[1] 管 8~15 号脚(AFRH);每根脚占 4 位(半字节),
 *             脚 0 占 bit[3:0]、脚 1 占 bit[7:4] …… 脚 7 占 bit[31:28]
 *
 *  源码四步:
 *    1) 选寄存器 : AFR[GPIO_PinSource >> 3]        // >>3 即除以 8
 *                  0~7 号脚 → AFR[0];8~15 号 → AFR[1]
 *    2) 算偏移   : (GPIO_PinSource & 7) * 4        // 取余再乘 4
 *    3) 清零     : AFR[..] &= ~(0xF << 偏移);      // 只清该脚那 4 位
 *    4) 写入     : AFR[..] |= (GPIO_AF << 偏移);   // 放入新复用号
 *    （读-改-写:只动这 4 位,同寄存器其它引脚不受影响）
 *
 *  例:PA9 配成 USART1_TX（AF7）:
 *    9 >> 3 = 1        → 动 AFR[1]
 *    (9 & 7) * 4 = 4   → 动 bit[7:4] 这格
 *    结果 : AFR[1] = (AFR[1] & ~0xF0) | 0x70
 *
 *  约束 : 第二参数是脚序号 0~15（GPIO_PinSourcex 宏）,不是 GPIO_Pin_x 位掩码,
 *    两者数值不同:GPIO_PinSource9 = 9,而 GPIO_Pin_9 = 0x0200（512）。
 *    把掩码当序号传会索引越界,写坏内存。转换工具:运行时 GPIO_PinSource(pin)
 *    函数、编译期 GPIO_PIN_NUM(pin) 宏（见 sys_bitband.h）。
 *
 *  复位后 AFR 全 0,所有脚处于 AF0,SWD 调试口(PA13/PA14)上电即可用,无需配置。
 */

#endif /* __FWLIB_GPIO_CORE_H */
