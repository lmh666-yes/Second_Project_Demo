#include "ext_io.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"

/* ================================================================
 *  ext_io.c —— 【外接】扩展模块封装库  实现文件
 * ================================================================
 *  引脚映射集中在下方"引脚表"，本文件只做"用 id 查表 → 读引脚"。
 *  移植时：改表项（端口/引脚/时钟）即可；数量变化同步改 ext_io.h。
 * ================================================================ */


/* ================================================================
 *                    外接引脚表（配置数据）
 * ================================================================
 * 数组下标 = 对外暴露的 id（0 ~ EXT_XXX_COUNT-1）
 * 表项     = { 端口, 引脚, 所属时钟 }
 *
 * 移植方法（改一行即可）:
 *   把 { GPIOC, GPIO_Pin_6, RCC_AHB1Periph_GPIOC } 三个字段
 *   换成新端口/引脚/时钟，例如改到 PD0：
 *     { GPIOD, GPIO_Pin_0, RCC_AHB1Periph_GPIOD }
 *
 * 注意 :
 *   ① 表项数必须与 ext_io.h 中的 EXT_XXX_COUNT 一致（增删同步改）；
 *   ② clk 字段是"双保险"——GPIO_InInit 内部本就会自动使能时钟，
 *      这里保留是为了让表项自洽、便于集中查改。
 * ================================================================ */
typedef struct {
    GPIO_TypeDef *port;   /* 引脚端口 */
    uint16_t      pin;    /* 引脚掩码（GPIO_Pin_x） */
    uint32_t      clk;    /* 对应 RCC_AHB1Periph_GPIOx（双保险，见上方注意②） */
} ExtPin_t;

/* 红外避障：id 0/1/2 → PC6 / PC7 / PC8 */
static const ExtPin_t ext_ir_list[EXT_IR_COUNT] = {
    { GPIOC, GPIO_Pin_6, RCC_AHB1Periph_GPIOC },
    { GPIOC, GPIO_Pin_7, RCC_AHB1Periph_GPIOC },
    { GPIOC, GPIO_Pin_8, RCC_AHB1Periph_GPIOC },
};

/* 循迹传感器：id 0/1 → PC9 / PC10 */
static const ExtPin_t ext_trace_list[EXT_TRACE_COUNT] = {
    { GPIOC, GPIO_Pin_9,  RCC_AHB1Periph_GPIOC },
    { GPIOC, GPIO_Pin_10, RCC_AHB1Periph_GPIOC },
};

/* 触摸/碰撞：id 0 → PC11 */
static const ExtPin_t ext_touch_list[EXT_TOUCH_COUNT] = {
    { GPIOC, GPIO_Pin_11, RCC_AHB1Periph_GPIOC },
};

/* 声音检测：id 0 → PC12 */
static const ExtPin_t ext_sound_list[EXT_SOUND_COUNT] = {
    { GPIOC, GPIO_Pin_12, RCC_AHB1Periph_GPIOC },
};

/* 编译期护栏：各表项数必须与 ext_io.h 的 COUNT 宏一致（不一致则此处直接编译不过） */
typedef char ext_ir_count_check   [(sizeof(ext_ir_list)    / sizeof(ext_ir_list[0])    == EXT_IR_COUNT)    ? 1 : -1];
typedef char ext_trace_count_check[(sizeof(ext_trace_list) / sizeof(ext_trace_list[0]) == EXT_TRACE_COUNT) ? 1 : -1];
typedef char ext_touch_count_check[(sizeof(ext_touch_list) / sizeof(ext_touch_list[0]) == EXT_TOUCH_COUNT) ? 1 : -1];
typedef char ext_sound_count_check[(sizeof(ext_sound_list) / sizeof(ext_sound_list[0]) == EXT_SOUND_COUNT) ? 1 : -1];


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

/* 第一层：通用打底——四类外接引脚统一进入"安全默认态"
 * 新增一类模块时，记得在这里追一行 ext_base_init(xxx, COUNT) */
