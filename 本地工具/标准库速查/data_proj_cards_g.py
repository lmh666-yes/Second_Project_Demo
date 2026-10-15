# -*- coding: utf-8 -*-
r"""data_proj_cards_g.py —— 项目实战卡（G 组）：㉔ DHT11 单总线温湿度传感器。

FLOW_P24_DHT11   —— 编号 ㉔ 的流程卡（检测端的第一路数据源）
GUIDE_P24_DHT11  —— 编号 ㉔ 的引导向导

写法跟 data_proj_cards.py 完全一致（**raw 三引号 + 单反斜杠 n**，别改成双反斜杠 n）：
    想要 JS 里的换行   -> 写 \n     （单反斜杠）
    想要 JS 里的引号   -> 写 \"     （单反斜杠 + 半角引号）
    想让 C 代码里显示字面 CRLF -> 写 \\r\\n（双反斜杠）
引号铁律：字符串内部不能出现裸的半角双引号；中文引号一律用全角 “ ”。

事实来源（逐条读过源码，不是凭印象写的）：
  板1 FWLIB\inc\sys_dht11.h / src\sys_dht11.c          —— SYS_DHT11_* 全套 API、PG9、SYS_DHT11_LOCK_IRQ
  板2 FWLIB\inc\dht11.h     / src\dht11.c              —— DHT11_* 另一套 API、20ms/1100ms/45us 三个宏
  FWLIB\inc,src\delay.{h,c}                            —— DWT 周期计数器实现，旧版 delay_ms 空转凑系数的教训
  FWLIB\inc\gpio_core.h                                —— OutInitOD / InInit(pull) / InRead / OutWrite
  FWLIB\inc\sys_clock.h                                —— 默认 168MHz，delay_us 的周期数换算基准
校验：python _proj_check.py data_proj_cards_g.py
"""

# ======================================================================
# ㉔ DHT11 单总线温湿度传感器（检测端第一路数据源）
# ======================================================================

