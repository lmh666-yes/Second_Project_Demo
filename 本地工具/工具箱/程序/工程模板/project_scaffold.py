# -*- coding: utf-8 -*-
r"""工程模板与驱动脚手架生成器。

用途：根据用户选择的外设与任务配置，一键生成可编译的 STM32F407 工程骨架。

两种生成风格：
  --style driver   （默认）调用驱动库，复制 .h/.c 文件
  --style register 只输出 main.c，里面全是寄存器操作，不依赖任何库

用法：
    python project_scaffold.py --style register --preset minimal --out D:/MyProject
    python project_scaffold.py --style driver  --preset envmonitor --out D:/MyProject
    python project_scaffold.py --list          # 列出可用外设
"""
import argparse
import os
import shutil
import sys

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass


def find_root(start):
    d = os.path.abspath(start)
    while True:
        if os.path.isdir(os.path.join(d, '.git')) or os.path.isdir(os.path.join(d, 'EnvMonitor')):
            return d
        p = os.path.dirname(d)
        if p == d:
            return None
        d = p


ROOT = find_root(os.path.dirname(os.path.abspath(__file__)))
if ROOT is None:
    _d = os.path.dirname(os.path.abspath(__file__))
    _hit = None
    for _ in range(6):
        if os.path.isdir(os.path.join(_d, 'EnvMonitor')):
            _hit = _d
            break
        _p = os.path.dirname(_d)
        if _p == _d:
            break
        _d = _p
    ROOT = _hit or _d

BOARD_SRC = {
    'F407ZE': os.path.join('EnvMonitor', 'Project_1_检测端', 'Project'),
    'F407ZG': os.path.join('EnvMonitor', 'Project_2_数据通信端', 'Project'),
}

# driver 风格：外设 → (头文件, 源文件, 依赖, 初始化代码片段)
PERIPHERALS = {
    'clock':   ('sys_clock.h',   'sys_clock.c',   ['stm32f4xx.h'], None),
    'tick':    ('sys_tick.h',    'sys_tick.c',    ['stm32f4xx.h'], None),
    'usart':   ('sys_usart.h',   'sys_usart.c',   ['stm32f4xx.h', 'sys_dma.h'], None),
    'gpio':    ('gpio_core.h',   'gpio_core.c',   ['stm32f4xx.h'], None),
    'led':     ('led.h',         'led.c',         ['stm32f4xx.h', 'gpio_core.h'], 'LED_Init();'),
    'key':     ('key.h',         'key.c',         ['stm32f4xx.h', 'gpio_core.h', 'delay.h'], 'KEY_Init();'),
    'beep':    ('beep.h',        'beep.c',        ['stm32f4xx.h', 'gpio_core.h'], 'BEEP_Init();'),
    'delay':   ('delay.h',       'delay.c',       ['stm32f4xx.h', 'sys_tick.h'], None),
    'nvic':    ('sys_nvic.h',    'sys_nvic.c',    ['stm32f4xx.h'], None),
    'exti':    ('sys_exti.h',    'sys_exti.c',    ['stm32f4xx.h', 'gpio_core.h', 'sys_nvic.h'], None),
    'tim':     ('sys_tim.h',     'sys_tim.c',     ['stm32f4xx.h', 'sys_nvic.h'], None),
    'adc':     ('sys_adc.h',     'sys_adc.c',     ['stm32f4xx.h'], None),
    'dma':     ('sys_dma.h',     'sys_dma.c',     ['stm32f4xx.h', 'sys_nvic.h'], None),
    'i2c':     ('sys_i2c.h',     'sys_i2c.c',     ['stm32f4xx.h', 'sys_nvic.h'], None),
    'spi':     ('sys_spi.h',     'sys_spi.c',     ['stm32f4xx.h', 'sys_nvic.h'], None),
    'rtc':     ('sys_rtc.h',     'sys_rtc.c',     ['stm32f4xx.h', 'sys_nvic.h', 'sys_clock.h'], None),
    'wdg':     ('sys_wdg.h',     'sys_wdg.c',     ['stm32f4xx.h'], None),
    'flash':   ('sys_flash.h',   'sys_flash.c',   ['stm32f4xx.h'], None),
    'pwr':     ('sys_pwr.h',     'sys_pwr.c',     ['stm32f4xx.h'], None),
    'rs485':   ('sys_rs485.h',   'sys_rs485.c',   ['stm32f4xx.h', 'sys_usart.h'], None),
    'frame':   ('sys_frame.h',   'sys_frame.c',   ['stm32f4xx.h', 'sys_usart.h'], None),
    'modbus':  ('sys_modbus.h',  'sys_modbus.c',  ['stm32f4xx.h', 'sys_usart.h'], None),
    'lora':    ('lora_e22.h',    'lora_e22.c',    ['stm32f4xx.h', 'sys_usart.h'], 'LORA_E22_Init();'),
    'oled':    ('sys_oled.h',    'sys_oled.c',    ['stm32f4xx.h', 'sys_i2c.h'], 'SYS_OLED_Init(SYS_I2C_1, 0x3C);'),
    'dht11':   ('sys_dht11.h',   'sys_dht11.c',   ['stm32f4xx.h', 'sys_tim.h'], 'SYS_DHT11_Init();'),
    'bmp280':  ('sys_bmp280.h',  'sys_bmp280.c',  ['stm32f4xx.h', 'sys_i2c.h'], None),
    'bh1750':  ('sys_bh1750.h',  'sys_bh1750.c',  ['stm32f4xx.h', 'sys_i2c.h'], None),
    'ccs811':  ('sys_ccs811.h',  'sys_ccs811.c',  ['stm32f4xx.h', 'sys_i2c.h'], None),
    'mpu6050': ('sys_mpu6050.h', 'sys_mpu6050.c', ['stm32f4xx.h', 'sys_i2c.h'], None),
    'eth':     ('sys_eth.h',     'sys_eth.c',     ['stm32f4xx.h', 'sys_nvic.h'], None),
}

