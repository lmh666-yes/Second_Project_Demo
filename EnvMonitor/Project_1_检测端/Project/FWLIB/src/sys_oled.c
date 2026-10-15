#include "sys_oled.h"
/* 实现层;接口与配套说明见 sys_oled.h */

/* ================================================================
 *  sys_oled.c — OLED 显示模块（SSD1306, I2C1）实现文件
 * ================================================================
 *  实现要点:显存缓冲 + 整帧刷新,ShowXxx/SetPixel 只改 1024 字节内存,由
 *    SYS_OLED_Refresh / RefreshDirty 发往屏(无闪烁,可批量改后一次刷);
 *    初始化 = 命令表循环发送(见 oled_init_seq);内置 8x8 ASCII 点阵为列排
 *    (每列 1 字节,bit0 = 顶端),与页格式显存一致;通信经 sys_i2c(自带超时与错误码)。
 *  显存结构(SSD1306 页格式):
 *    oled_buf[p * 128 + x] = 第 p 页(8 行一群)第 x 列的 8 个像素;
 *    bit0 = 页内第 0 行,bit7 = 第 7 行;8 页 × 128 列 = 1024 字节。
 *  共享资源与临界区:
 *    · oled_buf(1024B):改它的公共函数都持帧锁,否则 Refresh 推帧中途被改,出现半新半旧;
 *    · I2C1 总线(与 24C02/MPU6050 共用):刷屏期间须独占,不让其他器件事务插入。
 *    锁 = SYS_I2C_Lock/SYS_I2C_Unlock 的每总线递归互斥,经 OLED_FRAME_LOCK()/
 *      OLED_FRAME_UNLOCK() 取 s_bus;同任务可重入、可嵌套。
 *    持锁:Init / Clear / Refresh / RefreshDirty / MarkAllDirty / ShowChar / ShowString /
 *      ShowNum / ShowFloat / SetPixel / ShowBitmap;不持锁:ErrCount / LastErr(只读 volatile
 *      计数)、ErrClear(只写统计量)、DisplayOn / Off(1 条命令,sys_i2c 事务级加锁);
 *      内部 static(oled_cmd/oled_cmd1/oled_put_u32)只被持锁函数调用。
 *    刷新一屏 >20ms,临界区内只让出 CPU 等,不关中断;中断内不得调用本模块公共函数
 *      (sys_i2c 会跳过加锁)。上电初始化阶段调度器未启动,加锁为空操作。
 * ================================================================ */


/* 内部状态与数据表 */
static SysI2cId_t s_bus  = SYS_I2C_1;           /* 初始化时的总线 */
static uint8_t    s_addr = SYS_OLED_I2C_ADDR;   /* 器件地址 */

/* 显存缓冲(128x64 ÷ 8 = 1024 字节) */
static uint8_t oled_buf[SYS_OLED_WIDTH * SYS_OLED_PAGES];

/* 脏页位图:bit0~bit7 对应页 0~7,1 = 该页显存已改未上屏;上电初值 0xFF(内容未知)。
 * 置脏:改 oled_buf 的函数;清脏:该页写入屏成功后 */
static uint8_t s_oled_dirty = 0xFFU;

/* 上屏错误统计:累计失败写事务数 + 最近一次 SYS_I2C 错误码,用于判读静默失败
 * (屏未插/地址错/上拉不足时只表现为屏不亮)。见 SYS_OLED_ErrCount()/SYS_OLED_LastErr() */
static volatile uint16_t s_oled_err_cnt  = 0U;   /* 累计失败的写事务数 */
static volatile uint8_t  s_oled_last_err = 0U;   /* 最近一次的 SYS_I2C 错误码 */

/* SSD1306 初始化命令表:arg = OLED_NOARG 表示无参数;换 128x32 屏改 0xA8 多路复用比 */
#define OLED_NOARG  0xFF
typedef struct {
    uint8_t cmd;
    uint8_t arg;
} OledSeq_t;

