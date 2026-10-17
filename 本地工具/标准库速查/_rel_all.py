# -*- coding: utf-8 -*-
"""统一补齐 data_rel.js 的 CARD_REL 联动（取代 _add_rel.py 的两段式逻辑）。

为什么要重写：_add_rel.py 里 ENTRIES 只管"新卡建 key"、EXTRAS 只管"合并进已有 key"，
两者对不上就会出现"老卡既没 key、EXTRAS 又找不到 key"的死角。本脚本一条路径走完：
  - 17 张流程卡 + 17 个向导，凡是 CARD_REL 里还没有 key 的 → 新增 key
  - 已经有 key 的 → 把新联动合并进那个数组（同名目标去重）
名字一律从 data_flow.js / data_guide.js 的 n 字段按序号前缀取，绝不手打。
目标合法性：必须出现在卡片名集合里，或出现在 data_std_lib.js 的条目名里（项目自研 API）。
任一目标不合法就整体退出、不写文件（防"点了没反应"的死链）。
幂等：重跑只会报"已是最新"。
"""
import io, os, re, sys, subprocess

sys.stdout.reconfigure(encoding="utf-8")
HERE = os.path.dirname(os.path.abspath(__file__))
Q = chr(34)
P = os.path.join(HERE, "data_rel.js")


def read(p):
    return io.open(os.path.join(HERE, p), encoding="utf-8", newline="").read()


def names_of(p):
    s = read(p)
    return [m.group(1) for m in re.finditer(r"n:\s*" + Q + r"([^" + Q + r"]*)" + Q, s)]


FLOW = names_of("data_flow.js")
GUIDE = names_of("data_guide.js")
CARDS = FLOW + GUIDE

# 联动目标允许指向"页面上任何能点开的条目"，不只是卡片：
# 页面把 12 个 data 文件的条目全 push 进 ALL，所以 SPI_InitTypeDef / RTC_SetAlarm /
# PWR_EnterSTOPMode 这类外设结构体与函数在别的文件里也有条目，跳过去一样能打开。
# 只查 data_std_lib.js 会把这些合法目标误判成死链（踩过一次）。
STD = []
for _f in ("data_std_lib.js", "data_extra.js", "data_full.js", "data_tables.js",
           "data_usage.js", "data_cn.js", "data_demo.js", "data_datasheet.js"):
    try:
        STD += names_of(_f)
    except IOError:
        pass
KNOWN = set(CARDS) | set(STD)


def pick(lst, prefix):
    for n in lst:
        if n.startswith(prefix):
            return n
    raise SystemExit("找不到以 %r 开头的条目名" % prefix)


# ①..⑳ 的圈码从 U+2460 起，㉑..㉟ 从 U+3251 起。本项目模块卡编号只会往上加，
# 所以这里按「圈码 → 卡片名」自动建表，不再写死 range(17)：以后加新卡不用改本脚本。
CIRCLED = [chr(0x2460 + i) for i in range(20)] + [chr(0x3251 + i) for i in range(15)]

F = {}
for _idx, _c in enumerate(CIRCLED):
    for _n in FLOW:
        if _n.startswith(_c):
            F[str(_idx)] = _n
            break
G = {}
for n in GUIDE:
    G[n] = n


def Fx(i):
    """取第 i 张流程卡；编号还没落地的返回 None（新卡分批加时不会卡住脚本）。"""
    return F.get(str(i))


def gpick(prefix):
    """按前缀找引导向导；找不到返回 None。"""
    for n in GUIDE:
        if n.startswith(prefix):
            return n
    return None


# 项目实战卡（⑱ 起）必须「一张都不许少」：漏一张就意味着那一段联动整块消失，
#   而这种失败是静默的（gpick 返回 None → Lx 直接 return → 页面看不出来）。
#   所以下面这个 _WANT / MISSING 机制专门把静默失败变成显式报错，最后统一结算。
_WANT = []      # [(编号圈码, 说明)]
MISSING = []    # [(编号圈码, 说明)]


def want_flow(i, why):
    """声明「编号 i 的流程卡必须存在」。"""
    c = CIRCLED[i] if 0 <= i < len(CIRCLED) else "?%d" % i
    _WANT.append((c, why + "（流程卡）"))
    n = F.get(str(i))
    if n is None:
        MISSING.append((c, why + "（流程卡）"))
    return n


def want_guide(i, why):
    """声明「编号 i 的引导向导必须存在」，并按圈码前缀找它。"""
    c = CIRCLED[i] if 0 <= i < len(CIRCLED) else "?%d" % i
    _WANT.append((c, why + "（引导卡）"))
    n = gpick("\U0001f9ed " + c) if c != "?" else None
    if n is None:
        MISSING.append((c, why + "（引导卡）"))
        print("  [!!] 按前缀 %r 找不到引导卡 —— 这一段的联动会被跳过" % ("\U0001f9ed " + c))
    return n


