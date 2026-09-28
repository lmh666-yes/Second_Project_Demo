#ifndef __FWLIB_XPT2046_H
#define __FWLIB_XPT2046_H

#include "stm32f4xx.h"

/* ================================================================
 *  xpt2046.h —— 【彩屏附件】电阻触摸屏驱动（XPT2046）  头文件
 * ================================================================
 *  设计定位 : 器件级驱动（薄封装）—— 用"软件 SPI"读触摸坐标，
 *             对外只给"屏幕坐标 + 按下/抬起事件"，不暴露任何时序细节
 *  依赖     : gpio_core.h（引脚/电平/纳秒延时）
 *             sys_exti.h（可选：用 T_PEN 中断唤醒，见 XPT2046_USE_EXTI）
 *             at24c02.h（可选：校准参数掉电存储，见 XPT2046_USE_EEPROM）
 *  标准库关键词 : 无——**纯 GPIO 软件时序**（本板触摸信号不在任何 SPI 硬件引脚上）
 *
 *  【为什么用软件 SPI 而不是硬件 SPI】
 *      看接线：T_CLK=PB0、T_DIN=PF11、T_DOUT=PB2 —— 这三个分属 PB/PF，
 *      任何一个硬件 SPI 的 SCK/MOSI/MISO 都不是这一组，
 *      所以只能"位敲"（bit-bang）。本板触摸不追求速度，软件时序完全够用。
 *
 *  【接线（普中-天马 F407开发板，TFT 模块上的 XPT2046）】
 *      T_CS   = PC13     片选（低有效）
 *      T_CLK  = PB0      时钟（模块标 DCLK）
 *      T_DIN  = PF11     数据输入（MCU → 触摸，模块标 T_MOSI）
 *      T_DOUT = PB2      数据输出（触摸 → MCU，模块标 T_MISO）
 *      T_PEN  = PB1      笔中断（低有效；模块标 /PENIRQ）
 *      ⚠ PB2 同时也是 BOOT1 —— 复位时被采样，**上电后可以正常当 GPIO 用**
 *      ⚠ 触摸的 X/Y 原始值是 0~4095，与屏幕像素**不是**一一对应，
 *        必须校准（本模块提供校准 API + 掉电保存）
 *
 *  【使用方式（初始化 → 校准一次 → 读坐标）】
 *      LCD_Init();                                  // ① 先把屏点亮
 *      XPT2046_Init();                              // ② 起触摸
 *      if (XPT2046_CalibLoad() != 0) {              // ③ 有存过就用存的
 *          XPT2046_CalibDefault();                  //    没有先用典型值顶着
 *      }
 *      while (1) {
 *          uint16_t x, y;
 *          if (XPT2046_Read(&x, &y)) {              // ④ 1 = 有触摸
 *              LCD_DrawPoint(x, y, LCD_COLOR_RED);  //    画板效果
 *          }
 *      }
 *
 *  【校准怎么做（没校准一定画不准）】
 *      ① 用 XPT2046_ReadRaw 在屏幕四个角各记一组 (raw_x, raw_y)；
 *      ② 取 X 的最小/最大值、Y 的最小/最大值；
 *      ③ XPT2046_Calibrate(x_min, x_max, y_min, y_max, swap, inv_x, inv_y)
 *      ④ XPT2046_CalibSave() 存进 24C02，下次开机 XPT2046_CalibLoad() 取回。
 *      如果上下/左右颠倒，优先改 swap / inv 三个参数，而不是重新采点。
 *
 *  移植指引 : 换引脚只改 XPT2046_*_PORT/PIN 宏；换屏尺寸改 XPT2046_SCREEN_W/H。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
#define XPT2046_CS_PORT     GPIOC
#define XPT2046_CS_PIN      GPIO_Pin_13     /* T_CS   */

#define XPT2046_CLK_PORT    GPIOB
#define XPT2046_CLK_PIN     GPIO_Pin_0      /* T_CLK  */

#define XPT2046_DIN_PORT    GPIOF
#define XPT2046_DIN_PIN     GPIO_Pin_11     /* T_DIN  (MCU → 触摸) */

#define XPT2046_DOUT_PORT   GPIOB
#define XPT2046_DOUT_PIN    GPIO_Pin_2      /* T_DOUT (触摸 → MCU) */

