#ifndef __FWLIB_LCD_H
#define __FWLIB_LCD_H

#include "stm32f4xx.h"

/* ================================================================
 *  lcd.h —— 【板载】TFT-LCD 显示屏模块（FSMC + ILI9481）  头文件
 * ================================================================
 *  设计定位 : 标准外设库之上的"寄存器级"薄封装
 *             —— 总线：FSMC Bank1 · NE4（16 位 · 模式 A · SRAM）
 *             —— 控制器：**ILI9481**（3.5 寸 320x480 彩屏）
 *             —— 本版为"基础版"：初始化 + 清屏 / 画点 / 画线 / 填充
 *  标准库关键词 : RCC_AHB1PeriphClockCmd / RCC_AHB3PeriphClockCmd /
 *                 GPIO_PinAFConfig / GPIO_Init;FSMC 段为寄存器直写
 *                 （标准外设库不含 FSMC 外设驱动,只能直接配寄存器）
 *
 *  ⚠ 区块 3（扩展功能）尚未实现：图片显示
 *    —— 需要时按同样结构补充（序列/字库等数据放 .c）。
 *
 *  本板接线（普中-天马 F407开发板原理图；换板子按区块 1 修改）:
 *      数据线 : FSMC_D0 ~ FSMC_D15 —— F4 系列固定引脚映射（见 lcd.c 表）
 *      控制线 : CS = PG12 (FSMC_NE4)   RS = PF12 (FSMC_A6)
 *               WR = PD5  (FSMC_NWE)   RD = PD4  (FSMC_NOE)
 *               BL = PB15 (普通 GPIO，背光开关)
 *      ⚠ **复位**：34 脚接口的 RESET 脚接的是 **MCU 的 NRST**（与复位按键同网），
 *        不是普通 GPIO —— 所以本模块**不去翻转任何复位引脚**，
 *        MCU 复位时屏也一起复位。这就是"不用写 GPIO_RST 也能点亮"的原因。
 *      ⚠ **RS 那根线，原理图和板丝印叫法不同，别被赫到**：
 *        板原理图 = `FSMC_A6`（MCU 第 50 脚 PF12）；
 *        34 脚接口旁的板丝印印的是 `A10`（那是普中 LCD **模块侧**的命名）。
 *        本板真正的 FSMC_A10(PG0) 已经给了 SRAM 的 A17，所以 RS 只能是 A6。
 *        若换了别的板子，改下方 LCD_CMD_ADDR / LCD_DATA_ADDR 即可。
 *
 *  插接说明（本板 LCD 是"插座"，不是焊死的屏）:
 *      板上只有 2 排母排针插座，屏模块插上去才有显示——固件层面
 *      与"板载屏"完全一样（照样走 FSMC + 下面这几根引脚）：
 *      ① 不插屏 / 不用屏 → 不要调用 LCD_Init，其余功能不受影响；
 *      ② 基础版全程"只写不读"，没插屏也不会卡死（无等屏应答的循环）；
 *      ③ 屏插上后不亮 → 先查背光(BL)，再查接线：CS/RS/WR/RD 四项。
 *
 *  触摸屏（电阻屏 XPT2046）是另一组独立信号，**驱动已单独成模块**：
 *      XPT2046 模块（xpt2046.h / xpt2046.c）
 *      T_CS = PC13   T_CLK = PB0   T_DIN = PF11
 *      T_DOUT = PB2  T_PEN = PB1
 *      （与 lcd 无编译依赖，但屏幕尺寸宏必须两边一致）
 *
 *  使用方式 :
 *      LCD_Init();                                // ① 初始化(末步清屏为黑)
 *      LCD_Clear(LCD_COLOR_BLUE);                 // ② 整屏填充
 *      LCD_DrawLine(0, 0, 319, 479, LCD_COLOR_WHITE);
 *      LCD_FillRect(20, 20, 100, 80, LCD_COLOR_RED);
 *
 *  移植指引 :
 *      换控制引脚 → 改"区块 1"的宏；数据线是 F4 固定映射，一般不动；
 *      换屏型号/分辨率 → 改尺寸宏 + lcd.c 内的初始化序列表。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* -------------------- 屏幕分辨率 -------------------- */
