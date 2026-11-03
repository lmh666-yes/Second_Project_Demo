#ifndef __FWLIB_SYS_OLED_H
#define __FWLIB_SYS_OLED_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* sys_oled.h — 外接 OLED 显示模块(SSD1306, I2C, 128x64)头文件
 * 接线: VCC→3.3V / GND→GND / SCL→PB8(I2C1) / SDA→PB9(I2C1);与 24C02/MPU6050 共用 I2C1;器件地址 0x3C 或 0x3D
 * 并发: 帧缓冲 oled_buf 与 I2C1 总线由 sys_i2c 每总线递归互斥锁统一保护,模块内用 OLED_FRAME_LOCK()/UNLOCK() 取当前 s_bus 的锁
 * 约束: Init/Clear/Refresh/RefreshDirty/MarkAllDirty/ShowXxx/SetPixel 内部自行取锁;整屏刷新 ≥20ms,不可在中断中调用(中断内加锁被跳过) */


/* ================================================================
 *                    区块 1：定义与宏定义
 * ================================================================ */
/* 器件 7 位地址: 默认 0x3C;模块焊 0x3D 时改此处 */
#define SYS_OLED_I2C_ADDR   SYS_I2C_ADDR_SSD1306        /* 0x3C */

/* 屏幕分辨率 128x64;改 128x32 需改页数并核对初始化表的多路复用比 0xA8,见 .c 初始化表 */
#define SYS_OLED_WIDTH      128
#define SYS_OLED_HEIGHT     64
#define SYS_OLED_PAGES      (SYS_OLED_HEIGHT / 8)       /* 8 页,每页 8 像素高 */

/* 字库规格(内置 8x8 英文点阵) */
#define SYS_OLED_CHAR_W     8
#define SYS_OLED_CHAR_H     8


/* ================================================================
 *                    区块 2：基础功能(初始化/清屏/刷新/字符)
 * ================================================================ */
/* 初始化: I2C 自检 + SSD1306 上电命令表 + 清屏
 * 说明 : 内部 SYS_I2C_Init(bus, 400000);若同总线有仅耐 100kHz 的老器件,初始化后可再 SYS_I2C_Init(bus, 100000) 降速,OLED 仍可用
 * 返回 : 0 = 成功;1 = 器件无应答(排查见 sys_i2c.h 文件头) */
uint8_t SYS_OLED_Init(SysI2cId_t bus, uint8_t addr);

/* 清空显存(全屏黑);改后需 SYS_OLED_Refresh 才上屏 */
void SYS_OLED_Clear(void);

/* 整帧刷新: 把显存缓冲一次流水发到屏(1024 字节)
 * 约定 : ShowXxx/SetPixel 只改内存缓冲,调本函数才上屏,可多处改完一次刷屏
 * 返回 : 0 = 成功;1 = 失败(错误码见 SYS_OLED_LastErr,次数见 SYS_OLED_ErrCount)
 *  阻塞 : 1024 字节 @400kHz 约 23~25ms;失败时还需等 I2C 超时,不可放入 1ms 定时器中断 */
uint8_t SYS_OLED_Refresh(void);

/* 局部刷新: 只推送脏页
 * 说明 : Clear / ShowChar / ShowString / ShowNum / ShowFloat / SetPixel / ShowBitmap 改显存时同时标脏所在页;仅本函数发送脏页(一页 = 128 字节 = 1 次 I2C 数据事务),写入成功才清脏位
 * 返回 : 本次刷新成功的页数;0 = 无改动,不访问总线立即返回;1~8 = 实际写入页数
 *  阻塞 : 每页 128 字节 @400kHz 约 3ms,最坏 8 页 ≈ 整屏刷新,不可放入 1ms 定时器中断 */
uint8_t SYS_OLED_RefreshDirty(void);

/* 手动把 8 页全部标脏
 * 说明 : 外部绕过本模块改过屏内容时用;正常流程不需要,Clear 自动全标脏,整屏 Refresh 成功后脏位已清 */
void SYS_OLED_MarkAllDirty(void);

/* 累计上屏失败次数;屏不亮时先看此处:计数增长说明写入失败(接线/上拉/地址/总线被占),计数为 0 则查屏与供电 */
uint16_t SYS_OLED_ErrCount(void);

/* 最近一次失败对应的 SYS_I2C 错误码;0 = 从未失败,错误码见 sys_i2c.h SYS_I2C_ERR_* */
uint8_t SYS_OLED_LastErr(void);

/* 清零错误统计 */
void SYS_OLED_ErrClear(void);

/* 显示一个 ASCII 字符: 内置 8x8 字库,0x20~0x7E;其余字符显示为空格
 * 参数 : page 行号 0~7(每行 8 像素高);x 像素列 0~127 */
void SYS_OLED_ShowChar(uint8_t page, uint8_t x, char ch);

/* 显示字符串: 逐字符右移,到右边界停止,不自动换行;每行 16 字符(8x8 字库)
 * 参数 : page 行号 0~7;x 起始像素列 0~127 */
void SYS_OLED_ShowString(uint8_t page, uint8_t x, const char *str);


/* ================================================================
 *                    区块 3：扩展功能(数值/浮点/画点/位图)
 * ================================================================ */
/* 显示有符号十进制整数,自动处理负号;int32_t 全范围
 * 参数 : page 行号 0~7;x 起始像素列 0~127 */
void SYS_OLED_ShowNum(uint8_t page, uint8_t x, int32_t num);

/* 显示浮点数,固定小数位并四舍五入
 * 参数 : page 行号 0~7;x 起始像素列 0~127;dec 小数位数 0~3 */
void SYS_OLED_ShowFloat(uint8_t page, uint8_t x, float value, uint8_t dec);

/* 画单点: on = 1 亮,0 灭
 * 参数 : x 像素列 0~127;y 像素行 0~63 */
void SYS_OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on);

/* 显示位图: 数据按页、列排,先第 0 页 8 行逐列 1 字节,再第 1 页……每字节 bit0 在页顶(取模软件按此格式导出)
 * 参数 : page/x 起始页与起始列;w/h 位图像素宽高,均为 8 的倍数 */
void SYS_OLED_ShowBitmap(uint8_t page, uint8_t x, const uint8_t *bmp,
                         uint8_t w, uint8_t h);

/* 显示开关(睡眠用),不影响显存内容 */
void SYS_OLED_DisplayOn(void);
void SYS_OLED_DisplayOff(void);

#endif /* __FWLIB_SYS_OLED_H */