#define XPT2046_PEN_PORT    GPIOB
#define XPT2046_PEN_PIN     GPIO_Pin_1      /* T_PEN  (低 = 按下) */
#define XPT2046_PEN_ACTIVE_LOW   1          /* 1 = 低电平表示按下（本板如此） */

/* 屏幕分辨率 —— **必须与 lcd.h 的 LCD_WIDTH / LCD_HEIGHT 一致**，
 * 否则换算出来的坐标会和实际像素差一截（本板 3.2 寸 = 240×320） */
#define XPT2046_SCREEN_W    240
#define XPT2046_SCREEN_H    320

/* 软件 SPI 半周期延时（纳秒）。
 * XPT2046 的 DCLK 上限 2MHz → 半周期最短 250ns；取 500ns 约 1MHz，稳。 */
#define XPT2046_CLK_DELAY_NS    500U

/* 读一次坐标时的"重采样次数"与"去极值个数"（先剔最大最小再平均，抗噪）
 * 想更快：n 取 3、trim 取 0；想更稳：n 取 7、trim 取 1 */
#define XPT2046_SAMPLE_N        5U
#define XPT2046_SAMPLE_TRIM     1U

/* 功能开关（不需要就置 0，可省 Flash、也不引入对应依赖） */
#define XPT2046_USE_EEPROM      1U      /* 1 = 提供 CalibSave / CalibLoad（用 AT24C02） */
#define XPT2046_USE_EXTI        1U      /* 1 = 提供 ExtiInit（T_PEN=PB1 → EXTI 线 1） */

/* 校准数据在 EEPROM 里的位置（占 10 字节：0x80~0x89） */
#define XPT2046_CALIB_MAGIC     0x5AU

/* 通道号（ReadChannel 用） */
#define XPT2046_CH_X        0U
#define XPT2046_CH_Y        1U
#define XPT2046_CH_Z1       2U
#define XPT2046_CH_Z2       3U
#define XPT2046_CH_VBAT     4U
#define XPT2046_CH_AUX      5U
#define XPT2046_CH_TEMP0    6U
#define XPT2046_CH_TEMP1    7U

/* 触摸事件（GetEvent 用） */
#define XPT2046_EVENT_NONE      0U      /* 无触摸 */
#define XPT2046_EVENT_DOWN      1U      /* 按下（按下后每次调用都返回它） */
#define XPT2046_EVENT_UP        2U      /* 抬起（只在松手那一次返回） */

/* 编译期自检：屏幕尺寸与采样参数不能自相矛盾 */
#if (XPT2046_SAMPLE_TRIM * 2U) >= XPT2046_SAMPLE_N
#error "XPT2046_SAMPLE_TRIM is too large for XPT2046_SAMPLE_N"
#endif


/* ================================================================
 *                    区块 2：基础功能（读原始值 / 读坐标）
 * ================================================================ */
/* 初始化：把 5 根线配好（CS/CLK/DIN 输出，DOUT/PEN 输入上拉）
 * 返回 : 0 = 成功；1 = 失败（实际上只有参数问题才会失败，保持风格统一） */
uint8_t XPT2046_Init(void);

/* 笔是否按下（读 T_PEN 引脚，快；只用于判断"要不要开始读坐标"）
 * 返回 : 1 = 按下；0 = 松开 */
uint8_t XPT2046_IsPenDown(void);

/* 读指定通道的原始 ADC 值（0 ~ 4095）
 * 参数 : ch —— XPT2046_CH_X / _Y / _Z1 / _Z2 / _VBAT / _AUX / _TEMP0 / _TEMP1
 * 返回 : 12 位原始值；通道非法返回 0
 * 说明 : 这是最底层的一步——**没有任何滤波和校准**
 * 示例 : uint16_t rx = XPT2046_ReadChannel(XPT2046_CH_X); */
uint16_t XPT2046_ReadChannel(uint8_t ch);

/* 读 X/Y 原始值（一次采一组，无滤波）
 * 返回 : 0 = 成功；1 = 笔没按下（PEN 无效，读出来是垃圾值） */
uint8_t XPT2046_ReadRaw(uint16_t *raw_x, uint16_t *raw_y);

/* 读 X/Y 原始值（采 n 组，去掉最大最小各 trim 个再平均——抗抖动）
 * 参数 : n / trim 传 0 则用 XPT2046_SAMPLE_N / XPT2046_SAMPLE_TRIM
 * 返回 : 0 = 成功；1 = 笔没按下 */
