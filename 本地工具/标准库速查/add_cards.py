# -*- coding: utf-8 -*-
r"""add_cards.py —— 往 data_flow.js / data_guide.js 末尾追加「流程卡」与「引导向导」。

为什么写这个脚本：
  速查工具缺 SPI、输出比较（OC）、DAC、CAN、RTC 闹钟这几块，
  手工往 250KB 的 JS 里粘贴极易把引号/逗号搞坏，所以用脚本做：
  ① 扫描 JS 找到数组**真正**的结束位置（会跳过字符串字面量里的括号）
  ② 在结束位置前插入新条目（末尾原有条目补一个逗号）
  ③ 插入前自动备份 .bak

用法：python add_cards.py            # 追加
      python add_cards.py --check    # 只看会不会重复插入
幂等：已存在同名 n 字段则跳过。
"""
import io
import os
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))

# 另外几张卡（⑮ DAC / ⑯ CAN / ⑰ RTC 闹钟，流程卡与引导向导都有）
# 单独放在 data_extra_cards.py，免得这个文件越滚越大。
#   FLOW_EXTRA  = FLOW_DAC  + FLOW_CAN  + FLOW_RTC_ALARM
#   GUIDE_EXTRA = GUIDE_DAC + GUIDE_CAN + GUIDE_RTC_ALARM
from data_extra_cards import FLOW_EXTRA, GUIDE_EXTRA

# ---- 由 _norm_extra.py 生成：把文本源归一化成规范 JS 转义 ----
_norm_extra_ok = True


# ----------------------------------------------------------------------
# 工具：找到 `const NAME = [` 对应 `]` 的下标（跳过字符串与注释里的括号）
# ----------------------------------------------------------------------
def find_array_end(text, decl):
    i = text.index(decl)
    i = text.index("[", i)                      # 数组开始的 [
    depth = 0
    j = i
    n = len(text)
    while j < n:
        ch = text[j]
        if ch == '"' or ch == "'":              # 字符串字面量
            q = ch
            j += 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == q:
                    break
                j += 1
        elif ch == "/" and j + 1 < n and text[j + 1] == "*":   # 块注释
            k = text.find("*/", j + 2)
            j = (k + 1) if k >= 0 else n - 1
        elif ch == "/" and j + 1 < n and text[j + 1] == "/":   # 行注释
            k = text.find("\n", j)
            j = k if k >= 0 else n - 1
        elif ch == "[":
            depth += 1
        elif ch == "]":
            depth -= 1
            if depth == 0:
                return j
        j += 1
    raise RuntimeError("找不到数组结尾: " + decl)


def insert_before_array_end(path, decl, new_text, marker):
    text = io.open(path, encoding="utf-8", newline="").read()
    if marker in text:
        print("  跳过（已存在）: %s -> %s" % (os.path.basename(path), marker))
        return False
    end = find_array_end(text, decl)
    head = text[:end]
    tail = text[end:]
    # 末尾原有条目需要补一个逗号
    stripped = head.rstrip()
    if stripped.endswith("}"):
        head = stripped + ",\n\n"
    new_text = new_text.replace("\r\n", "\n")
    io.open(path + ".bak", "w", encoding="utf-8", newline="").write(text)
    io.open(path, "w", encoding="utf-8", newline="").write(head + new_text + tail)
    print("  已插入到 %s（备份 %s.bak）" % (os.path.basename(path), os.path.basename(path)))
    return True


