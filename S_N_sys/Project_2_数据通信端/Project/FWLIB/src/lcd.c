#include "lcd.h"
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_ms 等）独立文件 */
#include "lcd_font.h"   /* 8x16 ASCII 点阵字库 */

/* lcd.c：板载 TFT-LCD 显示屏（FSMC + ILI9481）实现文件
 * FSMC 用寄存器直写配置，模块只依赖 GPIO/RCC
 * 数据线为 F4 固定映射（见下表），控制线/背光走 lcd.h 宏
 * ILI9481 初始化用表驱动：一条命令一行 */


/* FSMC_D0 ~ D15 的引脚由 STM32F4 硬件固定，软件改不了；换板子时按下表实际接线改
 * 本表只把 16 个引脚配成 FSMC 复用，数组下标 i 对应数据线 D_i
 * 天马 F407 开发板实测线序（对照原理图 FSMC_D0~D15 网络名）：
 *   D0=PD14  D1=PD15  D2=PD0   D3=PD1   D4=PE7   D5=PE8
 *   D6=PE9   D7=PE10  D8=PE11  D9=PE12  D10=PE13 D11=PE14
 *   D12=PE15 D13=PD8  D14=PD9  D15=PD10 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} LcdDataPin_t;

static const LcdDataPin_t lcd_data_pins[16] = {
    { GPIOD, GPIO_Pin_14 },   /* D0  */
    { GPIOD, GPIO_Pin_15 },   /* D1  */
    { GPIOD, GPIO_Pin_0  },   /* D2  */
    { GPIOD, GPIO_Pin_1  },   /* D3  */
    { GPIOE, GPIO_Pin_7  },   /* D4  */
    { GPIOE, GPIO_Pin_8  },   /* D5  */
    { GPIOE, GPIO_Pin_9  },   /* D6  */
    { GPIOE, GPIO_Pin_10 },   /* D7  */
    { GPIOE, GPIO_Pin_11 },   /* D8  */
    { GPIOE, GPIO_Pin_12 },   /* D9  */
    { GPIOE, GPIO_Pin_13 },   /* D10 */
    { GPIOE, GPIO_Pin_14 },   /* D11 */
    { GPIOE, GPIO_Pin_15 },   /* D12 */
    { GPIOD, GPIO_Pin_8  },   /* D13 */
    { GPIOD, GPIO_Pin_9  },   /* D14 */
    { GPIOD, GPIO_Pin_10 },   /* D15 */
};


/* RS 引脚接在 FSMC 地址线上（本板 A6）：
 *   写 LCD_CMD_ADDR  → RS=0 → 屏收到命令
 *   写 LCD_DATA_ADDR → RS=1 → 屏收到数据 */
#define LCD_REG   (*(volatile uint16_t *)LCD_CMD_ADDR)
#define LCD_RAM   (*(volatile uint16_t *)LCD_DATA_ADDR)

/* 写命令 / 写数据 / 读数据 */
static void lcd_write_cmd(uint8_t cmd)   { LCD_REG  = (uint16_t)cmd; }
static void lcd_write_data(uint16_t dat) { LCD_RAM  = dat; }
static uint16_t lcd_read_data(void)      { return LCD_RAM; }

/* 当前显示方向下的宽 / 高
 * LCD_WIDTH / LCD_HEIGHT 是面板原生尺寸（竖屏 320x480），固定不变；
 * 本文件内部一律用 lcd_w / lcd_h，随方向对调 */
static uint16_t lcd_w   = LCD_WIDTH;
static uint16_t lcd_h   = LCD_HEIGHT;
static uint8_t  lcd_rot = (uint8_t)LCD_ROT_0;

/* 各方向对应的 MADCTL(0x36) 值，BGR 位(0x08)统一开：
 *   ROT_0 = LCD_MADCTL（默认 0x48，竖屏）  ROT_90  = 0x28（MV）
 *   ROT_180 = 0x88（MY）                   ROT_270 = 0xE8（MY|MX|MV）
 * 红蓝互换时把这几个值的 BGR 位取反 */