static const OledSeq_t oled_init_seq[] = {
    { 0xAE, OLED_NOARG },   /* 关显示(配置期间关,配完再开) */
    { 0xD5, 0x80 },         /* 时钟分频/振荡频率:默认推荐值 */
    { 0xA8, 0x3F },         /* 多路复用比 = 64 行(128x32 屏改 0x1F) */
    { 0xD3, 0x00 },         /* 显示偏移 = 0 */
    { 0x40, OLED_NOARG },   /* 显示起始行 = 0 */
    { 0x8D, 0x14 },         /* 电荷泵开;不开则屏无显示 */
    { 0x20, 0x00 },         /* 内存寻址 = 水平模式(整帧流式写用) */
    { 0xA1, OLED_NOARG },   /* 列地址映射:左右翻转(与 0xC8 配合) */
    { 0xC8, OLED_NOARG },   /* 行扫描方向:上下翻转(与 0xA1 配合) */
    { 0xDA, 0x12 },         /* COM 引脚配置(128x64 用 0x12) */
    { 0x81, 0xCF },         /* 对比度:0xCF(0x00~0xFF) */
    { 0xD9, 0xF1 },         /* 预充电周期:推荐值 */
    { 0xDB, 0x40 },         /* VCOMH 电压:推荐值 */
    { 0xA4, OLED_NOARG },   /* 显示跟随 RAM */
    { 0xA6, OLED_NOARG },   /* 正常显示(0xA7 = 反显) */
    { 0x2E, OLED_NOARG },   /* 关滚动 */
    { 0xAF, OLED_NOARG },   /* 开显示 */
};

/* 8x8 ASCII 字库(0x20~0x7E 共 95 字):每字 8 列,每列 1 字节,bit0 = 顶端
 * 来源 font8x8(Daniel Hepper, 公有领域),含 IBM/Marcel Sondaar 公有领域 VGA 点阵;
 * 已转置为列排,换字库保持此格式 */
