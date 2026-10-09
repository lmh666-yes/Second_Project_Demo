#include "ext_io.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_us 等）独立文件 */

/* ================================================================
 *  ext_io.c —— 【板载/外接】单引脚数字输入模块封装库  实现文件
 * ================================================================
 *  引脚映射集中在下方"引脚表"，本文件只做"用 id 查表 → 读引脚"。
 *  移植时：改表项（端口/引脚/时钟）即可；数量变化同步改 ext_io.h。
 *
 *  【天马 F407 开发板 · 引脚来源（普中-天马 F407开发板原理图）】
 *    红外接收头 IR1 数据脚 → PA8   （原理图网络名 IRED）
 *    电容触摸按键 Touch_Key → PA5  （与 STM_ADC 排针共用，见原理图 J8）
 *    板载光敏 LS1 分压输出  → PF7  （原理图网络名 LIGHT）
 *    WiFi 模块按键 W_KEY    → PF6
 *    外接红外避障模块 x4    → PC10 / PC11 / PC12 / PC8
 *                             ★这不是板载器件，需自己买 4 个接排针上
 * ================================================================ */


/* ================================================================
 *                    引脚表（配置数据）
 * ================================================================
 * 数组下标 = 对外暴露的 id（0 ~ EXT_XXX_COUNT-1）
 * 表项     = { 端口, 引脚, 所属时钟 }
 *
 * 移植方法（改一行即可）:
 *   把 { GPIOA, GPIO_Pin_8, RCC_AHB1Periph_GPIOA } 三个字段
 *   换成新端口/引脚/时钟，例如改到 PD0：
 *     { GPIOD, GPIO_Pin_0, RCC_AHB1Periph_GPIOD }
 *
 * 注意 :
 *   ① 表项数必须与 ext_io.h 中的 EXT_XXX_COUNT 一致（增删同步改）；
 *   ② clk 字段是"双保险"——GPIO_InInit 内部本就会自动使能时钟，
 *      这里保留是为了让表项自洽、便于集中查改。
 * ================================================================ */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint32_t      clk;
} ExtPin_t;

/* 红外接收头：id 0 → PA8（原理图 IRED） */
static const ExtPin_t ext_ir_list[EXT_IR_COUNT] = {
    { GPIOA, GPIO_Pin_8, RCC_AHB1Periph_GPIOA },
};

/* 电容触摸按键：id 0 → PA5（原理图 Touch_Key） */
static const ExtPin_t ext_touch_list[EXT_TOUCH_COUNT] = {
    { GPIOA, GPIO_Pin_5, RCC_AHB1Periph_GPIOA },
};

/* 板载光敏：id 0 → PF7（原理图 LIGHT；与 sys_adc 共用该脚） */
static const ExtPin_t ext_light_list[EXT_LIGHT_COUNT] = {
    { GPIOF, GPIO_Pin_7, RCC_AHB1Periph_GPIOF },
};

/* WiFi 模块按键：id 0 → PF6（原理图 W_KEY） */
static const ExtPin_t ext_key_list[EXT_KEY_COUNT] = {
    { GPIOF, GPIO_Pin_6, RCC_AHB1Periph_GPIOF },
};

/* 外接红外避障模块：★自己接线的，引脚按小车实际布局排
 *   建议顺序 : 0 = 前左  1 = 右  2 = 后右  3 = 左（视你的安装而定）
 *   改引脚只需改这里；数量变化同步改 ext_io.h 的 EXT_OBS_COUNT */
static const ExtPin_t ext_obs_list[EXT_OBS_COUNT] = {
    { GPIOC, GPIO_Pin_10, RCC_AHB1Periph_GPIOC },   /* P2-24 : 避障 0 */
    { GPIOC, GPIO_Pin_11, RCC_AHB1Periph_GPIOC },   /* P2-23 : 避障 1 */
    { GPIOC, GPIO_Pin_12, RCC_AHB1Periph_GPIOC },   /* P2-22 : 避障 2 */
    { GPIOC, GPIO_Pin_8,  RCC_AHB1Periph_GPIOC },   /* P2-32 : 避障 3 */
};

/* 编译期护栏：各表项数必须与 ext_io.h 的 COUNT 宏一致（不一致则此处直接编译不过） */
typedef char ext_ir_count_check   [(sizeof(ext_ir_list)    / sizeof(ext_ir_list[0])    == EXT_IR_COUNT)    ? 1 : -1];
typedef char ext_touch_count_check[(sizeof(ext_touch_list) / sizeof(ext_touch_list[0]) == EXT_TOUCH_COUNT) ? 1 : -1];
typedef char ext_light_count_check[(sizeof(ext_light_list) / sizeof(ext_light_list[0]) == EXT_LIGHT_COUNT) ? 1 : -1];
typedef char ext_key_count_check  [(sizeof(ext_key_list)   / sizeof(ext_key_list[0])   == EXT_KEY_COUNT)   ? 1 : -1];
typedef char ext_obs_count_check  [(sizeof(ext_obs_list)   / sizeof(ext_obs_list[0])   == EXT_OBS_COUNT)   ? 1 : -1];


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */

