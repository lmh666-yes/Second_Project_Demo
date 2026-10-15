#define GPIO_CORE_IMPL 1    /* 必须先定义再包含 gpio_core.h：让 .h 末尾的自登记转发宏生效，
                             * 下面这些函数定义会被宏改写。 */
#include "gpio_core.h"
#include <string.h>         /* strcmp / strncmp / strrchr：登记表的"同一文件"判定 */
#include <stdio.h>          /* printf：GPIO_ClaimDump() 打印整张表 */
/* 接口说明见 gpio_core.h */

/* 通用 GPIO 工具函数的实现：端口/引脚全部由调用方传入。 */


/* 端口指针与 AHB1 时钟位掩码的对照表
 * 上层传入端口指针，本表换算成 RCC_AHB1Periph_GPIOx 位掩码供 RCC 使用
 * 每项用 #if defined(GPIOx) 保护：只有芯片存在的端口入表，可直接复用到
 * 其它 STM32F4 型号 */
typedef struct {
    GPIO_TypeDef *port;
    uint32_t      clk;
} PortClock_t;

static const PortClock_t port_clock_map[] = {
#if defined(GPIOA)
    { GPIOA, RCC_AHB1Periph_GPIOA },
#endif
#if defined(GPIOB)
    { GPIOB, RCC_AHB1Periph_GPIOB },
#endif
#if defined(GPIOC)
    { GPIOC, RCC_AHB1Periph_GPIOC },
#endif
#if defined(GPIOD)
    { GPIOD, RCC_AHB1Periph_GPIOD },
#endif
#if defined(GPIOE)
    { GPIOE, RCC_AHB1Periph_GPIOE },
#endif
#if defined(GPIOF)
    { GPIOF, RCC_AHB1Periph_GPIOF },
#endif
#if defined(GPIOG)
    { GPIOG, RCC_AHB1Periph_GPIOG },
#endif
#if defined(GPIOH)
    { GPIOH, RCC_AHB1Periph_GPIOH },
#endif
#if defined(GPIOI)
    { GPIOI, RCC_AHB1Periph_GPIOI },
#endif
};

#define PORT_CLOCK_MAP_SIZE   (sizeof(port_clock_map) / sizeof(port_clock_map[0]))

/* 引脚占用登记表（GPIO_Claim）实现
 * 冲突只算不同源文件之间的抢占：同一文件对同一个脚重复配置是正常用法，
 * 判据为 who 中最后一个 ':' 之前的文件名部分是否相同
 * 表满不覆盖、不越界，只累加 GPIO_ClaimLost() 表示有登记未记上 */
#define GPIO_CLAIM_MAX   96U    /* 登记条目上限；每条约 20 字节 RAM（静态） */

static GpioClaim_t s_claim[GPIO_CLAIM_MAX];
static uint8_t     s_claim_n        = 0U;   /* 已用条目数 */
static uint8_t     s_claim_conflict = 0U;   /* 被两个以上文件抢掉的脚数 */
static uint16_t    s_claim_lost     = 0U;   /* 表满后没能登记的调用次数 */

/* 判断两个记录是否来自同一源文件:比最后一个 ':' 之前的文件名部分 */
static uint8_t claim_same_file(const char *a, const char *b)
{
    const char   *ca;
    const char   *cb;
    unsigned int  la;
    unsigned int  lb;

    if ((a == 0) || (b == 0)) return 0U;

    ca = strrchr(a, ':');       /* who 形如 "..\FWLIB\src\lora_e22.c:180" */
    cb = strrchr(b, ':');
    if ((ca == 0) || (cb == 0)) return (uint8_t)(strcmp(a, b) == 0);

    la = (unsigned int)(ca - a);
    lb = (unsigned int)(cb - b);
    if (la != lb) return 0U;
    return (uint8_t)(strncmp(a, b, (size_t)la) == 0);
}

/* 端口指针转端口字母，仅用于打印，不参与判断
 * 每项 #if 保护，可移植到只有部分端口的型号 */