PRESETS = {
    'minimal': {
        'board': 'F407ZE',
        'periph': ['clock', 'tick', 'usart', 'gpio', 'led', 'delay'],
        'tasks': [],
        'desc': '最小骨架：时钟 + SysTick + 调试串口 + LED',
    },
    'freertos': {
        'board': 'F407ZE',
        'periph': ['clock', 'tick', 'usart', 'gpio', 'led', 'delay', 'nvic'],
        'tasks': ['Blink:1:128'],
        'desc': '最小 FreeRTOS：一个 LED 闪烁任务',
    },
    'envmonitor': {
        'board': 'F407ZE',
        'periph': ['clock', 'tick', 'usart', 'gpio', 'led', 'key', 'beep', 'delay', 'nvic',
                   'exti', 'tim', 'adc', 'dma', 'i2c', 'spi', 'frame', 'lora', 'oled',
                   'dht11', 'bmp280', 'bh1750', 'ccs811'],
        'tasks': ['Sensor:3:512', 'Process:3:512', 'Comm:4:512', 'LoraRx:3:512', 'Monitor:5:256'],
        'desc': 'EnvMonitor 检测端完整配置（板1）',
    },
}


# ========================================================================
# 寄存器风格：各外设的初始化代码模板
# ========================================================================

REGISTER_PERIPH = {
    'usart': {
        'defs': '''/* ---------- USART1 (PA9/PA10, 115200) ---------- */
static void USART1_Init(void)
{
    /* 使能 GPIOA 和 USART1 时钟 */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA9 = TX, PA10 = RX : 复用功能 AF7 */
    GPIOA->MODER   &= ~((3U << 18) | (3U << 20));
    GPIOA->MODER   |=  (2U << 18) | (2U << 20);
    GPIOA->OSPEEDR |=  (3U << 18) | (3U << 20);   /* 100MHz */
    GPIOA->PUPDR   |=  (1U << 18) | (1U << 20);   /* 上拉 */
    GPIOA->OTYPER  &= ~((1U << 9)  | (1U << 10)); /* 推挽 */
    GPIOA->AFR[1]  |=  (7U << 4)   | (7U << 8);   /* AF7 = USART1 */

    /* 波特率 = 168MHz / 115200 ≈ 1458 (0x05B2) */
    USART1->BRR = 168000000 / 115200;

    /* 使能发送、接收、USART */
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void USART1_SendChar(char c)
{
    while (!(USART1->SR & USART_SR_TXE));   /* 等发送寄存器空 */
    USART1->DR = c;
}

static void USART1_SendString(const char *s)
{
    while (*s) {
        USART1_SendChar(*s++);
    }
}''',
        'init': 'USART1_Init();\nUSART1_SendString("\\r\\n[F407ZE] register skeleton ready\\r\\n");',
    },
    'led': {
        'defs': '''/* ---------- LED (PF9, PF10) ---------- */
static void LED_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN;

    /* PF9/PF10 = 输出 */
    GPIOF->MODER   |=  (1U << 18) | (1U << 20);
    GPIOF->OTYPER  &= ~((1U << 9) | (1U << 10));
    GPIOF->OSPEEDR |=  (2U << 18) | (2U << 20);
    GPIOF->PUPDR   &= ~((3U << 18) | (3U << 20));
    GPIOF->BSRR    =   (1U << 25) | (1U << 26);   /* 默认熄灭 */
}

static void LED_On(int idx)
{
    if (idx == 0) GPIOF->BSRR = (1U << 9);
    else          GPIOF->BSRR = (1U << 10);
}

static void LED_Off(int idx)
{
    if (idx == 0) GPIOF->BSRR = (1U << 25);
    else          GPIOF->BSRR = (1U << 26);
}

static void LED_Toggle(int idx)
{
    if (idx == 0) GPIOF->ODR ^= (1U << 9);
    else          GPIOF->ODR ^= (1U << 10);
}''',
        'init': 'LED_Init();',
    },
    'key': {
        'defs': '''/* ---------- KEY (PE2, PE3, PE4) ---------- */
static void KEY_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;

    /* PE2/3/4 = 输入 + 上拉 */
    GPIOE->MODER &= ~((3U << 4) | (3U << 6) | (3U << 8));
    GPIOE->PUPDR |=  (1U << 4) | (1U << 6) | (1U << 8);
}

static uint8_t KEY_Read(int idx)
{
    /* 按下 = 低电平 */
    if (idx == 0) return (GPIOE->IDR & (1U << 2)) ? 0 : 1;
    if (idx == 1) return (GPIOE->IDR & (1U << 3)) ? 0 : 1;
    if (idx == 2) return (GPIOE->IDR & (1U << 4)) ? 0 : 1;
    return 0;
}''',
        'init': 'KEY_Init();',
    },
    'beep': {
        'defs': '''/* ---------- BEEP (PF8) ---------- */
static void BEEP_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN;

    /* PF8 = 输出 */
    GPIOF->MODER  |=  (1U << 16);
    GPIOF->OTYPER &= ~(1U << 8);
    GPIOF->BSRR    =  (1U << 24);   /* 默认不响 */
}

static void BEEP_On(void)  { GPIOF->BSRR = (1U << 8); }
static void BEEP_Off(void) { GPIOF->BSRR = (1U << 24); }''',
        'init': 'BEEP_Init();',
    },
    'delay': {
        'defs': '''/* ---------- 软件延时（@168MHz，粗略） ---------- */
static void delay_ms(uint32_t ms)
{
    /* 约 168000 周期/ms，按 5 周期/loop 估算 */
    for (volatile uint32_t i = 0; i < ms * 33600; i++) { __NOP(); }
}''',
        'init': '',
    },
    'systick': {
        'defs': '''/* ---------- SysTick 延时 ---------- */
static volatile uint32_t g_tick = 0;

void SysTick_Handler(void)
{
    g_tick++;
}

static uint32_t GetTick(void)
{
    return g_tick;
}

static void Delay_ms(uint32_t ms)
{
    uint32_t start = GetTick();
    while ((GetTick() - start) < ms) { }
}

static void SysTick_Init(void)
{
    /* 1ms @ 168MHz */
    SysTick->LOAD = 168000 - 1;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
}''',
        'init': 'SysTick_Init();',
    },
    'nvic': {
        'defs': '''/* ---------- NVIC：分组 + SysTick 优先级 ---------- */
static void NVIC_Init(void)
{
    /* Group 4: 4 位全做抢占，无子优先级（FreeRTOS 推荐） */
    NVIC_SetPriorityGrouping(3);   /* AIRCR.PRIGROUP = 3 → Group_4 */

    /* SysTick 优先级 = 最低（15） */
    NVIC_SetPriority(SysTick_IRQn, 15 << 4);
}''',
        'init': 'NVIC_Init();',
    },
    'tim': {
        'defs': '''/* ---------- TIM3 基本定时器（1kHz 更新） ---------- */
static void TIM3_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    /* APB1 定时器时钟 = 84MHz（APB1 分频 ≠1 时时钟翻倍）
       84MHz / 8400 = 10kHz，再 /10 → 1kHz（1ms 一拍） */
    TIM3->PSC = 8400 - 1;
    TIM3->ARR = 10 - 1;
    TIM3->EGR = TIM_EGR_UG;      /* 立即把 PSC/ARR 装进影子寄存器 */
    TIM3->SR  = 0;               /* 清更新标志（否则一开就带旧标志） */
    TIM3->CR1 = TIM_CR1_CEN;     /* 启动计数 */
}

/* 1 = 本拍已过（读后自动清标志），轮询式用法；要中断就再开 TIM3->DIER |= TIM_DIER_UIE */
static uint8_t TIM3_Tick(void)
{
    if (TIM3->SR & TIM_SR_UIF) {
        TIM3->SR = ~TIM_SR_UIF;
        return 1;
    }
    return 0;
}''',
        'init': 'TIM3_Init();',
    },
    'adc': {
        'defs': '''/* ---------- ADC1（PA0 = 通道0，单次转换，12 位） ---------- */
static void ADC1_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    GPIOA->MODER |= (3U << 0);        /* PA0 = 模拟输入（MODER = 11） */

    ADC->CCR   = ADC_CCR_ADCPRE_0;    /* PCLK2/4 = 21MHz（上限 36MHz） */
    ADC1->CR2  = 0;                   /* 先关掉再配 */
    ADC1->CR1  = 0;                   /* 独立模式、12 位 */
    ADC1->SQR1 = 0;                   /* 规则序列长度 = 1 */
    ADC1->SQR3 = 0;                   /* 第 1 个转换 = 通道 0 */
    ADC1->SMPR2 = (7U << 0);          /* 480 周期采样（高阻源要采久一点） */
    ADC1->CR2 |= ADC_CR2_ADON;        /* 上电 */
}

static uint16_t ADC1_Read(void)
{
    ADC1->CR2 |= ADC_CR2_SWSTART;     /* 软件触发一次 */
    while (!(ADC1->SR & ADC_SR_EOC)) { }
    return (uint16_t)ADC1->DR;
}''',
        'init': 'ADC1_Init();',
    },
    'dma': {
        'defs': '''/* ---------- DMA2 Stream0（外设 ← 内存 的模板） ----------
   通道号 CHSEL、数据宽度、优先级要按你的外设补，这里只搭骨架。 */
static void DMA2_Stream0_Init(void *periph_addr, void *mem_addr, uint16_t n)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;

    DMA2_Stream0->CR = 0;
    while (DMA2_Stream0->CR & DMA_SxCR_EN) { }   /* 改配置前必须先关流 */

    DMA2_Stream0->PAR  = (uint32_t)periph_addr;  /* 外设数据寄存器地址 */
    DMA2_Stream0->M0AR = (uint32_t)mem_addr;     /* 内存首地址 */
    DMA2_Stream0->NDTR = n;                      /* 传输个数 */
    DMA2_Stream0->FCR  = 0;                      /* 直接模式（不经过 FIFO） */
    DMA2_Stream0->CR   = (3U << 6)               /* 方向 = 存储器 → 外设 */
                       | DMA_SxCR_MINC           /* 内存地址自增 */
                       | DMA_SxCR_TCIE;          /* 传完中断 */
}

static void DMA2_Stream0_Start(void) { DMA2_Stream0->CR |= DMA_SxCR_EN; }

/* 1 = 传完（读后自动清标志） */
static uint8_t DMA2_Stream0_Done(void)
{
    if (DMA2_LISR & DMA_LISR_TCIF0) {
        DMA2_LIFCR = DMA_LIFCR_CTCIF0;
        return 1;
    }
    return 0;
}''',
        'init': '/* DMA2_Stream0_Init(&USART1->DR, tx_buf, sizeof(tx_buf)); 按外设补 CHSEL */',
    },
    'i2c': {
        'defs': '''/* ---------- I2C1（PB6 = SCL，PB7 = SDA，100kHz 标准模式） ---------- */
static void I2C1_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    /* PB6/PB7 = 复用开漏 AF4 + 上拉（I2C 必须开漏，否则总线拉不低） */
    GPIOB->MODER   &= ~((3U << 12) | (3U << 14));
    GPIOB->MODER   |=  (2U << 12) | (2U << 14);
    GPIOB->OTYPER  |=  (1U << 6)  | (1U << 7);
    GPIOB->OSPEEDR |=  (3U << 12) | (3U << 14);
    GPIOB->PUPDR   |=  (1U << 12) | (1U << 14);
    GPIOB->AFR[0]  |=  (4U << 24) | (4U << 28);   /* AF4 = I2C1 */

    I2C1->CR1   = 0;                  /* 配置前先关 */
    I2C1->CR2   = 42;                 /* APB1 = 42MHz → TICK = 1us */
    I2C1->CCR   = 210;                /* 100kHz：42MHz / (2 × 100k) = 210 */
    I2C1->TRISE = 43;                 /* (1000ns + 1) × 42MHz ≈ 43 */
    I2C1->CR1   = I2C_CR1_PE;         /* 使能（要 400kHz 就再置 FS 位并按公式改 CCR） */
}

/* 寄存器式写一字节：Start → 7 位地址(写) → 寄存器号 → 数据 → Stop */
static void I2C1_WriteReg(uint8_t addr7, uint8_t reg, uint8_t val)
{
    I2C1->CR1 |= I2C_CR1_START;
    while (!(I2C1->SR1 & I2C_SR1_SB)) { }
    I2C1->DR = (uint8_t)(addr7 << 1);              /* 写方向 */
    while (!(I2C1->SR1 & I2C_SR1_ADDR)) { }
    (void)I2C1->SR1; (void)I2C1->SR2;              /* 读 SR1+SR2 清 ADDR */
    I2C1->DR = reg;
    while (!(I2C1->SR1 & I2C_SR1_TXE)) { }
    I2C1->DR = val;
    while (!(I2C1->SR1 & I2C_SR1_BTF)) { }
    I2C1->CR1 |= I2C_CR1_STOP;
}''',
        'init': 'I2C1_Init();',
    },
    'spi': {
        'defs': '''/* ---------- SPI1（PA5 = SCK，PA6 = MISO，PA7 = MOSI，主机） ---------- */
static void SPI1_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    /* PA5/6/7 = 复用 AF5 */
    GPIOA->MODER   &= ~((3U << 10) | (3U << 12) | (3U << 14));
    GPIOA->MODER   |=  (2U << 10) | (2U << 12) | (2U << 14);
    GPIOA->OSPEEDR |=  (3U << 10) | (3U << 12) | (3U << 14);
    GPIOA->AFR[0]  |=  (5U << 20) | (5U << 24) | (5U << 28);   /* AF5 = SPI1 */

    /* 主机 + 软件从机管理（SSM/SSI，省一根 NSS）；BR = 011 → 84MHz/16 ≈ 5.25MHz */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (3U << 3);
    SPI1->CR1 |= SPI_CR1_SPE;
}

/* 全双工交换一字节（发出去的同时收回来的） */
static uint8_t SPI1_Xfer(uint8_t b)
{
    while (!(SPI1->SR & SPI_SR_TXE)) { }
    *(volatile uint8_t *)&SPI1->DR = b;      /* DR 必须按字节访问（有 16 位模式） */
    while (!(SPI1->SR & SPI_SR_RXNE)) { }
    return *(volatile uint8_t *)&SPI1->DR;
}''',
        'init': 'SPI1_Init();',
    },
}