static const uint8_t lcd_madctl_tab[4] = {
    (uint8_t)LCD_MADCTL, 0x28U, 0x88U, 0xE8U
};


/* 内部辅助 */
/* 引脚掩码转引脚序号：统一走 gpio_core 的 GPIO_PinSource */

/* GPIO 复用初始化：把 16 根数据线 + 4 根控制线全部配成 FSMC 复用 */
static void lcd_gpio_init(void)
{
    GPIO_InitTypeDef gi;
    uint8_t i;

    /* 时钟：用到的 4 个端口 + FSMC 控制器(AHB3) */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD |
                           RCC_AHB1Periph_GPIOE |
                           RCC_AHB1Periph_GPIOF |
                           RCC_AHB1Periph_GPIOG, ENABLE);
    RCC_AHB3PeriphClockCmd(RCC_AHB3Periph_FSMC, ENABLE);

    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    /* 数据线 D0 ~ D15 */
    for (i = 0; i < 16U; i++) {
        gi.GPIO_Pin = lcd_data_pins[i].pin;
        GPIO_PinAFConfig(lcd_data_pins[i].port,
                         GPIO_PinSource(lcd_data_pins[i].pin), GPIO_AF_FSMC);
        GPIO_Init(lcd_data_pins[i].port, &gi);
    }

    /* 控制线：CS=PG12(NE4)  RS=PF12(A6)  WR=PD5(NWE)  RD=PD4(NOE) */
    gi.GPIO_Pin = GPIO_Pin_12;
    GPIO_PinAFConfig(GPIOG, GPIO_PinSource12, GPIO_AF_FSMC);
    GPIO_Init(GPIOG, &gi);

    gi.GPIO_Pin = GPIO_Pin_12;
    GPIO_PinAFConfig(GPIOF, GPIO_PinSource12, GPIO_AF_FSMC);
    GPIO_Init(GPIOF, &gi);

    gi.GPIO_Pin = GPIO_Pin_5;
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource5, GPIO_AF_FSMC);
    GPIO_Init(GPIOD, &gi);

    gi.GPIO_Pin = GPIO_Pin_4;
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource4, GPIO_AF_FSMC);
    GPIO_Init(GPIOD, &gi);
}

/* FSMC 寄存器配置：Bank1 NE4，16 位 SRAM，异步模式 A
 * 直接写 BCR4/BTR4（BTCR[6]/BTCR[7]），不依赖标准库 FSMC 组件 */
static void lcd_fsmc_init(void)
{
    uint32_t bcr = 0;
    uint32_t btr = 0;

    /* BCR4（存储器块控制）
     *   bit0   MBKEN = 1    存储器块使能
     *   bit3:2 MTYP  = 00   SRAM 类型
     *   bit5:4 MWID  = 01   16 位数据总线
     *   bit12  WREN  = 1    写使能
     *   其余位保持 0：异步模式 A、无等待、无突发 */
    bcr |= (1UL << 0);
    bcr |= (1UL << 4);
    bcr |= (1UL << 12);
    FSMC_Bank1->BTCR[6] = bcr;

    /* BTR4（时序），各字段编码 = 实际周期数 - 1
     *   ADDSET[3:0]  地址建立   ADDHLD[7:4]   地址保持
     *   DATAST[15:8] 数据建立   BUSTURN[19:16] 总线回转，先给 1 */
    btr |= (((uint32_t)LCD_FSMC_ADDR_SETUP - 1UL) & 0xFUL) << 0;
    btr |= (((uint32_t)LCD_FSMC_ADDR_HOLD  - 1UL) & 0xFUL) << 4;
    btr |= (((uint32_t)LCD_FSMC_DATA_SETUP - 1UL) & 0xFFUL) << 8;
    btr |= (1UL & 0xFUL) << 16;
    FSMC_Bank1->BTCR[7] = btr;
}

