#ifndef __FWLIB_KEY_H
#define __FWLIB_KEY_H

#include "stm32f4xx.h"

/* ================================================================
 *  key.h —— 【板载】按键模块  头文件
 * ================================================================
 *  设计定位 : 板载按键抽象层（薄封装：读状态 + 扫事件）
 *  依赖     : gpio_core.h（GPIO 输入工具）；按键中断组另基于 sys_exti.h
 *  标准库关键词 : GPIO_ReadInputDataBit / GPIO_Init / RCC_AHB1PeriphClockCmd
 *                 （中断组:SYSCFG_EXTILineConfig / EXTI_Init / NVIC_Init,经 sys_exti）
 *  电平极性 : 每键独立（KEYx_ACTIVE_LOW），函数内部自动适配
 *             —— 本板 KEY_UP(PA0) 高有效，其余三键低有效
 *
 *  两个读取接口的分工（重要）:
 *      KEY_Read(id) —— "现在按着吗？" 即时状态，无消抖；
 *      KEY_Scan()   —— "刚刚按下了哪个？" 事件上报，含 10ms 消抖。
 *
 *  使用方式 :
 *      KEY_Init();                  // ① 初始化
 *      if (KEY_Read(0)) { ... }     // ② 读即时状态（按住持续生效）
 *      uint8_t k = KEY_Scan();      // ③ 扫单击事件（按下瞬间报一次）
 *      if (k == 0) { ... }
 *
 *  移植指引 :
 *      换板子只改"区块 1"的四件套（端口/引脚/极性/上下拉）。
 *      ⚠ 本库的极性是"每键独立"的——因为同一块板上可能同时存在
 *        "一键接 3.3V（高有效）+ 其余键接地（低有效）"的接法。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* -------------------- 按键引脚 -------------------- */
/* 每个按键四个宏一套（端口 / 引脚 / 极性 / 上下拉），x 即对外暴露的 id
 *
 * 【天马 F407 开发板 · 对照《普中-天马 F407开发板原理图》】
 *   板上 4 个独立按键（丝印 KEY0 / KEY1 / KEY2 / KEY_UP）：
 *     id 0 → KEY0  (PE4) —— 另一端接 GND ，按下为低 → 上拉
 *     id 1 → KEY1  (PE3) —— 另一端接 GND ，按下为低 → 上拉
 *     id 2 → KEY2  (PE2) —— 另一端接 GND ，按下为低 → 上拉
 *     id 3 → KEY_UP(PA0) —— 另一端接 3.3V，按下为高 → 下拉
 *   ⚠ 本板 KEY_UP 与其余三键极性相反（KEY_UP 是为待机唤醒预留的）。
 *     原参考工程只提供一对全局宏 KEY_ACTIVE_LOW / KEY_PULL，
 *     无法表达"一键高有效 + 三键低有效"，故本库升级为每键独立配置。
 *   ⚠ 引脚可被复用：PA0 同时是 WKUP 唤醒脚（见 sys_pwr），PE2/PE3/PE4
 *     在别的板上可能是 FSMC/TRACE 脚，本板空闲。 */
#define KEY0_PORT   GPIOE
#define KEY0_PIN    GPIO_Pin_4
#define KEY1_PORT   GPIOE
#define KEY1_PIN    GPIO_Pin_3
#define KEY2_PORT   GPIOE
#define KEY2_PIN    GPIO_Pin_2
#define KEY3_PORT   GPIOA
#define KEY3_PIN    GPIO_Pin_0

/* -------------------- 每键电平极性 -------------------- */
/* 1 = 低电平按下（空闲被上拉为高，按下被拉到低）
 * 0 = 高电平按下（空闲被下拉为低，按下被拉到高）
 * 判断方法：看原理图——按键另一端接地 → 1；接 3.3V → 0 */
#define KEY0_ACTIVE_LOW   1
#define KEY1_ACTIVE_LOW   1
#define KEY2_ACTIVE_LOW   1
#define KEY3_ACTIVE_LOW   0

/* -------------------- 每键上下拉 -------------------- */
/* 必须与上面极性配套（输入引脚要给确定电平，不能悬空）：
 *   低电平按下（ACTIVE_LOW = 1）→ 配 GPIO_PuPd_UP（1）：空闲高、按下低
 *   高电平按下（ACTIVE_LOW = 0）→ 配 GPIO_PuPd_DOWN（2）：空闲低、按下高
 * 取值：0 = GPIO_PuPd_NOPULL（浮空）, 1 = GPIO_PuPd_UP（上拉）, 2 = GPIO_PuPd_DOWN（下拉） */