def gproj(i):
    """按编号取项目实战引导卡（⑱=17 … ㉗=26）；找不到返回 None 并记账。"""
    c = CIRCLED[i] if 0 <= i < len(CIRCLED) else None
    n = gpick("\U0001f9ed " + c) if c else None
    if n is None:
        print("  [!!] gproj(%d) 找不到引导卡（圈码 %s）" % (i, c))
    return n


def Lx(key, pairs):
    """登记联动；key 为空、或某个目标还没落地，就跳过它。"""
    if not key:
        return
    L(key, [(a, b) for a, b in pairs if a])


def Lc(i, pairs):
    """按流程卡编号登记联动（编号 = 圈码序号，⑱ 是 17）。"""
    Lx(Fx(i), pairs)
g_rtos = pick(GUIDE, "\U0001f9ed FreeRTOS")
g_rtc0 = pick(GUIDE, "\U0001f9ed RTC \u7535\u5b50\u949f")
g_uart = pick(GUIDE, "\U0001f9ed \u4e32\u53e3 DMA")
g_i2c = pick(GUIDE, "\U0001f9ed I2C")
g_adc = pick(GUIDE, "\U0001f9ed ADC")
g_flash = pick(GUIDE, "\U0001f9ed \u677f\u8f7d FLASH")
g_wdg = pick(GUIDE, "\U0001f9ed \u72ec\u7acb\u770b\u95e8\u72d7")
g_wwdg = pick(GUIDE, "\U0001f9ed \u7a97\u53e3\u770b\u95e8\u72d7")
g_pwm = pick(GUIDE, "\U0001f9ed \u5b9a\u65f6\u5668 PWM")
g_exti = pick(GUIDE, "\U0001f9ed EXTI")
g_frame = pick(GUIDE, "\U0001f9ed \u6570\u636e\u5e27")
g_ic = pick(GUIDE, "\U0001f9ed \u5b9a\u65f6\u5668\u8f93\u5165\u6355\u83b7")
g_spi = pick(GUIDE, "\U0001f9ed SPI")
g_oc = pick(GUIDE, "\U0001f9ed \u5b9a\u65f6\u5668\u8f93\u51fa\u6bd4\u8f83")
g_dac = pick(GUIDE, "\U0001f9ed DAC")
g_can = pick(GUIDE, "\U0001f9ed CAN")
g_rtc_alarm = pick(GUIDE, "\U0001f9ed RTC \u95f9\u949f")

# ---------------------------------------------------------------------------
# 联动内容：(key, [[目标, 为什么], ...])
#   · 老卡（①②③…）与新卡（⑬⑭…）一视同仁；key 不存在就新建，存在就合并。
#   · 每张流程卡 ↔ 它对应的向导；每条业务依赖都写成"为什么"。
# ---------------------------------------------------------------------------
LINKS = {}

def L(key, pairs):
    LINKS.setdefault(key, [])
    LINKS[key].extend(pairs)

# ① 串口接收
L(F["0"], [[g_uart, "配套引导：从空闲中断原理到环形缓冲，7 步讲透"],
           [F["1"], "接收和发送是一对：一个用 DMA+IDLE 收不定长包，一个用轮询/中断发"],
           [F["8"], "串口收到的是字节流，要变成「一帧」得靠状态机 + CRC16"],
           [F["9"], "LoRa 模组就挂在这条串口上：E22 透传靠「字节间停顿」切帧"],
           [F["10"], "ESP8266 同样走串口 AT 指令；一次收一行，别把两行粘一起"],
           [F["3"], "两者都靠 DMA 后台搬运：ADC 是循环模式，串口是正常模式 + IDLE 收尾"]])
# ② 串口发送
L(F["1"], [[g_uart, "配套引导：printf 重定向与发送超时保护"],
           [F["0"], "发送是「我主动说」，接收是「听别人说」，两半合起来才是一条链路"]])
# ③ I2C
L(F["2"], [[g_i2c, "配套引导：扫描 → 读写寄存器 → 多任务互斥，6 步"],
           [F["12"], "选型必看：I2C 两根线挂多器件、有应答能查错；SPI 四根线快得多但无应答"]])
# ④ ADC
L(F["3"], [[g_adc, "配套引导：规则组 / 循环模式 / DMA 半满中断，7 步"],
           [F["14"], "一个把模拟量读进来、一个把数字量送出去；配对理解最快"],
           [F["4"], "要「模拟量输出」但没有 DAC 时，常用 PWM + RC 滤波凑合（精度差很多）"],
           [F["0"], "两者都靠 DMA 后台搬运；ADC 是循环模式，串口是正常模式 + IDLE"]])