FLOW_P24_DHT11 = r"""
/* ===================== ㉔ DHT11 单总线温湿度（检测端第一路数据源） ===================== */
{m:"流程", t:"flow", n:"㉔ DHT11 单总线温湿度（检测端的第一路数据源）",
 d:"DHT11 只有一根数据线，命令发出去和数据收回来走的是同一根线，靠主机把线拉低多久来区分「起始信号」和「数据 0 / 1」。它不属于 UART、I2C 或 SPI，本卡多数问题与该特性有关。另一成问题来自「微秒延时到底准不准」和「读的这 4ms 会不会被中断打断」。",
 f:[
  ["先认清「单总线」到底单在哪","DATA 脚是双向的：主机先拉低它发命令，然后松手让上拉电阻把线拉高，从机再把线拉低 / 拉高，把自己的数据顶出来。所以「此刻谁在驱动这根线」是不断切换的。它没有波特率（不是 UART）、没有地址和 ACK（不是 I2C）、没有时钟线（不是 SPI），唯一的信息载体就是每段电平持续了多久","同一根脚 DS18B20 也想要，但两者协议完全不同，不能混用"],
  ["硬件先过关：上拉电阻和线长","数据线必须有 4.7k~10k 上拉电阻把空闲态拉高。三针的 DHT11 模块板一般已经焊好了，裸的四针传感器必须自己加，否则现象永远是「无应答」。杜邦线超过 20cm 时上升沿变缓，40 位里偶发一两位置错，表现成「有时成功、有时校验失败」","本项目两板原理图上 PG9 都已经有上拉：板2 是 10K，且与 DS18B20 共用这一根线"],
  ["上电先等 1 秒再读第一次","器件内部要 1 秒左右完成上电稳定，这期间总线上的电平不代表真实数据。上电就立刻读，结果多半是校验和不对或者干脆无应答，很容易被误判成「接线错了」","板1 的 test_dht11 就是 SYS_DHT11_Init 之后 delay_ms_dwt(1500) 再读"],
  ["发起始信号：拉低 ≥18ms 再释放","主机把线拉低至少 18ms（板2 驱动用 20ms 留余量），然后松手（开漏写 1 = 释放，靠上拉拉高）。从机识别到这段长低电平后，等 20~40us 再拉低 80us、又拉高 80us 作为应答","这段宽度由 delay_ms 决定：毫秒延时不准，现象就是「器件完全不应答」，而不是「读到的值不对」"],
  ["等应答要等三次，一次都不能少","① 等应答低（80us）② 等应答高（80us）③ 再等一次低（应答高结束、进入 bit0 的 50us 低电平起点）。少了第三次，位循环第一次等高会立刻返回，量到的是应答高电平的尾巴，凭空多读一个 1，整帧右移一位","现象：湿度恒 ≥128、校验和还大约 2/3 概率假通过，上层看到的是「数据超范围」而不是「读取失败」"],
  ["读 40 位：50us 低 + 高电平宽度定 0/1","每一位都以 50us 低电平开头；后面的高电平 26~28us 是 0、70us 是 1。所以判 0/1 的办法是量高电平持续了多少微秒，再跟一个阈值比（板2 取 45us，正好夹在 26 和 70 中间）","数据高位在前：每读一位先把当前字节左移一位，再把测到的 0/1 补到最低位"],
  ["40 位怎么排：湿度、温度、校验和","顺序固定：湿度整数 8 位 + 湿度小数 8 位 + 温度整数 8 位 + 温度小数 8 位 + 校验和 8 位（共 5 字节）。校验和 = 前 4 个字节之和的低 8 位。DHT11 的小数位实际恒为 0，这是器件特性不是读错（DHT22 才有真分辨率）","健康帧形如 45 0 24 0 69（45+0+24+0=69）：湿度 45%RH、温度 24℃"],
  ["微秒延时必须真的准，否则永远分不出 0 和 1","delay_us 是用 DWT 周期计数器实现的：cycles = us × (SystemCoreClock / 1000000)。如果换成「空循环凑系数」，delay_us(30) 实际可能是 200us，量到的高电平长度就全一样，读出来只会是全 0 或全 255。本项目历史上 delay_ms 就是空转凑系数、实测约 2.09ms/次（慢一倍），后果正是「DHT11 起始信号不足 18ms → 不应答」","怎么验证见下面 r 字段：拿逻辑分析仪量起始低电平宽度和高电平宽度"],
  ["读这 4ms 里要关中断，或者放进最高优先级任务","40 位读下来约 4ms，中途被串口中断或 FreeRTOS 1ms 节拍打断，等待高电平的计时就整体错位。板1 用 SYS_DHT11_LOCK_IRQ=1，在读取窗口里 __disable_irq()；这段关中断窗口的最坏值由「单段等待超时 × 最多等待次数」决定：100us × 41 次 ≈ 4.1ms","取舍：关中断 4ms 会推迟调度。实时性要求高时把读取放进优先级最高的任务里（不被抢占就等于临界区），或者接受偶发失败靠重试兜住"],
  ["采样率只有 1Hz，读太快只会拿到旧值","DHT11 内部最快 1 次/秒，读太密它要么返回上一次的旧值、要么直接不应答。板2 驱动用 DHT11_MIN_INTERVAL_MS=1100 把过密读取挡在门外并返回「间隔不足」，而不是给上层一帧脏数据","重试策略要分清：无应答重试没用（那是接线 / 上拉 / 上电时间问题，直接上报）；只有校验失败才值得退避重试"],
  ["拿到温湿度之后：打包、缓存、上行","DHT11 只负责产生数据，后面的事分给别人：独立任务每 2s 读一次 → 通过队列交给处理任务 → 温度 ×100、湿度 ×100 以大端 int16 填进 ㉒ 的 20 字节环境帧 → 交 ⑲ LoRa 上行；超限或断网时先写进 ⑱ 的本地缓存等补传","湿度 45%RH → 0x1194(4500)；温度 24℃ → 0x0960(2400)；负数温度按补码写，-5℃ → 0xFE0C"]
 ],
 a:[
  ["数据脚 / 上拉","PG9（GPIOG / GPIO_Pin_9），4.7k~10k 上拉","裸传感器必须自己加上拉；模块板通常已焊好"],
  ["起始低电平","≥18ms（板2 用 20ms）","主机拉低，从机靠这段长度识别命令"],
  ["释放后从机响应","20~40us 后开始拉低","从机的反应时间，不需要精确量"],
  ["应答低 / 应答高","各 80us","应答高结束还要再等一次低，这就是「三次等待」"],
  ["位起始低电平","50us","每一位都以它开头，量 0/1 时被顺带吃掉"],
  ["位 0 的高电平","26~28us","与位 1 差 2.5 倍，所以能用固定阈值分开"],
  ["位 1 的高电平","70us","板2 用 DHT11_BIT_THRESHOLD_US = 45 做分界"],
  ["校验和","前 4 字节之和的低 8 位","不对就整帧丢弃，绝不用可疑值凑合"],
  ["采样间隔","≥1000ms（板2 用 1100）","读太密会拿旧值或直接失败"],
  ["单段电平等待上限","100us（板1）/ 200us（板2）","板1 关中断窗口最坏 41×100us ≈ 4.1ms；板2 不关中断，最坏阻塞 ≈ 8.2ms"]
 ],
 e:"/* ============================================================\n *  ㉔ DHT11 单总线温湿度，完整参考实现\n *  平台：STM32F407 · 168MHz · 数据脚 PG9（GPIOG / GPIO_Pin_9）\n *  依赖：delay.c 的 DWT 微秒延时、gpio_core.h 的引脚切换封装\n *  说明：按板1 的 sys_dht11.c 写；板2 是同算法、另一套 API 名\n * ============================================================ */\n#include \"stm32f4xx.h\"\n#include \"delay.h\"\n#include \"gpio_core.h\"\n\n#define DHT11_PORT            GPIOG\n#define DHT11_PIN             GPIO_Pin_9\n#define DHT11_START_LOW_MS    20U      /* 手册 ≥18ms，给 2ms 余量        */\n#define DHT11_TIMEOUT_US      100U     /* 单段等待上限；手册最长 80us    */\n#define DHT11_BIT_HIGH_US     45U      /* 位1≈70us / 位0≈26us，取中间    */\n#define DHT11_RETRY           3U\n#define DHT11_TIMEOUT_CYC     0xFFFFFFFFUL\n\nstatic GPIO_TypeDef *s_port = 0;       /* 0 = 还没初始化                 */\nstatic uint16_t      s_pin  = 0U;\nstatic uint8_t       s_raw[5];         /* 最近一帧裸字节，排障用         */\n\n/* 等某个电平，带超时。绝不能写成 while(引脚 != 电平) 的死等：\n * 器件没插时那根线永远是高，死等会把整个任务焊死在这里。 */\nstatic uint32_t dht11_wait(uint8_t level, uint32_t timeout_us)\n{\n    uint32_t start = DWT->CYCCNT;\n    uint32_t limit = timeout_us * (SystemCoreClock / 1000000U);\n\n    while (GPIO_InRead(s_port, s_pin) != level)\n    {\n        if ((DWT->CYCCNT - start) > limit) { return DHT11_TIMEOUT_CYC; }\n    }\n    return (DWT->CYCCNT - start);      /* 返回这段电平占了多少个周期      */\n}\n\n/* 读一整帧：0 成功 / 1 无应答 / 2 校验和不对 */\nstatic uint8_t dht11_read_frame(void)\n{\n    uint8_t  i, data[5] = {0, 0, 0, 0, 0};\n    uint32_t irq, high, thresh;\n\n    delay_us(1);                       /* 顺带确保 DWT 计时已使能         */\n\n    /* ① 起始信号：拉低 ≥18ms 再松手（开漏写 1 = 释放总线），这段不关中断 */\n    GPIO_OutInitOD(s_port, s_pin);\n    GPIO_OutWrite(s_port, s_pin, 0U);\n    delay_ms(DHT11_START_LOW_MS);\n    GPIO_OutWrite(s_port, s_pin, 1U);\n\n    /* ② 关中断窗口从这一刻开始：40 位约 4ms，被打断就整体错位 */\n    irq = __get_PRIMASK();\n    __disable_irq();\n\n    GPIO_InInit(s_port, s_pin, GPIO_PuPd_UP);\n    thresh = (SystemCoreClock / 1000000U) * DHT11_BIT_HIGH_US;\n\n    /* ③ 三次等待：应答低起点 → 应答高起点 → 应答高结束（进入 bit0） */\n    if (dht11_wait(0U, DHT11_TIMEOUT_US) == DHT11_TIMEOUT_CYC ||\n        dht11_wait(1U, DHT11_TIMEOUT_US) == DHT11_TIMEOUT_CYC ||\n        dht11_wait(0U, DHT11_TIMEOUT_US) == DHT11_TIMEOUT_CYC)\n    {\n        __set_PRIMASK(irq);\n        GPIO_InInit(s_port, s_pin, GPIO_PuPd_UP);\n        return 1U;                     /* 没插 / 没上拉 / 上电不足 1s     */\n    }\n\n    /* ④ 40 位：每位的 50us 低电平已被「等高」消费掉，只量高电平宽度 */\n    for (i = 0U; i < 40U; i++)\n    {\n        if (dht11_wait(1U, DHT11_TIMEOUT_US) == DHT11_TIMEOUT_CYC) { break; }\n        high = dht11_wait(0U, DHT11_TIMEOUT_US);\n\n        data[i >> 3] = (uint8_t)(data[i >> 3] << 1);   /* 高位在前        */\n        if (high > thresh) { data[i >> 3] |= 0x01U; }\n    }\n\n    __set_PRIMASK(irq);                /* ⑤ 先恢复中断，再做别的         */\n    GPIO_InInit(s_port, s_pin, GPIO_PuPd_UP);          /* 空闲：输入+上拉 */\n\n    for (i = 0U; i < 5U; i++) { s_raw[i] = data[i]; }\n\n    /* ⑥ 校验和 = 前 4 字节之和的低 8 位；不对就整帧丢弃 */\n    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) { return 2U; }\n    return 0U;\n}\n\nvoid SYS_DHT11_Init(GPIO_TypeDef *port, uint16_t pin)\n{\n    s_port = port;\n    s_pin  = pin;\n    GPIO_InInit(port, pin, GPIO_PuPd_UP);  /* 空闲态就放开总线，别一直拉 */\n}\n\n/* 返回 0 成功 / -1 无响应 / -2 校验失败 */\nint SYS_DHT11_Read(float *temp_c, float *humi_rh)\n{\n    uint8_t r;\n\n    if (s_port == 0) { return -1; }\n\n    r = dht11_read_frame();\n    if (r == 1U) { return -1; }\n    if (r == 2U) { return -2; }        /* 不用可疑值，直接报错给上层   */\n\n    /* DHT11 小数位恒为 0，仍按 0.1 组合，将来换 DHT22 不用改调用方 */\n    if (humi_rh != 0) { *humi_rh = (float)s_raw[0] + (float)s_raw[1] * 0.1f; }\n    if (temp_c  != 0) { *temp_c  = (float)s_raw[2] + (float)s_raw[3] * 0.1f; }\n    return 0;\n}\n\n/* 带重试：无应答重试没用（接线 / 上拉问题），只有校验失败才退避重试 */\nint SYS_DHT11_ReadRetry(float *temp_c, float *humi_rh, uint8_t retries)\n{\n    uint8_t k;\n    int     r = -1;\n\n    for (k = 0U; k < retries; k++)\n    {\n        r = SYS_DHT11_Read(temp_c, humi_rh);\n        if (r == 0)  { return 0; }\n        if (r == -1) { break; }        /* 无应答：重试没有意义            */\n        delay_ms(20);                  /* 只有校验失败才退避重试          */\n    }\n    return r;\n}\n\n/* ⑦ 用法：独立任务里每 2s 调一次 ReadRetry，成功就把温湿度通过队列\n * 交给处理任务，由处理任务按大端 ×100 填进 ㉒ 的 20 字节环境帧。 */",
 r:"① 先用逻辑分析仪挂 PG9，抓一次完整的读：应该看到一条 ≥18ms 的低电平（起始信号），紧跟 80us 低 + 80us 高的应答，然后 40 个「50us 低 + 26us 或 70us 高」的位。量的起始低电平如果只有 8~9ms，就是毫秒延时被写成了慢一倍的粗延时。\n② 再抓位内的高电平宽度：0 应该 ≈26us、1 应该 ≈70us，两者必须能明显分开。如果全是同一个宽度（比如都 ≈80us），说明微秒延时不准，读出来只会是全 0 或全 255。\n③ 串口打印裸字节：健康帧形如 45 0 24 0 69，前 4 字节之和的低 8 位正好等于第 5 字节。连续打印 10 次，1Hz 采样率下应该每次都能通过校验。\n④ 拔掉传感器再读：应返回 -1（无响应），且必须在几毫秒内返回而不是卡住，这验证了超时判断有效。",
 k:"DHT11 单总线 温湿度 传感器 单总线时序 起始信号 18ms 应答 80us 40位 校验和 CHECKSUM 位0 位1 26us 70us 微秒延时 DWT CYCCNT delay_us 关中断 临界区 上拉电阻 4.7k 10k 采样率 1Hz 重试 超时 GPIOG GPIO_Pin_9 PG9 SYS_DHT11_Init SYS_DHT11_Read SYS_DHT11_GetRaw SYS_DHT11_ReadRetry SYS_DHT11_LOCK_IRQ DHT11_BIT_THRESHOLD_US DHT11_MIN_INTERVAL_MS DHT11 与 DS18B20 不能混用 检测端 第一路数据源 相关"
}
"""