def generate_register_main_c(out_dir, board, periph_names, tasks):
    """寄存器风格：只生成 main.c，里面全是寄存器操作。"""
    has_freertos = len(tasks) > 0
    has_usart    = 'usart' in periph_names
    has_led      = 'led'   in periph_names
    has_key      = 'key'   in periph_names
    has_beep     = 'beep'  in periph_names
    has_delay    = 'delay' in periph_names
    # tick 映射到 systick
    has_systick  = 'systick' in periph_names or 'tick' in periph_names

    # 收集需要的代码块
    defs_blocks = []
    init_blocks = []
    for name in periph_names:
        # tick 映射到 systick
        lookup_name = 'systick' if name == 'tick' else name
        info = REGISTER_PERIPH.get(lookup_name)
        if info:
            if info.get('defs'):
                defs_blocks.append(info['defs'])
            if info.get('init'):
                init_blocks.append(info['init'])

    # 如果既没有 systick 也没有 delay，但用户需要延时（有led裸机循环），自动加 delay
    if not has_systick and not has_delay and has_led and not has_freertos:
        info = REGISTER_PERIPH.get('delay')
        if info:
            defs_blocks.append(info['defs'])
        has_delay = True

    # 任务代码
    task_defs = []
    task_creates = []
    for t in tasks:
        parts = t.split(':')
        if len(parts) != 3:
            continue
        tname, prio, stack = parts[0], parts[1], parts[2]
        body = 'LED_Toggle(0);\n        vTaskDelay(pdMS_TO_TICKS(500));' if has_led else '/* TODO */\n        vTaskDelay(pdMS_TO_TICKS(100));'
        task_defs.append(
            'static void Task_%s(void *pvParameters)\n{'
            '\n    (void)pvParameters;\n    for (;;)\n    {'
            '\n        %s\n    }\n}' % (tname, body)
        )
        task_creates.append(
            '    xTaskCreate(Task_%s, "%s", %s, NULL, %s, NULL);'
            % (tname, tname, stack, prio)
        )

    lines = []
    lines.append('/* ================================================================')
    lines.append(' *  main.c — 由工程模板生成器自动生成（寄存器操作风格）')
    lines.append(' *  板子 : %s' % board)
    lines.append(' *  外设 : %s' % ', '.join(periph_names))
    lines.append(' * ================================================================ */')
    lines.append('')
    lines.append('#include "stm32f4xx.h"')
    lines.append('#include <stdint.h>')
    lines.append('')

    # FreeRTOS 头文件
    if has_freertos:
        lines.append('#include "FreeRTOS.h"')
        lines.append('#include "task.h"')
        lines.append('')

    # 外设定义代码块
    for block in defs_blocks:
        lines.append(block)
        lines.append('')

    # 任务定义
    for td in task_defs:
        lines.append(td)
        lines.append('')

    lines.append('int main(void)')
    lines.append('{')
    lines.append('    /* SystemInit() 已由启动文件调用：HSE 8MHz → PLL 168MHz */')
    lines.append('')

    # 统一开时钟
    ahb1_bits = []
    apb1_bits = []
    apb2_bits = []
    if 'usart' in periph_names:
        ahb1_bits.append('RCC_AHB1ENR_GPIOAEN')
        apb2_bits.append('RCC_APB2ENR_USART1EN')
    if 'led' in periph_names or 'beep' in periph_names:
        ahb1_bits.append('RCC_AHB1ENR_GPIOFEN')
    if 'key' in periph_names:
        ahb1_bits.append('RCC_AHB1ENR_GPIOEEN')

    if ahb1_bits:
        lines.append('    /* 开启 GPIO 时钟 */')
        lines.append('    RCC->AHB1ENR |= %s;' % ' | '.join(ahb1_bits))
    if apb1_bits:
        lines.append('    RCC->APB1ENR |= %s;' % ' | '.join(apb1_bits))
    if apb2_bits:
        lines.append('    RCC->APB2ENR |= %s;' % ' | '.join(apb2_bits))
    if ahb1_bits or apb1_bits or apb2_bits:
        lines.append('')

    # 初始化调用
    for block in init_blocks:
        for line in block.split('\n'):
            if line.strip():
                lines.append('    %s' % line)
        lines.append('')

    # FreeRTOS 任务
    if has_freertos:
        lines.append('    /* ---------- FreeRTOS 任务 ---------- */')
        for tc in task_creates:
            lines.append(tc)
        lines.append('    vTaskStartScheduler();')
        lines.append('')

    lines.append('    for (;;)')
    lines.append('    {')
    if has_led and not has_freertos:
        lines.append('        LED_Toggle(0);')
        if has_systick:
            lines.append('        Delay_ms(500);')
        elif has_delay:
            lines.append('        delay_ms(500);')
        else:
            lines.append('        for (volatile uint32_t i = 0; i < 5000000; i++);')
    else:
        lines.append('        /* TODO：主循环 */')
    lines.append('    }')
    lines.append('}')
    lines.append('')

    path = os.path.join(out_dir, 'main.c')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))
    return path