# ⑤ PWM
L(F["4"], [[g_pwm, "配套引导：时基与占空比怎么算，7 步"],
           [F["5"], "一个往外发波形、一个量外面的波形；想验证 PWM 对不对，用输入捕获量自己"],
           [F["13"], "输出比较是 PWM 的「底层动作」：PWM 只是 OC 的一种特例"]])
# ⑥ 输入捕获
L(F["5"], [[g_ic, "配套引导：边沿检测 / 溢出处理 / 频率与占空比换算，7 步"],
           [F["4"], "用自己输出的 PWM 去喂自己的输入捕获，是最省事的自测方法"],
           [F["13"], "输入捕获量波形、输出比较产波形，方向相反"]])
# ⑦ EXTI
L(F["6"], [[g_exti, "配套引导：中断线映射 + 优先级 + 消抖，8 步"],
           [F["16"], "RTC 闹钟/唤醒不是直接进 NVIC，要先过 EXTI Line17/22 这道门"]])
# ⑧ FreeRTOS
L(F["7"], [[g_rtos, "配套引导：任务 / 队列 / 信号量 / 优先级，10 步"],
           [F["0"], "经典组合：串口中断只负责把字节丢进队列，任务在队列上等，中断里绝不做重活"],
           [F["11"], "多任务下看门狗要用「心跳喂狗」：每个任务报到，全员到齐才喂"]])
# ⑨ 组帧
L(F["8"], [[g_frame, "配套引导：帧头 / 长度 / CRC16 / 状态机收包，6 步"],
           [F["15"], "CAN 一帧最多 8 字节，本项目 20 字节协议帧走 CAN 必须自己分包"],
           [F["9"], "LoRa 只负责把字节搬到空中，帧格式（帧头/长度/CRC16）全靠应用层自己定"]])
# ⑩ LoRa
L(F["9"], [[F["0"], "LoRa 挂在这条串口上；本工程收包走「串口中断 + 环形缓冲 + 字节间隔（SYS_LORA_FRAME_GAP_MS = 10 ms）切帧」，不是 DMA+IDLE"],
           [F["8"], "20 字节定稿帧就是通过 LoRa 传的：帧头 0xAA55 + CRC16-MODBUS"]])
# ⑪ ESP8266 / MQTT
L(F["10"], [[F["0"], "AT 指令是一问一答：发完命令要等 OK/ERROR，不能只看超时"],
            [F["11"], "联网任务是最容易卡死的一个，看门狗要能覆盖它"]])
# ⑫ IWDG
L(F["11"], [[g_wdg, "配套引导：超时算法与心跳喂狗，共 6 步。"],
            [F["7"], "心跳喂狗模式要和任务数对齐（SYS_WDG_HEARTBEAT_COUNT）"]])
# ⑬ SPI
L(F["12"], [[g_spi, "配套引导：SPI 为什么比 I2C 快、四种模式与 DMA 收发，10 步"],
            [F["2"], "反方向对照：器件多、线少选 I2C；要速度选 SPI"],
            ["W25QXX_Init", "本项目板2 的 W25Q128 Flash 走 SPI1；与 NRF24L01 共用总线，不能同时用"],
            ["SYS_SPI_Transfer", "同一条 SPI1 上的 NRF24L01 与 Flash 靠各自 CS 片选区分，共用总线要互斥（同一时刻只拉低一根 CS）"],
            ["SPI_InitTypeDef", "先填这个结构体（方向/模式/数据宽度/分频），再交给 SYS_SPI_Init"]])
# ⑭ 输出比较 OC
L(F["13"], [[g_oc, "配套引导：PWM 与输出比较到底差在哪，7 步讲透"],
            ["SYS_TIM_OcInit", "本项目已封装：六种 oc_mode（翻转/强制高/强制低/PWM1/PWM2/单脉冲）"],
            ["SYS_TIM_OcInitIT", "带中断版：回调在定时器中断里跑，NVIC 优先级必须 ≥5"],
            [F["4"], "PWM 是输出比较的一种特例；先看 PWM 卡再回来看这张更清楚"],
            [F["5"], "输入捕获是「量外面的波形」，输出比较是「往外产生波形」，别混"]])
# ⑮ DAC
L(F["14"], [[g_dac, "配套引导：从「DAC 不是 PWM 滤波」讲到 DMA 输出波形，7 步"],
            [F["3"], "一个是把模拟量读进来，一个是把数字量送出去，配对理解最快"],
            ["DAC_InitTypeDef", "触发源 / 波形发生器 / 缓冲 / 对齐方式都在这里配"],
            ["DAC_SetChannel1Data", "12 位右对齐写值；软件触发模式下还要调 DAC_SoftwareTriggerCmd"],
            ["DAC_SoftwareTriggerCmd", "软件触发模式下不调用它则无输出。"],
            ["DAC_DMACmd", "要用 DMA 输出波形时，除了 DMA 本身还必须在 DAC 侧打开 DMA 请求"],
            [F["0"], "波形输出常用 DMA 循环搬运，和串口 DMA 是同一套 DMA 控制器"]])