/* 本板彩屏是 **ILI9481 · 3.5 寸**，面板原生竖屏 **320x480**
 *   （板上丝印：`ILI9481`、`P1&320*480`）
 * 这里的两个值是"面板原生"尺寸，固定不变；横竖屏切换是在运行时
 * 用 LCD_SetRotation() 对调（内部另有 lcd_w/lcd_h）。 */
#define LCD_WIDTH               320
#define LCD_HEIGHT              480

/* -------------------- 背光控制 -------------------- */
#define LCD_BL_PORT             GPIOB
#define LCD_BL_PIN              GPIO_Pin_15
#define LCD_BL_ACTIVE_HIGH      1    /* 1 = 高电平点亮背光；0 = 低电平点亮 */

/* -------------------- FSMC 命令 / 数据地址 -------------------- */
/* 原理 : NOR/SRAM 模式下，"命令"与"数据"靠 RS 接的地址线区分——
 *       RS=A6 时两个地址相差 1<<(6+1) = 0x80（16 位总线按字节寻址）。
 * 基址：Bank1 · NE4 = 0x6C000000（见 STM32F4 参考手册存储器映射） */
#define LCD_CMD_ADDR            0x6C000000UL
#define LCD_DATA_ADDR           0x6C000080UL

/* -------------------- FSMC 时序参数 -------------------- */
/* 单位：HCLK 周期数（当前 168MHz，1 周期 ≈ 5.95ns）。
 * ILI9481 的 8080-16bit 写周期 tRCW 典型需 ~100ns：
 *   实际写周期 = (ADDR_SETUP + ADDR_HOLD + DATA_SETUP + 1) 个 HCLK
 *   6 + 1 + 9 + 1 = 17 周期 ≈ 101ns  ← 默认值就取这一组
 * 花屏/白屏/闪屏 → 把 DATA_SETUP 继续加大（9 → 15 → 20）；
 * 点亮正常后想提速 → 逐步减小（6 → 4），能稳定就是赚到。
 * ⚠ 时序以 HCLK 周期计：切换主频（如 sys_clock 切到 HSI 16MHz）后
 *    实际时间会成比例变化，屏幕异常时先按当前主频重新评估 */
#define LCD_FSMC_ADDR_SETUP     6    /* 地址建立时间 */
#define LCD_FSMC_ADDR_HOLD      1    /* 地址保持时间 */
#define LCD_FSMC_DATA_SETUP     9    /* 数据建立时间 */

/* -------------------- 显示方向（MADCTL） -------------------- */
/* 写 ILI9481 的 0x36 寄存器，决定横竖屏与颜色顺序（RGB/BGR）:
 *   0x48 = 竖屏(320x480) + BGR（默认，本模块实测配色正确）
 *   一个不行就试其它组合：0x88 / 0xE8 / 0x28 / 0xC8 / 0x68
 * 现象对照：颜色红蓝互换 → 翻转 BGR 位(0x08)；
 *           方向/镜像不对 → 调 MX(0x80)/MY(0x40)/MV(0x20) 位 */
#define LCD_MADCTL              0x48

/* -------------------- 颜色 -------------------- */
/* RGB565 取色宏：r/g/b 传 0~255 */
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


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：FSMC 总线 + 控制引脚 + ILI9481 上电序列（末步清屏为黑）
 *
 * 标准库调用链（库内部依次调用，可对照学习）:
 *   ① RCC_AHB1PeriphClockCmd + RCC_AHB3PeriphClockCmd  开 GPIO 端口 / FSMC 时钟
 *   ② GPIO_PinAFConfig + GPIO_Init        数据/控制引脚复用为 FSMC
 *   ③ FSMC 寄存器直写                     BCR/BTR 配置各片选时序（16 位 SRAM 模式 A）
 *   ④ 命令序列                            ILI9481 上电初始化（0xD0/0xC0/0xC8/…）
 *   ⑤ GPIO_OutInit + GPIO_OutSet         背光引脚（经 gpio_core）
 *   （无复位步骤：屏 RESET 接的是 MCU 的 NRST，MCU 复位时屏已一并复位）
 *
 * 说明 : 只需调用一次；内部含 120ms 上电等待（阻塞）
 * 示例 : LCD_Init();                              // 初始化并清屏为黑
 *        LCD_Clear(LCD_COLOR_BLUE);              // 整屏蓝色
 *        LCD_FillRect(20, 20, 100, 80, LCD_COLOR_RED); */
