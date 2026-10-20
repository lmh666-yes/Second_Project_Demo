/* ============================================================================
 *  data_build.js —— 「🧭 项目引导」数据（手写，非生成物）
 *
 *  用途：把「我要自己从头设计这个工程」这件事拆成阶段 + 动作。
 *        每个动作只讲四件事：
 *          为什么做 / 这一步做什么 / 用哪个【真实存在】的 API / 怎么算做成功了（验收）+ 坑
 *        代码只是“能跑的最小样子”，最终 main.c 由你自己写。
 *
 *  三条硬规矩（写内容时不许违反）：
 *    1. 只引用两板 FWLIB 里真实存在的 API（名字、参数个数与顺序、引脚都对得上源码）。
 *    2. 涉及 UI 的部分不由你写：板1 的 OLED 显示、板2 的 TFT 显示、Qt 界面，都由 AI 完成。
 *    3. 每一条都要能验收：必须写出“看到什么现象才算过”。
 *
 *  加载方式：标准库函数速查.html 在 data_guide.js 之后依次引入
 *              data_build.js（本文件，定义 STD_BUILD_GUIDE = 板1 阶段 0~3）
 *              data_build_b1.js（板1 阶段 4~9，push 进 STD_BUILD_GUIDE）
 *              data_build_b2.js（板2 全部阶段）
 *              data_build_qt.js（Qt 全部阶段）
 *            引擎随后执行 STD_BUILD_GUIDE.forEach(f => pushCard(f, "core", "guide"))，
 *            条目带 _b 标签（板1 / 板2 / Qt），由「🧭 项目引导」板块按工程过滤。
 *  维护：改了任一 data_build*.js → node --check 该文件 → node _build_regress.js
 *        → python check_quality.py → python build_mobile.py（手机版要一起更新）。
 * ========================================================================== */

/* 手册里可选的三块工程（下拉框顺序） */
const STD_BUILD_PROJ = [
  {
    tag: "板1",
    name: "板1 检测端（STM32F407ZET6 + FreeRTOS）",
    d: "采集 6 路环境量 → 处理 → 组帧 → 经 LoRa 发出去；OLED 与上位机 UI 由 AI 完成"
  },
  {
    tag: "板2",
    name: "板2 数据通信端（STM32F407ZGT6）",
    d: "收 LoRa → 转发给上位机（USART2）+ 断网缓存补传；TFT 显示由 AI 完成"
  },
  {
    tag: "Qt",
    name: "Qt 上位机（SerialMonitor）",
    d: "串口收帧解析 / 曲线与统计 / 告警 / 设备控制与补传；UI 由 AI 完成，你写数据与协议处理"
  }
];

/* 阶段条目：字段与「引导教程」一致，额外多一个 _b 工程标签
 *   n 名称 / d 一句话 / g 概述 / m 徽章 / k 搜索关键词 / f 步骤数组
 *   步骤 7 元组：[标题, 为什么, 这一步做什么, 代码, 怎么验证, 坑, 相关] */