# ⑯ CAN
L(F["15"], [[g_can, "配套引导：位时序怎么算、过滤器为什么必须配，8 步讲清"],
            [F["8"], "CAN 一帧只有 8 字节，本项目 20 字节协议帧要用 CAN 传必须自己分包"],
            ["CAN_InitTypeDef", "注意 ABOM（自动离线恢复）一定要 ENABLE，否则 Bus-Off 后永久卡死"],
            ["CAN_FilterInit", "未配置过滤器时收不到任何报文，且无报错。"],
            ["CAN_Transmit", "它返回成功只代表「已进邮箱」，真正结果要看 CAN_TransmitStatus"],
            ["CAN_Receive", "中断里必须读走 FIFO，否则 FMP0 一直有效、反复进中断"]])
# ⑰ RTC 闹钟 + 周期唤醒
L(F["16"], [[g_rtc_alarm, "配套引导：闹钟 / 周期唤醒怎么配、EXTI17/22 为什么必须配，7 步讲透"],
            [g_rtc0, "先能用 RTC 走时，再谈闹钟和周期唤醒"],
            [F["6"], "先把 EXTI 弄明白再看这张：RTC 的两条中断线都挂在 EXTI 上"],
            ["RTC_SetAlarm", "写闹钟值；写完后须调用 RTC_AlarmCmd(ENABLE) 才生效。"],
            ["RTC_AlarmCmd", "配置闹钟值后须调用它，否则闹钟不触发。"],
            ["RTC_SetWakeUpCounter", "周期唤醒的计数值：周期 = (N+1) / 唤醒时钟频率"],
            ["RTC_WakeUpCmd", "同理，不使能唤醒定时器，配置全对也从不触发"],
            ["PWR_EnterSTOPMode", "低功耗场景的灵魂：CPU 睡觉靠 RTC 唤醒（唤醒后必须重配时钟）"],
            ["EXTI_Init", "RTC 中断要先过 EXTI：闹钟走 Line17、唤醒走 Line22，漏配则「标志置了但中断不进」"]])

# ---- 引导向导侧的反向链接 ----
L(g_rtos, [[F["7"], "配套流程卡：完整可执行代码 + 逐行注释"],
           [F["11"], "任务卡死是现场第一大故障，喂狗要和任务框架一起设计"]])
L(g_rtc0, [[F["16"], "走时只是基础，到点自动干活靠闹钟与周期唤醒"]])
L(g_uart, [[F["0"], "配套流程卡：完整可执行代码 + 逐行注释"],
           [F["1"], "收与发是一套：DMA+IDLE 负责收，printf 重定向负责发"],
           [F["8"], "收到的字节流要靠组帧状态机切成一帧一帧"]])
L(g_i2c, [[F["2"], "配套流程卡：完整可执行代码 + 逐行注释"],
          [F["12"], "I2C 与 SPI 是两条最常用的板级总线，选型必看"]])
L(g_adc, [[F["3"], "配套流程卡：完整可执行代码 + 逐行注释"],
          [F["14"], "读模拟量与写模拟量配对理解最快"]])
L(g_flash, [[F["12"], "板载 Flash 就是挂在 SPI 上的（板2 走 SPI1）"],
            ["W25QXX_Init", "本项目板2 的 Flash 初始化入口"]])
L(g_wdg, [[F["11"], "配套流程卡：完整可执行代码 + 逐行注释"],
          [F["7"], "心跳喂狗必须和多任务框架一起看"]])
L(g_wwdg, [[F["11"], "WWDG 与 IWDG 的区别先看这张卡：一个窗口期，一个单纯超时"]])
L(g_pwm, [[F["4"], "配套流程卡：完整可执行代码 + 逐行注释"],
          [F["13"], "PWM 只是输出比较的一种特例，想真正吃透看这张"],
          [F["5"], "PWM 输出对不对，用输入捕获量自己最省事"]])
L(g_exti, [[F["6"], "配套流程卡：完整可执行代码 + 逐行注释"],
           [F["16"], "RTC 的闹钟与唤醒同样要过 EXTI 这道门"]])
L(g_frame, [[F["8"], "配套流程卡：完整可执行代码 + 逐行注释"],
            [F["9"], "这套帧格式的实际传输通道就是 LoRa"],
            [F["15"], "走高可靠总线时协议帧怎么分包看这张"]])
L(g_ic, [[F["5"], "配套流程卡：完整可执行代码 + 逐行注释"],
         [F["4"], "先用 PWM 产生已知波形，再喂给输入捕获验证"]])
