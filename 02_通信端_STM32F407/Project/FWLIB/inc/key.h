#ifndef __FWLIB_KEY_H
#define __FWLIB_KEY_H

#include "stm32f4xx.h"

/* key.h: 板载按键模块头文件；依赖 gpio_core.h（GPIO 输入工具），中断组基于 sys_exti.h
 * 电平极性每键独立（KEYx_ACTIVE_LOW），函数内部自动适配：本板 KEY_UP(PA0) 高有效，其余三键低有效
 * KEY_Read(id) 读即时状态，无消抖；KEY_Scan() 上报按下事件，含 KEY_DEBOUNCE_MS 消抖
 * 换板子只改区块 1 的四件套：端口 / 引脚 / 极性 / 上下拉 */


/* 区块 1：定义与宏定义区（换板子只改这里） */
/* 按键引脚 */
/* 每个按键四个宏一套（端口 / 引脚 / 极性 / 上下拉），x 即对外暴露的 id
 * 天马 F407 板 4 键（对照《普中-天马 F407开发板原理图》）：id 0 KEY0 PE4、id 1 KEY1 PE3、
 * id 2 KEY2 PE2 另一端接 GND，按下为低；id 3 KEY_UP PA0 另一端接 3.3V，按下为高
 * PA0 同时是 WKUP 唤醒脚（见 sys_pwr）；PE2/PE3/PE4 在别的板上可能是 FSMC/TRACE 脚 */
#define KEY0_PORT   GPIOE
#define KEY0_PIN    GPIO_Pin_4
#define KEY1_PORT   GPIOE
#define KEY1_PIN    GPIO_Pin_3
#define KEY2_PORT   GPIOE
#define KEY2_PIN    GPIO_Pin_2
#define KEY3_PORT   GPIOA
#define KEY3_PIN    GPIO_Pin_0

/* 每键电平极性 */
/* 1 = 低电平按下（空闲上拉为高，按下拉低）
 * 0 = 高电平按下（空闲下拉为低，按下拉高）
 * 依据原理图：按键另一端接地取 1，接 3.3V 取 0 */
#define KEY0_ACTIVE_LOW   1
#define KEY1_ACTIVE_LOW   1
#define KEY2_ACTIVE_LOW   1
#define KEY3_ACTIVE_LOW   0

/* 每键上下拉 */
/* 必须与上面极性配套，输入引脚不能悬空：
 * ACTIVE_LOW = 1 → 配 GPIO_PuPd_UP（1），空闲高、按下低；ACTIVE_LOW = 0 → 配 GPIO_PuPd_DOWN（2）
 * 取值：0 = GPIO_PuPd_NOPULL（浮空），1 = GPIO_PuPd_UP（上拉），2 = GPIO_PuPd_DOWN（下拉） */
#define KEY0_PULL         1
#define KEY1_PULL         1
#define KEY2_PULL         1
#define KEY3_PULL         2

/* 数量常量 */
/* 按键数量：合法 id 为 0 ~ KEY_COUNT-1；修改后须同步改 key.c 的四张表，项数保持一致 */
#define KEY_COUNT   4

/* KEY_Scan 的"无按键"返回值：0xFF 超出任何合法 id，便于区分 */
#define KEY_NONE    0xFF

/* 消抖与长按步进 */
/* KEY_Scan 的软件消抖延时（ms）：检测到新按下后延时再复测一次
 * 它是 KEY_Scan 的判定粒度，也是按键按下时的阻塞时间 */
#define KEY_DEBOUNCE_MS      10

/* KEY_LongPress 的累计步进（ms）：每轮延时与计时的粒度 */
#define KEY_HOLD_STEP_MS     10

/* 编译期自检 */
/* KEY_NONE 固定为 0xFF，所以按键 id 最多到 253（共 254 个） */
#if (KEY_COUNT < 1) || (KEY_COUNT > 254)
/* #error 文本必须是 ASCII：AC5 对中文报错信息解析不稳 */
#error "KEY_COUNT must be 1..254 (KEY_NONE occupies 0xFF)"
#endif  /* KEY_COUNT 范围自检 */


/* 区块 2：基础功能 */
/* 初始化：按键配置为输入 + 每键各自的 KEYx_PULL 上下拉，并复位内部边沿记录
 * 复位边沿记录是为了避免初始化后误报一次按下事件
 * 标准库：经 gpio_core → RCC_AHB1PeriphClockCmd + GPIO_Init（输入 + 上下拉） */
