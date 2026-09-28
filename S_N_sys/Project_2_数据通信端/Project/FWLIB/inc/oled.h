#ifndef __FWLIB_OLED_H
#define __FWLIB_OLED_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* ================================================================
 *  oled.h —— 【外接】0.96 寸 OLED 显示屏（SSD1306 / I2C）  头文件
 * ================================================================
 *  设计定位 : 器件级驱动 + 显存（framebuffer）绘图
 *             —— 所有绘制都先写显存，最后 OLED_Refresh() 一次推屏；
 *                避免"逐点写屏"造成的闪烁与缓慢
 *  依赖     : sys_i2c.h（I2C1 总线）、lcd_font.h（8×16 ASCII 点阵字库）
 *  标准库关键词 : 无——全部走 SYS_I2C 的寄存器写
 *
 *  【接线（普中-天马 F407开发板）】
 *      SCL = PB8   SDA = PB9  —— 与板载 24C02/MPU6050 共用 I2C1，
 *                                板载已有 4.7k 上拉，模块直接插 I2C 排针即可
 *      7 位地址 0x3C（模块背面一般标 0x78/0x7A，那是 8 位地址，右移 1 位）
 *
 *  使用方式 :
 *      SYS_I2C_Init(SYS_I2C_1, 400000);   // ① 开总线
 *      OLED_Init();                       // ② 初始化（含清屏）
 *      OLED_ShowString(0, 0, "hello oled");   // ③ 画到显存
 *      OLED_Refresh();                        // ④ 推屏
 *
 *  显存说明 : 128×64 / 8 = 1024 字节静态数组（RAM 占用，F407 128KB 毫无压力）
 *  移植指引 : 换屏改 OLED_WIDTH/HEIGHT/ADDR；换总线改 OLED_I2C_ID；
 *            这版按"页地址模式"写，绝大多数 SSD1306 模块通用。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
#define OLED_I2C_ID     SYS_I2C_1
#define OLED_ADDR       0x3C        /* 7 位地址 */

#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_PAGE_CNT   (OLED_HEIGHT / 8)   /* SSD1306 按 8 行一页组织 */

/* I2C 通信重试次数（失败自动重试，抗总线干扰） */
#define OLED_RETRY      3

/* 一次写显存的最大字节数：SSD1306 单次写不能跨页，且 I2C 缓冲不宜太大
 * （128 字节刚好是"一页一行"；sys_i2c 内部是逐字节写，不怕长） */
#define OLED_CHUNK      32

/* 编译期自检 */
#if (OLED_WIDTH < 1) || (OLED_HEIGHT < 8) || ((OLED_HEIGHT % 8) != 0)
#error "OLED_WIDTH/HEIGHT invalid (HEIGHT must be a multiple of 8)"
#endif


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：发 SSD1306 上电序列 + 清屏（内部含约 10ms 等待）
 * 前提 : 先 SYS_I2C_Init(OLED_I2C_ID, 400000)
 * 返回 : 无（不通时后续 Refresh 会静默失败，可用 OLED_IsOnline 判断）
 * 示例 : SYS_I2C_Init(SYS_I2C_1, 400000); OLED_Init(); */
void OLED_Init(void);

/* 探测模块是否在线（发一次写地址，收到应答即在线）
 * 返回 : 1 = 在线；0 = 不在线
 * 示例 : if (!OLED_IsOnline()) printf("%s\r\n", SYS_I2C_ErrStr(SYS_I2C_ERR_ADDR)); */
uint8_t OLED_IsOnline(void);

/* 只清显存（不清屏；下次 Refresh 才生效） */
void OLED_ClearBuffer(void);

/* 清显存 + 立即推屏（最常用的一步到位清屏） */
void OLED_Clear(void);

/* 把显存内容推到屏上（唯一的"真正显示"动作） */
void OLED_Refresh(void);

/* 屏幕开关（1 = 亮，0 = 灭；灭屏后显存内容保留） */
void OLED_DisplayOn(void);
void OLED_DisplayOff(void);

/* 亮度（0~255，默认 0xCF） */
void OLED_SetContrast(uint8_t contrast);


/* ================================================================
 *                    区块 3：扩展功能（绘图 / 文字）
 * ================================================================ */
/* ---- 像素级 ---- */
/* 画点：on = 1 点亮 / 0 熄灭；越界自动忽略
 * 示例 : OLED_DrawPoint(10, 10, 1); */
void OLED_DrawPoint(uint16_t x, uint16_t y, uint8_t on);

/* 读显存某点（1 = 亮） */
uint8_t OLED_GetPoint(uint16_t x, uint16_t y);

/* 反色整屏（显存取反，Refresh 后生效） */
void OLED_InvertScreen(void);

/* ---- 图元 ---- */
void OLED_DrawHLine  (uint16_t x0, uint16_t x1, uint16_t y, uint8_t on);
void OLED_DrawVLine  (uint16_t x, uint16_t y0, uint16_t y1, uint8_t on);
void OLED_DrawLine   (uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on);
void OLED_DrawRect   (uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on);
void OLED_FillRect   (uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on);
void OLED_DrawCircle (int16_t x0, int16_t y0, int16_t r, uint8_t on);

/* 进度条/柱状图：在 (x,y) 起、宽 w 高 h 的框内，按 permille（0~1000）填充
 * 示例 : OLED_ShowProgress(0, 54, 128, 8, 700);   // 70% 进度条 */
void OLED_ShowProgress(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t permille);

/* ---- 文字（8×16 点阵；每字符占 8 列 × 2 页，一行 16 字符、共 4 行） ---- */
/* 显示一个 ASCII 字符
 * 参数 : inv —— 0 = 黑底白字；1 = 白底黑字（反显，用来做"选中/高亮"）
 * 示例 : OLED_ShowChar(0, 0, 'A', 0); */
void OLED_ShowChar  (uint16_t x, uint16_t y, char ch, uint8_t inv);

/* 显示字符串（自动换行：超出右边界回到 x 起点、下移一行；
 * 支持 '\n' 强制换行）
 * 示例 : OLED_ShowString(0, 0, "temp: 25.3C", 0); */
void OLED_ShowString(uint16_t x, uint16_t y, const char *str, uint8_t inv);

/* 定宽显示无符号整数（前导补空格，方便数值原地刷新）
 * 示例 : OLED_ShowNum(0, 16, 12345, 5, 0);   // "12345" */
void OLED_ShowNum   (uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t inv);

/* 定宽显示定点小数（不用浮点）
 * 示例 : OLED_ShowFixed(0, 32, 253, 1, 3, 0);   // " 25.3"（val=253, frac=1） */
void OLED_ShowFixed (uint16_t x, uint16_t y, int32_t val, uint8_t frac,
                     uint8_t int_len, uint8_t inv);

/* 居中显示一行字符串（按 8×16 字格自动算起点 x）
 * 示例 : OLED_ShowStringCenter(0, "MPU6050", 0); */
void OLED_ShowStringCenter(uint16_t y, const char *str, uint8_t inv);

#endif /* __FWLIB_OLED_H */
