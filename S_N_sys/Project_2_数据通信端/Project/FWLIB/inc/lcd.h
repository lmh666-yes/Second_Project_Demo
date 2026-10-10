#ifndef __FWLIB_LCD_H
#define __FWLIB_LCD_H

#include "stm32f4xx.h"

/* TFT-LCD 显示屏模块（FSMC + ILI9481）
 * 总线：FSMC Bank1 NE4，16 位，模式 A，SRAM
 * 控制器：ILI9481，3.5 寸，320x480
 * 本版功能：初始化、清屏、画点、画线、填充
 * FSMC 段寄存器直写：标准外设库不含 FSMC 外设驱动
 *
 * 本板接线（普中-天马 F407 开发板原理图；换板子改区块 1）：
 *   数据线：FSMC_D0 ~ FSMC_D15，F4 系列固定引脚映射（见 lcd.c 表）
 *   控制线：CS = PG12 (FSMC_NE4)   RS = PF12 (FSMC_A6)
 *           WR = PD5 (FSMC_NWE)    RD = PD4 (FSMC_NOE)
 *           BL = PB15（普通 GPIO，背光开关）
 *   RESET：34 脚接口的 RESET 接 MCU 的 NRST，与复位按键同网，模块不翻转复位引脚
 *   RS：板原理图 = FSMC_A6（MCU 第 50 脚 PF12）；34 脚接口旁丝印 A10 是普中
 *       LCD 模块侧命名；本板 FSMC_A10 (PG0) 已给 SRAM 的 A17，RS 只能用 A6
 *
 * 本板 LCD 是插座，不插屏时不调用 LCD_Init 即可，其余功能不受影响；
 * 基础版只写不读，无等屏应答循环
 *
 * 触摸屏 XPT2046 驱动已单独成模块（xpt2046.h / xpt2046.c）
 *   T_CS = PC13   T_CLK = PB0   T_DIN = PF11
 *   T_DOUT = PB2  T_PEN = PB1
 *   与 lcd 无编译依赖，但屏幕尺寸宏必须两边一致 */


/* 区块 1：定义与宏定义区（换板子只改这里） */
/* -------------------- 屏幕分辨率 -------------------- */
/* 面板原生竖屏 320x480，固定不变；横竖屏切换由 LCD_SetRotation()
 * 在运行时对调（内部另有 lcd_w/lcd_h） */
#define LCD_WIDTH               320
#define LCD_HEIGHT              480

/* -------------------- 背光控制 -------------------- */
#define LCD_BL_PORT             GPIOB
#define LCD_BL_PIN              GPIO_Pin_15
#define LCD_BL_ACTIVE_HIGH      1    /* 背光极性：1 = 高电平点亮，0 = 低电平点亮 */

/* -------------------- FSMC 命令 / 数据地址 -------------------- */
/* NOR/SRAM 模式下命令与数据靠 RS 接的地址线区分：
 * RS=A6 时两个地址相差 1<<(6+1) = 0x80（16 位总线按字节寻址）
 * 基址 Bank1 NE4 = 0x6C000000（见 STM32F4 参考手册存储器映射） */
#define LCD_CMD_ADDR            0x6C000000UL
#define LCD_DATA_ADDR           0x6C000080UL

/* -------------------- FSMC 时序参数 -------------------- */
/* 单位：HCLK 周期数（当前 168MHz，1 周期 ≈ 5.95ns）
 * ILI9481 的 8080-16bit 写周期 tRCW 典型需 ~100ns
 *   实际写周期 = (ADDR_SETUP + ADDR_HOLD + DATA_SETUP + 1) 个 HCLK
 *   6 + 1 + 9 + 1 = 17 周期 ≈ 101ns，默认值取这一组
 * 时序以 HCLK 周期计：切换主频（如 sys_clock 切到 HSI 16MHz）后
 * 实际时间成比例变化，屏幕异常时先按当前主频重新评估 */
#define LCD_FSMC_ADDR_SETUP     6    /* 地址建立时间 */
#define LCD_FSMC_ADDR_HOLD      1    /* 地址保持时间 */
#define LCD_FSMC_DATA_SETUP     9    /* 数据建立时间 */

/* -------------------- 显示方向（MADCTL） -------------------- */
/* 写 ILI9481 的 0x36 寄存器，决定横竖屏与颜色顺序（RGB/BGR）
 *   0x48 = 竖屏(320x480) + BGR，默认值
 *   其它可选组合：0x88 / 0xE8 / 0x28 / 0xC8 / 0x68
 * 颜色红蓝互换：翻转 BGR 位(0x08)
 * 方向/镜像不对：调 MX(0x80) / MY(0x40) / MV(0x20) 位 */
#define LCD_MADCTL              0x48

/* -------------------- 颜色 -------------------- */
/* RGB565 取色宏；r/g/b 取值范围 0~255 */
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


/* 区块 2：基础功能 */
/* 初始化：FSMC 总线 + 控制引脚 + ILI9481 上电序列，末步清屏为黑
 *
 * 内部依次调用：
 *   1) RCC_AHB1PeriphClockCmd + RCC_AHB3PeriphClockCmd  开 GPIO 端口 / FSMC 时钟
 *   2) GPIO_PinAFConfig + GPIO_Init        数据/控制引脚复用为 FSMC
 *   3) FSMC 寄存器直写                     BCR/BTR 配置片选时序（16 位 SRAM 模式 A）
 *   4) 命令序列                            ILI9481 上电初始化（0xD0/0xC0/0xC8/…）
 *   5) GPIO_OutInit + GPIO_OutSet         背光引脚（经 gpio_core）
 *   无复位步骤：屏 RESET 接 MCU 的 NRST，MCU 复位时屏已一并复位
 *
 * 只需调用一次；内部含 120ms 上电等待（阻塞） */
