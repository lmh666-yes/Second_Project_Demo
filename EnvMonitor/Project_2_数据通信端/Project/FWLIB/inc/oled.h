#ifndef __FWLIB_OLED_H
#define __FWLIB_OLED_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* oled.h：0.96 寸 OLED 显示屏（SSD1306 / I2C）头文件
 * 功能 : 器件级驱动 + 显存绘图；绘制只改显存，OLED_Refresh() 一次推屏
 * 依赖 : sys_i2c.h（I2C1 总线）、lcd_font.h（8×16 ASCII 点阵字库）
 * 接线（普中-天马 F407 开发板）：SCL = PB8，SDA = PB9，与板载 24C02/MPU6050
 *       共用 I2C1，板载已有 4.7k 上拉；7 位地址 0x3C（模块背面标的 0x78/0x7A
 *       是 8 位地址，右移 1 位）
 * 显存 : 128×64 / 8 = 1024 字节静态数组
 * 移植 : 换屏改 OLED_WIDTH/HEIGHT/ADDR；换总线改 OLED_I2C_ID
 *
 * 多任务（FreeRTOS 下共用一条 I2C 总线）
 *      帧缓冲与 I2C1 总线由 sys_i2c 的每总线递归互斥锁保护，本模块用
 *      OLED_FRAME_LOCK()/UNLOCK() 取 OLED_I2C_ID 对应的锁。
 *      下列公共函数自己取锁，调用方无需再包一层：Init / ClearBuffer / Refresh /
 *      RefreshDirty / MarkAllDirty / Clear / DrawPoint / GetPoint / InvertScreen /
 *      DrawHLine / DrawVLine / DrawLine / DrawRect / FillRect / DrawCircle /
 *      ShowProgress / ShowChar / ShowString / ShowStringCenter / ShowNum / ShowFixed；
 *      纯绘图函数只改 RAM 也取锁，避免与 OLED_Refresh 并发时显存改到一半。
 *      不取锁的：ErrCount / LastErr / ErrClear / IsOnline / DisplayOn / DisplayOff /
 *      SetContrast（单条命令，sys_i2c 已在事务级加锁）。
 *      OLED_Refresh 逐页写，共 40 次独立 I2C 事务且整段持锁，期间 24C02/MPU6050
 *      的事务进不来；整屏刷新 ≥20ms，锁只让出 CPU，不关中断。
 *      需要多步操作整体原子时，调用方在外层 SYS_I2C_Lock(OLED_I2C_ID) 与
 *      SYS_I2C_Unlock(OLED_I2C_ID)，递归锁允许同任务重入，不会自锁死。
 *      不要在中断里调用本模块任何公共函数：中断里加锁会被跳过，长刷屏会拖垮
 *      实时性；上电初始化阶段调度器尚未启动，加锁是空操作 */


/* 区块 1：定义与宏定义区（换板子只改这里） */
#define OLED_I2C_ID     SYS_I2C_1
#define OLED_ADDR       0x3C        /* 7 位地址 */

#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_PAGE_CNT   (OLED_HEIGHT / 8)   /* SSD1306 按 8 行一页组织 */

/* I2C 通信重试次数（失败自动重试，抗总线干扰） */
#define OLED_RETRY      3

/* 一次写显存的最大字节数：SSD1306 单次写不能跨页（一页一行 128 字节），
 * I2C 缓冲不宜过大；sys_i2c 内部逐字节写，不受本值限制 */
#define OLED_CHUNK      32

/* 编译期自检 */
#if (OLED_WIDTH < 1) || (OLED_HEIGHT < 8) || ((OLED_HEIGHT % 8) != 0)
#error "OLED_WIDTH/HEIGHT invalid (HEIGHT must be a multiple of 8)"
#endif


/* 区块 2：基础功能 */
/* 初始化：发 SSD1306 上电序列 + 清屏（内部含约 10ms 等待）
 * 前提 : 先 SYS_I2C_Init(OLED_I2C_ID, 400000)
 * 返回 : 无（不通时后续 Refresh 会失败，可用 OLED_IsOnline / OLED_ErrCount 判断） */
void OLED_Init(void);

/* 探测模块是否在线（发一次写地址，收到应答即在线）
 * 返回 : 1 = 在线；0 = 不在线
 * 注意 : 能寻址不等于写得进去；总线上拉不足或走线过长时寻址成功而数据写失败，
 *        本函数仍返回 1，要看 OLED_ErrCount()（Refresh 后计数在涨即写失败） */