L(g_spi, [[F["12"], "配套流程卡：完整可执行代码 + 逐行注释"],
          ["SYS_SPI_Init", "本项目封装好的入口：一次配好时钟、引脚、模式、速度"],
          ["SYS_SPI_Transfer", "收发一长串（内部按字节循环，等待 TXE/RXNE）"]])
L(g_oc, [[F["13"], "配套流程卡：完整代码 + 六种 oc_mode 对照"],
         ["SYS_TIM_OcSetCompare", "运行中改比较值 → 改占空比 / 脉冲宽度"],
         ["SYS_TIM_OcStop", "停止输出；不用了要及时关，否则引脚一直被定时器占着"]])
L(g_dac, [[F["14"], "配套流程卡：完整代码 + 万用表实测验证法"]])
L(g_can, [[F["15"], "配套流程卡：完整代码 + 排错总表"]])
L(g_rtc_alarm, [[F["16"], "配套流程卡：完整代码 + 标志清除铁律"]])

# ===========================================================================
# 本项目 FWLIB 模块卡（⑱ 起）——编号索引：⑱=17 ⑲=18 ⑳=19 ㉑=20 ㉒=21
#                                       ㉓=22 ㉔=23 ㉕=24 ㉖=25 ㉗=26
# 一律用 Lc() / gpick() / Fx() 写：某张卡还没落地时自动跳过，脚本不会整体退出。
# ===========================================================================
g_proj_log = gproj(17)
# ⑱ SPI Flash 断网缓存日志
Lc(17, [[g_proj_log, "配套引导：从认芯片到断电恢复自检，一步步做"],
        [Fx(12), "底层就是 SPI1 主机：先把 SPI 收发跑通，再回来看这层缓存逻辑"],
        [g_flash, "同一个 Flash、两套用法：那张存参数，这张存掉线期间的数据"],
        ["W25QXX_Init", "入口：SPI1 时钟/引脚/片选一次配好，先跑通它再谈日志"],
        ["SPI_I2S_ReceiveData", "读 JEDEC ID 就是发一个命令字节再收三个字节——收数这一段靠它"],
        ["SPI_I2S_SendData", "读写都是先发命令/地址再收发，发送这一段靠它"],
        ["SPI_Cmd", "先把 SPI 外设使能起来，才谈得上扫日志区、恢复写指针"],
        [Fx(9), "断网时把数据写进这里，联网后走 LoRa 补传"],
        [Fx(19), "MQTT 判活失败才落本地；两边的「算不算掉线」口径必须一致"],
        [Fx(21), "往上传时按自定义帧打包，别把裸结构体直接丢上总线"]])
Lx(g_proj_log, [[Fx(17), "配套流程卡：完整代码 + 三个真实教训（ID / 原子提交 / 擦除单位）"],
                [g_flash, "板载 Flash 的另一个分区用法：参数保存与掉电默认值"],
                [g_spi, "底层 SPI 主机怎么配，先看这个"],
                ["SPI_Init", "更底层：SPI1 主机模式与软件片选怎么配"],
                ["W25QXX_Init", "更底层：SPI1 + 片选初始化"]])

# ⑲ LoRa E22 点对点
g_proj_lora = gproj(18)
Lc(18, [[g_proj_lora, "配套引导：三根模式脚 + AUX 握手，把「第一条指令总是丢」讲透"],
        [Fx(0), "LoRa 就挂在这条串口上，收发不定长数据靠 DMA + 空闲中断"],
        [Fx(21), "透明传输不划帧边界，必须配自定义帧才能切出一包一包"],
        [Fx(17), "链路断了就把数据落到本地缓存，等恢复再补传"],
        [Fx(20), "Modbus 走有线、LoRa 走无线，是现场两种典型链路"],
        [Fx(25), "两块板之间的无线链路就是它，联调总览里看全貌"],
        [Fx(9), "同一个模块的另一张卡：AT 配置命令逐条讲"]])
Lx(g_proj_lora, [[Fx(18), "配套流程卡：模式表 + 引脚分配（两板不同）+ 排错表"],
                 [Fx(0), "底层串口怎么收一整包，先看这张"],
                 [Fx(21), "上层协议帧，两者是一套"],
                 [Fx(17), "断链后的数据落到哪里"]])

# ⑳ ESP8266 + MQTT 上行
g_proj_mqtt = gproj(19)
Lc(19, [[g_proj_mqtt, "配套引导：AT 联网四步 → MQTT 连接 → 订阅 → 上报，含半开连接排查"],
        [Fx(0), "ESP8266 也是串口器件，AT 命令的收发都走这条链路"],
        [Fx(21), "MQTT 之外的自定义帧；上报内容怎么打包看这张"],
        [Fx(17), "只发 PINGREQ 不查 PINGRESP 会导致「假在线」，判活失败必须转本地缓存"],
        [Fx(9), "MQTT 与 LoRa 是两条互为备份的上行通道"],
        [Fx(25), "双板联调时两条通道怎么切，看总览卡"]])
