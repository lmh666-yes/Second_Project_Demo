#include "sram.h"
/* 配套指引 : "标准库对照 / 示例 / 扩展提示"注记见同名 .h;本文件为实现层 */
#include "gpio_core.h"

/* ================================================================
 *  sram.c —— FSMC 外扩 SRAM（IS62WV51216）  实现文件
 * ================================================================
 *  FSMC 是怎么"把外部芯片变成内存"的（一句话版）：
 *
 *      MCU 执行 *p = x  →  地址落在 Bank1/NE3 子区（0x68000000 起）
 *          → FSMC 硬件自动把地址拆成 A0~A18、把数据放到 D0~D15
 *          → 拉低 NE3（片选）+ NWE（写使能）+ NBL0/NBL1（选字节）
 *          → 外部芯片收到就存下了        读的时候同理，只是拉 NOE
 *
 *  所以整个过程**不需要 CPU 参与搬运**，这就是它比"软件模拟总线"快几个
 *  数量级的原因 —— 也是为什么外部 SRAM 能配合 DMA 直接喂给 LCD。
 *
 *  ⚠ 两个必须记住的点：
 *    ① 引脚必须配成 **GPIO_Mode_AF + AF12(FSMC)**，配成普通输出是会
 *       完全不工作的（本文件 fsmc_gpio_init 里配的是数据/地址/控制三大组）；
 *    ② SRAM 区域的**地址线不止你写的那一根**：FSMC 会把整段地址都译码，
 *       所以哪怕你只写 1 个字节，硬件也会把 A0~A18 全给出去。
 * ================================================================ */


/* ================================================================
 *                      内部状态
 * ================================================================ */
static uint8_t sram_ready = 0U;


/* ================================================================
 *                      内部小工具
 * ================================================================ */

/* FSMC_D0~D15 + A0~A18 + 控制线 全部配成 AF12，高速推挽（GPIO_OType_PP）
 * 说明 : 本板的 FSMC 引脚与 LCD 完全共用（同一组总线），
 *        所以这里配重复了也没关系（寄存器写入是幂等的）。 */
static void fsmc_gpio_init(void)
{
    GPIO_InitTypeDef gi;
    uint16_t pins;

    /* ---------- 数据线 D0~D15 ---------- */
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

    /* ---------- 地址线 A0~A18 ---------- */
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

    /* ---------- 控制线 ---------- */
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

    /* ⚠ 标准库**没有**时序结构体的 StructInit 函数，必须自己逐个赋值
     *   （只调用 FSMC_NORSRAMStructInit 的话 ni 里的时序指针是空的） */
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


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
uint8_t SRAM_Init(void)
{
#if (SYS_SRAM_ENABLE == 0)
    return 1U;
#else
    /* ① 开 FSMC 时钟（F4 的 FSMC 挂在 AHB3 总线上，不是 AHB1！） */
    RCC_AHB3PeriphClockCmd(RCC_AHB3Periph_FSMC, ENABLE);

    /* ② 引脚（AF12） + ③ 时序 */
    fsmc_gpio_init();
    fsmc_norsram_init();

    sram_ready = 1U;

    /* ④ 自检：任意写几个地址再读回，确认"真的有一块芯片在那儿"
     *    （没焊芯片时读回的是总线上的浮空值，图案对不上） */
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


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
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

/* ⚠ 不能用 0 当"通过"的返回值 —— 因为 0 号半字本身也可能出错，
 *   那样"错误位置 = 0" 就会与"通过"撞车。所以内部统一用
 *   SRAM_TEST_OK 表示通过，对外才换算成"0 = 通过"。 */
uint32_t SRAM_Test(uint32_t offset, uint32_t len)
{
    uint32_t bad;

    /* 不传范围 → 全片分 9 段抽检（每段 64 个半字），既能发现坏区又不慢 */
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

    /* 写一遍、读一遍（读的时候做累加，防止编译器把循环优化掉） */
    for (i = 0UL; i < SYS_SRAM_WORDS; i++) p[i] = (uint16_t)i;
    for (i = 0UL; i < SYS_SRAM_WORDS; i++) {
        if (p[i] != (uint16_t)i) bad++;
    }

    (void)bad;

    return DWT_ElapsedUs(t0);
}

/* 文件结束 */
