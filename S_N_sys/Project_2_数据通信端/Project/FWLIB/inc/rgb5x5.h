#ifndef __FWLIB_RGB5X5_H
#define __FWLIB_RGB5X5_H

#include "stm32f4xx.h"

/* ================================================================
 *  rgb5x5.h —— 【板载】5x5 全彩 LED 阵列（WS2812B x25）  头文件
 * ================================================================
 *  设计定位 : 一块 5x5 的 WS2812B 全彩屏（25 颗串联）驱动
 *             —— 单线协议，用 GPIO 位翻转 + DWT 精确计时（不需要定时器/DMA）
 *             —— 自带帧缓冲：先 SetPixel 想好整屏，再 Show() 一次发出去
 *  标准库关键词 : 只用 GPIO（经 gpio_core）；计时用 DWT->CYCCNT
 *                 —— 本模块**不占用任何定时器 / DMA / 中断**
 *
 *  【板上这套东西长什么样（原理图逐脚核对）】
 *      模块本体 : 一块标着 `WS2812B-RGBLED5*5` 的 25 颗灯板
 *      4 脚接口（丝印 `RGB`）:
 *          1 = GND
 *          2 = DIN   ← 网络 `RGB_DATA`
 *          3 = VCC   ← 网络 `EXVCC5`（外部 5V，注意不是 VCC5！）
 *          4 = DOUT  ← 留给级联下一块
 *      CN5（3 脚，就在灯板旁边）: 1 = `RGB_DATA`  2 = `VCC5`  3 = `EXVCC5`
 *
 *  ★★★ **上电前必须做两件接线，否则怎么都点不亮**（这是本板最大的坑）
 *      ① **CN5 的 2↔3 用跳线帽短接** —— 把板上 5V 送到 `EXVCC5` 给灯板供电
 *         （不短接 = 灯板一点电都没有，写什么代码都不亮）
 *      ② **CN5 的 1 脚（`RGB_DATA`）飞一根杜邦线到 P9 扩展排针上你选定的 IO**
 *         —— 原理图上 `RGB_DATA` **根本没有连到 MCU**（全图只在彩灯块里出现），
 *            所以必须自己接，然后把下面 `RGB5X5_DATA_PORT/PIN` 改成那根线的脚
 *
 *  ⚠ **3.3V 驱动 5V 供电的 WS2812B，电平是"勉强够"**
 *      WS2812B 手册的输入高电平门限是 `0.7 x VDD`，5V 供电时 = **3.5V**，
 *      而 MCU 只能输出 3.3V —— **严格来说不够**。
 *      实测多数批次能正常识别（门限没那么死），但如果你遇到"偶尔闪一下就死"
 *      "颜色错乱"，就是这个问题，解决办法二选一：
 *        · 把灯板供电降到 **4.3V 左右**（串一个普通硅二极管，1N4148/1N4007 都行）；
 *        · 或在数据线上加一级电平转换（74HCT245 / 一颗小 NPN 反相两级）。
 *
 *  【WS2812B 协议（面试点，就三句话）】
 *      · 单线串行：一颗 LED 收 24 bit 后，把**剩下的数据原样转发**给下一颗 ——
 *        所以 25 颗灯只需要**一根**信号线，不需要片选，也不需要时钟线。
 *      · 24 bit 的**顺序是 GRB，不是 RGB**（先绿后红再蓝！写反了颜色就乱）
 *      · 靠"高电平持续多久"区分 0 和 1：
 *            0 码：高 0.35us + 低 0.90us
 *            1 码：高 0.70us + 低 0.55us
 *            每 bit 总长固定 1.25us（800kHz），
 *            连续静默 > 50us 表示一帧结束（复位）。
 *
 *  ⇒ 因为对时序有要求（±150ns），本模块**发送期间会关中断**：
 *      25 颗 x 24bit = 600 bit x 1.25us ≈ **0.75ms**，
 *      加上复位也只有 1ms 左右，对绝大多数应用无感；
 *      但**如果你在用 FreeRTOS 且任务节拍要求很严**，
 *      可以把整个 Show() 包在一段临界区里，或改成 PWM+DMA 方案（见 .c 末尾说明）。
 *
 *  使用方式 :
 *      RGB5X5_Init();                       // ① 初始化（含接错线的自检提示）
 *      RGB5X5_SetPixel(0, 255, 0, 0);       // ② 第 0 颗灯设成纯红
 *      RGB5X5_SetPixelXY(2, 2, 0, 255, 0);  //    坐标版（x=列 0~4, y=行 0~4）
 *      RGB5X5_SetBrightness(20);            // ③ 亮度 20%（保护眼睛和电源）
 *      RGB5X5_Show();                       // ④ ★ 一次性刷到硬件
 *
 *  移植指引 : 换数据脚改 `RGB5X5_DATA_PORT/PIN`（CN5 那根线接哪就改哪）；
 *             换灯板形状改 `RGB5X5_WIDTH/HEIGHT` 与 `LED_COUNT`（记得同步）。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 0 = 不编译本模块 */