def generate_driver_main_c(out_dir, board, periph_names, tasks, fwlib_dir):
    """driver 风格：调用现有驱动库生成 main.c（原有逻辑）。"""
    includes = set()
    includes.add('stm32f4xx.h')
    includes.add('stdio.h')
    init_calls = []

    for name in periph_names:
        info = PERIPHERALS.get(name)
        if not info:
            continue
        h_file, c_file, deps, init_snip = info
        if h_file:
            includes.add(h_file)
        for d in deps or []:
            includes.add(d)
        if init_snip:
            init_calls.append(init_snip)

    has_usart = 'usart' in periph_names
    dbg_def = ''
    if has_usart:
        dbg_def = '\n#define DBG        SYS_USART_1\n#define DBG_BAUD   115200U\n'

    task_defs = []
    task_creates = []
    for t in tasks:
        parts = t.split(':')
        if len(parts) != 3:
            continue
        tname, prio, stack = parts[0], parts[1], parts[2]
        task_defs.append(
            'static void Task_%s(void *pvParameters)\n{'
            '\n    (void)pvParameters;\n    for (;;)\n    {'
            '\n        /* TODO */\n        vTaskDelay(pdMS_TO_TICKS(100));\n    }\n}' % tname
        )
        task_creates.append(
            '    xTaskCreate(Task_%s, "%s", %s, NULL, %s, NULL);'
            % (tname, tname, stack, prio)
        )

    has_freertos = len(tasks) > 0

    lines = []
    lines.append('/* ================================================================')
    lines.append(' *  main.c — 由工程模板生成器自动生成')
    lines.append(' *  板子 : %s' % board)
    lines.append(' *  外设 : %s' % ', '.join(periph_names))
    lines.append(' * ================================================================ */')
    lines.append('')
    for inc in sorted(includes):
        lines.append('#include "%s"' % inc)
    lines.append(dbg_def)
    lines.append('')

    if task_defs:
        for td in task_defs:
            lines.append(td)
            lines.append('')

    lines.append('int main(void)')
    lines.append('{')
    lines.append('    /* ---------- 阶段 1：时钟 ---------- */')
    lines.append('    SYS_CLK_ToHighSpeed();')
    if has_freertos:
        lines.append('    /* SysTick 由 FreeRTOS 端口层接管，不要调 SYS_TICK_Init() */')
    else:
        lines.append('    SYS_TICK_Init();')
    lines.append('')
    lines.append('    /* ---------- 阶段 2：基础外设 ---------- */')
    if has_usart:
        lines.append('    SYS_USART_Init(DBG, DBG_BAUD);')
        lines.append('    SYS_USART_SendString(DBG, "\\r\\n[%s] skeleton ready\\r\\n");' % board.lower())
    if 'led' in periph_names and 'led' not in [n for n in periph_names if PERIPHERALS.get(n, [None]*4)[3]]:
        lines.append('    LED_Init();')
    if 'key' in periph_names and 'key' not in [n for n in periph_names if PERIPHERALS.get(n, [None]*4)[3]]:
        lines.append('    KEY_Init();')
    if 'beep' in periph_names and 'beep' not in [n for n in periph_names if PERIPHERALS.get(n, [None]*4)[3]]:
        lines.append('    BEEP_Init();')
    for call in init_calls:
        lines.append('    %s' % call)
    lines.append('')

    if has_freertos:
        lines.append('    /* ---------- 阶段 3：FreeRTOS 任务 ---------- */')
        for tc in task_creates:
            lines.append(tc)
        lines.append('    vTaskStartScheduler();')
        lines.append('')

    lines.append('    for (;;)')
    lines.append('    {')
    if 'led' in periph_names and not has_freertos:
        lines.append('        LED_Toggle(0);')
        lines.append('        SYS_TICK_Delay_ms(500);')
    else:
        lines.append('        /* TODO：主循环 */')
    lines.append('    }')
    lines.append('}')
    lines.append('')

    path = os.path.join(out_dir, 'main.c')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))
    return path