uint8_t XPT2046_ReadRawFiltered(uint16_t *raw_x, uint16_t *raw_y, uint8_t n, uint8_t trim);

/* ★ 读屏幕坐标（原始值 → 校准换算，坐标范围 0~XPT2046_SCREEN_W-1）
 * 返回 : 1 = 有有效触摸（坐标已写入 x 和 y 指向的变量）；0 = 无触摸
 * 说明 : 内部已做"按下确认 + 多次采样去极值"，是最常用的一个函数
 * 示例 : uint16_t x, y; if (XPT2046_Read(&x, &y)) { 画点(x, y); } */
uint8_t XPT2046_Read(uint16_t *x, uint16_t *y);

/* 触摸事件（带边沿判别，适合做"画板 / 点击按钮"）
 * 参数 : x/y —— 有坐标时写入（EVENT_UP 时写的是最后有效坐标）
 * 返回 : XPT2046_EVENT_NONE / _DOWN / _UP
 * 用法 : 用 switch 分三种情况：DOWN 时画点（拖动就连续画）；
 *        UP 时那一帧才判定"一次点击"（避免拖着不停触发）；
 *        NONE 时不做事。 */
uint8_t XPT2046_GetEvent(uint16_t *x, uint16_t *y);

/* 阻塞等待笔按下 / 抬起（简单场景用；超时返回 0）
 * 示例 : if (XPT2046_PenWaitDown(10000)) { 说明 10 秒内按下了 } */
uint8_t XPT2046_PenWaitDown(uint32_t timeout_ms);
uint8_t XPT2046_PenWaitUp  (uint32_t timeout_ms);


/* ================================================================
 *                    区块 3：校准与扩展
 * ================================================================ */
/* 校准参数表（存起来就是这 10 个字节） */
typedef struct {
    uint16_t x_min;     /* 屏幕"最左"对应的原始 X */
    uint16_t x_max;     /* 屏幕"最右"对应的原始 X */
    uint16_t y_min;     /* 屏幕"最上"对应的原始 Y */
    uint16_t y_max;     /* 屏幕"最下"对应的原始 Y */
    uint8_t  swap_xy;   /* 1 = 交换 X/Y（屏装反 90° 时用） */
    uint8_t  invert_x;  /* 1 = X 左右镜像 */
    uint8_t  invert_y;  /* 1 = Y 上下镜像 */
} XptCalib_t;

/* 直接给一组范围（先用 ReadRaw 采点量出来） */
void XPT2046_Calibrate(uint16_t x_min, uint16_t x_max,
                       uint16_t y_min, uint16_t y_max,
                       uint8_t swap_xy, uint8_t invert_x, uint8_t invert_y);

/* 整表设置 / 读取 */
void XPT2046_SetCalib(const XptCalib_t *c);
void XPT2046_GetCalib(XptCalib_t *c);

/* 本板典型值兜底（**精度一般，正式用必须自己校准**）
 * 说明 : 给你一堆"能看出方向对不对"的数值，方便先把流程跑通 */
void XPT2046_CalibDefault(void);

/* 是否已校准（CalibDefault / Calibrate / CalibLoad 都会置有效） */
uint8_t XPT2046_IsCalibrated(void);

/* 校准参数存/取 EEPROM（AT24C02，占 0x80~0x89）
 * 返回 : 0 = 成功（Load 时表示"取到了有效数据"）；1 = 失败（无器件/没存过）
 * 示例 : if (XPT2046_CalibLoad() != 0) XPT2046_CalibDefault(); */
uint8_t XPT2046_CalibSave(void);
uint8_t XPT2046_CalibLoad(void);

/* 用 T_PEN（PB1 → EXTI 线 1）做"按下中断"，适合低功耗：平时不轮询
 * 参数 : callback —— 笔按下（下降沿）时回调；传 0 表示只清标志
 * 返回 : 0 = 成功；1 = 失败
 * 注意 : 线 1 本库未被其它模块占用（按键占了线 0/2/3/4） */
uint8_t XPT2046_ExtiInit(void (*callback)(void));
void    XPT2046_ExtiDisable(void);

#endif /* __FWLIB_XPT2046_H */