void EXT_IO_Init(void)
{
    ext_base_init(ext_ir_list,    EXT_IR_COUNT);
    ext_base_init(ext_trace_list, EXT_TRACE_COUNT);
    ext_base_init(ext_touch_list, EXT_TOUCH_COUNT);
    ext_base_init(ext_sound_list, EXT_SOUND_COUNT);
}

/* 第二层：各模块专属覆盖——默认态已满足需求，当前留空。
 * 需要个性化配置（改上下拉、换速度等）时在对应函数内追加。 */
void EXT_IR_Init(void)    { }
void EXT_TRACE_Init(void) { }
void EXT_TOUCH_Init(void) { }
void EXT_SOUND_Init(void) { }

/* 极性换算：把"原始电平"翻译成"是否检测到"（四个模块共用一份）
 *   active_low=1 → 低电平（0）= 检测到
 *   active_low=0 → 高电平（1）= 检测到 */
static uint8_t ext_to_detected(uint8_t level, uint8_t active_low)
{
    return active_low ? ((level == 0) ? 1 : 0)
                      : ((level != 0) ? 1 : 0);
}

/* 红外检测：id 越界返回 0（安全防护） */
uint8_t EXT_IR_Detected(uint8_t id)
{
    if (id >= EXT_IR_COUNT) return 0;
    return ext_to_detected(GPIO_InRead(ext_ir_list[id].port, ext_ir_list[id].pin),
                           EXT_IR_ACTIVE_LOW);
}

/* 循迹检测 */
uint8_t EXT_TRACE_Detected(uint8_t id)
{
    if (id >= EXT_TRACE_COUNT) return 0;
    return ext_to_detected(GPIO_InRead(ext_trace_list[id].port, ext_trace_list[id].pin),
                           EXT_TRACE_ACTIVE_LOW);
}

/* 触摸检测 */
uint8_t EXT_TOUCH_Detected(uint8_t id)
{
    if (id >= EXT_TOUCH_COUNT) return 0;
    return ext_to_detected(GPIO_InRead(ext_touch_list[id].port, ext_touch_list[id].pin),
                           EXT_TOUCH_ACTIVE_LOW);
}

/* 声音检测 */
uint8_t EXT_SOUND_Detected(uint8_t id)
{
    if (id >= EXT_SOUND_COUNT) return 0;
    return ext_to_detected(GPIO_InRead(ext_sound_list[id].port, ext_sound_list[id].pin),
                           EXT_SOUND_ACTIVE_LOW);
}


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */

/* 统计辅助：一张引脚表里"检测到"的路数 */
static uint8_t ext_count_detected(const ExtPin_t *list, uint8_t count, uint8_t active_low)
{
    uint8_t n = 0;

    for (uint8_t i = 0; i < count; i++) {
        if (ext_to_detected(GPIO_InRead(list[i].port, list[i].pin), active_low)) {
            n++;
        }
    }
    return n;
}

/* 红外：检测到几路（0 ~ EXT_IR_COUNT） */
uint8_t EXT_IR_CountDetected(void)
{
    return ext_count_detected(ext_ir_list, EXT_IR_COUNT, EXT_IR_ACTIVE_LOW);
}

/* 循迹：检测到几路 */
uint8_t EXT_TRACE_CountDetected(void)
{
    return ext_count_detected(ext_trace_list, EXT_TRACE_COUNT, EXT_TRACE_ACTIVE_LOW);
}

/* 触摸：检测到几路（通常 0 或 1） */
uint8_t EXT_TOUCH_CountDetected(void)
{
    return ext_count_detected(ext_touch_list, EXT_TOUCH_COUNT, EXT_TOUCH_ACTIVE_LOW);
}

/* 声音：检测到几路（通常 0 或 1） */
uint8_t EXT_SOUND_CountDetected(void)
{
    return ext_count_detected(ext_sound_list, EXT_SOUND_COUNT, EXT_SOUND_ACTIVE_LOW);
}