def copy_file(src_dir, dst_dir, rel_path):
    src = os.path.join(src_dir, rel_path)
    dst = os.path.join(dst_dir, rel_path)
    if not os.path.exists(src):
        return False
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    return True


def generate_freertos_config(out_dir, board):
    src = os.path.join(ROOT, BOARD_SRC[board], 'FreeRTOS', 'port', 'FreeRTOSConfig.h')
    dst = os.path.join(out_dir, 'FreeRTOSConfig.h')
    if os.path.exists(src):
        shutil.copy2(src, dst)
    else:
        with open(dst, 'w', encoding='utf-8', newline='\n') as f:
            f.write(FREERTOS_CONFIG_TEMPLATE)
    return dst


def generate_readme(out_dir, board, periph_names, tasks, style):
    lines = []
    lines.append('# 生成工程说明')
    lines.append('')
    lines.append('- 板子型号：%s' % board)
    lines.append('- 生成风格：**%s**' % ('寄存器操作' if style == 'register' else '驱动库调用'))
    lines.append('- 所选外设：%s' % ', '.join(periph_names))
    if tasks:
        lines.append('- FreeRTOS 任务：')
        for t in tasks:
            parts = t.split(':')
            lines.append('  - %s（优先级 %s，栈 %s 字）' % (parts[0], parts[1], parts[2]))
    else:
        lines.append('- FreeRTOS：未启用（裸机）')
    lines.append('')
    lines.append('## 目录结构')
    lines.append('```')
    lines.append('main.c')
    if style == 'driver':
        lines.append('FreeRTOSConfig.h')
        lines.append('FWLIB/inc/*.h')
        lines.append('FWLIB/src/*.c')
    else:
        lines.append('FreeRTOSConfig.h  (若启用了 FreeRTOS)')
    lines.append('```')
    lines.append('')
    lines.append('## 下一步')
    if style == 'register':
        lines.append('1. 用 Keil 新建工程，选 STM32F407Zx')
        lines.append('2. 在 C/C++ → Define 里加 `STM32F40_41xxx`')
        lines.append('3. 把 CMSIS 启动文件（startup_stm32f40_41xxx.s + system_stm32f4xx.c）加入工程')
        lines.append('4. 添加 main.c，编译下载验证')
        lines.append('5. 若用 FreeRTOS，还需加入 FreeRTOS 内核源码')
    else:
        lines.append('1. 用 Keil 新建工程，选 STM32F407Zx，添加本目录所有 .c 到工程')
        lines.append('2. 在 C/C++ → Define 里加 `USE_STDPERIPH_DRIVER,STM32F40_41xxx`')
        lines.append('3. 若用 FreeRTOS，还需把 FreeRTOS 内核源码加入工程')
        lines.append('4. 根据实际接线修改 FWLIB/inc 里的引脚宏')
        lines.append('5. 编译、下载、验证')
    lines.append('')

    path = os.path.join(out_dir, 'README.md')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))
    return path


