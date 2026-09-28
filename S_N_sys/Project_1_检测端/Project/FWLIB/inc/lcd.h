#ifndef __FWLIB_LCD_H
#define __FWLIB_LCD_H

#include "stm32f4xx.h"

/* ================================================================
 *  lcd.h —— 【板载】TFT-LCD 显示屏模块（FSMC + ILI9341）  头文件
 * ================================================================
 *  设计定位 : 标准外设库之上的"寄存器级"薄封装
 *             —— 总线：FSMC Bank1 · NE4（16 位 · 模式 A · SRAM）
 *             —— 控制器：ILI9341（2.8/3.5 寸彩屏最常见型号）
 *             —— 本版为"基础版"：初始化 + 清屏 / 画点 / 画线 / 填充
 *  标准库关键词 : RCC_AHB1PeriphClockCmd / RCC_AHB3PeriphClockCmd /
 *                 GPIO_PinAFConfig / GPIO_Init;FSMC 段为寄存器直写
 *                 （标准外设库不含 FSMC 外设驱动,只能直接配寄存器）
 *
 *  ⚠ 区块 3（扩展功能）尚未实现：字符与字符串（需点阵字库）、
 *    图片显示、横竖屏切换、读点、触摸屏驱动 —— 需要时按同样
 *    结构补充（序列/字库等数据放 .c）。
 *
 *  本板接线（对照 GEC-M4 原理图；换板子按区块 1 修改）:
 *      数据线 : FSMC_D0 ~ FSMC_D15 —— F4 系列固定引脚映射（见 lcd.c 表）
 *      控制线 : CS = PG12 (FSMC_NE4)   RS = PF12 (FSMC_A6)
 *               WR = PD5  (FSMC_NWE)   RD = PD4  (FSMC_NOE)
 *               BL = PB15 (普通 GPIO，背光开关)
 *      ⚠ RS(A6) 请对照你自己板子的实际连接核对：若 RS 接的是其它
 *        地址线（例如 A18），只需修改下方 LCD_CMD_ADDR / LCD_DATA_ADDR
 *
 *  插接说明（本板 LCD 是"插座"，不是焊死的屏）:
 *      板上只有 2 排母排针插座，屏模块插上去才有显示——固件层面
 *      与"板载屏"完全一样（照样走 FSMC + 下面这几根引脚）：
 *      ① 不插屏 / 不用屏 → 不要调用 LCD_Init，其余功能不受影响；
 *      ② 基础版全程"只写不读"，没插屏也不会卡死（无等屏应答的循环）；
 *      ③ 屏插上后不亮 → 先查背光(BL)，再查接线：CS/RS/WR/RD 四项。
 *
 *  使用方式 :
 *      LCD_Init();                                // ① 初始化(末步清屏为黑)
 *      LCD_Clear(LCD_COLOR_BLUE);                 // ② 整屏填充
 *      LCD_DrawLine(0, 0, 239, 319, LCD_COLOR_WHITE);
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
/* ILI9341 常见 240×320；若换 ILI9486(320×480)等屏，同步改这里
 * 与 lcd.c 的初始化序列 */
#define LCD_WIDTH               240
#define LCD_HEIGHT              320

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
/* 单位：HCLK 周期数（当前 168MHz）。花屏/白屏/闪屏 → 先把三个值都改大
 * 试试；正常点亮后可逐步调小提速（读时序本基础版暂未使用）。
 * ⚠ 时序以 HCLK 周期计：切换主频（如 sys_clock 切到 HSI 16MHz）后
 *    实际时间会成比例变化，屏幕异常时先按当前主频重新评估 */
#define LCD_FSMC_ADDR_SETUP     6    /* 地址建立时间 */
#define LCD_FSMC_ADDR_HOLD      1    /* 地址保持时间 */
#define LCD_FSMC_DATA_SETUP     6    /* 数据建立时间 */

/* -------------------- 显示方向（MADCTL） -------------------- */
/* 写 ILI9341 的 0x36 寄存器，决定横竖屏与颜色顺序（RGB/BGR）:
 *   0x48 = 竖屏 + BGR（默认，常见屏配色正确）
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
/* 初始化：FSMC 总线 + 控制引脚 + ILI9341 上电序列（末步清屏为黑）
 *
 * 标准库调用链（库内部依次调用，可对照学习）:
 *   ① RCC_AHB1PeriphClockCmd + RCC_AHB3PeriphClockCmd  开 GPIO 端口 / FSMC 时钟
 *   ② GPIO_PinAFConfig + GPIO_Init        数据/控制引脚复用为 FSMC
 *   ③ FSMC 寄存器直写                     BCR/BTR 配置各片选时序（16 位 SRAM 模式 A）
 *   ④ 命令序列                            ILI9341 上电初始化（0xCF/0xED/…）
 *   ⑤ GPIO_OutInit + GPIO_OutReset        背光引脚（经 gpio_core）
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
 * 示例 : LCD_SetWindow(0, 0, 239, 319);   // 全屏窗口 */
void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/* 画一个像素点（自动忽略越界坐标）
 * 示例 : LCD_DrawPoint(120, 160, LCD_COLOR_RED);   // 屏幕中心点红色 */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);

/* 填充矩形区域（含两端点；内部一个窗口 + 连续写像素，速度快）
 * 示例 : LCD_FillRect(20, 20, 100, 80, LCD_COLOR_RED);   // 左上角红块 */
void LCD_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/* 画直线（Bresenham 算法，横/竖/斜线都支持；起点终点任意方向）
 * 示例 : LCD_DrawLine(0, 0, 239, 319, LCD_COLOR_WHITE);   // 画对角线 */
void LCD_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);


/* ================================================================
 *                    区块 3：扩展功能（待补充）
 * ================================================================
 * 预留方向（需要时按本库统一风格添加）:
 *      - 字符 / 字符串显示（需 8×16 ASCII 点阵字库，放 .c）
 *      - 图片显示（取模数据 + 逐像素搬运）
 *      - 横屏 / 竖屏切换（改 MADCTL 0x36 参数与宽高映射）
 *      - 读点 / 读颜色（需 FSMC 读时序，当前未配置读参数）
 *      - 触摸屏（XPT2046：T_SCK=PB0 / T_CS=PC13 / T_PEN=PB1 / T_MOSI=PF11）
 * ================================================================ */

#endif /* __FWLIB_LCD_H */