static const uint8_t oled_font8x8[95][8] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x06,0x5F,0x5F,0x06,0x00,0x00,
    0x00,0x03,0x03,0x00,0x03,0x03,0x00,0x00,
    0x14,0x7F,0x7F,0x14,0x7F,0x7F,0x14,0x00,
    0x24,0x2E,0x6B,0x6B,0x3A,0x12,0x00,0x00,
    0x46,0x66,0x30,0x18,0x0C,0x66,0x62,0x00,
    0x30,0x7A,0x4F,0x5D,0x37,0x7A,0x48,0x00,
    0x04,0x07,0x03,0x00,0x00,0x00,0x00,0x00,
    0x00,0x1C,0x3E,0x63,0x41,0x00,0x00,0x00,
    0x00,0x41,0x63,0x3E,0x1C,0x00,0x00,0x00,
    0x08,0x2A,0x3E,0x1C,0x1C,0x3E,0x2A,0x08,
    0x08,0x08,0x3E,0x3E,0x08,0x08,0x00,0x00,
    0x00,0x80,0xE0,0x60,0x00,0x00,0x00,0x00,
    0x08,0x08,0x08,0x08,0x08,0x08,0x00,0x00,
    0x00,0x00,0x60,0x60,0x00,0x00,0x00,0x00,
    0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00,
    0x3E,0x7F,0x71,0x59,0x4D,0x7F,0x3E,0x00,
    0x40,0x42,0x7F,0x7F,0x40,0x40,0x00,0x00,
    0x62,0x73,0x59,0x49,0x6F,0x66,0x00,0x00,
    0x22,0x63,0x49,0x49,0x7F,0x36,0x00,0x00,
    0x18,0x1C,0x16,0x53,0x7F,0x7F,0x50,0x00,
    0x27,0x67,0x45,0x45,0x7D,0x39,0x00,0x00,
    0x3C,0x7E,0x4B,0x49,0x79,0x30,0x00,0x00,
    0x03,0x03,0x71,0x79,0x0F,0x07,0x00,0x00,
    0x36,0x7F,0x49,0x49,0x7F,0x36,0x00,0x00,
    0x06,0x4F,0x49,0x69,0x3F,0x1E,0x00,0x00,
    0x00,0x00,0x66,0x66,0x00,0x00,0x00,0x00,
    0x00,0x80,0xE6,0x66,0x00,0x00,0x00,0x00,
    0x08,0x1C,0x36,0x63,0x41,0x00,0x00,0x00,
    0x24,0x24,0x24,0x24,0x24,0x24,0x00,0x00,
    0x00,0x41,0x63,0x36,0x1C,0x08,0x00,0x00,
    0x02,0x03,0x51,0x59,0x0F,0x06,0x00,0x00,
    0x3E,0x7F,0x41,0x5D,0x5D,0x1F,0x1E,0x00,
    0x7C,0x7E,0x13,0x13,0x7E,0x7C,0x00,0x00,
    0x41,0x7F,0x7F,0x49,0x49,0x7F,0x36,0x00,
    0x1C,0x3E,0x63,0x41,0x41,0x63,0x22,0x00,
    0x41,0x7F,0x7F,0x41,0x63,0x3E,0x1C,0x00,
    0x41,0x7F,0x7F,0x49,0x5D,0x41,0x63,0x00,
    0x41,0x7F,0x7F,0x49,0x1D,0x01,0x03,0x00,
    0x1C,0x3E,0x63,0x41,0x51,0x73,0x72,0x00,
    0x7F,0x7F,0x08,0x08,0x7F,0x7F,0x00,0x00,
    0x00,0x41,0x7F,0x7F,0x41,0x00,0x00,0x00,
    0x30,0x70,0x40,0x41,0x7F,0x3F,0x01,0x00,
    0x41,0x7F,0x7F,0x08,0x1C,0x77,0x63,0x00,
    0x41,0x7F,0x7F,0x41,0x40,0x60,0x70,0x00,
    0x7F,0x7F,0x0E,0x1C,0x0E,0x7F,0x7F,0x00,
    0x7F,0x7F,0x06,0x0C,0x18,0x7F,0x7F,0x00,
    0x1C,0x3E,0x63,0x41,0x63,0x3E,0x1C,0x00,
    0x41,0x7F,0x7F,0x49,0x09,0x0F,0x06,0x00,
    0x1E,0x3F,0x21,0x71,0x7F,0x5E,0x00,0x00,
    0x41,0x7F,0x7F,0x09,0x19,0x7F,0x66,0x00,
    0x26,0x6F,0x4D,0x59,0x73,0x32,0x00,0x00,
    0x03,0x41,0x7F,0x7F,0x41,0x03,0x00,0x00,
    0x7F,0x7F,0x40,0x40,0x7F,0x7F,0x00,0x00,
    0x1F,0x3F,0x60,0x60,0x3F,0x1F,0x00,0x00,
    0x7F,0x7F,0x30,0x18,0x30,0x7F,0x7F,0x00,
    0x43,0x67,0x3C,0x18,0x3C,0x67,0x43,0x00,
    0x07,0x4F,0x78,0x78,0x4F,0x07,0x00,0x00,
    0x47,0x63,0x71,0x59,0x4D,0x67,0x73,0x00,
    0x00,0x7F,0x7F,0x41,0x41,0x00,0x00,0x00,
    0x01,0x03,0x06,0x0C,0x18,0x30,0x60,0x00,
    0x00,0x41,0x41,0x7F,0x7F,0x00,0x00,0x00,
    0x08,0x0C,0x06,0x03,0x06,0x0C,0x08,0x00,
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
    0x00,0x00,0x03,0x07,0x04,0x00,0x00,0x00,
    0x20,0x74,0x54,0x54,0x3C,0x78,0x40,0x00,
    0x41,0x7F,0x3F,0x48,0x48,0x78,0x30,0x00,
    0x38,0x7C,0x44,0x44,0x6C,0x28,0x00,0x00,
    0x30,0x78,0x48,0x49,0x3F,0x7F,0x40,0x00,
    0x38,0x7C,0x54,0x54,0x5C,0x18,0x00,0x00,
    0x48,0x7E,0x7F,0x49,0x03,0x02,0x00,0x00,
    0x98,0xBC,0xA4,0xA4,0xF8,0x7C,0x04,0x00,
    0x41,0x7F,0x7F,0x08,0x04,0x7C,0x78,0x00,
    0x00,0x44,0x7D,0x7D,0x40,0x00,0x00,0x00,
    0x60,0xE0,0x80,0x80,0xFD,0x7D,0x00,0x00,
    0x41,0x7F,0x7F,0x10,0x38,0x6C,0x44,0x00,
    0x00,0x41,0x7F,0x7F,0x40,0x00,0x00,0x00,
    0x7C,0x7C,0x18,0x38,0x1C,0x7C,0x78,0x00,
    0x7C,0x7C,0x04,0x04,0x7C,0x78,0x00,0x00,
    0x38,0x7C,0x44,0x44,0x7C,0x38,0x00,0x00,
    0x84,0xFC,0xF8,0xA4,0x24,0x3C,0x18,0x00,
    0x18,0x3C,0x24,0xA4,0xF8,0xFC,0x84,0x00,
    0x44,0x7C,0x78,0x4C,0x04,0x1C,0x18,0x00,
    0x48,0x5C,0x54,0x54,0x74,0x24,0x00,0x00,
    0x00,0x04,0x3E,0x7F,0x44,0x24,0x00,0x00,
    0x3C,0x7C,0x40,0x40,0x3C,0x7C,0x40,0x00,
    0x1C,0x3C,0x60,0x60,0x3C,0x1C,0x00,0x00,
    0x3C,0x7C,0x70,0x38,0x70,0x7C,0x3C,0x00,
    0x44,0x6C,0x38,0x10,0x38,0x6C,0x44,0x00,
    0x9C,0xBC,0xA0,0xA0,0xFC,0x7C,0x00,0x00,
    0x4C,0x64,0x74,0x5C,0x4C,0x64,0x00,0x00,
    0x08,0x08,0x3E,0x77,0x41,0x41,0x00,0x00,
    0x00,0x00,0x00,0x77,0x77,0x00,0x00,0x00,
    0x41,0x41,0x77,0x3E,0x08,0x08,0x00,0x00,
    0x02,0x03,0x01,0x03,0x02,0x03,0x01,0x00,
};

