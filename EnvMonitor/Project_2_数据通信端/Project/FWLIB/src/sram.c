#include "sram.h"
#include "gpio_core.h"
#include "delay.h"          /* DWT_GetUs / DWT_ElapsedUs（测速/超时） */

/* sram.c: FSMC 外扩 SRAM，器件型号 IS62WV51216
 * 地址落在 Bank1 的 NE3 子区（0x68000000 起），FSMC 硬件译码 A0~A18 与 D0~D15，
 * 并自动产生 NE3(片选) / NWE / NOE / NBL0 / NBL1，CPU 不参与搬运
 * 引脚必须配成 GPIO_Mode_AF + AF12(FSMC)，配成普通输出则总线不工作 */


/* 0 = 未初始化或自检失败，1 = 可用 */
static uint8_t sram_ready = 0U;


/* FSMC_D0~D15 + A0~A18 + 控制线配成 AF12，推挽输出，速度 100MHz
 * 本板 FSMC 引脚与 LCD 共用同一组总线，重复配置无影响：寄存器写入幂等 */
static void fsmc_gpio_init(void)
{
    GPIO_InitTypeDef gi;
    uint16_t pins;

    /* 数据线 D0~D15 */
    /* D0~D1  = PD14/PD15   D2~D3  = PD0/PD1
     * D4~D12 = PE7~PE15    D13~D15 = PD8/PD9/PD10 */
    GPIO_ClockEnable(GPIOD);
    GPIO_ClockEnable(GPIOE);

    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_Speed = GPIO_Speed_100MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;

    pins = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 |
           GPIO_Pin_14 | GPIO_Pin_15;

    gi.GPIO_Pin = pins;
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource0,  GPIO_AF_FSMC);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource1,  GPIO_AF_FSMC);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource8,  GPIO_AF_FSMC);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource9,  GPIO_AF_FSMC);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource10, GPIO_AF_FSMC);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource14, GPIO_AF_FSMC);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource15, GPIO_AF_FSMC);
    GPIO_Init(GPIOD, &gi);

    pins = GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 |
           GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    for (uint8_t i = 7U; i <= 15U; i++) {
        GPIO_PinAFConfig(GPIOE, (uint8_t)i, GPIO_AF_FSMC);
    }
    gi.GPIO_Pin = pins;
    GPIO_Init(GPIOE, &gi);

    /* 地址线 A0~A18 */
    /* A0 ~A5  = PF0~PF5     A6  = PF12
     * A7 ~A9  = PF13~PF15   A10~A15 = PG0~PG5
     * A16~A18 = PD11~PD13 */
    GPIO_ClockEnable(GPIOF);
    GPIO_ClockEnable(GPIOG);

    pins = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 |
           GPIO_Pin_5 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    for (uint8_t i = 0U; i <= 5U; i++)      GPIO_PinAFConfig(GPIOF, (uint8_t)i, GPIO_AF_FSMC);
    for (uint8_t i = 12U; i <= 15U; i++)    GPIO_PinAFConfig(GPIOF, (uint8_t)i, GPIO_AF_FSMC);
    gi.GPIO_Pin = pins;
    GPIO_Init(GPIOF, &gi);

    pins = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 |
           GPIO_Pin_5 | GPIO_Pin_10;        /* A10~A15 + NE3(PG10) */
    for (uint8_t i = 0U; i <= 5U; i++)      GPIO_PinAFConfig(GPIOG, (uint8_t)i, GPIO_AF_FSMC);
    GPIO_PinAFConfig(GPIOG, GPIO_PinSource10, GPIO_AF_FSMC);
    gi.GPIO_Pin = pins;
    GPIO_Init(GPIOG, &gi);

    pins = GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13;     /* A16~A18 */
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource11, GPIO_AF_FSMC);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource12, GPIO_AF_FSMC);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource13, GPIO_AF_FSMC);
    gi.GPIO_Pin = pins;
    GPIO_Init(GPIOD, &gi);

    /* 控制线 */
    /* NOE = PD4   NWE = PD5   NBL0 = PE0   NBL1 = PE1 */
    gi.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource4, GPIO_AF_FSMC);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource5, GPIO_AF_FSMC);
    GPIO_Init(GPIOD, &gi);

    gi.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource0, GPIO_AF_FSMC);
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource1, GPIO_AF_FSMC);
    GPIO_Init(GPIOE, &gi);
}