Lx(g_proj_mqtt, [[Fx(19), "配套流程卡：AT 命令全集 + 剩余长度编码 + 返回码分级"],
                 [Fx(17), "掉线判据失效的直接后果：数据静默丢失"],
                 [Fx(0), "底层串口收包机制"],
                 [Fx(25), "两条上行通道怎么容错"]])

# ㉑ Modbus RTU 从机 + RS485 半双工
g_proj_modbus = gproj(20)
Lc(20, [[g_proj_modbus, "配套引导：DE 脚方向切换为什么必须等 TC，9 步做通从机"],
        [Fx(8), "CRC16 与帧边界：这张是自定义协议，那张是标准 Modbus"],
        [Fx(0), "RS485 收发不定长帧同样靠 DMA + 空闲中断"],
        [Fx(18), "有线的 Modbus 与无线的 LoRa，现场两种链路对照看"],
        [Fx(25), "从机被主站轮询的时序，在双板联调里最好观察"]])
Lx(g_proj_modbus, [[Fx(20), "配套流程卡：功能码表 + 异常码 + 3.5 字符静默"],
                   [Fx(8), "另一种帧格式：本项目自定义 20 字节帧"],
                   [Fx(0), "底层串口机制"]])

# ㉒ sys_frame 自定义 20 字节帧
g_proj_frame = gproj(21)
Lc(21, [[g_proj_frame, "配套引导：从帧头到 CRC 逐字节拆，含接收状态机与超时"],
        [g_frame, "协议前传：那张讲为什么需要组帧，这张讲本项目已定案的帧格式"],
        [Fx(8), "同一套 CRC16-MODBUS，一个讲原理一个讲工程实现"],
        [Fx(18), "这套帧的实际无线通道"],
        [Fx(19), "MQTT 上报前也可先组帧再当载荷发"],
        [Fx(20), "工业总线上的标准帧格式，与自定义帧对照"],
        [Fx(17), "缓存里的记录往外传时按帧打包"],
        [Fx(25), "两板之间传的就是这种帧"]])
Lx(g_proj_frame, [[Fx(21), "配套流程卡：帧结构 + 校验范围 + 返回码"],
                  [g_frame, "先理解为什么要组帧"],
                  [Fx(18), "帧的无线通道"],
                  [Fx(20), "标准 Modbus 帧格式"]])

# ㉓ OLED SSD1306 显示
g_proj_oled = gproj(22)
Lc(22, [[g_proj_oled, "配套引导：控制字节 + 页寻址 + 整帧加锁，把花屏讲清"],
        [Fx(2), "OLED 就是挂在这条 I2C 上的一个从机，读写机制完全一样"],
        [g_i2c, "同一条总线上还挂着 24C02 / MPU6050 / 各传感器"],
        [Fx(7), "刷屏放在任务里做，别在主循环里阻塞"],
        [Fx(24), "同总线多从机的互斥与地址分配，这张讲得最细"],
        ["I2C_GenerateSTART", "扫地址的本质：挨个发 START + 从机地址，看有没有 ACK"],
        ["I2C_SendData", "命令与显存数据最终都从这里写下去"],
        ["I2C_ReceiveData", "回读显存做自检（可选）"]])
Lx(g_proj_oled, [[Fx(22), "配套流程卡：初始化序列 + 显存布局 + 撕裂根因"],
                 [Fx(2), "I2C 读写从机的通用套路"],
                 [Fx(24), "多从机共存时的总线互斥"]])

# ㉔ DHT11 单总线温湿度
g_proj_dht = gproj(23)
Lc(23, [[g_proj_dht, "配套引导：单总线时序 + 微秒级延时 + 校验和，5 步做成"],
        [Fx(7), "采集放独立任务，采完通过队列交给处理任务"],
        [Fx(17), "温湿度超限或断网时，写进本地缓存等补传"],
        [Fx(18), "采到的数据最终走 LoRa 传出去"],
        [Fx(25), "采集端到通信端的完整数据流，看总览卡"]])
Lx(g_proj_dht, [[Fx(23), "配套流程卡：40 位数据格式 + 时序容忍度 + 重试策略"],
                [Fx(17), "数据的本地归宿"],
                [Fx(18), "数据的上行通道"]])

