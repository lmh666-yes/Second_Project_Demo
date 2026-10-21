/* data_build_b2.js —— 板2 项目引导（手写、非生成物） */
const STD_BUILD_B2 = [
  /* ======================= 板2 · 阶段 0 ======================= */
  {
    n: "🧭 板2 · 阶段 0｜先拿地图：板2 的角色与验收习惯",
    m: "板2 项目引导",
    _b: "板2",
    d: "动手前先搞清楚：板2 在整条链路上干什么、哪些必须你写、每一步怎么算过。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：无（这是起点）。\n本阶段产出：数据流图：LoRa 收 → 转发 → 缓存/上云 → 补传；以及「你写什么」的边界。\n下一阶段怎么用：阶段 1 用它在工程里建 main.c 骨架。\n\n板2 是 STM32F407ZGT6 的数据通信端，工作只有一条主线：LoRa 收板1 的 20 字节环境帧 → 经 USART2 原样转发给上位机（Qt）→ 同时把「时间戳 + 整帧」写进 W25Q128 缓存；网络恢复后按上位机命令把历史帧补传回去。你要自己设计的是 main.c 与 FreeRTOS 任务划分（谁收、谁转发、谁收命令、谁补传、谁监控），FWLIB 里的驱动直接调用、不要改，TFT 界面由 AI 完成。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。\n本卡各步落点：\n· 第2步 板2 的角色与数据流 → 无代码；只确认 vTaskLoraRx → vTaskForward 这个接口将来落在 ④ 任务函数区。",
    k: "板2 分工 阶段地图 路线 验收习惯 项目引导 main.c FreeRTOS 任务划分 数据通信端 LoRa 转发 断网补传",
    f: [
      [
        "分工边界：你写什么、已经给你什么",
        "边界不清最费时间——写着写着跑去改库文件，后面 AI 给的修正就对不上了。",
        "记住三句话：① 你写 main.c 与任务函数（收 LoRa / 转发 / 收命令 / 补传 / 监控的划分与调用顺序）；② FWLIB 的驱动直接调用，不改，涉及 LoRa 的走 lora_e22、联网走 esp8266 与 mqtt、缓存走 w25qxx_log、屏幕走 lcd、触摸走 xpt2046；③ 显示与上位机 UI（板2 的 TFT 界面、Qt 界面）由 AI 完成。",
        "",
        "你能一句话说清：我要写 main.c + 任务函数，不改 FWLIB，不写界面。",
        "把「顺手改一下库里这个函数」当成坏习惯：库一改，双板之间的 20 字节帧格式与命令字口径就可能对不上，板1 那边发的帧板2 认不出来。",
        ""
      ],
      [
        "板2 的角色：一条帧从哪来、到哪去",
        "任务划分就是照这张数据流图切的；图没画清，任务就会多切或少切。",
        "整条链只有三个口：① LoRa 在 USART6（9600）；② 上位机在 USART2（115200）；③ 云端在 USART3 上的 ESP8266 + MQTT。板1 组好的 20 字节帧从 ① 进来，分别流向 ② 和 W25Q128；上位机从 ② 下来的命令决定 ③ 开不开、缓存怎么补传。",
        "LoRa(USART6,9600) --20B帧--> vTaskLoraRx --> 原样转发 --> vTaskForward --> USART2(115200) --> 上位机\n                                          \\--> 加时间戳 --> W25Q128 缓存（断网/开关打开时）\n上位机 --0x16 启停--> vTaskCmdRx --> vTaskReplay --0x15 历史帧--> USART2 --> 上位机",
        "你能在白板上画出这条链，并说出每一条断掉会怎样：LoRa 断 = 一条数据都收不到；USART2 断 = 上位机页面全空；网断 = 还能缓存，只是暂时上报不了云端。",
        "转发必须是「原样 20 字节」，不要重新组帧——重新组帧时间戳会变，上位机按时间戳对历史就对不上了。",
        "SYS_FRAME_Send / LORA_E22_Recv / SYS_FRAME_CMD_ENV"
      ],
      [
        "板2 的阶段地图（阶段 1~9，加上本张地图共 10 张卡）",
        "先看到全程，才知道今天这一步为什么要这么写。",
        "阶段 1 开工准备与工程骨架 → 阶段 2 时钟、滴答与基础外设 → 阶段 3 FreeRTOS 任务框架 → 阶段 4 LoRa 接收与协议解析 → 阶段 5 上行链路（ESP8266 AT 与 MQTT） → 阶段 6 Flash 缓存与断网补传 → 阶段 7 本地显示与触摸 → 阶段 8 联调验收 → 阶段 9 双通道并行（ESP8266 上云与上位机串口同时跑）。",
        "",
        "你能指着地图说出：现在在第几段、下一段要交什么。",
        "别跳段：没有阶段 4 的收帧与校验，阶段 6 的缓存里存的都是坏帧，补传上去上位机会画出一条锯齿曲线。",
        ""
      ],
      [
        "验收习惯：每一步都要「看得见的证据」",
        "这块板最后要上板实测，凭感觉写出来的代码一定返工。",
        "给每步准备一种证据：LED 亮灭、蜂鸣器响、USART1 打印一行、USART2 上位机收到一帧、MQTT 客户端看到一条消息、Keil 的 0 Error 0 Warning、函数返回码是 0。做完就记进你自己的进度文档。",
        "",
        "每完成一个动作，你能贴出一条证据（打印内容 / 上位机截图 / MQTT 收到的报文 / 现象描述）。",
        "「编译过了」不算证据——那只能证明语法对，不能证明时序与数据对。",
        ""
      ],
      [
        "你的进度文档：板2进度文档.md 只由你写",
        "板2 调试会反复（网连不上、补传重复、触摸坐标反），记录能省下大量重复劳动。",
        "在 D:\\GitHub_Project\\EnvMonitor\\Project_2_数据通信端\\板2进度文档.md 里自己记：今天做了什么、卡在哪、怎么解决的、下一步。每解决一个问题就补一行。",
        "",
        "文档里有至少 3 条「问题 → 原因 → 解决」的记录。",
        "别只记结论不记原因。比如「MQTT 连不上」这件事，原因可能是 WiFi 名写错、可能是 broker 端口不对、也可能是 ESP8266 供电不足，三种原因的修法完全不同。",
        ""
      ]
    ]
  },

  /* ======================= 板2 · 阶段 1 ======================= */
  {
    n: "🧭 板2 · 阶段 1｜开工准备与工程骨架",
    m: "板2 项目引导",
    _b: "板2",
    d: "把工程打开、认准目录、写出第一个能编译能下载的 main.c，并把调试串口与复位原因先打通。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：数据流图（无代码）。\n本阶段产出：能点灯、能打印、能打复位原因的 main.c：① 引用区 + ⑤ 初始化段。\n下一阶段怎么用：阶段 2 往 ⑤ 加外设初始化；骨架不动。\n\n本阶段的产物：① 你能说清工程里每个目录是干什么的；② 你自己的 main.c 能编译（0 Error 0 Warning）、能下载、LED 会按你的意思闪；③ USART1 115200 能打印出你自己写的字，并且每次上电会打印上次的复位原因。做完这三样，后面所有调试都有眼睛了。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。\n本卡各步落点：\n· 第1步 打开工程并认准目录 → 无代码：只确认 000标准模板库.uvprojx 与 main.c 路径。\n· 第2步 最小可编译 main.c → 按 ①~⑤ 五个横幅建骨架，与板1 同一套分区（板1/板2 布局必须一样，方便对照）。\n· 第4步 调试串口 → #include 进 ①；SYS_USART_Init 与打印语句进 ⑤ 初始化段。\n· 第5步 复位原因与故障现场 → #include 进 ①；txt/cause 是局部变量放 ⑤；SYS_FAULT_Init 进 ⑤ 初始化段。\n· 第6步 GPIO_Claim 引脚登记 → #include 进 ①；登记调用跟随对应外设的初始化，放 ⑤ 外设初始化段（谁 init 谁登记）。",
    k: "板2 工程骨架 Keil 打开 000标准模板库.uvprojx 目录 main.c 最小工程 LED 串口打印 115200 复位原因 0 Error 0 Warning",
    f: [
      [
        "打开工程并认准目录",
        "改错目录 = 编译过不了或下载了旧代码，是新手最常见的坑。",
        "用 Keil 打开 D:\\GitHub_Project\\EnvMonitor\\Project_2_数据通信端\\Project\\000标准模板库.uvprojx；在左侧工程树里认出四类分组：main.c（你自己的代码，就在 Project 根目录下）、FWLIB\\src（驱动实现，只读不改）、Startup（启动文件与中断向量）、FreeRTOS（内核与 port）。FreeRTOSConfig.h 在 FreeRTOS\\port 下，不在 Project 根目录。",
        "工程路径：D:\\GitHub_Project\\EnvMonitor\\Project_2_数据通信端\\Project\\000标准模板库.uvprojx\n待你写的文件：D:\\GitHub_Project\\EnvMonitor\\Project_2_数据通信端\\Project\\main.c\n内核配置：D:\\GitHub_Project\\EnvMonitor\\Project_2_数据通信端\\Project\\FreeRTOS\\port\\FreeRTOSConfig.h",
        "Keil 打开后能展开分组看到 FWLIB\\src\\sys_usart.c、lora_e22.c、w25qxx_log.c、lcd.c 等；双击 main.c 能定位到 Project\\main.c。",
        "工程名是 000标准模板库.uvprojx，旁边还有一个 .uvoptx 存的是显示设置（断点、打开的窗口），两者别搞混；不要用批处理清理后就以为代码没了——那只删中间产物。",
        ""
      ],
      [
        "写出最小可编译的 main.c（先只点灯）",
        "先要一个「能跑的基线」，后面每加一块功能都能对比出是谁搞坏的。",
        "把 main.c 清空重写：包含必要头文件，初始化时钟与滴答，然后一个死循环里翻转 LED。先不要加 LoRa、不要加 ESP8266、不要加 FreeRTOS——基线越干净越好。",
        "#include \"stm32f4xx.h\"\n#include \"sys_clock.h\"\n#include \"sys_tick.h\"\n#include \"led.h\"\n\nint main(void)\n{\n    SYS_CLK_ToHighSpeed();     /* 168MHz：HCLK 168 / PCLK1 42 / PCLK2 84 */\n    SYS_TICK_Init();           /* 1ms 滴答，SYS_TICK_Delay_ms 才有意义 */\n    LED_Init();                /* 本板 LED_COUNT = 2：LED0=PF9 LED1=PF10，低电平点亮 */\n\n    while (1) {\n        LED_Toggle(0);\n        SYS_TICK_Delay_ms(500);\n    }\n}",
        "编译 0 Error 0 Warning；下载后 LED0（PF9）每 0.5 秒亮灭一次。",
        "SYS_TICK_Init() 之前不要调用 SYS_TICK_Delay_ms()，否则滴答没起来会直接卡住；板2 的 LED_ACTIVE_LOW = 1、LED_COUNT = 2，只有 id 0 和 1 合法，LED_Toggle(2) 是空操作、不报错也不亮，容易让人以为灯坏了。",
        "SYS_CLK_ToHighSpeed / SYS_TICK_Init / LED_Init / LED_Toggle"
      ],
      [
        "把「0 Error 0 Warning」变成习惯",
        "警告里常常藏着真错误（比如隐式声明函数 = 参数类型全错）。",
        "每次编译后看 Build Output 最后两行：0 Error(s), 0 Warning(s)。有警告就去改到自己看懂为止；不确定某个警告什么意思，把原文贴进工具搜。",
        "",
        "Build Output 最后一行是 0 Error(s), 0 Warning(s)。",
        "板2 工程里 FWLIB 驱动多，最容易出的警告是「函数隐式声明」——通常是你忘了 #include 对应的头文件，编译能过但运行时参数全错位。",
        ""
      ],
      [
        "打通调试串口打印（USART1 / 115200）",
        "没有打印，你只能靠猜；有了打印，每一步都能自证。",
        "初始化 USART1（PA9 / PA10，115200），用 SYS_USART_SendLine 或 SYS_USART_Printf 打印启动横幅；以后每个模块初始化后都打一行「xxx ok」。板2 的调试口固定用 USART1，上位机口是 USART2，两个不要混。",
        "#include \"sys_usart.h\"\n\nSYS_USART_Init(SYS_USART_1, 115200);\nSYS_USART_SendLine(SYS_USART_1, \"=== board2 boot ===\");\nSYS_USART_Printf(SYS_USART_1, \"tick=%u\\r\\n\", (unsigned)SYS_TICK_GetTick());",
        "电脑串口助手 115200 8N1 能看到 “=== board2 boot ===”；复位一次就重新打印一次。",
        "USB 转 TTL 的 TX 接板子 PA10（USART1_RX）、RX 接 PA9（USART1_TX），地必须共；打印乱码先查波特率，再查是不是接成了 USART2（那是给上位机的口）。",
        "SYS_USART_Init / SYS_USART_SendLine / SYS_USART_Printf / SYS_USART_1"
      ],
      [
        "把复位原因和故障现场打出来",
        "板2 会长时间跑，出一次死机如果不留证据，你只能靠重下程序再复现。",
        "上电第一件事：SYS_WDG_ResetCause() 取复位原因，用 SYS_WDG_ResetCauseDecode 翻译成文字打印，再 SYS_WDG_ClearResetFlags() 清掉，避免下次误读。同时调 SYS_FAULT_Init() 装上 HardFault 现场记录，之后可用 SYS_FAULT_Report 把现场打成一行字。",
        "#include \"sys_wdg.h\"\n#include \"sys_fault.h\"\n\nchar txt[64];\nuint32_t cause;\n\nSYS_FAULT_Init();\ncause = SYS_WDG_ResetCause();\nif (SYS_WDG_ResetCauseDecode(cause, txt, sizeof(txt)) != 0U) {\n    SYS_USART_Printf(SYS_USART_1, \"reset: %s\\r\\n\", txt);\n}\nSYS_WDG_ClearResetFlags();",
        "按下复位键 → 打印“上电/掉电复位”；拔掉再插上 → 打印的内容与按复位键不同；故意让程序除零 → 复位后能打印出 HardFault 字样。",
        "SYS_WDG_ResetCauseDecode 返回 0 表示「没翻译出来」，此时 txt 里可能是空的，不要直接打印来当结论；SYS_WDG_ClearResetFlags 一定要调，否则第二次上电看到的还是上一次的原因。",
        "SYS_WDG_ResetCause / SYS_WDG_ResetCauseDecode / SYS_WDG_ClearResetFlags / SYS_FAULT_Init / SYS_FAULT_Report"
      ],
      [
        "引脚冲突先登记：gpio_core 的 GPIO_Claim",
        "板2 引脚复用比板1 密得多（PB15 既是 LCD 背光、PB14 是 Flash 片选、PE2~PE4 在别的板上是别的功能），冲突了现象是「两个外设都半死不活」。",
        "每用一个新引脚前，用 GPIO_Claim(port, pin, \"谁用的\") 登记一次；全部初始化完之后调 GPIO_ClaimDump() 把登记表整表打出来看有没有重复，并用 GPIO_ClaimConflictCount() 数冲突条数。",
        "#include \"gpio_core.h\"\n\nif (GPIO_Claim(GPIOB, GPIO_Pin_15, \"LCD_BL\") == 0U) {\n    SYS_USART_SendLine(SYS_USART_1, \"PB15 claim failed\\r\\n\");\n}\nSYS_USART_Printf(SYS_USART_1, \"claim=%u conflict=%u lost=%u\\r\\n\",\n                (unsigned)GPIO_ClaimCount(),\n                (unsigned)GPIO_ClaimConflictCount(),\n                (unsigned)GPIO_ClaimLost());",
        "GPIO_ClaimConflictCount() = 0；GPIO_ClaimLost() = 0；GPIO_ClaimDump() 的输出里每个引脚只出现一次。",
        "GPIO_ClaimDump 内部用 printf 输出，必须先让串口重定向可用（或改用 SYS_USART_Printf 自己打），否则你看不到任何东西还以为没冲突；登记表有容量上限，表满后 GPIO_ClaimLost 会增长，这个数不是 0 就说明你的登记白做了。",
        "GPIO_Claim / GPIO_ClaimDump / GPIO_ClaimCount / GPIO_ClaimConflictCount / GPIO_ClaimLost"
      ]
    ]
  },

  /* ======================= 板2 · 阶段 2 ======================= */
  {
    n: "⏱ 板2 · 阶段 2｜时钟、滴答与基础外设",
    m: "板2 项目引导",
    _b: "板2",
    d: "把板子的「时间」和「手脚」都配好：168MHz 时钟、1ms 滴答、LED/按键/蜂鸣器、两个串口，以及 TFT 背光。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：main.c 骨架 + 调试串口。\n本阶段产出：时钟、两路串口（USART1 调试 / USART2 上位机）、LCD 背光、按键、蜂鸣器。\n下一阶段怎么用：阶段 3 原样保留这些初始化，上 RTOS 前删 SYS_TICK_Init。\n\n这一阶段结束后你手里有一套可复用的基础件：LED 报状态、按键做输入、蜂鸣器做提示、USART1 做调试出口、USART2 当上位机通道、TFT 背光能开关。后面 LoRa 与 MQTT 的调试全靠这几个口。所有引脚都按工程里的宏来，不要背错。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。\n本卡各步落点：\n· 第1步 时钟 → SYS_CLK_ToHighSpeed() 是 ⑤ main 第一行；hclk/pclk1/pclk2 在 main 开头一起声明。\n· 第2步 滴答 → t0 局部放 ⑤；上 FreeRTOS 后 SYS_TICK_Init / Delay 整段删。\n· 第3步 LED → LED_Init() 进 ⑤；BlinkUpdate 要周期调，最终移 ④ 任务循环。\n· 第4步 按键 → k 局部放 ④ 按键任务（阶段 2 临时验证才放 ⑤）；KEY_Init 进 ⑤。\n· 第5步 两个串口分工 → #include 进 ①；两条 SYS_USART_Init/InitRxIT 都进 ⑤（USART1 调试、USART2 上位机）。\n· 第6步 蜂鸣器 → BEEP_Init 进 ⑤；提示音在 ④ 任务里。\n· 第7步 TFT 背光 → #include 进 ①；LCD_Init/BackLight 进 ⑤ 初始化段（你现在已有这一块）。",
    k: "板2 时钟 168MHz PCLK 42 84 滴答 延时 LED PF9 PF10 按键 PE2 PE3 PE4 PA0 蜂鸣器 USART1 PA9 PA10 USART2 PA2 PA3 中断接收 TFT 背光 PB15 LCD_BackLight",
    f: [
      [
        "时钟：确认真的跑在 168MHz",
        "外设频率决定波特率、定时器周期、SPI 速率，不确认就会「到处差一点」。",
        "调 SYS_CLK_ToHighSpeed()，再用 SYS_CLK_GetBusFreq 把三路总线频率打出来核对：HCLK 168 MHz、PCLK1 42 MHz、PCLK2 84 MHz。",
        "uint32_t hclk, pclk1, pclk2;\nSYS_CLK_ToHighSpeed();\nSYS_CLK_GetBusFreq(&hclk, &pclk1, &pclk2);\nSYS_USART_Printf(SYS_USART_1, \"HCLK=%u PCLK1=%u PCLK2=%u\\r\\n\",\n                (unsigned)(hclk / 1000000), (unsigned)(pclk1 / 1000000), (unsigned)(pclk2 / 1000000));",
        "打印 HCLK=168 PCLK1=42 PCLK2=84；改错分频立刻能看出差异。",
        "上限就是这三个数，超了外设会不稳；晶振不是 8MHz 时 SYS_CLK_ToHighSpeed 会返回错误码而不是静默降频，记得看返回值。",
        "SYS_CLK_ToHighSpeed / SYS_CLK_GetBusFreq / SYS_CLK_GetFreq"
      ],
      [
        "滴答与精确延时（上 FreeRTOS 之前）",
        "后面所有「等一会儿」「超时判断」都建立在滴答上。",
        "SYS_TICK_Init() 起 1ms 滴答，用 SYS_TICK_GetTick() 记时间、SYS_TICK_Elapsed() 算间隔、SYS_TICK_Timeout() 做超时、SYS_TICK_Every() 做周期触发。微秒级短延时用 SYS_TICK_Delay_us() 或 delay_us()。",
        "uint32_t t0 = SYS_TICK_GetTick();\nSYS_TICK_Delay_ms(300);\nSYS_USART_Printf(SYS_USART_1, \"elapsed=%u ms\\r\\n\",\n                (unsigned)SYS_TICK_Elapsed(t0));   /* 期望 300 左右 */",
        "打印 elapsed 在 298~302 之间（±1 滴答属正常）。",
        "SYS_TICK_Delay_ms 是忙等（会占住 CPU），进了 FreeRTOS 后任务里要改用 vTaskDelay；delay_us 依赖主频，主频没配对会整体偏差。",
        "SYS_TICK_Init / SYS_TICK_GetTick / SYS_TICK_Elapsed / SYS_TICK_Delay_us / delay_us"
      ],
      [
        "LED：把「看不见的状态」变成看得见",
        "板子跑起来却没现象时，LED 是唯一不需要电脑的调试手段。",
        "板2 只有 2 颗灯（LED0 = PF9、LED1 = PF10，低电平点亮，LED_COUNT = 2）。先 LED_Init()，用 LED_On/Off/Toggle 做单灯；LED_OnTo(n) 点亮前 n 颗；LED_ShowHex(v) 显示一个字节的低两位状态码。闪烁类接口要点亮后每 1ms 调一次 LED_BlinkUpdate()。",
        "LED_Init();\nLED_ShowHex(0x01);          /* 只 LED0 亮 */\nSYS_TICK_Delay_ms(800);\nLED_BlinkStart(0, 200, 500);   /* 200ms 周期、50% 占空比 */\nLED_BlinkUpdate();             /* 之后每 1ms 调一次 */",
        "LED_ShowHex(0x01) 时 LED0 亮而 LED1 灭；LED_BlinkStart 后 LED0 以 200ms 周期闪。",
        "LED_ShowHex 是「低位对应 LED0」，看到的亮法可能和你直觉相反；BlinkUpdate 漏调就不会闪（它不是中断驱动的）；只有 2 颗灯，别照抄板1 的 4 颗灯去写 LED_Toggle(3)。",
        "LED_Init / LED_On / LED_Off / LED_Toggle / LED_ShowHex / LED_OnTo / LED_BlinkStart / LED_BlinkUpdate"
      ],
      [
        "按键：4 个键 + 消抖 + 长按",
        "按键是最容易「看起来能用、实际会连发」的外设，也是你现场切转发开关的入口。",
        "板2 四键：id 0 = PE4、id 1 = PE3、id 2 = PE2（按下为低），id 3 = PA0（本板按下为高，极性由库内部适配）。KEY_Init() 后：KEY_Read(id) 读即时电平、KEY_Scan() 扫一次按下事件、KEY_ReadAll() 一次读 4 个位、KEY_WaitPress() 阻塞等按键、KEY_LongPress(id, ms) 判长按。",
        "uint8_t k;\nKEY_Init();\nwhile (1) {\n    if ((k = KEY_Scan()) != KEY_NONE) {   /* KEY_NONE = 0xFF */\n        SYS_USART_Printf(SYS_USART_1, \"key id=%u\\r\\n\", (unsigned)k);\n        BEEP_KeySound();\n    }\n    SYS_TICK_Delay_ms(5);\n}",
        "按哪个键就打印哪个 id（0~3），且只打印一次不连发；按住 1 秒用 KEY_LongPress(id, 1000) 能判成长按。",
        "KEY_Scan 需要被周期性调用（推荐 5~10ms 一次），放在超长延时里会漏按键；id 3（PA0）在本板是高电平按下，自己写读电平判断时最容易反，一律走 KEY_Scan / KEY_Read。",
        "KEY_Init / KEY_Scan / KEY_Read / KEY_ReadAll / KEY_LongPress / KEY_NONE"
      ],
      [
        "两个串口分工：USART1 调试、USART2 给上位机",
        "板2 同时要跟电脑、跟 Qt 上位机说话，口搞混了会一路查错方向。",
        "USART1（PA9 / PA10）留给自己打印；USART2（PA2 / PA3，115200）是给上位机（Qt）的数据通道。接收统一用中断模式：SYS_USART_InitRxIT(id, baud) 打开接收中断，主循环或任务里用 SYS_USART_Available(id) 拿字节数，再 SYS_USART_RxRead(id) 逐字节取或 SYS_USART_ReadBuf 批量取。",
        "int c;\nSYS_USART_Init(SYS_USART_1, 115200);        /* 调试口 */\nSYS_USART_InitRxIT(SYS_USART_2, 115200);    /* 上位机口，收命令 */\nwhile (1) {\n    while ((c = SYS_USART_RxRead(SYS_USART_2)) >= 0) {\n        SYS_USART_Printf(SYS_USART_1, \"qt rx %02X\\r\\n\", (unsigned)(c & 0xFF));\n    }\n    SYS_TICK_Delay_ms(1);\n}",
        "串口助手接 USART2、发什么字节，USART1 就打一行 “qt rx XX”；连发几百字节不丢。",
        "SYS_USART_RxRead 返回 int 且无数据时返回负值——不要用 uint8_t 接，否则 -1 变成 0xFF 被当成真数据；USART2 是给上位机的，别再拿它当打印口用，否则上位机页面会被你的调试文字污染。",
        "SYS_USART_Init / SYS_USART_InitRxIT / SYS_USART_Available / SYS_USART_RxRead / SYS_USART_ReadBuf / SYS_USART_RxFlush / SYS_USART_2"
      ],
      [
        "蜂鸣器：加一路「听得见」的提示",
        "调试时你常常离板子很远，声音比灯光更容易察觉；补传开始/结束、网络掉线都值得响一声。",
        "先 BEEP_Init()。简单响几声用 BEEP_Beep(times)，控制节奏用 BEEP_BeepEx(times, on_ms, off_ms)，按键音用 BEEP_KeySound()。想让它边跑边响用 BEEP_AsyncStart()，同样要每 1ms 调 BEEP_Update()；不想要了用 BEEP_AsyncStop() 立即静音。",
        "BEEP_Init();\nBEEP_Beep(2);                       /* 响两声 */\nBEEP_BeepEx(1, 100, 100);",
        "蜂鸣器按你给的次数与节奏响；无源蜂鸣器只会「咔」一声是正常的（需要 PWM 才有音调）。",
        "BEEP_AsyncStart 之后忘记调 BEEP_Update 会一直响；板2 串口、Flash、屏幕都在忙，别把 BEEP_Beep 放在中断或高优先级任务里做长音——它是阻塞等时间的。",
        "BEEP_Init / BEEP_Beep / BEEP_BeepEx / BEEP_KeySound / BEEP_AsyncStart / BEEP_Update / BEEP_AsyncStop"
      ],
      [
        "TFT 背光 BL = PB15：先把屏点「亮」",
        "屏不亮有两大原因——背光没开、初始化没跑通；先把背光单独验掉，后面查 LCD 就少一个变量。",
        "背光是一个普通 GPIO（PB15），由 LCD_BackLight(on) 控制。这一步不要初始化 FSMC、不要开屏，只验证背光这一路：上电开背光、按一下按键关背光，看屏幕从亮到暗。",
        "#include \"lcd.h\"\n\nLCD_BackLight(1);       /* PB15 = 1，背光亮 */\nBEEP_KeySound();\nKEY_WaitPress();\nLCD_BackLight(0);       /* 背光灭，屏幕内容还在（只是看不见）*/\n\nSYS_USART_Printf(SYS_USART_1, \"BL test done, w=%u h=%u\\r\\n\",\n                (unsigned)LCD_GetWidth(), (unsigned)LCD_GetHeight());",
        "按一下键屏幕明显变暗但还在（背光灭、像素保留），再按一下变亮；串口打印 w=320 h=480。",
        "背光灭不等于断电，屏里的内容还在，别据此判断「屏没初始化」；PB15 在本板上还被提到与 W25QXX 的 SPI2 引脚同号（见工程里的说明），所以背光这一路必须用库函数走，不要自己去配 PB15 的复用功能。",
        "LCD_BackLight / LCD_GetWidth / LCD_GetHeight"
      ]
    ]
  },

  /* ======================= 板2 · 阶段 3 ======================= */
  {
    n: "🧵 板2 · 阶段 3｜FreeRTOS 任务框架：谁收、谁转发、谁补传",
    m: "板2 项目引导",
    _b: "板2",
    d: "把 while(1) 单循环拆成 5 个任务：LoRa 收、转发、收命令、补传、监控，数据用队列在任务间传递。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：外设初始化 + while 单循环。\n本阶段产出：LoraRx / Forward / Replay / Cloud / Monitor 五个任务骨架 + 队列/互斥量 + 调度器。\n下一阶段怎么用：阶段 4 只往 vTaskLoraRx 里填收包与解析，任务框架不动。\n\n板2 的任务形态（照本工程 main.c 的划分）：vTaskLoraRx(pri3,512) 收 LoRa 并校验 → 队列 → vTaskForward(pri4,512) 原样转发 USART2 并写缓存；vTaskCmdRx(pri3,512) 收上位机命令；vTaskReplay(pri3,256) 按 50ms 节拍补传；vTaskMonitor(pri5,256) 管看门狗、心跳与栈水位。堆与优先级全部以板2 自己的 FreeRTOSConfig.h 为准，不要抄板1 的数字。【阶段 3 动手前必做】先调一次 SYS_NVIC_Init()：它只干一件事 —— NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4)，即 4 位全抢占（本库默认就该是这个分组）。必须在使能任何中断、起 FreeRTOS 之前调；不调就沿用复位默认分组（PriorityGroup_0，2 位抢占），FreeRTOS 的 configASSERT 会抓，但库不会替你调。本库 FreeRTOSConfig.h 的注释里也标了这件事（并给出写完整 main.c 后的位置：板1 第 989 行 / 板2 第 1581 行）。注意：阶段判定的判据里没有这一条，机器不会替你抓，得自己记得。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。\n本卡各步落点：\n· 第2步 最小可跑 → QueueHandle_t 句柄进 ②；taskA/taskB 进 ④（main 之前）；xQueueCreate/xTaskCreate 进 ⑤ 建任务段。\n· 第3步 优先级与栈 → 只改 ⑤ 里 xTaskCreate 的实参，不新增位置。\n· 第4步 核对 FreeRTOSConfig → 不在 main.c：只读核对 FreeRTOS/port/FreeRTOSConfig.h（板2 自己的那份，别抄板1）。\n· 第5步 周期节拍 → period/last 是 ④ vTaskReplay 内的局部变量。\n· 第6步 队列满与串口互斥 → s_qt_tx_mtx 进 ②；qt_send_frame 写成 static 辅助函数放 ④；xSemaphoreCreateMutex 进 ⑤（建任务之前）。\n· 第7步 看门狗与任务心跳 → APP_WDG_TIMEOUT_MS 进 ②；SYS_WDG_Init 进 ⑤；心跳调用放 ④ 各任务循环。",
    k: "板2 FreeRTOS 任务 队列 xQueueCreate xTaskCreate 优先级 栈大小 堆 vTaskDelay pdMS_TO_TICKS vTaskLoraRx vTaskForward vTaskCmdRx vTaskReplay vTaskMonitor 互斥量 看门狗 心跳",
    f: [
      [
        "先把任务与数据流画在纸上",
        "任务怎么划分，决定了后面所有代码长什么样；板2 的坑几乎都出在「谁等谁」。",
        "写一张表，每行一个任务：任务名 / 优先级 / 栈（字） / 周期 / 输入 / 输出。参考本工程：vTaskLoraRx 轮询收 LoRa 帧 → 队列；vTaskForward 取队列 → USART2 转发 + 写 W25Q128；vTaskCmdRx 收 USART2 命令字；vTaskReplay 只在收到补传命令后按 50ms 节拍发历史帧；vTaskMonitor 每秒喂狗并打栈水位。",
        "",
        "你的表能回答四个问题就算过：谁产生数据、谁消费、队列满了怎么办、谁最急（优先级最高）。",
        "不要「一个任务干全部」：LoRa 要一直轮询、转发要按队列节奏走、补传是被命令触发的，塞进同一个任务只会互相拖，而且一处卡死链路全停。",
        "xTaskCreate / xQueueCreate"
      ],
      [
        "最小可跑：两个任务 + 一个队列",
        "先证明调度器和队列真的在跑，再往里塞业务，出问题时才好定位。",
        "建一个深度 4 的队列，两个任务：A 每 500ms 放一个递增的数，B 收到就用 USART1 打印。队列和任务都要在 vTaskStartScheduler() 之前创建。",
        "QueueHandle_t q;\n\nstatic void taskA(void *p) {\n    uint32_t v = 0;\n    for (;;) {\n        v++;\n        xQueueSend(q, &v, portMAX_DELAY);\n        vTaskDelay(pdMS_TO_TICKS(500));\n    }\n}\n\nstatic void taskB(void *p) {\n    uint32_t got;\n    for (;;) {\n        if (xQueueReceive(q, &got, portMAX_DELAY) == pdPASS) {\n            SYS_USART_Printf(SYS_USART_1, \"B got %lu\\r\\n\", (unsigned long)got);\n        }\n    }\n}\n\nint main(void) {\n    SYS_CLK_ToHighSpeed();\n    SYS_USART_Init(SYS_USART_1, 115200);\n    SYS_FAULT_Init();\n    q = xQueueCreate(4, sizeof(uint32_t));\n    xTaskCreate(taskA, \"A\", 512, NULL, 3, NULL);\n    xTaskCreate(taskB, \"B\", 512, NULL, 3, NULL);\n    vTaskStartScheduler();\n    for (;;) { }\n}",
        "串口每 0.5 秒一行，数字连续不跳号；B 的打印不会被 A 的延时卡住。",
        "栈大小的单位是「字」（4 字节），512 就是 2KB；xQueueSend 传的是值不是指针，结构体必须定长；上了 FreeRTOS 之后不要再调 SYS_TICK_Init()，滴答由内核接管。",
        "xQueueCreate / xTaskCreate / xQueueSend / xQueueReceive / vTaskStartScheduler"
      ],
      [
        "板2 的优先级与栈取值",
        "优先级就是抢占顺序，配错了会出现「补传把转发饿死」或者「监控任务醒不过来」。",
        "照本工程取值：vTaskLoraRx / vTaskCmdRx / vTaskReplay 用 3，vTaskForward 用 4，vTaskMonitor 用 5；栈 512 字（2KB），Replay 与 Monitor 用 256 字（1KB）。0 和 1 留给空闲任务与内核，不要占用。",
        "xTaskCreate(vTaskLoraRx,  \"LoraRx\",  512, NULL, 3, NULL);\nxTaskCreate(vTaskForward, \"Forward\", 512, NULL, 4, NULL);\nxTaskCreate(vTaskCmdRx,   \"CmdRx\",   512, NULL, 3, NULL);\nxTaskCreate(vTaskReplay,  \"Replay\",  256, NULL, 3, NULL);\nxTaskCreate(vTaskMonitor, \"Monitor\", 256, NULL, 5, NULL);",
        "在 Monitor 里打印每个任务的剩余栈：uxTaskGetStackHighWaterMark(NULL)，稳定运行后剩余 > 50 字。",
        "优先级只差 1 就会抢占；转发优先级高于收帧是故意的（转发要跟上），但不要在 LoraRx 里放长阻塞；低优先级任务里不要写「不阻塞的死循环」，否则高优先级任务拿不到 CPU。",
        "xTaskCreate / uxTaskGetStackHighWaterMark / configMAX_PRIORITIES"
      ],
      [
        "打开板2 自己的 FreeRTOSConfig.h 核对，不要抄板1",
        "两块板的任务数、缓冲大小不一样，堆不够的现象是「程序不跑也没有报错」，非常迷惑人。",
        "用 Keil 打开 D:\\GitHub_Project\\EnvMonitor\\Project_2_数据通信端\\Project\\FreeRTOS\\port\\FreeRTOSConfig.h，逐条核对本工程真实取值：节拍 1000Hz、优先级档 8（0~7）、堆 30KB、只用动态分配、互斥量与计数信号量开着、栈溢出检查开着。核对完再写自己的任务。",
        "/* 板2 FreeRTOSConfig.h 里实际写着的值，照抄这一份，不要照抄板1 */\n#define configTICK_RATE_HZ      ((TickType_t)1000)\n#define configMAX_PRIORITIES    8\n#define configTOTAL_HEAP_SIZE   ((size_t)(30 * 1024))\n#define configSUPPORT_STATIC_ALLOCATION  0\n#define configCHECK_FOR_STACK_OVERFLOW   2",
        "你能在 FreeRTOSConfig.h 里指出这 5 行的位置，并算出 5 个任务的栈合计约 9KB（512×4 字 + 256×2 字 ≈ 8.5KB），30KB 堆有余量。",
        "configTOTAL_HEAP_SIZE 是「全部动态分配加起来」的上限，不是每个任务的；configCHECK_FOR_STACK_OVERFLOW 开着时栈溢出会走 configASSERT，别把它关掉来「解决」问题；堆不足时 vTaskStartScheduler 直接返回，你写在它后面的死循环会跑起来，看起来像「程序在跑但什么都没做」。",
        "configTOTAL_HEAP_SIZE / configTICK_RATE_HZ / configMAX_PRIORITIES / configSUPPORT_STATIC_ALLOCATION / xPortGetFreeHeapSize / configCHECK_FOR_STACK_OVERFLOW"
      ],
      [
        "周期节拍：vTaskDelay 与 pdMS_TO_TICKS",
        "RTOS 下延时写错，轻则补传节奏抖，重则整个任务被自己的等待锁住。",
        "收帧任务用短周期轮询（vTaskDelay(pdMS_TO_TICKS(10))）；补传用 vTaskDelay(pdMS_TO_TICKS(50)) 约 20 帧/秒；需要严格周期（监控 1s 心跳）改用 vTaskDelayUntil()，以当前 tick 为基准累加。",
        "static void vTaskReplay(void *pv) {\n    const TickType_t period = pdMS_TO_TICKS(50);\n    TickType_t last = xTaskGetTickCount();\n    for (;;) {\n        replay_step();          /* 没在补传时立刻返回 */\n        vTaskDelayUntil(&last, period);\n    }\n}",
        "任务里用 xTaskGetTickCount() 打时间戳，连续两次补传间隔稳定在 50ms±1 tick。",
        "任务里不要用 SYS_TICK_Delay_ms(50) 这类忙等；毫秒换算一律走 pdMS_TO_TICKS，直接写 50 会被当成 50 个 tick（本板 tick=1ms 时凑巧一样，改节拍就错）；微秒级延时用 delay_us()，但必须先初始化 DWT。",
        "vTaskDelay / vTaskDelayUntil / pdMS_TO_TICKS / xTaskGetTickCount / delay_us / delay_dwt_init"
      ],
      [
        "队列满了、串口在抢：超时与互斥量",
        "真实系统一定会遇到消费者变慢，没有超时策略就会死等或者悄悄丢数据；两个任务同时写 USART2 会把帧写花。",
        "发送侧统一用带超时的 xQueueSend(q, &m, pdMS_TO_TICKS(5))，返回非 pdPASS 就累加丢帧计数；USART2 是转发任务与补传任务共用的，用一把 xSemaphoreCreateMutex() 建互斥量，写之前 xSemaphoreTake、写完 xSemaphoreGive，保证一帧 20 字节不会被另一条帧插进中间。",
        "static SemaphoreHandle_t s_qt_tx_mtx = NULL;\n\nstatic void qt_send_frame(uint8_t cmd, const uint8_t *payload, uint16_t len)\n{\n    if (xSemaphoreTake(s_qt_tx_mtx, pdMS_TO_TICKS(50)) != pdPASS) {\n        s_drop++;                 /* 拿不到锁就丢这一帧，绝不无超时死等 */\n        return;\n    }\n    SYS_FRAME_Send(SYS_USART_2, cmd, payload, len);\n    xSemaphoreGive(s_qt_tx_mtx);\n}",
        "人为把补传任务的延时放大，能看到丢帧计数增长但系统不死锁；上位机抓到的每一帧都是完整的 20 字节，没有交错。",
        "队列深度是「最多存几个元素」不是字节数（板2 的帧队列深度 4 就够，一帧 20 字节）；元素按值拷贝，结构体里不要放指向临时变量的指针；互斥量只能在同一任务里成对 take/give，中断里要用带 FromISR 的版本。",
        "xQueueSend / xQueueReceive / portMAX_DELAY / pdPASS / xSemaphoreCreateMutex / xSemaphoreTake / xSemaphoreGive / SYS_FRAME_Send"
      ],
      [
        "看门狗与任务心跳：别让一个卡死的任务拖垮整板",
        "板2 要长期挂在现场，任何一个任务忘记喂狗都应该触发复位并留下原因，而不是黑屏不动。",
        "顶层用 SYS_WDG_Init(timeout_ms) 起独立看门狗（本工程按 4000ms 用），每个任务在自己的循环里调 SYS_WDG_Heartbeat(id) 报心跳；监控任务周期性调 SYS_WDG_HeartbeatAll() 检查是否所有心跳都到齐，到齐才 SYS_WDG_Feed() 喂狗。",
        "#define APP_WDG_TIMEOUT_MS   4000U\n\n/* 每个任务循环里各自调一次：SYS_WDG_Heartbeat(0U) / (1U) / (2U) ... */\n\nstatic void vTaskMonitor(void *pv) {\n    SYS_WDG_Init(APP_WDG_TIMEOUT_MS);\n    for (;;) {\n        if (SYS_WDG_HeartbeatAll() != 0U) {   /* 全部任务都报到 */\n            SYS_WDG_Feed();\n        }\n        SYS_USART_Printf(SYS_USART_1, \"pending=0x%X heap=%u\\r\\n\",\n                        (unsigned)SYS_WDG_HeartbeatPending(),\n                        (unsigned)xPortGetFreeHeapSize());\n        vTaskDelay(pdMS_TO_TICKS(1000));\n    }\n}",
        "正常运行 pending=0、狗不咬；把某个任务的 vTaskDelay 改成死循环不喂心跳，4 秒后板子复位，复位原因打印出看门狗。",
        "SYS_WDG_Init 必须在调度器起来之后、由任务调用（放 main 里会在调度前就开狗，容易自己咬自己）；心跳 id 的合法范围是 0 ~ SYS_WDG_HEARTBEAT_COUNT-1，超出范围的 Heartbeat 调用会被丢弃，看起来像「喂了还复位」；调试打断点时先别开狗，否则一停就复位。",
        "SYS_WDG_Init / SYS_WDG_Feed / SYS_WDG_Heartbeat / SYS_WDG_HeartbeatAll / SYS_WDG_HeartbeatPending / SYS_WDG_HeartbeatClear / xPortGetFreeHeapSize"
      ]
    ]
  },

  /* ======================= 板2 · 阶段 4 ======================= */
  {
    n: "📡 板2 · 阶段 4｜LoRa 接收与协议解析",
    m: "板2 项目引导",
    _b: "板2",
    d: "让板2 真的收到板1 的 20 字节环境帧：LoRa 初始化、收包、CRC16 校验、坏帧重同步，并把它变成可转发的数据。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：vTaskLoraRx 空壳 + SYS_FRAME 接口可用。\n本阶段产出：收包、逐字节喂帧、整帧校验、坏帧重同步、拆环境帧。\n下一阶段怎么用：阶段 5 用拆出的值与整帧做上云；收帧任务不再改。\n\n本阶段产物：LoRa 模块自检通过（返回 LORA_E22_OK）、能稳定收到板1 发来的 20 字节帧、能识别并丢弃坏帧、错帧计数在涨而正确帧计数同步在涨。这一阶段结束时，vTaskLoraRx 应该能把「一整帧 20 字节」投进队列，后面阶段 5、6 都只消费这个结果。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。\n本卡各步落点：\n· 第1步 LoRa 上电自检 → #include 进 ①；LORA_E22_Init 进 ⑤ 初始化段。\n· 第2步 收包 → 直接改 ④ vTaskLoraRx 的函数体。\n· 第3步 喂帧解析 → 放 ④ vTaskLoraRx 内；buf/n/r/cmd/payload 都是函数内局部。\n· 第4步 整帧校验 → 放 ④ vTaskLoraRx；s_rx_ok 这类计数器进 ②。\n· 第5步 重同步 → 放 ④ vTaskLoraRx；s_bad_last 进 ②。\n· 第6步 拆环境帧 → 局部变量放 ④ 收帧任务；拆出的值填进 ③ 的结果结构体再入队，转发仍用整帧。",
    k: "板2 LoRa E22 lora_e22 LORA_E22_Init LORA_E22_Recv USART6 9600 AUX M0 M1 透明模式 20字节帧 AA55 CRC16 重同步 SYS_FRAME_Feed SYS_FRAME_Get SYS_FRAME_Verify ErrCount",
    f: [
      [
        "LoRa 上电自检：先证明模块认得上",
        "收不到数据时，90% 的问题在「模块没配上」而不是「代码错了」；自检能把这件事先摘出去。",
        "本板 LoRa 挂在 USART6（9600），M0 / M1 / AUX 三根控制线都在 GPIOE 上，宏名是 SYS_LORA_M0_PORT/PIN、SYS_LORA_M1_PORT/PIN、SYS_LORA_AUX_PORT/PIN。依次调 LORA_E22_Init() 等 AUX 就绪，再用 LORA_E22_SendAT(\"AT\") 试一次指令，最后 LORA_E22_SetMode(LORA_E22_MODE_TRANSPARENT) 切到透明传输模式。",
        "#include \"lora_e22.h\"\n\nuint8_t r = LORA_E22_Init();\nP(\"LORA_E22_Init() = %u  (0=OK 1=AUX 一直忙 2=没模块)\\r\\n\", r);\nif (r != LORA_E22_OK) { /* 先查电与线，再继续 */ }\n\nr = LORA_E22_SendAT(\"AT\");\nSYS_USART_Printf(SYS_USART_1, \"AT -> %u\\r\\n\", (unsigned)r);\nr = LORA_E22_SetMode(LORA_E22_MODE_TRANSPARENT);\nSYS_USART_Printf(SYS_USART_1, \"mode -> %u, AUX=%u\\r\\n\",\n                (unsigned)r, (unsigned)LORA_E22_IsBusy());",
        "串口打印 LORA_E22_Init() = 0、AT -> 0、mode -> 0；把模块电源拔掉重试，能看到返回 1 或 2 —— 说明自检真的在测。",
        "返回值 1 是 AUX 一直忙（多半是没上电、M0/M1 接错、或者模块在配置态），2 是 AT 无回应（TXRX 接反、波特率不符）。SYS_LORA_UART 与 ESP8266_USART 不能是同一个口，工程里有编译期自检盯着这件事，改错会直接编不过——这是保护，不是刁难。",
        "LORA_E22_Init / LORA_E22_WaitReady / LORA_E22_SendAT / LORA_E22_SetMode / LORA_E22_IsBusy / LORA_E22_ReadConfig / LORA_E22_OK / LORA_E22_MODE_TRANSPARENT / SYS_LORA_UART / SYS_LORA_BAUD"
      ],
      [
        "收包：LORA_E22_Recv 的三个返回码",
        "收不到数据分「没数据」和「数据装不下」两种，分不清就会一直改错地方。",
        "在任务里循环调 LORA_E22_Recv(buf, sizeof(buf), &n)：返回 LORA_E22_OK 表示收到 n 字节；返回 LORA_E22_ERR_NO_DATA 表示这轮没数据（正常，继续等）；返回 LORA_E22_ERR_OVERFLOW 表示收到的帧超过了调用方给的 max（整帧作废，驱动返回前已把内部计数清掉，不必再补一次 Flush）；正常 20 字节帧不该出现，出现了先查是不是两帧粘在了一起。",
        "static void vTaskLoraRx(void *pv)\n{\n    uint8_t buf[256];\n    uint16_t n;\n    uint8_t r;\n\n    for (;;) {\n        r = LORA_E22_Recv(buf, (uint16_t)sizeof(buf), &n);\n        if (r == LORA_E22_OK) {\n            /* 把 buf[0..n) 喂给帧解析 */\n        } else if (r == LORA_E22_ERR_OVERFLOW) {\n            LORA_E22_Flush();       /* 可选：驱动已自清，这里只是显式复位 */\n        }\n        vTaskDelay(pdMS_TO_TICKS(10));\n    }\n}",
        "板1 每发一帧，这里就进一次 LORA_E22_OK 分支；验收标准是 overflow 计数保持 0（一帧 20 字节远小于 buf[256]，出现 overflow 说明收到的是粘帧，先调 SYS_LORA_FRAME_GAP_MS，而不是加大调用方缓冲——驱动自己的环形缓冲只有 SYS_LORA_RX_BUF_SIZE = 256 字节）。",
        "LORA_E22_ERR_NO_DATA 不是错误，如果把它算进错帧计数，你的错误率永远是 100%；轮询周期 10ms 就够（与驱动的 SYS_LORA_FRAME_GAP_MS = 10ms 对齐；本项目板1 每 2 秒才发一帧），周期太长会丢帧、太短纯浪费 CPU。",
        "LORA_E22_Recv / LORA_E22_Flush / LORA_E22_ERR_NO_DATA / LORA_E22_ERR_OVERFLOW / LORA_E22_OK / SYS_LORA_RX_BUF_SIZE"
      ],
      [
        "逐字节喂给帧解析：SYS_FRAME_Feed 与 SYS_FRAME_Get",
        "LoRa 是字节流，一帧 20 字节可能被拆成两次 Recv 收到，不能假设「一次 Recv 就是一帧」。",
        "把收到的每一个字节都喂给 SYS_FRAME_Feed(uart, byte)，内部状态机自动找 0xAA55 帧头；再调 SYS_FRAME_Available(uart) 看有没有攒出一整帧，有就用 SYS_FRAME_Get(uart, &cmd, payload, cap, &len) 取出来。这样拆分/粘包都不会错。",
        "#define FRAME_TOTAL   20U\n\nuint8_t cmd, payload[SYS_FRAME_MAX_PAYLOAD];\nuint16_t len;\n\nfor (i = 0; i < n; i++) {\n    SYS_FRAME_Feed(SYS_LORA_UART, buf[i]);\n    while (SYS_FRAME_Available(SYS_LORA_UART) != 0U) {\n        if (SYS_FRAME_Get(SYS_LORA_UART, &cmd, payload,\n                          (uint16_t)sizeof(payload), &len) == SYS_FRAME_OK) {\n            if ((cmd == SYS_FRAME_CMD_ENV) && (len == SYS_FRAME_ENV_LEN)) {\n                /* 收到一条合法环境帧 */\n            }\n        }\n    }\n}",
        "给板2 连发两帧（中间不加间隔），两条都能被取出来；把一帧拆成 7+13 两次发，也能取出完整一条。",
        "SYS_FRAME_Available 是「有几帧可取」不是「有几字节」；payload 缓冲至少要 SYS_FRAME_MAX_PAYLOAD（64）字节，给 12 字节会取不出来还报错；Feed 是纯状态机、不阻塞，放在任务里循环喂很安全。",
        "SYS_FRAME_Feed / SYS_FRAME_Available / SYS_FRAME_Get / SYS_FRAME_MAX_PAYLOAD / SYS_FRAME_OK / SYS_FRAME_CMD_ENV / SYS_FRAME_ENV_LEN"
      ],
      [
        "整帧校验与 CRC16：别信「帧头对了就是对的」",
        "线上干扰、模块重启、波特率偏差都会造出「帧头对、内容错」的帧；CRC 是唯一能挡住它的关卡。",
        "对已经收到的整帧缓冲调 SYS_FRAME_Verify(buf, len)，返回 SYS_FRAME_OK 才算合法；错误码分别告诉你哪里坏了：帧头、帧尾、长度不符（典型是两帧粘接）、CRC 错。CRC 本身是 CRC16，初值 0xFFFF、多项式 0xA001，校验范围从 CMD 起不含帧头 CRC 自身与帧尾；想单独验证算法可以用 MODBUS_CRC16(buf, len) 手算一遍对拍。",
        "uint8_t st = SYS_FRAME_Verify(frame, (uint16_t)sizeof(frame));\nif (st == SYS_FRAME_OK) {\n    s_rx_ok++;\n} else if (st == SYS_FRAME_ERR_CRC) {\n    s_rx_bad++;\n    SYS_USART_SendLine(SYS_USART_1, \"frame crc error\\r\\n\");\n} else if (st == SYS_FRAME_ERR_LEN) {\n    s_rx_bad++;             /* 两帧粘接，长度字段与实际不符 */\n}",
        "故意把板1 侧某一帧的数据字节改一个（比如把温度低字节加 1），板2 侧必须打印 crc error 并丢掉，正确帧计数不受影响。",
        "SYS_FRAME_ERR_CHECK 是旧名，值等于 SYS_FRAME_ERR_CRC，两个写哪个都能编过，但别以为它们是两种错误；不要在 CRC 错的时候「修一修继续用」——补传上去的坏帧比丢帧更麻烦，上位机会把它当真实数据画进曲线。",
        "SYS_FRAME_Verify / MODBUS_CRC16 / SYS_FRAME_OK / SYS_FRAME_ERR_HEAD / SYS_FRAME_ERR_TAIL / SYS_FRAME_ERR_LEN / SYS_FRAME_ERR_CRC / SYS_FRAME_ERR_CHECK"
      ],
      [
        "坏帧之后的重同步：Reset 与 ErrCount 一起看",
        "一帧坏掉后，解析器可能停在错误的状态里，把后面所有好帧都当成坏帧——现象是「错一次之后就再也不通了」。",
        "每个任务周期读一次 SYS_FRAME_ErrCount(uart)：正常收帧时它不涨；如果连续几个周期一直在涨，说明链路或解析器卡住了，调 SYS_FRAME_Reset(uart) 把状态机清回等帧头。把正确帧数与错误帧数一起打印，两者的比例才是判断链路的依据。",
        "#include \"sys_frame.h\"\n\nuint16_t bad = SYS_FRAME_ErrCount(SYS_LORA_UART);\nif (bad != s_bad_last) {\n    char line[64];\n    SYS_STR_Format(line, sizeof(line), \"lora err=%u ok=%u\", (unsigned)bad, (unsigned)s_rx_ok);\n    SYS_USART_SendLine(SYS_USART_1, line);\n    s_bad_last = bad;\n}\nif (s_bad_streak >= 3U) {          /* 连续三轮都在错 */\n    SYS_FRAME_Reset(SYS_LORA_UART);\n    s_bad_streak = 0U;\n}",
        "移动 LoRa 天线让链路变差：错误计数开始涨；把干扰去掉后，正确帧计数继续增长、错误计数停住，不需要复位板子。",
        "SYS_FRAME_Reset 只清解析状态，不会把接收缓冲里的字节清掉；ErrCount 是累计值不是增量，要自己存上一次的值做差；SYS_STR_Format 的返回值是写入长度，别拿它当错误码判断。",
        "SYS_FRAME_ErrCount / SYS_FRAME_Reset / SYS_STR_Format"
      ],
      [
        "把环境帧拆成可用的数据，但转发要留整帧",
        "转发给上位机要的是原始 20 字节，本地显示/上云要的是拆开的 6 个数值——两者都要，缺一个后面就得回头改。",
        "用 SYS_FRAME_UnpackEnv(payload, len, &temp_x100, &humi_x100, &press_x10, &light, &tvoc, &mq135) 把 12 字节数据段拆成 6 个定标整数（温度×100、湿度×100、气压×10，其余整型）。同时把「原来的 20 字节整帧」原样收好——补传历史帧时必须发原帧，不能重新组。",
        "int16_t  temp_x100, humi_x100, press_x10;\nuint16_t light, tvoc, mq135;\n\nif (SYS_FRAME_UnpackEnv(payload, len, &temp_x100, &humi_x100,\n                        &press_x10, &light, &tvoc, &mq135) == SYS_FRAME_OK) {\n    SYS_USART_Printf(SYS_USART_1, \"T=%d.%02d H=%d.%02d P=%d.%d L=%u TVOC=%u MQ=%u\\r\\n\",\n        temp_x100 / 100, (temp_x100 % 100 + 100) % 100,\n        humi_x100 / 100, (humi_x100 % 100 + 100) % 100,\n        press_x10 / 10,  (press_x10 % 10 + 10) % 10,\n        (unsigned)light, (unsigned)tvoc, (unsigned)mq135);\n}",
        "打印的温度小数位与板1 侧实际值一致（比如 25.31 打印成 25.31 而不是 25.30 或 -25.31）；6 个值都对得上板1 的显示。",
        "SYS_FRAME_UnpackEnv 入参是「数据段 payload」不是整帧，喂错会把 CMD 当温度读出来一堆怪数；负温度取小数时直接 %100 会得到负余数，要自己补正（上面加了 +100 再取模）。",
        "SYS_FRAME_UnpackEnv / SYS_FRAME_BuildEnv / SYS_FRAME_ENV_TEMP_OFF / SYS_FRAME_ENV_HUMI_OFF / SYS_FRAME_ENV_PRESS_OFF / SYS_FRAME_ENV_LIGHT_OFF / SYS_FRAME_ENV_TVOC_OFF / SYS_FRAME_ENV_MQ135_OFF"
      ]
    ]
  },

  /* ======================= 板2 · 阶段 5 ======================= */
  {
    n: "🌐 板2 · 阶段 5｜上行链路：ESP8266 AT 与 MQTT",
    m: "板2 项目引导",
    _b: "板2",
    d: "把环境数据送上云端：ESP8266 联网、连 MQTT broker、发布主题、断线重连，并把「网通不通」变成转发任务能读到的开关。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：vTaskLoraRx 能收能解析。\n本阶段产出：ESP8266 上线、MQTT 发布、保活与重连、下行命令轮询。\n下一阶段怎么用：阶段 6 断网时把帧写进 W25QXX，恢复后按命令补传。\n\n本阶段产物：ESP8266 自检通过 → 连上路由器拿到 IP → 连上 broker（CONNACK = 0）→ 能发布一条环境数据 → 拔网线/关热点后 MQTT_IsConnected() 变成 0、恢复后能自动重连。这个「通不通」的布尔量就是阶段 6 判断该缓存还是该补传的依据。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。\n本卡各步落点：\n· 第1步 ESP8266 自检 → #include 进 ①；ESP8266_Init 进 ⑤ 初始化段。\n· 第2步 联网 → 放 ④ vTaskCloud；SSID/密码这类配置宏进 ②（别写死在函数里）；ip 是局部。\n· 第3步 连 broker → 放 ④ vTaskCloud（ConnectTCP + MQTT_Connect）。\n· 第4步 发布环境数据 → 放 ④ vTaskCloud；主题前缀宏进 ②。\n· 第5步 保活与重连 → net_online 进 ②（跨任务要读）；服务调用放 ④。\n· 第6步 下行命令 → 放 ④ vTaskCloud 或单独命令任务；m 是局部变量。",
    k: "板2 ESP8266 AT 指令 USART3 PB10 PB11 mqtt MQTT_Connect MQTT_ConnectSimple MQTT_Publish CONNACK KeepAlive 重连 broker.emqx.io 1883",
    f: [
      [
        "ESP8266 上电与 AT 自检",
        "WiFi 模块供电、接线、波特率三件事任一不对，后面全是白忙；先用 AT 把模块本身摘干净。",
        "本板 WiFi 接在 USART3（PB10 / PB11），宏是 ESP8266_USART，默认波特率 ESP8266_DEFAULT_BAUD = 115200。先 ESP8266_Init(115200)，再 ESP8266_TestAT() 确认模块在，需要时 ESP8266_Reset() 重启模块、ESP8266_Flush() 清掉上电噪声。",
        "#include \"esp8266.h\"\n\nif (ESP8266_Init(115200U) != 0U) {\n    SYS_USART_SendLine(SYS_USART_1, \"[FAIL] ESP8266_Init: check power (200mA+ peak) and TXD/RXD\\r\\n\");\n}\nSYS_USART_Printf(SYS_USART_1, \"AT -> %u  ip=%s\\r\\n\",\n                (unsigned)ESP8266_TestAT(), ip);",
        "串口打印 AT -> 0 并且 ip 有值；把模块的 TX/RX 对调后重试，能看到自检失败——证明这一步真的在测。",
        "ESP8266 发射瞬间要 200mA 以上，用 USB-TTL 的 3.3V 小电流供电会「连上一会儿就掉线」，这是最常见的假故障；ESP8266_USART 与 SYS_LORA_UART 必须是不同的串口，工程里有编译期断言盯着，改错直接编不过。",
        "ESP8266_Init / ESP8266_TestAT / ESP8266_Reset / ESP8266_Flush / ESP8266_SendCmd / ESP8266_GetRawRx / ESP8266_USART / ESP8266_DEFAULT_BAUD / ESP8266_CMD_TIMEOUT_MS"
      ],
      [
        "联网：站点模式 + 连路由器 + 拿 IP",
        "只有真的拿到 IP，才能认为链路是通的；「AT 回了 OK」不代表连上网。",
        "先 ESP8266_SetMode(ESP8266_MODE_STA) 切站点模式，再 ESP8266_JoinAP(ssid, password) 连路由器（超时按 ESP8266_JOIN_TIMEOUT_MS = 15000ms 预留），最后 ESP8266_GetIP(ip, len) 取本机 IP 打印出来。换网络时可用 ESP8266_QuitAP() 断开。",
        "char ip[16];\n\nif (ESP8266_SetMode(ESP8266_MODE_STA) != 0U) { /* 失败 */ }\nif (ESP8266_JoinAP(ST_WIFI_SSID, ST_WIFI_PASS) != 0U) {\n    SYS_USART_SendLine(SYS_USART_1, \"join ap failed\\r\\n\");\n}\nif (ESP8266_GetIP(ip, sizeof(ip)) == 0U) {\n    SYS_USART_Printf(SYS_USART_1, \"ip=%s\\r\\n\", ip);\n}",
        "打印出一个 192.168.x.x 之类的地址，和路由器后台里这台设备的地址一致。",
        "把 WiFi 名/密码写成占位符（本工程模板里是 wifi-name-here / wifi-pass-here）就上线，现象是连不上还查不出原因——先把这两个宏换成真实值；密室里 5G 频段连不上要设成 2.4G；JoinAP 会阻塞十几秒，别放在高优先级任务里做。",
        "ESP8266_SetMode / ESP8266_JoinAP / ESP8266_QuitAP / ESP8266_GetIP / ESP8266_MODE_STA / ESP8266_JOIN_TIMEOUT_MS / ESP8266_MODE_AP / ESP8266_MODE_STA_AP"
      ],
      [
        "连上 broker：先建 TCP，再握手 MQTT",
        "MQTT 是跑在 TCP 之上的；TCP 没建起来就去连 MQTT，只会得到一个看不懂的超时。",
        "两步分开验：ESP8266_ConnectTCP(host, port) 建 TCP（明文 MQTT 端口是 MQTT_DEFAULT_PORT = 1883），成功后再 MQTT_ConnectSimple(host, client_id) 做 MQTT 握手。握手结果要看 MQTT_GetConnackCode()：0 表示接受，其它值分别对应协议版本、client id 被拒、服务器不可用等。",
        "uint8_t r;\n\nif (ESP8266_ConnectTCP(ST_MQTT_HOST, MQTT_DEFAULT_PORT) != 0U) { /* 建 TCP 失败 */ }\n\nr = MQTT_ConnectSimple(ST_MQTT_HOST, ST_MQTT_CLIENT);\nSYS_USART_Printf(SYS_USART_1, \"MQTT_ConnectSimple() = %u (%s)\\r\\n\", r, MQTT_ErrStr(r));\nSYS_USART_Printf(SYS_USART_1, \"connack=%u connected=%u alive=%u\\r\\n\",\n                (unsigned)MQTT_GetConnackCode(),\n                (unsigned)MQTT_IsConnected(), (unsigned)MQTT_IsAlive());",
        "打印 MQTT_ConnectSimple() = 0、connack=0、connected=1、alive=1；用手机热点临时关掉，这些量会变 0。",
        "MQTT_ErrStr(r) 是把返回码翻译成人话用的，出了错一定打出来，光看数字你会猜半天；client_id 重复（比如两台板用同一个名字）会被 broker 踢掉，本工程模板里用的是 f407-board2-test 这种带板名后缀的名字；完整版 MQTT_Connect 还带用户名密码，公共 broker 用 ConnectSimple 就够。",
        "ESP8266_ConnectTCP / ESP8266_CloseTCP / MQTT_Connect / MQTT_ConnectSimple / MQTT_GetConnackCode / MQTT_IsConnected / MQTT_ErrStr / MQTT_DEFAULT_PORT / MQTT_CONNACK_ACCEPTED"
      ],
      [
        "发布环境数据：主题怎么定、值怎么转字符串",
        "MQTT 的载荷是文本/字节流，浮点数直接丢过去只会得到乱码；主题设计错了以后没法扩展。",
        "文本用 MQTT_PublishStr(topic, text)，整数用 MQTT_PublishInt(topic, v)，定点小数用 MQTT_PublishFloat(topic, v, decimals)；要发原始字节（比如整帧 20 字节）用 MQTT_Publish(topic, payload, len, ...)。主题建议一层设备、一层量纲，例如 data/f407-001/temp、data/f407-001/env。",
        "MQTT_PublishFloat(\"data/f407-001/temp\", 25.31f, 2U);\nMQTT_PublishInt  (\"data/f407-001/tvoc\", (int32_t)tvoc);\nMQTT_PublishStr  (\"data/f407-001/stat\", \"online\");",
        "用电脑上的 MQTT 客户端订阅 data/f407-001/#，每发一次就能看到一条；温度显示 25.31（两位小数）而不是 2531。",
        "MQTT_PublishFloat 的 decimals 是「小数位数」，传 100 会得到一串没意义的数字；主题长度受 MQTT_TOPIC_MAX_LEN = 64 限制（含结尾的 0），名字起太长会被 MQTT_ERR_TOO_LONG 挡掉；上传报文总长受 MQTT_TX_BUF_SIZE = 256 限制，别把整帧 20 字节再拼上一堆文字。",
        "MQTT_Publish / MQTT_PublishStr / MQTT_PublishInt / MQTT_PublishFloat / MQTT_TOPIC_MAX_LEN / MQTT_TX_BUF_SIZE / MQTT_ERR_TOO_LONG"
      ],
      [
        "保活、判活与重连：把「网通不通」做成一个布尔量",
        "断网现场没人看着，一个安静的掉线如果没人发现，缓存就会一直涨、云端一直没数据。",
        "周期性调 MQTT_KeepAliveService()：它负责按 keepalive 周期发 PINGREQ 并检查是否收到过 broker 的字节，返回 MQTT_ERR_ALIVE_TIMEOUT 表示判活超时且已把内部连接标志清零。用 MQTT_IsAlive() 查链路活性、MQTT_IsConnected() 查是否已连接，两个都为真才发布。重连路径就一句：重新 MQTT_ConnectSimple 一次。",
        "static uint8_t net_online = 0U;\n\nuint8_t hb = MQTT_KeepAliveService();\nif (hb == MQTT_ERR_ALIVE_TIMEOUT) {\n    net_online = 0U;\n    SYS_USART_SendLine(SYS_USART_1, \"mqtt alive timeout, will re-connect\\r\\n\");\n}\nif (MQTT_IsAlive() != 0U && MQTT_IsConnected() != 0U) {\n    net_online = 1U;\n}",
        "关掉热点等约两分钟（保活 60s 的 2 倍），串口打印 alive timeout、connected 变 0；恢复热点后重新连接成功、connected 回 1。",
        "MQTT_IsAlive 与 MQTT_IsConnected 不是一回事：前者看「最近有没有收到 broker 的字节」，后者是内部握手标志，判活超时时后者会被清零而前者可能还没更新，所以两个一起用；重连前先 ESP8266_CloseTCP() 或确认 TCP 还在，直接重复 Connect 会叠出多条 TCP 连接；判活超时门限是 MQTT_ALIVE_TIMEOUT_S = keepalive×2。",
        "MQTT_KeepAliveService / MQTT_IsAlive / MQTT_IsConnected / MQTT_Ping / MQTT_Disconnect / MQTT_ERR_ALIVE_TIMEOUT / MQTT_KEEPALIVE_S / MQTT_ALIVE_TIMEOUT_S"
      ],
      [
        "下行命令：订阅 + 轮询取消息",
        "板2 不只是发数据，还要能被上位机/云端指挥（切转发、清缓存、开始补传）。",
        "先 MQTT_Subscribe(topic_filter, qos) 订阅命令主题，再在主循环/任务里轮询 MQTT_Poll(&msg)：返回 MQTT_OK 表示取到一条消息，msg 里的主题与载荷在 MqttMsg_t 里。收到后按命令字分派，处理完继续轮询。订阅应答可用 MQTT_GetSubAck() 核对；不需要了用 MQTT_Unsubscribe。",
        "MqttMsg_t m;\n\n(void)MQTT_Subscribe(\"cmd/f407-001\", 0U);\n\nfor (;;) {\n    if (MQTT_Poll(&m) == MQTT_OK) {\n        SYS_USART_Printf(SYS_USART_1, \"cmd topic=%s payload=%s\\r\\n\", m.topic, m.payload);\n    }\n    vTaskDelay(pdMS_TO_TICKS(50));\n}",
        "用 MQTT 客户端往 cmd/f407-001 发一条文本，板2 串口打印出同样的主题与内容；订阅前发的消息收不到（QoS0 不补发）。",
        "MQTT_Poll 是「取一条」，要循环调用直到返回非 MQTT_OK，否则消息会在内部缓冲里越积越多；QoS0 不保证送达，命令最好带上重发或让上位机侧也做超时重发；载荷上限是 MQTT_PAYLOAD_MAX_LEN = 128，超长会被截断或丢弃。",
        "MQTT_Subscribe / MQTT_Unsubscribe / MQTT_Poll / MQTT_GetSubAck / MqttMsg_t / MQTT_PAYLOAD_MAX_LEN / MQTT_TOPIC_MAX_LEN"
      ]
    ]
  },

  /* ======================= 板2 · 阶段 6 ======================= */
  {
    n: "💾 板2 · 阶段 6｜Flash 缓存与断网补传",
    m: "板2 项目引导",
    _b: "板2",
    d: "断网时不丢数据：把每一帧连同时间戳写进 W25Q128 的环形日志区，联网后按上位机命令逐条补传。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：能解析 + 能上云。\n本阶段产出：W25QXX 日志槽、写入、读回统计、清空、补传与去重。\n下一阶段怎么用：阶段 7 在屏上显示这些状态；阶段 8 用断网 30 秒做对账。\n\n本阶段产物：W25QXX_Init 认得芯片、W25QXX_LogInit 扫出写入断点、每收到一帧就写一条 32 字节记录（12 字节头 + 20 字节载荷，正好放下整帧）、能用 W25QXX_LogRead 按序号读回、能响应 0x16 启停补传并按 0x15 逐条发历史帧、每 50ms 一条，同时用 0x96 上报进度。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。\n本卡各步落点：\n· 第1步 认芯片与分区 → #include 进 ①；W25QXX_Init 进 ⑤ 初始化段。\n· 第2步 槽结构 → 32 字节槽的 typedef 进 ③；偏移/长度宏进 ②。\n· 第3步 LogInit/LogWrite → W25QXX_LogInit 进 ⑤ 初始化段；LogWrite 在 ④ 收帧/转发任务里。\n· 第4步 RTC 时间戳 → #include 进 ①；now/ts 局部放 ④。\n· 第5步 读回与统计 → 局部变量放 ④ 补传任务。\n· 第6步 清空缓存 → s_cache_on 进 ②；清缓存处理放 ④ 命令处理处（擦除期间先关写入）。\n· 第7步 补传 → REPLAY_PERIOD_MS 与 s_replay 系列状态进 ②；补传任务进 ④；xTaskCreate 进 ⑤。",
    k: "板2 W25Q128 w25qxx w25qxx_log 64KB 16扇区 32字节槽 128槽 环形 序号 时间戳 CRC16 0x15 历史帧 0x16 补传 0x96 进度 去重 SYS_RTC_GetUnix",
    f: [
      [
        "先认出芯片与分区：W25QXX_ReadID / W25QXX_Init",
        "Flash 不在总线上时，后面所有缓存逻辑都只是「写了个寂寞」；先把芯片本身验掉。",
        "本板 Flash 挂在 SPI1（PB3 / PB4 / PB5），片选 PB14，宏是 W25QXX_SPI_ID / W25QXX_CS_PORT / W25QXX_CS_PIN。先 W25QXX_ReadID() 看 ID，再 W25QXX_Init(0U) 初始化（0 = 用宏里的默认速度 W25QXX_SPI_SPEED = 10MHz）。分区表里 W25QXX_ADDR_LOG = 0x001000 是运行日志区的起点。",
        "#include \"w25qxx.h\"\n\nuint32_t id = W25QXX_ReadID();\nuint8_t  r  = W25QXX_Init(0U);\n\nSYS_USART_Printf(SYS_USART_1, \"JEDEC=%06X  Init=%u (0=OK 1=没上电 2=不是W25Q128)\\r\\n\",\n                (unsigned)id, (unsigned)r);",
        "ID 打印出 0xEF4018（Winbond）或 0xC84018（GD25Q128）之类，且 Init 返回 0；把片选脚悬空会打印 Init 失败。",
        "w25qxx.h 的宏里默认只认 Winbond 一种 ID（0xEF4018），碰上 GD 的片子要按头文件说明放开识别，否则会返回 2 让你以为芯片坏了；线长或有干扰时把速度降到 2MHz（W25QXX_SPI_SPEED），10MHz 在杜邦线上跑不稳。",
        "W25QXX_ReadID / W25QXX_Init / W25QXX_WaitBusy / W25QXX_IsBlank / W25QXX_SPI_ID / W25QXX_CS_PIN / W25QXX_SPI_SPEED / W25QXX_ADDR_LOG"
      ],
      [
        "理解日志区的槽结构：32 字节一槽，正好装下整帧",
        "不理解布局就会出现「写进去读出来对不上」，或者写超长把后面几条覆盖掉。",
        "日志区 W25QXX_LOG_BASE = W25QXX_ADDR_LOG（0x001000），占 W25QXX_LOG_SECTORS = 16 个扇区 = 64KB。一条记录占 W25QXX_LOG_SLOT_SIZE = 32 字节，4KB 扇区正好 128 槽，不跨页不跨扇区。槽头 12 字节：序号 seq(4) + 时间戳 ts(4) + 长度(2) + CRC16(2)；剩下 W25QXX_LOG_PAYLOAD = 20 字节正好放一帧 20 字节环境帧；空槽的 seq 是 W25QXX_LOG_EMPTY（全 1）。总容量 = 16 × 128 = 2048 条。",
        "/* 一条记录 = 12 字节头 + 20 字节载荷 = 32 字节 */\n/* [0..4)  seq  自增序号，0xFFFFFFFF 表示空槽 */\n/* [4..8)  ts   时间戳（本工程用 RTC 秒） */\n/* [8..10) len  本载荷实际字节数 */\n/* [10..12) CRC16 低字节在前 */\n/* [12..32) 载荷：整帧 20 字节环境帧 */",
        "你能算出来：容量 2048 条、绕回周期、以及「一帧 20 字节刚好等于载荷，一点都不剩」这件事。",
        "载荷正好 20 字节 = 整帧长度，所以任何「再塞点别的」的想法都会返回 W25QXX_LOG_ERR_TOO_LONG；写满后是覆盖最旧的记录，不是停止写入——所以长时间断网会丢掉最早的数据，这是设计而不是 bug，但你要在阶段 8 用 W25QXX_LogLost() 把它统计出来。",
        "W25QXX_LOG_BASE / W25QXX_LOG_SECTORS / W25QXX_LOG_SLOT_SIZE / W25QXX_LOG_HDR_SIZE / W25QXX_LOG_PAYLOAD / W25QXX_LOG_EMPTY / W25QXX_LogCapacity"
      ],
      [
        "初始化与写入：LogInit 扫断点，LogWrite 追一条",
        "掉电可能发生在两次写之间，只有靠序号（seq）才能找出「写到哪了」。",
        "上电调一次 W25QXX_LogInit()：它内部扫描全部槽位找到写入断点，耗时约 2048 × 11us ≈ 22ms，已有记录不清除。之后每收到一帧就 W25QXX_LogWrite(ts, frame, 20) 追一条：库内部先写时间戳/长度/CRC/数据，最后才单独写 seq，所以中途断电的那一槽会被当成空槽，下次从它开始写。纯文本调试可用 W25QXX_LogWriteStr(ts, text)。",
        "if (W25QXX_LogInit() != W25QXX_LOG_OK) {\n    SYS_USART_SendLine(SYS_USART_1, \"log init failed (先 W25QXX_Init)\\r\\n\");\n}\n\nif (W25QXX_LogWrite(ts, frame20, 20U) == W25QXX_LOG_OK) {\n    s_cache_ok++;\n} else {\n    s_cache_fail++;\n}",
        "每收到一帧后 W25QXX_LogCount() 加 1；断电再上电后计数保持（不清零），说明 init 找到了断点；写 2048 条以上能看到计数停在容量上、W25QXX_LogLost() 开始增长。",
        "W25QXX_LogInit 之前必须先 W25QXX_Init，忘了会返回 W25QXX_LOG_ERR_NOT_INIT（2）而不是静默失败；写一条要「读-改-写」一整个 32 字节槽，203 条左右就会跨一次扇区擦除（擦一个 4KB 扇区），那一拍的耗时会明显变长，别把它放在高优先级任务的关键路径上。",
        "W25QXX_LogInit / W25QXX_LogWrite / W25QXX_LogWriteStr / W25QXX_LogCount / W25QXX_LOG_OK / W25QXX_LOG_ERR_NOT_INIT / W25QXX_LOG_ERR_TOO_LONG / W25QXX_LOG_ERR_FLASH"
      ],
      [
        "时间戳从哪来：RTC 秒 + 大端写入",
        "没有时间戳，补传上去的历史帧只能按到达顺序画，上位机分不清这是 10 分钟前还是刚采的。",
        "本板有一路 RTC，上电 SYS_RTC_Init() 起时钟（返回 1 表示首次上电、时间需要自己设），取时间用 SYS_RTC_GetTime(&t)，SysRtc_t 里有 year / month / day / hour / minute / second / weekday。要一个整数时间戳就用 SYS_RTC_GetUnix()，正好 4 字节，可以原样当 W25QXX_LogWrite 的 ts。补传时 0x15 的历史帧要求把时间戳按大端放进数据段前 4 字节。",
        "SysRtc_t now;\nSYS_RTC_GetTime(&now);\nuint32_t ts = SYS_RTC_GetUnix();\n\nif (W25QXX_LogWrite(ts, frame20, 20U) == W25QXX_LOG_OK) { /* 缓存成功 */ }\n\n/* 补传时拼 0x15 历史帧：4 字节大端时间戳 + 20 字节原帧 */\nhist[SYS_FRAME_HISTORY_TS_OFF + 0] = (uint8_t)(ts >> 24);\nhist[SYS_FRAME_HISTORY_TS_OFF + 1] = (uint8_t)(ts >> 16);\nhist[SYS_FRAME_HISTORY_TS_OFF + 2] = (uint8_t)(ts >> 8);\nhist[SYS_FRAME_HISTORY_TS_OFF + 3] = (uint8_t)(ts);",
        "串口打印 ts 每缓存一条就变大；把板子断电 10 秒再上电，ts 继续往前走（不是从 0 开始），说明走的是 RTC 而不是开机秒数。",
        "ts 的单位由调用方约定，库只原样保存、不做换算——所以写进去是「秒」就必须全程按秒解释，混用「开机毫秒」会让补传出来的时间全乱；SYS_RTC_Init 返回 1 时时间还是默认值，需要 SYS_RTC_SetTime 或 SYS_RTC_SetUnix 设一次，不设的话历史帧的时间戳会全是同一个值；0x15 里时间戳是大端，写反了上位机会显示 1970 年附近的时间。",
        "SYS_RTC_Init / SYS_RTC_GetTime / SYS_RTC_GetUnix / SYS_RTC_SetUnix / SysRtc_t / SYS_FRAME_HISTORY_TS_OFF / SYS_FRAME_HISTORY_FRAME_OFF / SYS_FRAME_HISTORY_LEN"
      ],
      [
        "读回与统计：LogRead / LogTail / LogStat / LogLost",
        "补传要按序号一条条读，还要随时能报出「现在积了多少、丢了多少」。",
        "W25QXX_LogRead(index, &ts, buf, &len) 按序号读一条（buf 传 0 表示只要时间戳与长度，非常省时间）；W25QXX_LogTail(&ts, buf, &len) 读最新一条；W25QXX_LogCount() 现有条数；W25QXX_LogStat(&count, &cap, &head) 一次拿到条数、容量和下一条要写的槽号；W25QXX_LogLost() 是写满绕回时丢掉的条数；W25QXX_LogCapacity() 是总容量 2048。",
        "uint16_t count, cap, head;\nuint32_t ts; uint8_t buf[W25QXX_LOG_PAYLOAD]; uint16_t n;\n\nW25QXX_LogStat(&count, &cap, &head);\nSYS_USART_Printf(SYS_USART_1, \"log %u/%u head=%u lost=%u\\r\\n\",\n                (unsigned)count, (unsigned)cap, (unsigned)head,\n                (unsigned)W25QXX_LogLost());\n\nif (W25QXX_LogTail(&ts, buf, &n) == W25QXX_LOG_OK) {\n    SYS_USART_Printf(SYS_USART_1, \"tail ts=%u len=%u\\r\\n\", (unsigned)ts, (unsigned)n);\n}",
        "写入 5 条后 count=5；用 LogTail 读出来的 ts 与最后写入的那条一致；超过 2048 条后 count 停在 cap，lost 开始增长。",
        "W25QXX_LogStat 只有 3 个出参，丢帧数不在里面，必须另外调 W25QXX_LogLost()（这个很容易漏，报出去的统计就会少一项）；LogRead 的序号是「环形序号」不是物理槽号，越界会返回 W25QXX_LOG_ERR_EMPTY；读到坏记录会返回 W25QXX_LOG_ERR_CORRUPT，补传时要把这一条跳过并计数，不要当成「读到头了」。",
        "W25QXX_LogRead / W25QXX_LogTail / W25QXX_LogCount / W25QXX_LogStat / W25QXX_LogLost / W25QXX_LogCapacity / W25QXX_LOG_ERR_EMPTY / W25QXX_LOG_ERR_CORRUPT / W25QXX_LOG_PAYLOAD"
      ],
      [
        "清空缓存：LogClear 与它的擦除代价",
        "上位机点一次「清缓存」就要等好几秒，不提前知道会以为程序死了。",
        "W25QXX_LogClear() 会把 16 个 4KB 扇区全擦一遍，约 0.5~1 秒。调用点必须是低优先级任务或命令处理里，并且擦除期间要挡住新的写入（用标志量或者暂时关掉缓存开关）。想只清一部分就别用 LogClear，改自己按扇区 W25QXX_EraseSector。",
        "/* 0x13 清缓存命令的处理：擦除期间必须先关写入 */\ns_cache_on = 0U;\nif (W25QXX_LogClear() == W25QXX_LOG_OK) {\n    SYS_USART_SendLine(SYS_USART_1, \"log cleared\\r\\n\");\n}\ns_cache_on = 1U;",
        "发一次清空命令，串口能看出「清空」打印前有约 1 秒的停顿；清完 W25QXX_LogCount() = 0；清空前先把缓存开关关掉的版本，清空过程中收到的帧不会被写进去也不会写坏。",
        "擦除期间去写同一个扇区，写进去的数据会被擦掉——现象是「清完发现还有几条」，其实是清完之后又写进去的；LogClear 返回错误时不要假设已经清了，把返回码打出来；别在 USART2 的中断里做清空。",
        "W25QXX_LogClear / W25QXX_EraseSector / W25QXX_EraseBlock / W25QXX_EraseChip / W25QXX_IsBlank / W25QXX_WaitBusy"
      ],
      [
        "补传：0x15 历史帧 / 0x16 启停 / 0x96 进度，以及去重",
        "补传是最容易把上位机搞崩的功能：发重了曲线会跳，发漏了对账对不上。",
        "上位机用命令 SYS_FRAME_CMD_REPLAY（0x16）启停：数据段第 1 字节 0 = 停止、1 = 开始（LEN = 0 也视为开始）。板2 接命令后把「开始时刻的总条数」记下来、逐条读并组 SYS_FRAME_CMD_HISTORY（0x15，LEN = SYS_FRAME_HISTORY_LEN = 24：4 字节大端时间戳 + 20 字节原帧），每 50ms 发一条，并周期性回 SYS_FRAME_REPLAY_ACK_CMD（= 0x16 | 0x80 = 0x96，LEN = 4）报进度：第 0 字节是状态 SYS_FRAME_REPLAY_ST_*，第 1~2 字节已发条数（低字节在前），第 3 字节剩余条数。",
        "#define REPLAY_PERIOD_MS   50U\n\n/* 收到 0x16(1)：把此刻的条数冻结成 total，从头开始，并关掉写入 */\ns_replay_i     = 0U;\ns_replay_total = W25QXX_LogCount();\ns_replay_sent  = 0U;\ns_cache_on     = 0U;\ns_replay_st    = SYS_FRAME_REPLAY_ST_REPLAYING;\n\n/* 每 50ms 一步：读一条 -> 组 0x15 -> 经互斥量发到 USART2 */\nif (s_replay_st == SYS_FRAME_REPLAY_ST_REPLAYING) {\n    uint32_t ts; uint8_t buf[W25QXX_LOG_PAYLOAD]; uint16_t n;\n    if (W25QXX_LogRead(s_replay_i, &ts, buf, &n) == W25QXX_LOG_OK) {\n        uint8_t hist[SYS_FRAME_HISTORY_LEN];\n        /* [0..4) 大端 ts，[4..24) 原帧 */\n        qt_send_frame((uint8_t)SYS_FRAME_CMD_HISTORY, hist,\n                      (uint16_t)SYS_FRAME_HISTORY_LEN);\n        s_replay_i++;\n        s_replay_sent++;\n    } else {\n        s_replay_bad++;          /* 坏记录：跳过并计数，不要当读到头 */\n        s_replay_i++;\n    }\n    if (s_replay_i >= s_replay_total) {\n        s_replay_st = SYS_FRAME_REPLAY_ST_DONE;\n        s_cache_on  = 1U;        /* 补传结束，恢复写入 */\n    }\n}",
        "上位机发 0x16(1)：能连续收到 0x15 帧、约 20 帧/秒；同时收到 0x96 进度帧，已发数单调增长、剩余数递减；发 0x16(0) 立即停。把同一批数据补传两次，上位机按时间戳去重后曲线不重复。",
        "补传期间必须关掉写入，否则新帧会挤进正在补传的区间，剩下的条数算不准；补传的开始条数要「冻结」成 total 并且补传过程中不再变，边补边涨会让「剩余」永远归不了零；上位机重发 0x16(1) 要按「重新开始」处理（序号归零），不是接着上次发；0x15 的 LEN 恒为 24，写成 20 会让上位机按错长度解析。",
        "SYS_FRAME_CMD_HISTORY / SYS_FRAME_CMD_REPLAY / SYS_FRAME_REPLAY_ACK_CMD / SYS_FRAME_REPLAY_ACK_LEN / SYS_FRAME_REPLAY_ST_IDLE / SYS_FRAME_REPLAY_ST_REPLAYING / SYS_FRAME_REPLAY_ST_DONE / SYS_FRAME_REPLAY_ST_ERR / SYS_FRAME_RA_ST / SYS_FRAME_RA_SENT_LO / SYS_FRAME_RA_SENT_HI / SYS_FRAME_RA_REMAIN_LO"
      ]
    ]
  },

  /* ======================= 板2 · 阶段 7 ======================= */
  {
    n: "🖥 板2 · 阶段 7｜本地显示与触摸",
    m: "板2 项目引导",
    _b: "板2",
    d: "让板2 自己有个屏：把转发/缓存状态显示出来，并能用触摸做几个开关；界面长什么样由 AI 定，你负责把数据交上去、把触摸校准准。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：收 / 转发 / 缓存 / 上云 / 补传 都已跑通。\n本阶段产出：LCD 显示任务、触摸原始坐标、校准、命中判定。\n下一阶段怎么用：阶段 8 联调时看屏上状态；阶段 9 双通道验收也靠这块屏。\n\n本阶段产物：ILI9481 点亮（320×480，FSMC NE4，RS = PF12、CS = PG12、WR = PD5、RD = PD4、背光 PB15）、能画点线矩形和文字、能显示实时数据、XPT2046 能报出按下坐标、触摸校准值能存进 AT24C02 并在下次开机取回。记住边界：显示的内容与布局由 AI 完成，你要写的是「数据怎么进显示任务」和「怎么校准、怎么验收」。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。\n本卡各步落点：\n· 第1步 点亮屏幕 → #include 进 ①；LCD_Init/BackLight/SetRotation 进 ⑤ 初始化段。\n· 第2步 画点线字 → 画图语句放 ④ 显示任务里。\n· 第3步 显示数据队列 → 消息 typedef 进 ③；队列句柄进 ②；xQueueCreate 进 ⑤。\n· 第4步 触摸原始坐标 → #include 进 ①；XPT2046_Init 进 ⑤；轮询循环放 ④ 触摸任务。\n· 第5步 校准 → 校准流程放 ④（或 ⑤ 上电一次性调用并存 AT24C02）。\n· 第6步 验收 → 命中判断与重绘放 ④ 显示任务。",
    k: "板2 TFT ILI9481 lcd LCD_Init LCD_SetRotation LCD_ShowString LCD_ShowFixed 触摸 xpt2046 XPT2046_Read XPT2046_GetEvent 校准 XPT2046_Calibrate CalibSave AT24C02 320x480 FSMC NE4 PB15",
    f: [
      [
        "点亮屏幕：LCD_Init 与旋转方向",
        "屏不亮要先分清「背光没开」还是「初始化没跑通」，两者现象相近、修法完全不同。",
        "调 LCD_Init()：内部会开 GPIO 与 FSMC 时钟、把数据/控制引脚复用成 FSMC、按 ILI9481 的上电序列初始化，最后清屏为黑。上电默认是竖屏 320×480（LCD_ROT_0）；想横屏用 LCD_SetRotation(LCD_ROT_90) 或 LCD_ROT_270。取当前尺寸一定用 LCD_GetWidth() / LCD_GetHeight()，不要直接用 LCD_WIDTH / LCD_HEIGHT 两个面板原生宏。",
        "#include \"lcd.h\"\n\nLCD_Init();\nLCD_BackLight(1);\nLCD_SetRotation(LCD_ROT_0);\n\nSYS_USART_Printf(SYS_USART_1, \"lcd rot=%u w=%u h=%u\\r\\n\",\n                (unsigned)LCD_GetRotation(),\n                (unsigned)LCD_GetWidth(), (unsigned)LCD_GetHeight());\n\nLCD_Clear(LCD_COLOR_BLACK);\nLCD_FillRect(0, 0, 100, 100, LCD_COLOR_GREEN);",
        "屏上出现一块 100×100 的绿色方块；串口打印 rot=0 w=320 h=480；切成 LCD_ROT_90 后打印 w=480 h=320。",
        "RS 用的是 FSMC_A6（本板 FSMC_A10 已给 SRAM 的 A17），所以「另一个地址」的偏移是 0x80，别照抄网上 RS = A10 的例子；LCD_WIDTH / LCD_HEIGHT 是面板原生竖屏尺寸、不随旋转变，拿它算横屏坐标会画到屏幕外；背光灭的时候屏上内容还在，别用「看不见」判断初始化失败。",
        "LCD_Init / LCD_BackLight / LCD_SetRotation / LCD_GetRotation / LCD_GetWidth / LCD_GetHeight / LCD_Clear / LCD_WIDTH / LCD_HEIGHT / LCD_ROT_0 / LCD_ROT_90"
      ],
      [
        "画点、线、矩形与文字",
        "显示任务里 90% 的绘制就是这几种；先把它们跑通，AI 给的界面代码你才改得动。",
        "画点 LCD_DrawPoint(x, y, color)，线 LCD_DrawLine(x0, y0, x1, y1, color)，空心矩形 LCD_DrawRect，实心块 LCD_FillRect；文字用 LCD_ShowString(x, y, str, fc, bc, mode)，数字用 LCD_ShowNum(x, y, num, len, fc, bc, mode)，定点小数用 LCD_ShowFixed(x, y, val, frac, int_len, fc, bc, mode)。颜色用 LCD_COLOR_xxx 宏或 LCD_RGB565(r, g, b)。mode = 1 用背景色铺满字格（刷新数值用这个），mode = 0 是叠加、背景处不画。",
        "LCD_Clear(LCD_COLOR_BLACK);\nLCD_ShowString(10, 10, \"BOARD2 OK\", LCD_COLOR_WHITE, LCD_COLOR_BLACK, 1U);\nLCD_ShowFixed (10, 40, 2531, 2U, 3U, LCD_COLOR_YELLOW, LCD_COLOR_BLACK, 1U);\nLCD_ShowNum   (10, 70, (uint32_t)W25QXX_LogCount(), 4U,\n               LCD_COLOR_CYAN, LCD_COLOR_BLACK, 1U);\nLCD_DrawRect(0, 0, 120, 100, LCD_COLOR_RED);",
        "屏上出现 “BOARD2 OK”、黄字 25.31、青字四位数；反复调用那条 ShowFixed 时数字变化但不会留下旧字符的残影。",
        "刷新数值必须用 mode = 1（用背景色铺 8×16 字格）或自己先 FillRect 擦掉旧内容，否则 2531 变 987 时会留下「9 7」的残影；LCD_ShowFixed 的 val 是「按 10^frac 缩放后的整数」，25.31 要传 2531 不是 25；LCD_FillRect 的 x1 / y1 是包含在矩形内的。",
        "LCD_DrawPoint / LCD_DrawLine / LCD_DrawRect / LCD_FillRect / LCD_ShowString / LCD_ShowNum / LCD_ShowFixed / LCD_ShowChar / LCD_RGB565 / LCD_COLOR_BLACK / LCD_COLOR_WHITE / LCD_COLOR_GREEN / LCD_COLOR_YELLOW / LCD_COLOR_CYAN / LCD_COLOR_RED"
      ],
      [
        "把数据交给显示任务：单写者 + 队列",
        "LCD 是慢外设（FSMC 一次写要几十纳秒但刷一屏是几十毫秒），两个任务同时画屏会出现花屏、坐标错乱。",
        "规矩是「只有显示任务碰 LCD」：转发/收帧任务把要显示的内容打包成定长结构（比如 6 个数值 + 状态标志 + 缓存条数）塞进队列，显示任务以 200~500ms 周期取最新一份、整块重绘。不需要历史就用深度 1 的队列 + 覆盖式发送（队列满就丢掉旧的）。这一步具体画什么由 AI 定，你只要保证数据和绘制是解耦的。",
        "typedef struct {\n    int16_t  temp_x100, humi_x100, press_x10;\n    uint16_t light, tvoc, mq135;\n    uint16_t cache_cnt, replay_sent;\n    uint8_t  net_online, replay_st;\n} DispMsg_t;\n\nstatic QueueHandle_t s_q_disp;\n\nstatic void vTaskDisplay(void *pv) {\n    DispMsg_t d;\n    for (;;) {\n        if (xQueueReceive(s_q_disp, &d, pdMS_TO_TICKS(500)) == pdPASS) {\n            /* 整块重绘：AI 给的界面代码放这里 */\n            LCD_ShowFixed(10, 40, d.temp_x100, 2U, 3U,\n                          LCD_COLOR_YELLOW, LCD_COLOR_BLACK, 1U);\n        }\n        LED_BlinkUpdate();      /* 顺带把 1ms 的闪灯服务放进来（只要周期够准）*/\n    }\n}",
        "拔掉 LoRa 让数据停止更新：屏上的数字保持不变、不花屏、界面不卡；恢复后数字继续更新，没有跳字残影。",
        "不要在显示任务里做长耗时的整屏 Clear + 重画（会闪），只重画变化的区域或整块重绘一个区域；1ms 的 LED_BlinkUpdate 需要稳定周期，放显示任务里只有在它能按时醒来的前提下才成立，否则单独用定时器任务；DispMsg_t 必须是定长、不能含指针。",
        "xQueueCreate / xQueueReceive / xQueueSend / LCD_ShowFixed / LED_BlinkUpdate / vTaskDelay"
      ],
      [
        "触摸：先拿原始坐标，再谈按钮",
        "触摸最常见的问题是「坐标不对」，而不是「收不到按下」；把这两件事分开验最快。",
        "XPT2046_Init() 之后：XPT2046_IsPenDown() 判有没有按住；XPT2046_ReadRawFiltered(&rx, &ry, n, trim) 取滤波后的原始坐标（默认采 XPT2046_SAMPLE_N = 5 次、去掉 XPT2046_SAMPLE_TRIM = 1 个极值）；XPT2046_Read(&x, &y) 取校准后的屏幕坐标；XPT2046_GetEvent(&x, &y) 返回按下/抬起事件，配合 XPT2046_PenWaitDown / XPT2046_PenWaitUp 做等待。",
        "uint16_t rx, ry, x, y;\n\nXPT2046_Init();\nXPT2046_CalibDefault();     /* 先用兜底校准值，只为看原始范围 */\n\nwhile (1) {\n    if (XPT2046_IsPenDown() != 0U) {\n        XPT2046_ReadRawFiltered(&rx, &ry,\n                    (uint8_t)XPT2046_SAMPLE_N, (uint8_t)XPT2046_SAMPLE_TRIM);\n        XPT2046_Read(&x, &y);\n        SYS_USART_Printf(SYS_USART_1, \"raw=%u,%u  scr=%u,%u\\r\\n\",\n                        (unsigned)rx, (unsigned)ry, (unsigned)x, (unsigned)y);\n    }\n    SYS_TICK_Delay_ms(50);\n}",
        "笔尖从屏的左到右、上到下滑一遍，raw 的 X 与 Y 分别按一个方向单调变化，屏幕坐标也要跟着变；不按时没有输出。",
        "触摸是独立于 LCD 的一路 SPI（T_CS = PC13、T_CLK = PB0、T_DIN = PF11、T_DOUT = PB2、T_PEN = PB1），跟 LCD 的 FSMC 完全两码事，别指望 LCD_Init 会帮你初始化它；原始坐标范围（0~4095）和屏幕像素范围不是一个东西，所以 XPT2046_CalibDefault 只是兜底、精度一般，正式用必须自己校准；XPT2046_SCREEN_W / H = 240 / 320 是触摸模块自己声明的范围，与本板 320×480 的面板不是一回事，别拿它当屏幕尺寸用。",
        "XPT2046_Init / XPT2046_IsPenDown / XPT2046_ReadRaw / XPT2046_ReadRawFiltered / XPT2046_Read / XPT2046_GetEvent / XPT2046_PenWaitDown / XPT2046_PenWaitUp / XPT2046_CalibDefault / XPT2046_SAMPLE_N / XPT2046_SAMPLE_TRIM / XPT2046_SCREEN_W / XPT2046_SCREEN_H"
      ],
      [
        "校准触摸：采四角、算范围、存进 AT24C02",
        "不校准的触摸点不准，按错按钮比不能按还糟；而且校一次就应该记住。",
        "步骤：① 在屏上画四个角的目标点，提示用户依次点；② 每点一次用 XPT2046_ReadRaw 记下原始坐标；③ 取最左/最右/最上/最下的原始值当作 x_min / x_max / y_min / y_max；④ 方向不对就改 swap_xy / invert_x / invert_y；⑤ XPT2046_Calibrate(x_min, x_max, y_min, y_max, swap_xy, invert_x, invert_y) 写入；⑥ XPT2046_CalibSave() 存进 AT24C02，下次开机 XPT2046_CalibLoad() 取回；XPT2046_IsCalibrated() 用来判断要不要重新校。整表读写用 XPT2046_SetCalib / XPT2046_GetCalib（XptCalib_t）。",
        "XptCalib_t c;\nuint16_t rx, ry;\n\nXPT2046_CalibDefault();\nXPT2046_GetCalib(&c);\nSYS_USART_Printf(SYS_USART_1, \"x=[%u,%u] y=[%u,%u] swap=%u\\r\\n\",\n                (unsigned)c.x_min, (unsigned)c.x_max,\n                (unsigned)c.y_min, (unsigned)c.y_max, (unsigned)c.swap_xy);\n\n/* 采完四角后 */\nXPT2046_Calibrate(200U, 3800U, 250U, 3850U, 0U, 0U, 0U);\nSYS_USART_Printf(SYS_USART_1, \"calibrated=%u save=%u\\r\\n\",\n                (unsigned)XPT2046_IsCalibrated(), (unsigned)XPT2046_CalibSave());",
        "点屏上四个角，串口打印的原始坐标四角能围出一个稳定的矩形；CalibSave 返回成功；断电重上电后 XPT2046_IsCalibrated() 仍然是真、点哪儿落哪儿，偏差在几个像素内。",
        "CalibSave / CalibLoad 依赖 AT24C02（宏 XPT2046_USE_EEPROM = 1 时才编进去），用之前要先 AT24C02_Init(speed) 把 EEPROM 起来，否则保存失败但返回值可能让你以为是别的问题；四角采点时手不能抖——采到一次坏点会让整块屏都偏，最好每个角采多次取中位；上下或左右颠倒时先改 invert_x / invert_y，屏装反 90° 才用 swap_xy。",
        "XPT2046_Calibrate / XPT2046_SetCalib / XPT2046_GetCalib / XPT2046_CalibSave / XPT2046_CalibLoad / XPT2046_IsCalibrated / XptCalib_t / XPT2046_ReadRaw / AT24C02_Init / XPT2046_USE_EEPROM"
      ],
      [
        "验收触摸与显示：能看见现象才算过",
        "触摸的坑很隐蔽——「能点」和「点得准」是两回事，必须用可重复的方法验。",
        "准备一套固定验收动作：① 画一个 3×3 网格的按钮块（每块 80×80），按下时把该块填成另一种颜色并打印块号；② 从左到右、从上到下依次点九个块，九个块号必须按顺序打印、不能跳号也不能一次触发两个；③ 在屏四边中点各点一次，坐标应落在边缘附近；④ 连续点同一位置 20 次，打印的屏幕坐标最大最小差不大于几个像素。",
        "/* 命中判断：把校准后的屏幕坐标丢进格子里 */\nuint8_t hit = (uint8_t)((y / 80U) * 3U + (x / 80U));\nLCD_FillRect((uint16_t)(x / 80U) * 80U, (uint16_t)(y / 80U) * 80U,\n             (uint16_t)(x / 80U) * 80U + 79U, (uint16_t)(y / 80U) * 80U + 79U,\n             LCD_COLOR_BLUE);\nSYS_USART_Printf(SYS_USART_1, \"hit=%u at %u,%u\\r\\n\",\n                (unsigned)hit, (unsigned)x, (unsigned)y);",
        "九个格子依次点过去，串口打印 0~8 不重不漏；四边中点落在边缘；同一点连点 20 次坐标抖动只有几个像素。",
        "不要用「按下就立刻触发动作」做验收——按下时坐标还在抖，应该用按下事件定位、抬起事件确认（XPT2046_GetEvent 会有 DOWN 与 UP 两种事件）；屏是 320×480 而触摸是竖屏装的，横屏显示（LCD_ROT_90）时要把触摸坐标换算一次，否则点左边亮右边；界面的具体布局与配色由 AI 定，不要自己发明一套再去对不上。",
        "XPT2046_GetEvent / XPT2046_EVENT_DOWN / XPT2046_EVENT_UP / XPT2046_EVENT_NONE / LCD_FillRect / LCD_SetRotation / XPT2046_CH_X / XPT2046_CH_Y"
      ]
    ]
  },

  /* ======================= 板2 · 阶段 8 ======================= */
  {
    n: "✅ 板2 · 阶段 8｜联调验收",
    m: "板2 项目引导",
    _b: "板2",
    d: "把板1、板2、上位机连成一条链跑起来，用断网、长时间运行和故障注入把问题逼出来。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：全部链路 + 显示。\n本阶段产出：整链验收、断网 30 秒对账、长跑水位、故障现场、逐条验收清单。\n下一阶段怎么用：阶段 9 把云与串口两条通道同时跑，本阶段的统计口径继续沿用。\n\n本阶段产物：板1 → 板2 → 上位机整链连通并稳定；断网 30 秒能看到缓存增长、恢复后能补传且不重不漏；连续跑够时间后堆与栈都有余量、丢帧数可解释；看门狗与故障现场在需要时真的能咬到、能留下原因。最后形成一份你自己的验收清单。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。\n本卡各步落点：\n· 第1步 整链跑通 → 汇总打印放 ④ Monitor 或各自任务里，别在 main。\n· 第2步 断网 30 秒 → 联动逻辑放 ④ cloud 任务；net_online 进 ②。\n· 第3步 补传对账 → 放 ④ 补传任务。\n· 第4步 长时间跑 → 水位/堆/栈打印放 ④ Monitor。\n· 第5步 看门狗与故障现场 → 回调放 ④；故障标志与缓冲区进 ②。\n· 第6步 验收清单 → check() 写成 static 辅助函数放 ④；汇总在 ④ Monitor。",
    k: "板2 联调验收 整链 板1 板2 上位机 断网 30秒 缓存 补传 水位 丢帧 堆 栈 看门狗 故障现场 验收清单",
    f: [
      [
        "整链跑通：板1 → 板2 → 上位机",
        "分开能跑不等于连起来能跑；接口对不上、命令字写错，只有联调才暴露。",
        "按顺序上电并逐段确认：① 板1 上电、USART1 打印出采集值；② 板2 上电，USART1 打印 LoRa 自检通过、收到帧计数开始增长；③ 板2 的 USART2 接上位机，上位机界面出现数据并开始画曲线。全程把每一段的计数都打印出来。",
        "SYS_USART_Printf(SYS_USART_1, \"lora_ok=%u lora_bad=%u fwd=%u cache_ok=%u\\r\\n\",\n                (unsigned)s_rx_ok, (unsigned)s_rx_bad,\n                (unsigned)s_fwd_ok, (unsigned)s_cache_ok);",
        "上位机曲线连续、与板1 的显示值一致；板1 每发一帧，板2 的 lora_ok 加一、fwd 也加一；拔掉板1 的 LoRa 天线，板2 的 fwd 停止增长。",
        "三段用三个不同的串口速率（LoRa 9600 / 上位机与调试都 115200），别把调试口接到上位机口上，否则上位机会收到一堆调试文字；先确认板1 侧的帧总长就是 20 字节，长度对不上板2 会全部判成长度错误。",
        "SYS_FRAME_CMD_ENV / SYS_FRAME_ENV_LEN / SYS_FRAME_MAX_PAYLOAD / SYS_USART_1 / SYS_USART_2 / LORA_E22_Recv"
      ],
      [
        "断网 30 秒：看缓存增长与补传",
        "断网补传是板2 存在的理由，这段必须在真实断网下演一遍。",
        "① 先让 MQTT_IsConnected() 为 1；② 断开热点（不是拔板子电源）；③ 保持板1 继续发数据 30 秒；④ 观察 W25QXX_LogCount() 每收到一帧 +1，而 MQTT 侧不再发布；⑤ 恢复热点，等重连成功；⑥ 从上位机发 0x16(1) 触发补传，看 0x15 历史帧流回上位机、0x96 进度帧报数。",
        "/* 断网判断与缓存联动 */\nif (MQTT_IsAlive() != 0U && MQTT_IsConnected() != 0U) {\n    net_online = 1U;\n    (void)MQTT_PublishFloat(\"data/f407-001/temp\", (float)temp_x100 / 100.0f, 2U);\n} else {\n    net_online = 0U;      /* 只缓存，不发布 */\n}\n\nif ((s_cache_on != 0U) && (W25QXX_LogWrite(ts, frame20, 20U) == W25QXX_LOG_OK)) {\n    s_cache_ok++;\n}",
        "断网 30 秒、板1 每 2 秒一帧，缓存条数大约增加 15 条（按实际帧率算），串口两侧的计数能对上；恢复后补传出来的条数与断网期间缓存条数一致。",
        "断网期间一定要「同时」停止发布和开始缓存，只做一半就会出现「以为在上云其实没发」或者「缓存里空了一段」；重连后不要立刻开始补传，先确认 MQTT_IsConnected 与 IsAlive 都为真；W25QXX_LogCount 在补传期间会因为你关掉了写入而不再增长，这是对的，别以为卡住了。",
        "MQTT_IsConnected / MQTT_IsAlive / MQTT_PublishFloat / W25QXX_LogWrite / W25QXX_LogCount / SYS_FRAME_CMD_REPLAY / SYS_FRAME_CMD_HISTORY"
      ],
      [
        "补传对账：条数、时间戳、原帧、去重",
        "「看起来补上了」不等于「补对了」；差一条、时间戳乱一个，后面所有分析都不可信。",
        "做一次完整对账：① 记录缓存总数与补传起始时的冻结总数；② 补传结束后看 0x96 最后一帧的已发数是否等于冻结总数；③ 抽查几条：把板2 缓存里读出的时间戳与 0x15 帧里解出的时间戳对比；④ 把 0x15 里的 20 字节原帧再走一次 SYS_FRAME_Verify 确认 CRC 没错；⑤ 上位机按时间戳去重后条数应等于原始条数。",
        "uint32_t ts; uint8_t buf[W25QXX_LOG_PAYLOAD]; uint16_t n;\n\nif (W25QXX_LogRead(idx, &ts, buf, &n) == W25QXX_LOG_OK) {\n    SYS_USART_Printf(SYS_USART_1, \"slot %u ts=%u len=%u crc=%u\\r\\n\",\n        (unsigned)idx, (unsigned)ts, (unsigned)n,\n        (unsigned)SYS_FRAME_Verify(buf, n));   /* 期望 SYS_FRAME_OK */\n}",
        "抽查的每条都 crc=0；0x15 帧数量 + 坏记录数 = 冻结总数；上位机去重后条数与缓存条数一致。",
        "补传过程中如果又收到了新帧，新帧会挤进「剩余」的区间导致条数算不准——所以补传期间必须关写入，这一点阶段 6 已经定了，联调时要再确认一次；上位机重发 0x16(1) 时板2 必须把序号归零重新开始，否则会从上次的位置继续；坏记录要计数上报，不能悄悄跳过——悄悄跳过最后一定对不上账。",
        "W25QXX_LogRead / SYS_FRAME_Verify / SYS_FRAME_OK / SYS_FRAME_HISTORY_LEN / SYS_FRAME_HISTORY_TS_OFF / SYS_FRAME_RA_SENT_LO / SYS_FRAME_RA_SENT_HI / SYS_FRAME_RA_REMAIN_LO / SYS_FRAME_REPLAY_ST_DONE"
      ],
      [
        "长时间跑：水位、堆、栈、丢帧",
        "短时间跑没问题，不代表跑两小时没问题；内存泄漏、环形缓冲绕回、串口拥塞都是慢性的。",
        "让整链连续跑够时间（至少 1 小时，最好过夜），每小时记录一次：W25QXX_LogStat 的 count / cap / head、W25QXX_LogLost、xPortGetFreeHeapSize()、每个任务的 uxTaskGetStackHighWaterMark、以及转发/缓存的成功与失败计数。看趋势而不是看单点值。",
        "uint16_t count, cap, head;\nW25QXX_LogStat(&count, &cap, &head);\nSYS_USART_Printf(SYS_USART_1,\n    \"t=%us log=%u/%u head=%u lost=%u heap=%u stack=%u\\r\\n\",\n    (unsigned)(SYS_TICK_GetTick() / 1000U),\n    (unsigned)count, (unsigned)cap, (unsigned)head,\n    (unsigned)W25QXX_LogLost(), (unsigned)xPortGetFreeHeapSize(),\n    (unsigned)uxTaskGetStackHighWaterMark(NULL));",
        "堆剩余在头几分钟下降后保持稳定（不持续下降 = 没有泄漏）；每个任务剩余栈 > 50 字；丢帧数只在缓存绕回后才增长，且增长速率可解释（= 新帧率）。",
        "堆持续下降到 0 会触发 configUSE_MALLOC_FAILED_HOOK，之后的行为不可预期，所以「缓降」也要当问题查；环形缓冲写满后 LogCount 会停在容量上不再涨，别把这个当卡死——要看 head 是否还在动、LogLost 是否在涨；剩下栈水位是「历史最低剩余」，取值时先用 uxTaskGetStackHighWaterMark(NULL) 拿当前任务、传别的句柄拿别的任务。",
        "W25QXX_LogStat / W25QXX_LogLost / xPortGetFreeHeapSize / uxTaskGetStackHighWaterMark / SYS_TICK_GetTick / configUSE_MALLOC_FAILED_HOOK"
      ],
      [
        "看门狗与故障现场：故意把它咬出来一次",
        "没被验证过的看门狗等于没有看门狗；故障现场不试一次，真出事时你也读不懂。",
        "① 人为在一个任务里写一个不喂心跳的死循环，确认 4 秒后整板复位，并且复位后 USART1 打印的复位原因是看门狗；② 人为制造一次空指针访问，确认复位后能通过 SYS_FAULT_Happened() / SYS_FAULT_Report 拿到故障现场文字；③ 恢复代码，确认计数从 0 重新开始且不再复位。",
        "if (SYS_FAULT_Happened() != 0U) {\n    char buf[160];\n    if (SYS_FAULT_Report(buf, (uint32_t)sizeof(buf)) != 0U) {\n        SYS_USART_Printf(SYS_USART_1, \"fault: %s\\r\\n\", buf);\n    }\n    SYS_FAULT_Clear();\n}",
        "死循环版本：约 4 秒后复位，复位原因打印出独立看门狗，且复位后心跳计数 pending 能重新归零；空指针版本：复位后打印出故障现场那一行字。",
        "调试打断点时先关狗或延长超时，否则一暂停就复位，你会以为代码有问题；SYS_FAULT_Clear 要在打印之后调，先清就什么也看不到了；APP_WDG_TIMEOUT_MS 是应用级超时（本工程按 4000ms 用），和硬件看门狗的分频要一起核对，两处都对才算真的管用。",
        "SYS_WDG_Init / SYS_WDG_Feed / SYS_WDG_ResetCause / SYS_WDG_ResetCauseDecode / SYS_WDG_ClearResetFlags / SYS_WDG_Heartbeat / SYS_FAULT_Happened / SYS_FAULT_Report / SYS_FAULT_Clear"
      ],
      [
        "你自己的板2 验收清单",
        "把上面所有检查项落成一张表，以后每次改完代码重跑一遍，就能确定「这次改动有没有弄坏别的」。",
        "照本工程 main.c 的组织方式，把验收做成可重复跑的自测项：每项打印一行「项名 + 实测值 + 通过与否」，全部跑完打印一行汇总。清单至少包含：LoRa 自检、Flash ID、LogInit、收帧计数与 CRC 错误计数、USART2 转发计数、ESP8266 AT、连 AP、拿 IP、MQTT connack、发布一次、断网缓存计数、补传条数与 0x96 进度、LCD 尺寸与旋转、触摸九宫格命中、看门狗心跳全体到齐。",
        "static uint8_t check(const char *name, uint8_t ok)\n{\n    SYS_USART_Printf(SYS_USART_1, \"[%s] %s\\r\\n\", ok ? \"PASS\" : \"FAIL\", name);\n    return ok;\n}\n\ncheck(\"LORA_E22_Init\",         LORA_E22_Init() == LORA_E22_OK);\ncheck(\"W25QXX_Init\",           W25QXX_Init(0U) == 0U);\ncheck(\"W25QXX_LogInit\",        W25QXX_LogInit() == W25QXX_LOG_OK);\ncheck(\"MQTT_IsConnected\",      MQTT_IsConnected() != 0U);\ncheck(\"LCD 320x480\",           (LCD_GetWidth() == 320U) && (LCD_GetHeight() == 480U));",
        "跑一遍自测，每一项都是 [PASS]；拔掉 LoRa 天线重跑，只有 LoRa 那一项变 [FAIL]，其它项不受影响——这说明各项互相独立、清单可信。",
        "自测项之间要有依赖顺序（W25QXX_LogInit 必须在 W25QXX_Init 之后、MQTT_IsConnected 必须在一次成功的 Connect 之后），顺序错了会得到一堆假 FAIL；自测里不要用会阻塞十几秒的 ESP8266_JoinAP 反复跑，网络项只做一次、把结果缓存下来；清单只由你维护，界面与进度文档的排版由 AI 完成。",
        "LORA_E22_Init / LORA_E22_OK / W25QXX_Init / W25QXX_ReadID / W25QXX_LogInit / W25QXX_LOG_OK / ESP8266_TestAT / ESP8266_GetIP / MQTT_ConnectSimple / MQTT_IsConnected / LCD_GetWidth / LCD_GetHeight"
      ]
    ]
  },
  /* ======================= 板2 · 阶段 9 ======================= */
  {
    n: "🧭 板2 · 阶段 9｜双通道并行：ESP8266 上云与上位机串口同时跑",
    m: "板2 项目引导",
    _b: "板2",
    d: "最后一段：让「云端」和「上位机」两条路同时活着。给 ESP8266 单开一个任务、定 30 秒上云节拍、给上行帧加去重序号，并按 2048 条约 34 分钟的真实容量做验收。FWLIB 侧的缺陷由 AI 修，你只写 main.c、任务划分与协议处理。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：单通道全部跑通（收、转发、缓存、上云、补传）。\n本阶段产出：独立的 vTaskCloud + 上云队列 + 30 秒节拍 + 去重序号 + 死链重连 + 四场景验收。\n下一阶段怎么用：无（板2 终点）；下一段是双板联调，帧格式与去重规则由本阶段定，之后不许私改。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。\n本卡各步落点：\n· 第1步 三条链路与唯一争用点 → 无代码；确认争用点在 ④ 的发送路径。\n· 第2步 vTaskCloud → 新任务函数写进 ④（main 之前）。\n· 第3步 s_q_cloud → CloudMsg_t 进 ③；队列句柄进 ②；xQueueCreate 进 ⑤。\n· 第4步 上云节拍 → last 局部变量与循环放 ④ vTaskCloud。\n· 第5步 去重键 → s_up_seq 进 ②；写进帧的动作在 ④。\n· 第6步 断网能扛多久 → 汇总打印放 ④ Monitor。\n· 第7步 死链判定与重连 → 放 ④ vTaskCloud。\n· 第8步 双通道上板验收 → 汇总与判定放 ④ Monitor。",
    k: "板2 双通道 上云 ESP8266 MQTT vTaskCloud 上云队列 30秒节拍 去重序号 断网缓存 2048条34分钟 死链重连 KeepAlive 验收场景",
    f: [
      [
        "双通道同时「同时」在哪：三条链路与唯一争用点",
        "三条链路一起跑时，谁都用 ESP8266 就会把它变成瓶颈；先把数据流画清，才不会再写出「两条都慢」的代码。",
        "先画清三条链路再动手：1) LoRa(USART6，9600) 收板1 的 20 字节帧 → vTaskForward 原样发 USART2(上位机) → 同时加时间戳写 W25Q128；2) 上云端走 USART3 上的 ESP8266，只由 vTaskCloud 一个任务使用；3) 上位机下来的命令与历史补传仍走 USART2。三条链路真正共用的只有两样东西：W25QXX 缓存，和 USART2 的写权。",
        "LoRa(USART6,9600) --20B帧--> vTaskLoraRx --队列--> vTaskForward --USART2--> 上位机\n                                             \\--> 时间戳+原帧 --> W25Q128 缓存\n缓存/最新帧 --s_q_cloud--> vTaskCloud --USART3--> ESP8266 --MQTT--> 云端\n上位机 --0x16/0x15--> vTaskCmdRx/vTaskReplay --USART2--> 上位机",
        "拔掉 ESP8266 模块：LoRa → 上位机照常收帧、缓存照常写，只是云端没有数据；插回模块后上云自己恢复。这条现象就是「两条链路真的独立」的判据。",
        "1) 不要为了少建一个任务把上云塞进 vTaskForward 或 vTaskReplay：ESP8266 的一次 AT 调用最长要等 900 ms 超时，转发一帧才 20 字节，优先级再高也救不回来；2) 上云不要另开一路串口，USART3 就是 ESP8266 的，PB10/PB11 与 I2C2 复用，用 ESP8266 就别再用 I2C2。",
        "SYS_USART_Init / SYS_FRAME_Send / W25QXX_LogWrite / ESP8266_TestAT"
      ],
      [
        "给 ESP8266 单开一个任务（vTaskCloud）",
        "AT 指令是「一问一答」，两个任务同时发指令会把彼此的应答吃掉，这是双通道最容易踩的并发问题。",
        "沿用阶段 3 的建法：用 xTaskCreate 建 vTaskCloud，栈 512 字、优先级取 4（低于 vTaskMonitor 的 5，不低于 vTaskCmdRx 的 3）；把 esp8266 与 mqtt 的所有调用都收在这一个任务里，其它任务一律不碰这两个模块。",
        "static void vTaskCloud(void *pv)\n{\n    (void)pv;\n    for (;;) {\n        if (ESP8266_TestAT() != 0U) {\n            SYS_USART_SendLine(SYS_USART_1, \"esp lost\\r\\n\");\n        }\n        vTaskDelay(pdMS_TO_TICKS(30000));\n    }\n}\n\nxTaskCreate(vTaskCloud, \"Cloud\", 512, NULL, 4, NULL);",
        "USART1 上能看到 Cloud 任务自己的打印；拔掉 ESP8266 后只有它报错，vTaskForward 的转发计数继续照涨。跑一小时后看 uxTaskGetStackHighWaterMark，剩余栈要大于 50 字。",
        "1) 栈给 256 字会在解析 AT 应答时溢出，症状是「偶尔复位」，先按 512 字跑稳；2) 优先级不要高于 vTaskMonitor，否则监控任务报不上心跳，看门狗会把整板复位；3) 上 FreeRTOS 后不要再调 SYS_TICK_Init，滴答由内核接管。",
        "xTaskCreate / ESP8266_TestAT / uxTaskGetStackHighWaterMark / SYS_WDG_Heartbeat"
      ],
      [
        "一条上云队列 s_q_cloud（值拷贝，不是指针）",
        "转发任务不能在网络上等；中间必须有一个定长队列把「要发的东西」存下来，否则上云一卡就丢数据。",
        "1) 定义定长结构（时间戳 4 B + 20 字节原帧）；2) 用 xQueueCreate 建 8 个元素的队列；3) 转发任务用带超时的 xQueueSend 投递，返回不是 pdPASS 就累加丢弃计数；4) vTaskCloud 用 xQueueReceive 取，取到再发布。",
        "typedef struct { uint32_t ts; uint8_t frame[20]; } CloudMsg_t;\nstatic QueueHandle_t s_q_cloud;\n\ns_q_cloud = xQueueCreate(8, sizeof(CloudMsg_t));\n\n/* 转发任务里投递 */\nCloudMsg_t m;\nm.ts = ts;\nmemcpy(m.frame, frame20, 20U);\nif (xQueueSend(s_q_cloud, &m, pdMS_TO_TICKS(5)) != pdPASS) { s_cloud_drop++; }\n\n/* vTaskCloud 里取用 */\nif (xQueueReceive(s_q_cloud, &m, pdMS_TO_TICKS(1000)) == pdPASS) { /* 发布这一条 */ }",
        "队列满时 s_cloud_drop 增长而转发计数不受影响；临时把 vTaskCloud 的发布停掉，能看到队列很快满、丢弃计数上涨、缓存照常写。",
        "1) 队列元素必须是定长值拷贝，传指针会指向被下一次接收复用的缓冲，症状是「云端收到的全是最后一条」；2) xQueueSend 不要用 portMAX_DELAY，队列满时无限等会反过来把转发任务拖死；3) 队列深度按「一次网络抖动最多积压几条」定，8 条够 30 秒节拍用。",
        "xQueueCreate / xQueueSend / xQueueReceive / pdPASS / pdMS_TO_TICKS"
      ],
      [
        "上云节拍：30 秒一帧，越限立刻插播",
        "1 秒一帧对这个项目没意义：云端看不到更多信息，却把 MQTT 连接与 WiFi 占满，还让断网缓存的时间口径算不清。",
        "在 vTaskCloud 里做一个 30 秒节拍：节拍到点且链路在线就发一条最新状态；阈值越限时不节日拍、立刻插一帧告警。发布内容先用 MQTT_PublishFloat 发温度这种单值 topic，等节拍稳定后再换成整帧上传。",
        "TickType_t last = xTaskGetTickCount();\n\nfor (;;) {\n    if (MQTT_IsConnected() != 0U) {\n        (void)MQTT_PublishFloat(\"env/f407-002/temp\", (float)temp_x100 / 100.0f, 2U);\n    }\n    vTaskDelayUntil(&last, pdMS_TO_TICKS(30000));\n}",
        "手机上装一个 MQTT 客户端订阅同一个 topic，每 30 秒收到一条；把温度推过阈值，立刻多收一条（不必等 30 秒）。",
        "1) vTaskDelay 是「发完再睡 30 秒」，发布耗时会让周期越拖越长，要精确就用 xTaskDelayUntil 对齐固定节拍；2) 断网时不要继续调用发布，一次 AT 超时 900 ms 会把整个任务的节拍打乱，先判 MQTT_IsConnected 再发；3) 上报周期一旦改了，缓存能扛多久的口径要跟着改。",
        "MQTT_PublishFloat / MQTT_IsConnected / vTaskDelay / pdMS_TO_TICKS"
      ],
      [
        "去重键：给上行帧加 2 字节序号（先定协议再动手）",
        "同一帧有两条路（实时上云 + 断网补传），云端与上位机都会收到重复；只靠时间戳去重，同一秒两帧会被误判成重复而丢掉一条。",
        "1) 先把字段位置、字节序、复位规则定死并写进协议文档：序号加在板2 的「上行帧」里，板1 的 20 字节环境载荷不动，这样板1 固件不用改；2) 板2 侧每次组上行帧时自增序号并填入，再算 CRC；3) 补传时原样带出缓存里的序号，不重新编号。",
        "static uint16_t s_up_seq = 0U;\nuint16_t seq = ++s_up_seq;\n\n/* 组上行帧时把 seq 写进协议约定的 2 字节位置，再算 CRC 后发送 */\n/* 实时帧与补传帧使用同一套序号规则，订阅端按序号去重 */",
        "断网 2 分钟再补传：订阅端把实时帧与补传帧合起来按序号去重，总条数 = 实际帧数（不重不漏）；重启后序号从 1 重新开始，且订阅端能识别出「会话变了」。",
        "1) 只改板2 不改上位机与订阅端，多出来的 2 字节会被当成 CRC 错，先定协议版本再动手；2) 序号复位（重启、0x16 重发补传）必须有明确规则并通知订阅端，否则会被判成「序号回退 = 重复」整段丢掉；3) 组帧与解析两侧要同时改，验收时用 W25QXX_LogRead 读出的原帧再走一次 SYS_FRAME_Verify 确认 CRC。",
        "SYS_FRAME_Send / SYS_FRAME_Verify / SYS_FRAME_HISTORY_LEN / W25QXX_LogRead"
      ],
      [
        "断网到底能扛多久：2048 条约 34 分钟，不是文档里写的 16 小时",
        "容量口径错了，验收标准就是假的：按「16 小时」去测，测到一半一定对不上账。",
        "自己算一遍并打印出来：用 W25QXX_LogStat 拿容量与当前条数，按板1 的实际帧率换算成时间；把这个数写进你自己的进度文档，并观察写满之后 W25QXX_LogLost 的增长（绕圈覆盖，不是卡死）。",
        "uint16_t count, cap, head;\nW25QXX_LogStat(&count, &cap, &head);\nSYS_USART_Printf(SYS_USART_1, \"log %u/%u head=%u lost=%u\n\",\n                (unsigned)count, (unsigned)cap, (unsigned)head,\n                (unsigned)W25QXX_LogLost());\n/* 可扛时长 = cap x 帧周期秒数：2048 x 1 秒 约 34 分钟 */",
        "拔网 10 分钟，条数约 600（板1 2 秒 1 帧）；写满 cap 之后 count 停住、LogLost 开始涨，重新联网补传能读到的仍是最近 cap 条。",
        "1) 缓存写满后绕圈覆盖，补传上去的是一段「没有开头」的数据，对账必须按 cap 封顶算，不能按断网总时长算；2) LogLost 增长是正常的绕圈现象，不是故障；3) 缓存写入与擦除期间整机调度会被挂起（FWLIB 侧由 AI 改成不挂调度器的写法），在这之前别在写缓存的任务里做实时性要求高的事。",
        "W25QXX_LogStat / W25QXX_LogLost / W25QXX_LogRead / SYS_FRAME_HISTORY_LEN"
      ],
      [
        "死链判定与重连：60 秒心跳、120 秒无字节判死",
        "TCP 已经断了、本地还以为在线，是上云最常见的「假在线」；不判死就只会一直发失败。",
        "在 vTaskCloud 里维护三个状态：1) 在线：正常发布；2) 可疑：MQTT_IsAlive 为假，或 120 秒没有收到任何字节 → 先用 ESP8266_TestAT 确认模块还在；3) 重连：模块在就 ESP8266_ConnectTCP 后 MQTT_ConnectSimple，成功回在线，失败按 10 到 30 秒退避重试。",
        "if ((MQTT_IsConnected() == 0U) || (MQTT_IsAlive() == 0U)) {\n    if (ESP8266_TestAT() == 0U) {\n        if (ESP8266_ConnectTCP(ST_MQTT_HOST, MQTT_DEFAULT_PORT) == 0U) {\n            uint8_t r = MQTT_ConnectSimple(ST_MQTT_HOST, ST_MQTT_CLIENT);\n            SYS_USART_Printf(SYS_USART_1, \"reconnect=%u connack=%u\n\",\n                (unsigned)r, (unsigned)MQTT_GetConnackCode());\n        }\n    }\n}",
        "拔网 3 分钟后插回：USART1 上依次出现「判死 → AT 通过 → 重连 → connack=0」四行，期间缓存条数一直在涨；connack 不是 0 时用 MQTT_ErrStr 打出原因（4 多为用户名密码或 client id 被拒）。",
        "1) 判死阈值不要小于 keepalive（60 秒），否则网络一抖动就反复重连，重连期间反而发不出数据；2) 重连要退避，不要连续猛打（每次 AT 最长 900 ms 忙等）；3) 模块自己掉线（AT 也没反应）时不能只重连 MQTT，要 ESP8266_Reset 之后重新 JoinAP 拿 IP。",
        "MQTT_IsAlive / MQTT_IsConnected / MQTT_ConnectSimple / MQTT_GetConnackCode / ESP8266_ConnectTCP / ESP8266_TestAT"
      ],
      [
        "双通道上板验收：四个场景一次跑齐",
        "双通道的价值就在「同时」，两条路各测一遍不算通过；四个场景都留了证据，这一段才算做完。",
        "一次跑四个场景：1) 正常：上位机与云端 30 秒内各自都收到，数值一致；2) 断网：缓存按帧率增长，插回后补传对账（条数、时间戳、CRC、去重）；3) 拔 LoRa：上云还在发，但发的是缓存里的旧帧（说明两条路独立，不是从 LoRa 现取的）；4) 复位：缓存不丢、序号规则正确、复位后不重复发布同一帧。",
        "SYS_USART_Printf(SYS_USART_1, \"lora_ok=%u fwd=%u cache=%u cloud=%u online=%u\n\",\n    (unsigned)s_rx_ok, (unsigned)s_fwd_ok, (unsigned)s_cache_ok,\n    (unsigned)s_cloud_ok, (unsigned)MQTT_IsConnected());",
        "四个场景各留一条证据（上位机截图、云端收到的报文、串口打印、你自己的进度文档一行）；场景 3 里 lora_ok 停止增长而 cloud 继续增长。",
        "1) 场景 3 很容易被误读成「数据还是新的」，验收记录里必须写明「数据来自缓存」；2) 场景 4 要确认重启后序号规则生效，否则订阅端会把重启后的第一批判成重复；3) 场景 2 的补传期间必须停止写缓存，否则条数算不准。",
        "MQTT_PublishFloat / MQTT_IsConnected / W25QXX_LogRead / SYS_FRAME_CMD_REPLAY / SYS_FRAME_CMD_HISTORY"
      ]
    ]
  }
];
if (typeof STD_BUILD_GUIDE !== "undefined") { STD_BUILD_GUIDE.push.apply(STD_BUILD_GUIDE, STD_BUILD_B2); }