/* 控制字节:命令 0x00、数据 0x40(sys_i2c 的寄存器号参数) */
#define OLED_CTRL_CMD   0x00
#define OLED_CTRL_DATA  0x40

/* 帧锁:保护 oled_buf 与 I2C1 总线(见文件头);递归锁,同任务可重入、可嵌套。
 * 刷一屏 >20ms,锁内只让出 CPU 等,不关中断;中断内不调用本模块;上电初始化阶段为空操作。 */
#define OLED_FRAME_LOCK()    SYS_I2C_Lock(s_bus)
#define OLED_FRAME_UNLOCK()  SYS_I2C_Unlock(s_bus)


/* 内部辅助 */
/* 发命令流(len 字节,可含参数)，成功返回 0、失败返回 1 并记账 */
static uint8_t oled_cmd(const uint8_t *buf, uint8_t len)
{
    uint8_t err = SYS_I2C_WriteBytes(s_bus, s_addr, OLED_CTRL_CMD, buf, len);

    if (err != SYS_I2C_OK) {
        s_oled_err_cnt++;
        s_oled_last_err = err;
        return 1U;
    }
    return 0U;
}

/* 发单条无参命令 */
static void oled_cmd1(uint8_t cmd)
{
    (void)oled_cmd(&cmd, 1U);
}

/* 全部页标脏(调用方须已持帧锁) */
static void oled_dirty_all(void)
{
    s_oled_dirty = (uint8_t)((1U << SYS_OLED_PAGES) - 1U);   /* 8 页 = 0xFF */
}

/* 输出无符号数字,返回下一个可用 x */
static uint8_t oled_put_u32(uint8_t page, uint8_t x, uint32_t u)
{
    char buf[10];
    uint8_t n = 0;
    uint8_t i;

    do {
        buf[n++] = (char)('0' + (u % 10U));
        u /= 10U;
    } while (u != 0U && n < 10U);

    for (i = n; i > 0U; i--) {
        SYS_OLED_ShowChar(page, x, buf[i - 1U]);
        x = (uint8_t)(x + SYS_OLED_CHAR_W);
        if (x >= SYS_OLED_WIDTH) break;
    }
    return x;
}