#ifndef RGB5X5_ENABLE
#define RGB5X5_ENABLE       1
#endif

/* -------------------- 数据引脚（★按你飞线接的脚改） -------------------- */
/* 默认给 PC5（本板完全空闲的一脚，也在 P9 扩展排针上）
 * 本板其它可用空脚：PA1 / PA7 / PB12 / PB13 / PC1~PC4 / PC12 / PD3 / PG11 / PG13 / PG14
 * ⚠ PC1~PC4 默认被 uln2003 用掉了，别撞车 */
#define RGB5X5_DATA_PORT    GPIOC
#define RGB5X5_DATA_PIN     GPIO_Pin_5

/* -------------------- 阵列尺寸 -------------------- */
#define RGB5X5_WIDTH        5
#define RGB5X5_HEIGHT       5
#define RGB5X5_LED_COUNT    25      /* 必须 = WIDTH x HEIGHT，见 .c 编译期护栏 */

/* 数据顺序：WS2812B 是 GRB。换别的型号（如 SK6812-RGBW）才需要改 .c */
#define RGB5X5_COLOR_ORDER_GRB  1

/* -------------------- 时序（单位：HCLK 周期，@168MHz 时 1 周期 = 5.95ns） -------------------- */
/* 理论值（WS2812B 手册）:
 *      0 码: 高 0.35us / 低 0.90us      1 码: 高 0.70us / 低 0.55us
 *      @168MHz 换算: 59 / 151 周期      118 / 92 周期
 * ⚠ 由于位翻转本身要花几条指令，下面给了"补偿值"。**这是本模块唯一需要
 *   现场微调的地方**：如果颜色乱、亮度不对、只有第一颗亮，通常是这里差太多。
 *   调试方法：先用默认值；不行就把 4 个值整体 ±20% 再试（或看 .c 里的
 *   RGB5X5_TIMING_NOTE 说明）。 */
#define RGB5X5_CYC_T0H      59U
#define RGB5X5_CYC_T0L      151U
#define RGB5X5_CYC_T1H      118U
#define RGB5X5_CYC_T1L      92U

/* 位翻转与循环本身的固定开销（周期数），从上面 4 个值里扣掉
 * 想更精确：把下面 4 个值整体加/减这个数来试 */
#define RGB5X5_CYC_OVERHEAD 10U

/* 一帧结束的低电平复位时间（微秒）
 * WS2812B 手册要求 > 50us；老批次够，新批次（v4/v5）建议 280us 以上 */
#define RGB5X5_RESET_US     300U

/* -------------------- 亮度 -------------------- */
/* 0 ~ 100（%）。上电默认用这个值——**别一上来就 100**：
 * 25 颗全白 = 25 x 60mA ≈ 1.5A，板上 5V 供不动，会掉电重启！ */
#define RGB5X5_DEFAULT_BRIGHT    20U

/* 编译期自检：数量必须是 WIDTH x HEIGHT */
#if ((RGB5X5_LED_COUNT) != ((RGB5X5_WIDTH) * (RGB5X5_HEIGHT)))
#error "RGB5X5_LED_COUNT must equal WIDTH * HEIGHT"
#endif


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：配数据脚（推挽输出 GPIO_OType_PP）+ 清空帧缓冲 + 亮度设为默认值 +
 *         给灯板发一次"全灭"（把上电残留的花屏清掉）
 * 标准库 : GPIO_ClockEnable + GPIO_OutInit（经 gpio_core）
 * 示例 : RGB5X5_Init(); */
void RGB5X5_Init(void);

/* 设置某颗灯的颜色（**只改帧缓冲，不立即发**；要发请调 RGB5X5_Show）
 * 参数 : idx —— 第几颗（0 ~ LED_COUNT-1，按灯板串联顺序）
 *        r/g/b —— 0~255
 * 说明 : 亮度系数在这里即时生效（存进去的就是缩放后的值）
 * 示例 : RGB5X5_SetPixel(0, 255, 0, 0);   // 第 0 颗纯红 */
