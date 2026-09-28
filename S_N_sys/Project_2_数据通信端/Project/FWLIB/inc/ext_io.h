#ifndef __FWLIB_EXT_IO_H
#define __FWLIB_EXT_IO_H

#include "stm32f4xx.h"

/* ================================================================
 *  ext_io.h —— 【板载/外接】单引脚数字输入模块封装库  头文件
 * ================================================================
 *  芯片平台 : STM32F407ZGT6（标准外设库 StdPeriph）
 *  设计定位 : "一个引脚一根线"的数字输入型模块抽象层
 *             —— 红外接收头、电容触摸按键、光敏数字量、WiFi 按键
 *             —— 需要协议/时序的器件（DS18B20 单总线、WS2812、OLED、
 *                MPU6050…）不放在本模块，请单独建驱动文件
 *  依赖     : gpio_core.h（底层 GPIO 工具）
 *  标准库关键词 : GPIO_ReadInputDataBit / GPIO_Init / RCC_AHB1PeriphClockCmd
 *
 *  两层初始化模型（本模块的设计要点）:
 *      第一层 EXT_IO_Init()  —— 通用打底：把"所有"外接引脚统一设为
 *                              "输入 + EXT_BASE_PULL 上下拉"，
 *                              保证引脚不悬空、不误触发；
 *      第二层 EXT_XXX_Init() —— 专属覆盖：只给需要特殊配置的模块
 *                              做个性化设置；默认态已满足时留空。
 *      调用顺序：先打底、后覆盖——顺序颠倒会把个性化配置改回默认。
 *
 *  使用方式 :
 *      EXT_IO_Init();              // ① 打底（必须）
 *      EXT_IR_Init();              // ② 按需覆盖（默认可省）
 *      if (EXT_IR_Detected(0)) { } // ③ 读检测结果
 *
 *  移植指引 :
 *      本头文件管"策略"（数量、极性、打底上下拉）；
 *      引脚表在 ext_io.c，改端口/引脚请改 .c（见其注释）。
 *
 *  【天马 F407 开发板 · 资源对照（核对《普中-天马 F407开发板原理图》网络名）】
 *    本板没有"红外避障 / 循迹 / 碰撞 / 声音"那种多路外接模块
 *    （原参考工程的 PC6~PC12 在本板是 DCMI / SDIO 信号），
 *    故整组替换为本板实际存在的四路单引脚输入（各 1 路）：
 *      红外接收头    → PA8  网络名 IRED     （低有效：有载波时为低）
 *      WiFi 模块按键 → PF6  网络名 W_KEY    （低有效，接在 WiFi 模块座上）
 *      板载光敏      → PF7  网络名 LIGHT    （**模拟量**！见下方提醒）
 *      电容触摸板    → PA5  网络名 STM_ADC  （需 J8 短接 P_TOUCH↔STM_ADC）
 *
 *  【后来又加了一组：外接红外避障模块（EXT_OBS）】
 *    注意区别，两者完全不是一回事：
 *      EXT_IR  = 板载**红外接收头**（IRED），解红外遥控器的，看"有没有载波"；
 *      EXT_OBS = **外接的红外避障模块**，有障碍物时输出一个电平（一般为低）。
 *   本板没有避障模块，需要自己买 4 个接在**排针**上（默认 PC10/PC11/PC12/PC8），
 *   引脚在 ext_io.c 的 ext_obs_list 表里改，数量改 EXT_OBS_COUNT。
 * ⚠ 避障模块上的电位器要**在实际路面上调**：黑色地面吸红外，
 *    在桌面上调好的阀值放到地上可能就永远不触发了。
 *
 *  ⚠⚠ 两个容易踩的坑（原先把这两个当成普通数字输入，是错的）：
 *    ① 光敏（PF7）：原理图上 PF7 = LIGHT，是"47K 上拉 + 光敏电阻到地"
 *       的**模拟分压点**（数字比较输出 LSENS 并未引到 MCU）。
 *       当数字输入读只能得到一个"亮/暗粗阀值"，要精确光照请用
 *       sys_adc（PF7 = ADC3_IN5），例：SYS_ADC_ReadAvg(...)。
 *    ② 电容触摸（PA5）：手摸上去不会把引脚拉低，
 *       **当数字输入永远读不到变化**！它是靠"放电 → 经 1M 上拉充电 → 量充电时间"
 *       来判定的（手指靠近 → 电容变大 → 充电变慢）。
 *       本模块已内置该测量（EXT_TOUCH_MODE_CHARGE），无需自己写。
 *       ⚠ 用之前必须用跳线帽把 J8 的 P_TOUCH 与 STM_ADC 短接
 *         （J8 是共享模拟输入排针，共 5 个脚：
 *            R_ADC(板载可调电位器) / STM_ADC(PA5) / P_TOUCH(触摸电极) /
 *            STM_DAC(PA4) / TAD1(NTC&PT100 检测输出)）。
 *         PA5 本身同时也是 ADC12_IN5 —— 两路功能不能同时用。
 *    ③ **PA8 / PA4 与摄像头接口 DCMI 共用**（原理图上这两根线同时挂着
 *       `DCMI_XCLK` / `DCMI_HREF` 的标注）：插上摄像头模块后，
 *       PA8 的红外接收与 PA4 的 DAC 输出就都不能用了，二选一。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* -------------------- 触发极性 -------------------- */
