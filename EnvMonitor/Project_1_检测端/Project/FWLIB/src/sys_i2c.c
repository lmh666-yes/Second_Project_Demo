#include "sys_i2c.h"
/* 接口与错误码说明见 sys_i2c.h */
#include "gpio_core.h"

/* 公共事务入口均加锁, 锁由 FreeRTOS 提供; 裸机工程自行提供这两个头文件或改用空宏。 */
#include "FreeRTOS.h"
#include "semphr.h"

/* ================ sys_i2c.c — 【系统】I2C 总线模块 实现文件 ================ */
/*  实现: 直接操作 I2C 寄存器; I2C_Init 依当前 PCLK1 换算 CCR/TRISE 分频。
 *  公共事务三段式: 起条件/发地址(ERR_ADDR) → 传数据(ERR_DATA) →
 *  等结束/停(ERR_TIMEOUT); 阻塞实现, 无中断, SYS_I2C_TIMEOUT 封顶。
 *  互斥见下节; 接口与裸机移植见 sys_i2c.h。 */


/* ============ 内部配置表 ============ */
/* 总线编号（= SYS_I2C_x 枚举顺序 = 数组下标）→ 硬件资源映射:
 * i2c=外设指针(I2C1~I2C3); scl/sda=端口+掩码(引脚宏见 sys_i2c.h);
 * af=复用功能号(GPIO_AF_I2Cx); clk=RCC_APB1Periph_I2Cx。
 * 换引脚只改头文件宏, 增删总线才动本表（护栏核对项数）。 */
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


/* ======== 总线级递归互斥（多任务共用一条 I2C 总线） ======== */
/* 依据 : I2C 事务为 START → 地址 → 数据 → STOP 的连续时序; I2C1 挂
 *        24C02(0x50)/MPU6050(0x68)/OLED(0x3C), 两个任务的事务交错会使
 *        从机收错命令或读到脏数据, 锁粒度为一次完整事务。
 * 递归锁 : 本文件内部有嵌套调用(Scan 调 IsDeviceReady, BusReset 末尾重新
 *        初始化); 上层 OLED 可将整屏刷新(约 40 次事务)包成一帧。
 * 惰性创建 : 首次使用某条总线时建锁, 不要求调度器已启动。
 * 边界 : 调度器未运行时(上电初始化)加锁为空操作, 此时为单线程环境;
 *        中断上下文不加锁(__get_IPSR() 判断), 互斥量在中断中调用非法, 中断内
 *        直接调 SYS_I2C_xxx 需自行保证不与任务并发;
 *        锁不关中断, 无长临界区(整屏刷新约 20~25ms, 关中断会破坏实时性)。
 * 移植 : 裸机工程删本节并将 SYS_I2C_Lock/Unlock 改为空宏, 代价为两个头文件
 *        + 句柄数组 + 惰性创建的一段临界区。 */
static SemaphoreHandle_t s_bus_mtx[SYS_I2C_COUNT];   /* 每总线一把递归锁（惰性创建） */


/* ======== 内部辅助 ======== */
/* 引脚掩码 → 引脚序号：统一走 gpio_core 的 GPIO_PinSource */

/* 起始条件：非主机时才等总线空闲，再发 START，等 I2C_SR1_SB 置位。
 * 判据用 I2C_SR2_MSL 而非 I2C_SR2_BUSY: 读操作要发重复起始(RESTART),
 * 此时 BUSY 仍为 1, 等其清零会一直等到超时并返回 SYS_I2C_ERR_START。 */
static int i2c_start(I2C_TypeDef *I)
{
    uint32_t to;

    if ((I->SR2 & I2C_SR2_MSL) == 0U) {          /* 不是主机 → 才需要等空闲 */
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
    }
    return SYS_I2C_OK;
}

/* 发送 7 位地址 + 读写位，等 ADDR；AF 置位 = 无应答
 * read = 0 → 写方向(地址+0)；read = 1 → 读方向(地址+1) */