def run(board, periph_names, tasks, out_dir, style='driver'):
    if board not in BOARD_SRC:
        print('不支持的板子：%s。可用：%s' % (board, ', '.join(BOARD_SRC.keys())))
        return False

    os.makedirs(out_dir, exist_ok=True)

    if style == 'register':
        # 寄存器风格：只生成 main.c，不复制驱动库
        skipped = [n for n in periph_names
                   if ('systick' if n == 'tick' else n) not in REGISTER_PERIPH]
        if skipped:
            print('提示：寄存器风格还没有这些外设的模板，本次已跳过：%s'
                  % ', '.join(skipped))
            print('      （要它们就改用 --style driver，或用 --list 看支持清单）')
        main_path = generate_register_main_c(out_dir, board, periph_names, tasks)
        if tasks:
            generate_freertos_config(out_dir, board)
        readme_path = generate_readme(out_dir, board, periph_names, tasks, style)
        print('生成完成（寄存器风格）：%s' % out_dir)
        print('  生成文件：main.c' + (', FreeRTOSConfig.h' if tasks else ''))
        return True

    # driver 风格：复制驱动库文件
    src_proj = os.path.join(ROOT, BOARD_SRC[board])
    if not os.path.isdir(src_proj):
        print('模板源工程不存在：%s' % src_proj)
        return False

    copied = []
    missing = []
    for name in periph_names:
        info = PERIPHERALS.get(name)
        if not info:
            missing.append(name)
            continue
        h_file, c_file, deps, init_snip = info
        h_ok = copy_file(src_proj, out_dir, os.path.join('FWLIB', 'inc', h_file))
        c_ok = copy_file(src_proj, out_dir, os.path.join('FWLIB', 'src', c_file))
        if h_ok: copied.append(h_file)
        else:    missing.append(h_file)
        if c_ok: copied.append(c_file)
        else:    missing.append(c_file)

    main_path = generate_driver_main_c(out_dir, board, periph_names, tasks,
                                        os.path.join(out_dir, 'FWLIB'))
    if tasks:
        generate_freertos_config(out_dir, board)
    readme_path = generate_readme(out_dir, board, periph_names, tasks, style)

    print('生成完成（驱动库风格）：%s' % out_dir)
    print('  复制驱动：%d 个' % len(copied))
    if missing:
        print('  缺失：%s' % ', '.join(missing))
    print('  生成文件：main.c, FreeRTOSConfig.h, README.md')
    return True