#define KEY0_PULL         1
#define KEY1_PULL         1
#define KEY2_PULL         1
#define KEY3_PULL         2

/* -------------------- 数量常量 -------------------- */
/* 按键数量：合法 id 为 0 ~ KEY_COUNT-1
 * ⚠ 修改后必须同步修改 key.c 的四张表（项数要保持一致） */
#define KEY_COUNT   4

/* KEY_Scan 的"无按键"返回值：0xFF 超出任何合法 id，便于区分 */
#define KEY_NONE    0xFF

/* -------------------- 编译期自检 -------------------- */
/* KEY_NONE 固定为 0xFF，所以按键 id 最多到 253（共 254 个） */
#if (KEY_COUNT < 1) || (KEY_COUNT > 254)
/* 注意 : #error 文本必须是 ASCII（AC5 对中文报错信息解析不稳） */
#error "KEY_COUNT must be 1..254 (KEY_NONE occupies 0xFF)"
#endif  /* KEY_COUNT 范围自检 */


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：按键配置为输入 + 每键各自的 KEYx_PULL 上下拉；
 * 同时复位内部边沿记录（避免初始化后误报一次"按下事件"）
 * 标准库 : 经 gpio_core → RCC_AHB1PeriphClockCmd + GPIO_Init（输入+上下拉）*/
void    KEY_Init(void);

/* 即时读取：1 = 此刻按下，0 = 松开（无消抖）
 * 越界保护：id 超范围返回 0
 * 标准库 : 经 gpio_core → GPIO_ReadInputDataBit（读 IDR）
 * 示例 : if (KEY_Read(0)) { ... }      // KEY0 正按着(按住持续生效) */
uint8_t KEY_Read(uint8_t id);

/* 查询某按键的按下极性（1 = 低电平按下；0 = 高电平按下）
 * 用途 : 本板 KEY_UP 与其余三键极性相反，上层写"翻转逻辑"或
 *        做提示音/指示灯联动时可先问一句，避免写死假设
 * 越界保护：id 超范围返回 0
 * 标准库 : 无——纯读编译期配置表 */
uint8_t KEY_ActiveLow(uint8_t id);

/* 事件扫描：返回"本次新按下"的按键 id；无事件返回 KEY_NONE
 * 行为特征（重要）：
 *   ① 含 10ms 软件消抖，抖动不会误报；
 *   ② 边沿触发——只在"松开→按下"瞬间上报一次，长按不连发；
 *   ③ 阻塞式——检测到按键时占用约 10ms CPU 时间；
 *   ④ 多键同按时只上报先扫描到的一个，状态不会错乱
 * 标准库 : GPIO_ReadInputDataBit（读 IDR,经 gpio_core）+ 粗延时消抖
 * 示例 : uint8_t k = KEY_Scan();
 *        if (k == 0) { ... }   // KEY0 刚按下——只在按下瞬间报一次
 * 扩展提示 : 与长按组合 —— 先 KEY_Scan 拿 id、再 KEY_LongPress(id, ms)
 *            （先例:KEY_LongPress 注释里的典型用法）*/
uint8_t KEY_Scan(void);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 位掩码读取所有按键的即时状态（一次调用拿回全部）
 *   bit i = 1 → 按键 i 当前按下；bit i = 0 → 松开
 * 说明 : 即时读取、无消抖；适合"多键状态"判断（如组合键）
 * 覆盖范围：最多 32 个按键（KEY_COUNT ≤ 32 时全覆盖）
 * 标准库 : 逐键复用 KEY_Read 路径（GPIO_ReadInputDataBit）
 * 示例 : uint32_t m = KEY_ReadAll();   // m 的 bit0~bit3 对应 KEY0~KEY3
 *        if (m == 0x05U) { ... }      // KEY0 与 KEY2 同时按下 */
uint32_t KEY_ReadAll(void);

/* ----------------------------------------------------------------
 * 位带直读版本（与区块 2 的 KEY_Read 结果相同，实现不同）
 * ----------------------------------------------------------------
 * 库函数版：查"端口 + 掩码"表 → GPIO_InRead 读 IDR 后做位与；
 * 位带版  ：查"别名地址"表 → 一条 LDR 直读该位（0/1），
 *           地址在编译期算好、无函数调用、无位运算。
 * 返回、越界保护、极性适配均与 KEY_Read 一致；同样无消抖——
 * 要求消抖仍请用 KEY_Scan。原理见 gpio_core.h "位带"小节；
 * 换板子时自动跟随区块 1 宏，无需改动。
 * 标准库 : 无——一条 LDR 直读 IDR 位（别名地址编译期算好）
 * 示例 : if (KEY_BB_Read(0)) { ... }   // 读 KEY0(与 KEY_Read(0) 结果相同) */