/* 背光引脚初始化：普通推挽输出，灯由 LCD_BackLight 控制 */
static void lcd_bl_hw_init(void)
{
    GPIO_OutInit(LCD_BL_PORT, LCD_BL_PIN);
}


/* ILI9481 初始化序列表，每行一条命令：
 *   cmd 命令码 / len 数据长度 / dat 数据 / delay_ms 延时
 * 取自 ILI9481 数据手册推荐上电序列（电源/驱动/伽马设置），
 * 与普中 3.5" 9481 模块例程一致；换屏时整体替换本表 */

#define LCD_SEQ_END   0xFFFFU    /* 序列表结束哨兵（cmd 字段填它即结束） */

typedef struct {
    uint16_t      cmd;
    uint8_t       len;
    const uint8_t *dat;
    uint8_t       delay_ms;
} LcdSeq_t;

/* 各条命令的数据，照抄数据手册典型值 */
static const uint8_t lcd_pw_set_1[]   = { 0x07, 0x42, 0x18 };              /* D0 电源设定 1 */
static const uint8_t lcd_vcom_set[]   = { 0x00, 0x07, 0x10 };              /* D1 VCOM 设定 */
static const uint8_t lcd_pw_set_2[]   = { 0x01, 0x02 };                    /* D2 电源设定 2（常态） */
static const uint8_t lcd_panel_drv[]  = { 0x10, 0x3B, 0x00, 0x02, 0x11 }; /* C0 面板驱动 */
static const uint8_t lcd_timing[]     = { 0x10, 0x10, 0x88 };              /* C1 时序设定 */
static const uint8_t lcd_frame_rtn[]  = { 0x03 };                          /* C5 帧率 */
static const uint8_t lcd_iface_ctrl[] = { 0x02 };                          /* C6 接口控制 */
static const uint8_t lcd_gamma_ctrl[] = {                                   /* C8 伽马控制 */
    0x00, 0x25, 0x21, 0x30, 0x35, 0x1F,
    0x1F, 0x35, 0x35, 0x20, 0x03, 0x05 };
static const uint8_t lcd_pwr_ctl_1[]  = { 0x00 };                          /* B0 接口模式 */
static const uint8_t lcd_frame_mode[] = { 0x00, 0x00 };                    /* B1 帧模式 */
static const uint8_t lcd_inv_ctrl[]   = { 0x11 };                          /* B4 反转控制 */
static const uint8_t lcd_disp_func[]  = { 0x80, 0x02, 0x3B };              /* B6 显示功能 */
static const uint8_t lcd_pump_ctrl[]  = { 0x0A };                          /* E4 泵控制 */
static const uint8_t lcd_intf_ctrl[]  = { 0x00 };                          /* F0 接口控制 */
static const uint8_t lcd_pump_ratio[] = { 0x00, 0x02 };                    /* F3 泵比例 */
static const uint8_t lcd_if_ctrl[]    = { 0x55 };                          /* 3A 16bit */
static const uint8_t lcd_win_x[] = { 0x00, 0x00, 0x01, 0x3F };              /* 2A 列 0~319 */
static const uint8_t lcd_win_y[] = { 0x00, 0x00, 0x01, 0xDF };              /* 2B 行 0~479 */

