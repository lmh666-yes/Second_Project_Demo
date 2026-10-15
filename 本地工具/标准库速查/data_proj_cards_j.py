# -*- coding: utf-8 -*-
r"""data_proj_cards_j.py —— ㉗ 数字滤波 + 卡尔曼 + PID 闭环（流程卡）。

写法约定（与 data_proj_cards.py 一致）：
  本文件用 **raw 三引号** + **单反斜杠 n** 的写法。
       想要 JS 里的换行   -> 写 \n     （单反斜杠）
       想要 JS 里的引号   -> 写 \"     （单反斜杠 + 半角引号）
       想让 C 代码里显示字面 CRLF -> 写 \\r\\n（双反斜杠）
  不要写成「双反斜杠 n」，那会让代码框整段挤成一行。
引号铁律：字符串内部不能出现裸的半角双引号；中文引号一律用全角 “ ”。
"""

FLOW_P27_FILTER_PID = r"""
/* ===================== ㉗ 数字滤波 + 卡尔曼 + PID 闭环 ===================== */
{m:"流程", t:"flow", n:"㉗ 数字滤波 + 卡尔曼 + PID 闭环（把抖动的读数变成可信的控制量）",
 d:"传感器读回来的数一定在抖，执行器又必须收到一个稳的指令。中间这条链路要过三关：先用滤波挡掉野值、再用卡尔曼做状态估计、最后用 PID 算出控制量。本卡把这三关拆开讲，它们各自解决什么问题、参数怎么调、以及最容易埋雷的那几处（NaN 污染递推、α 用近似式、dt 不固定、积分饱和）。",
 f:[
  ["先分清三件事：滤波、估计、控制",
   "这三件事经常被混成一个“加个滤波就好了”，实际职责完全不同：\n· 滤波（sys_filter）：就地压掉噪声和野值，输入输出都是同一个物理量，没有“模型”概念。\n· 状态估计（kalman）：带一个“上一拍的最优估计”，每拍自己算该信新数据还是信历史。\n· 控制（pid）：拿目标值和实测值的差，算出该给执行器多少量。\n\n为什么必须分开想：滤波器会引入相位滞后，而滞后加在控制环里是会吃掉相位裕度、把系统推向振荡的。所以“测量通道上可以滤得狠一点，反馈通道上必须滤得轻一点”，这句话是整条链路的设计前提。",
   "先画一张数据流图：传感器 → 滤波 → 估计 → PID → 执行器，标出每一级的调用频率，再动手写代码"],
  ["四种无状态滤波：先拿它们挡野值",
   "sys_filter.h 里前五个函数都是无状态的，不记得历史，喂什么它算什么，适合放在采样最前面当“守门员”：\n· `FILTER_Limit(prev, now, max_step)`：本次值比上次跳得超过 max_step 就不信任本次，保持上次值。专治偶发尖刺。\n· `FILTER_Median(buf, n)`：排序取中位数。专治孤立野值，而且不像平均那样把阶跃拖缓。它会就地修改 buf。\n· `FILTER_MovingAvg(buf, n)`：算术平均，不修改 buf。n 越大越平滑、越滞后。\n· `FILTER_TrimmedMean(buf, n, trim)`：去掉最大 trim 个和最小 trim 个再平均，比赛常用。\n· `FILTER_MinMax(buf, n, &min, &max)`：只做极值统计，不滤波，调参时用它看抖动幅度。\n\n工程顺序是：先限幅、再中值/平均、最后才进卡尔曼或 PID。若先把野值喂进卡尔曼，滤波会把野值当作真实跳变跟随。",
   "中值/滑动平均/去极值平均都要自备数组，长度开到 FILTER_BUF_MAX（32）就够，建议实际用 4~16 且取 2 的幂"],
  ["一阶低通：一行实现，但 α 有两个问题",
   "低通是“有状态”里最简单的一个：`y[n] = α·x[n] + (1-α)·y[n-1]`。α 越大越信新数据。\n\n问题一：别用教材上的近似式。 教材常写 α ≈ 2π·fc/fs，那是泰勒展开的近似，只在 fc 远小于 fs 时成立。本库 `FILTER_LpfInit(f, fc_hz, fs_hz)` 用的是精确式 `α = 1 - exp(-2π·fc/fs)`。差别有多大：fs=1kHz 想滤到 50Hz 时，精确 α≈0.270、近似 α≈0.314，实际截止频率会高出约 16%，做算法对比实验时就对不上了。\n\n问题二：NaN 会永久污染。 `FILTER_LpfUpdate` 是递推的，只要有一拍喂进 NaN/Inf，之后喂多少正常值都救不回来。本库的做法是整拍丢弃（返回上次输出，状态不动），既不污染，也不把坏值当成 0（当成 0 会把估计值硬拉一步，而且这步永久留在状态里）。想把坏值计个数，调用方自己先判 `if (raw != raw) { fail++; }`。",
   "想“拧一个 0.0~1.0 的旋钮”就直接用 FILTER_LpfSetAlpha：0.1 很平滑、0.3 折中、0.5 以上基本没滤"],
  ["滑动平均的 O(n) 和 O(1)：做对比实验要用环形缓冲版",
   "`FILTER_MovingAvg(buf, n)` 是无状态的：每次调用都要把整个窗口重新求和（O(n)），窗口内容、该覆盖哪一个，全由调用方自己维护，写起来啰嗦，而且最容易错的就是回绕那一步（忘了丢掉最老的样本，曲线会越走越沉，看起来像滤波器突然变迟钝）。\n\n区块 4 给了一个有状态的环形缓冲版本：内部维护 running sum，每来一个新样本只做“减掉被挤出的最老的、加上新来的”，单步开销与窗口长度无关（O(1)）。\n\n为什么这件事重要：卡尔曼和一阶低通都是 O(1) 递推。如果拿无状态的 O(n) 滑动平均去和它们比耗时，结论是不公平的，窗口越长，滑动平均越吃亏。做“哪种滤波更省 CPU”的实验时，必须用 `FILTER_AvgUpdate` 那一版，大家口径才一致。\n\n还有一个细节：窗口没攒满时的预热期行为。本库返回的是“已有样本的平均值”，而不是把空缺当 0，否则上电后前 7 拍读数会明显偏小。",
   "FILTER_AvgInit(&avg, 8) 传 0 会用默认值 8，传超过 FILTER_BUF_MAX 会自动截到上限，不会越界"],
  ["卡尔曼：它比一阶低通到底强在哪",
   "两者都是一行调用、都有一个“平滑系数”，区别在于 α 是谁定的：\n· 一阶低通的 α 是写死的常数，调好之后，它就永远用同一个力度平滑。\n· 卡尔曼的增益 K 是每一拍自动算出来的：数据平稳时它自己把 K 压低（比手调的低通滤得更狠），数据真跳变时它自己把 K 抬高（比手调的低通跟得更快）。\n\n代价是它多了一个“模型”的假设（过程噪声 Q、测量噪声 R），所以调参从“拧一个旋钮”变成“说清两件事”。结构体里的 `x` 是状态估计（对外要的就是它）、`p` 是估计误差协方差、`k` 是上次算出的增益、`resid` 是上次残差（测量值减估计值，突变检测可以复用）。\n\n`KALMAN_Update` 第一次调用会把测量值直接当初始估计，避免从 0 慢慢往上爬，但如果第一个测量值本身就是野值，这个野值会成为起点，必要时先 Reset。",
   "卡尔曼是“一维”的：只估计一个标量。加速度+位置那种二维融合它做不了，得扩成矩阵版"],
  ["卡尔曼调参只要盯一个 K",
   "Q 和 R 的单位都是“物理量的平方”，硬算很容易劝退。\n· Q 调大 → 更信测量 → 跟得快、曲线毛；\n· R 调大 → 更信模型 → 更平滑、反应慢。\n\n不想算平方就用 `KALMAN_AutoTune(&kf, jump, smooth)`，按“手感”说需求：jump 是“目标真变了多少以内要跟得上”（按传感器单位，比如 1.0℃），smooth 是“多大的抖动要滤掉”（比如 8℃ 的乱跳）。它内部换算 `q = jump²`、`r = smooth²`。本项目默认 `KALMAN_DEFAULT_Q = 0.01f`、`KALMAN_DEFAULT_R = 4.00f`。\n\n真正的调参仪表是 K 值（`KALMAN_GetGain`）：\n· K 稳定在 0.0x → 滤得太狠，跳变时跟不上，把 Q 调大或 R 调小；\n· K 稳定在 0.5 以上 → 基本等于没滤，把 R 调大或 Q 调小；\n· 理想状态 → 平稳时 K 慢慢降到 0.1 附近，真跳变时瞬间冲到 0.8 以上。\n\n另外 `KALMAN_MIN_P = 0.000001f` 是协方差下限，防止 p 算到 0 之后除不开；`p` 是内部量，正常不该手改。",
   "在线调参用 KALMAN_SetQR，比如“静止时滤狠点、运动时跟快点”，只改 Q/R 不动状态"],
  ["坏值必须整拍丢弃，不能当 0",
   "这是整条链路里最隐蔽的一类 bug。卡尔曼和一阶低通都是带记忆的递推：`x` 会传下去。只要有一拍是 NaN 或 Inf，它就永远是 NaN/Inf，之后往后再喂多少正常数据都救不回来（除非重新 Init/Reset）。\n\n为什么“把坏值当 0”更糟：那等于告诉滤波器“这次测量真的是 0”，它会把这个错误证据按自己的增益权重永久写进状态里。少滤一拍只是这一次不准，写进状态是往后很多拍都不准。\n\n本库对这件事是显式检查的，不是随手写个 `if (q <= 0.0f)`：和 NaN 的任何比较结果都是假，所以 `q <= 0.0f` 拦不住 NaN 传进来，K 会算成 NaN，两个钳位判断同样拦不住，`x` 从此永远是 NaN。所以本库用 `kalman_is_finite` 这一类显式判据，`KALMAN_Init` 的 q/r 传 NaN 会返回 `KALMAN_ERR_PARAM`，`KALMAN_Reset` 的 x0 传 NaN/Inf 会按 0 处理。\n\n调用方该做什么：自己判、自己计数。滤波器不替做这个决定（它分不清“没读到”和“读到 0”）。",
   "打印调试时用 `if (z != z)` 抓 NaN（唯一一个自己不等于自己的值），比逐个 isfinite 判断快"],
  ["PID：位置式还是增量式",
   "模块把状态和参数打包进 `Pid_t`，所以能同时跑多个闭环（两轮小车的左右轮各一个 PID），互不干扰，这是它比“全局变量式 PID 函数”实用的地方。\n\n· 位置式（`PID_MODE_POSITION`）：输出 = 绝对控制量，直接赋给 PWM 占空比。有积分、稳态无静差，但积分饱和需要抗。\n· 增量式（`PID_MODE_INCREMENT`）：输出 = 控制量的增量，靠累加得到绝对量。天然抗积分饱和、切换无扰动，适合执行器自带记忆的场景。\n\n怎么选：舵机 / 阀门 / 温度 / 位置环 → 位置式；直流电机速度环 → 增量式也很常见。\n\n上电第一件事是 `PID_SetOutputLimit`，不设的话默认只有 `±100.0f`，一旦执行器量纲是“占空比千分比 0~1000”，上电瞬间就是满输出。",
   "切换模式或从手动切自动前先 PID_Reset / PID_PresetOutput，避免残留积分造成“抽一下”"],
  ["抗积分饱和三件套，少一个就等着超调",
   "积分项是“误差的累计”，只要误差一直在，它就一直涨。执行器已经到极限了积分还在涨，这就是积分饱和，等误差反号时，得先把这一大坨积分“吐”回去，系统才肯回头，表现就是大幅超调和来回冲。本模块三招都给了：\n· 积分分离 `PID_SetSeparateThreshold`：误差比阈值大时先不积分。起步阶段误差本来就大，这一步能显著压掉超调；0 = 关闭该功能。\n· 积分限幅 `PID_SetIntegralLimit`：把积分累计夹在范围内。经验取值是输出限幅的 50%~100%；设得太小会削弱消静差能力。\n· 微分先行 `PID_SetDerivOnMeasurement(pid, 1)`：微分只对测量值求导，不动设定值。设定值突变的场合（比如上位机改目标）不会有微分尖峰。\n\n另外还有 `PID_SetDeadZone`：误差绝对值小于死区就按 0 处理，专治带死区的电机和阀门那种“在目标附近来回抖”。",
   "调参顺序：先把 Ki、Kd 置 0，只加 Kp 到“开始小幅振荡”，再回退到 60~70%；加 Kd 压振荡；最后加 Ki 消静差，Ki 要小"],
  ["dt 必须固定，否则 Ki、Kd 全跑偏",
   "`PID_Update(pid, setpoint, measure, dt)` 的第 4 个参数是距上次调用的时间（秒）。传 0 时按 0.01s 即 10ms 处理，但那只是兜底，采样周期抖动会让 Ki/Kd 的效果完全跑偏：dt 忽大忽小，等于积分和微分系数每一拍都在变。\n\n所以不要在主循环里“有空就调一次”，要用固定周期来源：\n· `SYS_TIM_InitIT(id, freq_hz, callback)`，定时器中断回调，频率最稳；\n· `SYS_TICK_Every(&last, 10)`，毫秒节拍版，够用且不用占定时器；\n· `SYS_TICK_Timeout(start, 10)` + `SYS_TICK_Elapsed(start)`，手工算 dt。\n\n调试期把三个分量打出来看（`PID_GetTerms(&pid, &p, &i, &d)`），一眼就能看出是谁在使劲：P 主导是振荡、I 一直涨是饱和、D 很毛是噪声没滤干净。",
   "pid.h 文件头示例里 `PID_Update(&s_pid, 100.0f, ...)` 只传了 3 个参数，是旧版写法，实际为 4 个参数，不可照用"],
  ["落地清单：谁在算、谁在写、两板差异在哪",
   "① 算：滤波和 PID 都是纯算法模块，不访问寄存器，换芯片可直接复用；\n② 喂：采样用 `SYS_ADC_Read(adc, channel)`（两句参数，不是一句）或 `SYS_ADC_ReadAvg(adc, channel, times)`；要固定速率就用 `SYS_ADC_DmaTimerTrigInit` 让定时器触发；\n③ 出：`SYS_TIM_PwmSetDuty(id, ch, permille)` 收的是千分比 0~1000，不是 0.0~1.0 的浮点占空比，PID 算出浮点控制量后要自己换算并钳位；\n④ 两板差异：`sys_filter.c` / `kalman.c` / `pid.c` / `hcsr04.c` 只有板2 FWLIB 里有，板1 一个都没有；两板 `main.c` 目前都还没调用这四个模块，所以加进工程不会改变现有程序行为；\n⑤ 典型输入：HCSR04 超声波测距（`HCSR04_ReadCmMedian(cm, times)`，times 最多 `HCSR04_MEDIAN_MAX` 即 9 次）正好是这些滤波的用武之地，量程 2~400cm，没等到回波返回 `HCSR04_ERR_NO_ECHO`，超出量程返回 `HCSR04_ERR_RANGE`。",
   "引脚复用要留意：kalman.h / pid.h 示例里的 SYS_ADC、SYS_TIM 是示意，真接线的引脚以工程里 sys_adc.h / sys_tim.h 的板级配置为准"]
 ],
 a:[
  ["模块位置","板2 FWLIB：sys_filter.c(15KB) / kalman.c(9.9KB) / pid.c(8KB) / hcsr04.c(8.5KB)","板1 FWLIB 里这四个文件都不存在，是板2 独有"],
  ["依赖","无，四个模块都不碰外设，只用 float 和整数运算","纯算法，可以单独拷到任何工程里用"],
  ["返回码约定","全库统一 0 = 成功","KALMAN_OK 0 / KALMAN_ERR_PARAM 1"],
  ["HCSR04 返回码","OK 0 / ERR_PARAM 1 / ERR_NO_ECHO 2 / ERR_RANGE 3 / ERR_NOT_INIT 4","没回波和超量程是两种错误，别混成一个"],
  ["滤波缓冲上限","FILTER_BUF_MAX = 32","中值/滑动平均/去极值平均的数组开到这么大就够"],
  ["滑动平均默认窗口","FILTER_AVG_DEFAULT_SIZE = 8","取 2 的幂是因为除法最快；传 0 用默认、超上限自动截断"],
  ["一阶低通 α","精确式 α = 1 - exp(-2π·fc/fs)","教材的 α ≈ 2π·fc/fs 是近似：fs=1kHz、fc=50Hz 时实际截止会高约 16%"],
  ["α 手感","0.1 很平滑 / 0.3 折中 / 0.5 以上基本没滤","闭环控制环上的 α 别调过小（0.1~0.3），滞后会吃掉相位裕度"],
  ["卡尔曼默认参数","Q = 0.01f，R = 4.00f，初始 P = 1.0f","KALMAN_MIN_P = 0.000001f 是协方差下限，防止算到 0 除不开"],
  ["卡尔曼看 K 调参","平稳时降到 ~0.1、真跳变冲到 0.8+ 为理想","K 稳定在 0.0x = 滤太狠；K 稳定在 0.5+ = 等于没滤"],
  ["坏值策略","NaN / ±Inf 整拍丢弃，不当 0、不污染状态","递推带记忆，一拍 NaN 就永久坏，只能 Init/Reset 救"],
  ["PID 默认输出限幅","PID_DEFAULT_OUT_MIN -100.0f / MAX +100.0f","几乎总要改；不设就是上电满输出"],
  ["PID 两模式","PID_MODE_POSITION 0 / PID_MODE_INCREMENT 1","位置式用于舵机/阀门/温度，增量式用于电机速度环"],
  ["PID 的 dt","PID_Update 第 4 个参数，单位秒；传 0 按 0.01s 处理","周期必须固定，抖动会让 Ki/Kd 效果跑偏"],
  ["PWM 占空比量纲","SYS_TIM_PwmSetDuty(id, ch, permille)，千分比 0~1000","不是 0.0~1.0 的浮点，PID 输出要自己换算并钳位"],
  ["超声波量程","2~400cm；声速默认 343400 mm/s（20℃ 干空气）","可用 HCSR04_SetTempC10 按实测温度修正声速"]
 ],
 e:"/* ============================================================\n *  ㉗ 数字滤波 + 卡尔曼 + PID 闭环，完整可用代码\n *  链路：ADC 采样 -> 限幅挡野值 -> 一阶低通降噪 -> 卡尔曼估计 -> PID -> PWM\n *  平台：STM32F407 · 板2 FWLIB · sys_filter.h / kalman.h / pid.h\n * ============================================================ */\n#include \"stm32f4xx.h\"\n#include \"sys_filter.h\"\n#include \"kalman.h\"\n#include \"pid.h\"\n#include \"sys_adc.h\"\n#include \"sys_tim.h\"\n#include \"sys_tick.h\"\n#include <stdio.h>\n\n/* ---- ① 采样与执行器的量纲：全部集中在这里，别散落在函数里 ---- */\n#define LOOP_ADC        ADC1\n#define LOOP_ADC_CH     5U          /* 光电池通道                        */\n#define LOOP_PWM_TIM    SYS_TIM_1\n#define LOOP_PWM_CH     1U\n\n#define RAW_STEP_MAX    200U        /* 限幅：一次最多允许变这么多 ADC 码 */\n#define LPF_FC_HZ       20.0f       /* 截止频率：滤掉 50Hz 工频干扰       */\n#define LPF_FS_HZ       1000.0f     /* 采样率：和下面的 PID 周期对应      */\n\n/* ---- ② 三个对象一律 static：递推滤波器必须有记忆，别放栈上反复重建 ---- */\nstatic FilterLpf_t s_lpf;\nstatic Kalman_t    s_kf;\nstatic Pid_t       s_pid;\n\nstatic uint16_t s_prev_raw;         /* 限幅滤波要上次的值 */\nstatic uint32_t s_bad_cnt;          /* 坏值计数：滤波器不替我们数 */\nstatic uint32_t s_last_tick;\n\n/* ---- ③ PID 输出（浮点控制量）换算成 PWM 千分比并钳位 ---- */\nstatic uint16_t pid_out_to_permille(float out)\n{\n    float v = out;\n    if (v < 0.0f)    { v = 0.0f; }        /* 单向执行器：负的输出没有意义 */\n    if (v > 1000.0f) { v = 1000.0f; }\n    return (uint16_t)(v + 0.5f);          /* 四舍五入 */\n}\n\n/* ---- ④ 一次完整的采样 -> 滤波 -> 估计 -> 控制 ---- */\nstatic void loop_step(float target)\n{\n    uint16_t raw;\n    float    z, y, out;\n    float    dt = (float)SYS_TICK_Elapsed(s_last_tick) / 1000.0f;\n\n    s_last_tick = SYS_TICK_GetTick();\n\n    /* 4.1 采样。SYS_ADC_Read 是【两个】参数（adc, channel） */\n    raw = SYS_ADC_Read(LOOP_ADC, LOOP_ADC_CH);\n\n    /* 4.2 第一关：限幅。差得太多就说明是野值，保持上次值 */\n    raw = FILTER_Limit(s_prev_raw, raw, RAW_STEP_MAX);\n    s_prev_raw = raw;\n\n    /* 4.3 第二关：一阶低通。坏值先自己判掉，别喂进去 */\n    z = (float)raw;\n    if (z != z) { s_bad_cnt++; return; }   /* z != z 是抓 NaN 最快的写法 */\n    y = FILTER_LpfUpdate(&s_lpf, z);\n\n    /* 4.4 第三关：卡尔曼。它自己算增益 K，比固定 alpha 的低通更懂“该信谁” */\n    y = KALMAN_Update(&s_kf, y);\n\n    /* 4.5 控制：位置式 PID，输出是绝对量，直接送执行器 */\n    out = PID_Update(&s_pid, target, y, dt);\n    SYS_TIM_PwmSetDuty(LOOP_PWM_TIM, LOOP_PWM_CH, pid_out_to_permille(out));\n}\n\n/* ---- ⑤ 初始化：顺序是“先对象、再限幅、最后限幅值”，别反 ---- */\nvoid loop_init(float target)\n{\n    float p, i, d;\n\n    /* 5.1 滤波器：按截止频率 + 采样率算精确 alpha */\n    FILTER_LpfInit(&s_lpf, LPF_FC_HZ, LPF_FS_HZ);\n\n    /* 5.2 卡尔曼：不想算平方就用“手感”说法，1 个单位的变化要跟上，8 个单位的乱跳要滤掉 */\n    (void)KALMAN_AutoTune(&s_kf, 1.0f, 8.0f);\n    (void)KALMAN_Reset(&s_kf, target);\n\n    /* 5.3 PID：三系数 -> 输出限幅 -> 积分限幅（抗饱和）-> 微分先行 */\n    PID_Init(&s_pid, 20.0f, 5.0f, 0.5f);\n    PID_SetOutputLimit(&s_pid, 0.0f, 1000.0f);      /* 上电第一件事，必须设 */\n    PID_SetIntegralLimit(&s_pid, -500.0f, 500.0f);  /* 取输出限幅的 50%~100%  */\n    PID_SetDerivOnMeasurement(&s_pid, 1U);          /* 设定值突变不产生微分尖峰 */\n    PID_SetSeparateThreshold(&s_pid, 200.0f);       /* 误差大于 200 时先不积分   */\n\n    (void)PID_GetTerms(&s_pid, &p, &i, &d);         /* 确认能读出来 */\n    s_last_tick = SYS_TICK_GetTick();\n}\n\n/* ---- ⑥ 固定周期跑：用 SYS_TICK_Every 保证 dt 稳定，dt 抖动会让 Ki/Kd 跑偏 ---- */\nvoid loop_task(float target)\n{\n    uint32_t last = 0U;\n\n    loop_init(target);\n\n    for (;;)\n    {\n        if (SYS_TICK_Every(&last, 10U) != 0U)      /* 10ms 一次，dt 就是 0.01s */\n        {\n            loop_step(target);\n        }\n    }\n}\n\n/* ---- ⑦ 排错：把中间量全打出来，一眼看出是谁在捣乱 ---- */\nvoid loop_dump(void)\n{\n    float p, i, d;\n\n    PID_GetTerms(&s_pid, &p, &i, &d);\n    printf(\"K=%.3f resid=%.2f  P=%.2f I=%.2f D=%.2f bad=%lu\\r\\n\",\n           KALMAN_GetGain(&s_kf), KALMAN_GetResid(&s_kf),\n           p, i, d, (unsigned long)s_bad_cnt);\n}",
 r:"先只看 K 值：上电后让被测量保持不动，串口应该看到 K 从 1.0 慢慢降到 0.1 附近，同时输出值基本不动（噪声被压掉）；然后突然把被测量拉偏一大截，K 应该在下一拍冲到 0.8 以上、输出值立刻跟过去。如果 K 一直停在 0.5 以上，说明等于没滤（把 R 调大）；如果 K 一直贴在 0.0x 不抬头、跳变也跟不上，说明滤太狠（把 Q 调大）。再打印 PID 三个分量：稳态时 I 应该是一个不动的常数（不是一直在涨），P 接近 0，I 一直涨就是积分饱和，去查 PID_SetIntegralLimit。",
 k:"滤波 卡尔曼 Kalman PID 闭环 数字滤波 一阶低通 低通 滑动平均 中值滤波 限幅 去极值 抗积分饱和 积分分离 微分先行 位置式 增量式 调参 Q R 增益 残差 NaN 坏值 dt 固定周期 ADC PWM 占空比 千分比 超声波 HCSR04 测距 板2 独有"
}
"""
