#include "sys_i2c.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"

/* ================================================================
 *  sys_i2c.c —— 【系统】I2C 总线模块  实现文件
 * ================================================================
 *  实现路线 : 直接用 I2C 寄存器 + 标准库初始化结构体（I2C_Init
 *             内部会按当前 PCLK1 自动算 CCR/TRISE 分频）
 *
 *  每个公共函数的三段式结构:
 *      ① 起条件 / 发地址 —— 失败 → 总线忙或器件无应答(ERR_ADDR)
 *      ② 传数据         —— 失败 → 数据阶段无应答(ERR_DATA)
 *      ③ 等结束 / 停    —— 失败 → 超时(ERR_TIMEOUT，总线可能被拉死)
 *  哪一步失败就报哪个错，配合 SYS_I2C_ErrStr 能直接定位问题。
 *
 *  说明 : 本文件不含中断，全部为"查询标志 + 循环等待"的阻塞实现，
 *         简单直观；最长等待时间由 SYS_I2C_TIMEOUT 封顶。
 * ================================================================ */


/* ================================================================
 *                    内部配置表
 * ================================================================ */
/* 用途 : 总线编号（= SYS_I2C_x 枚举顺序 = 数组下标）→ 硬件资源映射
 * 字段 : i2c   = 外设指针（I2C1~I2C3）
 *        scl/sda = 引脚端口 + 掩码（引脚宏在 sys_i2c.h 区块 1）
 *        af    = 引脚复用功能号（I2C1/2/3 均为 GPIO_AF_I2Cx）
 *        clk   = 对应 RCC_APB1Periph_I2Cx 时钟位
 * 何时改 : 换引脚只改头文件宏;增删总线才动本表（护栏核对项数） */
typedef struct {
    I2C_TypeDef  *i2c;      /* 外设指针 */
    GPIO_TypeDef *scl_port; /* SCL 端口 */
    uint16_t      scl_pin;  /* SCL 引脚掩码 */
    GPIO_TypeDef *sda_port; /* SDA 端口 */
    uint16_t      sda_pin;  /* SDA 引脚掩码 */
    uint8_t       af;       /* 引脚复用功能号（I2C1/2/3 均为 GPIO_AF_I2Cx） */
    uint32_t      clk;      /* RCC_APB1Periph_I2Cx */
} I2cCfg_t;

static const I2cCfg_t i2c_cfg[SYS_I2C_COUNT] = {
    { I2C1, SYS_I2C1_SCL_PORT, SYS_I2C1_SCL_PIN, SYS_I2C1_SDA_PORT, SYS_I2C1_SDA_PIN, GPIO_AF_I2C1, RCC_APB1Periph_I2C1 },
    { I2C2, SYS_I2C2_SCL_PORT, SYS_I2C2_SCL_PIN, SYS_I2C2_SDA_PORT, SYS_I2C2_SDA_PIN, GPIO_AF_I2C2, RCC_APB1Periph_I2C2 },
    { I2C3, SYS_I2C3_SCL_PORT, SYS_I2C3_SCL_PIN, SYS_I2C3_SDA_PORT, SYS_I2C3_SDA_PIN, GPIO_AF_I2C3, RCC_APB1Periph_I2C3 },
};

/* 编译期护栏：配置表项数必须与 SYS_I2C_COUNT 一致 */
typedef char i2c_cfg_count_check[(sizeof(i2c_cfg) / sizeof(i2c_cfg[0]) == SYS_I2C_COUNT) ? 1 : -1];

/* 记录每路最近一次使用的速率：SYS_I2C_BusReset 恢复时要重新初始化 */
static uint32_t i2c_speed[SYS_I2C_COUNT];


/* ================================================================
 *                    内部辅助
 * ================================================================ */
/* 引脚掩码 → 引脚序号：统一走 gpio_core 的 GPIO_PinSource（不再重复实现） */

