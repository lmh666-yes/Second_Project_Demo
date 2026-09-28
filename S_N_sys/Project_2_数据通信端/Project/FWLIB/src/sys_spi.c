#include "sys_spi.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"

/* ================================================================
 *  sys_spi.c —— 【系统】SPI 主机模块  实现文件
 * ================================================================
 *  实现要点 :
 *    ① 分频器计算：F407 的 SPI 只能取 PCLK 的 2/4/8/…/256 分频，
 *       本模块把"目标 Hz"换算成"不超速的最高挡位"（类似 sys_tim
 *       的自动 PSC/ARR 思路，上层不用记 256/128/64… 这些挡位）；
 *    ② SPI1 在 APB2(PCLK2)，SPI2/3 在 APB1(PCLK1)——分频基准
 *       不同，切换主频后调用 SYS_SPI_Init 重算即可；
 *    ③ 收发用"查询 TXE/RXNE 标志"的阻塞写法，简单可靠；
 *       片选(CS)由上层 GPIO 控制（见头文件说明）。
 * ================================================================ */


/* ================================================================
 *                    内部配置表
 * ================================================================ */
typedef struct {
    SPI_TypeDef  *spi;
    GPIO_TypeDef *sck_port;   uint16_t sck_pin;
    GPIO_TypeDef *miso_port;  uint16_t miso_pin;
    GPIO_TypeDef *mosi_port;  uint16_t mosi_pin;
    uint8_t       af;         /* 复用功能号 */
    uint8_t       apb;        /* 1 = APB1, 2 = APB2 */
    uint32_t      clk;        /* RCC 时钟位 */
} SpiCfg_t;

static const SpiCfg_t spi_cfg[SYS_SPI_COUNT] = {
    { SPI1, SYS_SPI1_SCK_PORT, SYS_SPI1_SCK_PIN, SYS_SPI1_MISO_PORT, SYS_SPI1_MISO_PIN,
      SYS_SPI1_MOSI_PORT, SYS_SPI1_MOSI_PIN, GPIO_AF_SPI1, 2, RCC_APB2Periph_SPI1 },
    { SPI2, SYS_SPI2_SCK_PORT, SYS_SPI2_SCK_PIN, SYS_SPI2_MISO_PORT, SYS_SPI2_MISO_PIN,
      SYS_SPI2_MOSI_PORT, SYS_SPI2_MOSI_PIN, GPIO_AF_SPI2, 1, RCC_APB1Periph_SPI2 },
    { SPI3, SYS_SPI3_SCK_PORT, SYS_SPI3_SCK_PIN, SYS_SPI3_MISO_PORT, SYS_SPI3_MISO_PIN,
      SYS_SPI3_MOSI_PORT, SYS_SPI3_MOSI_PIN, GPIO_AF_SPI3, 1, RCC_APB1Periph_SPI3 },
};

/* 编译期护栏：配置表项数必须与 SYS_SPI_COUNT 一致 */
typedef char spi_cfg_count_check[(sizeof(spi_cfg) / sizeof(spi_cfg[0]) == SYS_SPI_COUNT) ? 1 : -1];

/* 分频挡位表：索引 0~7 → PCLK 的 2/4/8/16/32/64/128/256 分频
 * （宏值与 CR1 的 BR[2:0] 字段一一对应，可直接写寄存器） */
static const uint16_t spi_br_table[8] = {
    SPI_BaudRatePrescaler_2,   SPI_BaudRatePrescaler_4,
    SPI_BaudRatePrescaler_8,   SPI_BaudRatePrescaler_16,
    SPI_BaudRatePrescaler_32,  SPI_BaudRatePrescaler_64,
    SPI_BaudRatePrescaler_128, SPI_BaudRatePrescaler_256
};


/* ================================================================
 *                    内部辅助
 * ================================================================ */
/* 引脚掩码 → 引脚序号：统一走 gpio_core 的 GPIO_PinSource（不再重复实现） */