char GPIO_PortName(GPIO_TypeDef *port)
{
#if defined(GPIOA)
    if (port == GPIOA) return 'A';
#endif
#if defined(GPIOB)
    if (port == GPIOB) return 'B';
#endif
#if defined(GPIOC)
    if (port == GPIOC) return 'C';
#endif
#if defined(GPIOD)
    if (port == GPIOD) return 'D';
#endif
#if defined(GPIOE)
    if (port == GPIOE) return 'E';
#endif
#if defined(GPIOF)
    if (port == GPIOF) return 'F';
#endif
#if defined(GPIOG)
    if (port == GPIOG) return 'G';
#endif
#if defined(GPIOH)
    if (port == GPIOH) return 'H';
#endif
#if defined(GPIOI)
    if (port == GPIOI) return 'I';
#endif
    return '?';
}

/* 登记一个脚，返回码见 gpio_core.h
 * 只记录并暴露冲突，不阻断硬件配置 */
uint8_t GPIO_Claim(GPIO_TypeDef *port, uint16_t pin, const char *who)
{
    uint8_t i;

    if (port == 0) return 1U;   /* "这个脚没接"（端口传 0）：不登记 */

    for (i = 0U; i < s_claim_n; i++) {
        if ((s_claim[i].port == port) && (s_claim[i].pin == pin)) {
            s_claim[i].hits++;
            if (claim_same_file(s_claim[i].who[0], who) != 0U) {
                return 1U;      /* 本文件里重复配置：幂等（例如重复 Init） */
            }
            if (s_claim[i].conflict == 0U) {
                s_claim[i].conflict = 1U;
                s_claim_conflict++;
#if (GPIO_CLAIM_WHO_MAX > 1U)
                s_claim[i].who[1]   = who;      /* 记下"抢脚"的另一方 */
#endif
                /* 发现冲突立即打印一行告警：串口未初始化时这一行会丢，表里仍记着，
                 * Dump 一定打得出；打印串只用 ASCII，原因见 GPIO_ClaimDump */
                printf("\r\n[GPIO_Claim] CONFLICT %c%u : %s  vs  %s\r\n",
                       GPIO_PortName(port), (unsigned int)GPIO_PinSource(pin),
                       s_claim[i].who[0], who);
            }
            s_claim[i].nwho = GPIO_CLAIM_WHO_MAX;
            return 2U;          /* 已被另一个文件占了 */
        }
    }

    if (s_claim_n >= (uint8_t)GPIO_CLAIM_MAX) {
        s_claim_lost++;
        return 3U;              /* 表满：只计数，不越界 */
    }

    s_claim[s_claim_n].port     = port;
    s_claim[s_claim_n].pin      = pin;
    s_claim[s_claim_n].hits     = 1U;
    s_claim[s_claim_n].nwho     = 1U;
    s_claim[s_claim_n].conflict = 0U;
    s_claim[s_claim_n].who[0]   = who;
#if (GPIO_CLAIM_WHO_MAX > 1U)
    s_claim[s_claim_n].who[1]   = 0;
#endif
    s_claim_n++;
    return 0U;
}

uint8_t GPIO_ClaimCount(void)
{
    return s_claim_n;
}

const GpioClaim_t *GPIO_ClaimAt(uint8_t index)
{
    if (index >= s_claim_n) return 0;
    return &s_claim[index];
}

uint8_t GPIO_ClaimConflictCount(void)
{
    return s_claim_conflict;
}

uint16_t GPIO_ClaimLost(void)
{
    return s_claim_lost;
}

/* 打印整张表，逐条形如：
 *     [A5] 2 hits  ** taken by 2 files **
 *           ..\FWLIB\src\ntc_pt100.c:98    (first)
 *           ..\FWLIB\src\lora_e22.c:180    (** second - fix here)
 * 前提：printf 已重定向到串口
 * 打印串只用 ASCII：Keil AC5 按系统 ANSI 解析源码，本库源码为 UTF-8，
 * 中文字符串字面量会触发 #870-D invalid multibyte character sequence
 * 与 #8 missing closing quote */