void    KEY_Init(void);

/* 即时读取：1 = 此刻按下，0 = 松开（无消抖）
 * 越界保护：id 超范围返回 0
 * 标准库：经 gpio_core → GPIO_ReadInputDataBit（读 IDR） */
uint8_t KEY_Read(uint8_t id);

/* 查询某按键的按下极性：1 = 低电平按下，0 = 高电平按下
 * 本板 KEY_UP 与其余三键极性相反，上层不要写死假设
 * 越界保护：id 超范围返回 0；标准库：无，纯读编译期配置表 */
uint8_t KEY_ActiveLow(uint8_t id);

/* 事件扫描：返回本次新按下的按键 id；无事件返回 KEY_NONE
 * 含 KEY_DEBOUNCE_MS（默认 10ms）消抖，边沿触发：只在松开到按下的瞬间上报一次，长按不连发
 * 阻塞式，检测到按键时占用约 KEY_DEBOUNCE_MS 的 CPU 时间；多键同按时只上报先扫描到的一个
 * 标准库：经 gpio_core → GPIO_ReadInputDataBit（读 IDR）+ 粗延时消抖 */
uint8_t KEY_Scan(void);


/* 区块 3：扩展功能 */
/* 位掩码读取所有按键的即时状态：bit i = 1 表示按键 i 当前按下，0 表示松开
 * 即时读取无消抖；覆盖最多 32 个按键，KEY_COUNT ≤ 32 时全覆盖
 * 标准库：逐键复用 KEY_Read 路径（GPIO_ReadInputDataBit） */
uint32_t KEY_ReadAll(void);

/* 位带直读由 sys_bitband.h 提供宏（如 PAin(0) 直读电平） */

/* 阻塞等待任意按键按下，内部循环调用 KEY_Scan()，等待期间 CPU 空转
 * 返回：被按下的按键 id（KEY_NONE 不会出现） */
uint8_t KEY_WaitPress(void);

/* 长按检测：判断已按下的指定按键能否保持满 hold_ms
 * 返回：1 = 持续按住达到 hold_ms，0 = 中途松开或 id 越界
 * 前提：调用时该键处于按下状态，通常由 KEY_Scan 事件触发后调用
 * 说明：阻塞式，最长占用 hold_ms；以 KEY_HOLD_STEP_MS 为步进，粒度 ±KEY_HOLD_STEP_MS */
uint8_t KEY_LongPress(uint8_t id, uint32_t hold_ms);

/* 按键中断组合（基于 sys_exti）：中断只置标志，主循环取事件
 * 本组函数由硬件中断触发、即时置标志、非阻塞取走，Sleep 模式下按键仍可唤醒系统
 * KEY_Scan 系为主循环轮询，含 KEY_DEBOUNCE_MS 消抖，命中时阻塞一下 */

/* 开启全部按键的外部中断，触发沿按每键 KEYx_ACTIVE_LOW 自动适配；返回成功绑定的按键数（正常 = KEY_COUNT）
 * 内部完成 GPIO 打底 + SYSCFG 映射 + EXTI + NVIC；中断里只置标志，业务在主循环取
 * 每键占一条 EXTI 线（线号 = 引脚号），同一条线不要再被 SYS_EXTI_InitLine 绑定，否则互相覆盖
 * 无消抖，抖动或连按会产生多次事件；事件回调预置 8 键，KEY_COUNT ≤ 8 无需改 key.c；标准库：经 sys_exti → SYSCFG_EXTILineConfig + EXTI_Init + NVIC_Init */
uint8_t KEY_EXTI_Enable(void);

/* 快速判断有没有待处理事件：非阻塞、不取走，判断后仍需用 GetEvent 取走
 * 返回：1 = 至少一个按键事件未被取走，0 = 无 */
uint8_t KEY_EXTI_HasEvent(void);

/* 查询自上次查询以来被中断触发的按键（非阻塞）
 * 返回：按键 id，没有事件返回 KEY_NONE */
uint8_t KEY_EXTI_GetEvent(void);

/* 关闭按键外部中断（注销回调并清空标志） */
void    KEY_EXTI_Disable(void);

#endif /* __FWLIB_KEY_H */

