#include "lcd.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"
#include "delay.h"      /* 延时（delay_ms 等）独立文件 */

/* ================================================================
 *  lcd.c —— 【板载】TFT-LCD 显示屏模块（FSMC + ILI9341）  实现文件
 * ================================================================
 *  实现要点 :
 *    ① 总线用"寄存器直写"配置 FSMC（不依赖 FSMC 标准库组件，
 *       整个模块只用到 GPIO/RCC，换成任意 F4 都照常工作）；
 *    ② 数据线为 F4 固定映射（见下表），控制线/背光走 lcd.h 宏；
 *    ③ ILI9341 初始化用"表驱动"：一条命令一行，改屏/调参直观。
 *
 *  调屏速查（不亮 / 花屏时）:
 *      白屏不亮      → 检查背光引脚/极性、LCD_Init 是否调用;
 *      全黑          → 背光没开、或 0x29 未执行（序列被卡）;
 *      花屏/乱码色块 → 先调大 LCD_FSMC_* 三个时序宏;
 *      颜色红蓝互换  → 改 LCD_MADCTL 的 BGR 位;
 *      镜像/方向不对 → 调 LCD_MADCTL 的 MX/MY/MV 位;
 *      完全没反应    → 核对 RS 接的是哪根地址线（改 LCD_CMD/DATA_ADDR）
 * ================================================================ */


/* ================================================================
 *                 FSMC 数据线表（F4 固定映射，一般不用改）
 * ================================================================
 * STM32F4 全系的 FSMC_D0 ~ D15 都有固定引脚（改不了），
 * 换板子时保持原样即可；若你板子的接线确实不同（重映射硬件），
 * 改这里的 {端口, 引脚} 即可 */
/* 表结构说明 : 每行 = 一条数据线的"端口 + 引脚掩码";行号 = D0~D15 */
typedef struct {
    GPIO_TypeDef *port;   /* 数据线端口 */
    uint16_t      pin;    /* 数据线引脚掩码 */
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
    { GPIOD, GPIO_Pin_8  },   /* D12 */
    { GPIOD, GPIO_Pin_9  },   /* D13 */
    { GPIOD, GPIO_Pin_10 },   /* D14 */
    { GPIOE, GPIO_Pin_15 },   /* D15 */
};


/* ================================================================
 *                     命令 / 数据 访问接口
 * ================================================================
 * RS 引脚接在 FSMC 地址线上（本板 A6）：
 *   写 LCD_CMD_ADDR → RS=0 → 屏收到"命令"
 *   写 LCD_DATA_ADDR → RS=1 → 屏收到"数据" */
#define LCD_REG   (*(volatile uint16_t *)LCD_CMD_ADDR)
#define LCD_RAM   (*(volatile uint16_t *)LCD_DATA_ADDR)

/* 写命令 / 写数据（基础版只用写，读时序未配置） */
static void lcd_write_cmd(uint8_t cmd)   { LCD_REG  = (uint16_t)cmd; }
static void lcd_write_data(uint16_t dat) { LCD_RAM  = dat; }


/* ================================================================
 *                     内部辅助
 * ================================================================ */
/* 引脚掩码 → 引脚序号：统一走 gpio_core 的 GPIO_PinSource（不再重复实现） */

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

    /* ① 数据线 D0 ~ D15 */
    for (i = 0; i < 16U; i++) {
        gi.GPIO_Pin = lcd_data_pins[i].pin;
        GPIO_PinAFConfig(lcd_data_pins[i].port,
                         GPIO_PinSource(lcd_data_pins[i].pin), GPIO_AF_FSMC);
        GPIO_Init(lcd_data_pins[i].port, &gi);
    }

    /* ② 控制线：CS=PG12(NE4)  RS=PF12(A6)  WR=PD5(NWE)  RD=PD4(NOE) */
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

/* FSMC 寄存器配置（Bank1 · NE4 · 16 位 SRAM · 异步模式 A）
 * 说明 : 直接写 BCR4/BTR4（BTCR[6]/BTCR[7]），不依赖标准库 FSMC 组件 */