/* 起始条件：先等总线空闲，再发 START，等 SB 置位
 * 失败含义 : 总线一直忙(被拉死/无上拉) 或 起始后无 SB → ERR_START */
static int i2c_start(I2C_TypeDef *I)
{
    uint32_t to = SYS_I2C_TIMEOUT;

    while ((I->SR2 & I2C_SR2_BUSY) != 0U) {         /* 等总线空闲 */
        if (to-- == 0U) return SYS_I2C_ERR_START;
    }

    I->CR1 |= I2C_CR1_START;

    to = SYS_I2C_TIMEOUT;
    while ((I->SR1 & I2C_SR1_SB) == 0U) {
        if (to-- == 0U) return SYS_I2C_ERR_START;
        if ((I->SR1 & I2C_SR1_AF) != 0U) {          /* 异常应答保护 */
            I->SR1 &= (uint16_t)~I2C_SR1_AF;
            return SYS_I2C_ERR_START;
        }
    }
    return SYS_I2C_OK;
}

/* 发送 7 位地址 + 读写位，等 ADDR；AF 置位 = 无应答（重点错误）
 * read = 0 → 写方向(地址+0)；read = 1 → 读方向(地址+1) */
static int i2c_send_addr(I2C_TypeDef *I, uint8_t addr7, uint8_t read)
{
    uint32_t to = SYS_I2C_TIMEOUT;

    I->DR = (uint16_t)(((uint16_t)addr7 << 1) | (read ? 1U : 0U));

    while ((I->SR1 & I2C_SR1_ADDR) == 0U) {
        if (to-- == 0U) return SYS_I2C_ERR_TIMEOUT;
        if ((I->SR1 & I2C_SR1_AF) != 0U) {
            I->SR1 &= (uint16_t)~I2C_SR1_AF;        /* 清 AF，好发 STOP */
            return SYS_I2C_ERR_ADDR;                /* ← 无应答 */
        }
    }

    (void)I->SR1;                                   /* 读 SR1 + SR2 */
    (void)I->SR2;                                   /* 清 ADDR 标志 */
    return SYS_I2C_OK;
}

/* 写一个字节：等 TXE（数据寄存器空）再写 */
static int i2c_write_raw(I2C_TypeDef *I, uint8_t b)
{
    uint32_t to = SYS_I2C_TIMEOUT;

    while ((I->SR1 & I2C_SR1_TXE) == 0U) {
        if (to-- == 0U) return SYS_I2C_ERR_TIMEOUT;
        if ((I->SR1 & I2C_SR1_AF) != 0U) {
            I->SR1 &= (uint16_t)~I2C_SR1_AF;
            return SYS_I2C_ERR_DATA;
        }
    }
    I->DR = b;
    return SYS_I2C_OK;
}

/* 等 BTF（一个字节真正发完）——写周期收尾、读周期换方向前用 */
static int i2c_wait_btf(I2C_TypeDef *I)
{
    uint32_t to = SYS_I2C_TIMEOUT;

    while ((I->SR1 & I2C_SR1_BTF) == 0U) {
        if (to-- == 0U) return SYS_I2C_ERR_TIMEOUT;
        if ((I->SR1 & I2C_SR1_AF) != 0U) {
            I->SR1 &= (uint16_t)~I2C_SR1_AF;
            return SYS_I2C_ERR_DATA;
        }
    }
    return SYS_I2C_OK;
}

/* 等 RXNE（收到一个字节）再读走 */
static int i2c_read_raw(I2C_TypeDef *I, uint8_t *out)
{
    uint32_t to = SYS_I2C_TIMEOUT;

    while ((I->SR1 & I2C_SR1_RXNE) == 0U) {
        if (to-- == 0U) return SYS_I2C_ERR_TIMEOUT;
    }
    *out = (uint8_t)I->DR;
    return SYS_I2C_OK;
}

/* 发 STOP（不做等待，硬件自己完成收尾） */
static void i2c_stop(I2C_TypeDef *I)
{
    I->CR1 |= I2C_CR1_STOP;
}