GUIDE_P24_DHT11 = r"""
/* ===================== ㉔ DHT11 单总线温湿度引导 ===================== */
{m:"引导", t:"guide", n:"🧭 ㉔ DHT11 单总线温湿度引导（从一根线到一次可信的读数）",
 d:"DHT11 看起来简单：一根线、五个字节。但它是时序类器件的典型代表，协议的全部内容都写在「电平持续了多久」里，所以读不到数据时，问题往往不在协议理解，而在延时准不准、等待有没有超时、这 4ms 有没有被中断打断。这九步按「先把底子打准，再谈协议」的顺序排：先认线、再标定延时、然后才发起始信号、读 40 位、把关中断的账算清楚，最后才是接硬件和排障。",
 g:"STM32F407（168MHz）· DHT11 单总线温湿度 · 数据脚 PG9（GPIOG / GPIO_Pin_9，板上已有 4.7k~10k 上拉）· 对应 FWLIB 板1 的 sys_dht11.c（板2 对应 dht11.c，两套 API 名不同）",
 k:"DHT11 引导 向导 教程 单总线 温湿度 时序 微秒延时 DWT 起始信号 应答 40位 校验和 关中断 1Hz 采样率 上拉 排障 步骤",
 e:"/* ============================================================\n *  ㉔ DHT11 单总线温湿度，完整成品代码\n *  平台：STM32F407 · 168MHz · 数据脚 PG9（板上带上拉）\n *  说明：按板2 dht11.c 的思路写（整数 API + 间隔拦截 + 重试），\n *        板1 的对应封装是 sys_dht11.c 的 SYS_DHT11_* 一套。\n * ============================================================ */\n#include \"stm32f4xx.h\"\n#include \"delay.h\"\n#include \"gpio_core.h\"\n\n#define DQ_PORT          GPIOG\n#define DQ_PIN           GPIO_Pin_9\n#define START_LOW_MS     20U        /* 手册 ≥18ms                        */\n#define WAIT_TIMEOUT_US  100U       /* 单段等待上限，手册最长 80us       */\n#define BIT_HIGH_US      45U        /* 位1≈70us / 位0≈26us，取中间       */\n#define MIN_INTERVAL_MS  1100U      /* 采样率只有 1Hz                    */\n#define RETRY_N          3U\n\nstatic uint8_t  g_raw[5];\nstatic uint32_t g_last_us;\nstatic uint8_t  g_never = 1U;\n\n/* 微秒时间戳：板1 的 delay.h 没有 DWT_GetUs()，直接拿 CYCCNT 换算 */\nstatic uint32_t now_us(void)\n{\n    return DWT->CYCCNT / (SystemCoreClock / 1000000U);\n}\n\nstatic uint32_t wait_level(uint8_t level, uint32_t timeout_us)\n{\n    uint32_t t0  = DWT->CYCCNT;\n    uint32_t lim = timeout_us * (SystemCoreClock / 1000000U);\n\n    while (GPIO_InRead(DQ_PORT, DQ_PIN) != level)\n    {\n        if ((DWT->CYCCNT - t0) > lim) { return 0xFFFFFFFFUL; }  /* 超时 */\n    }\n    return (DWT->CYCCNT - t0);\n}\n\n/* 1 = 还没到采样间隔（这一帧不要读，DHT11 最快 1Hz） */\nstatic uint8_t too_soon(void)\n{\n    if (g_never != 0U) { return 0U; }\n    return ((now_us() - g_last_us) < (MIN_INTERVAL_MS * 1000U)) ? 1U : 0U;\n}\n\n/* 0 成功 / 1 无应答 / 2 校验和错 / 3 间隔不足 */\nstatic uint8_t read_frame(void)\n{\n    uint8_t  i, d[5] = {0, 0, 0, 0, 0};\n    uint32_t irq, high, thresh;\n\n    delay_us(1);                    /* 确保 DWT 计时已使能              */\n\n    /* ① 起始信号：拉低 20ms 再释放 */\n    GPIO_OutInitOD(DQ_PORT, DQ_PIN);\n    GPIO_OutWrite(DQ_PORT, DQ_PIN, 0U);\n    delay_ms(START_LOW_MS);\n    GPIO_OutWrite(DQ_PORT, DQ_PIN, 1U);\n\n    /* ② 关中断窗口开始 */\n    irq = __get_PRIMASK();\n    __disable_irq();\n    GPIO_InInit(DQ_PORT, DQ_PIN, GPIO_PuPd_UP);\n    thresh = (SystemCoreClock / 1000000U) * BIT_HIGH_US;\n\n    /* ③ 三次等待：应答低 → 应答高 → 应答高结束（进入 bit0） */\n    if (wait_level(0U, WAIT_TIMEOUT_US) == 0xFFFFFFFFUL ||\n        wait_level(1U, WAIT_TIMEOUT_US) == 0xFFFFFFFFUL ||\n        wait_level(0U, WAIT_TIMEOUT_US) == 0xFFFFFFFFUL)\n    {\n        __set_PRIMASK(irq);\n        GPIO_InInit(DQ_PORT, DQ_PIN, GPIO_PuPd_UP);\n        return 1U;\n    }\n\n    /* ④ 40 位，高位在前 */\n    for (i = 0U; i < 40U; i++)\n    {\n        if (wait_level(1U, WAIT_TIMEOUT_US) == 0xFFFFFFFFUL) { break; }\n        high = wait_level(0U, WAIT_TIMEOUT_US);\n        d[i >> 3] = (uint8_t)(d[i >> 3] << 1);\n        if (high > thresh) { d[i >> 3] |= 0x01U; }\n    }\n\n    __set_PRIMASK(irq);\n    GPIO_InInit(DQ_PORT, DQ_PIN, GPIO_PuPd_UP);\n    for (i = 0U; i < 5U; i++) { g_raw[i] = d[i]; }\n\n    /* ⑤ 校验和 = 前 4 字节之和的低 8 位 */\n    if ((uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4]) { return 2U; }\n\n    g_last_us = now_us();\n    g_never   = 0U;\n    return 0U;\n}\n\n/* 对外：整数温湿度；返回 0 成功 / 1 无应答 / 2 校验错 / 3 间隔不足 */\nint DHT11_Get(uint8_t *temp_c, uint8_t *humi_pct)\n{\n    uint8_t k, r = 1U;\n\n    if (temp_c == 0 || humi_pct == 0) { return 4; }\n\n    for (k = 0U; k < RETRY_N; k++)\n    {\n        r = read_frame();\n        if (r == 0U) { break; }\n        if (r == 1U) { return 1; }   /* 无应答：重试也没有意义           */\n        if (r == 3U) { return 3; }   /* 间隔不足：别拿旧值              */\n        delay_ms(50);                /* 只有校验失败才退避重试           */\n    }\n    if (r != 0U) { return (int)r; }\n\n    *humi_pct = g_raw[0];            /* 小数位恒为 0，取整数即可         */\n    *temp_c   = g_raw[2];\n    return 0;\n}\n\nvoid DHT11_DumpRaw(void)\n{\n    printf(\"DHT11 raw: %02X %02X %02X %02X %02X\\r\\n\",\n           g_raw[0], g_raw[1], g_raw[2], g_raw[3], g_raw[4]);\n}",
 f:[
  [
   "先认清「一根线」这件事：此刻到底谁在驱动它",
   "DHT11 的 DATA 是双向线：主机先拉低它发命令，松手之后由从机拉低 / 拉高，把 40 位数据「顶」出来。所以同一根脚在几毫秒内要换好几次主人，代码里也就必须能来回切换输入 / 输出。\n\n两种切法都能跑，本项目两板各用了一种：\n· 板1（sys_dht11.c）：每次读之前 GPIO_OutInitOD 配成开漏输出，进临界区后再 GPIO_InInit 配成输入上拉，读完再切回输入上拉。\n· 板2（dht11.c）：初始化时一次配成开漏输出就不再切方向，靠板上 10K 上拉拉高，读电平直接读 IDR（GPIO_InRead）。\n\n为什么输出要用开漏（GPIO_OutInitOD）而不是推挽：开漏写 1 等于高阻态、等于「松手」，上拉电阻把线拉高；从机这时才能把线拉低。推挽写 1 是主动输出高电平，会跟从机对着干，轻则读不出来，重则有对灌电流。",
   "① 先确认接线：PG9 接 DHT11 的 DATA；② 初始化就把 PG9 配成输入上拉（空闲态放开总线）；③ 写一小段往返切换的代码，量一量电平对不对。",
   "/* 引脚在「输入上拉」和「开漏输出」之间来回切 */\nGPIO_InInit(GPIOG, GPIO_Pin_9, GPIO_PuPd_UP);   /* 空闲：高阻输入，总线放开 */\n\n/* 要发起始信号时 */\nGPIO_OutInitOD(GPIOG, GPIO_Pin_9);             /* 开漏输出 */\nGPIO_OutWrite(GPIOG, GPIO_Pin_9, 0U);          /* 拉低   */\ndelay_ms(20);\nGPIO_OutWrite(GPIOG, GPIO_Pin_9, 1U);          /* 松手：开漏高 = 高阻态 */\n\n/* 要收数据时 */\nGPIO_InInit(GPIOG, GPIO_Pin_9, GPIO_PuPd_UP);  /* 转回输入，读真实电平 */\nif (GPIO_InRead(GPIOG, GPIO_Pin_9) != 0U) { /* 高 */ }",
   "空闲时用万用表量 PG9 应该是 3.3V（上拉把它拉高了）。\n再手动试：配成开漏写 0 → 量到 0V；写 1 → 又回到 3.3V。\n如果开漏写 1 之后量出来还是 0V，说明引脚被别的东西拉死了（短路、或者同一根线上还插着 DS18B20 在拉低）。",
   "输出配成推挽是这里最常见的方向性错误：推挽写 1 会与从机的下拉对抗，读到的永远是 1。\n想「看一眼现在的电平」必须用 GPIO_InRead（读 IDR，引脚真实电平）；GPIO_OutRead 读的是 ODR，那是软件刚写进去的值，不是线上的真实电平，用它判断会一直「看起来是对的」。\n切模式之后不要忘了写一次电平（拉低或释放），别让引脚停在不确定的状态上。",
   "GPIO_OutInitOD GPIO_InInit GPIO_InRead GPIO_OutWrite GPIO_PuPd_UP GPIOG GPIO_Pin_9"
  ],
  [
   "把微秒延时标定准（这一步偷懒，后面全白干）",
   "DHT11 的全部信息都在「电平持续了多久」里：80us 的应答、50us 的位起始、26us 的 0、70us 的 1。判 0 和判 1 的差别只有 44us。\n\n本项目的 delay_us 是用 DWT 周期计数器做的：把 DWT->CYCCNT 使能，cycles = us × (SystemCoreClock / 1000000)，然后忙等周期数走完。168MHz 下 1us = 168 个周期，50us 阈值就是 8400 个周期，这个换算是自动跟随主频的，改主频不用改代码，但前提是 SystemCoreClock 这个 CMSIS 变量是对的。\n\n为什么必须强调「准」：本项目历史上 delay_ms 是用「空转 50000 次约等于 1ms」凑系数凑出来的，从未标定，实测约 2.09ms/次（慢了一倍）。它被 LED、按键消抖和 DHT11 一起用，于是现场出现「DHT11 起始信号只有 9ms，不到手册要求的 18ms → 器件根本不应答」这种谁也说不清的现象。延时不准时，读出的数据并非「有点误差」，而是全 0 或全 255：因为高电平无论 26us 还是 70us，都被同一个错误的尺子量成同一长度。",
   "① 用 delay_us 做一个已知宽度的方波；② 拿逻辑分析仪或示波器量实际宽度；③ 量起始低电平是不是 20ms。三条都对上，再往下走。",
   "/* 让 PG9 每 500us 翻转一次：量出来应该是 1kHz、占空比 50% 的方波 */\nGPIO_OutInit(GPIOG, GPIO_Pin_9);\nfor (;;)\n{\n    GPIO_OutToggle(GPIOG, GPIO_Pin_9);\n    delay_us(500);          /* 实测半周期必须 ≈500us */\n}\n\n/* 起始信号那段也一样要量 */\nGPIO_OutInitOD(GPIOG, GPIO_Pin_9);\nGPIO_OutWrite(GPIOG, GPIO_Pin_9, 0U);\ndelay_ms(DHT11_START_LOW_MS);   /* 逻辑分析仪上必须 ≥18ms */\nGPIO_OutWrite(GPIOG, GPIO_Pin_9, 1U);",
   "逻辑分析仪接 PG9，跑上面的方波测试：半周期应为 500us，误差在百分之几以内。\n再抓一次起始信号：低电平宽度必须 ≥18ms（本工程用 20ms）。\n如果量出来是 1000us 或 41ms 这种整数倍偏差，就是用了未标定的粗延时。",
   "别用「空循环凑系数」的延时函数做任何单总线时序。本项目 delay_loop 就是这种未标定粗延时，只能给 LED / 蜂鸣器打节拍，绝不能进 DHT11。\n微秒延时是忙等，要放在能被打断也没关系的位置；一旦进入关中断窗口，这几百微秒也是真正不让系统响应中断的时间，要计入实时性。\n换主频（SYS_CLK_Switch）之后 SystemCoreClock 会自动更新，延时跟着变，但 SYS_TICK_Init 要重新调；微秒延时一律用 delay.h 的 DWT 版，不要自己再写一套。",
   "delay_us delay_ms delay_ms_dwt DWT CYCCNT SystemCoreClock SYS_CLK_Switch delay_loop"
  ],
  [
   "起始信号 + 三次等待（最容易错的一步）",
   "完整时序是这样的（数字都来自 DHT11 手册）：\n\n① 主机拉低 ≥18ms（板2 用 20ms 留余量）；\n② 主机松手，等 20~40us；\n③ 从机拉低 80us → 再拉高 80us（这就是「应答」）；\n④ 从机接着发 40 位，每位 = 50us 低电平 + 高电平（26~28us 表示 0，70us 表示 1）；\n⑤ 最后一位结束后再来 50us 低电平，总线放开。\n\n代码上最容易错的地方是「等应答」：必须等三次，等低（应答低起点）、等高（应答高起点）、再等一次低（应答高结束，也正好是 bit0 的 50us 低电平起点）。少这第三次，位循环第一次「等高电平」时线已经是高的，函数立刻返回，量到的是应答高电平的尾巴（60~80us，大于 45us 阈值），于是凭空多读出一个 1，整帧 40 位整体右移一位：第 1 字节最高位被强行置 1（湿度恒 ≥128）、第 5 字节丢掉真正的最后一位。\n\n这一错误最隐蔽之处是：校验和约有 2/3 概率误通过，所以上层看到的并非「读取失败」，而是「湿度 200% 这类超范围数据」，排障时容易怀疑传感器，实际原因是软件少等了一个边沿。",
   "写出「发起始 → 等低 → 等高 → 再等低」的四步，并且每一次等待都带超时（超时就返回「无应答」，绝不死等）。",
   "/* ① 起始信号 */\nGPIO_OutInitOD(DQ_PORT, DQ_PIN);\nGPIO_OutWrite(DQ_PORT, DQ_PIN, 0U);\ndelay_ms(20);\nGPIO_OutWrite(DQ_PORT, DQ_PIN, 1U);\n\n/* ② 进临界区，转输入 */\nirq = __get_PRIMASK();\n__disable_irq();\nGPIO_InInit(DQ_PORT, DQ_PIN, GPIO_PuPd_UP);\n\n/* ③ 三次等待，一次都不能少 */\nif (wait_level(0U, 100U) == TIMEOUT ||   /* 应答低起点 */\n    wait_level(1U, 100U) == TIMEOUT ||   /* 应答高起点 */\n    wait_level(0U, 100U) == TIMEOUT)     /* 应答高结束 → bit0 起点 */\n{\n    __set_PRIMASK(irq);\n    GPIO_InInit(DQ_PORT, DQ_PIN, GPIO_PuPd_UP);\n    return 1U;                            /* 无应答 */\n}",
   "用逻辑分析仪抓一次完整读：起始低电平 ≥18ms，之后是 80us 低 + 80us 高的应答，然后才是 40 个位。\n软件侧：把 5 个裸字节打出来，第一个字节（湿度整数）应该在 20~90 之间。\n如果湿度整数恒定 ≥128，或者校验和时对时错，先回头数一数是不是只等了两次。",
   "少了第三次等待 → 整帧右移一位（上面已展开讲过现象）。\n等待不带宽限、写成 while(引脚 != 电平); 的死等 → 传感器没插时总线一直是高，任务永远卡在这一行；上 FreeRTOS 后表现为「这个任务再也没输出」，而且看门狗也救不了（因为别的任务可能还在跑）。\n起始信号的 18ms 用错函数（比如用了未标定的粗延时）→ 器件不应答，返回「无响应」，和「没插线」的现象一模一样，很容易误判成硬件问题。",
   "GPIO_OutInitOD GPIO_InInit delay_ms __disable_irq __set_PRIMASK __get_PRIMASK"
  ],
  [
   "读 40 位：量高电平宽度，分 0 和 1",
   "每位都以 50us 低电平开头，后面的高电平宽度才是信息：26~28us 是 0，70us 是 1。所以读一位的动作就是「先等高电平开始，再量它持续了多少微秒」。\n\n判据不要写死具体微秒数，而是取两者中间的一个阈值（板2 用 DHT11_BIT_THRESHOLD_US = 45）。真正的判据是「高电平周期数 > 45us 对应的周期数」，这样主频变了也不用改。\n\n40 位的排列是固定的 5 个字节：湿度整数、湿度小数、温度整数、温度小数、校验和，每字节高位在前。所以循环里每读一位就 `byte = byte << 1;`，测到是 1 就 `|= 1`。DHT11 的两个小数字节实际恒为 0，别以为是「读漏了」，要真小数得换 DHT22/AM2302，那种是 16 位编码，解析方式完全不同。",
   "循环 40 次：等高电平开始 → 量高电平宽度 → 移位存位；循环结束后算校验和。",
   "/* ④ 40 位，高位在前 */\nfor (i = 0U; i < 40U; i++)\n{\n    if (wait_level(1U, 100U) == TIMEOUT) { break; }   /* 等上升沿 */\n    high_us = wait_level(0U, 100U);                   /* 高电平有多长 */\n\n    data[i >> 3] = (uint8_t)(data[i >> 3] << 1);      /* 先左移 */\n    if (high_us > 45U) { data[i >> 3] |= 0x01U; }     /* 再补最低位 */\n}\n\n/* ⑥ 校验和 = 前 4 字节之和的低 8 位 */\nsum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);\nif (sum != data[4]) { return 2U; }                    /* 整帧丢弃 */",
   "串口把 5 个字节打出来：健康帧形如 45 0 24 0 69（45+0+24+0=69）。\n连续跑 30 秒，每 2 秒一帧，绝大多数应该是健康帧；偶尔一帧校验失败是正常的（环境干扰、被中断蹭到），靠重试兜。\n如果每一帧都失败，先把高电平实测宽度打出来，看 0 和 1 是不是真的分得开。",
   "校验和不对绝不能拿着可疑值往下用。宁可上报一次失败、让上层沿用上一次的值或标记为无效，也不要让 200% 的湿度流进 LoRa 帧和本地缓存。\n移位方向别写反：DHT11 是高位在前，写成低位在前会让温湿度变成一个看似合理、完全错的值，而且校验和照样通过（校验和只查字节和，不管位序），这类错误能骗过所有自检。\n位 0 和位 1 的余量只有 44us，杜邦线过长 / 上拉偏大都会让边沿变缓，表现成「偶尔某一位翻转」。线长超过 20cm 就该考虑缩短或换屏蔽线。",
   "GPIO_InRead DWT CYCCNT 校验和 高位在前 wait_level"
  ],
  [
   "关中断与实时性的取舍：这 4ms 到底该不该关",
   "40 位大约 4ms，加上应答段，一次读取 4~5ms。这段时间里，只要有个中断进来占用几百微秒，所有「等高电平」的计时就整体错位一位甚至几位，读出来就是坏帧。所以时序敏感的读取必须放进临界区。\n\n板1 的做法是宏开关 SYS_DHT11_LOCK_IRQ = 1：进读取窗口时 irq = __get_PRIMASK(); __disable_irq();，读完立刻 __set_PRIMASK(irq); 恢复。板2 全库没有关中断，靠的是「读取很短 + 失败就重试」。\n\n关中断的代价要算清楚：板1 的单段等待超时是 100us，最坏情况下（器件不响应）41 次等待全部超时 ≈ 4.1ms；如果把超时放宽到 200us，最坏就是 8.2ms，在 FreeRTOS 上表现为 1ms 的 tick 被推迟 8 个，vTaskDelay(1) 变成实际 9ms，串口接收也可能丢字节。所以超时值不是随便填的：手册说任何一段电平都不超过 80us，取 100us 已经有 25% 余量，没必要取更大。",
   "① 用 __get_PRIMASK 保存现场、退出时 __set_PRIMASK 原样写回（别直接 __enable_irq 把别人的关中断状态冲掉）；② 临界区里只做时序，不做 printf、不做队列发送；③ 想清楚这段时间最长是多久，并把它写进注释。",
   "/* ② 进临界区：先存现场的 PRIMASK，退出时原样恢复 */\nirq = __get_PRIMASK();\n__disable_irq();\n\n/* ... 这里只做时序：三次等待 + 40 位，约 4ms ... */\n\n/* ⑤ 先恢复中断，再做别的 */\n__set_PRIMASK(irq);\nGPIO_InInit(DQ_PORT, DQ_PIN, GPIO_PuPd_UP);   /* 恢复空闲态 */\n\n/* 反例：退出时直接 __enable_irq()，会把「本来就关着中断」的\n * 上层现场强行打开，多层临界区嵌套时就出错了。 */",
   "在临界区前后各翻转一个空闲引脚，用逻辑分析仪量这段高电平：正常读到数据时应该 ≈4~5ms；把传感器拔掉，量到的是最坏值（约 4.1ms，等于 41×100us），这就是系统被推迟响应中断的时间。\n如果这个值比实时性预算大，方案只有两个：把读取放进优先级最高的任务里，或者接受偶发失败靠重试。",
   "临界区里不要放 printf / 队列 / 延时较长的东西，那会把 4ms 变成几十毫秒，看门狗、串口、RTOS tick 全都受影响。\n恢复现场要用 __set_PRIMASK(旧值)，不要用 __enable_irq() 无脑开。\n上了 FreeRTOS 之后，关中断 4ms 期间 tick 会累积、恢复后一次性补上，表现为「这一拍的任务集体晚了几毫秒」。可以接受，但要知道它存在。\n另一个思路：不关中断，把 DHT11 读取任务的优先级设成最高（不被抢占 == 天然临界区），这样就不需要关中断，代价是这几毫秒里别的任务都排不上。",
   "__disable_irq __enable_irq __get_PRIMASK __set_PRIMASK PRIMASK 临界区 FreeRTOS tick 实时性"
  ],
  [
   "1Hz 节拍与重试策略：读快了只会拿到旧值",
   "DHT11 内部要重新采集，手册给的采样周期是 ≥1 秒。读得太密，器件要么把上一次的结果再吐一遍，要么干脆不应答。所以「读太快」的症状很特别：并非报错，而是温度湿度数分钟不变，易被误判为「传感器坏了」，实际读到的是旧值。\n\n板2 的做法很值得抄：驱动内部用 DHT11_MIN_INTERVAL_MS = 1100 和上一次成功读取的时间戳做比较，间隔不够就直接返回「间隔不足（3）」，不给脏数据，把节拍的责任放在驱动里，上层就算每秒调一次也不会读坏。\n\n重试策略要分清两类失败：\n· 无应答（-1 / 1）：传感器没插、线松、没上拉、上电不满 1 秒。重试没有意义（还会白等一阵），直接上报更利于排障。\n· 校验失败（-2 / 2）：时序被干扰了，值得退避一下再读一次（板2 等 50ms，板1 等 20ms），这也是日常最常用的路径。",
   "① 记录「上次成功读取」的微秒时间戳；② 每次读之前先判间隔；③ 把「无应答」和「校验失败」分开处理。",
   "/* 用时间戳拦住过密读取（板1 的 delay.h 没有 DWT_GetUs，用 CYCCNT 换算） */\nstatic uint32_t now_us(void) { return DWT->CYCCNT / (SystemCoreClock / 1000000U); }\n\nif (g_never == 0U && (now_us() - g_last_us) < 1100U * 1000U)\n{\n    return 3;                 /* 间隔不足：宁可返回这个码，也不给旧值 */\n}\n\n/* 重试：只有校验失败才退避重试 */\nfor (k = 0U; k < 3U; k++)\n{\n    r = read_frame();\n    if (r == 0U) { break; }\n    if (r == 1U) { return 1; }   /* 无应答：重试无意义               */\n    delay_ms(50);                /* 校验失败：退避 50ms 再来一次     */\n}",
   "应用层每 2 秒调一次，串口日志里应该是稳定的「两秒一帧」，而不是刷屏。\n如果强行改成每 100ms 调一次：应当看到返回值是「间隔不足（3）」，而不是同样的温湿度被反复上报。\n拔掉传感器：立刻返回「无应答（1）」，并且不要花三次重试的时间。",
   "别把「读得快」当成「数据实时」。温湿度的物理变化本来就慢，1Hz 完全够，2 秒一帧更是绰绰有余。\n别在重试里把「无应答」也重试三遍还各等 50ms，那会让一次失败阻塞 150ms 以上，采集任务直接变成系统瓶颈。\n微秒时间戳要用差值法比较（now - last），不要比大小：DWT 的 CYCCNT 约 25.6 秒 @168MHz 就回绕一次（板2 的 DWT_GetUs 取的是微秒、约 71 分钟回绕），直接比大小会在回绕那天抽风。\n上电第一次读之前要 delay_ms(1000) 让器件稳定，否则第一次必然是坏的。",
   "DHT11_MIN_INTERVAL_MS DHT11_TimeToNextRead DWT_GetUs DWT_ElapsedUs CYCCNT 回绕 差值法 重试 退避"
  ],
  [
   "硬件与接线：上拉、线长、上电 1 秒，还有 DS18B20 这个邻居",
   "单总线靠上拉电阻把空闲态拉高，这是协议能工作的前提，不是可选项：\n· 三针的 DHT11 模块：板上已经焊了 4.7k~10k，直接插。\n· 裸的四针传感器：必须自己在 DATA 和 VCC 之间加 4.7k~10k。忘了加，现象就是「永远无应答」，和「没插」一模一样。\n\n接线方向也别搞错：直插的 DHT11，把网面朝自己，从左往右是 1=VCC、2=DATA、3=NC（空脚）、4=GND。插反了两端正好是电源对地，器件会发热。\n\n本项目两板的 PG9 都接好了上拉，但板2 的原理图注释里写得很清楚：这个 J6 接口的网络名是 DHT11/DS18B20，板上只有一路 10K 上拉，两个器件二选一，不能同时插。不能同时插的原因：两者都是单总线，但协议完全不同（DHT11 是「主机起始 + 从机应答 + 40 位定时编码」，DS18B20 是「复位脉冲 + 存在脉冲 + ROM/功能命令 + 1-Wire 时隙」）。两个器件同时挂在同一根线上，会同时响应主机的起始/复位脉冲，谁的数据都读不出来，而且互相拉低还可能把电平拖坏。\n\n线长：超过 20cm 时分布电容让上升沿变缓，偶发位错；这时优先缩短线、其次是适当减小上拉（下限不要低于 4.7k，否则从机拉不低）。",
   "① 确认上拉电阻（模块看丝印，裸器件自己焊）；② 确认方向：1=VCC、2=DATA、3=NC、4=GND；③ 确认这根线上没有第二个单总线器件；④ 上电先等 1 秒再读第一帧。",
   "/* 接线确认（本项目两板一致）\n *   DHT11 1=VCC  → 3.3V / 5V\n *   DHT11 2=DATA → PG9（板上已有 4.7k~10k 上拉到 3.3V）\n *   DHT11 3=NC   → 空\n *   DHT11 4=GND  → GND\n *\n * 同一根 PG9 上不要再插 DS18B20：协议不同，会互相干扰。\n */\n\nSYS_DHT11_Init(SYS_DHT11_PORT, SYS_DHT11_PIN);\ndelay_ms_dwt(1500);      /* 上电稳定约 1s 之后才读得到有效数据 */",
   "空闲时量 PG9 应为 3.3V（上拉有效）。\n插上传感器、上电等 1 秒后读，前三次里应该至少有一次成功。\n如果一直无应答：先量 DATA 对 VCC 的电阻，应该在 4.7k~10k 之间（模块）或接近无穷（裸器件，说明缺上拉）。",
   "拿 DS18B20 的代码去读 DHT11（或对调）是最常见的浪费：两者虽然都是单总线、都接在同一类接口上，但时序和命令体系完全不同，驱动不能互换。\n上电立刻就读 → 第一帧必然失败，然后被误判成「接线错了」。给它 1 秒。\n用 5V 供电而 MCU 是 3.3V 时，DATA 的上拉要拉到 3.3V，别拉到 5V（除非确认引脚耐压）。\n传感器贴着发热元件（稳压芯片、LoRa 模块）装，温度会偏高好几度，这不是读数问题，是安装问题。",
   "上拉电阻 4.7k 10k GPIOG GPIO_Pin_9 PG9 DS18B20 不能混用 上电稳定 1s"
  ],
  [
   "拿到数据之后：打包成 20 字节帧、超限落本地、队列交给处理任务",
   "读完的温湿度只是两个整数，得想清楚它往哪去。本项目检测端的链路是：\n\nDHT11 → 独立任务（每 2s）→ 队列 → 处理任务 → 环境帧 → 上行 / 本地缓存\n\n几个具体的问题：\n· 放独立任务：读取一次要阻塞 4~5ms（还关中断），塞进主循环会让整个系统的节拍抖动；做成任务并用 vTaskDelay 稳住 2s 节拍最省心。\n· 用队列而不是全局变量：全局变量在「读一半被别的任务抢走」时会读到新旧混合的数据；队列拷贝的是整份样本，天然一致。\n· 打包成大端 int16：本项目 ㉒ 的 sys_frame 环境帧里，温度是 ℃×100、湿度是 %RH×100，全部大端。湿度 45%RH → 0x1194(4500)，温度 24℃ → 0x0960(2400)，-5℃ 用补码 0xFE0C。\n· 超限或断网先落本地：⑱ 那套 Flash 缓存的意义就在这里，传感器读到的值先判断合理范围（湿度 20~90%RH、温度 0~50℃ 是可靠量程），异常值不要往上报，同时判断链路状态，不通就先写缓存。（另 ㉒ 里气压是 hPa×10，不是 ×100，写错会溢出）",
   "① 把读取放进独立任务，用队列把样本交给处理任务；② 处理任务里做「校验通过 + 量程检查」双重判断；③ 通过后按大端 ×100 填进 20 字节环境帧，链路不通就先落本地缓存。",
   "typedef struct { float temp_c; float humi_rh; } env_sample_t;\n\nvoid Task_DHT11(void *arg)\n{\n    env_sample_t s;\n    SYS_DHT11_Init(SYS_DHT11_PORT, SYS_DHT11_PIN);\n    delay_ms_dwt(1000);                 /* 上电稳定 1s                 */\n\n    for (;;)\n    {\n        if (SYS_DHT11_ReadRetry(&s.temp_c, &s.humi_rh, 3U) == 0)\n        {\n            xQueueSend(g_env_queue, &s, 0U);   /* 拷贝整份样本，天然一致 */\n        }\n        vTaskDelay(pdMS_TO_TICKS(2000));\n    }\n}\n\n/* 处理任务里：校验通过 + 量程检查，再按大端 ×100 填进 20 字节帧 */\ntemp_x100 = (int16_t)(s.temp_c * 100.0f);\nhumi_x100 = (int16_t)(s.humi_rh * 100.0f);\nSYS_FRAME_BuildEnv(frame, sizeof(frame), temp_x100, humi_x100, press, light, tvoc, mq135);",
   "串口打印处理任务收到的每一条样本，温湿度应该每 2 秒更新一次。\n把 LoRa 拔掉（模拟断网）：数据应该开始进 ⑱ 的本地缓存，条数递增而不是凭空消失。\n用上位机看 20 字节帧：第 0~1 字节是温度大端，第 2~3 字节是湿度大端，位置和 ㉒ 的偏移定义一致。",
   "别在采集任务里做 printf 和打包，更别在关中断的窗口里做，那会把 4ms 拉长成几十毫秒。\n别用全局变量在任务间传样本，尤其是「读一半被抢占」的场景。\n帧里的多字节字段是大端，写反了在自家两板之间收发能对上（都错），一接上位机就露馅。\n异常值（湿度 0、温度 85 这种）通常意味着读坏了或者器件没插，不要直接上报，也不要直接写进缓存污染历史数据。",
   "SYS_FRAME_BuildEnv sys_frame 20 字节 大端 int16 x100 FreeRTOS xQueueSend vTaskDelay 独立任务 断网缓存 异常值过滤"
  ],
  [
   "排障表：全 FF、全 00、个别位不同、校验和假通过",
   "读不到正确的数据时，先看裸字节，不要先改代码。5 个裸字节能直接告诉故障在哪一层：\n\n· 全 FF FF FF FF FF：总线一直是高。器件没应答，没插、接触不良、缺上拉、或者上电不满 1 秒。也有可能是起始信号没有发出去（引脚没真正拉低）。\n· 全 00 00 00 00 00：总线一直是低。引脚被拉死，或者方向配错（比如一直当输出在拉低），或者线对地短路。\n· 只有个别位不同：时序抖动。杜邦线太长、上拉偏大、或者读取期间被高频中断打断。缩短线、或者确认关中断确实生效。\n· 湿度整数恒 ≥128、校验和却大部分能过：这就是少了「第三次等待」，整帧右移一位，本卡重点讲过的那个问题。\n· 数据完全不动：读太密了，器件一直吐旧值。检查采样间隔是不是 ≥1 秒。\n\n排障顺序建议：先打印裸字节 → 再上逻辑分析仪量起始信号和位宽 → 最后才怀疑传感器本身。否则会在换第三个传感器之后才发现延时函数写错。",
   "① 打开裸字节打印（板1 的 SYS_DHT11_GetRaw 配 printf）；② 对照上面的现象表定位；③ 只有前两步都排除掉，才去怀疑器件。",
   "uint8_t raw[5];\nfloat   t = 0.0f, h = 0.0f;\nint     r;\n\nr = SYS_DHT11_Read(&t, &h);\nSYS_DHT11_GetRaw(raw);\nprintf(\"r=%d  raw: %02X %02X %02X %02X %02X\\r\\n\",\n       r, raw[0], raw[1], raw[2], raw[3], raw[4]);\n\nif (r == -1) { /* 全 FF：查 PG9 接线、器件方向、上拉、上电是否够 1s */ }\nif (r == -2) { /* 校验失败：看是不是个别位不同，或确认 LOCK_IRQ 是否为 1 */ }",
   "连续读 10 次，把返回值分布打出来：应该是绝大多数 r=0。\n拔掉传感器：应该立刻变成 r=-1 且裸字节全 FF。\n把 PG9 短到 GND 再读：应该变成裸字节全 00（这就是「总线被拉死」的现象）。",
   "别一上来就换传感器，「永远无应答」里硬件问题（缺上拉）和软件问题（延时不准、起始信号太短）各占一半，先用逻辑分析仪分清是哪一半。\n校验和通过不等于数据正确：位序写反、整帧移位都有可能让校验和照样通过。始终用「校验 + 量程」双重判断。\n天气原因也要考虑：湿度 90%RH 以上、温度 0℃ 以下都在 DHT11 的可靠量程之外，读出来的数看看就好，别当成故障。",
   "SYS_DHT11_GetRaw 裸字节 排障 全FF 全00 校验和 假通过 量程 逻辑分析仪 示波器"
  ]
 ]
}
"""
