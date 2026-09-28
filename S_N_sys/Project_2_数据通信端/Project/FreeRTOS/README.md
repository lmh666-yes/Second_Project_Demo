# FreeRTOS 内核（工程内置源码）

| 项目 | 说明 |
|---|---|
| 版本 | **FreeRTOS-Kernel V10.4.6**（推荐版本；`tskKERNEL_VERSION_NUMBER` 可查） |
| 来源 | [FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) 标签 **V10.4.6** |
| 许可 | MIT（原文见本目录 `LICENSE.md`，升级/发布时请保留） |
| 适配 | STM32F407ZET6 @168MHz + ARM Compiler 5（AC5） |
| 布局 | 教程式三文件夹：`src`（内核 .c）/ `inc`（内核头文件）/ `port`（移植层 + 内存管理 + 工程配置） |

## 目录结构

```
FreeRTOS/
├── LICENSE.md                  # MIT 许可原文（内核原样保留）
├── src/                        # 内核 .c（5 个，已加入工程编译组）
│   ├── tasks.c                 # 任务管理：创建 / 调度 / 延时
│   ├── queue.c                 # 队列（信号量 / 互斥量共用这套实现）
│   ├── list.c                  # 内核链表（调度器的基础数据结构）
│   ├── timers.c                # 软件定时器
│   └── event_groups.c          # 事件组
├── inc/                        # 内核头文件（原样保留：FreeRTOS.h / task.h /
│                               #   queue.h / semphr.h / timers.h / portable.h …）
└── port/                       # 移植层 + 内存管理 + 本工程配置
    ├── port.c                  # Cortex-M4F 端口（AC5）：上下文切换的汇编在这
    ├── portmacro.h             # 端口宏（基础类型 / 临界区 / 开关中断）
    ├── heap_4.c                # 动态内存方案 4（带相邻空闲块合并）
    ├── FreeRTOSConfig.h        # 本工程的功能裁剪与参数配置（重点看这个）
    └── freertos_hooks.c        # 栈溢出 / 堆不足钩子实现（报警 + 停机）
```

## 为什么 `inc/` 的头文件比 `src/` 的 .c 多？（头文件 ↔ 实现对照）

不是文件丢了——**头文件是内核"全部功能的目录"（声明 / 宏 / 类型），实现只保留用得到的那部分**。
本目录 18 个头文件与 5 个已编译 .c 的对应关系：

| 头文件 | 对应实现 | 说明 |
|---|---|---|
| `task.h` | `src/tasks.c` ✅ | 任务管理（创建 / 调度 / 延时） |
| `queue.h` | `src/queue.c` ✅ | 队列 |
| `list.h` | `src/list.c` ✅ | 链表（调度器内部数据结构） |
| `timers.h` | `src/timers.c` ✅ | 软件定时器 |
| `event_groups.h` | `src/event_groups.c` ✅ | 事件组 |
| `semphr.h` | **无独立 .c** | 信号量 / 互斥量全是对 `queue.c` 的宏与内联包装——"头文件本身就是接口实现" |
| `stream_buffer.h` | `stream_buffer.c` ❌ 未加入编译 | 流缓冲功能未启用；要用就把 .c 加入编译组并打开对应配置宏 |
| `message_buffer.h` | 同上 ❌ | 消息缓冲（基于流缓冲封装） |
| `croutine.h` | `croutine.c` ❌ 未加入编译 | 协程（老功能，很少使用） |
| `FreeRTOS.h` / `portable.h` / `projdefs.h` / `atomic.h` / `mpu_wrappers.h` / `mpu_prototypes.h` / `stack_macros.h` / `StackMacros.h` / `deprecated_definitions.h` | **无 .c（纯声明 / 纯宏）** | 被上面 5 个 .c 共用的基础定义、端口接口与内部宏 |

一句话三个原因：

1. **通用内核按"全功能"配头文件，按"本项目用得到"配 .c**（哪些功能编译进来由 `FreeRTOSConfig.h` 决定）；
2. **"没有 .c 的头文件"是 C 工程的常态**（`semphr.h` 全内联、`projdefs.h` 全是宏、`mpu_*.h` 只是另一套端口的声明）；
3. **`inc/` 保持上游原样不裁剪**，升级替换和对照原版源码都方便。