/* 配 FSMC 的时序寄存器（BCR/BTR 由标准库填） */
static void fsmc_norsram_init(void)
{
    FSMC_NORSRAMInitTypeDef ni;
    FSMC_NORSRAMTimingInitTypeDef ti;

    /* 标准库无时序结构体的 StructInit 函数，ti 各字段逐个赋值
     * FSMC_NORSRAMStructInit 只复位 ni 自身字段，ni 的时序指针仍需指向 ti */
    ti.FSMC_AddressSetupTime      = SYS_SRAM_ADDR_SETUP;
    ti.FSMC_AddressHoldTime       = SYS_SRAM_ADDR_HOLD;
    ti.FSMC_DataSetupTime         = SYS_SRAM_DATA_SETUP;
    ti.FSMC_BusTurnAroundDuration = SYS_SRAM_BUS_TURN;
    ti.FSMC_CLKDivision           = 0;
    ti.FSMC_DataLatency           = 0;
    ti.FSMC_AccessMode            = FSMC_AccessMode_A;   /* SRAM 标准时序 */

    FSMC_NORSRAMStructInit(&ni);            /* 先把 ni 的其它字段复位为 0 */

    ni.FSMC_Bank                  = SYS_SRAM_BANK;
    ni.FSMC_DataAddressMux        = FSMC_DataAddressMux_Disable;
    ni.FSMC_MemoryType            = FSMC_MemoryType_SRAM;
    ni.FSMC_MemoryDataWidth       = FSMC_MemoryDataWidth_16b;   /* 512K x 16 */
    ni.FSMC_BurstAccessMode       = FSMC_BurstAccessMode_Disable;
    ni.FSMC_AsynchronousWait      = FSMC_AsynchronousWait_Disable;
    ni.FSMC_WaitSignalPolarity    = FSMC_WaitSignalPolarity_Low;
    ni.FSMC_WrapMode              = FSMC_WrapMode_Disable;
    ni.FSMC_WaitSignalActive      = FSMC_WaitSignalActive_BeforeWaitState;
    ni.FSMC_WriteOperation        = FSMC_WriteOperation_Enable;
    ni.FSMC_WaitSignal            = FSMC_WaitSignal_Disable;
    ni.FSMC_ExtendedMode          = FSMC_ExtendedMode_Disable;
    ni.FSMC_WriteBurst            = FSMC_WriteBurst_Disable;
    ni.FSMC_ReadWriteTimingStruct = &ti;
    ni.FSMC_WriteTimingStruct     = &ti;

    FSMC_NORSRAMInit(&ni);
    FSMC_NORSRAMCmd(SYS_SRAM_BANK, ENABLE);
}


uint8_t SRAM_Init(void)
{
#if (SYS_SRAM_ENABLE == 0)
    return 1U;
#else
    /* 开 FSMC 时钟：F4 的 FSMC 挂在 AHB3 总线 */
    RCC_AHB3PeriphClockCmd(RCC_AHB3Periph_FSMC, ENABLE);

    /* 配引脚 AF12 与时序 */
    fsmc_gpio_init();
    fsmc_norsram_init();

    sram_ready = 1U;

    /* 自检：写几个地址再读回，图案需一致
     * 未焊芯片时读回总线浮空值，图案不匹配 */
    if (SRAM_Test(0UL, 0UL) != 0UL) {
        sram_ready = 0U;
        return 1U;
    }

    return 0U;
#endif
}

uint8_t SRAM_IsReady(void)
{
    return sram_ready;
}

uint32_t SRAM_GetSize(void)
{
    return SYS_SRAM_SIZE_BYTES;
}

volatile uint16_t *SRAM_Ptr(void)
{
    return (volatile uint16_t *)SYS_SRAM_BASE_ADDR;
}

uint16_t SRAM_ReadWord(uint32_t offset)
{
    return *(volatile uint16_t *)(SYS_SRAM_BASE_ADDR + (offset << 1));
}

uint8_t SRAM_WriteWord(uint32_t offset, uint16_t value)
{
    if (offset >= SYS_SRAM_WORDS) return 1U;

    *(volatile uint16_t *)(SYS_SRAM_BASE_ADDR + (offset << 1)) = value;
    return 0U;
}

uint8_t SRAM_ReadBytes(uint32_t offset, uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if (buf == 0 || len == 0UL) return 1U;
    if ((offset + len) > SYS_SRAM_SIZE_BYTES) return 1U;

    for (i = 0UL; i < len; i++) {
        buf[i] = *(volatile uint8_t *)(SYS_SRAM_BASE_ADDR + offset + i);
    }
    return 0U;
}

uint8_t SRAM_WriteBytes(uint32_t offset, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if (buf == 0 || len == 0UL) return 1U;
    if ((offset + len) > SYS_SRAM_SIZE_BYTES) return 1U;

    for (i = 0UL; i < len; i++) {
        *(volatile uint8_t *)(SYS_SRAM_BASE_ADDR + offset + i) = buf[i];
    }
    return 0U;
}