/* 该引脚是否占用 PB3/PB4（JTAG 的 JTDO / NJTRST，上电默认被调试口占用）
 * 用途 : 只在真正用到这些引脚时才关 JTAG——换到其它引脚后
 *       本模块不会去动调试口配置 */
static uint8_t spi_is_jtag_pin(GPIO_TypeDef *port, uint16_t pin)
{
    if (port != GPIOB) return 0U;
    return (pin == GPIO_Pin_3 || pin == GPIO_Pin_4) ? 1U : 0U;
}

/* 取该 SPI 所在总线的频率（SPI1→PCLK2，SPI2/3→PCLK1） */
static uint32_t spi_pclk(const SpiCfg_t *p)
{
    RCC_ClocksTypeDef clocks;

    RCC_GetClocksFreq(&clocks);
    return (p->apb == 2U) ? clocks.PCLK2_Frequency : clocks.PCLK1_Frequency;
}

/* 目标速率 → 分频挡位索引（选"不超速的最高挡"） */
static uint8_t spi_br_pick(uint32_t pclk, uint32_t speed)
{
    uint8_t i;

    for (i = 0; i < 8U; i++) {
        /* 索引 i 对应 2^(i+1) 分频 */
        if ((pclk >> (i + 1U)) <= speed) return i;
    }
    return 7U;      /* 最慢挡兜底 */
}

/* 等 SPI 总线彻底空闲（BSY 清零）——改速率/切换前必须等 */
static void spi_wait_idle(SPI_TypeDef *S)
{
    while (SPI_I2S_GetFlagStatus(S, SPI_I2S_FLAG_BSY) != RESET);
}

/* 参数检查 */
static const SpiCfg_t *spi_get(SysSpiId_t id)
{
    if (id >= SYS_SPI_COUNT) return 0;
    return &spi_cfg[id];
}

/* 填 SPI_InitTypeDef 并写寄存器（Init/SetSpeed 共用） */
static void spi_config_write(SPI_TypeDef *S, uint32_t pclk, uint32_t speed, uint8_t mode)
{
    SPI_InitTypeDef si;

    SPI_StructInit(&si);
    si.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    si.SPI_Mode              = SPI_Mode_Master;
    si.SPI_DataSize          = SPI_DataSize_8b;
    si.SPI_CPOL              = (mode & 0x02U) ? SPI_CPOL_High : SPI_CPOL_Low;
    si.SPI_CPHA              = (mode & 0x01U) ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;
    si.SPI_NSS               = SPI_NSS_Soft;          /* 软件片选 */
    si.SPI_BaudRatePrescaler = spi_br_table[spi_br_pick(pclk, speed)];
    si.SPI_FirstBit          = SPI_FirstBit_MSB;
    si.SPI_CRCPolynomial     = 7U;
    SPI_Init(S, &si);
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
void SYS_SPI_Init(SysSpiId_t id, uint32_t speed, SysSpiMode_t mode)
{
    const SpiCfg_t *p = spi_get(id);
    GPIO_InitTypeDef gi;
    uint32_t pclk;

    if (p == 0) return;
    if (speed == 0U) speed = SYS_SPI_DEFAULT_SPEED;

    /* ① 时钟：SPI 外设（SPI1 挂 APB2，SPI2/3 挂 APB1）+ 引脚端口 */
    if (p->apb == 2U) RCC_APB2PeriphClockCmd(p->clk, ENABLE);
    else              RCC_APB1PeriphClockCmd(p->clk, ENABLE);
    GPIO_ClockEnable(p->sck_port);
    GPIO_ClockEnable(p->miso_port);
    GPIO_ClockEnable(p->mosi_port);

#if SYS_SPI_FREE_JTAG
    /* ② 释放 JTAG 引脚：仅当本路引脚落在 PB3/PB4 上时才关（看真实引脚，不看 id）
     *    PB3=JTDO、PB4=NJTRST 上电默认被 JTAG 占用，不关则 SPI 收发全为 0；
     *    关闭方式：写 SYSCFG->MEMRMP 的 SWJ_CFG 位(26:24)=0b010
     *    ——「只关 JTAG、保留 SWD」（PA13/PA14 调试不受影响），写一次即可；
     *    换引脚后本段自动跳过，不会碰到调试口配置 */
    if (spi_is_jtag_pin(p->sck_port, p->sck_pin)  ||
        spi_is_jtag_pin(p->miso_port, p->miso_pin) ||
        spi_is_jtag_pin(p->mosi_port, p->mosi_pin)) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
        SYSCFG->MEMRMP = (SYSCFG->MEMRMP & ~(0x7UL << 24U)) | (0x2UL << 24U);
    }
#else
    (void)0;
#endif

    /* ③ 引脚复用：SCK/MOSI 推挽输出（GPIO_OType_PP）；MISO 输入(上拉 GPIO_PuPd_UP 抗悬空) */
    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    gi.GPIO_Pin = p->sck_pin;
    GPIO_PinAFConfig(p->sck_port, GPIO_PinSource(p->sck_pin), p->af);
    GPIO_Init(p->sck_port, &gi);

    gi.GPIO_Pin = p->mosi_pin;
    GPIO_PinAFConfig(p->mosi_port, GPIO_PinSource(p->mosi_pin), p->af);
    GPIO_Init(p->mosi_port, &gi);

    gi.GPIO_Pin  = p->miso_pin;
    gi.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_PinAFConfig(p->miso_port, GPIO_PinSource(p->miso_pin), p->af);
    GPIO_Init(p->miso_port, &gi);

    /* ④ 外设参数 + 使能 */
    SPI_Cmd(p->spi, DISABLE);                    /* 配置期间先关 */
    pclk = spi_pclk(p);
    spi_config_write(p->spi, pclk, speed, (uint8_t)mode);
    SPI_Cmd(p->spi, ENABLE);
}

