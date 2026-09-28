#ifndef __FWLIB_SYS_OLED_H
#define __FWLIB_SYS_OLED_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* ================================================================
 *  sys_oled.h —— 【外接】OLED 显示模块（SSD1306, I2C 版, 128x64）  头文件
 * ================================================================
 *  设计定位 : 外接 0.96 寸 OLED 的"薄封装"—— 显存缓冲 + 内置 8x8
 *             英文字库,数据本地显示的"最小够用"方案
 *             （板载 TFT-LCD 见 lcd 模块;本模块面向外接 I2C OLED）
 *  标准库关键词 : 无——屏幕通信走 sys_i2c 的 I2C 时序;本模块的组织方式
 *                 是教科书式 SSD1306 驱动:命令表 + 显存缓冲 + 整帧刷新
 *
 *  本板接线（OLED 为外接模块,接线到排针）:
 *      四线接法: VCC→3.3V / GND→GND / SCL→PB8(I2C1) / SDA→PB9(I2C1)
 *      —— 与板载 24C02/MPU6050 共用 I2C1 总线,排针同名信号直接并联
 *      器件地址: 0x3C(默认) 或 0x3D(模块上地址电阻不同)——见区块 1 宏
 *
 *  使用方式 :
 *      SYS_OLED_Init(SYS_I2C_1, SYS_OLED_I2C_ADDR);   // ① 初始化(自检+点亮)
 *      SYS_OLED_ShowString(0, 0, "TEMP: 25.4 C");     // ② 写第 0 页(共 8 页)
 *      SYS_OLED_Refresh();                            // ③ 刷屏(改完务必刷新)
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 器件 7 位地址:0x3C = 常见默认;部分模块焊 0x3D(复位脚接 VCC)后改这里 */
#define SYS_OLED_I2C_ADDR   SYS_I2C_ADDR_SSD1306        /* 0x3C */

/* 屏幕分辨率(128x64 是 0.96 寸最常见的规格;换 128x32 需改页数并核对
 * 初始化表里的多路复用比 0xA8——见 .c 的初始化表注释) */
#define SYS_OLED_WIDTH      128
#define SYS_OLED_HEIGHT     64
#define SYS_OLED_PAGES      (SYS_OLED_HEIGHT / 8)       /* 8 页,每页 8 像素高 */

/* 字库规格(内置 8x8 英文点阵) */
#define SYS_OLED_CHAR_W     8
#define SYS_OLED_CHAR_H     8


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化: I2C 自检 + SSD1306 上电命令表 + 清屏
 * 说明 : 内部先 SYS_I2C_Init(bus, 400000)——OLED 支持 400kHz;
 *        同总线上若有"只耐 100kHz"的老器件,初始化后用
 *        SYS_I2C_Init(bus, 100000) 再降回来即可(OLED 仍可用)
 * 返回 : 0 = 成功;1 = 器件无应答(接线/地址/供电排查见 sys_i2c.h 文件头)
 * 示例 : if (SYS_OLED_Init(SYS_I2C_1, SYS_OLED_I2C_ADDR) != 0) {
 *            // 没检测到屏——查四根线/地址 0x3C 与 0x3D
 *        } */
uint8_t SYS_OLED_Init(SysI2cId_t bus, uint8_t addr);

/* 清空显存(全屏黑)——改完必须 SYS_OLED_Refresh 才上屏 */
void SYS_OLED_Clear(void);

/* 把显存缓冲整帧刷到屏(I2C 一次流水发 1024 字节)
 * 约定 : ShowXxx/SetPixel 改的都是内存缓冲,调本函数才真正显示——
 *        好处是可以改完多处内容后一次刷屏,无闪烁 */
void SYS_OLED_Refresh(void);

/* 显示一个 ASCII 字符(内置 8x8 字库,0x20~0x7E;其余字符显示为空格)
 * 参数 : page —— 行号 0~7(每行 8 像素高);x —— 像素列 0~127
 * 示例 : SYS_OLED_ShowChar(1, 16, 'A'); */
void SYS_OLED_ShowChar(uint8_t page, uint8_t x, char ch);

/* 显示字符串(逐字符右移;碰到右边界停止,不自动换行)
 * 示例 : SYS_OLED_ShowString(0, 0, "Hello OLED");   // 每行可放 16 字符 */
void SYS_OLED_ShowString(uint8_t page, uint8_t x, const char *str);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 显示有符号十进制整数(自动处理负号;INT32 全范围)
 * 示例 : SYS_OLED_ShowNum(2, 0, -1234); */
void SYS_OLED_ShowNum(uint8_t page, uint8_t x, int32_t num);

/* 显示浮点数(固定小数位;四舍五入)——传感器数值显示用
 * 参数 : dec —— 小数位数 0~3
 * 示例 : SYS_OLED_ShowFloat(2, 48, 25.43f, 1);    // 屏上显示 25.4 */
void SYS_OLED_ShowFloat(uint8_t page, uint8_t x, float value, uint8_t dec);

/* 画单点(on = 1 亮 / 0 灭)——自绘图形、进度条等用
 * 示例 : SYS_OLED_SetPixel(64, 32, 1); */
void SYS_OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on);

/* 显示位图(数据格式:按页、列排——先第 0 页 8 行逐列 1 字节,
 * 再第 1 页……每字节 bit0 在页顶。用取模软件时按此格式导出即可)
 * 参数 : page/x —— 起始页与起始列;w/h —— 位图像素宽高(均为 8 的倍数)
 * 示例 : SYS_OLED_ShowBitmap(3, 56, bmp, 16, 16); */
void SYS_OLED_ShowBitmap(uint8_t page, uint8_t x, const uint8_t *bmp,
                         uint8_t w, uint8_t h);

/* 显示开/关(睡眠用;不影响显存内容) */
void SYS_OLED_DisplayOn(void);
void SYS_OLED_DisplayOff(void);

#endif /* __FWLIB_SYS_OLED_H */