/* 1 = 低电平有效（检测到 → 引脚为低）；0 = 高电平有效
 * 判断方法：多数模块"有信号时输出低"，即 1；
 *          若读到的结果与实际相反，翻转对应开关即可。 */
#define EXT_IR_ACTIVE_LOW     1   /* 红外接收头 IRED：无载波输出高、有载波输出低 */
#define EXT_KEY_ACTIVE_LOW    1   /* WiFi 模块按键 W_KEY：按下接地 */
/* 外接红外避障模块：绝大多数型号"有障碍输出低"（低有效）；
 * 若读到结果与实际相反（无障碍也报“有障碍”），把它改成 0 */
#define EXT_OBS_ACTIVE_LOW    1

/* ⚠ 下面两个仅在"改用普通数字读"时才有意义：
 *   光敏（PF7=LIGHT）是模拟量，数字读只是粗阀值；
 *   触摸（PA5）当数字读无用（见文件头提醒）。
 * 保留宏是为了兼容"自己接了一个数字型光敏模块 / 数字触摸模块"的场合。 */
#define EXT_LIGHT_ACTIVE_LOW  1   /* 光敏：光越强分压越低 */
#define EXT_TOUCH_ACTIVE_LOW  1   /* 数字型触摸模块：触摸时拉低（本板不适用） */

/* -------------------- 电容触摸工作方式 -------------------- */
/* 1 = 电容充电时间法（本板 P_TOUCH + 1M 上拉，**必须用这个**）
 * 0 = 普通数字电平读（仅供外接"数字输出"触摸模块时使用） */
#define EXT_TOUCH_MODE_CHARGE  1

/* 判定阀值（µs）：实测充电时间超过基准 + 本值就算"摸到了"
 * 实测参考（3.3V / 1M / 无触摸）：约 8~12µs；手指按下约 20~40µs。
 * 如果不灵敏/误触发，先用串口把 EXT_TOUCH_ChargeTimeUs() 打出来看看 */
#define EXT_TOUCH_DELTA_US     15

/* 单次充电测量的超时上限（µs）——防止引脚被短路/损坏时死等 */
#define EXT_TOUCH_TIMEOUT_US   2000

/* -------------------- 打底上下拉 -------------------- */
/* 打底时给所有外接引脚设置的上下拉（决定"无信号时"的静止电平）:
 *   模块低电平有效（ACTIVE_LOW=1）→ 配 GPIO_PuPd_UP（1）：
 *     无信号时引脚被拉高 = "未检测到"，不会误触发；
 *   模块高电平有效（ACTIVE_LOW=0）→ 配 GPIO_PuPd_DOWN（2）
 * 取值：0 = GPIO_PuPd_NOPULL（浮空）, 1 = GPIO_PuPd_UP（上拉）, 2 = GPIO_PuPd_DOWN（下拉） */
#define EXT_BASE_PULL  1

/* -------------------- 数量常量 -------------------- */
/* 每类模块的检测路数：合法 id 为 0 ~ 数量-1
 * ⚠ 修改后必须同步修改 ext_io.c 中对应的引脚表（项数要一致） */
#define EXT_IR_COUNT    1
#define EXT_TOUCH_COUNT 1
#define EXT_LIGHT_COUNT 1
#define EXT_KEY_COUNT   1
/* 外接红外避障模块路数（4WD 小车：前左/前右/左/右）—— 引脚见 ext_io.c */
#define EXT_OBS_COUNT   4


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 第一层：通用打底——把四类模块引脚统一配置为"输入 + EXT_BASE_PULL"
 * 必须在第二层 EXT_XXX_Init() 之前调用
 * 标准库 : 经 gpio_core → RCC_AHB1PeriphClockCmd + GPIO_Init（输入+上下拉）
 * 示例 : EXT_IO_Init();            // main 开头打底一次
 * 扩展提示 : 加"新模块"三件套 —— ① 本头文件加 EXT_XXX_COUNT;
 *            ② ext_io.c 加引脚表(照红外那组抄);③ 加 EXT_XXX_Detected */
void    EXT_IO_Init(void);

/* ---- 红外接收头（id：0 ~ EXT_IR_COUNT-1） ---- */

/* 第二层：专属覆盖——默认态已满足，当前函数体留空；
 * 若该模块需要特殊配置，在 .c 内对应函数里追加（打底之后执行） */
