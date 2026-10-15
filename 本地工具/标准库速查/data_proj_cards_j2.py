# -*- coding: utf-8 -*-
r"""data_proj_cards_j2.py —— ㉗ 数字滤波 + 卡尔曼 + PID 闭环（引导向导）。

写法约定（与 data_proj_cards.py 一致）：
  本文件用 **raw 三引号** + **单反斜杠 n** 的写法。
       想要 JS 里的换行   -> 写 \n     （单反斜杠）
       想要 JS 里的引号   -> 写 \"     （单反斜杠 + 半角引号）
       想让 C 代码里显示字面 CRLF -> 写 \\r\\n（双反斜杠）
  不要写成「双反斜杠 n」，那会让代码框整段挤成一行。
引号铁律：字符串内部不能出现裸的半角双引号；中文引号一律用全角 “ ”。
引导卡每一步必须是**正好 7 个元素**：标题 / 为什么 / 做什么 / 代码 / 怎么验证 / 常见问题 / 相关条目。
"""

GUIDE_P27_FILTER_PID = r"""
/* ===================== 滤波与 PID 闭环引导 ===================== */
{m:"引导", t:"guide", n:"🧭 ㉗ 数字滤波 + 卡尔曼 + PID 闭环引导（从抖动的读数到能用的控制量）",
 d:"从“读回来的数一直在跳”这个现象开始，一步一步把滤波、卡尔曼、PID 这三关搭起来。重点是讲清每一关的作用、参数调整方向，以及三处会让整条链路失灵的要点：NaN 污染递推、α 用近似式、dt 不固定。",
 g:"STM32F407 · 板2 FWLIB 的 sys_filter.h / kalman.h / pid.h（板1 没有这三个模块）· 链路：ADC → 限幅 → 低通 → 卡尔曼 → PID → PWM",
 k:"滤波 引导 教程 卡尔曼 Kalman PID 闭环 数字滤波 一阶低通 滑动平均 中值 限幅 去极值 抗积分饱和 积分分离 微分先行 调参 Q R 增益 NaN dt 固定周期 ADC PWM 千分比 超声波 板2",
 e:"/* ============================================================\n *  ㉗ 滤波 + 卡尔曼 + PID 闭环，按引导 10 步搭出来的成品\n *  链路：ADC 采样 -> 限幅挡野值 -> 一阶低通降噪 -> 卡尔曼估计 -> PID -> PWM\n *  平台：STM32F407 · sys_filter.h / kalman.h / pid.h / sys_adc.h / sys_tim.h\n * ============================================================ */\n#include \"stm32f4xx.h\"\n#include \"sys_filter.h\"\n#include \"kalman.h\"\n#include \"pid.h\"\n#include \"sys_adc.h\"\n#include \"sys_tim.h\"\n#include \"sys_tick.h\"\n#include <stdio.h>\n\n/* ---- 采样与执行器：量纲集中在这里 ---- */\n#define LOOP_ADC        ADC1\n#define LOOP_ADC_CH     5U\n#define LOOP_PWM_TIM    SYS_TIM_1\n#define LOOP_PWM_CH     1U\n#define LOOP_PERIOD_MS  10U\n\n#define RAW_STEP_MAX    200U\n#define LPF_FC_HZ       20.0f\n#define LPF_FS_HZ       1000.0f\n\n/* ---- 三个对象一律 static：递推滤波器必须有记忆 ---- */\nstatic FilterLpf_t s_lpf;\nstatic Kalman_t    s_kf;\nstatic Pid_t       s_pid;\nstatic uint16_t    s_prev_raw;\nstatic uint32_t    s_bad_cnt;\n\n/* ---- PID 浮点输出 -> PWM 千分比（0~1000）并钳位 ---- */\nstatic uint16_t out_to_permille(float out)\n{\n    float v = out;\n    if (v < 0.0f)    { v = 0.0f; }\n    if (v > 1000.0f) { v = 1000.0f; }\n    return (uint16_t)(v + 0.5f);\n}\n\n/* ---- 初始化：先对象、再限幅 ---- */\nvoid loop_init(float target)\n{\n    FILTER_LpfInit(&s_lpf, LPF_FC_HZ, LPF_FS_HZ);\n\n    (void)KALMAN_AutoTune(&s_kf, 1.0f, 8.0f);   /* 1 个单位要跟上，8 个单位要滤掉 */\n    KALMAN_Reset(&s_kf, target);\n\n    PID_Init(&s_pid, 20.0f, 5.0f, 0.5f);\n    PID_SetOutputLimit(&s_pid, 0.0f, 1000.0f);       /* 必须设 */\n    PID_SetIntegralLimit(&s_pid, -500.0f, 500.0f);\n    PID_SetDerivOnMeasurement(&s_pid, 1U);\n    PID_SetSeparateThreshold(&s_pid, 200.0f);\n}\n\n/* ---- 跑一拍：采样 -> 限幅 -> 低通 -> 卡尔曼 -> PID -> PWM ---- */\nvoid loop_step(float target, float dt)\n{\n    uint16_t raw;\n    float    z, y, out;\n\n    raw = SYS_ADC_Read(LOOP_ADC, LOOP_ADC_CH);\n\n    raw = FILTER_Limit(s_prev_raw, raw, RAW_STEP_MAX);\n    s_prev_raw = raw;\n\n    z = (float)raw;\n    if (z != z) { s_bad_cnt++; return; }\n    y = FILTER_LpfUpdate(&s_lpf, z);\n\n    y = KALMAN_Update(&s_kf, y);\n\n    out = PID_Update(&s_pid, target, y, dt);\n    SYS_TIM_PwmSetDuty(LOOP_PWM_TIM, LOOP_PWM_CH, out_to_permille(out));\n}\n\n/* ---- 固定周期主循环：dt 稳定，Ki/Kd 才有意义 ---- */\nvoid loop_task(float target)\n{\n    uint32_t last = 0U;\n\n    loop_init(target);\n\n    for (;;)\n    {\n        uint32_t t0 = SYS_TICK_GetTick();\n        if (SYS_TICK_Every(&last, LOOP_PERIOD_MS) != 0U)\n        {\n            float dt = (float)SYS_TICK_Elapsed(t0) / 1000.0f;\n            loop_step(target, (dt > 0.0f) ? dt : 0.01f);\n        }\n    }\n}\n\n/* ---- 排错：一眼看出是谁在捣乱 ---- */\nvoid loop_dump(void)\n{\n    float p, i, d;\n    PID_GetTerms(&s_pid, &p, &i, &d);\n    printf(\"K=%.3f resid=%.2f P=%.2f I=%.2f D=%.2f bad=%lu\\r\\n\",\n           KALMAN_GetGain(&s_kf), KALMAN_GetResid(&s_kf),\n           p, i, d, (unsigned long)s_bad_cnt);\n}",
 f:[
  [
   "先画链路、定频率：这一步就决定了后面所有参数",
   "滤波和 PID 的成败一大半取决于采样频率和调用周期，而不是系数。原因很直接：PID 的积分项是“误差 × dt 的累加”、微分项是“误差 / dt 的差分”，dt 一旦抖动，Ki 和 Kd 相当于每一拍都在变；一阶低通的 α 也是由 fc/fs 算出来的，多久调一次更新，就决定了 fs 到底是多少。\n\n所以画链路的时候要同时标出每一级的调用频率，而且要区分两类频率：测量通道可以比控制通道快（多采几次取平均），但控制通道必须是固定的一个周期。本项目取 10ms 控制周期，采样侧按 1kHz 算 α。",
   "在纸上（或注释里）写下三件事：① 采样通道和频率；② 滤波各级的更新频率；③ PID 的调用周期。三者不一致时，以 PID 的周期为准反推 α。",
   "/* 先把频率定死再写代码，别等调参调不出来才回来找原因。\n *\n *   控制周期  : 10ms   -> PID 的 dt = 0.01s，且必须固定\n *   采样频率  : 1kHz   -> 一阶低通的 fs = 1000Hz\n *   截止频率  : 20Hz   -> 滤掉 50Hz 工频干扰，同时不给闭环带来太多滞后\n *\n *   为什么截止频率选 20Hz 而不是 5Hz：\n *     5Hz 更平滑，但滞后更大，闭环里会吃掉相位裕度 -> 容易振荡。\n *   不用 100Hz 的原因：\n *     100Hz 几乎等于没滤，50Hz 工频会原样传进 PID。\n */\n#define LOOP_PERIOD_MS  10U      /* PID 周期：10ms，必须固定 */\n#define LPF_FC_HZ       20.0f    /* 截止频率 */\n#define LPF_FS_HZ       1000.0f  /* 采样率 */",
   "能说清“PID 每 10ms 跑一次、低通按 1kHz 采样、截止频率 20Hz”这三者的关系。\n如果算出来 fc 和 fs 很接近（比如 fc=400Hz、fs=1kHz），就要重新想，那种情况下低通的相位滞后会非常大。",
   "别在主循环里“有空就调一次 PID”。dt 抖动 20% 就足以让调好的参数在负载变化时失稳。\n别把控制周期定得比传感器响应还快。传感器本身要 100ms 才出一个新值， 1ms 跑一次 PID，微分项全是噪声。\n别把“采样频率”和“控制频率”混成一个宏，后面改了采样方式（比如改成 DMA 定时器触发）时，这两个值会分开。",
   "ADC_Init ADC_RegularChannelConfig TIM_TimeBaseInit"
  ],
  [
   "把 ADC 通道跑起来，先拿到一个能信的原始值",
   "滤波的前提是原始值本身是对的。如果 ADC 时钟、通道号、采样时间配错，拿到的是一串与真实物理量无关的数字，后面三级滤波和 PID 全都在认真地处理垃圾。\n\n本库的 ADC 封装是 `SYS_ADC_Init(adc, channel, ...)`，读取是 `SYS_ADC_Read(adc, channel)`，是两个参数（哪个 ADC、哪个通道），不是一句 `SYS_ADC_Read()`。要一次采多次可以 `SYS_ADC_ReadAvg(adc, channel, times)`，这是硬件层面的多次转换再平均，比自己写 for 循环更简单也更快。\n\n另外工程里还有 `SYS_ADC_ReadOK(raw)` 做有效性判断、`SYS_ADC_ReadMilliVolt(adc, channel)` 直接换算成毫伏。",
   "① 选一个板上真的接了的通道（对着原理图确认，别抄示例里的通道号）；② `SYS_ADC_Init` 初始化；③ 用 `SYS_ADC_Read` 循环打印几十次，看数值范围是否合理、跳动幅度有多大。",
   "/* 第 2 步：先把裸值打出来看。这一段的输出就是后面所有调参的基准。 */\n#include \"sys_adc.h\"\n#include <stdio.h>\n\n#define RAW_ADC     ADC1\n#define RAW_CH      5U\n\nvoid adc_probe(void)\n{\n    uint16_t i, raw;\n\n    SYS_ADC_Init(RAW_ADC, RAW_CH);\n\n    for (i = 0U; i < 50U; i++)\n    {\n        raw = SYS_ADC_Read(RAW_ADC, RAW_CH);   /* 两个参数：adc, channel */\n\n        if (SYS_ADC_ReadOK(raw) == 0U)         /* 值本身是否可疑 */\n        {\n            printf(\"raw = %u   (无效)\\r\\n\", (unsigned)raw);\n        }\n        else\n        {\n            printf(\"raw = %u   = %lu mV\\r\\n\", (unsigned)raw,\n                   (unsigned long)SYS_ADC_ReadMilliVolt(RAW_ADC, RAW_CH));\n        }\n        SYS_TICK_Delay_ms(10U);\n    }\n}",
   "串口打出 50 行数值。判断标准有三条：\n① 范围：12 位 ADC 满量程 4095，实测应该落在合理区间（比如光电管暗态接近 0、亮态几千），如果永远是 4095 或永远 0，先查通道和外部电路；\n② 抖动幅度：静止不动时看最大值减最小值，这个差值就是需要滤掉多少的唯一依据，它直接决定后面 Q/R 和窗口长度的量级；\n③ 单调性：用手挡住传感器，数值应该朝一个方向连续变化。",
   "`SYS_ADC_Read` 是两个参数。只写一个参数在 C 里是编译错误，但如果抄的是 kalman.h 文件头那种旧示例（写的是 `SYS_ADC_Read()`），要先对着 sys_adc.h 改过来。\n别用同一个 ADC 的两个通道交叉读而不检查冲突，`SYS_ADC_ConflictCount()` 就是给这件事准备的。\n测抖动幅度时不要开滤波，否则看到的是滤波后的结果，会把真实抖动估计小。",
   "ADC_Init ADC_RegularChannelConfig ADC_GetConversionValue ADC_SoftwareStartConv"
  ],
  [
   "第一关：限幅，把野值挡在滤波器和 PID 之外",
   "野值（spike / 尖刺）和随机噪声是两种东西，要分开处理：\n· 随机噪声：围绕真值上下小幅抖动，用平均或低通对付；\n· 野值：偶发的一两个离谱值（比如 2000 突然跳到 3800），用平均是处理不掉的，它会把均值整个拉偏，而且要把整窗数据都污染掉。\n\n专治野值的就是 `FILTER_Limit(prev, now, max_step)`：本次值和上次值相差超过 max_step，就不信任本次，保持上次值。它只有一个参数要调，而且物理意义极直观，“一秒内这个物理量不可能变这么多”。\n\n为什么必须放在最前面：卡尔曼是有记忆的最优估计，它遇到野值会按自己的增益认真地把野值当成真实跳变跟上去，然后要好几拍才能回来。先限幅等于替它把明显的错值挡在门外。",
   "① 先用第 2 步测出的“正常抖动幅度”定 max_step，一般取正常抖动的 5~10 倍；② 把 `FILTER_Limit` 接在采样和低通之间；③ 同时挂一个计数，统计一天里限幅到底拦截了多少次，拦截太频繁说明 max_step 太小。",
   "/* 第一关：限幅。max_step 按“物理上不可能变化这么快”来定。 */\n#include \"sys_filter.h\"\n\n#define RAW_STEP_MAX   200U   /* 正常抖动约 20~40 时，取 200 已经很宽松 */\n\nstatic uint16_t s_prev_raw;\nstatic uint32_t s_limit_hit;   /* 拦截次数：调参和排错都要看 */\n\nstatic uint16_t guard(uint16_t now)\n{\n    uint16_t v = FILTER_Limit(s_prev_raw, now, RAW_STEP_MAX);\n\n    if (v != now) { s_limit_hit++; }   /* 说明这次被判成野值了 */\n    s_prev_raw = v;\n    return v;\n}",
   "人为制造一次野值来验证：正常读数时用手快速挡一下再拿开，`s_limit_hit` 应该加 1，而输出值只被拖了一拍就恢复。\n如果 `s_limit_hit` 一直在涨（正常操作下也在涨），说明 max_step 定得太小，真实的快速变化被误判成野值了。",
   "max_step 定得太小会把真实的快速变化当成野值挡掉，闭环里表现为“系统反应迟钝”，而且很难查，因为现象不像 bug。\n限幅只能挡“单点野值”。连续两个野值它挡不住（第二个会被当成合法的跳变），那种场合要在后面再补一级中值滤波。\n想一次处理一批野值，用 `FILTER_Median(buf, n)`，但它会就地修改传进去的 buf，如果那个数组还要给别人用，先拷一份。",
   "GPIO_Init ADC_Init TIM_TimeBaseInit"
  ],
  [
   "第二关：一阶低通，用精确式算 α（别用教材近似式）",
   "一阶低通是“有状态”滤波里最简单的一个：`y[n] = α·x[n] + (1-α)·y[n-1]`。α 越大越信任新数据（响应快、平滑弱），越小越信任历史（平滑强、滞后大）。计算量极小、参数直观，是闭环控制里最常用的滤波。\n\n第一个问题是 α 怎么算。教材上常见 `α ≈ 2π·fc/fs`，那是泰勒展开的近似式，只在 fc 远小于 fs 时成立。本库 `FILTER_LpfInit(f, fc_hz, fs_hz)` 用的是精确式 `α = 1 - exp(-2π·fc/fs)`。差别有多大：fs=1kHz、想滤到 50Hz 时，精确 α≈0.270、近似 α≈0.314，实际截止频率会高出约 16%。做算法对比实验（或者要用理论算相位滞后）时就对不上了。\n\n第二个问题是 NaN。低通是递推的，只要有一拍喂进 NaN/Inf，之后喂多少正常值都救不回来，`y` 从此永远是 NaN。本库的处理是整拍丢弃（返回上次输出，状态完全不动）。",
   "① 用 `FILTER_LpfInit(&lpf, fc_hz, fs_hz)`，不要手算 α；② 想手工“拧旋钮”就用 `FILTER_LpfSetAlpha`（0.1 很平滑、0.3 折中、0.5 以上基本没滤）；③ 调用前自己判坏值并计数，不要把 NaN 喂进去。",
   "/* 第二关：一阶低通。α 让库按截止频率算，别自己写 2*pi*fc/fs。 */\n#include \"sys_filter.h\"\n#include <stdio.h>\n\nstatic FilterLpf_t s_lpf;\nstatic uint32_t    s_bad_cnt;     /* 坏值计数：滤波器不替我们数 */\n\nstatic float denoise(float z)\n{\n    if (z != z) { s_bad_cnt++; return 0.0f; }   /* z != z 是抓 NaN 最快的写法 */\n\n    return FILTER_LpfUpdate(&s_lpf, z);\n}\n\nvoid lpf_demo(void)\n{\n    FILTER_LpfInit(&s_lpf, 20.0f, 1000.0f);     /* 20Hz 截止、1kHz 采样 */\n\n    printf(\"alpha = %.4f\\r\\n\", (double)s_lpf.alpha);\n    /* 精确式应打印 0.1179 附近（fc=20, fs=1000），近似式会算出 0.1257 */\n}",
   "① 初始化后打印 `s_lpf.alpha`，和精确式 `1 - exp(-2π·20/1000)` ≈ 0.1179 对得上（近似式会给 0.1257）；\n② 喂一个阶跃（从一个固定值突然跳到另一个固定值），输出应该指数逼近新值，而不是一跳到位；\n③ 故意喂一次 NaN：`FILTER_LpfUpdate(&s_lpf, 0.0f/0.0f)`，输出应该保持上一拍的值不变，且 `s_bad_cnt` 能加上去。",
   "用近似式 `α ≈ 2π·fc/fs` 会让实际截止频率偏高，做对比实验时结论会错，而且不容易发现，现象只是“理论算的滞后和实测对不上”。\n把 NaN 当 0 处理是更糟的选择：那等于告诉滤波器“这次测量真的是 0”，它会把这个错误证据按权重永久写进状态。整拍丢弃才是对的。\nα 别调到 0.05 以下还指望闭环稳：滞后会吃掉相位裕度。经验上控制环里的 α 取 0.1~0.3。",
   "GPIO_Init ADC_Init TIM_TimeBaseInit"
  ],
  [
   "第三关：卡尔曼，让“平滑系数”自己动",
   "一阶低通和一维卡尔曼都是一行调用、都表现为“一个平滑系数”，区别在于 α 是谁定的：\n· 低通的 α 是写死的常数，调好之后永远用同一个力度；\n· 卡尔曼的增益 K 是每一拍自动算出来的：数据平稳时它自己把 K 压低（比手调的低通滤得更狠），数据真跳变时它自己把 K 抬高（比手调的低通跟得更快）。\n\n代价是它多了一个模型假设（过程噪声 Q、测量噪声 R），调参从“拧一个旋钮”变成“说清两件事”。不想算平方就用 `KALMAN_AutoTune(&kf, jump, smooth)`：jump 是“目标真变了多少以内要跟得上”（按传感器单位），smooth 是“多大的抖动要滤掉”，内部换算 `q = jump²`、`r = smooth²`。\n\n`Kalman_t` 里对外要的只有 `x`（当前最优估计），`p` 是估计误差协方差、`k` 是上次增益、`resid` 是上次残差。第一次调用会把测量值直接当初始估计，避免从 0 慢慢往上爬。",
   "① `KALMAN_Init(&kf, q, r)` 或 `KALMAN_AutoTune(&kf, jump, smooth)`；② 第一个测量值如果是野值（比如刚从限幅里恢复），先 `KALMAN_Reset(&kf, x0)` 把起点拉正；③ 把 `KALMAN_Update` 接在低通之后，低通已经把高频噪声压掉了，卡尔曼可以专心对付“该跟还是该稳”。",
   "/* 第三关：卡尔曼。按“手感”调，不用算协方差。 */\n#include \"kalman.h\"\n\nstatic Kalman_t s_kf;\n\nvoid kalman_setup(float first)\n{\n    /* 1 个单位的变化要跟得上，8 个单位的乱跳要滤掉 */\n    if (KALMAN_AutoTune(&s_kf, 1.0f, 8.0f) != KALMAN_OK)\n    {\n        return;   /* 参数非法（含 NaN）会返回 KALMAN_ERR_PARAM */\n    }\n\n    KALMAN_Reset(&s_kf, first);   /* x0 传 NaN/Inf 时按 0 处理 */\n}\n\nfloat kalman_step(float z)\n{\n    return KALMAN_Update(&s_kf, z);   /* 第一次调用直接把 z 当初始估计 */\n}",
   "① 初始化后连续喂同一个常数 20 次，输出应该收敛到那个常数、`KALMAN_GetGain` 逐渐降到 0.1 以下；\n② 然后突然喂一个差很多的值（比如原来 1000、现在 3000），`K` 应该在下一次读到时就冲到 0.8 以上，输出跟着大幅前移；\n③ `KALMAN_GetResid` 应该反映“测量偏哪边”的正负号，突变检测可以直接复用它。",
   "`KALMAN_Init` 的 q/r 传 NaN 会返回 `KALMAN_ERR_PARAM`，这是刻意的：普通的 `q <= 0.0f` 判断拦不住 NaN（和 NaN 的任何比较都是假），NaN 一旦进了状态就永远出不来。所以检查返回值，别把它当装饰。\n这是一维卡尔曼，只估计一个标量。加速度+位置那种二维融合它做不了，得扩成矩阵版。\n结构体声明成 static 或全局，别放栈上反复重建，重建会丢掉 `x` 和 `p`，等于每拍都从零开始，这时它退化成“直接取测量值”，一点滤波效果都没有。",
   "ADC_Init ADC_RegularChannelConfig ADC_GetConversionValue"
  ],
  [
   "看 K 值和残差来调参，而不是凭感觉拧 Q/R",
   "调参如果只盯着“输出曲线好不好看”，很容易陷入“两个方向都试过、都不满意”。更好的办法是盯内部量：K 是卡尔曼自己算出来的“这一拍它有多信新数据”，它把滤波器的状态直接暴露给了。\n\n判读规则（本库文件头也写了同一张表）：\n· K 稳定在 0.0x → 滤得太狠，跳变时跟不上 → 把 Q 调大或 R 调小；\n· K 稳定在 0.5 以上 → 基本等于没滤 → 把 R 调大或 Q 调小；\n· 理想状态：平稳时 K 慢慢降到 0.1 附近，真跳变时瞬间冲到 0.8 以上。\n\nQ 和 R 的单位是“物理量的平方”，硬算很劝退。方向就行：Q 调大 = 更信测量 = 跟得快、曲线毛；R 调大 = 更信模型 = 更平滑、反应慢。想在线切换手感（静止时滤狠点、运动时跟快点）就调 `KALMAN_SetQR`，它只改 Q/R、不动状态。",
   "① 串口每 100ms 打一行 `K / resid / 输出值`；② 让被测量保持不动 30 秒，观察 K 的稳态值；③ 人为拉偏一次，观察 K 的峰值；④ 按上面的规则只改一个参数（Q 或 R），再重复。",
   "/* 调参仪表盘：把卡尔曼的内部量打出来，比看输出曲线快得多。 */\n#include \"kalman.h\"\n#include <stdio.h>\n\nstatic Kalman_t s_kf;\n\nvoid kalman_dashboard(float z)\n{\n    float y = KALMAN_Update(&s_kf, z);\n    float k = KALMAN_GetGain(&s_kf);\n\n    printf(\"z=%.1f y=%.1f K=%.3f resid=%.2f\\r\\n\",\n           (double)z, (double)y, (double)k, (double)KALMAN_GetResid(&s_kf));\n\n    /*\n     * 典型判读：\n     *   K 长期 0.03  -> 太平滑，跳变跟不上 -> KALMAN_SetQR(&kf, 0.05f, 4.0f)\n     *   K 长期 0.60  -> 等于没滤           -> KALMAN_SetQR(&kf, 0.01f, 16.0f)\n     *   平稳 0.1 / 跳变 0.8  -> 已经调好，别再动\n     */\n}",
   "静止 30 秒后串口里 K 应该稳定在一个小的值（0.05~0.15 之间比较理想）；用手拉偏时那一行的 K 应该明显跳高。\n如果 K 一直不动（比如启动后永远是 1.000），说明初始 `p` 或者参数设成了极端值，检查有没有漏调 `KALMAN_Init`/`AutoTune`。",
   "别同时改 Q 和 R，两个都在动，就分不清是谁的效果了。一次只改一个，改完至少观察 30 秒。\n别用输出曲线“看起来平滑”当唯一标准。过度平滑往往意味着跳变时跟不上，而在闭环里这直接表现为响应慢、超调大。\n`KALMAN_MIN_P = 0.000001f` 是协方差下限（防止 p 算到 0 之后除不开），它是内部保护，不要为了让 K 变小而去动它。",
   "ADC_Init ADC_RegularChannelConfig ADC_GetConversionValue"
  ],
  [
   "第四关：PID 上电第一件事是设输出限幅",
   "PID 的职责是“拿目标值和实测值的差算控制量”，但它不知道执行器能接受多大。`PID_Init` 之后默认的输出限幅只有 `±100.0f`（`PID_DEFAULT_OUT_MIN/MAX`），而实际执行器往往是别的量纲，PWM 占空比是千分比 0~1000、DAC 是 0~4095、阀门可能是 0~100%。\n\n量纲不匹配的后果并非“控制效果差”，而是上电瞬间满输出：PID 看到误差很大，直接给一个远超执行器上限的值，执行器满功率冲出去。所以 `PID_SetOutputLimit` 必须在 `PID_Init` 之后立刻调用，别等调参时再说。\n\n还有一件容易踩的事：`SYS_TIM_PwmSetDuty(id, ch, permille)` 收的是千分比 0~1000 的整数，不是 0.0~1.0 的浮点占空比。PID 算出浮点控制量后要自己换算并钳位，这个换算函数应该单独写一个，别散落在各处。",
   "① `PID_Init` 给一组初值（先给 0 也行，后面 `PID_SetTunings` 再调）；② 紧接着 `PID_SetOutputLimit`；③ 写一个 `out_to_permille()` 负责浮点转千分比并钳位；④ 位置式直接用输出，增量式要输出是“增量”。",
   "/* 第四关：PID 初始化 + 量纲换算。限幅必须在 Init 之后马上设。 */\n#include \"pid.h\"\n#include \"sys_tim.h\"\n\n#define PWM_TIM   SYS_TIM_1\n#define PWM_CH    1U\n\nstatic Pid_t s_pid;\n\nstatic uint16_t out_to_permille(float out)\n{\n    float v = out;\n\n    if (v < 0.0f)    { v = 0.0f; }        /* 单向执行器：负输出没有意义 */\n    if (v > 1000.0f) { v = 1000.0f; }\n\n    return (uint16_t)(v + 0.5f);          /* 四舍五入，不要直接截断 */\n}\n\nvoid pid_setup(void)\n{\n    PID_Init(&s_pid, 20.0f, 5.0f, 0.5f);\n\n    /* 上电第一件事：告诉 PID 执行器的真实量纲 */\n    PID_SetOutputLimit(&s_pid, 0.0f, 1000.0f);\n\n    PID_SetMode(&s_pid, PID_MODE_POSITION);   /* 位置式：输出就是绝对占空比 */\n}\n\nvoid pid_run(float target, float measure, float dt)\n{\n    float out = PID_Update(&s_pid, target, measure, dt);\n\n    SYS_TIM_PwmSetDuty(PWM_TIM, PWM_CH, out_to_permille(out));\n}",
   "① 把输出限幅故意设成 `0.0f`，观察执行器应该完全没反应（证明限幅真的生效）；\n② 恢复 0~1000 后，让误差很大，输出应该正好停在 1000，不会超过；\n③ 打印 `PID_GetOutput(&s_pid)` 和 `s_pid.p_term / i_term / d_term`，确认量纲和预期的一致。",
   "忘了设输出限幅是“上电就满输出”的头号原因，而且现象像硬件故障（电机猛冲、阀门到底）。\n`SYS_TIM_PwmSetDuty` 收千分比整数，不是 0.0~1.0 的 float。把 0.5 直接传进去等于 0.5‰、几乎为零输出，很容易误判成“PWM 没配好”。\npid.h 文件头示例里 `PID_Update(&s_pid, 100.0f, ...)` 只传了 3 个参数，那是旧版写法；实际签名是 4 个参数（多一个 dt），照搬会编译不过。",
   "TIM_OC1Init TIM_SetCompare1 TIM_Cmd TIM_TimeBaseInit"
  ],
  [
   "抗积分饱和三件套：少一个就会大幅超调",
   "积分项是“误差的累计”，只要误差一直存在，它就一直涨。执行器已经到极限了、积分还在涨，这就是积分饱和。等误差反号时，得先把这一大坨积分“吐”回去系统才肯回头，表现为大幅超调和来回冲。\n\n本模块把三种常用手段都给了，实际工程里通常至少用前两个：\n· 积分限幅 `PID_SetIntegralLimit(pid, min, max)`：把积分累计夹在范围内。经验取值是输出限幅的 50%~100%；设得太小会削弱消静差的能力。\n· 积分分离 `PID_SetSeparateThreshold(pid, threshold)`：误差绝对值比阈值大时暂停积分。起步阶段误差本来就大，这一步能显著压掉超调；传 0 = 关闭该功能。\n· 微分先行 `PID_SetDerivOnMeasurement(pid, 1)`：微分只对测量值求导，不动设定值。上位机改目标时不会产生微分尖峰。\n\n另外 `PID_SetDeadZone(pid, dz)` 管的是另一件事：误差绝对值小于死区就按 0 处理，专治带死区的电机和阀门那种“在目标附近来回抖”。",
   "① 先设积分限幅（取输出限幅的 50%~100%）；② 再设积分分离阈值（约为满量程误差的 10%~20%）；③ 设定值会被上位机改动的场合，打开微分先行；④ 用 `PID_GetTerms` 观察三个分量，确认稳态时 I 是一个不动的常数。",
   "/* 抗积分饱和：限幅 + 分离 + 微分先行，三个一起上。 */\n#include \"pid.h\"\n#include <stdio.h>\n\nstatic Pid_t s_pid;\n\nvoid pid_anti_windup(void)\n{\n    /* ① 积分限幅：取输出限幅的 50%~100% */\n    PID_SetIntegralLimit(&s_pid, -500.0f, 500.0f);\n\n    /* ② 积分分离：误差还很大时先不积分，专治起步超调 */\n    PID_SetSeparateThreshold(&s_pid, 200.0f);   /* 0 表示关闭该功能 */\n\n    /* ③ 微分先行：设定值突变不产生微分尖峰 */\n    PID_SetDerivOnMeasurement(&s_pid, 1U);\n}\n\nvoid pid_watch(void)\n{\n    float p, i, d;\n\n    PID_GetTerms(&s_pid, &p, &i, &d);\n\n    /* 稳态时：P 接近 0、I 是一个不动的常数、D 接近 0。\n     *   I 一直在涨           -> 积分饱和（查上面三条有没有设上）\n     *   D 一直在正负乱跳     -> 传感器噪声没滤干净，先回去加滤波\n     *   P 一直很大且反号振荡 -> Kp 太大\n     */\n    printf(\"P=%.2f I=%.2f D=%.2f\\r\\n\", (double)p, (double)i, (double)d);\n}",
   "故意做一次“大目标阶跃”测试：把目标值从 0 突然改到满量程。\n· 关掉积分限幅和积分分离时，会看到输出冲到上限后卡在那里很久，然后回来时冲过头；\n· 打开之后，同样阶跃下超调明显变小、I 分量在接近目标时才开始累积。\n这就是“抗积分饱和到底有没有用”最直接的证据。",
   "只设积分限幅、不设积分分离，遇到大阶跃仍然会有明显超调，两者治的不是同一个阶段（限幅管“累计太多”，分离管“起步不该积”）。\n`PID_SetSeparateThreshold` 传 0 是关闭功能，不是“阈值为 0”。想让它生效必须给一个正数。\n微分对噪声非常敏感。先确认第 3 步的滤波真的在工作，再开微分项，否则 D 分量只会放大噪声。",
   "TIM_OC1Init TIM_SetCompare1 TIM_Cmd"
  ],
  [
   "固定 dt：把闭环放进固定周期，再谈整定",
   "`PID_Update(pid, setpoint, measure, dt)` 的第 4 个参数是距上次调用的时间（秒）。传 0 时按 0.01s 即 10ms 处理，但那只是兜底。\n\n为什么 dt 必须固定：PID 的积分项按 `误差 × dt` 累加、微分项按 `误差变化 / dt` 计算。dt 抖动等于 Ki 和 Kd 每一拍都在变，按 dt=10ms 调好的参数，一旦主循环因为别的事情慢了 5ms，等效 Ki 就偏了 50%。所以“调参调不出来”里有一大半是 dt 不固定造成的。\n\n固定周期有三个现成来源：`SYS_TIM_InitIT(id, freq_hz, callback)`（定时器中断回调，最稳）、`SYS_TICK_Every(&last, 10)`（毫秒节拍版，够用且不占定时器）、`SYS_TICK_Timeout` + `SYS_TICK_Elapsed`（手工算 dt）。\n\n搭好固定周期之后，整定按经典顺序走：先把 Ki、Kd 置 0，只加 Kp 到“开始小幅振荡”，再回退到 60~70%；然后加 Kd 压振荡；最后加 Ki 消静差，Ki 要小。",
   "① 用 `SYS_TICK_Every` 或定时器中断把 `loop_step` 绑到固定周期；② 把真实 dt 用 `SYS_TICK_Elapsed` 算出来传进去（比写死 0.01f 更能反映真实抖动，也能帮发现抖动有多大）；③ 按 P → D → I 的顺序整定，每步都用 `PID_GetTerms` 看分量。",
   "/* 固定周期 + 真实 dt + 接进 FreeRTOS 任务。 */\n#include \"pid.h\"\n#include \"sys_tick.h\"\n#include \"FreeRTOS.h\"\n#include \"task.h\"\n\n#define LOOP_PERIOD_MS  10U\n\nvoid loop_task(void *arg)\n{\n    TickType_t last = xTaskGetTickCount();\n\n    (void)arg;\n\n    for (;;)\n    {\n        uint32_t t0 = SYS_TICK_GetTick();\n        float    dt;\n\n        /* ① 用 RTOS 的周期延时保证 10ms 一次，从根上消灭 dt 抖动 */\n        vTaskDelayUntil(&last, pdMS_TO_TICKS(LOOP_PERIOD_MS));\n\n        /* ② 把真实经过的时间算出来（正常情况下就是 0.010） */\n        dt = (float)SYS_TICK_Elapsed(t0) / 1000.0f;\n        if ((dt <= 0.0f) || (dt > 0.1f)) { dt = 0.01f; }   /* 明显异常就用标称值 */\n\n        loop_step(target_value(), dt);\n    }\n}",
   "① 把 dt 打印出来，正常情况下应该稳定在 0.010 附近；如果经常看到 0.013、0.017 这种值，说明有别的东西在拖慢任务，先解决它再调参；\n② 整定 P 的时候，Kp 从小到大加，输出应该从“几乎不动”变成“小幅振荡”；\n③ 最后 `PID_GetTerms` 稳态下 I 是常数、P≈0、D≈0，就是调好了。",
   "别在主循环里“有空就调一次”。哪怕平均周期是对的，抖动也会让参数在负载变化时失稳。\n别把 dt 写死成 0.01f 又用不固定周期调用，那样 PID 以为自己在 10ms 跑，实际不是，等效系数全错。\nFreeRTOS 里用 `vTaskDelay` 而不是 `vTaskDelayUntil` 会累积漂移（每次延时都从“现在”重新算），周期越长漂得越多；固定周期任务要用 `vTaskDelayUntil`。",
   "vTaskDelay vTaskDelayUntil TIM_ITConfig TIM_GetITStatus TIM_ClearITPendingBit"
  ],
  [
   "落地清单：谁在算、谁在写、两板差在哪、怎么排错",
   "最后一步不写新逻辑，只做四件事：确认链路顺序、确认两板差异、接上真实传感器、把所有中间量做成可观测的。\n\n链路顺序（这一步最关键，顺序错了后面全是白费）：传感器 → 限幅（挡野值）→ 低通/平均（降噪声）→ 卡尔曼（估计）→ PID（控制）→ 执行器。限幅必须在滤波器之前，否则野值会污染滤波器状态；卡尔曼在 PID 之前，因为 PID 要的是尽量准的实测值。\n\n两板差异：`sys_filter.c`、`kalman.c`、`pid.c`、`hcsr04.c` 只有板2 FWLIB 里有，板1 一个都没有。而且两板 `main.c` 目前都还没调用这几个模块，所以把它加进工程时，现有程序行为不会改变，这也是一个安全信号：出问题时一定是新加的代码。\n\n真实传感器：本工程里最适合当这套算法输入的是 HCSR04 超声波，测距天然带野值、天然需要滤波。它的量程是 2~400cm，没等到回波返回 `HCSR04_ERR_NO_ECHO`、超出量程返回 `HCSR04_ERR_RANGE`（两种错误要分开处理），还有现成的 `HCSR04_ReadCmMedian(cm, times)` 做中值滤波，times 最多 9 次。",
   "① 把链路顺序写进注释钉死；② 确认工程里只有板2 有这四个 .c（板1 编译时不要引用）；③ 用一个可观测的输入源（HCSR04 或电位器）跑通全链路；④ 写一个 `loop_dump()` 把 K、resid、P/I/D、坏值计数一起打出来。",
   "/* 全链路 + 可观测性：出问题时先看这一行输出。 */\n#include \"sys_filter.h\"\n#include \"kalman.h\"\n#include \"pid.h\"\n#include \"hcsr04.h\"\n#include \"sys_adc.h\"\n#include <stdio.h>\n\nvoid loop_dump(void)\n{\n    float p, i, d;\n\n    PID_GetTerms(&s_pid, &p, &i, &d);\n\n    printf(\"K=%.3f resid=%.2f | P=%.2f I=%.2f D=%.2f | bad=%lu\\r\\n\",\n           (double)KALMAN_GetGain(&s_kf), (double)KALMAN_GetResid(&s_kf),\n           (double)p, (double)i, (double)d, (unsigned long)s_bad_cnt);\n}\n\n/* 超声波当输入源时的典型处理：先中值再限幅，两种错误分别处理 */\nfloat distance_step(float target)\n{\n    uint16_t cm = 0U;\n    uint8_t  rc = HCSR04_ReadCmMedian(&cm, 5U);\n\n    if (rc == HCSR04_ERR_NO_ECHO) { return KALMAN_GetValue(&s_kf); }  /* 没回波：沿用上次 */\n    if (rc == HCSR04_ERR_RANGE)   { return KALMAN_GetValue(&s_kf); }  /* 超量程：同上 */\n    if (rc != HCSR04_OK)          { return KALMAN_GetValue(&s_kf); }\n\n    return KALMAN_Update(&s_kf, (float)cm);\n}",
   "① 断点或串口确认调用顺序真的是“限幅 → 低通 → 卡尔曼 → PID”；\n② 板1 编译应该照样 0 Error 0 Warning（因为它没有引用这四个模块）；\n③ `loop_dump()` 那一行能同时看到卡尔曼和 PID 的内部状态，这就是以后查任何“控制不对”问题的第一现场。",
   "顺序错了最典型的两种：先滤波再限幅（野值污染滤波器状态，要好几拍才恢复）、先 PID 再滤波（PID 拿噪声去算微分，输出乱跳）。\n板1 里 `#include \"kalman.h\"` 会直接报找不到头文件，这四个模块是板2 独有的，写跨板代码时要靠条件编译隔开。\n`HCSR04_ERR_NO_ECHO`（没回波）和 `HCSR04_ERR_RANGE`（有回波但超量程）不是一回事：前者可能是目标太远或角度偏，后者说明物体在 2cm 以内或真的超 400cm。都当“读失败”处理会掩盖真实情况。",
   "vTaskDelay vTaskDelayUntil ADC_Init ADC_RegularChannelConfig ADC_GetConversionValue"
  ]
 ]
}
"""