void LCD_Init(void);

/* 背光开 / 关；极性由 LCD_BL_ACTIVE_HIGH 自动适配
 * 参数 on 非 0 = 开，0 = 关
 * 内部使用 GPIO_SetBits / GPIO_ResetBits（经 gpio_core 的 GPIO_OutXxx） */
void LCD_BackLight(uint8_t on);

/* 全屏填充指定颜色，即清屏
 * 参数 color 为 RGB565 颜色值，用 LCD_COLOR_xxx 宏或 LCD_RGB565(r, g, b) 取色
 * 往 FSMC 数据地址直写像素，时序由硬件处理；以下 Draw / Fill 系列同，
 * 细节见 .c 的 LCD_WR_* 宏 */
void LCD_Clear(uint16_t color);

/* 设置写入窗口（后续画点/填充的矩形区域，含两端点）
 * 说明 : 坐标越界自动截断到屏幕范围内；
 *        x0>x1 / y0>y1 自动交换 */
void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/* 画一个像素点；越界坐标自动忽略 */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);

/* 填充矩形区域，含两端点；内部设窗口后连续写像素 */
void LCD_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/* 画直线，Bresenham 算法；起点终点任意方向 */
void LCD_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);


/* 区块 3：扩展功能 */
/* 显示方向切换、读点、几何图元、字符/字符串/数字显示
 *
 * 未提供的能力：
 *   图片显示（需要取模数据，依赖具体工具链输出格式）
 *   触摸屏驱动已拆到独立模块：见 xpt2046.h（T_CLK=PB0 / T_CS=PC13 /
 *   T_PEN=PB1 / T_DIN=PF11 / T_DOUT=PB2） */

/* -------------------- 显示方向（横竖屏） -------------------- */
/* 说明 : 改的是 ILI9481 的 MADCTL(0x36)；切到 90/270 时宽高自动对调，
 *         画图前应先取 LCD_GetWidth() / LCD_GetHeight()，
 *         不要直接用 LCD_WIDTH / LCD_HEIGHT 两个面板原生宏
 * 若方向或镜像不对，改 lcd.c 里各档的 BGR 位（本库默认全部开 BGR） */
typedef enum {
    LCD_ROT_0   = 0,        /* 竖屏（上电默认，MADCTL = LCD_MADCTL） */
    LCD_ROT_90  = 1,        /* 横屏（向右旋 90°） */
    LCD_ROT_180 = 2,        /* 竖屏（倒置） */
    LCD_ROT_270 = 3         /* 横屏（向左旋 90°） */
} LcdRot_t;

/* 切换显示方向：写 MADCTL + 对调宽高 + 清屏（内容会丢） */
void     LCD_SetRotation(LcdRot_t rot);

/* 当前方向下屏幕的可用宽 / 高（竖屏 320x480，横屏 480x320） */
uint16_t LCD_GetWidth (void);
uint16_t LCD_GetHeight(void);
/* 读取当前方向枚举值（0~3） */
uint8_t  LCD_GetRotation(void);

/* -------------------- 读点 -------------------- */
/* 读回某坐标的像素颜色（RGB565）；越界返回 0
 * 走 ILI9481 的 0x2E（Memory Read），FSMC 异步模式 A 本身支持读，
 * 无需额外配置读时序
 * 读一次含 1 次空读 + 1 次真读，比写点慢，不适合放进高频循环
 * ILI9481 读时序比写时序慢很多，读回值不对时先把
 * LCD_FSMC_DATA_SETUP 加大到 15 再试 */
uint16_t LCD_ReadPoint(uint16_t x, uint16_t y);

/* -------------------- 几何图元 -------------------- */
/* 空心矩形，四条边；坐标自动交换，越界自动裁剪，下同 */
void LCD_DrawRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/* 空心圆，Bresenham 八分对称，无浮点 */
void LCD_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

/* 实心圆，逐行换算弦宽，用填充矩形画 */
void LCD_FillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

/* -------------------- 字符 / 字符串 / 数字 -------------------- */
/* 显示一个 ASCII 字符（8x16 点阵，字库见 lcd_font.h）
 * 参数 : x,y  = 字符左上角坐标
 *        ch   = 字符（不在 0x20~0x7E 范围内按空格处理）
 *        fc   = 字色；bc = 背景色（叠加模式用）
 *        mode = 0 叠加，背景处不画，保留原有内容
 *                1 = 不叠加，用 bc 铺满 8x16 字格 */
void LCD_ShowChar  (uint16_t x, uint16_t y, char ch,
                    uint16_t fc, uint16_t bc, uint8_t mode);

/* 显示一个字符串，逐个字符向右推进，遇到 '\0' 结束；
 * 超出右边界回到 x 起点、y 下移一行 */
void LCD_ShowString(uint16_t x, uint16_t y, const char *str,
                    uint16_t fc, uint16_t bc, uint8_t mode);

/* 显示无符号整数，定宽补空格，便于刷新数值时覆盖旧内容
 * 参数 : num = 待显示值；len = 显示位数（0 或 >10 按 10 处理） */
void LCD_ShowNum   (uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                    uint16_t fc, uint16_t bc, uint8_t mode);

/* 显示定点小数：val 按 10^frac 缩放（如 12345 + frac=2 显示 "123.45"）
 * 不使用浮点，AC5 下避免浮点运算开销 */
void LCD_ShowFixed (uint16_t x, uint16_t y, int32_t val, uint8_t frac, uint8_t int_len,
                    uint16_t fc, uint16_t bc, uint8_t mode);

#endif /* __FWLIB_LCD_H */