/* 打底辅助：把一张引脚表整体配置为"输入 + EXT_BASE_PULL"
 * 说明 : RCC 使能 + 引脚配置两件事在此完成；
 *       表项里的 clk 用来开时钟，GPIO_InInit 内部还会再开一次
 *       （幂等操作，重复使能不产生副作用） */
static void ext_base_init(const ExtPin_t *list, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        RCC_AHB1PeriphClockCmd(list[i].clk, ENABLE);
        GPIO_InInit(list[i].port, list[i].pin, EXT_BASE_PULL);
    }
}

/* 第一层：通用打底——各引脚统一进入"安全默认态"
 * 新增一类模块时，记得在这里追一行 ext_base_init(xxx, COUNT) */
void EXT_IO_Init(void)
{
    ext_base_init(ext_ir_list,    EXT_IR_COUNT);
    ext_base_init(ext_touch_list, EXT_TOUCH_COUNT);
    ext_base_init(ext_light_list, EXT_LIGHT_COUNT);
    ext_base_init(ext_key_list,   EXT_KEY_COUNT);
    ext_base_init(ext_obs_list,   EXT_OBS_COUNT);
}

/* 第二层：各模块专属覆盖——默认态已满足需求，当前留空。
 * 需要个性化配置（改上下拉、换速度等）时在对应函数内追加。 */
void EXT_IR_Init(void)    { }
void EXT_TOUCH_Init(void) { }
void EXT_LIGHT_Init(void) { }
void EXT_KEY_Init(void)   { }
void EXT_OBS_Init(void)   { }

/* 极性换算：把"原始电平"翻译成"是否检测到"（四类模块共用一份）
 *   active_low=1 → 低电平（0）= 检测到
 *   active_low=0 → 高电平（1）= 检测到 */
static uint8_t ext_to_detected(uint8_t level, uint8_t active_low)
{
    return active_low ? ((level == 0) ? 1 : 0)
                      : ((level != 0) ? 1 : 0);
}

/* 红外接收头检测：id 越界返回 0（安全防护） */
uint8_t EXT_IR_Detected(uint8_t id)
{
    if (id >= EXT_IR_COUNT) return 0;
    return ext_to_detected(GPIO_InRead(ext_ir_list[id].port, ext_ir_list[id].pin),
                           EXT_IR_ACTIVE_LOW);
}

/* 电容触摸按键检测
 * ⚠ 不能用 GPIO 数字读！见 .h 文件头的两个拓坑说明。
 * 实现：采一次充电时间，与"首帧自动校准得到"的基准比较。 */
#if (EXT_TOUCH_MODE_CHARGE)
static uint32_t ext_touch_base[EXT_TOUCH_COUNT];
static uint8_t  ext_touch_cal [EXT_TOUCH_COUNT];

/* 单次测量：放电 → 浮空输入（靠外部 1M 充电）→ 量到引脚变高的微秒数
 * 返回 : 充电时间（µs）；超时返回 EXT_TOUCH_TIMEOUT_US
 * 说明 : 必须用"浮空输入"——若开内部上拉（约 40K），充电会快得多，
 *        时间常数小到 µs 分辨率下不可测，手指带来的电容变化就被淹没了。 */
uint32_t EXT_TOUCH_ChargeTimeUs(uint8_t id)
{
    const ExtPin_t *p;
    uint32_t t0;
    uint32_t t;

    if (id >= EXT_TOUCH_COUNT) return 0;

    p = &ext_touch_list[id];

    /* ① 放电：推挽输出（GPIO_OType_PP）拉低，等电容放干净 */
    GPIO_OutInit(p->port, p->pin);
    GPIO_OutReset(p->port, p->pin);
    delay_us(5);

    /* ② 开始充电：切成浮空输入（GPIO_PuPd_NOPULL），外部 1M 开始充电 */
    GPIO_InInit(p->port, p->pin, GPIO_PuPd_NOPULL);

    t0 = DWT_GetUs();
    while (GPIO_InRead(p->port, p->pin) == 0) {
        if (DWT_ElapsedUs(t0) > EXT_TOUCH_TIMEOUT_US) break;
    }
    t = DWT_ElapsedUs(t0);

    return (t > EXT_TOUCH_TIMEOUT_US) ? EXT_TOUCH_TIMEOUT_US : t;
}

