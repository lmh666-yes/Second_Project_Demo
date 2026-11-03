#ifndef __FWLIB_RGB5X5_H
#define __FWLIB_RGB5X5_H

#include "stm32f4xx.h"

/* rgb5x5.h : 板载 5x5 全彩 LED 阵列（WS2812B x25）头文件
 *
 * 单线协议，GPIO 位翻转 + DWT->CYCCNT 计时，不占用定时器、DMA、中断。
 * 自带帧缓冲：先 SetPixel 填整屏，再 Show 一次发出。
 *
 * 接线（原理图逐脚核对）:
 *   灯板 4 脚接口（丝印 RGB）: 1 = GND  2 = DIN（网络 RGB_DATA）
 *                              3 = VCC（网络 EXVCC5，外部 5V，不是 VCC5）  4 = DOUT（级联下一块）
 *   CN5（3 脚，灯板旁边）: 1 = RGB_DATA  2 = VCC5  3 = EXVCC5
 *   上电前两处接线:
 *     1) CN5 的 2-3 用跳线帽短接，把板上 5V 送到 EXVCC5 给灯板供电
 *     2) CN5 的 1 脚（RGB_DATA）飞线到 P9 扩展排针选定的 IO：原理图中 RGB_DATA 未连到 MCU，
 *        接好后把下方 RGB5X5_DATA_PORT/PIN 改成该脚
 *
 * 电平: WS2812B 手册输入高电平门限 0.7 x VDD，5V 供电时为 3.5V，MCU 输出 3.3V 低于该门限。
 *   多数批次能识别；出现闪一下就灭或颜色错乱时，可把灯板供电降到 4.3V 左右
 *   （串一个硅二极管，1N4148/1N4007），或在数据线上加一级电平转换（74HCT245）。
 *
 * 协议: 单线串行，一颗 LED 收 24 bit 后把剩余数据原样转发给下一颗，
 *   25 颗灯共用一根信号线，不需要片选和时钟线；24 bit 顺序为 GRB。
 *   0 码 高 0.35us + 低 0.90us；1 码 高 0.70us + 低 0.55us；
 *   每 bit 总长 1.25us（800kHz）；连续静默 > 50us 为一帧结束（复位）。
 *
 * 时序要求 ±150ns，发送期间关中断：25 x 24bit = 600 bit x 1.25us ≈ 0.75ms，加复位约 1ms。
 * FreeRTOS 下任务节拍要求严格时，可把 Show() 包在临界区里，或改用 PWM+DMA 方案。
 *
 * 移植: 换数据脚改 RGB5X5_DATA_PORT/PIN；换灯板形状改 RGB5X5_WIDTH/HEIGHT 与 LED_COUNT，
 *   数量宏需同步。
 */


/* 0 = 不编译本模块 */
#ifndef RGB5X5_ENABLE
#define RGB5X5_ENABLE       1
#endif

/* 数据引脚：按实际飞线所接的脚修改 */
/* 默认 PC5（本板空闲脚，在 P9 扩展排针上）
 * 其它可用空脚：PA1 / PA7 / PB12 / PB13 / PC1~PC4 / PC12 / PD3 / PG11 / PG13 / PG14
 * PC1~PC4 默认被 uln2003 占用，不能复用 */
#define RGB5X5_DATA_PORT    GPIOC
#define RGB5X5_DATA_PIN     GPIO_Pin_5

/* 阵列尺寸 */
#define RGB5X5_WIDTH        5
#define RGB5X5_HEIGHT       5
#define RGB5X5_LED_COUNT    25      /* 必须 = WIDTH x HEIGHT，见 .c 编译期护栏 */

/* 数据顺序：WS2812B 是 GRB。换别的型号（如 SK6812-RGBW）才需要改 .c */
#define RGB5X5_COLOR_ORDER_GRB  1

/* 时序，单位 HCLK 周期；@168MHz 时 1 周期 = 5.95ns */
/* 理论值（WS2812B 手册）:
 *   0 码: 高 0.35us / 低 0.90us      1 码: 高 0.70us / 低 0.55us
 *   @168MHz 换算: 59 / 151 周期      118 / 92 周期
 * 位翻转和循环本身有指令开销，需要时把 4 个值整体调整
 * 出现颜色错乱、亮度不对或只有第一颗亮时，把 4 个值整体 ±20% 再试
 * （.c 中 RGB5X5_TIMING_NOTE）。 */