# ㉕ AT24C02 + I2C 传感器组合
g_proj_i2c = gproj(24)
Lc(24, [[g_proj_i2c, "配套引导：一条总线挂多个从机，地址分配 + 互斥 + 分页写"],
        [Fx(2), "单从机读写的通用做法，先看那张再看这张"],
        [g_i2c, "I2C 外设本身的配置（时钟、开漏、上拉）"],
        [Fx(22), "OLED 也是这条总线上的从机，多任务下最考验互斥"],
        [Fx(17), "EEPROM 存参数、Flash 存缓存，两者分工别混"],
        [g_flash, "参数保存的另一条路：片内 Flash 扇区 A/B 轮换"],
        ["I2C_GenerateSTART", "总线接好第一件事：发 START + 从机地址，把应答的地址摸出来"],
        ["I2C_SendData", "写从机寄存器 / 多字节数据"],
        ["I2C_ReceiveData", "读从机寄存器 / 多字节数据"],
        ["I2C_SoftwareResetCmd", "总线被从机拉死时的救场手段之一（另一个是手动发 9 个时钟）"]])
Lx(g_proj_i2c, [[Fx(24), "配套流程卡：器件地址总表 + 分页写 + 互斥量用法"],
                [Fx(2), "I2C 基础读写"],
                [Fx(22), "同总线的显示器件"],
                [g_flash, "参数到底存 EEPROM 还是片内 Flash"]])

# ㉖ FreeRTOS 双板联调总览
g_proj_sys = gproj(25)
Lc(25, [[g_proj_sys, "配套引导：从两块单板到一条数据链路，任务/队列/超时怎么排"],
        [Fx(7), "任务骨架的通用写法先看这张"],
        [g_rtos, "FreeRTOS 基础：任务、队列、信号量、互斥量"],
        [Fx(23), "检测端采集任务"],
        [Fx(17), "断网缓存的落盘与补传"],
        [Fx(18), "无线链路的收发"],
        [Fx(19), "无线上行与云端上报"],
        [Fx(20), "有线从机侧"],
        [Fx(21), "两板之间的数据帧"]])
Lx(g_proj_sys, [[Fx(25), "配套流程卡：两板任务表 + 队列尺寸 + 超时预算"],
                [g_rtos, "先把 FreeRTOS 基础过一遍"],
                [g_wdg, "联调时最容易忽略的就是喂狗"]])
Lx(g_rtos, [[Fx(25), "本项目两板的任务划分与队列设计实例"]])

# ㉗ PID / Kalman / 滑动平均
g_proj_algo = gproj(26)
Lc(26, [[g_proj_algo, "配套引导：从滑动平均到 PID，三种算法各自解决什么问题"],
        [Fx(3), "算法处理的对象就是 ADC 采回来的模拟量"],
        [Fx(4), "PID 的输出通常送给 PWM 调占空比"],
        [Fx(7), "算法放在独立任务里跑固定周期，节奏才稳"],
        [g_adc, "ADC 采集本身怎么配"],
        [g_pwm, "执行器侧怎么接"]])
Lx(g_proj_algo, [[Fx(26), "配套流程卡：三种算法对照 + 采样周期决定成败"],
                 [g_adc, "输入从哪来"],
                 [g_pwm, "输出到哪去"]])

# 老向导补几条通往本项目模块卡的路（合并进已有 key）
Lx(g_flash, [[Fx(17), "同一个 Flash 的另一半用途：断网缓存日志"],
             [Fx(24), "参数也可以放外部 EEPROM，两者取舍看这张"]])
Lx(g_spi, [[Fx(17), "SPI 主机最典型的应用：W25QXX 缓存日志"]])
Lx(g_i2c, [[Fx(22), "I2C 显示器件的完整用法"],
           [Fx(23), "单总线传感器（不是 I2C，但常一起挂）"],
           [Fx(24), "一条总线挂多个从机的工程做法"]])
Lx(g_frame, [[Fx(21), "本项目已定案的 20 字节帧格式"]])
Lx(g_uart, [[Fx(18), "串口后面挂 LoRa 模块的完整做法"],
            [Fx(19), "串口后面挂 ESP8266 + MQTT 的完整做法"],
            [Fx(20), "串口后面挂 RS485 收发器的完整做法"]])
Lx(g_adc, [[Fx(26), "采到的模拟量怎么滤波 / 怎么闭环"]])
Lx(g_pwm, [[Fx(26), "PWM 只是执行器，控制律要另写"]])

# ---- 引导向导侧的反向链接（本项目模块卡，逐一登记；卡没落地就跳过） ----
for _gi, _fi, _why in ((g_proj_log, 17, "配套流程卡：完整代码 + 三个真实教训"),
                       (g_proj_lora, 18, "配套流程卡：模式表 + 引脚分配 + 排错表"),
                       (g_proj_mqtt, 19, "配套流程卡：AT 命令全集 + 返回码分级"),
                       (g_proj_modbus, 20, "配套流程卡：功能码表 + 异常码 + 静默间隔"),
                       (g_proj_frame, 21, "配套流程卡：帧结构 + 校验范围 + 返回码"),
                       (g_proj_oled, 22, "配套流程卡：初始化序列 + 显存布局 + 撕裂根因"),
                       (g_proj_dht, 23, "配套流程卡：40 位数据格式 + 时序容忍度"),
                       (g_proj_i2c, 24, "配套流程卡：器件地址总表 + 分页写"),
                       (g_proj_sys, 25, "配套流程卡：两板任务表 + 队列尺寸"),
                       (g_proj_algo, 26, "配套流程卡：三种算法对照")):
    Lx(_gi, [[Fx(_fi), _why]])