# ======================================================================
# 一、新增「流程卡」（data_flow.js）—— 字段 m/t/n/d/f/a/e/r/k
# ======================================================================
FLOW_NEW = r"""
/* ===================== ⑬ SPI 主机（接 Flash / 无线模块 / 触摸屏） ===================== */
{m:"流程", t:"flow", n:"⑬ SPI 主机通信（W25QXX Flash / NRF24L01 / XPT2046 触摸）",
 d:"SPI 是“同步全双工”总线：每发一个字节就同时收一个字节。本项目板2 的 W25Q128 断网缓存、NRF24L01 无线、XPT2046 触摸全靠它。SPI 调不通多为模式（CPOL/CPHA）或片选配置错误。",
 f:[
  ["开时钟（3 个）","SPI1 挂在 APB2，引脚在 GPIOA（AHB1），用 DMA 还要开 DMA2（AHB1），一条都不能漏"],
  ["配引脚","SCK/MISO/MOSI 复用推挽（GPIO_Mode_AF + GPIO_OType_PP + GPIO_Speed_100MHz）；NSS 用软件管理时，CS 另配成普通推挽输出"],
  ["填 SPI_InitTypeDef","五个关键项：方向、模式(CPOL/CPHA)、数据宽度、位序(MSB/LSB)、预分频。改一个就通不了"],
  ["SPI_Init + SPI_Cmd","先 SPI_Init 再 SPI_Cmd(ENABLE)；顺序反了收发全废（SPI 使能前部分位不可写）"],
  ["写“一个字节”的收发函数","先等 TXE（发送缓冲空）→ 写 DR → 再等 RXNE（接收缓冲非空）→ 读 DR。发和收是同一件事"],
  ["接上具体器件","Flash 用 0x03 读 / 0x02 页写 / 0x20 擦扇区；每个命令前拉低 CS、命令发完拉高 CS"],
  ["上 FreeRTOS 加总线互斥","同一个 SPI 挂两个从机（Flash + NRF24L01）时，必须用互斥量保证“一次只有一个任务在动这条总线”"]
 ],
 a:[
  ["SPI1 引脚","SCK=PA5 / MISO=PA6 / MOSI=PA7 / NSS=PA4（或软件 CS）","映射随重映射变化，查数据手册"],
  ["模式选择","W25QXX 与 NRF24L01 都要 SPI_MODE_0（CPOL=0,CPHA=0）","模式错 = 收全 0xFF 或全 0x00，最常见故障"],
  ["片选 CS","每个从机一根独立的 CS；SPI 本身没有寻址能力","多从机共用 SCK/MISO/MOSI，只分开 CS"],
  ["预分频","SPI_BaudRatePrescaler_2/4/8.../256","W25QXX 可跑到 80MHz+，NRF24L01 只能 10MHz"],
  ["板 1 实情","SPI1：PB3(SCK)/PB4(MISO)/PB5(MOSI) 与 JTAG 复用","先用 GPIO_PinRemapConfig 解掉 JTAG 才当 SPI 用"],
  ["板 2 实情","SPI1 被 W25QXX 与 NRF24L01 共用；板子上没有以太网 PHY","两个器件不能同时用，或必须分时切换 CS"]
 ],
 e:"/* ============ SPI1 主机：初始化 + 收发一个字节（裸机版） ============ */\n#include \"stm32f4xx.h\"\n\n/* ---- 1. 开时钟：SPI1 在 APB2，引脚在 GPIOA（AHB1） ---- */\nvoid SPI1_Master_Init(void)\n{\n    GPIO_InitTypeDef g;\n    SPI_InitTypeDef  s;\n\n    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);\n    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1,  ENABLE);\n\n    /* ---- 2. 引脚复用：SCK=PA5 MISO=PA6 MOSI=PA7 ---- */\n    GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_SPI1);\n    GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_SPI1);\n    GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_SPI1);\n\n    g.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;\n    g.GPIO_Mode  = GPIO_Mode_AF;          /* 复用功能 */\n    g.GPIO_OType = GPIO_OType_PP;         /* 推挽 */\n    g.GPIO_Speed = GPIO_Speed_100MHz;     /* SPI 高速时必须 100MHz */\n    g.GPIO_PuPd  = GPIO_PuPd_UP;\n    GPIO_Init(GPIOA, &g);\n\n    /* ---- CS 用软件管理：另配成普通推挽输出，空闲拉高 ---- */\n    g.GPIO_Pin  = GPIO_Pin_4;\n    g.GPIO_Mode = GPIO_Mode_OUT;\n    g.GPIO_PuPd = GPIO_PuPd_UP;\n    GPIO_Init(GPIOA, &g);\n    GPIO_SetBits(GPIOA, GPIO_Pin_4);          /* CS 空闲高 */\n\n    /* ---- 3. SPI 参数（模式 0：CPOL=0 / CPHA=1Edge） ---- */\n    s.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;\n    s.SPI_Mode              = SPI_Mode_Master;\n    s.SPI_DataSize          = SPI_DataSize_8b;\n    s.SPI_CPOL              = SPI_CPOL_Low;    /* 空闲时 SCK = 低 */\n    s.SPI_CPHA              = SPI_CPHA_1Edge;  /* 第 1 个边沿采样 */\n    s.SPI_NSS               = SPI_NSS_Soft;    /* 软件 CS */\n    s.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;   /* 84MHz/8 = 10.5MHz */\n    s.SPI_FirstBit          = SPI_FirstBit_MSB;/* Flash 必须 MSB 在前 */\n    s.SPI_CRCPolynomial     = 7;\n    SPI_Init(SPI1, &s);\n\n    /* ---- 4. 顺序不能反：先 Init 再 Cmd ---- */\n    SPI_Cmd(SPI1, ENABLE);\n}\n\n/* ---- 5. 收发一个字节（SPI 是全双工：发一个必然收一个） ---- */\nuint8_t SPI1_TransferByte(uint8_t tx)\n{\n    /* 等发送缓冲空 */\n    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) { }\n    SPI_I2S_SendData(SPI1, tx);\n    /* 等接收缓冲非空 */\n    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET) { }\n    return (uint8_t)SPI_I2S_ReceiveData(SPI1);\n}\n\n/* ---- 6. 用法：读 Flash 的 JEDEC ID（命令 0x9F，应得 0xC84018=GD25Q128） ---- */\nuint32_t Flash_ReadID(void)\n{\n    uint32_t id;\n    GPIO_ResetBits(GPIOA, GPIO_Pin_4);        /* CS 拉低：开始 */\n    (void)SPI1_TransferByte(0x9F);            /* 命令 */\n    id  = (uint32_t)SPI1_TransferByte(0xFF) << 16;\n    id |= (uint32_t)SPI1_TransferByte(0xFF) << 8;\n    id |= (uint32_t)SPI1_TransferByte(0xFF);\n    GPIO_SetBits(GPIOA, GPIO_Pin_4);          /* CS 拉高：结束 */\n    return id;\n}\n\n/* 常见问题：\n * 1) 忘了 SPI_Cmd(ENABLE) → 两个 while 都死等，看起来像“程序卡死”\n * 2) CPOL/CPHA 填错 → 收回来全是 0xFF 或 0x00（时序采错位）\n * 3) 忘了拉低 CS → 从机根本不响应，读回全 0xFF\n * 4) 多从机共用 SPI 却没有各自独立的 CS → 两个器件同时驱动 MISO，数据互撞\n *\n * 本项目 FWLIB 里已经有现成驱动，直接用，别手搓：\n *     板2：SYS_SPI_Init / SYS_SPI_TransferByte / SYS_SPI_Transfer / SYS_SPI_Read\n *          W25QXX_Init / W25QXX_ReadID / W25QXX_Read / W25QXX_Write / W25QXX_EraseSector\n *     板1：sys_spi.c 同一套 API（SYS_SPI_*） *    SYS_SPI_TransferByte 内部自带超时（SYS_SPI_TIMEOUT），\n *      从机没接好时返回 0xFF 并累加 SYS_SPI_TimeoutCount()，不会死等。\n */",
 r:"SPI 主机能收发一个字节 → 能读 Flash 的 JEDEC ID、能读写 NRF24L01 寄存器。这是板2 断网缓存（W25Q128）与无线模块的地基。",
 k:"SPI SPI1 主机 初始化 全双工 SCK MISO MOSI NSS CS 片选 CPOL CPHA 模式0 模式3 预分频 复用 W25QXX W25Q128 Flash NRF24L01 触摸 XPT2046 通信 总线 快速 高速 SPI_I2S_SendData SPI_I2S_ReceiveData SPI_Cmd SPI_Init 相关"},

/* ===================== ⑭ 输出比较 OC（和 PWM 不是一回事） ===================== */
{m:"流程", t:"flow", n:"⑭ 定时器输出比较 OC（单脉冲 / 精确翻转，别和 PWM 混）",
 d:"PWM 是“输出比较的一种特殊用法”：ARR 定周期、CCR 定占空比、每周期都输出。而输出比较 OC 本身是“计数到 CCR 时做一次动作”，可以只翻转一次、可以只进一次中断不发波形、可以输出移相脉冲。做精确定时、步进电机脉冲、超声触发时必须用它。",
 f:[
  ["想清楚要哪种","① 只进中断不碰引脚 → TIM_OCMode_Timing；② 到点翻转电平 → TIM_OCMode_Toggle；③ 强制输出高低 → TIM_OCMode_Active/Inactive；④ 移相 PWM → Toggle + 中断里重装 CCR"],
  ["开时钟 + 配引脚","只有“要输出到引脚”的模式才需要配复用推挽；纯中断模式不用配引脚"],
  ["配时基","TIM_TimeBaseInit 定 PSC（分频）与 ARR（周期）→ 决定时间分辨率"],
  ["配输出比较通道","填 TIM_OCInitTypeDef：OCMode 选模式、OCPolarity 定极性、Pulse = 第一次比较值"],
  ["TIM_OCxInit + TIM_SelectOCxM","初始化后要改模式用 TIM_SelectOCxM（运行期切换模式的唯一正确入口）"],
  ["决定要不要进中断","TIM_ITConfig(TIMx, TIM_IT_CCx, ENABLE) + NVIC_Init → 到点进 TIMx_IRQHandler"],
  ["中断里必须清标志","TIM_ClearITPendingBit(TIMx, TIM_IT_CCx)，否则会连续进中断把 CPU 占满"],
  ["（高级用法）单脉冲","TIM_SelectOnePulseMode(TIMx, TIM_OPMode_Single)：到点自动停，不用手动关"],
  ["（高级用法）强制输出","TIM_ForcedOCxConfig(TIMx, TIM_ForcedAction_Active/Inactive)：立刻把引脚拉高/拉低，软件控制电平"]
 ],
 a:[
  ["PWM vs OC","PWM = ARR 定周期 + CCR 定占空比 + 连续输出；OC = 计数到 CCR 做一次动作","“要连续波形”用 PWM，“要一次/中断/精确时刻”用 OC"],
  ["TIM_OCMode_Timing","只置 CCxIF 标志，引脚不动","做精确定时器，不影响引脚"],
  ["TIM_OCMode_Toggle","到 CCR 时引脚翻转一次","单脉冲输出、移相方波、步进电机"],
  ["TIM_OCMode_Active","到 CCR 时引脚强制有效电平（高）","单次脉冲、超声触发"],
  ["单脉冲模式","TIM_OPMode_Single，到点自动停","发一个宽度精确的脉冲后不再重复"],
  ["中断函数名","TIM1_CC_IRQHandler / TIM2_IRQHandler","通用定时器 TIM2~5 用 TIMx_IRQHandler（不分通道）"],
  ["本工程现成封装","sys_tim.c 的 SYS_TIM_* 与 PWM 相关接口","改频率时会按比例缩放 PWM 通道，OC 通道要自己重算 CCR"]
 ],
 e:"/* ============ 输出比较三种典型用法（对照着看就懂区别） ============ */\n#include \"stm32f4xx.h\"\n\n/* ------------------------------------------------------------------\n * 用法 A：纯定时，到点进中断，引脚完全不动\n *   适合：精确定时采样、软件计时器、任务调度节拍\n * ------------------------------------------------------------------ */\nstatic volatile uint32_t g_tick_100us = 0;\n\nvoid TIM_OC_Timing_Init(void)\n{\n    GPIO_InitTypeDef        g;\n    TIM_TimeBaseInitTypeDef tb;\n    TIM_OCInitTypeDef       oc;\n    NVIC_InitTypeDef        ni;\n\n    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);\n\n    /* 时基：84MHz/(83+1)=1MHz，计数 100 次 = 100µs */\n    tb.TIM_Prescaler         = 84 - 1;\n    tb.TIM_CounterMode       = TIM_CounterMode_Up;\n    tb.TIM_Period            = 100 - 1;\n    tb.TIM_ClockDivision     = TIM_CKD_DIV1;\n    tb.TIM_RepetitionCounter = 0;\n    TIM_TimeBaseInit(TIM3, &tb);\n\n    /* 关键：OCMode = Timing，只置标志，不碰引脚 */\n    oc.TIM_OCMode      = TIM_OCMode_Timing;\n    oc.TIM_OutputState = TIM_OutputState_Disable;   /* 引脚输出关掉 */\n    oc.TIM_Pulse       = 50;                        /* 计到 50 就触发（周期中点） */\n    oc.TIM_OCPolarity  = TIM_OCPolarity_High;\n    TIM_OC1Init(TIM3, &oc);\n\n    /* 开通道 1 的比较中断 */\n    TIM_ITConfig(TIM3, TIM_IT_CC1, ENABLE);\n\n    ni.NVIC_IRQChannel                   = TIM3_IRQn;\n    ni.NVIC_IRQChannelPreemptionPriority = 5;      /* FreeRTOS 下必须 ≥5 */\n    ni.NVIC_IRQChannelSubPriority        = 0;\n    ni.NVIC_IRQChannelCmd                = ENABLE;\n    NVIC_Init(&ni);\n\n    TIM_Cmd(TIM3, ENABLE);\n}\n\n/* TIM3 中断：通用定时器所有通道共用一个中断函数 */\nvoid TIM3_IRQHandler(void)\n{\n    if (TIM_GetITStatus(TIM3, TIM_IT_CC1) != RESET)\n    {\n        TIM_ClearITPendingBit(TIM3, TIM_IT_CC1);   /* 必须清，否则反复进 */\n        g_tick_100us++;\n        /* 下一次比较点 = 当前 + 100（自动重装，形成等间隔中断） */\n        TIM_SetCompare1(TIM3, (uint16_t)(TIM_GetCounter(TIM3) + 50));\n    }\n}\n\n/* ------------------------------------------------------------------\n * 用法 B：单次精确脉冲，引脚输出一个宽度由 CCR 决定的高电平\n *   适合：超声触发、步进电机走一步、给传感器一个精确时钟\n * ------------------------------------------------------------------ */\nvoid TIM_OC_OneShot_Init(void)      /* 例：TIM2_CH1 = PA0 */\n{\n    GPIO_InitTypeDef        g;\n    TIM_TimeBaseInitTypeDef tb;\n    TIM_OCInitTypeDef       oc;\n\n    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);\n    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,  ENABLE);\n\n    GPIO_PinAFConfig(GPIOA, GPIO_PinSource0, GPIO_AF_TIM2);\n    g.GPIO_Pin   = GPIO_Pin_0;\n    g.GPIO_Mode  = GPIO_Mode_AF;\n    g.GPIO_OType = GPIO_OType_PP;\n    g.GPIO_Speed = GPIO_Speed_100MHz;\n    g.GPIO_PuPd  = GPIO_PuPd_UP;\n    GPIO_Init(GPIOA, &g);\n\n    /* 1MHz 计数：1 个 tick = 1µs */\n    tb.TIM_Prescaler         = 84 - 1;\n    tb.TIM_CounterMode       = TIM_CounterMode_Up;\n    tb.TIM_Period            = 0xFFFF;\n    tb.TIM_ClockDivision     = TIM_CKD_DIV1;\n    tb.TIM_RepetitionCounter = 0;\n    TIM_TimeBaseInit(TIM2, &tb);\n\n    /* Active 模式：计到 CCR 时引脚变高（发脉冲起点） */\n    oc.TIM_OCMode      = TIM_OCMode_Active;\n    oc.TIM_OutputState = TIM_OutputState_Enable;\n    oc.TIM_Pulse       = 10;              /* 10µs 后拉高 */\n    oc.TIM_OCPolarity  = TIM_OCPolarity_High;\n    TIM_OC1Init(TIM2, &oc);\n    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Disable);  /* 运行期改 CCR 立即生效 */\n\n    TIM_Cmd(TIM2, ENABLE);\n}\n\n/* 发一个脉冲：先归零计数，再让它跑到 CCR+宽度 */\nvoid Pulse_Once(uint16_t width_us)\n{\n    TIM_SetCounter(TIM2, 0);\n    TIM_SetCompare1(TIM2, width_us);\n    TIM_Cmd(TIM2, ENABLE);\n    /* 等待这一轮跑完（裸机可忙等；RTOS 里请用事件/中断通知） */\n    while (TIM_GetCounter(TIM2) < width_us) { }\n    TIM_Cmd(TIM2, DISABLE);\n    TIM_SetCounter(TIM2, 0);\n    GPIO_ResetBits(GPIOA, GPIO_Pin_0);      /* 收尾拉低，保证下次从低开始 */\n}\n\n/* ------------------------------------------------------------------\n * 用法 C：运行期切换模式 + 强制输出（软件控制引脚电平）\n * ------------------------------------------------------------------ */\nvoid OC_SwitchDemo(void)\n{\n    /* 运行期改模式：必须用 TIM_SelectOCxM（直接改结构体再 Init 是错的） */\n    TIM_SelectOCxM(TIM2, TIM_Channel_1, TIM_OCMode_Toggle);\n    TIM_CCxCmd(TIM2, TIM_Channel_1, TIM_CCx_Enable);\n\n    /* 不看计数，立刻把引脚拉高/拉低（做“软件电平控制”） */\n    TIM_ForcedOC1Config(TIM2, TIM_ForcedAction_Active);     /* 立刻高 */\n    TIM_ForcedOC1Config(TIM2, TIM_ForcedAction_Inactive);   /* 立刻低 */\n}\n\n/* 常见问题：\n * 1) 把“输出比较”当成 PWM 用 → 以为引脚会自动出波形。\n *    只有 OutputState=Enable 且 OCMode 是 PWM/Active/Toggle 时引脚才动。\n * 2) 忘了 TIM_ClearITPendingBit → 中断里出不来，CPU 100% 占用。\n * 3) 通用定时器 TIM2~5 只有一个中断入口，多个通道要在同一个函数里分别判。\n * 4) 运行期改 OCMode：用 TIM_SelectOCxM，不要改结构体再 TIM_OCxInit。\n * 5) 高级定时器 TIM1/TIM8 还要 TIM_CtrlPWMOutputs (ENABLE)，否则引脚无输出。\n *\n * 本项目 FWLIB：sys_tim.c 已有 SYS_TIM_* 封装（含 PWM 通道与频率调整），\n *   但输出比较/单脉冲的封装还没做，要用 OC 就照本卡自己加，\n *    NVIC 优先级要 ≥5（FreeRTOS 红线），且中断函数名别和库里 __weak 的撞。\n */",
 r:"能做出 PWM 之外的精确定时：等间隔 100µs 中断、宽度精确的单次脉冲、运行期切换输出模式、软件强制引脚电平。",
 k:"输出比较 OC 输出捕获 定时器 单脉冲 one shot Timing Toggle Active Inactive 强制输出 移相 步进电机 精确 脉冲 中断 TIM_OCMode TIM_SelectOCxM TIM_ForcedOC TIM_SelectOnePulseMode TIM_SetCompare 比较 匹配 与PWM区别"}
"""
# ⑮ DAC / ⑯ CAN / ⑰ RTC 闹钟三张流程卡由 data_extra_cards.py 提供（FLOW_EXTRA）