uint8_t SYS_SPI_TransferByte(SysSpiId_t id, uint8_t tx)
{
    const SpiCfg_t *p = spi_get(id);

    if (p == 0) return 0xFFU;

    while (SPI_I2S_GetFlagStatus(p->spi, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(p->spi, tx);

    while (SPI_I2S_GetFlagStatus(p->spi, SPI_I2S_FLAG_RXNE) == RESET);
    return (uint8_t)SPI_I2S_ReceiveData(p->spi);
}

void SYS_SPI_Transfer(SysSpiId_t id, const uint8_t *txbuf, uint8_t *rxbuf, uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++) {
        uint8_t tx = (txbuf != 0) ? txbuf[i] : 0xFFU;
        uint8_t rx = SYS_SPI_TransferByte(id, tx);

        if (rxbuf != 0) rxbuf[i] = rx;
    }
}


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
void SYS_SPI_Write(SysSpiId_t id, const uint8_t *buf, uint16_t len)
{
    SYS_SPI_Transfer(id, buf, 0, len);
}

void SYS_SPI_Read(SysSpiId_t id, uint8_t *buf, uint16_t len)
{
    SYS_SPI_Transfer(id, 0, buf, len);
}

void SYS_SPI_SetSpeed(SysSpiId_t id, uint32_t speed)
{
    const SpiCfg_t *p = spi_get(id);
    uint32_t pclk;

    if (p == 0) return;
    if (speed == 0U) speed = SYS_SPI_DEFAULT_SPEED;

    spi_wait_idle(p->spi);                       /* 等当前字节发完 */

    pclk = spi_pclk(p);

    /* 只改 CR1 的 BR[2:0] 分频位，不动其它配置 */
    p->spi->CR1 &= (uint16_t)~SPI_CR1_BR;
    p->spi->CR1 |= spi_br_table[spi_br_pick(pclk, speed)];
}
