#ifndef __FWLIB_XPT2046_H
#define __FWLIB_XPT2046_H

#include "stm32f4xx.h"

/* XPT2046 电阻触摸屏驱动。依赖 gpio_core.h（引脚/电平/纳秒延时）；
 * 可选依赖 sys_exti.h（T_PEN 中断，见 XPT2046_USE_EXTI）、
 * at24c02.h（校准参数掉电存储，见 XPT2046_USE_EEPROM）。
 * 触摸信号不在任何 SPI 硬件引脚上，用纯 GPIO 软件时序读写。
 *
 * 接线（普中-天马 F407 开发板，TFT 模块上的 XPT2046）：
 *   T_CS   = PC13   片选，低有效
 *   T_CLK  = PB0    时钟（模块标 DCLK）
 *   T_DIN  = PF11   数据输入（MCU → 触摸，模块标 T_MOSI）
 *   T_DOUT = PB2    数据输出（触摸 → MCU，模块标 T_MISO）
 *   T_PEN  = PB1    笔中断，低有效（模块标 /PENIRQ）
 *   T_CLK/T_DIN/T_DOUT 分属 PB/PF，不是任何硬件 SPI 的 SCK/MOSI/MISO 组合，只能位敲。
 *   PB2 同时是 BOOT1，复位时被采样，上电后可正常当 GPIO 用。
 *   触摸的 X/Y 原始值 0~4095，与屏幕像素不是一一对应，必须校准。
 *
 * 校准：用 XPT2046_ReadRaw 在屏幕四角各记一组 (raw_x, raw_y)，分别取 X、Y 的最小
 * 最大值，再调用 XPT2046_Calibrate(x_min, x_max, y_min, y_max, swap, inv_x, inv_y)，
 * 存入 24C02 后下次开机由 XPT2046_CalibLoad 取回。上下或左右颠倒时改 swap/inv 参数。
 * 换引脚只改 XPT2046_*_PORT/PIN 宏；换屏尺寸改 XPT2046_SCREEN_W/H。 */


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

/* 屏幕分辨率，必须与 lcd.h 的 LCD_WIDTH / LCD_HEIGHT 一致，否则换算出的
 * 坐标与实际像素不符。本板 3.2 寸 = 240×320 */
#define XPT2046_SCREEN_W    240
#define XPT2046_SCREEN_H    320

/* 软件 SPI 半周期延时，单位 ns。DCLK 上限 2MHz，半周期最短 250ns；
 * 取 500ns 约 1MHz */
#define XPT2046_CLK_DELAY_NS    500U

/* 读一次坐标的重采样次数与去极值个数：先剔最大最小各 trim 个再平均，抗噪。
 * n 取 3、trim 取 0 更快；n 取 7、trim 取 1 更稳 */
#define XPT2046_SAMPLE_N        5U
#define XPT2046_SAMPLE_TRIM     1U

/* 功能开关，置 0 可省 Flash 且不引入对应依赖 */
#define XPT2046_USE_EEPROM      1U      /* 1 = 提供 CalibSave / CalibLoad（用 AT24C02） */
#define XPT2046_USE_EXTI        1U      /* 1 = 提供 ExtiInit（T_PEN=PB1 → EXTI 线 1） */

/* 校准数据在 EEPROM 里的位置，占 10 字节：0x80~0x89 */
#define XPT2046_CALIB_MAGIC     0x5AU

/* 通道号，ReadChannel 参数 */
#define XPT2046_CH_X        0U
#define XPT2046_CH_Y        1U
#define XPT2046_CH_Z1       2U
#define XPT2046_CH_Z2       3U
#define XPT2046_CH_VBAT     4U
#define XPT2046_CH_AUX      5U
#define XPT2046_CH_TEMP0    6U
#define XPT2046_CH_TEMP1    7U

/* 触摸事件，GetEvent 返回值 */
#define XPT2046_EVENT_NONE      0U      /* 无触摸 */
#define XPT2046_EVENT_DOWN      1U      /* 按下（按下后每次调用都返回它） */
#define XPT2046_EVENT_UP        2U      /* 抬起（只在松手那一次返回） */

/* 编译期自检：去极值个数不能占满采样次数 */
#if (XPT2046_SAMPLE_TRIM * 2U) >= XPT2046_SAMPLE_N
#error "XPT2046_SAMPLE_TRIM is too large for XPT2046_SAMPLE_N"
#endif


/* 初始化：CS/CLK/DIN 配为输出，DOUT/PEN 配为输入上拉
 * 返回 : 0 = 成功；1 = 失败 */