uint8_t KEY_BB_Read(uint8_t id);

/* 阻塞等待任意按键按下（"按任意键继续"场景）
 * 返回 : 被按下的按键 id（KEY_NONE 不会出现）
 * 说明 : 内部循环调用 KEY_Scan()，等待期间 CPU 空转
 * 示例 : uint8_t k = KEY_WaitPress();   // 卡在这里直到某个键被按下 */
uint8_t KEY_WaitPress(void);

/* 长按检测：判断"已按下"的指定按键能否保持满 hold_ms
 *  返回 : 1 = 持续按住达到 hold_ms；0 = 中途松开或 id 越界
 *  前提 : 调用时该键处于按下状态（通常由 KEY_Scan 事件触发后调用）
 *  说明 : 阻塞式，最长占用 hold_ms；检测以 10ms 为步进（粒度 ±10ms）
 *  典型用法 : 
 *      uint8_t k = KEY_Scan();                 // 按下瞬间
 *      if (k == 0 && KEY_LongPress(0, 1000)) { // 确认长按（≥1s）
 *          ... 长按功能 ...
 *      } */
uint8_t KEY_LongPress(uint8_t id, uint32_t hold_ms);

/* ----------------------------------------------------------------
 * 按键中断组合（基于 sys_exti；中断只"置标志"，主循环取事件）
 * ----------------------------------------------------------------
 * 与轮询方式的分工 : 
 *   KEY_Scan 系   —— 主循环轮询，含 10ms 消抖，命中时阻塞一下；
 *   本组函数      —— 硬件中断触发、即时置标志、非阻塞取走；
 *                   且 Sleep 模式下按键仍可把系统唤醒。
 * 选择建议：要零延迟响应 / 要低功耗唤醒 → 本组；
 *           要消抖/长按等按键语义      → KEY_Scan 系。 */

/* 开启全部按键的外部中断（触发沿按每键 KEYx_ACTIVE_LOW 自动适配）
 * 返回 : 成功绑定的按键数（正常 = KEY_COUNT）
 * 说明 : ① 内部自动完成 GPIO 打底 + SYSCFG 映射 + EXTI + NVIC；
 *      ② 中断里只置标志，业务在主循环取——典型:先 KEY_EXTI_HasEvent
 *         判断有无、再 KEY_EXTI_GetEvent 取走;
 *      ③ 每键占一条 EXTI 线（线号 = 引脚号）——同一线不要再被
 *         SYS_EXTI_InitLine 绑定，否则互相覆盖；
 *      ④ 无消抖：抖动/连按会产生多次事件，要求严格时上层滤波;
 *      ⑤ 事件回调预置 8 键：KEY_COUNT ≤ 8 无需改 key.c 事件段
 * 标准库 : 经 sys_exti → SYSCFG_EXTILineConfig + EXTI_Init + NVIC_Init
 *          （完整调用链见 sys_exti.h 中 SYS_EXTI_InitLine 的注释）
 * 示例 : KEY_EXTI_Enable();                       // 4 个键全部开中断
 *        uint8_t k = KEY_EXTI_GetEvent();         // 主循环取事件(0xFF = 无) */
uint8_t KEY_EXTI_Enable(void);

/* 快速判断"有没有待处理事件"（非阻塞、不取走：判断后仍需 GetEvent 取走）
 * 返回 : 1 = 至少一个按键事件未被取走；0 = 无
 * 用途 : 主循环两层写法（与教材 key_event_flag 判别先行的思路一致）:
 *        if (KEY_EXTI_HasEvent()) { ... KEY_EXTI_GetEvent() ... }
 * 示例 : if (KEY_EXTI_HasEvent()) { LED_Toggle(0); } */
uint8_t KEY_EXTI_HasEvent(void);

/* 查询"自上次查询以来被中断触发的按键"（非阻塞）
 * 返回 : 按键 id；没有事件返回 KEY_NONE
 * 示例 : uint8_t k = KEY_EXTI_GetEvent();
 *        if (k != KEY_NONE) { ... }   // 有键被按(中断方式) */
uint8_t KEY_EXTI_GetEvent(void);

/* 关闭按键外部中断（注销回调并清空标志） */
void    KEY_EXTI_Disable(void);

#endif /* __FWLIB_KEY_H */

