#include "FreeRTOS.h"
#include "task.h"
#include "sys_usart.h"

/* ================================================================
 *  freertos_hooks.c —— FreeRTOS 钩子函数实现
 * ================================================================
 *  当 FreeRTOSConfig.h 里打开了对应开关，内核在"出事"时会调用
 *  这里的两把钩子；本实现把原因从串口打出来，再用死循环把现场
 *  "停住"——方便跟着串口信息定位问题。
 *
 *  提醒 :
 *    ① 报警串口默认 USART1——可改下面的 RTOS_HOOK_USART 宏；
 *    ② 该串口若未初始化，提示会被静默跳过（不会卡死——这是
 *       SYS_USART_SendByte 的"未初始化直接返回"保护）；
 *    ③ 调试时可直接在两处 for(;;) 上打断点，查看调用栈。
 * ================================================================ */
/* 报警串口（换成你实际使用的串口编号即可） */
#define RTOS_HOOK_USART   SYS_USART_1

/* 栈溢出钩子：某任务栈不够用（configCHECK_FOR_STACK_OVERFLOW=2 时触发）
 * 常见原因：任务里定义了大数组/深层递归；增大创建任务时的栈深度即可 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;

    /* 字符串只能用 ASCII（AC5 对 UTF-8 中文字符串解析有问题） */
    SYS_USART_SendLine(RTOS_HOOK_USART, "[FreeRTOS] !! STACK OVERFLOW, task:");
    SYS_USART_SendLine(RTOS_HOOK_USART, (pcTaskName != 0) ? pcTaskName : "(unnamed)");

    taskDISABLE_INTERRUPTS();
    for (;;);
}

/* 堆不足钩子：pvPortMalloc 失败（configTOTAL_HEAP_SIZE 太小 /
 * 创建任务后忘记删除导致耗尽）——调大堆或排查泄漏 */
void vApplicationMallocFailedHook(void)
{
    SYS_USART_SendLine(RTOS_HOOK_USART, "[FreeRTOS] !! MALLOC FAILED: enlarge configTOTAL_HEAP_SIZE");

    taskDISABLE_INTERRUPTS();
    for (;;);
}