# ======================================================================
# 二、新增「引导向导」（data_guide.js）—— 字段 m/t/n/d/g/f/e/k
#   f 的每一步 = [标题, 为什么, 做什么, 代码, 怎么验证, 常见问题, 相关条目]
# ======================================================================
GUIDE_NEW = r"""
/* ===================== ⑬ SPI 主机引导 ===================== */
{m:"引导", t:"guide", n:"🧭 SPI 主机引导（接 W25QXX Flash / NRF24L01 / 触摸屏）",
 d:"从零把 SPI 调通，再用它读写板上的 W25Q128 Flash，这是板2「断网缓存」功能的地基。每一步都有能立刻验证的现象。",
 g:"STM32F407 · SPI1 · 模式 0(CPOL=0/CPHA=0) · 软件片选 · 10.5MHz · 对应 FWLIB 的 sys_spi.c / w25qxx.c",
 k:"SPI 引导 向导 教程 步骤 SPI1 初始化 SCK MISO MOSI NSS CS 片选 CPOL CPHA 模式 全双工 W25QXX W25Q128 Flash NRF24L01 触摸 XPT2046 读ID JEDEC 页写 扇区擦除 总线 互斥 复用 排错 不通",
 e:"/* ============================================================\n *  SPI1 主机 + W25QXX Flash 完整可用代码 *   按「SPI 主机引导」10 步搭出来的成品\n *  平台：STM32F407 · SPI1(PA5/PA6/PA7) · CS=PA4(软件)\n * ============================================================ */\n#include \"stm32f4xx.h\"\n\n#define CS_LOW()   GPIO_ResetBits(GPIOA, GPIO_Pin_4)\n#define CS_HIGH()  GPIO_SetBits(GPIOA, GPIO_Pin_4)\n\n/* ---------- 1~4 步：初始化 ---------- */\nvoid SPI1_Init_Mode0(void)\n{\n    GPIO_InitTypeDef g;\n    SPI_InitTypeDef  s;\n\n    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);\n    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1,  ENABLE);\n\n    GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_SPI1);\n    GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_SPI1);\n    GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_SPI1);\n\n    g.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;\n    g.GPIO_Mode  = GPIO_Mode_AF;\n    g.GPIO_OType = GPIO_OType_PP;\n    g.GPIO_Speed = GPIO_Speed_100MHz;\n    g.GPIO_PuPd  = GPIO_PuPd_UP;\n    GPIO_Init(GPIOA, &g);\n\n    g.GPIO_Pin  = GPIO_Pin_4;              /* CS 普通输出 */\n    g.GPIO_Mode = GPIO_Mode_OUT;\n    GPIO_Init(GPIOA, &g);\n    CS_HIGH();\n\n    s.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;\n    s.SPI_Mode              = SPI_Mode_Master;\n    s.SPI_DataSize          = SPI_DataSize_8b;\n    s.SPI_CPOL              = SPI_CPOL_Low;\n    s.SPI_CPHA              = SPI_CPHA_1Edge;\n    s.SPI_NSS               = SPI_NSS_Soft;\n    s.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;\n    s.SPI_FirstBit          = SPI_FirstBit_MSB;\n    s.SPI_CRCPolynomial     = 7;\n    SPI_Init(SPI1, &s);\n    SPI_Cmd(SPI1, ENABLE);\n}\n\n/* ---------- 5 步：收发一个字节（带超时，不会死等） ---------- */\nuint8_t SPI1_Byte(uint8_t tx)\n{\n    uint32_t to = 200000;\n    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) { if (to-- == 0) return 0xFF; }\n    SPI_I2S_SendData(SPI1, tx);\n    to = 200000;\n    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET) { if (to-- == 0) return 0xFF; }\n    return (uint8_t)SPI_I2S_ReceiveData(SPI1);\n}\n\n/* ---------- 6~7 步：读 JEDEC ID（W25Q128 = 0xC84018） ---------- */\nuint32_t W25XX_ReadID(void)\n{\n    uint32_t id;\n    CS_LOW();\n    (void)SPI1_Byte(0x9F);\n    id  = (uint32_t)SPI1_Byte(0xFF) << 16;\n    id |= (uint32_t)SPI1_Byte(0xFF) << 8;\n    id |= (uint32_t)SPI1_Byte(0xFF);\n    CS_HIGH();\n    return id;\n}\n\n/* ---------- 8~9 步：等忙 + 页写 ---------- */\nstatic uint8_t W25XX_ReadStatus(void)\n{\n    uint8_t st;\n    CS_LOW();\n    (void)SPI1_Byte(0x05);\n    st = SPI1_Byte(0xFF);\n    CS_HIGH();\n    return st;\n}\n\nstatic void W25XX_WaitBusy(void)\n{\n    while ((W25XX_ReadStatus() & 0x01) != 0) { }   /* bit0 = BUSY */\n}\n\nstatic void W25XX_WriteEnable(void)\n{\n    CS_LOW();\n    (void)SPI1_Byte(0x06);\n    CS_HIGH();\n}\n\nvoid W25XX_WritePage(const uint8_t *src, uint32_t addr, uint16_t len)\n{\n    uint16_t i;\n    if (len > 256) len = 256;\n\n    W25XX_WaitBusy();\n    W25XX_WriteEnable();\n\n    CS_LOW();\n    (void)SPI1_Byte(0x02);                    /* PAGE PROGRAM */\n    (void)SPI1_Byte((uint8_t)(addr >> 16));\n    (void)SPI1_Byte((uint8_t)(addr >> 8));\n    (void)SPI1_Byte((uint8_t)(addr));\n    for (i = 0; i < len; i++) { (void)SPI1_Byte(src[i]); }\n    CS_HIGH();\n\n    W25XX_WaitBusy();\n}\n\n/* ---------- 擦一个 4KB 扇区 ---------- */\nvoid W25XX_EraseSector(uint32_t addr)\n{\n    W25XX_WaitBusy();\n    W25XX_WriteEnable();\n    CS_LOW();\n    (void)SPI1_Byte(0x20);                    /* SECTOR ERASE 4KB */\n    (void)SPI1_Byte((uint8_t)(addr >> 16));\n    (void)SPI1_Byte((uint8_t)(addr >> 8));\n    (void)SPI1_Byte((uint8_t)(addr));\n    CS_HIGH();\n    W25XX_WaitBusy();\n}\n\n/* ---------- 自检：读 ID → 擦 → 写 → 读回比对 ---------- */\nint SPI_Flash_SelfTest(void)\n{\n    uint32_t id;\n    uint8_t  wr[8] = { 0xA5, 0x5A, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC };\n    uint8_t  rd[8] = { 0 };\n    uint8_t  i;\n    uint32_t addr = 0x001000;                 /* 用日志区之外的一页 */\n\n    SPI1_Init_Mode0();\n\n    id = W25XX_ReadID();\n    if (id == 0xFFFFFFFF || id == 0x00000000) return 1;   /* 没器件 */\n    if ((id & 0xFF) != 0x18)                  return 2;   /* 容量不是 128Mbit */\n\n    W25XX_EraseSector(addr & ~0xFFF);\n    W25XX_WritePage(wr, addr, sizeof(wr));\n\n    CS_LOW();\n    (void)SPI1_Byte(0x03);                    /* READ DATA */\n    (void)SPI1_Byte((uint8_t)(addr >> 16));\n    (void)SPI1_Byte((uint8_t)(addr >> 8));\n    (void)SPI1_Byte((uint8_t)(addr));\n    for (i = 0; i < sizeof(rd); i++) { rd[i] = SPI1_Byte(0xFF); }\n    CS_HIGH();\n\n    for (i = 0; i < sizeof(rd); i++)\n    {\n        if (rd[i] != wr[i]) return 3;         /* 写进去读回来不一致 */\n    }\n    return 0;                                 /* 0 = SPI 与 Flash 全通 */\n}\n\n/* 简化做法：本项目 FWLIB 已经写好了（带超时、带日志缓存）：\n *   板2：SYS_SPI_Init / SYS_SPI_TransferByte / W25QXX_Init / W25QXX_ReadID /\n *        W25QXX_Read / W25QXX_Write / W25QXX_WriteSafe / W25QXX_EraseSector /\n *        W25QXX_IsBlank / W25QXX_LogInit / W25QXX_LogWrite / W25QXX_LogRead\n *   板1：sys_spi.c 同一套 SYS_SPI_* API\n */",
 f:[
  [
   "先搞清 SPI 是什么：它不是“又一个串口”",
   "串口(USART)是异步的：双方各自按约定波特率计时，不需要共享时钟线；SPI 是同步的：主机出一根 SCK 时钟线，从机跟着这个时钟收发。\n这带来两个直接后果：\n① 没有起始位/停止位/波特率协商，速度可以做到几十 MHz，比串口快两个数量级；\n② 没有寻址机制，SPI 总线上不能像 I2C 那样“喊地址”，只能靠每个从机一根独立的片选(CS)来决定跟谁说话。\n\n还要接受一个反直觉的事实：SPI 是全双工的。每发一个字节，必然同时收到一个字节；想“只读”，就得发一个哑字节(0xFF)去产生时钟。",
   "打开数据手册找到 SPI1 的引脚表，确认本板用的是哪三个脚（SCK/MISO/MOSI）。\n板1（GEC-M4）是 PB3/PB4/PB5；板2 用的是 PA5/PA6/PA7。\n先只是“看一眼知道在哪”，不用动代码。",
   "/* 只需这张对应表：\n *   SCK  = Serial Clock   主机出时钟（唯一由主机产生的信号）\n *   MISO = Master In  Slave Out   从机 → 主机\n *   MOSI = Master Out Slave In    主机 → 从机\n *   CS   = Chip Select    片选，低有效；每个从机一根\n *\n * 接线口诀：SCK-SCK、MISO-MISO、MOSI-MOSI、CS-CS，和串口不同，SPI 不需要交叉。\n * （串口的 TX 要接对方 RX，这是最容易接错的地方，SPI 反而简单）\n */",
   "能画出“一个主机 + 两个从机”的接线图：SCK/MISO/MOSI 三根线两个从机并联共用，CS 各一根。\n画对了说明已经理解 SPI 与 I2C/串口的本质区别。",
   "别把 MISO/MOSI 接反：接反的典型现象是“读回来全是 0xFF”或“全是 0x00”。\n别把 CS 接到从机的其他脚上：从机会一直不被选中，SPI 上什么都收不到。\n板1 特别SPI1 用的 PB3/PB4 默认被 JTAG 占着（PB3=JTDO、PB4=NJTRST），必须先解掉才能当 SPI 用（第 3 步会讲）。",
   "SPI_InitTypeDef GPIO_InitTypeDef"
  ],
  [
   "开时钟：SPI 要开三个时钟，少一个就静默失败",
   "STM32 的外设时钟是分级门控的，不打开时钟，寄存器写了也不生效、而且不会报错，这是嵌入式调试里最浪费时间的一类问题。\nSPI1 涉及三个不同的时钟域：\n· SPI1 本体 → APB2（高速外设总线）\n· SCK/MISO/MOSI/CS 引脚 → AHB1（GPIO 挂在 AHB1）\n· 如果还要配 DMA → AHB1（DMA 也在 AHB1，但是 DMA2，因为 SPI1 的 DMA 请求映射在 DMA2）",
   "在初始化函数开头把这三个时钟都打开，然后再去碰任何寄存器。\n顺序建议：先开时钟 → 再配引脚 → 再配 SPI。",
   "void SPI1_ClockOn(void)\n{\n    /* ① SPI1 本体在 APB2 */\n    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);\n\n    /* ② 引脚在 AHB1（本示例用 PA5/PA6/PA7，CS 用 PA4） */\n    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);\n\n    /* ③ 要上 DMA 时（SPI1 固定映射到 DMA2）：\n     *    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);\n     *    SPI1_TX = DMA2_Stream3 / 通道3\n     *    SPI1_RX = DMA2_Stream0 / 通道3\n     */\n}\n\n/* 板1 用 PB3/PB4/PB5 的版本：\n *    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);\n *    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB, ENABLE);\n */",
   "编译通过即可。这一步没有直观现象，但没有它，后面每一步都会“看起来像是寄存器没配好”。",
   "忘了开时钟的典型症状：“我明明写了 SPI_Cmd(ENABLE)，为什么读 ID 还是 0xFFFFFFFF？”\n排查法：在调试器里看 RCC->APB2ENR 的 bit12（SPI1EN）是否为 1。\nCS 引脚如果和 SPI 引脚不同的端口（比如 PA7 做 MOSI、PB0 做 CS），两个端口时钟都要开。",
   "RCC_APB2PeriphClockCmd RCC_AHB1PeriphClockCmd"
  ],
  [
   "配引脚：复用推挽 + 解掉 JTAG 占用（板1 必做）",
   "引脚要当 SPI 用，必须在 GPIO 里做两件事：把引脚模式设成复用(GPIO_Mode_AF)，再用 GPIO_PinAFConfig 指定它复用成哪个外设。只设模式不指定功能号是不行的。\n另外还有两个容易漏的点：\n· 输出类型必须是推挽(GPIO_OType_PP)，不能是开漏，开漏需要外部上拉才能出高电平，SPI 高速下会变成锯齿波；\n· 速度要选 GPIO_Speed_50MHz 或 100MHz，默认的低速档在 10MHz 时钟下边沿会变缓，导致从机采样错误。",
   "① 三个 SPI 引脚：GPIO_Mode_AF + GPIO_OType_PP + GPIO_Speed_100MHz + GPIO_PuPd_UP，并用 GPIO_PinAFConfig 逐个指定 GPIO_AF_SPI1。\n② CS 引脚：不要配成复用，配成普通推挽输出(GPIO_Mode_OUT)，初始化完立刻拉高（空闲状态）。\n③ 板1 若用 PB3/PB4，先解 JTAG。",
   "void SPI1_PinInit(void)\n{\n    GPIO_InitTypeDef g;\n\n    /* 板1 必做：PB3=JTDO、PB4=NJTRST 默认被 JTAG 占用。\n     *   解开后 JTAG 只剩 SWD（PB3/PB4 不再是调试口，SWD 仍可用）。\n     *   板2 用 PA5/6/7 没有这个问题，这两行可以注释掉。\n     */\n    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);\n\n    /* ---- ① SCK/MISO/MOSI：复用推挽 ---- */\n    GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_SPI1);   /* SCK  */\n    GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_SPI1);   /* MISO */\n    GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_SPI1);   /* MOSI */\n\n    g.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;\n    g.GPIO_Mode  = GPIO_Mode_AF;         /* 复用 */\n    g.GPIO_OType = GPIO_OType_PP;        /* 推挽，绝不能是 OD */\n    g.GPIO_Speed = GPIO_Speed_100MHz;    /* 高速档 */\n    g.GPIO_PuPd  = GPIO_PuPd_UP;\n    GPIO_Init(GPIOA, &g);\n\n    /* ---- ② CS：普通推挽输出，空闲拉高 ---- */\n    g.GPIO_Pin   = GPIO_Pin_4;\n    g.GPIO_Mode  = GPIO_Mode_OUT;        /* 不是 AF。软件控制 */\n    g.GPIO_OType = GPIO_OType_PP;\n    g.GPIO_Speed = GPIO_Speed_100MHz;\n    g.GPIO_PuPd  = GPIO_PuPd_UP;\n    GPIO_Init(GPIOA, &g);\n    GPIO_SetBits(GPIOA, GPIO_Pin_4);     /* CS 空闲必须是高 */\n}",
   "用示波器（或逻辑分析仪）量 CS 脚：初始化完成后应该是高电平（3.3V）。\n没有示波器也行：用万用表量 CS 对地电压 ≈3.3V 就对了。\n如果量到 0V，说明这几行引脚配置没跑，或者跑在了后面的代码里。",
   "CS 千万不能配成 GPIO_Mode_AF。配成复用的话 CS 由 SPI 外设控制，而 SPI_NSS_Soft 模式下外设根本不管 CS，结果是 CS 永远悬空/不变，现象就是“读回来全 0xFF”。\nGPIO_Speed 选低了（2MHz）在 10MHz 时钟下会通信不稳，表现为偶发读错。\n板1 用 PB3/PB4 却忘了解 JTAG：引脚被调试口占着，SPI 完全没波形。\nGPIO_PuPd 建议 UP：MISO 悬空时不会被拉成随机值，读回更稳定。",
   "GPIO_InitTypeDef GPIO_PinAFConfig GPIO_Init GPIO_PinRemapConfig"
  ],
  [
   "填 SPI_InitTypeDef：五个关键项定生死",
   "SPI 参数里真正决定“通不通”的是这五项，其余都有安全默认值：\n① 方向，本项目都是一主一从全双工；\n② 模式（CPOL/CPHA），最常错的一项，见下；\n③ 数据宽度，8 位（16 位只在接某些 ADC/DAC 时用）；\n④ 位序，Flash 必须 MSB 在前；\n⑤ 预分频，决定 SCK 频率，从机有上限。\n\nCPOL/CPHA 到底怎么选？\n· CPOL 决定“空闲时 SCK 是高还是低”；\n· CPHA 决定“在第一个边沿还是第二个边沿采样数据”。\n四个组合叫模式 0~3。绝大多数器件（W25QXX、NRF24L01、XPT2046、SD 卡）都用模式 0：CPOL=0（空闲低）、CPHA=0（第 1 个边沿采样）。",
   "填结构体 → SPI_Init → SPI_Cmd(ENABLE)。\n顺序：一定要 SPI_Init 在前、SPI_Cmd 在后，反了会有部分配置位写不进去。",
   "void SPI1_ModeInit(void)\n{\n    SPI_InitTypeDef s;\n\n    /* 模式 0：CPOL=Low + CPHA=1Edge，W25QXX / NRF24L01 / XPT2046 都用它 */\n    s.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;\n    s.SPI_Mode              = SPI_Mode_Master;\n    s.SPI_DataSize          = SPI_DataSize_8b;\n    s.SPI_CPOL              = SPI_CPOL_Low;      /* 空闲时 SCK = 低 */\n    s.SPI_CPHA              = SPI_CPHA_1Edge;    /* 第 1 个边沿采样 */\n    s.SPI_NSS               = SPI_NSS_Soft;      /* 软件 CS（推荐） */\n    s.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;   /* 84MHz/8 = 10.5MHz */\n    s.SPI_FirstBit          = SPI_FirstBit_MSB;  /* Flash 必须 MSB 在前 */\n    s.SPI_CRCPolynomial     = 7;                 /* 不用硬件 CRC，填 7 即可 */\n    SPI_Init(SPI1, &s);\n\n    SPI_Cmd(SPI1, ENABLE);        /* 必须在 SPI_Init 之后 */\n}\n\n/* 各模式的对照（拿不准就抄“模式 0”）：\n *   模式 0: CPOL_Low  + CPHA_1Edge    ← 最常用\n *   模式 1: CPOL_Low  + CPHA_2Edge\n *   模式 2: CPOL_High + CPHA_1Edge\n *   模式 3: CPOL_High + CPHA_2Edge\n */",
   "验证方式很直接：量 SCK 脚（或看逻辑分析仪）。\n· SPI 使能但还没收发时：SCK 应该停在低电平（因为 CPOL_Low），量到低就说明 CPOL 对了；\n· 真正发数据时才有波形。\n如果 SCK 一直是低、发数据也没波形 → SPI_Cmd(ENABLE) 没执行或时钟没开。",
   "CPOL/CPHA 错的现象：能收到数据但全是 0xFF 或 0x00，或者读回的 ID 每字节都错位。\nSPI_NSS 选 SPI_NSS_Hard（硬件 NSS）时，多从机会互相干扰（NSS 被从机拉低会让主机退出主模式），多从机场合一律用 SPI_NSS_Soft。\n预分频别一上来就填 Prescaler_2：先拿 8 或 16 调通，再逐步提速。\nSPI_CRCPolynomial 只有在硬件 CRC 模式才有意义，普通用法填任意值即可。",
   "SPI_InitTypeDef SPI_Init SPI_Cmd"
  ],
  [
   "写“收发一个字节”的函数，SPI 的读和写是同一件事",
   "这是 SPI 最反直觉、也最容易写错的地方。\nSPI 没有独立的“读周期”和“写周期”：主机产生时钟，在每个时钟周期里同时把 MOSI 上的数据推出去、把 MISO 上的数据收进来。所以：\n· 想“写数据”：调用这个函数，忽略返回值；\n· 想“读数据”：也调用这个函数，但必须发一个哑字节(0xFF)去产生时钟，从机才有时钟把数据推出来；\n· Flash 的读时序里有一堆 `(void)SPI1_Byte(0xFF)`，那些并非“写 0xFF 给 Flash”，而是“给 8 个时钟，读 8 位”。\n\n代码上就是等 TXE（发送缓冲空）→ 写 DR → 等 RXNE（接收缓冲非空）→ 读 DR。",
   "写一个 `uint8_t SPI_TransferByte(uint8_t tx)`：\n① 等 TXE 为 1；② 写 tx 到 DR；③ 等 RXNE 为 1；④ 返回 DR 的值。\n两个等待都要带超时，否则从机没接好时会永久卡死。",
   "/* 带超时的版本（推荐）：从机没接好也不会把程序卡死 */\nuint8_t SPI1_TransferByte(uint8_t tx)\n{\n    uint32_t to;\n\n    /* ① 等发送缓冲空 */\n    to = 200000;\n    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET)\n    {\n        if (to-- == 0) { return 0xFF; }       /* 超时：SPI 时钟没开或 SPE=0 */\n    }\n    SPI_I2S_SendData(SPI1, tx);\n\n    /* ② 等接收缓冲非空 */\n    to = 200000;\n    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET)\n    {\n        if (to-- == 0) { return 0xFF; }\n    }\n    return (uint8_t)SPI_I2S_ReceiveData(SPI1);\n}\n\n/* 关键认知：这个函数“发一个字节”的同时就“收了一个字节”。\n *   读 Flash 数据时那些 SPI1_TransferByte(0xFF)：\n *       并非“给 Flash 写 0xFF”，而是“给 8 个时钟，读 8 位”。\n *\n * 本项目 FWLIB 的实现就是这个（sys_spi.c 的 SYS_SPI_TransferByte），\n *   并且内部还累加 SYS_SPI_TimeoutCount()，出问题时可以直接读这个计数。\n */",
   "用这个函数发一个字节，在示波器上应该看到 SCK 出现 8 个时钟脉冲（每个字节 8 个边沿）；\n没有示波器就看返回值：连续调用 `SPI1_TransferByte(0xFF)`，如果总是返回 0xFF 或总是 0x00，说明时序不对（多半是 CPOL/CPHA）或从机没选上。",
   "忘了 SPI_Cmd(ENABLE) → TXE 永远不为 1，两个 while 都出不来，表现为“程序卡死在这里”。这正是要加超时的原因。\n以为“读”需要单独的读函数 → 在标准库里确实有 SPI_I2S_ReceiveData，但它不产生时钟。只用它读会拿到上一次的残留值。必须用“发一个字节”的函数。\n一次收发只读一次 DR、写一次 DR，多发或少发都会让后续数据错位一个字节。\n连续发多个字节时，TXE 可以在发完当前字节后立刻为 1（缓冲空），而 RXNE 要等整字节收完，所以严格来说是先等 TXE 写、再等 RXNE 读，顺序不能反。",
   "SPI_I2S_SendData SPI_I2S_ReceiveData SPI_I2S_GetFlagStatus"
  ],
  [
   "接上 W25QXX Flash：读 JEDEC ID 验明正身",
   "Flash 类的 SPI 器件都遵循同一套套路：拉低 CS → 发命令字节 → 发地址(如果需要) → 收发数据 → 拉高 CS。\n第一个要发的命令永远是 0x9F（JEDEC ID），它是只读的、不依赖任何配置，最适合用来验证“SPI 到底通了没有”。\n返回 3 个字节，含义是：厂商 ID、存储类型、容量。\n本项目板2 用的是 GD25Q128（兆易创新）：厂商 0xC8、类型 0x40、容量 0x18（= 128Mbit = 16MB）→ 合起来 0xC84018。\n（如果是 Winbond W25Q128，就是 0xEF4018，厂商字节不同，容量字节都是 0x18）",
   "写一个 `uint32_t W25QXX_ReadID(void)`：CS 拉低 → 发 0x9F → 连读 3 个字节拼成 24 位 → CS 拉高。\n如果返回 0xFFFFFF 或 0x000000，先别改代码，去看下一步的排查表。",
   "uint32_t W25QXX_ReadID(void)\n{\n    uint32_t id;\n\n    GPIO_ResetBits(GPIOA, GPIO_Pin_4);          /* CS 拉低：开始 */\n    (void)SPI1_TransferByte(0x9F);              /* JEDEC ID 命令 */\n    id  = (uint32_t)SPI1_TransferByte(0xFF) << 16;   /* 厂商 ID */\n    id |= (uint32_t)SPI1_TransferByte(0xFF) << 8;    /* 存储类型 */\n    id |= (uint32_t)SPI1_TransferByte(0xFF);         /* 容量     */\n    GPIO_SetBits(GPIOA, GPIO_Pin_4);            /* CS 拉高：结束 */\n\n    return id;\n}\n\n/* 结果判读：\n *   0xC84018  → GD25Q128（本项目板2 用的）全通\n *   0xEF4018  → Winbond W25Q128 全通\n *   0xFFFFFF  → ① MISO 没接/断线 ② CS 没拉低 ③ 从机没供电\n *   0x000000  → ① MISO 被拉死到地 ② 引脚配成了输出而不是 AF\n *   其它值    → 时钟太快或模式不对，先把预分频调大到 Prescaler_32 再试\n */",
   "这一步是整个 SPI 调试的分水岭：\n· 读出 0xC84018（或 0xEF4018）→ SPI 完全通了，后面都是纯逻辑问题；\n· 读出 0xFFFFFF → 硬件/配置问题，按下面的顺序查：\n  ① 从机供电了没（量 VCC 3.3V）；② CS 真的是低吗（量 CS 脚）；③ MISO 有没有虚焊/接反；④ 时钟开没开；⑤ CPOL/CPHA 对不对。\n· 读出 0x000000 → 大多数是 MISO 被配置成推挽输出并被拉低了。",
   "读 ID 不依赖 Flash 里存了什么，任何状态都能读，所以它是排查 SPI 的常用手段。\n不用先擦除、也不用写使能，0x9F 是纯只读命令。\n如果 ID 的 3 个字节互相错位（比如读到 0x4018C8），那是位序问题：查 SPI_FirstBit 必须是 MSB。\n本项目 FWLIB 里已经有 `W25QXX_Init(speed)`，它内部会读 ID 并只校验容量字节（0x18），这样换任何一家 128Mbit 的芯片都能用，不会被厂商字节卡住。",
   "SPI_InitTypeDef SPI_I2S_SendData SPI_I2S_ReceiveData"
  ],
  [
   "读懂状态寄存器：为什么每次写/擦之前都要等 BUSY",
   "Flash 的物理特性决定了三件事，这是所有 Flash 驱动的核心：\n① 写只能把 1 变成 0，不能把 0 变回 1；把 0 变 1 必须按扇区整块擦除（4KB 起步）；\n② 擦和写都不是瞬间完成的，擦一个 4KB 扇区典型 45ms，最长可到 400ms；\n③ 在这个过程中芯片忙，此时发任何命令都会被静默忽略（不报错、不响应）。\n\n所以驱动的铁律是：发写/擦命令前，必须先确认 WEL(写使能)置位；发完后必须等 BUSY 清零。这两件事都靠读状态寄存器（命令 0x05）完成：\n· bit0 = BUSY（1 = 忙着）\n· bit1 = WEL（1 = 写使能已置位）",
   "写两个小工具函数：\n① `W25XX_ReadStatus()`，发 0x05 读回 1 字节；\n② `W25XX_WaitBusy()`，循环读状态直到 BUSY=0。\n再写 `W25XX_WriteEnable()`，只发 0x06 命令（没有任何参数）。",
   "static uint8_t W25XX_ReadStatus(void)\n{\n    uint8_t st;\n    GPIO_ResetBits(GPIOA, GPIO_Pin_4);\n    (void)SPI1_TransferByte(0x05);             /* READ STATUS REGISTER 1 */\n    st = SPI1_TransferByte(0xFF);\n    GPIO_SetBits(GPIOA, GPIO_Pin_4);\n    return st;\n}\n\n/* 等它忙完，带超时，芯片坏了也不会死等 */\nstatic uint8_t W25XX_WaitBusy(void)\n{\n    uint32_t to = 300000;                      /* 约 300ms 上限 */\n    while ((W25XX_ReadStatus() & 0x01) != 0)   /* bit0 = BUSY */\n    {\n        if (to-- == 0) { return 1; }           /* 超时 */\n    }\n    return 0;\n}\n\n/* WRITE ENABLE：只发一个命令字节，没有参数、没有返回 */\nstatic void W25XX_WriteEnable(void)\n{\n    GPIO_ResetBits(GPIOA, GPIO_Pin_4);\n    (void)SPI1_TransferByte(0x06);\n    GPIO_SetBits(GPIOA, GPIO_Pin_4);\n}\n\n/* 更稳的写法：发完 WREN 回头确认 WEL 真的置上了。\n *   为什么要确认：如果芯片还在忙、或 CS 时序被打断，WREN 会被静默忽略，\n *   接着发的页写/擦除命令也会被忽略，现象是“写函数返回成功，读回来还是 0xFF”。\n */\nstatic uint8_t W25XX_WriteEnableChecked(void)\n{\n    W25XX_WriteEnable();\n    if ((W25XX_ReadStatus() & 0x02) != 0) { return 0; }   /* bit1 = WEL */\n    W25XX_WriteEnable();                                  /* 再给一次机会 */\n    if ((W25XX_ReadStatus() & 0x02) != 0) { return 0; }\n    return 1;                                             /* 没置上 → 别发写命令 */\n}",
   "在调试器里单步走一遍：\n· 调 `W25XX_WriteEnable()` 之后，`W25XX_ReadStatus()` 的返回值 bit1 应该是 1；\n· 调 `W25XX_WaitBusy()` 时如果芯片空闲，应该立刻返回；\n· 擦一个扇区时再调 WaitBusy，应该能观察到它循环几十毫秒才返回（这正说明等待是有效的）。",
   "写使能是“一次性”的：每发一条页写/擦除命令之前都要重新发一次 0x06，不能发一次管很久。\n忘了等 BUSY 就发下一条命令 → 命令被静默丢弃，但函数返回成功（这才是最问题的：数据没写进去，程序却以为写成功了）。\nWaitBusy 一定要有超时：芯片没焊好/供电不足时会永远忙，没有超时的死循环直接让整个系统假死。\n读状态寄存器本身也要拉 CS：有人以为读状态不用 CS，结果是读到随机值。\n本项目 FWLIB 的 `w25qxx.c` 已经实现了这些（`W25QXX_WaitBusy` 带 100ms 超时、`w25_write_enable_checked` 会回读 WEL），照用即可。",
   "SPI_I2S_SendData SPI_I2S_ReceiveData"
  ],
  [
   "页写：为什么一次最多只能写 256 字节",
   "Flash 内部的页(page) 是 256 字节。写操作有个非常硬性的限制：\n一次页写命令里，如果数据跨越了 256 字节的页边界，地址不会进位到下一页，而是回绕到本页开头覆盖已经写过的数据。\n\n，如果从地址 0x00F0 开始连续写 300 字节，写到最后会绕回 0x00F0 把开头的 32 字节覆盖掉，而且不会报任何错，数据静默损坏。\n所以所有 Flash 驱动的写函数都必须自己拆分：算出“本次最多能写到本页末尾”，写一页、等忙、再写下一段。",
   "写 `W25QXX_Write(const uint8_t *src, uint32_t addr, uint32_t len)`：\n循环里每次都算 `in_page = 256 - (addr & 0xFF)`（本页还剩多少），本次只写 `min(in_page, 剩余)`，写完等 BUSY 再继续。",
   "uint8_t W25QXX_Write(const uint8_t *src, uint32_t addr, uint32_t len)\n{\n    uint32_t done = 0;\n\n    if (src == 0 || len == 0) { return 1; }\n\n    while (done < len)\n    {\n        /* 本次最多能写多少：不能跨 256 字节页边界 */\n        uint16_t in_page = (uint16_t)(256 - ((addr + done) & 0xFF));\n        uint16_t n = (uint16_t)((len - done) > in_page ? in_page : (len - done));\n        uint16_t i;\n\n        /* ① 写使能（每页都要重新发。并确认 WEL 置位） */\n        if (W25XX_WriteEnableChecked() != 0) { return 3; }\n\n        /* ② 页写命令 0x02 + 24 位地址 + 数据 */\n        GPIO_ResetBits(GPIOA, GPIO_Pin_4);\n        (void)SPI1_TransferByte(0x02);                 /* PAGE PROGRAM */\n        (void)SPI1_TransferByte((uint8_t)((addr + done) >> 16));\n        (void)SPI1_TransferByte((uint8_t)((addr + done) >> 8));\n        (void)SPI1_TransferByte((uint8_t)((addr + done)));\n        for (i = 0; i < n; i++) { (void)SPI1_TransferByte(src[done + i]); }\n        GPIO_SetBits(GPIOA, GPIO_Pin_4);     /* CS 拉高才真正开始写。 */\n\n        /* ③ 等它忙完（典型 0.4ms/页） */\n        if (W25XX_WaitBusy() != 0) { return 2; }\n\n        done += n;\n    }\n    return 0;\n}\n\n/* 只写得出 1→0：如果目标区域之前写过（不是 0xFF），必须先擦除整扇区。\n *   擦除后读回来应是全 0xFF。读回验证：\n *       W25QXX_Read(buf, addr, len);   // 应该等于刚写的内容\n */",
   "验证方法（必须做一次，否则不知道自己有没有出错）：\n① 擦一个扇区（应读回全 0xFF）；\n② 从非 256 对齐的地址开始写 300 字节（比如 addr = 0x1000 + 0xF0）；\n③ 读回比对，全部一致才说明跨页拆分写对了。\n如果不拆分就写，会在页边界处发现前 16 字节被覆盖。",
   "漏了写使能 → 页写命令被忽略，函数返回成功但数据没写进去（读回还是 0xFF）。这是 Flash 驱动最常见的 bug。\nCS 拉高的时机：拉低 CS→发命令/地址/数据→拉高 CS 才开始真正写入。有人写完数据不等 CS 拉高就急着读状态，会读到“不忙”（还没开始）。\n等 BUSY 用错位置：一定要在 CS 拉高之后再等。\n先写后擦 → 擦除会把刚写的数据一起清掉。正确顺序永远是：擦扇区 → 写数据。\n本项目 FWLIB 的 `W25QXX_Write` 已实现跨页拆分（`in_page = PAGE_SIZE - ((addr+done) & (PAGE_SIZE-1))`），并且会回读校验 WEL。",
   "SPI_I2S_SendData SPI_I2S_ReceiveData"
  ],
  [
   "扇区擦除：4KB 是最小单位，别拿它当“擦一个字节”",
   "Flash 的擦除粒度就是扇区（4KB），没有更小的了。\n这意味着两件事，都必须提前想清楚：\n① 想改一个字节，得把整个 4KB 读出来 → 改那一个字节 → 擦掉整个扇区 → 把 4KB 全部写回去（这叫“读-改-写”，代价是 4KB 的 RAM 缓冲 + 一次完整的擦写）；\n② 擦除会连带抹掉同扇区里所有别的数据。如果参数只占 16 字节，却和日志共用同一个扇区，那么每次存参数都会把日志开头 4KB 抹掉。\n\n更硬的一点：给非 4KB 对齐的地址发擦除命令，芯片会擦掉整个扇区（只看地址高位），而且不报错。所以驱动里必须自己拦对齐错误。",
   "写 `W25QXX_EraseSector(uint32_t addr)`：\n① 先检查地址 4KB 对齐（不对齐直接返回错误，不要默默擦除）；\n② 等 BUSY 清零；③ 发写使能并确认 WEL；④ 发 0x20 + 24 位地址；⑤ 拉高 CS；⑥ 等 BUSY（典型 45ms、最长 400ms）。",
   "uint8_t W25QXX_EraseSector(uint32_t addr)\n{\n    /* ① 对齐校验：给非对齐地址芯片会擦掉整个扇区，必须拦住 */\n    if ((addr & 0x0FFF) != 0) { return 1; }\n    if (addr >= (16UL * 1024UL * 1024UL)) { return 1; }\n\n    /* ② 写使能并确认 WEL（否则擦除命令被静默忽略） */\n    if (W25XX_WriteEnableChecked() != 0) { return 3; }\n\n    /* ③ 发命令前确认不忙 */\n    if (W25XX_WaitBusy() != 0) { return 2; }\n\n    GPIO_ResetBits(GPIOA, GPIO_Pin_4);\n    (void)SPI1_TransferByte(0x20);                      /* SECTOR ERASE 4KB */\n    (void)SPI1_TransferByte((uint8_t)(addr >> 16));\n    (void)SPI1_TransferByte((uint8_t)(addr >> 8));\n    (void)SPI1_TransferByte((uint8_t)(addr));\n    GPIO_SetBits(GPIOA, GPIO_Pin_4);\n\n    /* ④ 擦除很慢：典型 45ms，最坏 400ms。这段时间 CPU 是死等的，\n     *    调用前建议先喂狗，并放在低优先级任务里。\n     */\n    return W25XX_WaitBusy();\n}\n\n/* ---- 判断一段区域是不是空的（应为全 0xFF） ---- */\nuint8_t W25QXX_IsBlank(uint32_t addr, uint32_t len)\n{\n    uint8_t buf[32];\n    while (len > 0)\n    {\n        uint32_t n = (len > sizeof(buf)) ? (uint32_t)sizeof(buf) : len;\n        uint32_t i;\n        /* 读一块判一块 */\n        /* W25QXX_Read(buf, addr, n); */\n        for (i = 0; i < n; i++) { if (buf[i] != 0xFF) { return 0; } }\n        addr += n;  len -= n;\n    }\n    return 1;\n}",
   "① 擦一个扇区前先读一遍（记录内容）→ 擦除 → 再读一遍，应该全是 0xFF；\n② 在一个扇区偏移 0 处写几个字节，再在偏移 0x800（同扇区）写几个字节，然后只擦扇区起始 → 两处都变 0xFF（这验证了“擦除的粒度是整扇区”这个认知）。",
   "最危险的问题：把“擦一个扇区”当成“擦一个字节”用。本项目板2 的日志环形缓冲就踩过这个问题，为了腾出 1 个槽位擦掉整个 4KB，把同扇区里另外 63 条尚未补传的记录一起销毁了。\n  正确做法是：要么把扇区切得更细来规划，要么在擦除时同步修正计数并记录“丢了多少条”。\n擦除期间 CPU 从 Flash 取指会被硬件暂挂（同一条 Flash 总线），看门狗可能被咬，所以擦除前先喂狗、或把擦除放在低优先级任务里。\n地址对齐不只是“看起来整齐”：非对齐地址会让芯片擦掉整扇区，不报错。\n整片擦除(CHIP ERASE 0xC7)要 20~100 秒，期间不要按复位，也别指望看门狗不咬。",
   "SPI_I2S_SendData SPI_I2S_ReceiveData"
  ],
  [
   "算容量、分配扇区、加总线互斥，从“能读写”到“能用”",
   "读写通了只是第一步。真正用到工程里还要做三件事：\n\n① 规划地址空间。 16MB 的 Flash 有 4096 个扇区。要提前定好：哪一段放参数（配置文件）、哪一段放日志（断网缓存）、哪一段留作升级。混用会让“存参数”把“日志”擦掉。\n本项目板2 的规划是：参数区从 0x000000 起，日志区从 0x001000 起、占 16 个扇区（64KB）、槽位 64 字节 → 共 1024 条记录。\n\n② 处理多任务并发。 如果采集任务在写 Flash、补传任务同时在读 Flash，两个任务会互相打断 SPI 事务（CS 被对方拉高、字节流错位），结果是随机数据损坏。必须用互斥量把“一整次读写操作”包起来。\n\n③ 估算寿命与耗时。 Flash 每个扇区约 10 万次擦写。按“每 10 秒写一条、每天 8640 条、每条占 1/64 个扇区”估算，也要经常检查写入策略。",
   "① 在头文件里用宏定义好各分区的基址与大小；\n② 所有“读一整条记录”/“写一整条记录”的入口函数都加互斥保护；\n③ 加一个“溢出计数”，一旦因为环形回绕丢了数据，能被上层看到并报警（而不是静默丢失）。",
   "/* ---- ① 分区规划（写进头文件，别散落在各处） ---- */\n#define FLASH_PARAM_ADDR     0x000000UL          /* 参数区：扇区 0~0 */\n#define FLASH_LOG_ADDR       0x001000UL          /* 日志区：从扇区 1 开始 */\n#define FLASH_LOG_SECTORS    16UL                /* 独占 16 个扇区 = 64KB */\n#define FLASH_SECTOR_SIZE    4096UL\n#define FLASH_PAGE_SIZE      256UL\n#define FLASH_SLOT_SIZE      64UL                /* 每条日志占 64 字节 */\n#define FLASH_LOG_CAPACITY   ((FLASH_LOG_SECTORS * FLASH_SECTOR_SIZE) / FLASH_SLOT_SIZE)  /* 1024 条 */\n\n/* ---- ② 用互斥量包住“一整次操作”（FreeRTOS 下） ---- */\n#include \"FreeRTOS.h\"\n#include \"semphr.h\"\n\nstatic SemaphoreHandle_t s_spi_mutex = NULL;\n\nstatic void spi_lock_init(void)\n{\n    s_spi_mutex = xSemaphoreCreateMutex();\n}\n\n/* 整条记录写入：拿锁 → 写 → 放锁。锁的范围要盖住 CS 拉低到拉高的全过程 */\nuint8_t log_write_safe(uint32_t ts, const uint8_t *data, uint16_t len)\n{\n    uint8_t ret;\n\n    if (s_spi_mutex != NULL) { (void)xSemaphoreTake(s_spi_mutex, portMAX_DELAY); }\n\n    /* ……这里做真正的 Flash 写（擦扇区判断、页写、写状态字）…… */\n    ret = 0;\n\n    if (s_spi_mutex != NULL) { (void)xSemaphoreGive(s_spi_mutex); }\n    return ret;\n}\n\n/* ---- ③ 溢出计数：丢了数据要让上层知道 ---- */\nstatic uint16_t s_lost = 0;\n\n/* 环形缓冲回绕、不得不擦掉未补传记录时：s_lost++ */\nuint16_t log_lost_count(void)\n{\n    return s_lost;    /* 只要 >0，就说明缓存已经在丢数据：断网太久或补传太慢 */\n}",
   "① 打印分区表，核对每个区的起止地址没有重叠；\n② 写一个并发压力测试：一个任务不停写日志、另一个任务不停读日志，跑 10 分钟，检查读出的记录 CRC 是否全部正确（这正是验证互斥量有没有生效的方法）；\n③ 把 `log_lost_count()` 一起打印出来，正常情况下应该恒为 0。",
   "不用互斥量的后果并非“偶尔出错”，而是随机损坏：SPI 事务被中途打断后，从机可能停在“以为还在收地址”的状态，后续所有数据都错位。\n锁的范围要够大：必须包住从 CS 拉低到 CS 拉高的完整事务。只包住“发一个字节”是没用的。\n无 RTOS 的裸机工程里，用 `taskENTER_CRITICAL()` 或直接关中断代替（但要别关太久，会影响串口接收）。\n环形缓冲一定要有溢出策略：宁可“拒绝新数据 + 报警”，也不要“默默覆盖未发送的旧数据”。本项目原来的实现是后者，擦掉整个扇区导致 63 条待补传记录消失，加 `s_lost` 计数就是为了让这种丢失可见。\n别忘了 `W25QXX_LogLost()` 这类接口要暴露到应用层，否则计数加了也没人看。",
   "W25QXX_Init W25QXX_LogInit W25QXX_LogWrite W25QXX_LogRead"
  ]
 ],
 k:"SPI 引导 向导 教程 步骤 SPI1 初始化 开时钟 配引脚 复用 推挽 JTAG PB3 PB4 CPOL CPHA 模式0 SPI_Init SPI_Cmd 收发一个字节 全双工 片选 CS W25QXX W25Q128 GD25Q128 JEDEC ID 0xC84018 0xEF4018 状态寄存器 BUSY WEL 写使能 页写 256 跨页 扇区擦除 4KB 对齐 读改写 分区 互斥 并发 排错 不通 全FF 全00"},

/* ===================== ⑭ 定时器输出比较（OC）引导 ===================== */
{m:"引导", t:"guide", n:"🧭 定时器输出比较 OC 引导（PWM ≠ 输出比较，别混）",
 d:"把“输出比较”和“PWM”彻底分清，然后做出三种实用东西：等间隔精确中断、宽度精确的单次脉冲、运行期切换输出模式。",
 g:"STM32F407 · TIM2/TIM3 通用定时器 · APB1=84MHz(TIM 定时器时钟) · 对应 FWLIB 的 sys_tim.c",
 k:"定时器 输出比较 OC 引导 向导 教程 步骤 PWM 区别 单脉冲 one shot 精确定时 TIM_OCMode Timing Toggle Active Inactive 强制输出 TIM_SelectOCxM TIM_ForcedOC 中断 清标志 CCR ARR PSC 移相 步进电机 超声触发 比较匹配",
 e:"/* ============================================================\n *  定时器输出比较（OC）三种典型用法 *   按「定时器输出比较 OC 引导」搭出来的成品\n *  平台：STM32F407 · TIM3(等间隔中断) / TIM2(单脉冲 + 模式切换)\n * ============================================================ */\n#include \"stm32f4xx.h\"\n\nstatic volatile uint32_t g_tick_100us = 0;\n\n/* ---------- 用法 A：纯定时中断（引脚不动） ---------- */\nvoid OC_Timing_Init_100us(void)\n{\n    TIM_TimeBaseInitTypeDef tb;\n    TIM_OCInitTypeDef       oc;\n    NVIC_InitTypeDef        ni;\n\n    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);\n\n    tb.TIM_Prescaler         = 84 - 1;      /* 84MHz/84 = 1MHz = 1µs */\n    tb.TIM_CounterMode       = TIM_CounterMode_Up;\n    tb.TIM_Period            = 100 - 1;     /* 100µs 一个周期 */\n    tb.TIM_ClockDivision     = TIM_CKD_DIV1;\n    tb.TIM_RepetitionCounter = 0;\n    TIM_TimeBaseInit(TIM3, &tb);\n\n    oc.TIM_OCMode      = TIM_OCMode_Timing;          /* 只置标志，不碰引脚 */\n    oc.TIM_OutputState = TIM_OutputState_Disable;    /* 引脚输出关掉 */\n    oc.TIM_Pulse       = 50;                         /* 周期中点触发 */\n    oc.TIM_OCPolarity  = TIM_OCPolarity_High;\n    TIM_OC1Init(TIM3, &oc);\n\n    TIM_ITConfig(TIM3, TIM_IT_CC1, ENABLE);\n\n    ni.NVIC_IRQChannel                   = TIM3_IRQn;\n    ni.NVIC_IRQChannelPreemptionPriority = 5;        /* FreeRTOS 红线 ≥5 */\n    ni.NVIC_IRQChannelSubPriority        = 0;\n    ni.NVIC_IRQChannelCmd                = ENABLE;\n    NVIC_Init(&ni);\n\n    TIM_Cmd(TIM3, ENABLE);\n}\n\n/* 通用定时器：所有通道共用一个中断函数 */\nvoid TIM3_IRQHandler(void)\n{\n    if (TIM_GetITStatus(TIM3, TIM_IT_CC1) != RESET)\n    {\n        TIM_ClearITPendingBit(TIM3, TIM_IT_CC1);     /* 必须清 */\n        g_tick_100us++;\n        TIM_SetCompare1(TIM3, (uint16_t)(TIM_GetCounter(TIM3) + 50));\n    }\n}\n\n/* ---------- 用法 B：单次精确脉冲 ---------- */\nvoid OC_OneShot_Init(void)      /* TIM2_CH1 = PA0 */\n{\n    GPIO_InitTypeDef        g;\n    TIM_TimeBaseInitTypeDef tb;\n    TIM_OCInitTypeDef       oc;\n\n    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);\n    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,  ENABLE);\n\n    GPIO_PinAFConfig(GPIOA, GPIO_PinSource0, GPIO_AF_TIM2);\n    g.GPIO_Pin   = GPIO_Pin_0;\n    g.GPIO_Mode  = GPIO_Mode_AF;\n    g.GPIO_OType = GPIO_OType_PP;\n    g.GPIO_Speed = GPIO_Speed_100MHz;\n    g.GPIO_PuPd  = GPIO_PuPd_UP;\n    GPIO_Init(GPIOA, &g);\n\n    tb.TIM_Prescaler         = 84 - 1;      /* 1µs */\n    tb.TIM_CounterMode       = TIM_CounterMode_Up;\n    tb.TIM_Period            = 0xFFFF;\n    tb.TIM_ClockDivision     = TIM_CKD_DIV1;\n    tb.TIM_RepetitionCounter = 0;\n    TIM_TimeBaseInit(TIM2, &tb);\n\n    oc.TIM_OCMode      = TIM_OCMode_Active;          /* 到 CCR 引脚变高 */\n    oc.TIM_OutputState = TIM_OutputState_Enable;\n    oc.TIM_Pulse       = 10;\n    oc.TIM_OCPolarity  = TIM_OCPolarity_High;\n    TIM_OC1Init(TIM2, &oc);\n    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Disable);   /* 运行期改 CCR 立即生效 */\n\n    TIM_Cmd(TIM2, ENABLE);\n}\n\nvoid Pulse_Once(uint16_t width_us)\n{\n    TIM_SetCounter(TIM2, 0);\n    TIM_SetCompare1(TIM2, width_us);\n    TIM_Cmd(TIM2, ENABLE);\n    while (TIM_GetCounter(TIM2) < width_us) { }\n    TIM_Cmd(TIM2, DISABLE);\n    TIM_SetCounter(TIM2, 0);\n    GPIO_ResetBits(GPIOA, GPIO_Pin_0);\n}\n\n/* ---------- 用法 C：运行期切换模式 / 强制输出 ---------- */\nvoid OC_SwitchDemo(void)\n{\n    TIM_SelectOCxM(TIM2, TIM_Channel_1, TIM_OCMode_Toggle);   /* 运行期改模式用这个 */\n    TIM_CCxCmd(TIM2, TIM_Channel_1, TIM_CCx_Enable);\n\n    TIM_ForcedOC1Config(TIM2, TIM_ForcedAction_Active);       /* 立刻高 */\n    TIM_ForcedOC1Config(TIM2, TIM_ForcedAction_Inactive);     /* 立刻低 */\n}\n\n/* 本项目 FWLIB：sys_tim.c 有 SYS_TIM_* 封装（PWM 通道 + 频率调整），\n *   但输出比较/单脉冲的清封还没做，要用就照本卡加，\n *    NVIC 优先级 ≥5，且中断函数名别和库里 __weak 的撞。\n */",
 f:[
  [
   "先分清 PWM 与输出比较：一个是“周期性输出”，一个是“到点做动作”",
   "很多人把这两个当同一件事，结果想要“发一个单脉冲”时怎么调 PWM 都不对。\n\n输出比较(OC)是硬件机制：定时器计数器 CNT 一直在跑，当它等于比较寄存器 CCR 时，硬件做一个动作，这个动作由 `TIM_OCMode` 决定：\n· `TIM_OCMode_Timing`：只把标志位置起来（引脚完全不动），用来做精确定时中断；\n· `TIM_OCMode_Active` / `Inactive`：把引脚强制拉成有效/无效电平（一次，不是持续）；\n· `TIM_OCMode_Toggle`：把引脚电平翻转一次；\n· `TIM_OCMode_PWM1/PWM2`：在 CNT<CCR 期间保持一种电平、CNT>CCR 期间保持另一种，这才是 PWM。\n\n所以PWM 只是输出比较的一种特殊用法：它靠“ARR 决定周期、CCR 决定占空比”实现“自动重复”。而输出比较本身是“一次动作”，可以做单脉冲、移相、精确时刻触发。",
   "在脑子里（或纸上）过一遍：我要的是“持续不断的波形”，还是“到某个时刻做一件事”？\n· 持续波形 → 用 PWM（找「定时器 PWM 输出」那张卡）；\n· 精确时刻/单次/翻转/中断 → 用输出比较（本卡）。\n先把这个判断做对，能省掉大量试错。",
   "/* 一张表把四种模式的差别钉死：\n *\n *  OCMode            到 CNT==CCR 时做什么            引脚         典型用途\n *  ---------------------------------------------------------------------------\n *  Timing            只置 CCxIF 标志，什么也不做      不动         精确定时、软件计时\n *  Active            引脚变“有效电平”（通常高）       变一次       单次脉冲、超声触发\n *  Inactive          引脚变“无效电平”（通常低）       变一次       脉冲收尾、软件控电平\n *  Toggle            引脚电平翻转                     翻一次       单脉冲、移相方波、步进电机\n *  PWM1 / PWM2       在 CNT<CCR 与 CNT>CCR 间保持不同电平  持续      调光、调速、蜂鸣器\n *\n * PWM 是“会自己重复”的输出比较。\n */",
   "能对着这张表说出“我要发一个 10µs 的超声触发脉冲”该选哪个模式（答案：Active 或 Toggle + 中断里关掉定时器）。\n说不出来就再看一遍 why。",
   "最常见的错误认知：“输出比较模式会让引脚输出一个脉冲然后就停”，不会自动停。定时器还在跑，CNT 会再次穿过 CCR，所以会周期性重复。要“只发一次”，必须配合单脉冲模式或手动 `TIM_Cmd(DISABLE)`。\n另一个误解：以为 OCMode 可以在运行时随便改。改结构体再调 TIM_OCxInit 是错的（初始化函数会把通道重新配置一遍，可能影响正在输出的波形），运行时必须用 `TIM_SelectOCxM`。",
   "TIM_OCInitTypeDef TIM_OC1Init TIM_SelectOCxM"
  ],
  [
   "配时基：PSC 和 ARR 决定“时间分辨率”与“最长周期”",
   "定时器的时间 = 时钟 ÷ (PSC+1)，然后每来一个这样的“tick”计数加 1；数到 ARR 就溢出（向上计数模式）。\n两个寄存器各管一头：\n· PSC 决定 1 个 tick 是多少时间（分辨率），PSC=83 时，84MHz/84 = 1MHz，即 1 tick = 1µs；\n· ARR 决定数多少个 tick 溢出（周期上限），ARR=999 时，周期 = 1000µs。\n\n STM32 的定时器时钟有个特点：挂在 APB1 上的定时器（TIM2~5、TIM6~7 等），如果 APB1 预分频不是 1，定时器时钟会是 APB1 的两倍。本项目 APB1 = 42MHz，所以 TIM2~5 的时钟是 84MHz，配 PSC 时要按 84MHz 算，不是 42MHz。",
   "按公式算一遍：`1 tick 时间 = 1 / (TIMxCLK / (PSC+1))`。\n想 1µs 一个 tick 就用 PSC=83（TIMxCLK=84MHz）。\n想 100µs 溢出一次，就把 ARR 设成 99（100 个 tick）。",
   "void OC_TimeBase_Config(void)\n{\n    TIM_TimeBaseInitTypeDef tb;\n\n    /* TIM2~5 挂在 APB1 上，但时钟是 APB1 的 2 倍。\n     *   本项目 APB1 = 42MHz → TIM2~5 的时钟 = 84MHz\n     *   （TIM1/TIM8 在 APB2 上，时钟 = 168MHz） */\n\n    tb.TIM_Prescaler         = 84 - 1;      /* 84MHz / 84 = 1MHz → 1 tick = 1µs */\n    tb.TIM_CounterMode       = TIM_CounterMode_Up;\n    tb.TIM_Period            = 100 - 1;     /* 数 100 个 tick = 100µs 溢出一次 */\n    tb.TIM_ClockDivision     = TIM_CKD_DIV1;/* 仅影响数字滤波器采样，与计数无关 */\n    tb.TIM_RepetitionCounter = 0;           /* 只有高级定时器 TIM1/8 用得到 */\n    TIM_TimeBaseInit(TIM3, &tb);\n\n    /* 运行期改 ARR 要开影子寄存器预装载，否则改动可能在本周期中途生效 */\n    TIM_ARRPreloadConfig(TIM3, ENABLE);\n}\n\n/* 算周期的小抄：周期(µs) = (PSC+1) * (ARR+1) / TIMxCLK(MHz)\n *   要 1ms @84MHz：PSC=83, ARR=999\n *   要 10ms @84MHz：PSC=839, ARR=999\n *   要 1s   @84MHz：PSC=8399, ARR=9999\n */",
   "① 在调试器里看 `TIM3->PSC` 和 `TIM3->ARR` 是否等于算的值；\n② 开定时器后用秒表/示波器粗测中断周期（本步还没开中断，所以先看 CNT 在动：连续两次读 `TIM_GetCounter(TIM3)` 应该有变化）。",
   "最大的问题：把 APB1 的 42MHz 当成 TIM2~5 的时钟（实际是 84MHz）。用错的话所有时间都差 2 倍。规律记牢：APB 预分频 ≠ 1 时，定时器时钟 = APBx 时钟 × 2。\nPSC 和 ARR 都是 16 位（0~65535），STM32F407 上只有 TIM2/TIM5 是 32 位。想要很长的定时要级联或多次计数。\n改 ARR 一定要先 `TIM_ARRPreloadConfig(ENABLE)`，否则改大的值时可能立刻溢出、周期乱掉。\n`TIM_ClockDivision` 容易被误解：它不是用来分频计数的，只影响输入滤波器的采样时钟。",
   "TIM_TimeBaseInitTypeDef TIM_TimeBaseInit TIM_ARRPreloadConfig"
  ],
  [
   "配输出比较通道：OCMode 选对，OutputState 决定引脚动不动",
   "通道配置里最容易搞混的是 `TIM_OutputState` 和 `TIM_OCMode` 的关系：\n· `TIM_OutputState_Disable` → 引脚被断开，无论 OCMode 是什么，引脚都没波形；\n· `TIM_OutputState_Enable` → 引脚连接上，具体波形由 OCMode 决定。\n\n所以做“纯定时中断”时，正确写法是 `OCMode = Timing` 并且 `OutputState = Disable`（双保险，防止改模式时误输出）。\n做单脉冲时则 `OCMode = Active/Toggle` + `OutputState = Enable`。\n\n`TIM_Pulse(CCR)` 是第一次比较值；`TIM_OCPolarity` 决定“有效电平”是高还是低，它不只影响输出，`Toggle` 模式下极性没有意义（翻转就是翻转）。",
   "填 `TIM_OCInitTypeDef` 并调用 `TIM_OCxInit`（通道 1 用 TIM_OC1Init、通道 2 用 TIM_OC2Init，以此类推）。\n然后按需要开预装载：想在运行期平滑改 CCR 就 `TIM_OCxPreloadConfig(ENABLE)`；想让改动立即生效就 `DISABLE`。",
   "void OC_Channel_Config(void)\n{\n    TIM_OCInitTypeDef oc;\n\n    /* ===== 用法 A：纯定时（引脚不动） ===== */\n    oc.TIM_OCMode      = TIM_OCMode_Timing;\n    oc.TIM_OutputState = TIM_OutputState_Disable;    /* 引脚断开 */\n    oc.TIM_Pulse       = 50;                         /* 第一次比较点 */\n    oc.TIM_OCPolarity  = TIM_OCPolarity_High;\n    TIM_OC1Init(TIM3, &oc);\n\n    /* ===== 用法 B：单脉冲（到点翻转引脚） ===== */\n    oc.TIM_OCMode      = TIM_OCMode_Toggle;\n    oc.TIM_OutputState = TIM_OutputState_Enable;     /* 引脚接上 */\n    oc.TIM_Pulse       = 100;\n    oc.TIM_OCPolarity  = TIM_OCPolarity_High;        /* Toggle 模式下无实际意义 */\n    TIM_OC1Init(TIM2, &oc);\n\n    /* 预装载的意义：\n     *   ENABLE  → 新写的 CCR 在下一个更新事件才生效（波形不抖，适合连续调占空比）\n     *   DISABLE → 新写的 CCR 立即生效（适合单脉冲这种一次性动作）\n     */\n    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Disable);\n\n    /* 运行期想改模式，只能用这个函数（不要改结构体再 Init）：\n     *   TIM_SelectOCxM(TIM2, TIM_Channel_1, TIM_OCMode_Timing);\n     */\n}\n\n/* 高级定时器 TIM1/TIM8 额外一步：不开主输出引脚没波形\n *   TIM_CtrlPWMOutputs(TIM1, ENABLE);\n *   通用定时器 TIM2~5 不需要这一步。\n */",
   "① 用法 A（Timing + OutputState_Disable）：用示波器量对应引脚，应该完全没有波形，如果量到方波，说明 OutputState 或 OCMode 配错了；\n② 用法 B（Toggle + Enable）：量引脚应该能看到电平在 CCR 时刻翻转。",
   "`OutputState_Disable` + `OCMode_PWM1` 是最隐蔽的组合：波形出不来，但代码看起来没错。\n高级定时器 TIM1/TIM8 忘了 `TIM_CtrlPWMOutputs(ENABLE)` → 引脚一直没波形（这是 TIM1 与 TIM2 最本质的使用差别）。\n运行期改模式用 `TIM_SelectOCxM`，不要改结构体再调 `TIM_OCxInit`：后者会把整个通道重新配置，正在输出的波形会中断，还可能把预装载等设置一并重置。\n通道与引脚的对应关系是固定的（TIM3_CH1=PA6/PB4/PC6 等多个可选），要用 `GPIO_PinAFConfig` 选对功能号，别只看引脚名。",
   "TIM_OCInitTypeDef TIM_OC1Init TIM_SelectOCxM TIM_OC1PreloadConfig TIM_CtrlPWMOutputs"
  ],
  [
   "开中断：通用定时器所有通道共用一个中断入口",
   "要用输出比较做“到点了通知我干活”，就得开比较中断。\n这里有两个必须的点：\n\n① 中断函数名。 通用定时器 TIM2~TIM5 的中断向量是每个定时器一个，叫 `TIMx_IRQHandler`，不分通道。 TIM3 的 CH1/CH2/CH3/CH4 触发的中断都进 `TIM3_IRQHandler`，需要在函数里分别判断是哪个通道。\n（高级定时器 TIM1/TIM8 结构更细：有 `TIM1_UP_TIM10_IRQHandler`、`TIM1_CC_IRQHandler` 等，捕获/比较有专用向量）\n\n② 必须清标志。 不清的话中断标志一直是 1，出中断后立刻又进去，CPU 100% 被占满，看起来像“死机”。",
   "① `TIM_ITConfig(TIMx, TIM_IT_CCx, ENABLE)` 打开通道比较中断；\n② 配 NVIC（优先级 ≥5）；\n③ 写中断函数，第一件事就是判断并清标志。",
   "void OC_IT_Config(void)\n{\n    NVIC_InitTypeDef ni;\n\n    /* ① 打开通道 1 的比较中断 */\n    TIM_ITConfig(TIM3, TIM_IT_CC1, ENABLE);\n\n    /* ② NVIC：数值必须 ≥ configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5) */\n    ni.NVIC_IRQChannel                   = TIM3_IRQn;\n    ni.NVIC_IRQChannelPreemptionPriority = 5;\n    ni.NVIC_IRQChannelSubPriority        = 0;\n    ni.NVIC_IRQChannelCmd                = ENABLE;\n    NVIC_Init(&ni);\n\n    TIM_Cmd(TIM3, ENABLE);\n}\n\n/* 通用定时器：一个定时器只有一个中断入口，所有通道都在这里处理 */\nvoid TIM3_IRQHandler(void)\n{\n    /* 第一件事永远是判断 + 清标志 */\n    if (TIM_GetITStatus(TIM3, TIM_IT_CC1) != RESET)\n    {\n        TIM_ClearITPendingBit(TIM3, TIM_IT_CC1);    /* 不清会反复进。 */\n        g_tick_100us++;\n        /* 自动重装下一次比较点：形成严格等间隔的中断 */\n        TIM_SetCompare1(TIM3, (uint16_t)(TIM_GetCounter(TIM3) + 50));\n    }\n\n    /* 如果还开了通道 2，在这里继续判 */\n    if (TIM_GetITStatus(TIM3, TIM_IT_CC2) != RESET)\n    {\n        TIM_ClearITPendingBit(TIM3, TIM_IT_CC2);\n        /* ... */\n    }\n}\n\n/* 高级定时器 TIM1 的向量名不同（容易写错）：\n *   TIM1_BRK_TIM9_IRQHandler\n *   TIM1_UP_TIM10_IRQHandler      ← 更新中断在这里\n *   TIM1_TRG_COM_TIM11_IRQHandler\n *   TIM1_CC_IRQHandler            ← 捕获/比较中断在这里\n */",
   "① 在中断里对一个全局计数器 `g_tick_100us++`，主循环每 500ms 打印一次，应该稳定增长（约 +5000/次）；\n② 如果增长得飞快（比如每秒几万次），基本可以断定是忘了清标志；\n③ 如果完全不增长，检查 NVIC 是否使能、`TIM_Cmd` 是否调了、中断函数名是否拼错（拼错不会报错，函数被静默忽略）。",
   "忘了 `TIM_ClearITPendingBit` → 出中断立刻再进，CPU 被占满。这是输出比较/捕获中断的第一号问题。\n中断函数名拼错：比如写成 `TIM3_IRQhandler`（小写 h），编译器不报错，函数永远不被调用。这是最浪费时间的错误之一，建议直接从中断向量表的 startup 文件里复制名字。\n优先级 <5 时在中断里调 FreeRTOS 的 `xxxFromISR` 会破坏内核（本项目 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`）。\n在 RTOS 下，中断里只应做“置标志/发信号量”，繁重工作交给任务。\n中断里更新 CCR 时如果开了预装载，新值要等下一个更新事件才生效。",
   "TIM_ITConfig TIM_ClearITPendingBit TIM_GetITStatus TIM_SetCompare1"
  ],
  [
   "做“等间隔精确中断”（用法 A），比 RTOS 节拍更细的时基",
   "有了输出比较，就能做出任意精度的周期中断：让 CNT 每跑到 100 就跑一次中断，就在中断里把下一个比较点设成“当前 CNT + 100”。\n这比用 ARR 溢出中断好在：比较点可以随时改（改 CCR 就行），不用重配整个时基；而且可以实现不均匀的触发序列（比如先 100µs、再 250µs）。\n\n典型用途：按固定时间间隔采样 ADC、给步进电机发等间隔脉冲、做软件 PLL。",
   "在中断里用 `TIM_SetCompare1(TIM3, TIM_GetCounter(TIM3) + step)` 自动推开下一次比较点。\n用 `current + step` 而不是直接写固定值，这样能自动跟上（避免累积误差）。",
   "static volatile uint32_t g_tick_100us = 0;\n\nvoid TIM3_IRQHandler(void)\n{\n    if (TIM_GetITStatus(TIM3, TIM_IT_CC1) != RESET)\n    {\n        TIM_ClearITPendingBit(TIM3, TIM_IT_CC1);\n\n        g_tick_100us++;\n\n        /* 关键：下一次比较点 = 当前计数 + 步长\n         *   用“当前值 + step”而不是固定值，可以避免累积误差、\n         *   也不会因为中断响应延迟而漏掉比较点。\n         */\n        TIM_SetCompare1(TIM3, (uint16_t)(TIM_GetCounter(TIM3) + 50));\n\n        /* 在 RTOS 下：这里只置标志/发信号量 */\n        /* BaseType_t hpw = pdFALSE;\n         * xSemaphoreGiveFromISR(g_tickSem, &hpw);\n         * portYIELD_FROM_ISR(hpw);\n         */\n    }\n}\n\n/* 溢出保护：CNT 是 16 位，current + 50 超过 65535 会回绕。\n *   由于比较是“相等比较”，回绕后仍能正确匹配（只要步长 < ARR），\n *   但要把 ARR 设成 0xFFFF 让 CNT 一直自由跑，不要让它频繁溢出重置。\n */",
   "① 主循环里每 1 秒打印一次 `g_tick_100us`，每次应该增加约 10000（因为 100µs 一次）；\n② 用示波器在另一个 GPIO 上做翻转（中断里 GPIO_ToggleBits），能看到非常稳定的 100µs 方波；\n③ 与 `SYS_TICK_GetTick()` 对照，长时间运行不应有可观测的漂移。",
   "`TIM_SetCompare1` 传的是下一个比较点，不是“增量”。写成 `TIM_SetCompare1(TIM3, 50)` 会变成“每次都在 CNT 到达 50 时触发”，而不是等间隔。\n中断里做的事要尽量少：中断响应延迟会直接变成比较点的抖动。\n如果步长小于中断处理本身耗时，会“追不上”，表现为周期被拉长。这时要么加大步长，要么用 DMA 触发。\nRTOS 下别忘了 `portYIELD_FROM_ISR`，否则信号量发出后不能立刻切换到等待任务。",
   "TIM_SetCompare1 TIM_GetCounter TIM_ClearITPendingBit"
  ],
  [
   "做“单次精确脉冲”（用法 B），超声触发、步进电机移一步",
   "单次脉冲的核心是：让定时器只跑一趟就停。有两条路：\n\n路子一（手动控制）：`TIM_SetCounter` 归零 → 设好 CCR → `TIM_Cmd(ENABLE)` → 等 CNT 跑过 CCR（引脚动作）→ `TIM_Cmd(DISABLE)` → 拉低引脚收尾。\n优点是灵活（宽度任意、随时可发）；缺点是“等待”这段是忙等（RTOS 下要小心）。\n\n路子二（硬件单脉冲）：`TIM_SelectOnePulseMode(TIMx, TIM_OPMode_Single)`。硬件到点自动停，不需要软件等待，精度最高。缺点是要提前配好时基（脉冲宽度靠 ARR/CCR 决定）。",
   "先用路子一实现，因为它直观、可控。\n实现时三点：① 每次发之前先 `TIM_SetCounter(TIM2, 0)` 归零；② 发完把引脚拉低；③ 用 `TIM_OCxPreloadConfig(DISABLE)` 保证改 CCR 立即生效。",
   "/* ---- 单脉冲初始化（TIM2_CH1 = PA0，1 tick = 1µs） ---- */\nvoid OC_OneShot_Init(void)\n{\n    GPIO_InitTypeDef        g;\n    TIM_OCInitTypeDef       oc;\n\n    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);\n    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,  ENABLE);\n\n    GPIO_PinAFConfig(GPIOA, GPIO_PinSource0, GPIO_AF_TIM2);\n    g.GPIO_Pin   = GPIO_Pin_0;\n    g.GPIO_Mode  = GPIO_Mode_AF;\n    g.GPIO_OType = GPIO_OType_PP;\n    g.GPIO_Speed = GPIO_Speed_100MHz;\n    g.GPIO_PuPd  = GPIO_PuPd_UP;\n    GPIO_Init(GPIOA, &g);\n\n    /* 时基已在别的函数里配好（PSC=83 → 1µs） */\n\n    oc.TIM_OCMode      = TIM_OCMode_Active;          /* 到 CCR 引脚变高 */\n    oc.TIM_OutputState = TIM_OutputState_Enable;\n    oc.TIM_Pulse       = 10;\n    oc.TIM_OCPolarity  = TIM_OCPolarity_High;\n    TIM_OC1Init(TIM2, &oc);\n\n    /* 关预装载：改 CCR 立即生效（单脉冲要的是“立刻”） */\n    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Disable);\n\n    TIM_Cmd(TIM2, ENABLE);\n}\n\n/* ---- 发一个宽度 width_us 的脉冲 ---- */\nvoid Pulse_Once(uint16_t width_us)\n{\n    TIM_Cmd(TIM2, DISABLE);              /* 先停，保证从确定状态开始 */\n    TIM_SetCounter(TIM2, 0);             /* 归零，否则 CNT 可能已经跑过 CCR */\n    TIM_SetCompare1(TIM2, width_us);     /* 设宽度 */\n    TIM_Cmd(TIM2, ENABLE);               /* 开跑 */\n\n    /* 等它跑过 CCR（引脚已变高）再等宽度结束 */\n    while (TIM_GetCounter(TIM2) < width_us) { }\n\n    TIM_Cmd(TIM2, DISABLE);\n    TIM_SetCounter(TIM2, 0);\n    GPIO_ResetBits(GPIOA, GPIO_Pin_0);   /* 收尾拉低，下次从低开始 */\n}\n\n/* ---- 硬件单脉冲模式（更精确，不用忙等） ---- */\nvoid OC_OnePulseMode_Enable(void)\n{\n    /* 到点自动停，不需要软件 DISABLE */\n    TIM_SelectOnePulseMode(TIM2, TIM_OPMode_Single);\n    /* 之后每发一次：TIM_SetCounter(TIM2,0); TIM_Cmd(TIM2, ENABLE); 即可 */\n}\n\n/* 忙等的替代方案（RTOS 下推荐）：\n *   开通道中断，在中断里 TIM_Cmd(DISABLE) + 拉低引脚 + 发信号量唤醒任务。\n */",
   "最直观的验证：接一个 LED（或示波器）到 PA0，调用 `Pulse_Once(500)`，应该看到一闪即逝的 500µs 高电平；\n用示波器能量到精确的 500µs 宽度和稳定的幅度。\n连续调 10 次，宽度应该完全一致（不一致说明有中断干扰）。",
   "忘了 `TIM_SetCounter(TIM2, 0)` → CNT 可能已经跑过 CCR 了，这次脉冲根本不会发出（现象：偶尔发得出来、偶尔发不出来）。\n忘了把引脚拉低收尾 → 下次脉冲起点是高电平，脉冲宽度变成两倍或形状不对。\nActive 模式发完不停定时器 → CNT 会继续跑到 ARR 溢出、再次穿过 CCR，引脚会重复变高（不是只发一次）。要停就 `TIM_Cmd(DISABLE)`。\n忙等 `while (TIM_GetCounter(TIM2) < width_us)` 期间，RTOS 的低优先级任务得不到 CPU。宽脉冲（>1ms）建议改用中断通知。\n`TIM_SelectOnePulseMode` 是硬件单脉冲，配合 Active 模式最省心。",
   "TIM_SetCounter TIM_GetCounter TIM_SelectOnePulseMode TIM_Cmd"
  ],
  [
   "运行期切换模式 + 强制输出（用法 C），把定时器当“软件 GPIO”用",
   "有两件事只有懂输出比较的人才会做：\n\n① 运行期切换 OCMode。 比如先让通道输出 PWM，某个事件后改成 Toggle 做单脉冲。必须用 `TIM_SelectOCxM`，而且改完要 `TIM_CCxCmd(..., TIM_CCx_Enable)` 让通道输出真正生效。\n\n② 强制输出（ForcedOC）。 `TIM_ForcedOCxConfig(TIMx, TIM_ForcedAction_Active/Inactive)` 会立刻把引脚拉成指定电平，完全不看计数器。这等于用定时器的通道当一个“可以精确同步的 GPIO”，比直接操作 GPIO 好在：它能和定时器的其他通道同步翻转，做多路严格对齐的时序。",
   "① 用 `TIM_SelectOCxM` 切模式，用 `TIM_CCxCmd` 使能通道输出；\n② 用 `TIM_ForcedOC1Config/2Config/3Config/4Config` 强制电平。",
   "/* ---- ① 运行期切换输出模式 ---- */\nvoid OC_SwitchToToggle(void)\n{\n    /* 运行期改模式：只能用 TIM_SelectOCxM\n     *   （改结构体再调 TIM_OCxInit 会重新配置整个通道，可能打断输出） */\n    TIM_SelectOCxM(TIM2, TIM_Channel_1, TIM_OCMode_Toggle);\n\n    /* 改完还要使能通道输出，否则改了也不生效 */\n    TIM_CCxCmd(TIM2, TIM_Channel_1, TIM_CCx_Enable);\n}\n\n/* ---- ② 强制输出：不看计数，立刻把引脚拉高/拉低 ---- */\nvoid OC_ForceHigh(void)\n{\n    TIM_ForcedOC1Config(TIM2, TIM_ForcedAction_Active);      /* 立刻“有效电平”（高） */\n}\n\nvoid OC_ForceLow(void)\n{\n    TIM_ForcedOC1Config(TIM2, TIM_ForcedAction_Inactive);    /* 立刻“无效电平”（低） */\n}\n\n/* 强制输出的实用场景：\n *   · 多路输出需要“同时”翻转 → 用同一个定时器的多个通道，\n *     一次调用就能让它们对齐（比依次操作 GPIO 精确得多）\n *   · 单脉冲的“收尾拉低”可以改用它，比 GPIO_ResetBits 更“贴硬件”\n *\n * 强制输出之后，通道就停在强制状态了；要恢复“跟着计数器走”，\n *   必须重新用 TIM_SelectOCxM 设回目标模式。\n */",
   "在示波器上：调用 `OC_ForceHigh()` 应该立刻看到电平变高、`OC_ForceLow()` 立刻变低，中间不应该有任何周期性波形。\n如果还能看到方波，说明 OCMode 没有被真正改成强制模式（`TIM_SelectOCxM` 没调或通道没使能）。",
   "改完模式忘了 `TIM_CCxCmd(..., TIM_CCx_Enable)` → 切换“看起来成功了”但没有输出。\n强制输出是一次性的状态设置，不是持续生效的“模式”；一旦再发生比较匹配事件，行为会回到 OCMode 决定的方式。\n`TIM_ForcedAction_Active` 的“有效电平”由 `TIM_OCPolarity` 决定，极性配成 Low 时，“Active”反而是低电平。容易看反。\n高级定时器 TIM1/TIM8 还要开 `TIM_CtrlPWMOutputs`，否则强制输出也出不来。\n本项目 FWLIB 已经把输出比较封装好了，不必自己写寄存器：`SYS_TIM_OcInit(id, ch, port, pin, af, oc_mode, cycle_hz, ccr)` 一次配完引脚/时基/OC/启动（六种模式任选，`port` 传 0 就不占引脚）；`SYS_TIM_OcInitIT(..., callback)` 再挂上比较中断回调（“冻结模式 + 本中断”= 一个不占引脚的软定时器）；`SYS_TIM_OcSetCompare` 改比较值、`SYS_TIM_OcSetFreq` 运行中改频率（CCR 等比缩放）、`SYS_TIM_OcStop` 停单个信道。\n用库函数时唯一要的是：`OcInitIT` 注册的回调运行在定时器中断里，NVIC 优先级必须 ≥5（FreeRTOS 的 BASEPRI 红线）；另外别自己再写同名的 `TIMx_IRQHandler`，会与库里的 `__weak` 定义抢符号。",
   "TIM_SelectOCxM TIM_CCxCmd TIM_ForcedOC1Config"
  ]
 ],
 k:"输出比较 输出捕获 OC 引导 向导 教程 步骤 PWM区别 单脉冲 one shot 精确定时 等间隔 中断 TIM_OCMode Timing Toggle Active Inactive 强制输出 TIM_SelectOCxM TIM_ForcedOC TIM_SelectOnePulseMode TIM_CCxCmd CCR 预装载 移相 步进电机 超声触发 硬件单脉冲"}
"""