/* 基础功能 */
uint8_t SYS_OLED_Init(SysI2cId_t bus, uint8_t addr)
{
    uint8_t i;
    uint8_t tmp[2];

    if (bus >= SYS_I2C_COUNT) return 1;

    /* 先取帧锁再改写 s_bus/s_addr,使锁与所保护的总线一致;自检、清屏、首次刷屏均在锁内。
     * 调度器未运行时加锁为空操作(见 sys_i2c.h) */
    OLED_FRAME_LOCK();

    s_bus  = bus;
    s_addr = addr;

    /* 总线就绪(OLED 支持 400kHz) + 器件自检 */
    SYS_I2C_Init(bus, 400000);
    if (SYS_I2C_IsDeviceReady(bus, addr) != SYS_I2C_OK) {
        OLED_FRAME_UNLOCK();
        return 1;
    }

    /* 依次发送初始化命令表;单条写失败不中断初始化(屏可能刚上电未就绪),失败计入错误统计 */
    for (i = 0U; i < (uint8_t)(sizeof(oled_init_seq) / sizeof(oled_init_seq[0])); i++) {
        tmp[0] = oled_init_seq[i].cmd;
        if (oled_init_seq[i].arg == OLED_NOARG) {
            (void)oled_cmd(tmp, 1U);
        } else {
            tmp[1] = oled_init_seq[i].arg;
            (void)oled_cmd(tmp, 2U);
        }
    }

    /* 清屏并刷出。首次刷屏失败不计入 Init 失败:器件已应答(IsDeviceReady 通过),
     * 偶发写失败由调用方后续 Refresh 补救 */
    SYS_OLED_Clear();          /* 内部取锁→清 RAM→放锁;本函数已持锁,递归计数 +1 */
    (void)SYS_OLED_Refresh();  /* 内部取锁→整帧上屏→放锁;同上 */
    OLED_FRAME_UNLOCK();
    return 0;
}

void SYS_OLED_Clear(void)
{
    uint16_t i;

    /* 只动 RAM,但须与 Refresh 互斥,否则半清半旧的显存被推上屏;递归锁,同任务可重入 */
    OLED_FRAME_LOCK();
    for (i = 0U; i < (uint16_t)sizeof(oled_buf); i++) {
        oled_buf[i] = 0x00;
    }
    oled_dirty_all();          /* 8 页全变,全标脏 */
    OLED_FRAME_UNLOCK();
}

uint8_t SYS_OLED_Refresh(void)
{
    uint8_t tmp[3];
    uint8_t err;

    OLED_FRAME_LOCK();

    /* 设定整屏窗口:列 0~127、页 0~7(水平寻址模式下数据自动铺满) */
    tmp[0] = 0x21; tmp[1] = 0x00; tmp[2] = SYS_OLED_WIDTH - 1U;
    (void)oled_cmd(tmp, 3U);
    tmp[0] = 0x22; tmp[1] = 0x00; tmp[2] = SYS_OLED_PAGES - 1U;
    (void)oled_cmd(tmp, 3U);

    /* 整帧 1024 字节一次发出:@400kHz 约 23~25ms(不含重试),失败还要等 I2C 超时
     * (SYS_I2C_TIMEOUT = 200000 次循环,最坏数十 ms)。不可用于 1ms 定时器中断;调用方看返回值降级 */
    err = SYS_I2C_WriteBytes(s_bus, s_addr, OLED_CTRL_DATA, oled_buf,
                             (uint16_t)sizeof(oled_buf));
    if (err != SYS_I2C_OK) {
        s_oled_err_cnt++;
        s_oled_last_err = err;
        OLED_FRAME_UNLOCK();
        return 1U;
    }
    s_oled_dirty = 0x00U;      /* 8 页已上屏,脏位全清(失败时保留,待下次重刷) */
    OLED_FRAME_UNLOCK();
    return 0U;
}

/* 局部刷新:只把脏页推上屏(每页 128 字节),全干净时不碰总线;
 * SYS_OLED_Refresh 则不分脏净,整屏 1024 字节一次发完。
 * 脏位由 Clear / ShowChar / ShowString / ShowNum / ShowFloat / SetPixel / ShowBitmap 置位。
 * 返回:实际写入的页数(0 = 全干净;1~8);未写成功的页保留脏位,下次重试 */
