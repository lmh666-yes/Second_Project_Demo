#include "sys_i2c.h"
#include "gpio_core.h"

/* 公共事务入口都要加锁，锁用 FreeRTOS 递归互斥量实现，故引入下面两个头文件
 * 裸机工程复用本文件时需自行提供这两个头文件，或删掉本节与公共入口里的加锁 */
#include "FreeRTOS.h"
#include "semphr.h"

/* sys_i2c.c: 系统 I2C 总线模块实现。直接操作 I2C 寄存器，I2C_Init 按当前 PCLK1
 * 自动算 CCR/TRISE 分频。公共函数按三段式组织，哪一步失败就返回哪一步的错误码:
 *   1) 起条件 / 发地址: 失败 ERR_ADDR（总线忙或器件无应答）
 *   2) 传数据:          失败 ERR_DATA（数据阶段无应答）
 *   3) 等结束 / 停:     失败 ERR_TIMEOUT（超时，总线可能被拉死）
 * 配合 SYS_I2C_ErrStr 取错误文字；全部为查询标志加循环等待的阻塞实现，
 * 不含中断，最长等待由 SYS_I2C_TIMEOUT 封顶 */


/* 内部配置表 */
typedef struct {
    I2C_TypeDef  *i2c;
    GPIO_TypeDef *scl_port;
    uint16_t      scl_pin;
    GPIO_TypeDef *sda_port;
    uint16_t      sda_pin;
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


/* 总线级递归互斥（多任务共用一条 I2C 总线）
 * I2C1 上挂 24C02(0x50) / MPU6050(0x68) / OLED(0x3C)。I2C 事务是 START 到 STOP
 * 的连续时序，两个任务的事务一旦交错，从机会收错命令、读到脏数据，
 * 因此锁的粒度是一次完整事务，不是单个字节或单个寄存器。
 * 用递归锁的原因: 1) Scan 逐个探测会调 IsDeviceReady，BusReset 末尾要重新初始化，
 * 同一任务需重入；2) OLED 刷一屏是连续多次事务，同一任务会多次进出锁。
 * 惰性创建: 任何时刻第一次用到某条总线就建锁，调用方无需关心初始化顺序，
 * 也不要求调度器已启动。
 * 边界: 1) 调度器未运行时加锁是空操作，此时是单线程环境，上电初始化阶段走这里；
 * 2) 中断上下文不加锁（用 __get_IPSR() 判断），互斥量会阻塞并可能触发任务切换，
 * 中断服务程序里要用 I2C 应交给任务处理（队列或信号量唤醒）；
 * 3) 加锁不关中断、不长临界区，刷一屏要 20~25ms，用 __disable_irq 会毁掉实时性。 */
static SemaphoreHandle_t s_bus_mtx[SYS_I2C_COUNT];   /* 每总线一把递归锁（惰性创建） */


/* 内部辅助 */
/* 引脚掩码转引脚序号统一用 gpio_core 的 GPIO_PinSource */

/* 起始条件: 当前不是主机时才等总线空闲，再发 START，等 I2C_SR1_SB 置位
 * 不能无条件等 I2C_SR2_BUSY: BUSY 从第一个 START 起置 1，到 STOP 才清，
 * 而读操作要发重复起始（RESTART），此时 BUSY 仍为 1，空等会一直等到超时返回 ERR_START
 * 判据改用 I2C_SR2_MSL: 已经是主机就跳过空等 */
/* 硬件错误标志统一检查，覆盖 I2C SR1 的三个错误位:
 * BERR (SR1 bit8) 总线错误: START/STOP 出现在不该出现的位置，多主机冲突或 SDA 走线串扰
 * ARLO (SR1 bit9) 仲裁丢失: 发地址/数据时被别的器件抢占，多主机下常见，本次事务作废
 * OVR  (SR1 bit10) 溢出: 数据还没被读走又来了一个，读到的值不可信
 * 三个位都必须软件写 0 清除，不清会一直挂着干扰后续检查
 * 本函数在等待某个标志的循环里调用，发现错误就清标志并返回对应错误码（0 表示无错误）
 * 返回 SYS_I2C_ERR_BUS 时调用方必须发 STOP 复位总线 */
static int i2c_check_hw_err(I2C_TypeDef *I)
{
    uint16_t sr1 = I->SR1;

    if ((sr1 & I2C_SR1_BERR) != 0U) {
        I->SR1 &= (uint16_t)~I2C_SR1_BERR;      /* 写 0 清除 */
        return SYS_I2C_ERR_BUS;
    }
    if ((sr1 & I2C_SR1_ARLO) != 0U) {
        I->SR1 &= (uint16_t)~I2C_SR1_ARLO;
        return SYS_I2C_ERR_BUS;                 /* 被抢占总线：本次事务作废 */
    }
    if ((sr1 & I2C_SR1_OVR) != 0U) {
        I->SR1 &= (uint16_t)~I2C_SR1_OVR;
        return SYS_I2C_ERR_DATA;                /* 数据溢出，读到的值不可信 */
    }
    return SYS_I2C_OK;
}

static int i2c_start(I2C_TypeDef *I)
{
    uint32_t to;
    int      herr;

    if ((I->SR2 & I2C_SR2_MSL) == 0U) {          /* 不是主机时才等空闲 */
        to = SYS_I2C_TIMEOUT;
        while ((I->SR2 & I2C_SR2_BUSY) != 0U) {
            if (to-- == 0U) return SYS_I2C_ERR_START;
        }
    }

    I->CR1 |= I2C_CR1_START;

    to = SYS_I2C_TIMEOUT;
    while ((I->SR1 & I2C_SR1_SB) == 0U) {
        if (to-- == 0U) return SYS_I2C_ERR_START;
        if ((I->SR1 & I2C_SR1_AF) != 0U) {          /* 异常应答保护 */
            I->SR1 &= (uint16_t)~I2C_SR1_AF;
            return SYS_I2C_ERR_START;
        }
        herr = i2c_check_hw_err(I);                 /* BERR / ARLO / OVR */
        if (herr != SYS_I2C_OK) return herr;
    }
    return SYS_I2C_OK;
}

/* 发送 7 位地址加读写位，等 I2C_SR1_ADDR 置位；I2C_SR1_AF 置位表示无应答
 * read = 0 写方向（地址位为 0），read = 1 读方向（地址位为 1） */
static int i2c_send_addr(I2C_TypeDef *I, uint8_t addr7, uint8_t read)
{
    uint32_t to = SYS_I2C_TIMEOUT;
    int      herr;

    I->DR = (uint16_t)(((uint16_t)addr7 << 1) | (read ? 1U : 0U));

    while ((I->SR1 & I2C_SR1_ADDR) == 0U) {
        if (to-- == 0U) return SYS_I2C_ERR_TIMEOUT;
        if ((I->SR1 & I2C_SR1_AF) != 0U) {
            I->SR1 &= (uint16_t)~I2C_SR1_AF;        /* 清 AF，好发 STOP */
            return SYS_I2C_ERR_ADDR;                /* 无应答 */
        }
        herr = i2c_check_hw_err(I);                 /* ARLO 在多主机下很常见 */
        if (herr != SYS_I2C_OK) return herr;
    }

    (void)I->SR1;                                   /* 读 SR1 + SR2 */
    (void)I->SR2;                                   /* 清 ADDR 标志 */
    return SYS_I2C_OK;
}

/* 写一个字节：等 TXE（数据寄存器空）再写 */
static int i2c_write_raw(I2C_TypeDef *I, uint8_t b)
{
    uint32_t to = SYS_I2C_TIMEOUT;
    int      herr;

    while ((I->SR1 & I2C_SR1_TXE) == 0U) {
        if (to-- == 0U) return SYS_I2C_ERR_TIMEOUT;
        if ((I->SR1 & I2C_SR1_AF) != 0U) {
            I->SR1 &= (uint16_t)~I2C_SR1_AF;
            return SYS_I2C_ERR_DATA;
        }
        herr = i2c_check_hw_err(I);
        if (herr != SYS_I2C_OK) return herr;
    }
    I->DR = b;
    return SYS_I2C_OK;
}

/* 等 BTF（一个字节真正发完）: 写周期收尾、读周期换方向前用 */
static int i2c_wait_btf(I2C_TypeDef *I)
{
    uint32_t to = SYS_I2C_TIMEOUT;
    int      herr;

    while ((I->SR1 & I2C_SR1_BTF) == 0U) {
        if (to-- == 0U) return SYS_I2C_ERR_TIMEOUT;
        if ((I->SR1 & I2C_SR1_AF) != 0U) {
            I->SR1 &= (uint16_t)~I2C_SR1_AF;
            return SYS_I2C_ERR_DATA;
        }
        herr = i2c_check_hw_err(I);             /* BERR / ARLO / OVR */
        if (herr != SYS_I2C_OK) return herr;
    }
    return SYS_I2C_OK;
}

/* 等 RXNE（收到一个字节）再读走 */
static int i2c_read_raw(I2C_TypeDef *I, uint8_t *out)
{
    uint32_t to = SYS_I2C_TIMEOUT;
    int      herr;

    while ((I->SR1 & I2C_SR1_RXNE) == 0U) {
        if (to-- == 0U) return SYS_I2C_ERR_TIMEOUT;
        /* 读得太慢时 OVR 置位，DR 里的值已不是要读的那个，必须报错而不是照读 */
        herr = i2c_check_hw_err(I);
        if (herr != SYS_I2C_OK) return herr;
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


/* 基础功能 */
/* 初始化: 时钟、引脚（复用开漏 GPIO_OType_OD 加上拉 GPIO_PuPd_UP）、外设参数（速率） */
static void i2c_init_locked(SysI2cId_t id, uint32_t speed)
{
    const I2cCfg_t *p = i2c_get(id);
    I2C_InitTypeDef ii;
    GPIO_InitTypeDef gi;

    if (p == 0) return;
    if (speed == 0U) speed = 100000U;

    i2c_speed[id] = speed;

    /* 时钟: I2C 外设（挂 APB1）+ 两个引脚端口 */
    RCC_APB1PeriphClockCmd(p->clk, ENABLE);
    GPIO_ClockEnable(p->scl_port);
    GPIO_ClockEnable(p->sda_port);

    /* 引脚: 复用功能 + 开漏输出 + 内部上拉，对应 GPIO_Mode_AF / GPIO_OType_OD / GPIO_PuPd_UP，
     * 这种线与形态是 I2C 总线要求；板上另有外部 4.7k 上拉，不能改成推挽（GPIO_OType_PP） */
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

    /* 外设参数: 速率交给标准库自动换算分频 */
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
static int i2c_isdeviceready_locked(SysI2cId_t id, uint8_t addr7)
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
static int i2c_writebyte_locked(SysI2cId_t id, uint8_t addr7, uint8_t reg, uint8_t data)
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

/* 只发一个字节（命令或只写寄存器号，后面不带数据），时序 START、地址W、cmd、等 BTF、STOP
 * 与 WriteByte 的区别: WriteByte 发寄存器号加数据两个字节，本函数只发一个
 * 用于 BH1750（0x01 上电、0x10 连续测量）、CCS811（APP_START = 0xF4）这类命令即所写字节的器件
 * 不能用 WriteBytes(..., NULL, 0) 顶替，它判 buf==0||len==0 直接返回 ERR_PARAM */
static int i2c_writecmd_locked(SysI2cId_t id, uint8_t addr7, uint8_t cmd)
{
    const I2cCfg_t *p = i2c_get(id);
    int err;

    if (p == 0) return SYS_I2C_ERR_PARAM;

    err = i2c_start(p->i2c);                    if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_send_addr(p->i2c, addr7, 0);      if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_write_raw(p->i2c, cmd);           if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_wait_btf(p->i2c);                 if (err != 0) { i2c_stop(p->i2c); return err; }

    i2c_stop(p->i2c);
    return SYS_I2C_OK;
}

/* 读单寄存器：START → 地址W → 寄存器号 → 重复START → 地址R → 读 → NACK+STOP */
static int i2c_readbyte_locked(SysI2cId_t id, uint8_t addr7, uint8_t reg, uint8_t *out)
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


/* 扩展功能 */
/* 连写：寄存器号之后连续写 len 个字节（地址不递增的器件靠器件内部处理） */
static int i2c_writebytes_locked(SysI2cId_t id, uint8_t addr7, uint8_t reg,
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
static int i2c_readbytes_locked(SysI2cId_t id, uint8_t addr7, uint8_t reg,
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

/* 无寄存器号连读: 不发任何寄存器或命令字节，直接发 START 后读 len 个字节
 * 用于 BH1750 这类没有寄存器号概念的器件，连续测量模式下直接读 2 字节就是当前光照值
 * 不能用 SYS_I2C_ReadBytes 替代: ReadBytes 一定先发一个 reg 字节，对该器件这个字节是命令
 * （BH1750 的 0x00 是 Power Down），会把器件状态改掉
 * 调用前器件必须已处于可输出数据的状态，如 BH1750 需先发 0x01 上电 + 0x10 连续 H 分辨率 */
static int i2c_readraw_locked(SysI2cId_t id, uint8_t addr7,
                              uint8_t *buf, uint16_t len)
{
    const I2cCfg_t *p = i2c_get(id);
    uint16_t i;
    int err;

    if (p == 0 || buf == 0 || len == 0U) return SYS_I2C_ERR_PARAM;

    /* 直接 START 加地址读位，中间没有任何写字节，这是与 ReadBytes 的唯一区别 */
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

/* 错误码转简明文字，排查无应答时的第一手信息
 * 字符串只能用 ASCII: AC5 解析源码里的 UTF-8 中文字符串会出错，中文解释放在本注释里
 * START 起始失败: 总线忙或被拉死（查上拉与接线，可试 BusReset）
 * ADDR  器件无应答: 地址错（注意 7 位/8 位）或未接、未供电
 * DATA  数据无应答: 寄存器号或写序列超出器件范围 */
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
static void i2c_busreset_locked(SysI2cId_t id)
{
    const I2cCfg_t *p = i2c_get(id);
    GPIO_InitTypeDef gi;
    uint8_t  i;
    volatile uint32_t d;

    if (p == 0) return;

    /* 关掉 I2C 外设，把引脚改为普通开漏输出（GPIO_Mode_OUT + GPIO_OType_OD）手动控制 */
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

    /* 拨 9 个时钟: 让卡在半途的从机走完当前位并释放 SDA */
    for (i = 0; i < 9U; i++) {
        GPIO_ResetBits(p->scl_port, p->scl_pin);
        for (d = 0; d < SYS_I2C_BUS_DELAY; d++);
        GPIO_SetBits(p->scl_port, p->scl_pin);
        for (d = 0; d < SYS_I2C_BUS_DELAY; d++);
    }

    /* 补一个 STOP 条件: SDA 拉低、SCL 拉高、SDA 拉高 */
    GPIO_ResetBits(p->sda_port, p->sda_pin);
    for (d = 0; d < SYS_I2C_BUS_DELAY; d++);
    GPIO_SetBits(p->scl_port, p->scl_pin);
    for (d = 0; d < SYS_I2C_BUS_DELAY; d++);
    GPIO_SetBits(p->sda_port, p->sda_pin);
    for (d = 0; d < SYS_I2C_BUS_DELAY; d++);

    /* 按上次速率重新初始化，引脚切回复用模式；本函数已在锁内，直接调内部实现 */
    i2c_init_locked(id, i2c_speed[id]);
}

/* 总线扫描与 16 位寄存器读写 */
/* 扫描总线上的所有器件（标准范围 0x08 ~ 0x77） */
static uint8_t i2c_scan_locked(SysI2cId_t id, uint8_t *found, uint8_t max)
{
    uint8_t n = 0U;
    uint8_t a;

    for (a = 0x08U; a <= 0x77U; a++) {
        if (SYS_I2C_IsDeviceReady(id, a) == SYS_I2C_OK) {   /* 公共入口：递归锁可重入 */
            if ((found != 0) && (n < max)) found[n] = a;
            n++;
        }
    }
    return n;
}

/* 16 位寄存器地址连写 */
static int i2c_writereg16_locked(SysI2cId_t id, uint8_t addr7, uint16_t reg,
                                 const uint8_t *buf, uint16_t len)
{
    const I2cCfg_t *p = i2c_get(id);
    uint16_t i;
    int err;

    if (p == 0 || buf == 0 || len == 0U) return SYS_I2C_ERR_PARAM;

    err = i2c_start(p->i2c);                          if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_send_addr(p->i2c, addr7, 0);            if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_write_raw(p->i2c, (uint8_t)(reg >> 8)); if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_write_raw(p->i2c, (uint8_t)(reg & 0xFFU)); if (err != 0) { i2c_stop(p->i2c); return err; }

    for (i = 0U; i < len; i++) {
        err = i2c_write_raw(p->i2c, buf[i]);          if (err != 0) { i2c_stop(p->i2c); return err; }
    }

    err = i2c_wait_btf(p->i2c);                       if (err != 0) { i2c_stop(p->i2c); return err; }

    i2c_stop(p->i2c);
    return SYS_I2C_OK;
}

/* 16 位寄存器地址连读（重复起始换向,收尾规则与 ReadBytes 相同） */
static int i2c_readreg16_locked(SysI2cId_t id, uint8_t addr7, uint16_t reg,
                                uint8_t *buf, uint16_t len)
{
    const I2cCfg_t *p = i2c_get(id);
    uint16_t i;
    int err;

    if (p == 0 || buf == 0 || len == 0U) return SYS_I2C_ERR_PARAM;

    err = i2c_start(p->i2c);                          if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_send_addr(p->i2c, addr7, 0);            if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_write_raw(p->i2c, (uint8_t)(reg >> 8)); if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_write_raw(p->i2c, (uint8_t)(reg & 0xFFU)); if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_wait_btf(p->i2c);                       if (err != 0) { i2c_stop(p->i2c); return err; }

    err = i2c_start(p->i2c);                          if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_send_addr(p->i2c, addr7, 1);            if (err != 0) { i2c_stop(p->i2c); return err; }

    if (len == 1U) {
        p->i2c->CR1 &= (uint16_t)~I2C_CR1_ACK;
        i2c_stop(p->i2c);
        err = i2c_read_raw(p->i2c, &buf[0]);
    } else {
        err = SYS_I2C_OK;
        for (i = 0U; i < len; i++) {
            err = i2c_read_raw(p->i2c, &buf[i]);
            if (err != 0) break;

            if (i == (uint16_t)(len - 2U)) {
                p->i2c->CR1 &= (uint16_t)~I2C_CR1_ACK;
                i2c_stop(p->i2c);
            }
        }
    }

    p->i2c->CR1 |= I2C_CR1_ACK;
    return err;
}

/* 公共锁入口 SYS_I2C_Lock / SYS_I2C_Unlock */

void SYS_I2C_Lock(SysI2cId_t bus)
{
    BaseType_t sched;

    if ((uint32_t)bus >= (uint32_t)SYS_I2C_COUNT) return;      /* 参数非法：空操作 */

    /* 中断上下文不加锁: 互斥量会阻塞并可能触发任务切换，中断里调用是非法操作；
     * __get_IPSR() != 0 表示当前处于异常/中断处理中（Cortex-M 标准做法） */
    if (__get_IPSR() != 0U) return;

    /* 调度器未运行: 此时是单线程环境，加锁是空操作，上电初始化阶段走这里；
     * 不是漏加锁，还没有任何任务在跑，不可能有并发 */
    sched = xTaskGetSchedulerState();
    if (sched != taskSCHEDULER_RUNNING) return;

    /* 惰性建锁: 第一次用到该总线时创建。用 taskENTER_CRITICAL() 保护判断为空
     * 到创建这一段，避免两个任务同时走到这里各建一把锁；临界区只有几条指令 */
    if (s_bus_mtx[bus] == 0) {
        taskENTER_CRITICAL();
        if (s_bus_mtx[bus] == 0) {
            s_bus_mtx[bus] = xSemaphoreCreateRecursiveMutex();
        }
        taskEXIT_CRITICAL();
    }

    /* 建锁失败（堆不够）则跳过加锁，退化为无互斥也不能卡死 */
    if (s_bus_mtx[bus] == 0) return;

    /* 拿锁: portMAX_DELAY 表示一直等，等待期间出让 CPU 且不关中断；
     * 持锁者总能在有限时间内放锁，每个事务都有 SYS_I2C_TIMEOUT 封顶 */
    (void)xSemaphoreTakeRecursive(s_bus_mtx[bus], portMAX_DELAY);
}

void SYS_I2C_Unlock(SysI2cId_t bus)
{
    if ((uint32_t)bus >= (uint32_t)SYS_I2C_COUNT) return;
    if (__get_IPSR() != 0U) return;
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return;
    if (s_bus_mtx[bus] == 0) return;
    (void)xSemaphoreGiveRecursive(s_bus_mtx[bus]);
}


/* 公共事务入口，全部在此加锁
 * 每个函数先锁总线，再调同名 _locked 实现，最后解锁，一次完整 I2C 事务中途
 * 不会被别的任务插进来；实现体统一为 static xxx_locked()，只有 Lock 包装一个出口，
 * 不会出现提前 return 忘记解锁
 * 需要多次事务原子时用 SYS_I2C_Lock()/Unlock() 把整段包起来，递归锁允许同任务重入
 * 不加锁的场合: 调度器未运行（上电初始化）、中断上下文，见 SYS_I2C_Lock */

void SYS_I2C_Init(SysI2cId_t id, uint32_t speed)
{
    SYS_I2C_Lock(id);
    i2c_init_locked(id, speed);
    SYS_I2C_Unlock(id);
}

int SYS_I2C_IsDeviceReady(SysI2cId_t id, uint8_t addr7)
{
    int err;

    SYS_I2C_Lock(id);
    err = i2c_isdeviceready_locked(id, addr7);
    SYS_I2C_Unlock(id);
    return err;
}

int SYS_I2C_WriteByte(SysI2cId_t id, uint8_t addr7, uint8_t reg, uint8_t data)
{
    int err;

    SYS_I2C_Lock(id);
    err = i2c_writebyte_locked(id, addr7, reg, data);
    SYS_I2C_Unlock(id);
    return err;
}

int SYS_I2C_ReadByte(SysI2cId_t id, uint8_t addr7, uint8_t reg, uint8_t *out)
{
    int err;

    SYS_I2C_Lock(id);
    err = i2c_readbyte_locked(id, addr7, reg, out);
    SYS_I2C_Unlock(id);
    return err;
}

int SYS_I2C_WriteBytes(SysI2cId_t id, uint8_t addr7, uint8_t reg,
                       const uint8_t *buf, uint16_t len)
{
    int err;

    SYS_I2C_Lock(id);
    err = i2c_writebytes_locked(id, addr7, reg, buf, len);
    SYS_I2C_Unlock(id);
    return err;
}

int SYS_I2C_ReadBytes(SysI2cId_t id, uint8_t addr7, uint8_t reg,
                      uint8_t *buf, uint16_t len)
{
    int err;

    SYS_I2C_Lock(id);
    err = i2c_readbytes_locked(id, addr7, reg, buf, len);
    SYS_I2C_Unlock(id);
    return err;
}

int SYS_I2C_ReadRaw(SysI2cId_t id, uint8_t addr7, uint8_t *buf, uint16_t len)
{
    int err;

    SYS_I2C_Lock(id);
    err = i2c_readraw_locked(id, addr7, buf, len);
    SYS_I2C_Unlock(id);
    return err;
}

int SYS_I2C_WriteCmd(SysI2cId_t id, uint8_t addr7, uint8_t cmd)
{
    int err;

    SYS_I2C_Lock(id);
    err = i2c_writecmd_locked(id, addr7, cmd);
    SYS_I2C_Unlock(id);
    return err;
}

void SYS_I2C_BusReset(SysI2cId_t id)
{
    SYS_I2C_Lock(id);
    i2c_busreset_locked(id);
    SYS_I2C_Unlock(id);
}

/* 整段扫描持锁: 一次扫描 0x08~0x77 视为一次逻辑操作
 * 内部逐个探测调公共入口 SYS_I2C_IsDeviceReady，递归锁允许同任务重入
 * 总线全空时每次探测都要等满超时，整段可能持续数十到数百毫秒，期间别的任务拿不到总线 */
uint8_t SYS_I2C_Scan(SysI2cId_t id, uint8_t *found, uint8_t max)
{
    uint8_t n;

    SYS_I2C_Lock(id);
    n = i2c_scan_locked(id, found, max);
    SYS_I2C_Unlock(id);
    return n;
}

int SYS_I2C_WriteReg16(SysI2cId_t id, uint8_t addr7, uint16_t reg,
                       const uint8_t *buf, uint16_t len)
{
    int err;

    SYS_I2C_Lock(id);
    err = i2c_writereg16_locked(id, addr7, reg, buf, len);
    SYS_I2C_Unlock(id);
    return err;
}

int SYS_I2C_ReadReg16(SysI2cId_t id, uint8_t addr7, uint16_t reg,
                      uint8_t *buf, uint16_t len)
{
    int err;

    SYS_I2C_Lock(id);
    err = i2c_readreg16_locked(id, addr7, reg, buf, len);
    SYS_I2C_Unlock(id);
    return err;
}