void LCD_Init(void);

/* 背光开 / 关（极性由 LCD_BL_ACTIVE_HIGH 自动适配）
 * 参数 : on —— 非 0 = 开（点亮背光），0 = 关
 * 标准库 : GPIO_SetBits / GPIO_ResetBits（经 gpio_core 的 GPIO_OutXxx）
 * 示例 : LCD_BackLight(1);   // 开背光(0 = 关) */
void LCD_BackLight(uint8_t on);

/* 全屏填充指定颜色（最常用的"清屏"）
 * 参数 : color —— 颜色值（RGB565）:用 LCD_COLOR_xxx 宏，
 *                 或 LCD_RGB565(r, g, b) 现场取色（宏区见上方）
 * 标准库 : 无——往 FSMC 数据地址直写像素（硬件时序自动处理,
 *          以下 Draw/Fill 系列同;细节见 .c 的 LCD_WR_* 宏）
 * 示例 : LCD_Clear(LCD_COLOR_BLUE);   // 清屏为蓝 */
void LCD_Clear(uint16_t color);

/* 设置写入窗口（后续画点/填充的矩形区域，含两端点）
 * 说明 : 坐标越界会自动截断到屏幕范围内；
 *        x0>x1 / y0>y1 会自动交换（容错）
 * 示例 : LCD_SetWindow(0, 0, 319, 479);   // 全屏窗口 */
void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/* 画一个像素点（自动忽略越界坐标）
 * 示例 : LCD_DrawPoint(160, 240, LCD_COLOR_RED);   // 屏幕中心点红色 */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);

/* 填充矩形区域（含两端点；内部一个窗口 + 连续写像素，速度快）
 * 示例 : LCD_FillRect(20, 20, 100, 80, LCD_COLOR_RED);   // 左上角红块 */
void LCD_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/* 画直线（Bresenham 算法，横/竖/斜线都支持；起点终点任意方向）
 * 示例 : LCD_DrawLine(0, 0, 319, 479, LCD_COLOR_WHITE);   // 画对角线 */
void LCD_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================
 * 本区块是"补全"部分：参考工程里标注为"待补充"的显示方向切换、
 * 读点、几何图元、字符/字符串/数字显示，这里按同样的表驱动风格补齐。
 *
 * ⚠ 仍**未**提供的能力（需要时按本库风格自行添加）:
 *      - 图片显示（需要取模数据，依赖具体工具链输出格式）
 *      - 触摸屏驱动已拆到独立模块：见 xpt2046.h（T_CLK=PB0 / T_CS=PC13 /
 *        T_PEN=PB1 / T_DIN=PF11 / T_DOUT=PB2）
 * ================================================================ */

/* -------------------- 显示方向（横竖屏） -------------------- */
/* 说明 : 改的是 ILI9481 的 MADCTL(0x36)；切到 90/270 时宽高自动对调，
 *        所以画图前应先问一句 LCD_GetWidth() / LCD_GetHeight()，
 *        不要死用 LCD_WIDTH / LCD_HEIGHT 两个"面板原生"宏。
 * 现象对照 : 方向/镜像不对 → 试另一档；颜色红蓝互换 → 调 lcd.c 里
 *            各档的 BGR 位（本库默认全部开 BGR） */
typedef enum {
    LCD_ROT_0   = 0,        /* 竖屏（上电默认，MADCTL = LCD_MADCTL） */
    LCD_ROT_90  = 1,        /* 横屏（向右旋 90°） */
    LCD_ROT_180 = 2,        /* 竖屏（倒置） */
    LCD_ROT_270 = 3         /* 横屏（向左旋 90°） */
} LcdRot_t;