# ======================================================================
# 四、执行：插进 data_flow.js / data_guide.js
# ======================================================================
FLOW_ALL = FLOW_NEW + ",\n" + FLOW_EXTRA    # ⑬ SPI / ⑭ OC / ⑮ DAC / ⑯ CAN / ⑰ RTC
GUIDE_ALL = GUIDE_NEW + ",\n" + GUIDE_EXTRA  # 同名主题的 5 个引导向导

FLOW_FILE = "data_flow.js"
GUIDE_FILE = "data_guide.js"
FLOW_DECL = "const STD_FLOW = ["
GUIDE_DECL = "const STD_GUIDE = ["

# 幂等标记：拿每张卡 n 字段的开头当"这张卡在不在文件里"的判据
FLOW_MARKER = '⑬ SPI 主机通信'
GUIDE_MARKER = '🧭 SPI 主机引导'


def _check_pairing(text, is_guide):
    """粗校验：t 字段与数组归属是否对得上（流程卡不能混进 STD_GUIDE）。"""
    want = 't:"guide"' if is_guide else 't:"flow"'
    other = 't:"flow"' if is_guide else 't:"guide"'
    n_want = text.count(want)
    n_other = text.count(other)
    if n_other:
        raise RuntimeError(
            "待插入文本里混进了 %d 个 %s（本数组只应放 %s）" % (n_other, other, want))
    return n_want