static void lcd_fsmc_init(void)
{
    uint32_t bcr = 0;
    uint32_t btr = 0;

    /* ---- BCR4（存储器块控制）----
     *   bit0   MBKEN  = 1        存储器块使能
     *   bit3:2 MTYP   = 00       SRAM 类型
     *   bit5:4 MWID   = 01       16 位数据总线（01=16bit！）
     *   bit12  WREN   = 1        写使能
     *   其余位保持 0（异步模式 A、无等待、无突发） */
    bcr |= (1UL << 0);
    bcr |= (1UL << 4);
    bcr |= (1UL << 12);
    FSMC_Bank1->BTCR[6] = bcr;

    /* ---- BTR4（时序）----
     *   各字段编码 = 实际周期数 - 1
     *   ADDSET[3:0]  地址建立   ADDHLD[7:4] 地址保持
     *   DATAST[15:8] 数据建立   BUSTURN[19:16] 总线回转(先给 1) */
    btr |= (((uint32_t)LCD_FSMC_ADDR_SETUP - 1UL) & 0xFUL) << 0;
    btr |= (((uint32_t)LCD_FSMC_ADDR_HOLD  - 1UL) & 0xFUL) << 4;
    btr |= (((uint32_t)LCD_FSMC_DATA_SETUP - 1UL) & 0xFFUL) << 8;
    btr |= (1UL & 0xFUL) << 16;
    FSMC_Bank1->BTCR[7] = btr;
}

/* 背光引脚初始化（普通推挽输出 GPIO_OType_PP，不额外开灯——由 LCD_BackLight 控制） */
static void lcd_bl_hw_init(void)
{
    GPIO_OutInit(LCD_BL_PORT, LCD_BL_PIN);
}


/* ================================================================
 *              ILI9341 初始化序列表（表驱动，改屏最直观）
 * ================================================================
 * 每行 = 一条命令：cmd 命令码 / len 数据长度 / dat 数据 / delay_ms 延时
 * 换屏（如 ILI9486）时整体替换本表即可；
 * 数据均取自公开常用序列，若色彩/对比不满意可从 0xC0/0xE0/0xE1
 * 几条开始微调（不影响"能点亮"） */

#define LCD_SEQ_END   0xFFFFU    /* 序列表结束哨兵（cmd 字段填它即结束） */

/* 表结构说明（初始化序列表的每一行 = 一条 LCD 命令）:
 *   cmd      = 命令码（0xXX）;len = 参数个数;
 *   dat      = 参数数组指针（无参数填 0）;delay_ms = 发完后等多少毫秒
 * 结束标记 : cmd 填 LCD_SEQ_END 即结束（不用单独写行数） */
typedef struct {
    uint16_t      cmd;        /* 命令码（LCD_SEQ_END = 结束） */
    uint8_t       len;        /* 参数个数（0~255） */
    const uint8_t *dat;       /* 参数数组（无参数填 0） */
    uint8_t       delay_ms;   /* 命令后延时（毫秒;0 = 不等） */
} LcdSeq_t;

/* —— 各条命令的数据（原样照抄数据手册典型值）—— */
static const uint8_t lcd_pw_ctrl_b[]  = { 0x00, 0xC1, 0x30 };             /* CF */
static const uint8_t lcd_pw_on_seq[]  = { 0x64, 0x03, 0x12, 0x81 };       /* ED */
static const uint8_t lcd_drv_tim_a[]  = { 0x85, 0x00, 0x78 };             /* E8 */
static const uint8_t lcd_pw_ctrl_a[]  = { 0x39, 0x2C, 0x00, 0x34, 0x02 }; /* CB */
static const uint8_t lcd_pump_ratio[] = { 0x20 };                          /* F7 */
static const uint8_t lcd_drv_tim_c[]  = { 0x00, 0x00 };                    /* EA */
static const uint8_t lcd_vcom_1[]     = { 0x23 };                          /* C0 */
static const uint8_t lcd_vcom_2[]     = { 0x10 };                          /* C1 */
static const uint8_t lcd_vcom_reg[]   = { 0x3E, 0x28 };                    /* C5 */
static const uint8_t lcd_vcom_3[]     = { 0x86 };                          /* C7 */
static const uint8_t lcd_if_ctrl[]    = { 0x55 };                          /* 3A 16bit */
static const uint8_t lcd_frame_rtn[]  = { 0x00, 0x18 };                    /* B1 */
static const uint8_t lcd_func_ctrl[]  = { 0x08, 0x82, 0x27 };              /* B6 */
static const uint8_t lcd_gam_dis[]    = { 0x00 };                          /* F2 */
static const uint8_t lcd_gam_set[]    = { 0x01 };                          /* 26 */
static const uint8_t lcd_gamma_pos[]  = {                                   /* E0 */
    0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x06, 0x4E, 0xF9,
    0x39, 0x41, 0x0F, 0x2C, 0x1A, 0x17, 0x18 };
static const uint8_t lcd_gamma_neg[]  = {                                   /* E1 */
    0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
    0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F };

