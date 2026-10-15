#include "boot_uart.h"

#include "stm32f4xx.h"

#define BOOT_PCLK2_HZ       84000000UL    /* APB2 = 168MHz / 2（SystemInit 默认主频） */
#define BOOT_UART_BAUD      115200UL

/* 寄存器位直接用设备头里的（RCC_AHB1ENR_GPIOAEN / RCC_APB2ENR_USART1EN /
   USART_SR_TXE / USART_CR1_UE / USART_CR1_TE），别重复定义 —— 同名重定义会报 #47-D。 */

void Boot_UartInit(void)
{
    /* 1) 时钟：GPIOA（AHB1）+ USART1（APB2） */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* 2) PA9 → 复用功能 7（USART1_TX）；只要发，不配 RX */
    GPIOA->MODER &= ~(3UL << (9 * 2));
    GPIOA->MODER |=  (2UL << (9 * 2));              /* 10b = 复用功能 */
    GPIOA->OSPEEDR |= (3UL << (9 * 2));             /* 最高速度，波形干净 */
    GPIOA->OTYPER &= ~(1UL << 9);                   /* 推挽 */
    GPIOA->PUPDR &= ~(3UL << (9 * 2));              /* 无上下拉 */
    GPIOA->AFR[1] &= ~(0xFUL << ((9 - 8) * 4));     /* AFR[1] 管引脚 8~15 */
    GPIOA->AFR[1] |=  (7UL << ((9 - 8) * 4));       /* AF7 = USART1 */

    /* 3) 波特率：BRR = PCLK2 / 波特（84MHz / 115200 ≈ 729） */
    USART1->BRR = (BOOT_PCLK2_HZ + (BOOT_UART_BAUD / 2u)) / BOOT_UART_BAUD;

    /* 4) 使能：UE + TE（只发，不需要 RXNE 中断） */
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE;
}

void Boot_UartPutc(char c)
{
    uint32_t guard = 0x00100000UL;
    while ((USART1->SR & USART_SR_TXE) == 0u) {
        if (--guard == 0u)
            return;                     /* 卡不住：宁可丢一个字，也不让 Boot 挂死 */
    }
    USART1->DR = (uint16_t)(uint8_t)c;
}

void Boot_UartStr(const char *s)
{
    if (s == 0)
        return;
    while (*s != '\0')
        Boot_UartPutc(*s++);
}

void Boot_UartHex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;
    Boot_UartStr("0x");
    for (i = 28; i >= 0; i -= 4)
        Boot_UartPutc(hex[(v >> i) & 0xFu]);
}

void Boot_UartDec(uint32_t v)
{
    char buf[11];
    int i = 0;
    if (v == 0u) {
        Boot_UartPutc('0');
        return;
    }
    while (v > 0u && i < 10) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0)
        Boot_UartPutc(buf[--i]);
}
