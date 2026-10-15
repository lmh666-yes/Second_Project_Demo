#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ================================================================
 *  FreeRTOSConfig.h —— FreeRTOS 内核配置文件（"标准模板"工程专用）
 * ================================================================
 *  适配 : STM32F407ZET6 @168MHz / ARM Compiler 5 / heap_4
 *         FreeRTOS-Kernel V10.4.6（来源与许可见 ../LICENSE.md）
 *
 *  学习提示 : FreeRTOS 的"可裁剪性"就体现在这个文件——想要哪个
 *  功能（互斥量/计数信号量/软件定时器…）就把对应宏置 1；
 *  用不到的置 0，内核代码就不会编进去（省 Flash/RAM）。
 * ================================================================ */

/* ---------------- 基础调度 ---------------- */
#define configUSE_PREEMPTION                    1    /* 抢占式调度（核心机制） */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0    /* 通用选任务算法 */
#define configUSE_TICKLESS_IDLE                 0    /* 不用低功耗节拍停摆 */
/* configCPU_CLOCK_HZ 与 sys_clock 的运行时切频无联动：
 *    上 RTOS 后不要调用 SYS_CLK_Switch 切到低速档，否则节拍频率
 *    名义值不变、实际变慢（真要切频需同步改本值后重建） */
#define configCPU_CLOCK_HZ                      (168000000UL)          /* 主频 */
#define configTICK_RATE_HZ                      ((TickType_t)1000)     /* 节拍 1ms */
#define configMAX_PRIORITIES                    8    /* 优先级档数：0~7，大 = 高 */
#define configMINIMAL_STACK_SIZE                ((unsigned short)128)  /* 空闲任务栈(字) */
#define configMAX_TASK_NAME_LEN                 16   /* 任务名最大字符数 */
#define configUSE_16_BIT_TICKS                  0    /* 32 位节拍计数 */
#define configIDLE_SHOULD_YIELD                 1    /* 空闲任务礼让同优先任务 */
#define configUSE_TIME_SLICING                  1    /* 同优先级时间片轮转 */

/* ---------------- 内存管理（heap_4.c） ---------------- */
#define configSUPPORT_STATIC_ALLOCATION         0    /* 只用动态分配，入门简单 */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ((size_t)(30 * 1024))  /* 堆 30KB */
#define configAPPLICATION_ALLOCATED_HEAP        0    /* 堆由 heap_4.c 定义 */

/* ---------------- 同步与通信开关 ---------------- */
#define configUSE_MUTEXES                       1    /* 互斥量 */
#define configUSE_RECURSIVE_MUTEXES             1    /* 递归互斥量 */
#define configUSE_COUNTING_SEMAPHORES           1    /* 计数信号量 */
#define configUSE_QUEUE_SETS                    0    /* 队列集（进阶再学） */
#define configQUEUE_REGISTRY_SIZE               8    /* 队列调试登记表容量 */

/* ---------------- 软件定时器 ---------------- */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               3    /* 定时器服务任务优先级 */
#define configTIMER_QUEUE_LENGTH                10   /* 定时器命令队列长度 */
#define configTIMER_TASK_STACK_DEPTH            128

/* ---------------- 钩子函数（实现见 freertos_hooks.c） ---------------- */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1    /* 堆不足时报警 */
#define configCHECK_FOR_STACK_OVERFLOW          2    /* 栈溢出检查（含模式串比对） */

/* ---------------- 断言（调试利器） ---------------- */
#define configASSERT(x) \
    if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;); }

/* ---------------- 中断优先级（与 NVIC 分组强相关） ----------------
 * Cortex-M 上"数值越小 = 优先级越高"。
 *    · configLIBRARY_LOWEST_INTERRUPT_PRIORITY(15)：
 *      内核/节拍所在的最低档（数值最大、优先级最低）；
 *    · configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5)：
 *      "能调用 FreeRTOS 接口的最高优先级"——
 *      凡是 ISR 里要调用 xxxFromISR 系列接口，其中断优先级
 *      数值必须 ≥ 5，否则会破坏内核临界区（configASSERT 会抓）。
 *  与库模块配合：sys_usart / sys_tim / sys_exti / sys_rtc / sys_dma /
 *    sys_can 等模块的 SYS_XXX_IRQ_PRE_PRIO 实际都配成 5，正好等于
 *    configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY，所以可以直接在这些
 *    ISR 里调用 xxxFromISR 接口，不需要改这些优先级宏。
 *  NVIC 分组：必须用 NVIC_PriorityGroup_4（见 sys_nvic.h，4 位全做抢占，
 *    共 16 档，FreeRTOS 推荐）。若改成 Group_2（2 位抢占 + 2 位子优先级），
 *    PRE=5 会被截成 2 位变成 1，IPR=0x10 < BASEPRI(0x50)，所有 ISR 都会越过
 *    内核的 syscall 上限，在 ISR 里调 FromISR 接口会破坏临界区，configASSERT
 *    会抓。分组由 SYS_NVIC_Init() 设置，板1 在 main.c:989、板2 在 main.c:1581
 *    调用，这两句不要删。 */
#define configPRIO_BITS                         4   /* 即 CMSIS 宏 __NVIC_PRIO_BITS（F4 为 4 位） */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY       15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ---------------- 内核异常处理函数改名 ----------------
 * 说明 : FreeRTOS 端口层提供 SVC / PendSV / SysTick 三个汇编入口，
 *        名字映射成启动文件里的标准向量名（否则链接不上）。
 *        sys_tick.c 里同名函数是"弱定义"，会被这里自动顶替——
 *        用 FreeRTOS 时 sys_tick 的延时函数即失效，改用 vTaskDelay。 */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

/* ---------------- 功能裁剪（各 API 是否编进内核） ---------------- */
#define INCLUDE_vTaskPrioritySet             1
#define INCLUDE_uxTaskPriorityGet            1
#define INCLUDE_vTaskDelete                  1
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_vTaskDelayUntil              1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_xTaskGetSchedulerState       1
#define INCLUDE_xTaskGetCurrentTaskHandle    1
#define INCLUDE_uxTaskGetStackHighWaterMark  1   /* 查"任务栈还余多少" */
#define INCLUDE_xTimerPendFunctionCall       1   /* 中断里"留言"给定时器任务执行 */

#endif /* FREERTOS_CONFIG_H */