void    EXT_IR_Init   (void);

/* 读取检测结果：1 = 检测到（触发），0 = 未检测到或 id 越界
 * 说明 : 触发极性自动适配（见 EXT_IR_ACTIVE_LOW）；
 *       即时读取、不含消抖；要解码红外遥控请配合 sys_exti + 定时器自行实现
 * 标准库 : 经 gpio_core → GPIO_ReadInputDataBit（读 IDR;以下各 Detected 同）
 * 示例 : if (EXT_IR_Detected(0)) { ... }    // 红外接收头正收到载波 */
uint8_t EXT_IR_Detected(uint8_t id);

/* ---- 电容触摸按键（id：0 ~ EXT_TOUCH_COUNT-1） ---- */
void    EXT_TOUCH_Init   (void);
/* 示例 : if (EXT_TOUCH_Detected(0)) { ... }   // 触摸按键被摸到 */
uint8_t EXT_TOUCH_Detected(uint8_t id);

#if (EXT_TOUCH_MODE_CHARGE)
/* 单次测量的充电时间（µs）——调阀值/排查灵敏度时打出来看
 * 示例 : printf("chg=%lu\r\n", (unsigned long)EXT_TOUCH_ChargeTimeUs(0)); */
uint32_t EXT_TOUCH_ChargeTimeUs(uint8_t id);

/* 重新校准：把当前状态当作"未触摸"基准（上电稳定后调一次）
 * 说明 : 不调也行——EXT_TOUCH_Detected 首帧会自动校准 */
void     EXT_TOUCH_Calibrate(uint8_t id);

/* 取当前基准值（µs） */
uint32_t EXT_TOUCH_GetBaseline(uint8_t id);
#endif

/* ---- 板载光敏（id：0 ~ EXT_LIGHT_COUNT-1） ----
 * ⚠ 与 sys_adc 的 SYS_ADC_LIGHT_* 是同一根引脚（PF7）：
 *   本模块按"数字阈值"用（亮/暗两态），sys_adc 按"模拟量"用（读具体值）。
 *   两者都会改引脚配置（一个配数字输入、一个配模拟输入），
 *   同一工程里请只用其中一种。 */
void    EXT_LIGHT_Init   (void);
/* 示例 : if (EXT_LIGHT_Detected(0)) { ... }   // 光照强度超过阈值 */
uint8_t EXT_LIGHT_Detected(uint8_t id);

/* ---- WiFi 模块按键（id：0 ~ EXT_KEY_COUNT-1） ---- */
void    EXT_KEY_Init     (void);
/* 示例 : if (EXT_KEY_Detected(0)) { ... }    // W_KEY 被按下 */
uint8_t EXT_KEY_Detected(uint8_t id);

/* ---- 外接红外避障模块（id：0 ~ EXT_OBS_COUNT-1） ----
 * 默认引脚（在 ext_io.c 的 ext_obs_list 里改）:
 *   id 0 → PC10（P2-24）  id 1 → PC11（P2-23）
 *   id 2 → PC12（P2-22）  id 3 → PC8 （P2-32）
 * 接线 : 每个模块 VCC→3.3V或5V（看模块规格）、GND→GND、OUT→对应引脚
 * 说明 : 输入已配成上拉（EXT_BASE_PULL），模块未接时读为“无障碍” ✓ */
void    EXT_OBS_Init     (void);
/* 示例 : if (EXT_OBS_Detected(0)) { ... }    // 0 号方向有障碍 */
uint8_t EXT_OBS_Detected(uint8_t id);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 多路计数：统计"检测到"的路数（0 ~ EXT_XXX_COUNT）
 * 典型用途 : 多路输入时判断"有几路同时有效"
 * 标准库 : 逐路复用各 Detected 路径（GPIO_ReadInputDataBit）
 * 示例 : uint8_t n = EXT_TOUCH_CountDetected();   // 有效路数(其余三类同) */
uint8_t EXT_IR_CountDetected   (void);   /* 例:n = EXT_IR_CountDetected() */
uint8_t EXT_TOUCH_CountDetected(void);   /* 例:n = EXT_TOUCH_CountDetected() */
uint8_t EXT_LIGHT_CountDetected(void);   /* 例:n = EXT_LIGHT_CountDetected() */
uint8_t EXT_KEY_CountDetected  (void);   /* 例:n = EXT_KEY_CountDetected() */
uint8_t EXT_OBS_CountDetected  (void);   /* 例:n = EXT_OBS_CountDetected() 避开逻辑用 */

/* 目前本模块只覆盖"读单引脚"型输入，以上为常用聚合接口。
 * 若以后加入需要协议/定时器的模块（超声波、蓝牙、单总线等），
 * 建议单独建 ext_xxx.c/.h，不放在本文件里。 */

#endif /* __FWLIB_EXT_IO_H */