static const LcdSeq_t lcd_init_seq[] = {
    /* 退出睡眠，手册要求等 120ms */
    { 0x11, 0, 0, 120 },

    /* 电源 / VCOM / 面板驱动，ILI9481 手册推荐值 */
    { 0xD0, 3, lcd_pw_set_1,   0 },
    { 0xD1, 3, lcd_vcom_set,   0 },
    { 0xD2, 2, lcd_pw_set_2,   0 },
    { 0xC0, 5, lcd_panel_drv,  0 },
    { 0xC1, 3, lcd_timing,     0 },
    { 0xC5, 1, lcd_frame_rtn,  0 },
    { 0xC6, 1, lcd_iface_ctrl, 0 },
    { 0xC8, 12, lcd_gamma_ctrl,0 },

    /* 显示方向：取值来自 lcd.h 的 LCD_MADCTL */
    { 0x36, 1, 0,              0 },   /* dat==0 的哨兵：执行时用 LCD_MADCTL 替换 */

    /* 像素格式 = 16bit（RGB565） */
    { 0x3A, 1, lcd_if_ctrl,    0 },

    /* 接口 / 帧 / 反转控制 */
    { 0xB0, 1, lcd_pwr_ctl_1,  0 },
    { 0xB1, 2, lcd_frame_mode, 0 },
    { 0xB4, 1, lcd_inv_ctrl,   0 },
    { 0xB6, 3, lcd_disp_func,  0 },
    { 0xE4, 1, lcd_pump_ctrl,  0 },
    { 0xF0, 1, lcd_intf_ctrl,  0 },
    { 0xF3, 2, lcd_pump_ratio, 0 },

    /* 默认刷新窗口 = 全屏 320x480，与 LCD_WIDTH/HEIGHT 对应 */
    { 0x2A, 4, lcd_win_x,      0 },
    { 0x2B, 4, lcd_win_y,      0 },

    /* 开显示，手册要求等 20ms */
    { 0x29, 0, 0, 20 },

    { LCD_SEQ_END, 0, 0, 0 }        /* 结束哨兵 */
};

/* 执行初始化序列：逐条写命令，delay_ms 非 0 时延时 */
static void lcd_run_seq(void)
{
    const LcdSeq_t *p;

    for (p = lcd_init_seq; p->cmd != LCD_SEQ_END; p++) {
        lcd_write_cmd((uint8_t)p->cmd);

        if (p->len != 0U) {
            if (p->dat == 0) {
                /* MADCTL 特例：用 lcd.h 的宏值 */
                lcd_write_data(LCD_MADCTL);
            } else {
                for (uint8_t i = 0; i < p->len; i++) {
                    lcd_write_data(p->dat[i]);
                }
            }
        }
        if (p->delay_ms != 0U) {
            delay_ms(p->delay_ms);
        }
    }
}


/* 区块 2：基础功能 */
/* 初始化顺序：FSMC 总线 → 控制引脚 → 背光 → 屏上电序列 → 清屏 */
void LCD_Init(void)
{
    lcd_gpio_init();
    lcd_fsmc_init();
    lcd_bl_hw_init();

    LCD_BackLight(1);        /* 先开背光 */
    delay_ms(50);            /* 等屏内部上电稳定 */

    lcd_run_seq();           /* 执行 ILI9481 初始化序列 */

    /* 方向复位到上电默认，MADCTL 已由序列表写入，这里只同步软件状态 */
    lcd_rot = (uint8_t)LCD_ROT_0;
    lcd_w   = LCD_WIDTH;
    lcd_h   = LCD_HEIGHT;

    LCD_Clear(LCD_COLOR_BLACK);   /* 上电清成黑屏，避免雪花噪点 */
}

/* 背光开关（极性自动适配） */
void LCD_BackLight(uint8_t on)
{
#if LCD_BL_ACTIVE_HIGH
    if (on) GPIO_OutSet  (LCD_BL_PORT, LCD_BL_PIN);
    else    GPIO_OutReset(LCD_BL_PORT, LCD_BL_PIN);
#else
    if (on) GPIO_OutReset(LCD_BL_PORT, LCD_BL_PIN);
    else    GPIO_OutSet  (LCD_BL_PORT, LCD_BL_PIN);
#endif
}