uint8_t SYS_OLED_RefreshDirty(void)
{
    uint8_t tmp[3];
    uint8_t p;
    uint8_t cnt = 0U;

    OLED_FRAME_LOCK();

    if (s_oled_dirty == 0x00U) {           /* 全干净:不碰总线 */
        OLED_FRAME_UNLOCK();
        return 0U;
    }

    for (p = 0U; p < (uint8_t)SYS_OLED_PAGES; p++) {
        uint8_t bit = (uint8_t)(1U << p);
        uint8_t ok  = 1U;
        uint8_t err;

        if ((s_oled_dirty & bit) == 0x00U) continue;   /* 该页未变,跳过 */

        /* 窗口限定为这一页:列 0~127、页 p~p(水平寻址下不会溢到别页) */
        tmp[0] = 0x21; tmp[1] = 0x00; tmp[2] = SYS_OLED_WIDTH - 1U;
        if (oled_cmd(tmp, 3U) != 0U) ok = 0U;
        tmp[0] = 0x22; tmp[1] = p;    tmp[2] = p;
        if (oled_cmd(tmp, 3U) != 0U) ok = 0U;

        /* 这一页的 128 字节 */
        err = SYS_I2C_WriteBytes(s_bus, s_addr, OLED_CTRL_DATA,
                                 &oled_buf[(uint16_t)p * SYS_OLED_WIDTH],
                                 SYS_OLED_WIDTH);
        if (err != SYS_I2C_OK) {
            s_oled_err_cnt++;
            s_oled_last_err = err;
            ok = 0U;
        }

        if (ok != 0U) {                    /* 真写进去了,才清这一页的脏位 */
            s_oled_dirty &= (uint8_t)~bit;
            cnt++;
        }
    }

    OLED_FRAME_UNLOCK();
    return cnt;
}

/* 手动把 8 页全标脏(整屏重刷一遍);用于外部绕过本模块改过屏内容的场合。
 * 正常流程无需调用:Clear 自动全标脏,Refresh 成功后脏位自行清空 */
void SYS_OLED_MarkAllDirty(void)
{
    OLED_FRAME_LOCK();
    oled_dirty_all();
    OLED_FRAME_UNLOCK();
}

/* 累计上屏失败次数,0 = 自上次清零以来全部成功。屏不亮时:计数在涨 = 通信问题
 * (接线/上拉/地址/总线被占);恒 0 则查屏与供电 */
uint16_t SYS_OLED_ErrCount(void)
{
    return s_oled_err_cnt;
}

/* 最近一次失败的 SYS_I2C 错误码（0 = 从未失败；见 sys_i2c.h 的 SYS_I2C_ERR_*） */
uint8_t SYS_OLED_LastErr(void)
{
    return s_oled_last_err;
}

/* 清零错误统计 */
void SYS_OLED_ErrClear(void)
{
    s_oled_err_cnt  = 0U;
    s_oled_last_err = 0U;
}

void SYS_OLED_ShowChar(uint8_t page, uint8_t x, char ch)
{
    const uint8_t *glyph;
    uint8_t        c;
    uint8_t        i;

    OLED_FRAME_LOCK();
    if (page >= SYS_OLED_PAGES) { OLED_FRAME_UNLOCK(); return; }

    c = (uint8_t)ch;
    if (c < 0x20U || c > 0x7EU) c = (uint8_t)' ';       /* 字库外显示空格 */
    glyph = oled_font8x8[c - 0x20U];

    for (i = 0U; i < SYS_OLED_CHAR_W; i++) {
        if ((uint16_t)x + i >= SYS_OLED_WIDTH) break;
        oled_buf[(uint16_t)page * SYS_OLED_WIDTH + x + i] = glyph[i];
    }
    if (i > 0U) {                          /* 至少写了一列,该页标脏(整串字符经此路径) */
        s_oled_dirty |= (uint8_t)(1U << page);
    }
    OLED_FRAME_UNLOCK();
}

void SYS_OLED_ShowString(uint8_t page, uint8_t x, const char *str)
{
    if (str == 0) return;

    /* 整串放同一临界区,避免半新半旧的撕裂画面(内部 ShowChar 会再取一次锁) */
    OLED_FRAME_LOCK();
    while (*str != '\0') {
        if (page >= SYS_OLED_PAGES) break;
        if (x + SYS_OLED_CHAR_W > SYS_OLED_WIDTH) break;    /* 到右边界停 */
        SYS_OLED_ShowChar(page, x, *str);
        x = (uint8_t)(x + SYS_OLED_CHAR_W);
        if (x >= SYS_OLED_WIDTH) break;
        str++;
    }
    OLED_FRAME_UNLOCK();
}