# ---------------------------------------------------------------------------
# 项目实战卡齐备性检查（⑱~㉗）
#   为什么要有这一步：gpick() 找不到卡时返回 None，Lx() 拿到 None 就静默 return
#   —— 页面照样生成，只是那一段联动整块消失，肉眼根本看不出来。
#   所以这里把「10 张流程卡 + 10 张引导卡」当硬前提，缺一张就整体失败、不写文件。
# ---------------------------------------------------------------------------
_proj_missing = []
for _i in range(17, 27):
    _c = CIRCLED[_i]
    if F.get(str(_i)) is None:
        _proj_missing.append((_c, "流程卡"))
    if gpick("\U0001f9ed " + _c) is None:
        _proj_missing.append((_c, "引导卡"))
if _proj_missing:
    print("项目实战卡缺卡，未写文件（缺 %d 张）：" % len(_proj_missing))
    for _c, _k in _proj_missing:
        print("   圈码 %s  %s" % (_c, _k))
    raise SystemExit(1)
print("项目实战卡齐备：⑱~㉗ 共 10 张流程卡 + 10 张引导卡全部到位")


# ---------------------------------------------------------------------------
# 合法性检查
# ---------------------------------------------------------------------------
bad = []
for name, rels in LINKS.items():
    if name not in KNOWN:
        bad.append("key 不在条目表里: " + name)
    for a, _b in rels:
        if a not in KNOWN:
            bad.append("目标不在条目表里: %s -> %s" % (name, a))
if bad:
    print("联动目标校验失败，未写文件：")
    for b in bad:
        print("   ", b)
    raise SystemExit(1)
print("目标校验通过：%d 个 key，%d 条联动说明" % (len(LINKS), sum(len(v) for v in LINKS.values())))

# ---------------------------------------------------------------------------
# 写入：已有 key 合并进数组；没有的 key 新增到 行首 }; 之前
# ---------------------------------------------------------------------------
t = read("data_rel.js")
add = 0
new_keys = []
for name, rels in LINKS.items():
    key = Q + name + Q
    m = re.search(r"(?m)^" + re.escape(key) + r"\s*:\s*\[", t)
    if m:
        i = m.end()
        depth = 1
        while i < len(t):
            c = t[i]
            if c == "\\":
                i += 2
                continue
            if c == "[":
                depth += 1
            elif c == "]":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        body = t[m.end():i]
        fresh = []
        for a, b in rels:
            if (Q + a + Q) in body:
                continue
            fresh.append("[" + Q + a + Q + "," + Q + b + Q + "]")
        if not fresh:
            continue
        t = t[:i] + "," + ",".join(fresh) + t[i:]
        add += len(fresh)
        print("  合并 %2d 条 -> %s" % (len(fresh), name[:40]))
    else:
        pairs = ",".join("[" + Q + a + Q + "," + Q + b + Q + "]" for a, b in rels)
        new_keys.append(key + ": [" + pairs + "],")
        add += len(rels)

if new_keys:
    p = t.rfind("\n};")
    if p < 0:
        raise SystemExit("找不到 CARD_REL 结尾的行首 };")
    head = t[:p].rstrip()
    if not head.endswith(","):
        head += ","
    MARKER = "/* ================= 卡片联动（流程卡 × 引导向导 × 自研 API） ================= */"
    if MARKER in head:
        tail = "\n"
    else:
        tail = "\n\n" + MARKER + "\n"
    t = head + tail + "\n".join(new_keys) + "\n" + t[p:]

if add == 0:
    print("已是最新，无需写入")
else:
    io.open(P, "w", encoding="utf-8", newline="").write(t)
    print("写入完成：%d 条新联动，%d 字节" % (add, len(t)))

r = subprocess.run(["node", "--check", P], capture_output=True, text=True, encoding="utf-8")
print("node --check data_rel.js exit=%d %s" % (r.returncode, (r.stderr or "").strip()[:300]))
tt = read("data_rel.js")
keys = re.findall(r'(?m)^"([^"]+)"\s*:\s*\[', tt)
cover = [n for n in CARDS if n in set(keys)]
print("CARD_REL 顶层 key 数: %d（其中卡片 %d/%d）" % (len(keys), len(cover), len(CARDS)))