/* 交换/截断后设置窗口，并把实际窗口尺寸带回
 * 返回 : 1 = 窗口有效（命令已发送）；0 = 起点在屏外（未发送命令）
 * LCD_SetWindow 与 LCD_FillRect 共用本函数，截断规则只有一份 */
static uint8_t lcd_set_window_ext(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                                  uint16_t *w, uint16_t *h)
{
    uint16_t t;

    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }   /* 容错：交换 */
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }

    if (x1 >= lcd_w)  x1 = (uint16_t)(lcd_w - 1U); /* 截断到屏幕内 */
    if (y1 >= lcd_h) y1 = (uint16_t)(lcd_h - 1U);
    if (x0 >= lcd_w || y0 >= lcd_h) return 0U;  /* 起点都在屏外：直接拒绝 */

    if (w != 0) *w = (uint16_t)(x1 - x0 + 1U);
    if (h != 0) *h = (uint16_t)(y1 - y0 + 1U);

    /* 列地址设置（0x2A）：x0/x1 各 2 字节，高字节在前 */
    lcd_write_cmd(0x2A);
    lcd_write_data((uint16_t)(x0 >> 8)); lcd_write_data((uint16_t)(x0 & 0xFFU));
    lcd_write_data((uint16_t)(x1 >> 8)); lcd_write_data((uint16_t)(x1 & 0xFFU));

    /* 行地址设置（0x2B） */
    lcd_write_cmd(0x2B);
    lcd_write_data((uint16_t)(y0 >> 8)); lcd_write_data((uint16_t)(y0 & 0xFFU));
    lcd_write_data((uint16_t)(y1 >> 8)); lcd_write_data((uint16_t)(y1 & 0xFFU));

    /* 0x2C 写显存：之后的数据按窗口顺序自动填充 */
    lcd_write_cmd(0x2C);

    return 1U;
}

/* 设置写入窗口：坐标自动截断 + 自动交换颠倒的起止点 */
void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    (void)lcd_set_window_ext(x0, y0, x1, y1, 0, 0);
}

/* 全屏清屏：一个窗口内连续写像素 */
void LCD_Clear(uint16_t color)
{
    LCD_FillRect(0, 0, (uint16_t)(lcd_w - 1U), (uint16_t)(lcd_h - 1U), color);
}

/* 画点：把窗口缩到 1×1 再写一个像素 */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= lcd_w || y >= lcd_h) return;

    LCD_SetWindow(x, y, x, y);
    lcd_write_data(color);
}

/* 填充矩形：窗内连续写 w×h 个像素
 * 尺寸由扩展版窗口函数带回，截断规则与 LCD_SetWindow 共用 */
void LCD_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    uint16_t w;
    uint16_t h;
    uint32_t count;

    if (lcd_set_window_ext(x0, y0, x1, y1, &w, &h) == 0U) return;  /* 完全在屏外 */

    count = (uint32_t)w * (uint32_t)h;

    while (count-- != 0U) {
        lcd_write_data(color);
    }
}

/* 画直线（Bresenham）：任意方向、无浮点 */
void LCD_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    int16_t  x  = (int16_t)x0;
    int16_t  y  = (int16_t)y0;
    int16_t  ex = (int16_t)x1;   /* 终点 x */
    int16_t  ey = (int16_t)y1;   /* 终点 y */
    int16_t  dx = (ex > x) ? (int16_t)(ex - x) : (int16_t)(x - ex);
    int16_t  dy = (ey > y) ? (int16_t)(y - ey) : (int16_t)(ey - y);  /* 用负值便于统一比较 */
    int16_t  sx = (x < ex) ? 1 : -1;
    int16_t  sy = (y < ey) ? 1 : -1;
    int16_t  err = (int16_t)(dx + dy);

    for (;;) {
        LCD_DrawPoint((uint16_t)x, (uint16_t)y, color);
        if (x == ex && y == ey) break;

        {
            int16_t e2 = (int16_t)(2 * err);
            if (e2 >= dy) { err = (int16_t)(err + dy); x = (int16_t)(x + sx); }
            if (e2 <= dx) { err = (int16_t)(err + dx); y = (int16_t)(y + sy); }
        }
    }
}