void RGB5X5_SetPixel(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);

/* 按坐标设置（x = 列 0~WIDTH-1，y = 行 0~HEIGHT-1）
 * 说明 : 灯板内部是"蛇形"还是"逐行直排"取决于你手里那块模块，
 *        本模块默认按**逐行左→右、从上到下**编号（idx = y*WIDTH + x）；
 *        若不是，请改 .c 里的 rgb5x5_xy_to_index()
 * 示例 : RGB5X5_SetPixelXY(0, 0, 255, 255, 255);   // 左上角白 */
void RGB5X5_SetPixelXY(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b);

/* 整屏填充同一颜色（只改缓冲）
 * 示例 : RGB5X5_Fill(255, 255, 255);   // 全白（记得先降亮度！） */
void RGB5X5_Fill(uint8_t r, uint8_t g, uint8_t b);

/* 整屏全灭（只改缓冲） */
void RGB5X5_Clear(void);

/* 设置亮度（0~100%），对**之后**的 SetPixel 生效
 * 说明 : 这是"软件整体缩放"，不是调硬件电流 —— 好处是不用给每颗灯重算；
 *        想在保持颜色比例的前提下压暗，就用它
 * 示例 : RGB5X5_SetBrightness(10);   // 压到 10%，整屏最大电流约 150mA */
void RGB5X5_SetBrightness(uint8_t percent);
uint8_t RGB5X5_GetBrightness(void);

/* ★ 刷新：把帧缓冲一次性发给灯板（发送期间会关中断，约 1ms）
 * 说明 : 必须调用它才会真的亮；连续改多个像素后只调一次即可
 * 示例 : RGB5X5_SetPixel(...); RGB5X5_SetPixel(...); RGB5X5_Show(); */
void RGB5X5_Show(void);

/* 取帧缓冲里某颗灯的原始值（未乘亮度的 0~255）
 * 示例 : uint8_t r = RGB5X5_GetPixelR(3); */
uint8_t RGB5X5_GetPixelR(uint8_t idx);
uint8_t RGB5X5_GetPixelG(uint8_t idx);
uint8_t RGB5X5_GetPixelB(uint8_t idx);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* HSV → RGB（色相/饱和度/明度，0~255 各一路）
 * 用途 : 做彩虹渐变、呼吸灯、色轮动画的通用底层
 * 示例 : uint8_t r, g, b;
 *        RGB5X5_HsvToRgb(200, 255, 255, &r, &g, &b); */
void RGB5X5_HsvToRgb(uint8_t h, uint8_t s, uint8_t v,
                     uint8_t *r, uint8_t *g, uint8_t *b);

/* 直接在某个坐标上按 HSV 设置（不用自己算 RGB 端口）
 * 示例 : RGB5X5_SetPixelHSV(2, 2, 128, 255, 200); */
void RGB5X5_SetPixelHSV(uint8_t x, uint8_t y, uint8_t h, uint8_t s, uint8_t v);

/* 彩虹效果：把 25 颗灯按位置 + phase 偏移铺成一条彩虹（phase 每次 +1 就流动）
 * 说明 : 典型用法是在 sys_tick 回调里每 30~50ms 调一次并 Show
 * 示例 : RGB5X5_Rainbow(phase++); */
void RGB5X5_Rainbow(uint8_t phase);

/* 只点亮一个坐标（其余全灭）—— 做"贪吃蛇 / 扫描线"最省事
 * 示例 : RGB5X5_Dot(2, 3, 255, 255, 0); */
void RGB5X5_Dot(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b);

/* 关闭（发一次全灭并停住）：比"Fill(0,0,0)"多了一步，让灯板彻底安静
 * 说明 : WS2812B 本身**没有硬件开关**，只能靠发 0 让它不发光；
 *        不发光时电流仍有约 0.6mA/颗（静态），想彻底断电只能硬件断 5V */
void RGB5X5_Off(void);

/* 读回最近一次 Show 用了多少微秒（自检时序用）
 * 示例 : RGB5X5_Show(); printf("show = %lu us\r\n", RGB5X5_LastShowUs()); */
uint32_t RGB5X5_LastShowUs(void);

#endif /* __FWLIB_RGB5X5_H */