void GPIO_ClaimDump(void)
{
    uint8_t i;
    uint8_t j;
    uint8_t s;

    printf("\r\n==== GPIO_Claim pin registry ====\r\n");
    for (i = 0U; i < s_claim_n; i++) {
        s = GPIO_PinSource(s_claim[i].pin);     /* 组合掩码返回 0xFF */
        if (s != 0xFFU) {
            printf("[%c%u] ", GPIO_PortName(s_claim[i].port), (unsigned int)s);
        } else {
            printf("[%c*0x%04X] ", GPIO_PortName(s_claim[i].port),
                   (unsigned int)s_claim[i].pin);
        }
        printf("%u hits", (unsigned int)s_claim[i].hits);
        if (s_claim[i].conflict != 0U) {
            printf("  ** taken by %u files **", (unsigned int)s_claim[i].nwho);
        }
        printf("\r\n");

        for (j = 0U; j < s_claim[i].nwho; j++) {
            if (s_claim[i].who[j] != 0) {
                printf("      %s%s\r\n", s_claim[i].who[j],
                       (j == 0U) ? "   (first)" : "   (** second - fix here)");
            }
        }
    }
    printf("total %u pins, conflict %u, not-recorded(table full) %u\r\n",
           (unsigned int)s_claim_n, (unsigned int)s_claim_conflict,
           (unsigned int)s_claim_lost);
    printf("==== target : conflict 0 / lost 0 ====\r\n");
}

/* 带登记的 GPIO_Init：gpio_core.h 把 GPIO_Init 重定义成宏转发到这里，
 * who 由宏在调用点取 __FILE__/__LINE__；全库配脚动作都进登记表，
 * USART/SPI/I2C/CAN/FSMC 直接调标准库 GPIO_Init 的复用脚也覆盖 */
void GPIO_InitEx(GPIO_TypeDef *port, GPIO_InitTypeDef *init, const char *who)
{
    if (init != 0) {
        (void)GPIO_Claim(port, init->GPIO_Pin, who);
    }
    GPIO_Init(port, init);
}

/* 自动使能指定端口的 AHB1 时钟
 * 幂等：重复调用无副作用
 * 未收录的端口不操作，避免误改其它寄存器 */
void GPIO_ClockEnable(GPIO_TypeDef *port)
{
    for (uint8_t i = 0; i < (uint8_t)PORT_CLOCK_MAP_SIZE; i++) {
        if (port_clock_map[i].port == port) {
            RCC_AHB1PeriphClockCmd(port_clock_map[i].clk, ENABLE);
            return;
        }
    }
}


/* 通用 GPIO 输出 */
/* 推挽输出初始化，固定配置：
 *   Mode=GPIO_Mode_OUT / OType=GPIO_OType_PP / Speed=GPIO_Speed_100MHz / PuPd=GPIO_PuPd_NOPULL
 * 其它速度等级用标准库 API 另行配置 */
void GPIO_OutInitEx(GPIO_TypeDef *port, uint16_t pin, const char *who)
{
    GPIO_InitTypeDef gi;

    (void)GPIO_Claim(port, pin, who);   /* 登记；who 由 .h 的宏在调用点自动填 */
    GPIO_ClockEnable(port);

    gi.GPIO_Pin   = pin;
    gi.GPIO_Mode  = GPIO_Mode_OUT;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(port, &gi);
}

/* 开漏输出初始化：与 GPIO_OutInitEx 只差 OType 与上下拉
 * 用 GPIO_PuPd_UP 的依据：
 *   开漏输出的高电平靠外部上拉，使用者都是单总线器件（DHT11、DS18B20），
 *   空闲必须为高，典型外部上拉 4.7k~10k，未接外部上拉时引脚浮空
 *   内部弱上拉约 30~50kΩ 不能替代 4.7k 外部上拉（边沿会变缓），但可避免浮空
 *   OD 输出写 1 时不驱动高电平，内部上拉不会被拉低 */