/* 参数检查：id 合法则返回配置项指针，否则返回 0 */
static const I2cCfg_t *i2c_get(SysI2cId_t id)
{
    if (id >= SYS_I2C_COUNT) return 0;
    return &i2c_cfg[id];
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 初始化：时钟 → 引脚(复用开漏 GPIO_OType_OD + 上拉 GPIO_PuPd_UP) → 外设参数（速率） */
void SYS_I2C_Init(SysI2cId_t id, uint32_t speed)
{
    const I2cCfg_t *p = i2c_get(id);
    I2C_InitTypeDef ii;
    GPIO_InitTypeDef gi;

    if (p == 0) return;
    if (speed == 0U) speed = 100000U;

    i2c_speed[id] = speed;

    /* ① 时钟：I2C 外设（挂 APB1）+ 两个引脚端口 */
    RCC_APB1PeriphClockCmd(p->clk, ENABLE);
    GPIO_ClockEnable(p->scl_port);
    GPIO_ClockEnable(p->sda_port);

    /* ② 引脚：复用功能(GPIO_Mode_AF) + 开漏输出(GPIO_OType_OD) + 内部上拉(GPIO_PuPd_UP)
     *    （开漏 + 上拉 = I2C 总线的"线与"电气形态；板上另有
     *      外部 4.7k 上拉，双重保险。不要改成推挽！） */
    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_OD;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;

    gi.GPIO_Pin = p->scl_pin;
    GPIO_PinAFConfig(p->scl_port, GPIO_PinSource(p->scl_pin), p->af);
    GPIO_Init(p->scl_port, &gi);

    gi.GPIO_Pin = p->sda_pin;
    GPIO_PinAFConfig(p->sda_port, GPIO_PinSource(p->sda_pin), p->af);
    GPIO_Init(p->sda_port, &gi);

    /* ③ 外设参数：速率交给标准库自动换算分频 */
    I2C_DeInit(p->i2c);
    I2C_StructInit(&ii);
    ii.I2C_ClockSpeed          = speed;
    ii.I2C_Mode                = I2C_Mode_I2C;
    ii.I2C_DutyCycle           = I2C_DutyCycle_2;   /* 快速模式低/高=2:1 */
    ii.I2C_OwnAddress1         = 0x0AU;             /* 主机随便填一个 7 位地址 */
    ii.I2C_Ack                 = I2C_Ack_Enable;
    ii.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(p->i2c, &ii);
    I2C_Cmd(p->i2c, ENABLE);
}

/* 探测器件：START + 地址(写) + 看是否应答 + STOP */
int SYS_I2C_IsDeviceReady(SysI2cId_t id, uint8_t addr7)
{
    const I2cCfg_t *p = i2c_get(id);
    int err;

    if (p == 0) return SYS_I2C_ERR_PARAM;

    err = i2c_start(p->i2c);
    if (err != 0) { i2c_stop(p->i2c); return err; }

    err = i2c_send_addr(p->i2c, addr7, 0);
    i2c_stop(p->i2c);                       /* 探测完就收线 */
    return err;
}

/* 写单寄存器：START → 地址W → 寄存器号 → 数据 → STOP */
int SYS_I2C_WriteByte(SysI2cId_t id, uint8_t addr7, uint8_t reg, uint8_t data)
{
    const I2cCfg_t *p = i2c_get(id);
    int err;

    if (p == 0) return SYS_I2C_ERR_PARAM;

    err = i2c_start(p->i2c);                    if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_send_addr(p->i2c, addr7, 0);      if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_write_raw(p->i2c, reg);           if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_write_raw(p->i2c, data);          if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_wait_btf(p->i2c);                 if (err != 0) { i2c_stop(p->i2c); return err; }

    i2c_stop(p->i2c);
    return SYS_I2C_OK;
}

/* 读单寄存器：START → 地址W → 寄存器号 → 重复START → 地址R → 读 → NACK+STOP */
int SYS_I2C_ReadByte(SysI2cId_t id, uint8_t addr7, uint8_t reg, uint8_t *out)
{
    const I2cCfg_t *p = i2c_get(id);
    int err;

    if (p == 0 || out == 0) return SYS_I2C_ERR_PARAM;

    err = i2c_start(p->i2c);                    if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_send_addr(p->i2c, addr7, 0);      if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_write_raw(p->i2c, reg);           if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_wait_btf(p->i2c);                 if (err != 0) { i2c_stop(p->i2c); return err; }

    /* 重复起始 → 地址+读 */
    err = i2c_start(p->i2c);                    if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_send_addr(p->i2c, addr7, 1);      if (err != 0) { i2c_stop(p->i2c); return err; }

    /* 单字节读：先清 ACK（最后一个字节回 NACK）+ 预约 STOP，再取数 */
    p->i2c->CR1 &= (uint16_t)~I2C_CR1_ACK;
    i2c_stop(p->i2c);

    err = i2c_read_raw(p->i2c, out);

    p->i2c->CR1 |= I2C_CR1_ACK;                 /* 恢复 ACK，供下次传输 */
    return err;
}


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 连写：寄存器号之后连续写 len 个字节（地址不递增的器件靠器件内部处理） */
int SYS_I2C_WriteBytes(SysI2cId_t id, uint8_t addr7, uint8_t reg,
                       const uint8_t *buf, uint16_t len)
{
    const I2cCfg_t *p = i2c_get(id);
    uint16_t i;
    int err;

    if (p == 0 || buf == 0 || len == 0U) return SYS_I2C_ERR_PARAM;

    err = i2c_start(p->i2c);                    if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_send_addr(p->i2c, addr7, 0);      if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_write_raw(p->i2c, reg);           if (err != 0) { i2c_stop(p->i2c); return err; }

    for (i = 0; i < len; i++) {
        err = i2c_write_raw(p->i2c, buf[i]);    if (err != 0) { i2c_stop(p->i2c); return err; }
    }

    err = i2c_wait_btf(p->i2c);                 if (err != 0) { i2c_stop(p->i2c); return err; }

    i2c_stop(p->i2c);
    return SYS_I2C_OK;
}

/* 连读：长度 1 与 >1 的收尾方式不同（ACK/NACK 时机的硬件要求） */
int SYS_I2C_ReadBytes(SysI2cId_t id, uint8_t addr7, uint8_t reg,
                      uint8_t *buf, uint16_t len)
{
    const I2cCfg_t *p = i2c_get(id);
    uint16_t i;
    int err;

    if (p == 0 || buf == 0 || len == 0U) return SYS_I2C_ERR_PARAM;

    err = i2c_start(p->i2c);                    if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_send_addr(p->i2c, addr7, 0);      if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_write_raw(p->i2c, reg);           if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_wait_btf(p->i2c);                 if (err != 0) { i2c_stop(p->i2c); return err; }

    err = i2c_start(p->i2c);                    if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_send_addr(p->i2c, addr7, 1);      if (err != 0) { i2c_stop(p->i2c); return err; }

    if (len == 1U) {
        /* 单字节：清 ACK 后立刻 STOP，再读 */
        p->i2c->CR1 &= (uint16_t)~I2C_CR1_ACK;
        i2c_stop(p->i2c);
        err = i2c_read_raw(p->i2c, &buf[0]);
    } else {
        /* 多字节：读到"倒数第二个"字节后清 ACK + 预约 STOP，
         * 让最后一个字节自动被 NACK，再读完最后一个字节 */
        err = SYS_I2C_OK;
        for (i = 0; i < len; i++) {
            err = i2c_read_raw(p->i2c, &buf[i]);
            if (err != 0) break;

            if (i == (uint16_t)(len - 2U)) {
                p->i2c->CR1 &= (uint16_t)~I2C_CR1_ACK;
                i2c_stop(p->i2c);
            }
        }
    }

    p->i2c->CR1 |= I2C_CR1_ACK;                 /* 恢复 ACK */
    return err;
}

/* 错误码 → 简明文字（排查"无应答"时的第一手信息）
 * 说明 : 字符串只能用 ASCII——AC5 对源码里的 UTF-8 中文字符串
 *        解析会出错；中文解释统一放在本注释里：
 *        START  起始失败:总线忙/被拉死(查上拉与接线,可试 BusReset)
 *        ADDR   器件无应答:地址错(注意 7 位/8 位)/未接/未供电
 *        DATA   数据无应答:寄存器号或写序列超出器件范围 */
const char *SYS_I2C_ErrStr(int err)
{
    switch (err) {
    case SYS_I2C_OK:          return "OK";
    case SYS_I2C_ERR_PARAM:   return "PARAM error (id/pointer)";
    case SYS_I2C_ERR_START:   return "START fail: bus busy/stuck - check pull-up, try BusReset";
    case SYS_I2C_ERR_ADDR:    return "NO ACK at ADDR: wrong addr(7bit vs 8bit?)/no wire/no power";
    case SYS_I2C_ERR_DATA:    return "NO ACK at DATA: reg addr out of range?";
    case SYS_I2C_ERR_TIMEOUT: return "TIMEOUT: bus stuck - try BusReset";
    case SYS_I2C_ERR_BUS:     return "BUS error (BERR)";
    default:                  return "UNKNOWN error";
    }
}

/* 总线恢复：手动拨 9 个时钟 + STOP，再重新初始化外设
 * 适用 : 读操作一直超时、SCL/SDA 疑似被从机拉死不动 */
void SYS_I2C_BusReset(SysI2cId_t id)
{
    const I2cCfg_t *p = i2c_get(id);
    GPIO_InitTypeDef gi;
    uint8_t  i;
    volatile uint32_t d;

    if (p == 0) return;

    /* ① 关掉 I2C 外设，把引脚"降级"为普通开漏输出(GPIO_OType_OD)手动控制 */
    I2C_Cmd(p->i2c, DISABLE);
    GPIO_ClockEnable(p->scl_port);
    GPIO_ClockEnable(p->sda_port);

    gi.GPIO_Mode  = GPIO_Mode_OUT;
    gi.GPIO_OType = GPIO_OType_OD;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;

    gi.GPIO_Pin = p->scl_pin;
    GPIO_Init(p->scl_port, &gi);
    gi.GPIO_Pin = p->sda_pin;
    GPIO_Init(p->sda_port, &gi);

    GPIO_SetBits(p->sda_port, p->sda_pin);      /* 释放数据线 */
    GPIO_SetBits(p->scl_port, p->scl_pin);      /* 释放时钟线 */

    /* ② 拨 9 个时钟：让卡在半途的从机走完当前位、释放 SDA */
    for (i = 0; i < 9U; i++) {
        GPIO_ResetBits(p->scl_port, p->scl_pin);
        for (d = 0; d < SYS_I2C_BUS_DELAY; d++);
        GPIO_SetBits(p->scl_port, p->scl_pin);
        for (d = 0; d < SYS_I2C_BUS_DELAY; d++);
    }

    /* ③ 补一个 STOP 条件：SDA 低 → SCL 高 → SDA 高 */
    GPIO_ResetBits(p->sda_port, p->sda_pin);
    for (d = 0; d < SYS_I2C_BUS_DELAY; d++);
    GPIO_SetBits(p->scl_port, p->scl_pin);
    for (d = 0; d < SYS_I2C_BUS_DELAY; d++);
    GPIO_SetBits(p->sda_port, p->sda_pin);
    for (d = 0; d < SYS_I2C_BUS_DELAY; d++);

    /* ④ 按上次速率重新初始化（引脚会切回复用模式） */
    SYS_I2C_Init(id, i2c_speed[id]);
}