uint8_t OLED_IsOnline(void);

/* 上屏失败次数（累计，volatile，任务里随时可读）
 * 判读 : 计数在涨 = 写不进去（接线/上拉/地址/总线被占）；
 *        计数为 0 仍不亮 = 问题在屏本身或对比度/供电，不在通信 */
uint16_t OLED_ErrCount(void);

/* 最近一次失败对应的 SYS_I2C 错误码（0 = 从未失败；见 sys_i2c.h 的 SYS_I2C_ERR_*） */
uint8_t OLED_LastErr(void);

/* 清零错误统计 */
void OLED_ErrClear(void);

/* 只清显存（不清屏；下次 Refresh 才生效） */
void OLED_ClearBuffer(void);

/* 清显存 + 立即推屏
 * 返回 : 0 = 成功；非 0 = 写失败的页数（见 OLED_Refresh） */
uint8_t OLED_Clear(void);

/* 把显存内容推到屏上，唯一的真正显示动作
 * 返回 : 0 = 全部 8 页写成功；> 0 = 写失败的页数（1 ~ 8）
 * 阻塞 : 40 次 I2C 事务，整屏数据约 23~25ms（400kHz，不含重试）；不可放进
 *        1ms 定时器中断；屏不亮时重试 + 超时可能放大到数百毫秒 */
uint8_t OLED_Refresh(void);

/* 局部刷新：只把变过的页推上屏（脏页跟踪），改一小块时比整屏快
 * 说明 : ClearBuffer / DrawPoint（以及所有走它的图元、字符）/ InvertScreen
 *        改显存时会把所在页标脏；本函数只写脏页（每页 128 字节），某页写进屏
 *        后才清其脏位，没写成功的页保留脏位下次再试；整屏 OLED_Refresh 成功后
 *        脏位也会清干净
 * 返回 : 本次成功刷新的页数；0 = 没有改动（不碰总线，立即返回）；1~8 = 页数
 * 阻塞 : 每页约 4 次数据事务（≈3ms @400kHz，不含重试），最坏 8 页 ≈ 整屏刷新；
 *        同样不要放进 1ms 定时器中断 */
uint8_t OLED_RefreshDirty(void);

/* 手动把 8 页全部标脏
 * 场合 : 外部绕过本模块直接改过屏内容，或下次局部刷新要整屏重来
 * 说明 : 正常流程不需要：OLED_ClearBuffer 会自动全标脏，整屏 OLED_Refresh
 *        成功后脏位也已清干净 */
void OLED_MarkAllDirty(void);

/* 屏幕开关（1 = 亮，0 = 灭；灭屏后显存内容保留） */
void OLED_DisplayOn(void);
void OLED_DisplayOff(void);

/* 亮度（0~255，默认 0xCF） */
void OLED_SetContrast(uint8_t contrast);


/* 区块 3：扩展功能（绘图 / 文字） */
/* ---- 像素级 ---- */
/* 画点：on = 1 点亮 / 0 熄灭；越界自动忽略 */
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

/* 进度条/柱状图：在 (x,y) 起、宽 w 高 h 的框内按 permille（0~1000）填充 */
void OLED_ShowProgress(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t permille);

/* ---- 文字（8×16 点阵；每字符占 8 列 × 2 页，一行 16 字符、共 4 行） ---- */
/* 显示一个 ASCII 字符
 * 参数 : inv，0 = 黑底白字；1 = 白底黑字（反显，用于选中/高亮） */
void OLED_ShowChar  (uint16_t x, uint16_t y, char ch, uint8_t inv);

/* 显示字符串（自动换行：超出右边界回到 x 起点并下移一行；支持 '\n' 强制换行） */
void OLED_ShowString(uint16_t x, uint16_t y, const char *str, uint8_t inv);

/* 定宽显示无符号整数（前导补空格，便于数值原地刷新） */
void OLED_ShowNum   (uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t inv);

/* 定宽显示定点小数（不用浮点）：val 按 10^frac 放大，frac 上限 6；
 * int_len 为整数位宽度，传 0 按 1 处理；val 为负时前面加 '-' */
void OLED_ShowFixed (uint16_t x, uint16_t y, int32_t val, uint8_t frac,
                     uint8_t int_len, uint8_t inv);

/* 居中显示一行字符串（按 8×16 字格自动算起点 x） */
void OLED_ShowStringCenter(uint16_t y, const char *str, uint8_t inv);

#endif /* __FWLIB_OLED_H */