def main():
    check_only = "--check" in sys.argv

    print("待插入（流程卡 %s）: %d 张" % (FLOW_FILE, _check_pairing(FLOW_ALL, False)))
    print("待插入（引导向导 %s）: %d 个" % (GUIDE_FILE, _check_pairing(GUIDE_ALL, True)))
    print()

    paths = [(os.path.join(HERE, FLOW_FILE), FLOW_DECL, FLOW_ALL, FLOW_MARKER, False),
             (os.path.join(HERE, GUIDE_FILE), GUIDE_DECL, GUIDE_ALL, GUIDE_MARKER, True)]

    if check_only:
        for path, decl, new_text, marker, is_guide in paths:
            text = io.open(path, encoding="utf-8", newline="").read()
            state = "已存在，会跳过" if marker in text else "尚未插入"
            print("  %-16s %s（%s）" % (os.path.basename(path), marker, state))
        return 0

    changed = 0
    for path, decl, new_text, marker, is_guide in paths:
        if insert_before_array_end(path, decl, new_text, marker):
            changed += 1

    print()
    print("完成：%d 个文件被改动。" % changed)
    if changed:
        print("接下来必做：node --check %s %s" % (FLOW_FILE, GUIDE_FILE))
        print("再重打手机版：python build_mobile.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