void GPIO_OutInitODEx(GPIO_TypeDef *port, uint16_t pin, const char *who)
{
    GPIO_InitTypeDef gi;

    (void)GPIO_Claim(port, pin, who);   /* 登记 */
    GPIO_ClockEnable(port);

    gi.GPIO_Pin   = pin;
    gi.GPIO_Mode  = GPIO_Mode_OUT;
    gi.GPIO_OType = GPIO_OType_OD;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;       /* 单总线空闲必须为高，见上方说明 */
    GPIO_Init(port, &gi);
}

/* 电平操作：直接写 BSRR 寄存器，单次写生效，无需读-改-写
 * 前提：引脚已配置为输出，本组不负责时钟与模式 */
void GPIO_OutSet   (GPIO_TypeDef *port, uint16_t pin) { GPIO_SetBits   (port, pin); }
void GPIO_OutReset (GPIO_TypeDef *port, uint16_t pin) { GPIO_ResetBits (port, pin); }
void GPIO_OutToggle(GPIO_TypeDef *port, uint16_t pin) { GPIO_ToggleBits(port, pin); }

/* 按参数写电平：非 0 输出高（BSRR 置位），0 输出低（BSRR 复位位） */
void GPIO_OutWrite (GPIO_TypeDef *port, uint16_t pin, uint8_t level)
{
    if (level != 0U) {
        GPIO_SetBits(port, pin);
    } else {
        GPIO_ResetBits(port, pin);
    }
}

/* 读输出数据寄存器 ODR：返回软件设置的电平，不是引脚真实电平
 * 用于确认已写入的输出状态 */
uint8_t GPIO_OutRead(GPIO_TypeDef *port, uint16_t pin)
{
    return GPIO_ReadOutputDataBit(port, pin);
}


/* 通用 GPIO 输入 */
/* 输入初始化：Mode=IN，按 pull 参数选择上下拉
 *   pull：1=上拉（空闲高） / 2=下拉（空闲低） / 其它=浮空
 * 与输出组一样先登记并开时钟 */
void GPIO_InInitEx(GPIO_TypeDef *port, uint16_t pin, uint8_t pull, const char *who)
{
    GPIO_InitTypeDef gi;

    (void)GPIO_Claim(port, pin, who);   /* 登记 */
    GPIO_ClockEnable(port);

    gi.GPIO_Pin   = pin;
    gi.GPIO_Mode  = GPIO_Mode_IN;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    gi.GPIO_OType = GPIO_OType_PP;

    switch (pull) {
        case 1:  gi.GPIO_PuPd = GPIO_PuPd_UP;     break;
        case 2:  gi.GPIO_PuPd = GPIO_PuPd_DOWN;   break;
        default: gi.GPIO_PuPd = GPIO_PuPd_NOPULL; break;
    }
    GPIO_Init(port, &gi);
}

/* 读输入数据寄存器 IDR：引脚上的真实电平（1=高，0=低）
 * 即时读取，不含去抖，按键类抖动信号需上层做软件滤波 */
uint8_t GPIO_InRead(GPIO_TypeDef *port, uint16_t pin)
{
    return GPIO_ReadInputDataBit(port, pin);
}

/* 单引脚掩码转位号（0~15），GPIO_PinAFConfig 用的是位号
 * 库内引脚均以单引脚掩码配置；多引脚组合或 0 返回 0xFF（非法） */
uint8_t GPIO_PinSource(uint16_t pin)
{
    for (uint8_t s = 0; s < 16U; s++) {
        if (pin == (uint16_t)(1UL << s)) return s;
    }
    return 0xFFU;
}


/* 延时实现均在 delay.c：delay_ms / delay_loop（粗延时）、
 * delay_cycles / delay_us / delay_ns / delay_ms_dwt（DWT 精准延时）、
 * DWT_GetCycles / DWT_GetUs / DWT_ElapsedUs（DWT 测时读数） */