> 对照：本工程的 `FWLIB`（自己写的库）是 21 个 .h + 21 个 .c 一一对应——因为每个模块都实现了；
> 通用内核"头多实现少"是另一套组织方式（可裁剪），两者都正常，场景不同。

## 使用方式

- 工程里已把 `src/` 的 5 个内核 .c 与 `port/` 的 `port.c` / `heap_4.c` /
  `freertos_hooks.c` 加入编译组；头文件路径已配好两条：
  `.\FreeRTOS\inc`（内核头）与 `.\FreeRTOS\port`（端口宏 + FreeRTOSConfig.h）——
  直接 `#include "FreeRTOS.h"` / `#include "task.h"` 即可使用。
- **内核源码保持原样不改**：升级只替换 `src/` 与 `inc/` 下的同名文件；
  `port/` 里是本工程的移植适配与配置。

## 升级版本的方法（换内核版时照做）

1. 从 [FreeRTOS-Kernel 仓库](https://github.com/FreeRTOS/FreeRTOS-Kernel) 下载目标版本
   （推荐当前用的 **V10.4.6**）；
2. `src/` ← 替换内核 .c（若新版本新增了内核功能，还要把对应 .c 一起加进来并加入编译组）；
3. `inc/` ← 替换全部内核头文件；
4. `port/` ← 只更新 `port.c / portmacro.h / heap_4.c`（从对应发行包的
   `portable/RVDS/ARM_CM4F` 与 `portable/MemMang` 取）；`FreeRTOSConfig.h`、
   `freertos_hooks.c` 是本工程的，**不要动**；
5. 全量编译（应 0 错 0 警）；重点检查 `FreeRTOSConfig.h` 是否有被新版本弃用的宏。

## 与库模块的相处规则（重点）

- SysTick 归 FreeRTOS（`sys_tick.c` 的 `SysTick_Handler` 为弱定义，被自动顶替），
  延时改用 `vTaskDelay`；驱动级毫秒时序可用 `Delay_ms_DWT`（见主 README 5.1）;
- ISR 里只允许调用 `xxxFromISR` 系列接口，且该中断优先级数值要
  ≥ `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`（=5）；
- NVIC 分组建议 `NVIC_PriorityGroup_4`（FreeRTOS 官方推荐）；
- **上 RTOS 后不要调用 `SYS_CLK_Switch()` 切低速档**：节拍按
  `configCPU_CLOCK_HZ` 固定换算，切频后实际节拍失准（见 FreeRTOSConfig.h 注释）；
- 内核三入口 `SVC_Handler / PendSV_Handler / SysTick_Handler` 由 `port/port.c`
  提供（向量归属规则见主 README 5.22）；
- 钩子（栈溢出 / 堆不足）会经串口报警后停机；报警串口可在
  `port/freertos_hooks.c` 的 `RTOS_HOOK_USART` 处修改。
- 更多用法示例见工程根目录 `README.md` 的 **FreeRTOS 基础** 章节。

## 深入阅读指引（想学内核实现时从哪看起）

| 想搞懂什么 | 去哪读 | 入口提示 |
|---|---|---|
| 任务调度与上下文切换 | `src/tasks.c` + `port/port.c` | 先看调度器启动 `vTaskStartScheduler`；"切上下文"的汇编在 port.c 的 `xPortPendSVHandler` |
| 队列 / 信号量 / 互斥量 | `src/queue.c` | 三者共用一套队列实现——从 `xQueueGenericSend` 看起 |
| 动态内存（malloc 替身） | `port/heap_4.c` | 从 `pvPortMalloc` 读起（空闲块链表 + 相邻合并） |
| 软件定时器 | `src/timers.c` | 定时器服务任务入口 `prvTimerTask` |
| 内核配置怎么改 | `port/FreeRTOSConfig.h` | 每个宏都有中文注释；要/不要某功能就改这里 |
| 栈溢出报警在哪 | `port/freertos_hooks.c` | 两个钩子函数自带说明与串口报警 |

> 内核源码保持原样不改（升级直接替换 `src/` + `inc/` 同名文件）；找符号最快的办法：全局搜索函数名，从函数上方的注释读起。
