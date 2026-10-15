#ifndef BOOT_UART_H
#define BOOT_UART_H

/* ============================================================
 *  boot_uart —— Bootloader 的最小串口输出（USART1：PA9=TX / PA10=RX，接 CH340 跳线）
 *
 *  为什么直接写寄存器：
 *    Bootloader 只有 16KB，不想为了一句日志把 SPL 的 usart/gpio/rcc 源文件全拖进来。
 *    这里只做"能往外打字"这一件事，二十来行就够，且不依赖任何 SPL 编译单元。
 *
 *  前提：进 main 之前启动文件已调 SystemInit() 把主频配到 168MHz（APB2 = 84MHz）；
 *        所以 115200 的 BRR 直接按 84MHz 算。换主频要改下面的 BOOT_PCLK2_HZ。
 * ============================================================ */

#include <stdint.h>

void Boot_UartInit(void);
void Boot_UartPutc(char c);
void Boot_UartStr(const char *s);          /* 输出字符串（\r\n 自己带上） */
void Boot_UartHex32(uint32_t v);           /* 输出 8 位十六进制（打地址/大小用） */
void Boot_UartDec(uint32_t v);             /* 输出十进制 */

#endif /* BOOT_UART_H */