uint8_t SRAM_ReadWords(uint32_t offset, uint16_t *buf, uint32_t count)
{
    uint32_t i;

    if (buf == 0 || count == 0UL) return 1U;
    if ((offset + count) > SYS_SRAM_WORDS) return 1U;

    for (i = 0UL; i < count; i++) {
        buf[i] = *(volatile uint16_t *)(SYS_SRAM_BASE_ADDR + ((offset + i) << 1));
    }
    return 0U;
}

uint8_t SRAM_WriteWords(uint32_t offset, const uint16_t *buf, uint32_t count)
{
    uint32_t i;

    if (buf == 0 || count == 0UL) return 1U;
    if ((offset + count) > SYS_SRAM_WORDS) return 1U;

    for (i = 0UL; i < count; i++) {
        *(volatile uint16_t *)(SYS_SRAM_BASE_ADDR + ((offset + i) << 1)) = buf[i];
    }
    return 0U;
}


/* 半字图案自检的公共实现：start 为半字偏移，count 为半字数
 * 返回 : SRAM_TEST_OK = 全通过；否则 = 第一个出错的半字索引 */
#define SRAM_TEST_OK   0xFFFFFFFFUL

static uint32_t sram_test_words(uint32_t start, uint32_t count)
{
    volatile uint16_t *p = (volatile uint16_t *)SYS_SRAM_BASE_ADDR;
    uint32_t i;
    uint32_t step;

    /* 图案一：地址异或图案（能抓出地址线粘连/短路） */
    for (i = start; i < (start + count); i++) {
        p[i] = (uint16_t)(i ^ 0x5A5AU);
    }
    for (i = start; i < (start + count); i++) {
        if (p[i] != (uint16_t)(i ^ 0x5A5AU)) return i;
    }

    /* 图案二：走 1（0x0001 逐位左移，能抓出数据线粘连） */
    for (i = start; i < (start + count); i++) {
        step = (i - start) & 0x0FUL;
        p[i] = (uint16_t)(1U << step);
    }
    for (i = start; i < (start + count); i++) {
        step = (i - start) & 0x0FUL;
        if (p[i] != (uint16_t)(1U << step)) return i;
    }

    /* 图案三：全 0 / 全 1 */
    for (i = start; i < (start + count); i++) p[i] = 0x0000U;
    for (i = start; i < (start + count); i++) {
        if (p[i] != 0x0000U) return i;
    }
    for (i = start; i < (start + count); i++) p[i] = 0xFFFFU;
    for (i = start; i < (start + count); i++) {
        if (p[i] != 0xFFFFU) return i;
    }

    return SRAM_TEST_OK;
}

/* 内部用 SRAM_TEST_OK 表示通过：0 号半字本身也可能出错，若以 0 表示通过会与
 *   错误位置 0 冲突；对外接口换算成 0 = 通过 */
uint32_t SRAM_Test(uint32_t offset, uint32_t len)
{
    uint32_t bad;

    /* offset 与 len 都为 0 时全片分 9 段抽检，每段 64 个半字 */
    if (offset == 0UL && len == 0UL) {
        uint32_t seg;

        for (seg = 0UL; seg < 9UL; seg++) {
            uint32_t start = (SYS_SRAM_WORDS / 9UL) * seg;

            bad = sram_test_words(start, 64UL);
            if (bad != SRAM_TEST_OK) return (bad + 1UL);   /* 换成 1 起的序号 */
        }
        return 0UL;
    }

    if ((offset + len) > SYS_SRAM_SIZE_BYTES) return 0xFFFFFFFFUL;
    if (len < 2UL) return 0UL;                          /* 不到一个半字，没法测 */

    bad = sram_test_words(offset >> 1, len >> 1);

    return (bad == SRAM_TEST_OK) ? 0UL : (bad + 1UL);
}

void SRAM_Clear(void)
{
    volatile uint16_t *p = (volatile uint16_t *)SYS_SRAM_BASE_ADDR;
    uint32_t i;

    for (i = 0UL; i < SYS_SRAM_WORDS; i++) p[i] = 0x0000U;
}

uint32_t SRAM_SpeedTestUs(void)
{
    volatile uint16_t *p = (volatile uint16_t *)SYS_SRAM_BASE_ADDR;
    uint32_t t0;
    uint32_t i;
    uint32_t bad = 0UL;

    t0 = DWT_GetUs();

    /* 先写后读回校验，读回值参与比较，避免循环被优化掉 */
    for (i = 0UL; i < SYS_SRAM_WORDS; i++) p[i] = (uint16_t)i;
    for (i = 0UL; i < SYS_SRAM_WORDS; i++) {
        if (p[i] != (uint16_t)i) bad++;
    }

    (void)bad;

    return DWT_ElapsedUs(t0);
}