static const LcdSeq_t lcd_init_seq[] = {
    /* 电源与驱动（ILI9341 常用初始化序列） */
    { 0xCF, 3, lcd_pw_ctrl_b,  0 },
    { 0xED, 4, lcd_pw_on_seq,  0 },
    { 0xE8, 3, lcd_drv_tim_a,  0 },
    { 0xCB, 5, lcd_pw_ctrl_a,  0 },
    { 0xF7, 1, lcd_pump_ratio, 0 },
    { 0xEA, 2, lcd_drv_tim_c,  0 },
    { 0xC0, 1, lcd_vcom_1,     0 },
    { 0xC1, 1, lcd_vcom_2,     0 },
    { 0xC5, 2, lcd_vcom_reg,   0 },
    { 0xC7, 1, lcd_vcom_3,     0 },

    /* 显示方向（改 lcd.h 的 LCD_MADCTL，不动这里） */
    { 0x36, 1, 0,              0 },   /* dat==0 的哨兵：执行时用 LCD_MADCTL 替换 */

    /* 像素格式 = 16bit（RGB565） */
    { 0x3A, 1, lcd_if_ctrl,    0 },

    /* 帧率 / 初始化功能控制 */
    { 0xB1, 2, lcd_frame_rtn,  0 },
    { 0xB6, 3, lcd_func_ctrl,  0 },
    { 0xF2, 1, lcd_gam_dis,    0 },
    { 0x26, 1, lcd_gam_set,    0 },

    /* 伽马（不满意可注释掉这两条，改用屏内默认伽马） */
    { 0xE0, 15, lcd_gamma_pos, 0 },
    { 0xE1, 15, lcd_gamma_neg, 0 },

    /* 退出睡眠 → 等 120ms；开显示 → 等 20ms */
    { 0x11, 0, 0, 120 },
    { 0x29, 0, 0,  20 },

    { LCD_SEQ_END, 0, 0, 0 }        /* 结束哨兵 */
};

/* 执行初始化序列（表驱动 + 延时字段） */
static void lcd_run_seq(void)
{
    const LcdSeq_t *p;

    for (p = lcd_init_seq; p->cmd != LCD_SEQ_END; p++) {
        lcd_write_cmd((uint8_t)p->cmd);

        if (p->len != 0U) {
            if (p->dat == 0) {
                /* MADCTL 特例：用 lcd.h 的宏值（可随时改方向/配色） */
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


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化顺序：FSMC 总线 → 控制引脚 → 背光 → 屏上电序列 → 清屏 */
void LCD_Init(void)
{
    lcd_gpio_init();
    lcd_fsmc_init();
    lcd_bl_hw_init();

    LCD_BackLight(1);        /* 先开背光（不亮时至少能看到"白屏"） */
    delay_ms(50);            /* 等屏内部上电稳定 */

    lcd_run_seq();           /* ILI9341 初始化序列（含 2 处必需延时） */

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

/* 内部扩展版：交换/截断后设置窗口，并把"实际窗口尺寸"带回
 * 返回 : 1 = 窗口有效（命令已发送）；0 = 起点在屏外（未发送任何命令）
 * 说明 : LCD_SetWindow 与 LCD_FillRect 共用本函数——截断规则只有一份 */
static uint8_t lcd_set_window_ext(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                                  uint16_t *w, uint16_t *h)
{
    uint16_t t;

    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }   /* 容错：交换 */
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }

    if (x1 >= LCD_WIDTH)  x1 = LCD_WIDTH  - 1U; /* 截断到屏幕内 */
    if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1U;
    if (x0 >= LCD_WIDTH || y0 >= LCD_HEIGHT) return 0U;  /* 起点都在屏外：直接拒绝 */

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

    /* 进入"准备写显存"（0x2C）：之后的数据按窗口顺序自动填充 */
    lcd_write_cmd(0x2C);

    return 1U;
}

/* 设置写入窗口：坐标自动截断 + 自动交换颠倒的起止点 */
void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    (void)lcd_set_window_ext(x0, y0, x1, y1, 0, 0);
}

/* 全屏清屏：一个窗口 + 连续写像素（比逐点快得多） */
void LCD_Clear(uint16_t color)
{
    LCD_FillRect(0, 0, LCD_WIDTH - 1U, LCD_HEIGHT - 1U, color);
}

/* 画点：把窗口缩到 1×1 再写一个像素 */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;

    LCD_SetWindow(x, y, x, y);
    lcd_write_data(color);
}

/* 填充矩形：窗内连续写 w×h 个像素（尺寸由扩展版窗口函数带回，
 * 截断规则与 LCD_SetWindow 共用——不存在"两份规则"） */
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