const STD_BUILD_GUIDE = [
  /* ======================= 板1 · 阶段 0 ======================= */
  {
    n: "🏗 板1 · 阶段 0｜先拿地图：分工、路线与验收习惯",
    m: "板1 项目引导",
    _b: "板1",
    d: "动手前先搞清楚：哪些必须你写、哪些已经给你、板1 一共几个阶段、每一步怎么算过。",
    g: "板1 的最终形态：main.c 里跑 FreeRTOS，6 路传感器采上来 → 处理后组帧 → 经 LoRa 发给板2。你要自己设计的是 main.c 与任务划分（谁采、谁处理、谁发、谁管缓存与显示）。FWLIB 里的 SYS_* 驱动已写好，直接用、不要改。OLED 的界面由 AI 完成。",
    k: "板1 分工 阶段地图 路线 验收习惯 项目引导 main.c FreeRTOS 任务划分",
    f: [
      [
        "分工边界：你写什么、已经给你什么",
        "边界不清最费时间——写着写着跑去改库文件，后面 AI 给的修正就对不上了。",
        "记住三句话：① 你写 main.c 与任务函数（采集 / 处理 / 组帧 / 发送 / 缓存 / 监控的划分与调用顺序）；② FWLIB 的 SYS_* 驱动直接调用，不改；③ 显示与上位机 UI（板1 OLED、板2 TFT、Qt 界面）由 AI 完成。",
        "",
        "你能一句话说清：我要写 main.c + 任务函数，不改 FWLIB，不写界面。",
        "把“顺手改一下库里这个函数”当成坏习惯：库一改，双板之间的协议与阈值口径就可能对不上。",
        ""
      ],
      [
        "本手册怎么用：每个动作四件套",
        "手册不是代码仓库，是“动作清单”——你要能自己判断做到没做到。",
        "点开任意一个动作，按顺序看：为什么做 → 这一步做什么 → 用哪个真实 API → 怎么验证（验收）+ 坑。代码只是最小样例，允许你写得更顺手，但 API 名字和参数要按工具里的来。",
        "",
        "你随手能查到一个 API 的完整签名：工具「📖 资料查询」里搜 SYS_USART_Init，能看到参数、返回值、调用示例。",
        "不要把示例代码整段复制进 main.c 就完事——先弄清它为什么这么写，否则一出问题无从下手。",
        ""
      ],
      [
        "板1 的阶段地图（阶段 1~9，加上本张地图共 10 张卡）",
        "先看到全程，才知道今天这一步为什么要这么写。",
        "阶段 1 开工准备与工程骨架 → 阶段 2 时钟与基础外设 → 阶段 3 FreeRTOS 任务框架 → 阶段 4 传感器采集 → 阶段 5 数据处理与阈值 → 阶段 6 协议组帧与 CRC → 阶段 7 LoRa 发送 → 阶段 8 本地显示与交互（OLED，界面 AI 给） → 阶段 9 联调验收。",
        "",
        "你能指着地图说出：现在在第几段、下一段要交什么。",
        "别跳段：没有阶段 2 的滴答，阶段 3 的 vTaskDelay 根本不对时基。",
        ""
      ],
      [
        "验收习惯：每一步都要“看得见的证据”",
        "这套工程最后要上板实测，凭感觉写出来的代码一定返工。",
        "给每步准备一种证据：LED 亮灭、蜂鸣器响、串口打印一行、OLED 出一个数、Keil 的 0 Error 0 Warning、逻辑分析仪抓到的一段波形。做完就记进你自己的进度文档。",
        "",
        "每完成一个动作，你能贴出一条证据（打印内容 / 现象 / 波形截图）。",
        "“编译过了”不算证据——那只能证明语法对，不能证明时序与数据对。",
        ""
      ]
    ]
  },

  /* ======================= 板1 · 阶段 1 ======================= */
  {
    n: "🏗 板1 · 阶段 1｜开工准备与工程骨架",
    m: "板1 项目引导",
    _b: "板1",
    d: "把工程打开、认准目录、写出第一个能编译能下载的 main.c，并把串口打印通道打通。",
    g: "本阶段的产物：① 你能说清工程里每个目录是干什么的；② 你自己的 main.c 能编译（0 Error 0 Warning）、能下载、LED 会按你的意思闪；③ 串口 115200 能打印出你自己写的字。做完这三样，后面所有调试都有眼睛了。",
    k: "板1 工程骨架 Keil 打开 目录 main.c 最小工程 LED 串口打印 115200 0 Error 0 Warning",
    f: [
      [
        "打开工程并认准目录",
        "改错目录 = 编译过不了或下载了旧代码，是新手最常见的坑。",
        "用 Keil 打开 D:\\GitHub_Project\\EnvMonitor\\Project_1_检测端\\Project\\标准模板.uvprojx；在左侧工程树里认出四类分组：main.c（你自己的代码）、FWLIB\\src（驱动实现，只读不改）、Startup（启动文件与中断向量）、FreeRTOS（内核）。注意：main.c 就在 Project 根目录下，不在 User\\ 里。",
        "工程路径：D:\\GitHub_Project\\EnvMonitor\\Project_1_检测端\\Project\\标准模板.uvprojx\n待你写的文件：D:\\GitHub_Project\\EnvMonitor\\Project_1_检测端\\Project\\main.c",
        "Keil 打开后能展开分组看到 FWLIB\\src\\sys_usart.c、sys_frame.c 等；双击 main.c 能定位到 Project\\main.c。",
        "工程文件是 标准模板.uvprojx，另有一个 标准模板.uvoptx 保存的是显示设置（比如断点、打开的窗口），两者别搞混；不要用 keilkilll.bat 清理后就以为代码没了——它只删中间产物。",
        ""
      ],
      [
        "写出最小可编译的 main.c（先只点灯）",
        "先要一个“能跑的基线”，后面每加一块功能都能对比出是谁搞坏的。",
        "把 main.c 清空重写：包含必要头文件，初始化时钟与滴答，然后一个死循环里翻转 LED。先不要加传感器、不要加 FreeRTOS——基线越干净越好。",
        "#include \"stm32f4xx.h\"\n#include \"sys_clock.h\"\n#include \"sys_tick.h\"\n#include \"led.h\"\n\nint main(void)\n{\n    SYS_CLK_ToHighSpeed();     /* 168MHz：HCLK 168 / PCLK1 42 / PCLK2 84 */\n    SYS_TICK_Init();           /* 1ms 滴答，SYS_TICK_Delay_ms 才有意义 */\n    LED_Init();                /* LED0=PF9 LED1=PF10 LED2=PE13 LED3=PE14，低电平点亮 */\n\n    while (1) {\n        LED_Toggle(0);\n        SYS_TICK_Delay_ms(500);\n    }\n}",
        "编译 0 Error 0 Warning；下载后板1 的 LED0 每 0.5 秒亮灭一次。",
        "SYS_TICK_Init() 之前不要调用 SYS_TICK_Delay_ms()，否则滴答没起来会直接卡住；LED 是低电平点亮（LED_ACTIVE_LOW = 1），看到“反了”不是接线错。",
        "SYS_CLK_ToHighSpeed / SYS_TICK_Init / LED_Init"
      ],
      [
        "把“0 Error 0 Warning”变成习惯",
        "警告里常常藏着真错误（比如隐式声明函数 = 参数类型全错）。",
        "每次编译后看 Build Output 最后两行：0 Error(s), 0 Warning(s)。有警告就去改到自己看懂为止；不确定某个警告什么意思，把原文贴进工具搜。",
        "",
        "Build Output 最后一行是 0 Error(s), 0 Warning(s)。",
        "把警告当“能跑就行”忽略，是后面最难查的一类 bug（比如 uint16_t 与 int 混用导致的值回绕）。",
        ""
      ],
      [
        "打通串口打印（115200）",
        "没有打印，你只能靠猜；有了打印，每一步都能自证。",
        "初始化 USART1（PA9/PA10，115200），用 SYS_USART_SendLine 或 SYS_USART_Printf 打印启动横幅；以后每个模块初始化后都打一行“xxx ok”。",
        "#include \"sys_usart.h\"\n\nSYS_USART_Init(SYS_USART_1, 115200);\nSYS_USART_SendLine(SYS_USART_1, \"=== board1 boot ===\");\nSYS_USART_Printf(SYS_USART_1, \"tick=%u\\r\\n\", (unsigned)SYS_TICK_GetTick());",
        "电脑串口助手 115200 8N1 能看到 “=== board1 boot ===”；复位一次就重新打印一次。",
        "USB 转 TTL 的 TX 接板子 PA10(RX)、RX 接 PA9(TX)，地必须共；打印乱码先查波特率，再查是不是接成了 USB-TTL 的 3.3V 供电不足。",
        "SYS_USART_Init / SYS_USART_SendLine / SYS_USART_Printf"
      ],
      [
        "建立你的进度文档节奏",
        "板1 调试会反复，记录能省下大量重复劳动。",
        "在 D:\\GitHub_Project\\EnvMonitor\\Project_1_检测端\\板1进度文档.md 里自己记（这份文档只由你写）：今天做了什么、卡在哪、怎么解决的、下一步。每解决一个问题就补一行。",
        "",
        "文档里有至少 3 条“问题 → 原因 → 解决”的记录。",
        "别只在脑子里记——三天后再回来，你会忘了当时为什么改这一行。",
        ""
      ]
    ]
  },

  /* ======================= 板1 · 阶段 2 ======================= */
  {
    n: "🏗 板1 · 阶段 2｜时钟、滴答与基础外设",
    m: "板1 项目引导",
    _b: "板1",
    d: "把板子的“时间”和“手脚”都配好：168MHz 时钟、1ms 滴答、LED/蜂鸣器/按键/串口收发。",
    g: "这一阶段结束后，你手里有一套可复用的“基础件”：LED 能报状态、蜂鸣器能提示、按键能输入、串口能收发。后面传感器采集与 LoRa 通信全靠它们做调试出口。所有引脚都按工程里的宏来，不要背错。",
    k: "板1 时钟 168MHz PCLK 42 84 滴答 延时 LED PF9 蜂鸣器 PF8 按键 PA0 PE2 PE3 PE4 串口中断 115200",
    f: [
      [
        "时钟：确认真的跑在 168MHz",
        "外设频率决定波特率、定时器周期、SPI/I2C 速率，不确认就会“到处差一点”。",
        "调 SYS_CLK_ToHighSpeed()，再用 SYS_CLK_GetBusFreq 把三路总线频率打出来核对：HCLK 168 MHz、PCLK1 42 MHz、PCLK2 84 MHz。",
        "uint32_t hclk, pclk1, pclk2;\nSYS_CLK_ToHighSpeed();\nSYS_CLK_GetBusFreq(&hclk, &pclk1, &pclk2);\nSYS_USART_Printf(SYS_USART_1, \"HCLK=%u PCLK1=%u PCLK2=%u\\r\\n\",\n                (unsigned)(hclk / 1000000), (unsigned)(pclk1 / 1000000), (unsigned)(pclk2 / 1000000));",
        "打印 HCLK=168 PCLK1=42 PCLK2=84；改错分频立刻能看出差异。",
        "上限就是这三个数（SYS_CLK_MAX_HCLK 168000000 / MAX_PCLK1 42000000 / MAX_PCLK2 84000000），超了外设会不稳；晶振不是 8MHz 时 HighSpeed 会返回错误码而不是静默降频，记得看返回值。",
        "SYS_CLK_ToHighSpeed / SYS_CLK_GetBusFreq / SYS_CLK_GetFreq"
      ],
      [
        "滴答与精确延时",
        "后面所有“等一会儿”“超时判断”“周期任务”都建立在滴答上。",
        "SYS_TICK_Init() 起 1ms 滴答，用 SYS_TICK_GetTick() 记时间、SYS_TICK_Elapsed() 算间隔、SYS_TICK_Timeout() 做超时、SYS_TICK_Every() 做周期触发。微秒级短延时用 SYS_TICK_Delay_us() 或 delay_us()。",
        "uint32_t t0 = SYS_TICK_GetTick();\nSYS_TICK_Delay_ms(300);\nSYS_USART_Printf(SYS_USART_1, \"elapsed=%u ms\\r\\n\",\n                (unsigned)SYS_TICK_Elapsed(t0));   /* 期望 300 左右 */",
        "打印 elapsed 在 298~302 之间（±1 滴答属正常）。",
        "SYS_TICK_Delay_ms 是忙等（会占住 CPU），进了 FreeRTOS 后任务里要改用 vTaskDelay；delay_us 依赖主频，主频没配对会整体偏差。",
        "SYS_TICK_Init / SYS_TICK_GetTick / SYS_TICK_Elapsed / SYS_TICK_Delay_us"
      ],
      [
        "LED：把“看不见的状态”变成看得见",
        "板子跑起来却没现象时，LED 是唯一不需要电脑的调试手段。",
        "LED0 PF9 / LED1 PF10 / LED2 PE13 / LED3 PE14（低电平点亮）。先 LED_Init()，用 LED_On/Off/Toggle 做单灯；用 LED_OnTo(n) 做“进度条”；用 LED_ShowHex(v) 显示一个字节的状态码。闪烁类接口要点亮后每 1ms 调一次 LED_BlinkUpdate()。",
        "LED_Init();\nLED_ShowHex(0x0A);          /* 二进制 1010：LED1、LED3 亮（低电平点亮）*/\nSYS_TICK_Delay_ms(800);\nLED_BlinkStart(0, 200, 500);\n/* 之后主循环或 1ms 滴答里反复调用 */\nLED_BlinkUpdate();",
        "LED_ShowHex(0x0A) 时 LED1 与 LED3 亮；BlinkStart 后 LED0 以 200ms 周期、50% 占空比闪。",
        "LED_ShowHex 是“低位对应 LED0”，看到的亮法可能和你直觉相反；BlinkUpdate 漏调就不会闪（它不是中断驱动的）。",
        "LED_Init / LED_On / LED_Toggle / LED_ShowHex / LED_BlinkStart / LED_BlinkUpdate"
      ],
      [
        "蜂鸣器：加一路“听得见”的提示",
        "调试时你常常离板子很远，声音比灯光更容易察觉。",
        "蜂鸣器在 PF8（高电平响）。BEEP_Init() 后用 BEEP_Beep(times) 简单响几声，BEEP_BeepEx(times, on_ms, off_ms) 控制节奏，BEEP_KeySound() 做按键音。想让它边跑边响用 BEEP_AsyncStart()，同样要每 1ms 调 BEEP_Update()。",
        "BEEP_Init();\nBEEP_Beep(2);                       /* 响两声 */\nBEEP_BeepEx(1, 100, 100);\nBEEP_KeySound();",
        "蜂鸣器按你给的次数与节奏响；无源蜂鸣器只会“咔”一声是正常的（需要 PWM 才有音调）。",
        "BEEP_AsyncStart 之后忘记调 BEEP_Update 会一直响；BEEP_ACTIVE_LOW 是 0，接线反了会常响不停。",
        "BEEP_Init / BEEP_Beep / BEEP_BeepEx / BEEP_AsyncStart / BEEP_Update"
      ],
      [
        "按键：4 个键 + 消抖 + 长按",
        "按键是最容易“看起来能用、实际会连发”的外设。",
        "按键 KEY1 PA0 / KEY2 PE2 / KEY3 PE3 / KEY4 PE4（低电平按下，内部上拉）。KEY_Init() 后：KEY_Read(id) 读电平、KEY_Scan() 扫一次、KEY_ReadAll() 一次读 4 个位、KEY_WaitPress() 阻塞等按键、KEY_LongPress(id, ms) 判断长按。",
        "uint8_t k;\nKEY_Init();\nwhile (1) {\n    if ((k = KEY_Scan()) != KEY_NONE) {   /* KEY_NONE = 0xFF */\n        SYS_USART_Printf(SYS_USART_1, \"key id=%u\\r\\n\", (unsigned)k);\n        BEEP_KeySound();\n    }\n    SYS_TICK_Delay_ms(5);\n}",
        "按哪个键就打印哪个 id（0~3），且只打印一次不连发；按住 1 秒用 KEY_LongPress 能判成长按。",
        "KEY_Scan 需要被周期性调用（推荐 5~10ms 一次），放在超长延时里会漏按键；消抖时间由 KEY_DEBOUNCE_MS=10 决定，改动要连带验证。",
        "KEY_Init / KEY_Scan / KEY_Read / KEY_LongPress / KEY_NONE"
      ],
      [
        "串口中断接收：为后面的协议帧做准备",
        "板1 既要发数据也要收指令，轮询会丢字节。",
        "用 SYS_USART_InitRxIT(id, baud) 打开接收中断，主循环里轮询 SYS_USART_Available(id) 拿到字节数，再用 SYS_USART_RxRead(id) 逐字节取或 SYS_USART_ReadBuf() 批量取；SYS_USART_RxFlush() 清空。",
        "int c;\nSYS_USART_InitRxIT(SYS_USART_1, 115200);\nwhile (1) {\n    while ((c = SYS_USART_RxRead(SYS_USART_1)) >= 0) {\n        SYS_USART_SendByte(SYS_USART_1, (uint8_t)c);   /* 回显，验证收得到 */\n    }\n    SYS_TICK_Delay_ms(1);\n}",
        "串口助手发什么字符就回显什么；连发一长串（几百字节）不丢字符。",
        "RxRead 返回 int 且无数据时返回负值——不要用 uint8_t 接，否则 -1 会变成 0xFF 被当成真数据。",
        "SYS_USART_InitRxIT / SYS_USART_Available / SYS_USART_RxRead / SYS_USART_ReadBuf"
      ]
    ]
  },
  {
    n: "🧵 板1 · 阶段 3｜FreeRTOS 任务框架：谁采、谁处理、谁发",
    m: "板1 项目引导",
    _b: "板1",
    d: "把 while(1) 单循环拆成多个任务：采样、处理、通信、下行接收、监控，数据用队列在任务间传递。",
    g: "板1 的工程形态是 FreeRTOS 抢占式调度：tick 1ms、优先级 0~7（数字大=优先级高）、堆 30KB 用 heap_4、只支持动态分配。参考划分（与参考实现一致，旧 main.c 已备份在 D:\GitHub_Project\_参考实现备份）：Sensor(pri3,512 字) 周期采样 → s_q_sensor → Process(pri3,512) 换算与判阈 → s_q_frame → Comm(pri4,512) 组帧经 LoRa 发出；LoraRx(pri3,512) 收下行指令；Monitor(pri5,256) 管看门狗与栈水位。【阶段 3 动手前必做】先调一次 SYS_NVIC_Init()：它只干一件事 —— NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4)，即 4 位全抢占（本库默认就该是这个分组）。必须在使能任何中断、起 FreeRTOS 之前调；不调就沿用复位默认分组（PriorityGroup_0，2 位抢占），FreeRTOS 的 configASSERT 会抓，但库不会替你调。本库 FreeRTOSConfig.h 的注释里也标了这件事（并给出写完整 main.c 后的位置：板1 第 989 行 / 板2 第 1581 行）。注意：阶段判定的判据里没有这一条，机器不会替你抓，得自己记得。",
    k: "FreeRTOS 任务 队列 xQueueCreate xTaskCreate 优先级 栈大小 堆 30KB vTaskDelay pdMS_TO_TICKS 任务划分 抢占 板1",
    f: [
      [
        "先把任务与数据流画在纸上",
        "任务怎么划分，决定了后面所有代码长什么样；板1 的坑几乎都出在“谁等谁”。",
        "写一张表，每行一个任务：任务名 / 优先级 / 栈（字） / 周期 / 输入 / 输出。参考本工程：Sensor 每 2s 采 6 路 → 队列 s_q_sensor；Process 收队列做换算与判阈 → 队列 s_q_frame；Comm 从队列取帧发 LoRa；LoraRx 收板2 下行；Monitor 每 1s 喂狗并打印栈水位。",
        "",
        "你的表能回答四个问题就算过：谁产生数据、谁消费、队列满了怎么办、谁最急（优先级最高）。",
        "不要“一个任务干全部”：采样要 2s 一次、发送要 20ms 一次，塞进同一个任务只会互相拖，而且一处卡死全系统停摆。",
        "xTaskCreate / xQueueCreate"
      ],
      [
        "最小可跑：两个任务 + 一个队列",
        "先证明调度器和队列真的在跑，再往里塞业务，出问题时才好定位。",
        "建一个深度 4 的队列，两个任务：A 每 500ms 放一个递增的数，B 收到就用串口打印。队列和任务都要在 vTaskStartScheduler() 之前创建。",
        "QueueHandle_t q;\n\nvoid taskA(void *p) {\n    uint32_t v = 0;\n    for (;;) {\n        v++;\n        xQueueSend(q, &v, portMAX_DELAY);\n        vTaskDelay(pdMS_TO_TICKS(500));\n    }\n}\n\nvoid taskB(void *p) {\n    uint32_t got;\n    for (;;) {\n        if (xQueueReceive(q, &got, portMAX_DELAY) == pdPASS) {\n            SYS_USART_Printf(SYS_USART_1, \"B got %lu\\r\\n\", (unsigned long)got);\n        }\n    }\n}\n\nint main(void) {\n    SYS_CLK_ToHighSpeed();\n    SYS_USART_Init(SYS_USART_1, 115200);\n    SYS_FAULT_Init();\n    q = xQueueCreate(4, sizeof(uint32_t));\n    xTaskCreate(taskA, \"A\", 512, NULL, 3, NULL);\n    xTaskCreate(taskB, \"B\", 512, NULL, 3, NULL);\n    vTaskStartScheduler();\n    for (;;) { }\n}",
        "串口每 0.5 秒一行，数字连续不跳号；B 的打印不会被 A 的延时卡住。",
        "栈大小的单位是“字”（4 字节），512 就是 2KB；xQueueSend 传的是值不是指针，结构体必须定长；上 FreeRTOS 后不要调 SYS_TICK_Init()。",
        "xQueueCreate / xTaskCreate / xQueueSend / xQueueReceive / vTaskStartScheduler"
      ],
      [
        "把优先级和栈大小定成本工程能用的值",
        "优先级就是抢占顺序，配错了会出现“发送饿死采样”或“监控任务醒不过来”。",
        "照本工程取值：Sensor / Process / LoraRx 用 3，Comm / Forward 用 4，Monitor 用 5；栈 512 字（2KB），Monitor 256 字（1KB）。空闲任务是内核建的，不要占用 0/1。",
        "xTaskCreate(vTaskSensor,  \"Sensor\",  512, NULL, 3, NULL);\nxTaskCreate(vTaskProcess, \"Process\", 512, NULL, 3, NULL);\nxTaskCreate(vTaskComm,    \"Comm\",    512, NULL, 4, NULL);\nxTaskCreate(vTaskLoraRx,  \"LoraRx\",  512, NULL, 3, NULL);\nxTaskCreate(vTaskMonitor, \"Monitor\", 256, NULL, 5, NULL);",
        "在 Monitor 里打印每个任务的剩余栈：uxTaskGetStackHighWaterMark(NULL)，稳定运行后剩余 >50 字。",
        "优先级只差 1 就会抢占；低优先级任务里不要写“不阻塞的死循环”，否则高优先级任务拿不到 CPU。",
        "xTaskCreate / uxTaskGetStackHighWaterMark / configMAX_PRIORITIES"
      ],
      [
        "周期节拍：vTaskDelay 与 pdMS_TO_TICKS",
        "RTOS 下延时写错，轻则曲线抖动，重则整个任务被自己的等待锁住。",
        "采样任务用 vTaskDelay(pdMS_TO_TICKS(2000))；发送间隔 pdMS_TO_TICKS(20)；需要严格周期改用 vTaskDelayUntil()，以当前 tick 为基准累加。",
        "void vTaskSensor(void *p) {\n    const TickType_t period = pdMS_TO_TICKS(2000);\n    TickType_t last = xTaskGetTickCount();\n    float t, h;\n    for (;;) {\n        SYS_DHT11_Read(&t, &h);\n        xQueueSend(s_q_sensor, &msg, pdMS_TO_TICKS(5));\n        vTaskDelayUntil(&last, period);\n    }\n}",
        "任务里用 xTaskGetTickCount() 打时间戳，相邻两行间隔稳定在 2000ms±1 tick。",
        "任务里不要用 SYS_TICK_Delay_ms(2000) 这类长阻塞；微秒级延时用 delay_us()，但必须先 delay_dwt_init()；毫秒换算一律走 pdMS_TO_TICKS。",
        "vTaskDelay / vTaskDelayUntil / pdMS_TO_TICKS / xTaskGetTickCount / delay_dwt_init / delay_us"
      ],
      [
        "队列满了、数据来晚了：超时与丢帧计数",
        "真实系统一定会遇到消费者变慢，没有超时策略就会死等或者悄悄丢数据。",
        "发送侧统一用带超时的 xQueueSend(q, &m, pdMS_TO_TICKS(5))，返回非 pdPASS 就累加丢帧计数；只在确定一直有数据时才用 portMAX_DELAY。",
        "if (xQueueSend(s_q_sensor, &msg, pdMS_TO_TICKS(5)) != pdPASS) {\n    s_dropCnt++;\n}",
        "人为把 Process 任务的延时放大到 5 秒，能看到丢帧计数增长，但系统不死锁、Monitor 仍在跑。",
        "队列深度是“最多存几个元素”不是字节数；元素按值拷贝，结构体里不要放指向临时变量的指针。",
        "xQueueSend / xQueueReceive / portMAX_DELAY / pdPASS"
      ],
      [
        "算一遍堆和栈：30KB 够不够",
        "vTaskStartScheduler() 返回就是堆不够，现象是“程序不跑也没有 HardFault”，很迷惑人。",
        "估算：5 个 512 字 + 1 个 256 字的任务栈约 11KB；两个队列（4×20 字节帧 + 4×sizeof(SensorMsg_t)）几百字节；每个 TCB 约 100 字节；合计 12~13KB，30KB 堆有余量。确认 FreeRTOSConfig.h 里 configTOTAL_HEAP_SIZE = 30*1024。",
        "/* FreeRTOSConfig.h */\n#define configTOTAL_HEAP_SIZE   (30 * 1024)\n#define configTICK_RATE_HZ      1000\n#define configMAX_PRIORITIES    8\n#define configSUPPORT_STATIC_ALLOCATION 0",
        "上电后程序持续运行，vTaskStartScheduler() 之后的死循环永远不执行。",
        "别把 4KB 大缓存放任务栈里，改成 static 或全局；configSUPPORT_STATIC_ALLOCATION 为 0 时只能用动态创建。",
        "configTOTAL_HEAP_SIZE / configTICK_RATE_HZ / configMAX_PRIORITIES / xPortGetFreeHeapSize"
      ]
    ]
  }
];