static int i2c_send_addr(I2C_TypeDef *I, uint8_t addr7, uint8_t read)
{
    uint32_t to = SYS_I2C_TIMEOUT;

    I->DR = (uint16_t)(((uint16_t)addr7 << 1) | (read ? 1U : 0U));

    while ((I->SR1 & I2C_SR1_ADDR) == 0U) {
        if (to-- == 0U) return SYS_I2C_ERR_TIMEOUT;
        if ((I->SR1 & I2C_SR1_AF) != 0U) {
            I->SR1 &= (uint16_t)~I2C_SR1_AF;        /* 清 AF，好发 STOP */
            return SYS_I2C_ERR_ADDR;                /* 无应答 */
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

/* 等 BTF（当前字节发送完成）：写周期收尾、读周期换向前使用 */
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


/* ======== 区块 2：基础功能 ======== */
/* 初始化：时钟 → 引脚(复用开漏 GPIO_OType_OD + 上拉 GPIO_PuPd_UP) → 外设参数(速率) */
static void i2c_init_locked(SysI2cId_t id, uint32_t speed)
{
    const I2cCfg_t *p = i2c_get(id);
    I2C_InitTypeDef ii;
    GPIO_InitTypeDef gi;

    if (p == 0) return;
    if (speed == 0U) speed = 100000U;

    i2c_speed[id] = speed;

    /* 时钟：I2C 外设（APB1）+ 两个引脚端口 */
    RCC_APB1PeriphClockCmd(p->clk, ENABLE);
    GPIO_ClockEnable(p->scl_port);
    GPIO_ClockEnable(p->sda_port);

    /* 引脚：复用(GPIO_Mode_AF) + 开漏(GPIO_OType_OD) + 内部上拉(GPIO_PuPd_UP)。
     * 依据：开漏 + 上拉为 I2C 线与电气形态, 板上另有外部 4.7k 上拉, 不可改为推挽。 */
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

    /* 外设参数：速率由标准库换算 CCR/TRISE 分频 */
    I2C_DeInit(p->i2c);
    I2C_StructInit(&ii);
    ii.I2C_ClockSpeed          = speed;
    ii.I2C_Mode                = I2C_Mode_I2C;
    ii.I2C_DutyCycle           = I2C_DutyCycle_2;   /* 快速模式低/高=2:1 */
    ii.I2C_OwnAddress1         = 0x0AU;             /* 主机地址, 任意 7 位值 */
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

/* 只发一个字节（命令, 不带数据）：START → 地址W → cmd → 等 BTF → STOP。
 * 用于 BH1750(0x01 上电 / 0x10 连续测量)、CCS811(APP_START = 0xF4) 等
 * "所写字节即命令"的器件; 不能用 WriteBytes(..., NULL, 0) 替代, 该函数对
 * buf==0 || len==0 直接返回 SYS_I2C_ERR_PARAM。 */
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

    /* 重复起始 → 地址+读（此时总线上没有 STOP，BUSY 仍为 1，必须走 restart 分支） */
    err = i2c_start(p->i2c);           if (err != 0) { i2c_stop(p->i2c); return err; }
    err = i2c_send_addr(p->i2c, addr7, 1);      if (err != 0) { i2c_stop(p->i2c); return err; }

    /* 单字节读：先清 ACK（最后一个字节回 NACK）+ 预约 STOP，再取数 */
    p->i2c->CR1 &= (uint16_t)~I2C_CR1_ACK;
    i2c_stop(p->i2c);

    err = i2c_read_raw(p->i2c, out);

    p->i2c->CR1 |= I2C_CR1_ACK;                 /* 恢复 ACK，供下次传输 */
    return err;
}


/* ======== 区块 3：扩展功能 ======== */
/* 连写：寄存器号之后连续写 len 个字节（地址不递增的器件由器件内部处理） */
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

    /* 重复起始 → 地址+读（中途无 STOP，BUSY 仍为 1 → restart 分支） */
    err = i2c_start(p->i2c);           if (err != 0) { i2c_stop(p->i2c); return err; }
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

/* 无寄存器号连读：START → 地址R, 中途不发写字节, 直接读 len 个字节。
 * 用途: BH1750 等"所写字节即命令"的器件, 连续测量模式下直接读 2 字节即光照值。
 * 约束: 不能用 SYS_I2C_ReadBytes 替代, 它必先发一个 reg 字节, 对这类器件该字节
 *       即命令(BH1750 的 0x00 为 Power Down); 调用前器件须已可输出数据(如先发
 *       0x01 上电 + 0x10 连续 H 分辨率)。 */
static int i2c_readraw_locked(SysI2cId_t id, uint8_t addr7,
                              uint8_t *buf, uint16_t len)
{
    const I2cCfg_t *p = i2c_get(id);
    uint16_t i;
    int err;

    if (p == 0 || buf == 0 || len == 0U) return SYS_I2C_ERR_PARAM;

    /* 直接 START → 地址+R, 中间无写字节 */
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

/* 错误码 → 简明文字。字符串只能用 ASCII(AC5 解析源码中的 UTF-8 中文字符串会
 * 出错), 中文解释放在本注释: START 总线忙/被拉死(查上拉与接线, 可试 BusReset);
 * ADDR 器件无应答(地址错/未接/未供电); DATA 数据无应答(寄存器号或写序列超出
 * 器件范围)。 */
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
 * 适用 : 读操作持续超时、SCL/SDA 被从机拉死不动 */
static void i2c_busreset_locked(SysI2cId_t id)
{
    const I2cCfg_t *p = i2c_get(id);
    GPIO_InitTypeDef gi;
    uint8_t  i;
    volatile uint32_t d;

    if (p == 0) return;

    /* 关闭 I2C 外设, 引脚改为普通开漏输出(GPIO_OType_OD)手动控制 */
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

    /* 拨 9 个时钟: 使卡在半途的从机走完当前位并释放 SDA */
    for (i = 0; i < 9U; i++) {
        GPIO_ResetBits(p->scl_port, p->scl_pin);
        for (d = 0; d < SYS_I2C_BUS_DELAY; d++);
        GPIO_SetBits(p->scl_port, p->scl_pin);
        for (d = 0; d < SYS_I2C_BUS_DELAY; d++);
    }

    /* 补 STOP 条件: SDA 低 → SCL 高 → SDA 高 */
    GPIO_ResetBits(p->sda_port, p->sda_pin);
    for (d = 0; d < SYS_I2C_BUS_DELAY; d++);
    GPIO_SetBits(p->scl_port, p->scl_pin);
    for (d = 0; d < SYS_I2C_BUS_DELAY; d++);
    GPIO_SetBits(p->sda_port, p->sda_pin);
    for (d = 0; d < SYS_I2C_BUS_DELAY; d++);

    /* 按上次速率重新初始化(引脚切回复用模式);
     * 调用已加锁的内部实现, 避免在锁内再走公共入口 */
    i2c_init_locked(id, i2c_speed[id]);
}

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

    err = i2c_start(p->i2c);                 if (err != 0) { i2c_stop(p->i2c); return err; }
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

/* ======== 新公共入口：SYS_I2C_Lock / SYS_I2C_Unlock ======== */

void SYS_I2C_Lock(SysI2cId_t bus)
{
    BaseType_t sched;

    if ((uint32_t)bus >= (uint32_t)SYS_I2C_COUNT) return;      /* 参数非法：空操作 */

    /* 中断上下文不加锁: 互斥量会阻塞并可能切换任务, 中断中调用非法。
     * __get_IPSR() != 0 表示当前处于异常/中断处理中。 */
    if (__get_IPSR() != 0U) return;

    /* 调度器未运行(上电初始化阶段)时为单线程环境, 加锁为空操作。 */
    sched = xTaskGetSchedulerState();
    if (sched != taskSCHEDULER_RUNNING) return;

    /* 惰性建锁: 首次使用该总线时创建。用 taskENTER_CRITICAL() 保护"判空 →
     * 创建", 避免两个任务各建一把锁而使互斥失效; 临界区仅几条指令。 */
    if (s_bus_mtx[bus] == 0) {
        taskENTER_CRITICAL();
        if (s_bus_mtx[bus] == 0) {
            s_bus_mtx[bus] = xSemaphoreCreateRecursiveMutex();
        }
        taskEXIT_CRITICAL();
    }

    /* 建锁失败(堆不足)则跳过加锁: 退化为无互斥而非阻塞 */
    if (s_bus_mtx[bus] == 0) return;

    /* 拿锁: portMAX_DELAY 无限等待, 等待期间让出 CPU 且不关中断;
     * 持锁者必在有限时间内放锁(每个事务由 SYS_I2C_TIMEOUT 封顶)。 */
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


/* ======== 公共事务入口（全部在此加锁） ======== */
/* 每个函数均为"锁总线 → 调同名 _locked 实现 → 解锁", 一次完整事务不会被其他
 * 任务插入; 只有 Lock 包装这一个出口, 不会提前 return 漏解锁。需多次事务原子
 * 时用 SYS_I2C_Lock()/Unlock() 包住整段(递归锁允许同任务重入, OLED 刷一整屏)。
 * 不加锁的场合见 SYS_I2C_Lock。 */

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

/* 扫描：整段持锁, 内部逐个探测走公共入口 SYS_I2C_IsDeviceReady(递归锁可重入)。
 * 总线全空时每次探测等满超时, 整段可能持续数十~数百毫秒, 期间其他任务
 * 无法取得该总线。属调试手段, 不宜频繁调用。 */
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