/* 区块 3：扩展功能 */

/* 显示方向切换：写一次 MADCTL(0x36)，对调 lcd_w / lcd_h，再清屏
 * 必须清屏：换 MADCTL 后显存扫描顺序变了，旧内容会错位成花屏
 * 宽高对调规则：ROT_0 / ROT_180 保持面板原生；ROT_90 / ROT_270 互换 */
void LCD_SetRotation(LcdRot_t rot)
{
    if ((uint8_t)rot > 3U) return;          /* 越界保护 */

    lcd_rot = (uint8_t)rot;

    lcd_write_cmd(0x36);                     /* MADCTL：内存访问控制 */
    lcd_write_data((uint16_t)lcd_madctl_tab[lcd_rot]);

    if (lcd_rot == (uint8_t)LCD_ROT_90 || lcd_rot == (uint8_t)LCD_ROT_270) {
        lcd_w = LCD_HEIGHT;                  /* 横屏：宽高互换 */
        lcd_h = LCD_WIDTH;
    } else {
        lcd_w = LCD_WIDTH;                   /* 竖屏：原生尺寸 */
        lcd_h = LCD_HEIGHT;
    }

    LCD_Clear(LCD_COLOR_BLACK);
}

uint16_t LCD_GetWidth(void)     { return lcd_w; }
uint16_t LCD_GetHeight(void)    { return lcd_h; }
uint8_t  LCD_GetRotation(void)  { return lcd_rot; }


/* 读点：用 ILI9481 的 0x2E Memory Read
 * 时序：设 1x1 窗口 → 发 0x2E → 空读一次丢弃 → 真读一次
 * FSMC 异步模式 A 本身支持读，不用额外配读时序 */
uint16_t LCD_ReadPoint(uint16_t x, uint16_t y)
{
    uint16_t c;

    if (x >= lcd_w || y >= lcd_h) return 0U;

    LCD_SetWindow(x, y, x, y);               /* 该函数末尾已发 0x2C */
    lcd_write_cmd(0x2E);                     /* RAMRD：读显存 */
    (void)lcd_read_data();                   /* 第一次读是无效值 */
    c = lcd_read_data();                     /* 第二次才是真数据 */
    return c;
}


/* 空心矩形：四条边，越界由 LCD_DrawPoint 忽略 */
void LCD_DrawRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    uint16_t t;

    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }   /* 容错：交换 */
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }

    LCD_DrawLine(x0, y0, x1, y0, color);        /* 上 */
    LCD_DrawLine(x0, y1, x1, y1, color);        /* 下 */
    LCD_DrawLine(x0, y0, x0, y1, color);        /* 左 */
    LCD_DrawLine(x1, y0, x1, y1, color);        /* 右 */
}

/* 八分对称画 8 个点：画圆只算 1/8 圆弧 */
static void lcd_plot8(int16_t xc, int16_t yc, int16_t x, int16_t y, uint16_t color)
{
    LCD_DrawPoint((uint16_t)(xc + x), (uint16_t)(yc + y), color);
    LCD_DrawPoint((uint16_t)(xc - x), (uint16_t)(yc + y), color);
    LCD_DrawPoint((uint16_t)(xc + x), (uint16_t)(yc - y), color);
    LCD_DrawPoint((uint16_t)(xc - x), (uint16_t)(yc - y), color);
    LCD_DrawPoint((uint16_t)(xc + y), (uint16_t)(yc + x), color);
    LCD_DrawPoint((uint16_t)(xc - y), (uint16_t)(yc + x), color);
    LCD_DrawPoint((uint16_t)(xc + y), (uint16_t)(yc - x), color);
    LCD_DrawPoint((uint16_t)(xc - y), (uint16_t)(yc - x), color);
}