/* 切换显示方向：写 MADCTL + 对调宽高 + 清屏（内容会丢，属正常）
 * 示例 : LCD_SetRotation(LCD_ROT_90);   // 切成横屏 */
void     LCD_SetRotation(LcdRot_t rot);

/* 当前方向下屏幕的可用宽 / 高（竖屏 320 x 480，横屏 480 x 320） */
uint16_t LCD_GetWidth (void);
uint16_t LCD_GetHeight(void);
/* 读取当前方向枚举值（0~3） */
uint8_t  LCD_GetRotation(void);

/* -------------------- 读点 -------------------- */
/* 读回某坐标的像素颜色（RGB565）
 * 说明 : 走 ILI9481 的 0x2E（Memory Read）——FSMC 异步模式 A 本身支持读，
 *        无需额外配置读时序；越界返回 0
 * 代价 : 读一次含 1 次空读 + 1 次真读，比写点慢得多（别放进高频循环）
 *        ⚠ ILI9481 的读时序比写时序慢很多，若读回值不对，
 *          先把 lcd.h 的 LCD_FSMC_DATA_SETUP 加大到 15 再试
 * 示例 : uint16_t c = LCD_ReadPoint(10, 10); */
uint16_t LCD_ReadPoint(uint16_t x, uint16_t y);

/* -------------------- 几何图元 -------------------- */
/* 空心矩形（四条边；坐标自动交换，越界自动裁剪，下同）
 * 示例 : LCD_DrawRect(20, 20, 100, 80, LCD_COLOR_WHITE); */
void LCD_DrawRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/* 空心圆（Bresenham 八分对称，无浮点）
 * 示例 : LCD_DrawCircle(160, 240, 60, LCD_COLOR_GREEN); */
void LCD_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

/* 实心圆（逐行换算弦宽，用填充矩形画） */
void LCD_FillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

/* -------------------- 字符 / 字符串 / 数字 -------------------- */
/* 显示一个 ASCII 字符（8x16 点阵，字库见 lcd_font.h）
 * 参数 : x,y  —— 字符左上角坐标
 *        ch   —— 字符（不在 0x20~0x7E 范围内按空格处理）
 *        fc   —— 字色；bc —— 背景色（叠加模式用）
 *        mode —— 0 = 叠加（背景处不画，保留原有内容）
 *                1 = 不叠加（用 bc 铺满 8x16 字格）
 * 示例 : LCD_ShowChar(10, 10, 'A', LCD_COLOR_WHITE, LCD_COLOR_BLACK, 1); */
void LCD_ShowChar  (uint16_t x, uint16_t y, char ch,
                    uint16_t fc, uint16_t bc, uint8_t mode);

/* 显示一个字符串（逐个字符向右推进，遇到 '\0' 结束；
 * 自动换行：超出右边界回到 x 起点、y 下移一行）
 * 示例 : LCD_ShowString(10, 10, "hello stm32", LCD_COLOR_WHITE, LCD_COLOR_BLACK, 1); */
void LCD_ShowString(uint16_t x, uint16_t y, const char *str,
                    uint16_t fc, uint16_t bc, uint8_t mode);

/* 显示无符号整数（定宽补空格，方便数值刷新时"擦干净"旧内容）
 * 参数 : num —— 待显示值；len —— 显示位数（0 或 >10 按 10 处理）
 * 示例 : LCD_ShowNum(10, 30, 12345, 5, LCD_COLOR_WHITE, LCD_COLOR_BLACK, 1);  // "12345" */
void LCD_ShowNum   (uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                    uint16_t fc, uint16_t bc, uint8_t mode);

/* 显示定点小数：val 按 10^frac 缩放（如 12345 + frac=2 → "123.45"）
 * 说明 : 不使用浮点（AC5 下更省、更稳）
 * 示例 : LCD_ShowFixed(10, 50, 31415, 2, 6, LCD_COLOR_WHITE, LCD_COLOR_BLACK, 1); // "3.14" 区间内 */
void LCD_ShowFixed (uint16_t x, uint16_t y, int32_t val, uint8_t frac, uint8_t int_len,
                    uint16_t fc, uint16_t bc, uint8_t mode);

#endif /* __FWLIB_LCD_H */