#define RGB5X5_CYC_T0H      59U
#define RGB5X5_CYC_T0L      151U
#define RGB5X5_CYC_T1H      118U
#define RGB5X5_CYC_T1L      92U

/* 位翻转与循环本身的固定开销（周期数），调这 4 个时序值时用于估算补偿量 */
#define RGB5X5_CYC_OVERHEAD 10U

/* 一帧结束的低电平复位时间（us）
 * WS2812B 手册要求 > 50us；新批次（v4/v5）建议 280us 以上 */
#define RGB5X5_RESET_US     300U

/* 默认亮度，取值 0~100（%）
 * 25 颗全白 = 25 x 60mA ≈ 1.5A，板上 5V 供不起会掉电重启，故默认 20 */
#define RGB5X5_DEFAULT_BRIGHT    20U

/* 编译期自检：数量必须是 WIDTH x HEIGHT */
#if ((RGB5X5_LED_COUNT) != ((RGB5X5_WIDTH) * (RGB5X5_HEIGHT)))
#error "RGB5X5_LED_COUNT must equal WIDTH * HEIGHT"
#endif


/* 初始化：数据脚配为推挽输出（GPIO_OType_PP），清空帧缓冲，亮度设为默认值，
 * 并向灯板发一次全灭，清掉上电残留显示
 * 底层 : GPIO_ClockEnable + GPIO_OutInit（经 gpio_core） */
void RGB5X5_Init(void);

/* 设置某颗灯的颜色，只改帧缓冲，需调 RGB5X5_Show 才发出
 * 参数 : idx = 灯序号（0 ~ LED_COUNT-1，按灯板串联顺序）
 *        r/g/b = 0~255
 * 说明 : 亮度系数在此处即时生效，存入的是缩放后的值 */
void RGB5X5_SetPixel(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);

/* 按坐标设置颜色：x = 列 0~WIDTH-1，y = 行 0~HEIGHT-1
 * 编号默认逐行左到右、从上到下（idx = y*WIDTH + x）；
 * 灯板内部若为蛇形走线，改 .c 里的 rgb5x5_xy_to_index() */
void RGB5X5_SetPixelXY(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b);

/* 整屏填充同一颜色，只改缓冲 */
void RGB5X5_Fill(uint8_t r, uint8_t g, uint8_t b);

/* 整屏全灭（只改缓冲） */
void RGB5X5_Clear(void);

/* 设置亮度，取值 0~100（%），只对之后的 SetPixel 生效
 * 属软件整体缩放，不改硬件电流；想按比例压暗又保持颜色关系时使用 */
void RGB5X5_SetBrightness(uint8_t percent);
uint8_t RGB5X5_GetBrightness(void);

/* 刷新：把帧缓冲一次性发给灯板，发送期间关中断，耗时约 1ms
 * 改完多个像素后调用一次即可 */
void RGB5X5_Show(void);

/* 取帧缓冲中某颗灯的原始值（未乘亮度的 0~255） */
uint8_t RGB5X5_GetPixelR(uint8_t idx);
uint8_t RGB5X5_GetPixelG(uint8_t idx);
uint8_t RGB5X5_GetPixelB(uint8_t idx);


/* HSV 转 RGB：色相、饱和度、明度取值均为 0~255，结果写入 r/g/b */
void RGB5X5_HsvToRgb(uint8_t h, uint8_t s, uint8_t v,
                     uint8_t *r, uint8_t *g, uint8_t *b);

/* 在指定坐标上按 HSV 设置颜色：h/s/v 取值均为 0~255 */
void RGB5X5_SetPixelHSV(uint8_t x, uint8_t y, uint8_t h, uint8_t s, uint8_t v);

/* 彩虹效果：按灯的位置加 phase 偏移取色，phase 每帧 +1 产生流动
 * 通常在 sys_tick 回调里每 30~50ms 调用一次并 Show */
void RGB5X5_Rainbow(uint8_t phase);

/* 只点亮一个坐标，其余全灭 */
void RGB5X5_Dot(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b);

/* 关闭：向灯板发一次全灭
 * WS2812B 无硬件开关，只能靠发 0 停止发光；不发光时静态电流仍有约 0.6mA/颗，
 * 要彻底断电需断开 5V */
void RGB5X5_Off(void);

/* 返回最近一次 Show 的耗时（us），自检时序用 */
uint32_t RGB5X5_LastShowUs(void);

#endif /* __FWLIB_RGB5X5_H */