/* 空心圆（Bresenham 中点圆算法；r < 0 直接返回） */
void LCD_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    int16_t x = 0;
    int16_t y = r;
    int16_t d = (int16_t)(1 - r);

    if (r < 0) return;

    while (x <= y) {
        lcd_plot8(x0, y0, x, y, color);
        if (d < 0) {
            d = (int16_t)(d + 2 * x + 3);
        } else {
            d = (int16_t)(d + 2 * (x - y) + 5);
            y--;
        }
        x++;
    }
}

/* 实心圆：逐行求弦宽，用填充矩形画水平条
 * 半宽 dx = sqrt(r^2 - dy^2)，用增量法避免开方与浮点 */
void LCD_FillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    int16_t dy;
    int16_t dx;

    if (r < 0) return;

    dx = r;
    for (dy = 0; dy <= r; dy++) {
        int16_t xa;
        int16_t xb;
        int16_t yy;

        /* 收缩 dx 直到满足 dx² + dy² ≤ r²（整数比较，无浮点） */
        while ((int32_t)dx * dx + (int32_t)dy * dy > (int32_t)r * r) {
            dx--;
        }

        /* 画 dy 与 -dy 两条水平线，dy = 0 时只画一条
         * 必须先把 s16 坐标裁到屏幕内再转 u16：负数转 u16 会变成大数，
         * 被 FillRect 当成屏外丢弃 */
        xa = (int16_t)(x0 - dx);
        xb = (int16_t)(x0 + dx);
        if (xa < 0) xa = 0;
        if (xb >= (int16_t)lcd_w) xb = (int16_t)(lcd_w - 1U);

        if (xa <= xb) {
            yy = (int16_t)(y0 + dy);
            if (yy >= 0 && yy < (int16_t)lcd_h) {
                LCD_FillRect((uint16_t)xa, (uint16_t)yy, (uint16_t)xb, (uint16_t)yy, color);
            }
            if (dy != 0) {
                yy = (int16_t)(y0 - dy);
                if (yy >= 0 && yy < (int16_t)lcd_h) {
                    LCD_FillRect((uint16_t)xa, (uint16_t)yy, (uint16_t)xb, (uint16_t)yy, color);
                }
            }
        }
    }
}


/* 字符 / 字符串 / 数字，8x16 ASCII 点阵，字库见 lcd_font.h
 * 字模：每字符 16 字节 = 16 行，每行 bit7 是最左像素
 *   mode = 1 不叠加：每个像素都写，用 fc 或 bc
 *   mode = 0 叠加  ：只有为 1 的像素写 fc，背景保持原样 */
void LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint16_t fc, uint16_t bc, uint8_t mode)
{
    uint8_t i;
    uint8_t j;
    uint8_t row;
    uint8_t idx;
    uint16_t color;

    /* 字符码转字库下标：范围外的字符按空格画 */
    idx = (uint8_t)ch;
    if (idx < 0x20U || idx > 0x7EU) idx = 0x20U;
    idx = (uint8_t)(idx - 0x20U);

    /* 字格必须完整落在屏内才画，露头的不画
     * LCD_ShowString 已提前做了自动换行 */
    if (x >= lcd_w || y >= lcd_h) return;
    if ((uint32_t)x + LCD_FONT8X16_W  > lcd_w) return;
    if ((uint32_t)y + LCD_FONT8X16_H  > lcd_h) return;

    for (i = 0; i < LCD_FONT8X16_H; i++) {
        row = lcd_font8x16[idx][i];
        for (j = 0; j < LCD_FONT8X16_W; j++) {
            if (row & (uint8_t)(0x80U >> j)) color = fc;
            else {
                if (mode != 0U) color = bc;   /* 不叠加：背景也画 */
                else continue;                /* 叠加：背景跳过 */
            }
            LCD_DrawPoint((uint16_t)(x + j), (uint16_t)(y + i), color);
        }
    }
}