/* 扩展功能 */
void SYS_OLED_ShowNum(uint8_t page, uint8_t x, int32_t num)
{
    OLED_FRAME_LOCK();
    if (page >= SYS_OLED_PAGES) { OLED_FRAME_UNLOCK(); return; }

    if (num < 0) {
        SYS_OLED_ShowChar(page, x, '-');
        x = (uint8_t)(x + SYS_OLED_CHAR_W);
        /* 0u - 负数 = 绝对值(对 INT32_MIN 无溢出) */
        (void)oled_put_u32(page, x, 0U - (uint32_t)num);
    } else {
        (void)oled_put_u32(page, x, (uint32_t)num);
    }
    OLED_FRAME_UNLOCK();
}

void SYS_OLED_ShowFloat(uint8_t page, uint8_t x, float value, uint8_t dec)
{
    int32_t  ip;
    uint32_t frac = 0U;
    uint32_t scale = 1U;
    uint8_t  i;
    uint8_t  neg = 0U;
    float    v;

    OLED_FRAME_LOCK();
    if (page >= SYS_OLED_PAGES) { OLED_FRAME_UNLOCK(); return; }
    if (dec > 3U) dec = 3U;

    v = value;
    if (v < 0.0f) { neg = 1U; v = -v; }

    /* 整数部分截断;小数部分四舍五入,进位回卷到整数 */
    ip = (int32_t)v;
    for (i = 0U; i < dec; i++) scale *= 10U;
    if (dec > 0U) {
        float f = (v - (float)ip) * (float)scale;
        frac = (uint32_t)(f + 0.5f);
        if (frac >= scale) { frac -= scale; ip++; }
    }

    if (neg) {
        SYS_OLED_ShowChar(page, x, '-');
        x = (uint8_t)(x + SYS_OLED_CHAR_W);
    }
    x = oled_put_u32(page, x, (uint32_t)ip);

    if (dec > 0U) {
        /* 小数点 + 固定 dec 位小数(补零) */
        char buf[3];
        SYS_OLED_ShowChar(page, x, '.');
        x = (uint8_t)(x + SYS_OLED_CHAR_W);
        for (i = dec; i > 0U; i--) {
            buf[i - 1U] = (char)('0' + (frac % 10U));
            frac /= 10U;
        }
        for (i = 0U; i < dec; i++) {
            SYS_OLED_ShowChar(page, x, buf[i]);
            x = (uint8_t)(x + SYS_OLED_CHAR_W);
            if (x >= SYS_OLED_WIDTH) break;
        }
    }
    OLED_FRAME_UNLOCK();
}

void SYS_OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on)
{
    uint16_t idx;

    OLED_FRAME_LOCK();
    if (x >= SYS_OLED_WIDTH || y >= SYS_OLED_HEIGHT) { OLED_FRAME_UNLOCK(); return; }

    idx = (uint16_t)(y / 8U) * SYS_OLED_WIDTH + x;
    if (on) oled_buf[idx] |= (uint8_t)(1U << (y % 8U));
    else    oled_buf[idx] &= (uint8_t)~(1U << (y % 8U));
    s_oled_dirty |= (uint8_t)(1U << (uint8_t)(y / 8U));    /* 点所在的那一页标脏 */
    OLED_FRAME_UNLOCK();
}

void SYS_OLED_ShowBitmap(uint8_t page, uint8_t x, const uint8_t *bmp,
                         uint8_t w, uint8_t h)
{
    uint8_t py, cx;

    OLED_FRAME_LOCK();
    if (bmp == 0 || w == 0U || h == 0U) { OLED_FRAME_UNLOCK(); return; }

    for (py = 0U; py < (uint8_t)(h / 8U); py++) {
        if ((uint16_t)page + py >= SYS_OLED_PAGES) break;
        for (cx = 0U; cx < w; cx++) {
            if ((uint16_t)x + cx >= SYS_OLED_WIDTH) break;
            oled_buf[(uint16_t)(page + py) * SYS_OLED_WIDTH + x + cx] =
                bmp[(uint16_t)py * w + cx];
        }
        if (cx > 0U) {                     /* 这一页写了至少一列,标脏 */
            s_oled_dirty |= (uint8_t)(1U << (uint8_t)(page + py));
        }
    }
    OLED_FRAME_UNLOCK();
}

void SYS_OLED_DisplayOn(void)
{
    oled_cmd1(0xAF);
}

void SYS_OLED_DisplayOff(void)
{
    oled_cmd1(0xAE);
}
