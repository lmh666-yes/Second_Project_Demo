#ifndef __FWLIB_LCD_H
#define __FWLIB_LCD_H

#include "stm32f4xx.h"

/*  lcd.h — TFT-LCD 显示屏（FSMC + ILI9341，240×320）
 *  总线 FSMC Bank1 NE4：16 位、模式 A、SRAM；数据线 FSMC_D0~D15 为 F4 固定映射：
 *  D0=PD14 D1=PD15 D2=PD0 D3=PD1 D4=PE7 D5=PE8 D6=PE9 D7=PE10
 *  D8=PE11 D9=PE12 D10=PE13 D11=PE14 D12=PD8 D13=PD9 D14=PD10 D15=PE15。
 *  控制线 CS=PG12(NE4) RS=PF12(A6) WR=PD5(NWE) RD=PD4(NOE) BL=PB15，
 *  数据线与控制线均复用 GPIO_AF_FSMC，背光为普通 GPIO 输出。
 *  接线依据 GEC-M4原理图2016-07-29，寄存器与存储器映射依据
 *  1-STM32F4xx中文参考手册1，命令序列取自 ILI9341 数据手册典型值。
 *  屏为插座式模块：不接屏时不调用 LCD_Init，其余接口不受影响；不亮时
 *  先查 BL，再查 CS / RS / WR / RD。只写不读，无等屏应答循环。 */

/* ILI9341 240×320；换屏（如 ILI9486 320×480、SSD1963 800×480）需同步改
 * 本处宽高与 lcd.c 内的初始化命令序列 */
#define LCD_WIDTH               240
#define LCD_HEIGHT              320

/* 背光经普通 GPIO 输出控制，非 FSMC 信号；LED 不亮先量 PB15 电平，
 * 再查 PB15 到屏背光脚的连线 */
#define LCD_BL_PORT             GPIOB
#define LCD_BL_PIN              GPIO_Pin_15
#define LCD_BL_ACTIVE_HIGH      1    /* 1 = 高电平点亮背光；0 = 低电平点亮 */

/* FSMC 片选与基址：Bank1 NE4 对应 0x6C000000，16 位总线按字节寻址；
 * NOR/SRAM 模式下命令与数据由 RS 所接地址线区分：RS=A6 时两地址相
 * 差 1<<(6+1) = 0x80，命令地址 +0x00、数据地址 +0x80 */
#define LCD_CMD_ADDR            0x6C000000UL
#define LCD_DATA_ADDR           0x6C000080UL

/* 写时序参数，单位 HCLK 周期，当前 HCLK = 168MHz；主频变更后实际时间成
 * 比例变化，需按新主频重算。读时序未配置（BTR 的 DATAST / ADDHLD 未置位），
 * 因此不支持读点、读 ID 等回读操作；BTR 各位域定义与周期数换算见
 * 中文参考手册 FSMC 章节。
 * 时序不足时现象为花屏 / 白屏 / 闪屏，可将三个值调大后重测。 */
#define LCD_FSMC_ADDR_SETUP     6    /* 地址建立时间 */
#define LCD_FSMC_ADDR_HOLD      1    /* 地址保持时间 */
#define LCD_FSMC_DATA_SETUP     6    /* 数据建立时间 */

/* ILI9341 命令 0x36（MADCTL）：0x48 = 竖屏 + BGR；0x08 置位翻转 BGR
 * （红蓝互换），MX(0x80)/MY(0x40)/MV(0x20) 控制方向与镜像。
 * 改显示方向时同步核对 LCD_WIDTH / LCD_HEIGHT 映射。 */
#define LCD_MADCTL              0x48

/* RGB565 取色宏，用于 LCD_Clear / Draw / Fill 系列的 color 参数：r/g/b 传 0~255 */
#define LCD_RGB565(r, g, b)  ((uint16_t)((((uint16_t)(r) & 0xF8U) << 8) | \
                                         (((uint16_t)(g) & 0xFCU) << 3) | \
                                         (((uint16_t)(b)) >> 3)))

#define LCD_COLOR_BLACK         0x0000
#define LCD_COLOR_WHITE         0xFFFF
#define LCD_COLOR_RED           0xF800
#define LCD_COLOR_GREEN         0x07E0
#define LCD_COLOR_BLUE          0x001F
#define LCD_COLOR_YELLOW        0xFFE0
#define LCD_COLOR_CYAN          0x07FF
#define LCD_COLOR_MAGENTA       0xF81F
#define LCD_COLOR_GRAY          0x8410
#define LCD_COLOR_ORANGE        0xFD20

/* 以上为 RGB565 字面量，可直接作为 color 参数传入：红 = 0xF800、
 * 绿 = 0x07E0、蓝 = 0x001F，灰 0x8410 为三通道各取半量程 */


/* 初始化 FSMC 总线、控制引脚与 ILI9341 上电序列，末步清屏为黑。
 * 只需调用一次；含 120ms 上电等待（阻塞）。
 * 加电顺序：GPIO 端口与 FSMC 时钟（RCC_AHB1/AHB3）→ 数据线与控制线
 * 复用 GPIO_PinAFConfig → FSMC 片选时序寄存器 BCR/BTR（16 位 SRAM 模式 A，
 * BTR 字段编码 = 实际周期数 - 1）→ ILI9341 命令序列（0xCF / 0xED 等，
 * 命令后延时，含 120ms 上电等待）→ 背光引脚。
 * 不接屏时不要调用本函数，其余接口不受影响。 */
void LCD_Init(void);

/* 背光开 / 关，极性由 LCD_BL_ACTIVE_HIGH 适配
 * on — 非 0 点亮，0 熄灭 */
void LCD_BackLight(uint8_t on);

/* 全屏填充指定颜色
 * color — RGB565，取 LCD_COLOR_xxx 或 LCD_RGB565(r, g, b) */
void LCD_Clear(uint16_t color);

/* 设置后续画点 / 填充的写入窗口（矩形区域），含两端点
 * x0..x1、y0..y1 — 各坐标 0~239 / 0~319；越界截断到屏幕范围，
 * x0>x1 或 y0>y1 自动交换 */
void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/* 画一个像素点
 * x、y — 0~239 / 0~319；越界坐标忽略，不改变当前写入窗口 */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);

/* 填充矩形区域，含两端点，内部单窗口连续写像素
 * x0..x1、y0..y1 — 需在屏幕范围内，两端点按从小到大给出 */
void LCD_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/* 画直线，Bresenham 算法，起点终点任意方向（含横线、竖线、斜线），含两端点
 * x0、y0 / x1、y1 — 各坐标 0~239 / 0~319，需在屏幕范围内 */
void LCD_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);


/* 未实现：字符 / 字符串（需 8×16 ASCII 点阵字库，数据放 .c）、图片显示、
 * 横竖屏切换（改 MADCTL 0x36 参数与宽高映射，两者需一致）、
 * 读点（需先按上述寄存器字段配置 FSMC 读时序）、
 * 触摸屏 XPT2046（T_SCK=PB0 / T_CS=PC13 / T_PEN=PB1 / T_MOSI=PF11，
 * 另需 SPI 与笔中断引脚初始化） */

#endif /* __FWLIB_LCD_H */