void LCD_ShowString(uint16_t x, uint16_t y, const char *str,
                    uint16_t fc, uint16_t bc, uint8_t mode)
{
    uint16_t xs = x;                              /* 行起点（换行用） */

    if (str == 0) return;

    while (*str != '\0') {
        if (*str == '\n') {                      /* 显式换行 */
            x  = xs;
            y  = (uint16_t)(y + LCD_FONT8X16_H);
            str++;
            continue;
        }

        if ((uint32_t)x + LCD_FONT8X16_W > lcd_w) {   /* 到右边界：自动换行 */
            x  = xs;
            y  = (uint16_t)(y + LCD_FONT8X16_H);
        }
        if ((uint32_t)y + LCD_FONT8X16_H > lcd_h) break;   /* 到底了：停 */

        LCD_ShowChar(x, y, *str, fc, bc, mode);
        x = (uint16_t)(x + LCD_FONT8X16_W);
        str++;
    }
}

void LCD_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                 uint16_t fc, uint16_t bc, uint8_t mode)
{
    uint8_t i;
    uint8_t n;
    uint32_t div = 1U;

    if (len == 0U || len > 10U) len = 10U;        /* 32 位十进制最多 10 位 */

    /* 找最高位对应的权值，定宽左边用空格补齐 */
    for (n = 1U; n < len; n++) div *= 10U;

    for (i = 0U; i < len; i++) {
        uint8_t d = (uint8_t)((num / div) % 10U);
        /* 前导零画空格 */
        LCD_ShowChar((uint16_t)(x + (uint16_t)i * LCD_FONT8X16_W), y,
                     (div > num && i != (uint8_t)(len - 1U)) ? ' ' : (char)('0' + d),
                     fc, bc, mode);
        div /= 10U;
    }
}

/* 定点小数：val 已按 10^frac 放大（12345，frac=2 显示 123.45）
 * 不用浮点：分别输出整数部分、'.'、小数部分 */
void LCD_ShowFixed(uint16_t x, uint16_t y, int32_t val, uint8_t frac, uint8_t int_len,
                   uint16_t fc, uint16_t bc, uint8_t mode)
{
    uint32_t mag;
    uint32_t div;
    uint32_t ip;
    uint32_t fp;
    uint16_t px = x;
    uint8_t  i;

    if (frac > 6U) frac = 6U;                     /* frac 上限 6，超出 32 位精度无意义 */
    if (int_len == 0U) int_len = 1U;

    if (val < 0) {
        LCD_ShowChar(px, y, '-', fc, bc, mode);
        px = (uint16_t)(px + LCD_FONT8X16_W);
        mag = (uint32_t)(-val);
    } else {
        mag = (uint32_t)val;
    }

    div = 1U;
    for (i = 0U; i < frac; i++) div *= 10U;

    ip = mag / div;                               /* 整数部分 */
    fp = mag % div;                               /* 小数部分 */

    /* 整数部分，右对齐定宽 */
    LCD_ShowNum(px, y, ip, int_len, fc, bc, mode);
    px = (uint16_t)(px + (uint16_t)int_len * LCD_FONT8X16_W);

    if (frac != 0U) {
        uint32_t p = div / 10U;               /* 最高位小数的权值 */

        LCD_ShowChar(px, y, '.', fc, bc, mode);
        px = (uint16_t)(px + LCD_FONT8X16_W);

        /* 小数部分定宽补前导 0，0.05 不能显示成 0.5
         * 权值递减：frac=2 时依次取 (fp/10)%10 与 fp%10 */
        for (i = 0U; i < frac; i++) {
            LCD_ShowChar((uint16_t)(px + (uint16_t)i * LCD_FONT8X16_W), y,
                         (char)('0' + (uint8_t)((fp / p) % 10U)), fc, bc, mode);
            p /= 10U;
        }
    }
}