uint8_t XPT2046_Init(void);

/* 笔是否按下（读 T_PEN 引脚，只用于判断要不要开始读坐标）
 * 返回 : 1 = 按下；0 = 松开 */
uint8_t XPT2046_IsPenDown(void);

/* 读指定通道的原始 ADC 值
 * 参数 : ch 取 XPT2046_CH_X / _Y / _Z1 / _Z2 / _VBAT / _AUX / _TEMP0 / _TEMP1
 * 返回 : 12 位原始值 0~4095；通道非法返回 0
 * 说明 : 最底层一步，无滤波和校准
 */
uint16_t XPT2046_ReadChannel(uint8_t ch);

/* 读 X/Y 原始值，一次采一组，无滤波
 * 返回 : 0 = 成功；1 = 笔没按下（PEN 无效，读出的是无效值） */
uint8_t XPT2046_ReadRaw(uint16_t *raw_x, uint16_t *raw_y);

/* 读 X/Y 原始值，采 n 组，去掉最大最小各 trim 个再平均
 * 参数 : n / trim 传 0 则用 XPT2046_SAMPLE_N / XPT2046_SAMPLE_TRIM
 * 返回 : 0 = 成功；1 = 笔没按下 */
uint8_t XPT2046_ReadRawFiltered(uint16_t *raw_x, uint16_t *raw_y, uint8_t n, uint8_t trim);

/* 读屏幕坐标，原始值经校准换算，坐标范围 0~XPT2046_SCREEN_W-1
 * 返回 : 1 = 有有效触摸（坐标写入 x、y）；0 = 无触摸
 * 说明 : 内部含按下确认与多次采样去极值 */
uint8_t XPT2046_Read(uint16_t *x, uint16_t *y);

/* 触摸事件，带边沿判别：DOWN 时每次调用都返回，UP 只在松手那一次返回
 * 参数 : x/y 有坐标时写入；EVENT_UP 时写的是最后有效坐标
 * 返回 : XPT2046_EVENT_NONE / _DOWN / _UP */
uint8_t XPT2046_GetEvent(uint16_t *x, uint16_t *y);

/* 阻塞等待笔按下 / 抬起，超时返回 0，单位 ms */
uint8_t XPT2046_PenWaitDown(uint32_t timeout_ms);
uint8_t XPT2046_PenWaitUp  (uint32_t timeout_ms);


/* 校准参数表，存起来就是这 10 个字节 */
typedef struct {
    uint16_t x_min;     /* 屏幕最左对应的原始 X */
    uint16_t x_max;     /* 屏幕最右对应的原始 X */
    uint16_t y_min;     /* 屏幕最上对应的原始 Y */
    uint16_t y_max;     /* 屏幕最下对应的原始 Y */
    uint8_t  swap_xy;   /* 1 = 交换 X/Y，屏装反 90° 时用 */
    uint8_t  invert_x;  /* 1 = X 左右镜像 */
    uint8_t  invert_y;  /* 1 = Y 上下镜像 */
} XptCalib_t;

/* 直接给一组范围，范围用 ReadRaw 采点量出 */
void XPT2046_Calibrate(uint16_t x_min, uint16_t x_max,
                       uint16_t y_min, uint16_t y_max,
                       uint8_t swap_xy, uint8_t invert_x, uint8_t invert_y);

/* 整表设置 / 读取 */
void XPT2046_SetCalib(const XptCalib_t *c);
void XPT2046_GetCalib(XptCalib_t *c);

/* 本板典型值兜底，精度一般，正式使用需自行校准 */
void XPT2046_CalibDefault(void);

/* 是否已校准（CalibDefault / Calibrate / CalibLoad 都会置有效） */
uint8_t XPT2046_IsCalibrated(void);

/* 校准参数存/取 EEPROM（AT24C02，占 0x80~0x89）
 * 返回 : 0 = 成功（Load 时表示取到有效数据）；1 = 失败（无器件或没存过） */
uint8_t XPT2046_CalibSave(void);
uint8_t XPT2046_CalibLoad(void);

/* 用 T_PEN（PB1 → EXTI 线 1）做按下中断，平时不轮询
 * 参数 : callback 在笔按下（下降沿）时回调；传 0 表示只清标志
 * 返回 : 0 = 成功；1 = 失败
 * 注意 : 线 1 未被其它模块占用，按键占了线 0/2/3/4 */
uint8_t XPT2046_ExtiInit(void (*callback)(void));
void    XPT2046_ExtiDisable(void);

#endif /* __FWLIB_XPT2046_H */