/* 重新校准：把当前状态当作"未触摸"基准
 * 用途 : 上电稳定后、换环境（温湿度变化大）时调一次 */
void EXT_TOUCH_Calibrate(uint8_t id)
{
    if (id >= EXT_TOUCH_COUNT) return;

    ext_touch_base[id] = EXT_TOUCH_ChargeTimeUs(id);
    ext_touch_cal [id] = 1;
}

/* 取基准值（µs；未校准时为 0） */
uint32_t EXT_TOUCH_GetBaseline(uint8_t id)
{
    if (id >= EXT_TOUCH_COUNT) return 0;
    return ext_touch_base[id];
}
#endif /* EXT_TOUCH_MODE_CHARGE */

/* 电容触摸按键检测：1 = 摸到，0 = 未摸到 */
uint8_t EXT_TOUCH_Detected(uint8_t id)
{
    if (id >= EXT_TOUCH_COUNT) return 0;

#if (EXT_TOUCH_MODE_CHARGE)
    {
        uint32_t t = EXT_TOUCH_ChargeTimeUs(id);

        /* 首帧自动校准，避免"上电第一下就误触发" */
        if (ext_touch_cal[id] == 0) {
            ext_touch_base[id] = t;
            ext_touch_cal [id] = 1;
            return 0;
        }

        if (t > ext_touch_base[id] + EXT_TOUCH_DELTA_US) return 1;

        /* 未触摸时缓慢跟踪基准（抵消温湿度引起的漂移） */
        ext_touch_base[id] = (ext_touch_base[id] * 7U + t) / 8U;
        return 0;
    }
#else
    return ext_to_detected(GPIO_InRead(ext_touch_list[id].port, ext_touch_list[id].pin),
                           EXT_TOUCH_ACTIVE_LOW);
#endif
}

/* 板载光敏检测
 * ⚠ PF7 = LIGHT 是**模拟分压点**（47K 上拉 + 光敏电阻到地），
 *   这里只是把它当数字输入做了个"亮/暗粗阀值"；
 *   要精确光照值请用 sys_adc 读 PF7（ADC3_IN5）。 */
uint8_t EXT_LIGHT_Detected(uint8_t id)
{
    if (id >= EXT_LIGHT_COUNT) return 0;
    return ext_to_detected(GPIO_InRead(ext_light_list[id].port, ext_light_list[id].pin),
                           EXT_LIGHT_ACTIVE_LOW);
}

/* WiFi 模块按键检测 */
uint8_t EXT_KEY_Detected(uint8_t id)
{
    if (id >= EXT_KEY_COUNT) return 0;
    return ext_to_detected(GPIO_InRead(ext_key_list[id].port, ext_key_list[id].pin),
                           EXT_KEY_ACTIVE_LOW);
}

/* 外接红外避障模块检测：有障碍 → 1
 * 说明 : 纯数字读，没做消抖 —— 避障模块自带比较器和迟滞，
 *        而且车碰到波动时本来就该保持原来的转向，所以这里不需要消抖 */
uint8_t EXT_OBS_Detected(uint8_t id)
{
    if (id >= EXT_OBS_COUNT) return 0;
    return ext_to_detected(GPIO_InRead(ext_obs_list[id].port, ext_obs_list[id].pin),
                           EXT_OBS_ACTIVE_LOW);
}


/* ================================================================
 *                    区块 3：扩展功能（多路计数）
 * ================================================================ */
/* 统计"检测到"的路数：逐路复用各 Detected 路径
 * （各 Detected 内部已含越界/极性处理，这里直接调用即可） */
uint8_t EXT_IR_CountDetected(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < EXT_IR_COUNT; i++) {
        if (EXT_IR_Detected(i)) n++;
    }
    return n;
}

uint8_t EXT_TOUCH_CountDetected(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < EXT_TOUCH_COUNT; i++) {
        if (EXT_TOUCH_Detected(i)) n++;
    }
    return n;
}

uint8_t EXT_LIGHT_CountDetected(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < EXT_LIGHT_COUNT; i++) {
        if (EXT_LIGHT_Detected(i)) n++;
    }
    return n;
}

uint8_t EXT_KEY_CountDetected(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < EXT_KEY_COUNT; i++) {
        if (EXT_KEY_Detected(i)) n++;
    }
    return n;
}

uint8_t EXT_OBS_CountDetected(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < EXT_OBS_COUNT; i++) {
        if (EXT_OBS_Detected(i)) n++;
    }
    return n;
}