FREERTOS_CONFIG_TEMPLATE = r'''#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (168000000UL)
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                ((unsigned short)128)
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TIME_SLICING                  1

#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ((size_t)(30 * 1024))
#define configAPPLICATION_ALLOCATED_HEAP        0

#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configQUEUE_REGISTRY_SIZE               8

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               3
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            128

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2

#define configASSERT(x) \
    if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;); }

#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY       15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#define INCLUDE_vTaskPrioritySet             1
#define INCLUDE_uxTaskPriorityGet            1
#define INCLUDE_vTaskDelete                  1
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_vTaskDelayUntil              1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_xTaskGetSchedulerState       1
#define INCLUDE_xTaskGetCurrentTaskHandle    1
#define INCLUDE_uxTaskGetStackHighWaterMark  1
#define INCLUDE_xTimerPendFunctionCall       1

#endif /* FREERTOS_CONFIG_H */
'''


def main():
    parser = argparse.ArgumentParser(description='STM32 工程模板生成器')
    parser.add_argument('--board', default='F407ZE', help='板子型号（F407ZE/F407ZG）')
    parser.add_argument('--out', help='输出目录')
    parser.add_argument('--periph', default='', help='外设列表，逗号分隔')
    parser.add_argument('--tasks', default='', help='任务列表，格式：Name:prio:stack,Name:prio:stack')
    parser.add_argument('--preset', help='使用预设配置（minimal/freertos/envmonitor）')
    parser.add_argument('--style', default='driver', choices=['driver', 'register'],
                        help='生成风格：driver=调用驱动库（默认），register=寄存器操作')
    parser.add_argument('--list', action='store_true', help='列出可用外设')
    parser.add_argument('--list-presets', action='store_true', help='列出可用预设')
    args = parser.parse_args()

    if args.list:
        print('可用外设（driver 风格，%d 个）：' % len(PERIPHERALS))
        for name, info in PERIPHERALS.items():
            print('  %-12s %s / %s' % (name, info[0], info[1]))
        print()
        print('寄存器风格支持：%s' % ', '.join(sorted(REGISTER_PERIPH)))
        return

    if args.list_presets:
        print('可用预设：')
        for name, cfg in PRESETS.items():
            print('  %-12s %s' % (name, cfg['desc']))
            print('    外设：%s' % ', '.join(cfg['periph']))
            if cfg['tasks']:
                print('    任务：%s' % ', '.join(cfg['tasks']))
        return

    if not args.out:
        print('错误：--out 未指定')
        sys.exit(1)

    if args.preset:
        cfg = PRESETS.get(args.preset)
        if not cfg:
            print('未知预设：%s' % args.preset)
            sys.exit(1)
        board = cfg['board']
        periph = cfg['periph']
        tasks = cfg['tasks']
    else:
        board = args.board
        periph = [p.strip() for p in args.periph.split(',') if p.strip()]
        tasks = [t.strip() for t in args.tasks.split(',') if t.strip()]

    ok = run(board, periph, tasks, args.out, style=args.style)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
