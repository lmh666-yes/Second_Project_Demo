# -*- coding: utf-8 -*-
"""
gen_tables.py —— 生成 data_tables.js（速查工具「查找表」模块）。

七张表：
  1. 中断向量表（IRQn）        从 DFP stm32f4xx.h 的 IRQn_Type 枚举提取
  2. 时钟使能表（RCC 外设宏）  —— 从 DFP stm32f4xx_rcc.h 提取，按总线分组
  3. 复用功能表（AF 编号）      —— 从 DFP stm32f4xx_gpio.h 的 GPIO_AF_xxx 宏提取
  4. FLASH 扇区表              —— F407 标准扇区/地址（静态）
  5. DMA 请求映射表（常用）     —— 常用外设 → 数据流/通道（以 RM0090 表 42/43 为准）
  6. 中断宏表（XXX_IT_*）       从 DFP 全部头文件提取
  7. 标志宏表（XXX_FLAG_*）     —— 同上

用法：python gen_tables.py     （DFP 升级后重跑）
"""
import re, os, io, glob, sys
try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
DFP = r"E:\k5_v5\ARM\PACK\Keil\STM32F4xx_DFP\1.0.8\Device"
OUT = os.path.join(HERE, "data_tables.js")


def esc(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def read_first(pattern):
    paths = glob.glob(os.path.join(DFP, "**", pattern), recursive=True)
    if not paths:
        sys.exit("[错误] DFP 里找不到: " + pattern)
    return io.open(paths[0], encoding="utf-8", errors="ignore").read()


# ---------------- 1) 中断向量表 ----------------
h = read_first("stm32f4xx.h")
m = re.search(r"typedef enum[^{]*\{(.*?)\}\s*IRQn_Type", h, re.S)
if not m:
    sys.exit("[错误] stm32f4xx.h 里没找到 IRQn_Type 枚举")
irqs = []
seen_i = set()
for nm, val in re.findall(r"([A-Za-z_0-9]+_IRQn)\s*=\s*(-?\d+)", m.group(1)):
    if nm not in seen_i:
        seen_i.add(nm)
        irqs.append((nm, int(val)))

IRQ_USE = {
    "NonMaskableInt_IRQn": "不可屏蔽中断（NMI）", "HardFault_IRQn": "硬件错误（致命异常）",
    "MemoryManagement_IRQn": "内存管理错误", "BusFault_IRQn": "总线错误",
    "UsageFault_IRQn": "用法错误", "SVCall_IRQn": "系统服务调用（SVC）",
    "DebugMonitor_IRQn": "调试监视", "PendSV_IRQn": "可挂起系统调用（RTOS 任务切换用）",
    "SysTick_IRQn": "系统滴答定时器（时基）",
    "WWDG_IRQn": "窗口看门狗（提前唤醒）", "PVD_IRQn": "电源电压监测（掉电预警）",
    "TAMP_STAMP_IRQn": "备份域侵入/时间戳", "RTC_WKUP_IRQn": "RTC 唤醒（闹钟/周期唤醒，EXTI22）",
    "FLASH_IRQn": "Flash 操作完成", "RCC_IRQn": "时钟就绪", "ADC_IRQn": "ADC 转换完成（ADC1/2/3）",
    "CAN1_TX_IRQn": "CAN1 发送", "CAN1_RX0_IRQn": "CAN1 接收 FIFO0", "CAN1_RX1_IRQn": "CAN1 接收 FIFO1",
    "CAN1_SCE_IRQn": "CAN1 状态变化/错误", "TIM1_BRK_TIM9_IRQn": "TIM1 刹车 / TIM9",
    "TIM1_UP_TIM10_IRQn": "TIM1 更新 / TIM10", "TIM1_TRG_COM_TIM11_IRQn": "TIM1 触发/通信 / TIM11",
    "TIM1_CC_IRQn": "TIM1 捕获比较", "TIM2_IRQn": "TIM2（更新/捕获/比较）", "TIM3_IRQn": "TIM3",
    "TIM4_IRQn": "TIM4", "TIM5_IRQn": "TIM5", "TIM6_DAC_IRQn": "TIM6 / DAC",
    "TIM7_IRQn": "TIM7", "TIM8_BRK_TIM12_IRQn": "TIM8 刹车 / TIM12",
    "TIM8_UP_TIM13_IRQn": "TIM8 更新 / TIM13", "TIM8_TRG_COM_TIM14_IRQn": "TIM8 触发/通信 / TIM14",
    "TIM8_CC_IRQn": "TIM8 捕获比较", "I2C1_EV_IRQn": "I2C1 事件（收发过程）", "I2C1_ER_IRQn": "I2C1 错误",
    "I2C2_EV_IRQn": "I2C2 事件", "I2C2_ER_IRQn": "I2C2 错误", "I2C3_EV_IRQn": "I2C3 事件",
    "I2C3_ER_IRQn": "I2C3 错误", "SPI1_IRQn": "SPI1", "SPI2_IRQn": "SPI2", "SPI3_IRQn": "SPI3",
    "USART1_IRQn": "串口1（收/发/空闲）", "USART2_IRQn": "串口2", "USART3_IRQn": "串口3",
    "USART6_IRQn": "串口6", "UART4_IRQn": "串口4", "UART5_IRQn": "串口5",
    "RTC_Alarm_IRQn": "RTC 闹钟 A/B（EXTI17）", "OTG_FS_WKUP_IRQn": "USB OTG FS 唤醒",
    "OTG_FS_IRQn": "USB OTG FS（设备模式）", "OTG_HS_IRQn": "USB OTG HS",
    "SDIO_IRQn": "SDIO 卡", "DCMI_IRQn": "DCMI 摄像头", "FPU_IRQn": "浮点单元",
    "HASH_RNG_IRQn": "HASH / 随机数发生器", "ETH_IRQn": "以太网",
    "DMA1_Stream0_IRQn": "DMA1 数据流0 传输事件", "DMA1_Stream1_IRQn": "DMA1 数据流1",
    "DMA1_Stream2_IRQn": "DMA1 数据流2", "DMA1_Stream3_IRQn": "DMA1 数据流3",
    "DMA1_Stream4_IRQn": "DMA1 数据流4", "DMA1_Stream5_IRQn": "DMA1 数据流5",
    "DMA1_Stream6_IRQn": "DMA1 数据流6", "DMA1_Stream7_IRQn": "DMA1 数据流7",
    "DMA2_Stream0_IRQn": "DMA2 数据流0（ADC1 常用）", "DMA2_Stream1_IRQn": "DMA2 数据流1",
    "DMA2_Stream2_IRQn": "DMA2 数据流2（ADC2 常用）", "DMA2_Stream3_IRQn": "DMA2 数据流3（SPI1_TX/SDIO 等）",
    "DMA2_Stream4_IRQn": "DMA2 数据流4", "DMA2_Stream5_IRQn": "DMA2 数据流5（USART1_RX 常用）",
    "DMA2_Stream6_IRQn": "DMA2 数据流6（USART6_TX 等）", "DMA2_Stream7_IRQn": "DMA2 数据流7（USART1_TX 常用）",
}
for i in range(10, 16):
    pass
for i in range(0, 16):
    IRQ_USE.setdefault("EXTI%d_IRQn" % i, "外部中断线 %d" % i)
IRQ_USE["EXTI9_5_IRQn"] = "外部中断线 5~9"
IRQ_USE["EXTI15_10_IRQn"] = "外部中断线 10~15"

# 从启动文件提取全部中断处理函数名集合；IRQn 的 handler 名 = IRQn 名换后缀（与启动文件核对）
hset = set()
sp = glob.glob(os.path.join(DFP, "Source", "ARM", "startup_stm32f40_41xxx.s"))
if sp:
    hset = set(re.findall(r"\b([A-Za-z_]\w*_IRQHandler)\b",
                          io.open(sp[0], encoding="utf-8", errors="ignore").read()))
CORE_HANDLER = {"NonMaskableInt_IRQn": "NMI_Handler", "HardFault_IRQn": "HardFault_Handler",
                "MemoryManagement_IRQn": "MemManage_Handler", "BusFault_IRQn": "BusFault_Handler",
                "UsageFault_IRQn": "UsageFault_Handler", "SVCall_IRQn": "SVC_Handler",
                "DebugMonitor_IRQn": "DebugMon_Handler", "PendSV_IRQn": "PendSV_Handler",
                "SysTick_IRQn": "SysTick_Handler"}

def handler_of(nm, n):
    if n < 0:
        return CORE_HANDLER.get(nm, "")
    cand = nm.replace("_IRQn", "_IRQHandler")
    if cand in hset:
        return cand
    return "—（F429 外设，F407 无此向量）"

MORE_USE = {
    "FSMC_IRQn": "FSMC 外扩存储控制器（F407 也有：接外部 SRAM / LCD 时用）",
    "FMC_IRQn": "48 号中断，F429 称 FMC_IRQn，F407 工程用 FSMC_IRQn / FSMC_IRQHandler",
    "ETH_WKUP_IRQn": "以太网唤醒（WOL，从低功耗模式唤醒）",
    "CAN2_TX_IRQn": "CAN2 发送", "CAN2_RX0_IRQn": "CAN2 接收 FIFO0", "CAN2_RX1_IRQn": "CAN2 接收 FIFO1",
    "CAN2_SCE_IRQn": "CAN2 状态变化 / 错误",
    "OTG_HS_EP1_OUT_IRQn": "USB OTG HS 端点 1 输出", "OTG_HS_EP1_IN_IRQn": "USB OTG HS 端点 1 输入",
    "OTG_HS_WKUP_IRQn": "USB OTG HS 唤醒",
    "CRYP_IRQn": "加密加速（F415/417 专有，F407 无）",
}
for k, v in MORE_USE.items():
    IRQ_USE.setdefault(k, v)

F429_ONLY = {
    "UART7_IRQn": "UART7 串口", "UART8_IRQn": "UART8 串口",
    "SPI4_IRQn": "SPI4", "SPI5_IRQn": "SPI5", "SPI6_IRQn": "SPI6", "SAI1_IRQn": "SAI1 音频接口",
    "LTDC_IRQn": "LTDC 屏幕控制器", "LTDC_ER_IRQn": "LTDC 刷新错误", "DMA2D_IRQn": "DMA2D 图形加速",
}
for k, v in F429_ONLY.items():
    IRQ_USE.setdefault(k, "F429 专有（" + v + "），F407 无此向量")

rows_irq = [[nm, handler_of(nm, v), str(v), IRQ_USE.get(nm, "")] for nm, v in irqs]

# ---------------- 2) 时钟使能表 ----------------
rcc = read_first("stm32f4xx_rcc.h")
rc = re.findall(r"#define\s+(RCC_(AHB1|AHB2|AHB3|APB1|APB2)Periph_([A-Za-z0-9_]+))\s", rcc)
BUS_ORDER = {"AHB1": 0, "AHB2": 1, "AHB3": 2, "APB1": 3, "APB2": 4}
BUS_CN = {"AHB1": "AHB1（168MHz）", "AHB2": "AHB2（168MHz）", "AHB3": "AHB3（168MHz）",
          "APB1": "APB1（84MHz，最高）", "APB2": "APB2（84MHz，最高）"}
rc_uniq, seen_c = [], set()
for macro, bus, dev in rc:
    if macro not in seen_c:
        seen_c.add(macro)
        rc_uniq.append((BUS_ORDER[bus], dev, macro, bus))
rc_uniq.sort(key=lambda x: (x[0], x[1]))
rows_clk = [[BUS_CN[b], d, m] for _, d, m, b in rc_uniq]

# ---------------- 3) 复用功能表 ----------------
gpio = read_first("stm32f4xx_gpio.h")
af = re.findall(r"#define\s+GPIO_AF_([A-Za-z0-9_]+)\s+\(\(uint8_t\)0x([0-9A-Fa-f]{1,2})\)", gpio)
AF_CN = {
    "SYS": "系统（SWD 调试口、MCO 时钟输出等内核功能）",
    "TIM1": "定时器 TIM1", "TIM2": "定时器 TIM2", "TIM3": "定时器 TIM3", "TIM4": "定时器 TIM4",
    "TIM5": "定时器 TIM5", "TIM8": "定时器 TIM8", "TIM9": "定时器 TIM9", "TIM10": "定时器 TIM10",
    "TIM11": "定时器 TIM11", "TIM12": "定时器 TIM12", "TIM13": "定时器 TIM13", "TIM14": "定时器 TIM14",
    "I2C1": "I2C1 总线", "I2C2": "I2C2 总线", "I2C3": "I2C3 总线",
    "SPI1": "SPI1", "SPI2": "SPI2 / I2S2", "SPI3": "SPI3 / I2S3",
    "USART1": "串口1", "USART2": "串口2", "USART3": "串口3", "USART4": "串口4（UART4）",
    "USART5": "串口5（UART5）", "USART6": "串口6",
    "UART4": "串口4", "UART5": "串口5",
    "CAN1": "CAN1", "CAN2": "CAN2", "ETH": "以太网", "FMC": "FMC 外部存储控制器",
    "FSMC": "FSMC（外部存储/LCD）", "SDIO": "SDIO 卡", "DCMI": "DCMI 摄像头",
    "OTG_FS": "USB OTG FS", "OTG_HS": "USB OTG HS", "LTDC": "LTDC 液晶控制器",
    "DAC": "DAC 输出", "SAI1": "SAI1 音频", "SAI2": "SAI2 音频", "SAI3": "SAI3 音频",
    "MCO1": "时钟输出 MCO1", "MCO2": "时钟输出 MCO2", "EVENTOUT": "内核事件输出（示波器调试）",
}
rows_af = []
seen_a = set()
for name, val in af:
    if name in seen_a:
        continue
    seen_a.add(name)
    rows_af.append((int(val, 16), "GPIO_AF_" + name, AF_CN.get(name, ""), "AF" + str(int(val, 16))))
rows_af.sort(key=lambda x: (x[0], x[1]))
rows_af = [[r[3], r[1], r[2]] for r in rows_af]

# ---------------- 4) FLASH 扇区表（F407 标准） ----------------
sec = [("0", "0x0800 0000 ~ 0x0800 3FFF", "16 KB", "ZE / ZG"), ("1", "0x0800 4000 ~ 0x0800 7FFF", "16 KB", "ZE / ZG"),
       ("2", "0x0800 8000 ~ 0x0800 BFFF", "16 KB", "ZE / ZG"), ("3", "0x0800 C000 ~ 0x0800 FFFF", "16 KB", "ZE / ZG"),
       ("4", "0x0801 0000 ~ 0x0801 FFFF", "64 KB", "ZE / ZG"), ("5", "0x0802 0000 ~ 0x0803 FFFF", "128 KB", "ZE / ZG"),
       ("6", "0x0804 0000 ~ 0x0805 FFFF", "128 KB", "ZE / ZG"), ("7", "0x0806 0000 ~ 0x0807 FFFF", "128 KB", "ZE / ZG（ZE 的最后一个扇区）"),
       ("8", "0x0808 0000 ~ 0x0809 FFFF", "128 KB", "仅 ZG（1MB 版）"), ("9", "0x080A 0000 ~ 0x080B FFFF", "128 KB", "仅 ZG"),
       ("10", "0x080C 0000 ~ 0x080D FFFF", "128 KB", "仅 ZG"), ("11", "0x080E 0000 ~ 0x080F FFFF", "128 KB", "仅 ZG（参数区常放这里）")]
rows_flash = [["FLASH_Sector_" + a, b, c, d] for a, b, c, d in sec]

# ---------------- 5) DMA 请求映射表（常用；以 RM0090 表 42/43 为准） ----------------
dma = [
    ("USART1_RX", "—", "流5 / 通道4"), ("USART1_TX", "—", "流7 / 通道4"),
    ("USART2_RX", "流5 / 通道4", "—"), ("USART2_TX", "流6 / 通道4", "—"),
    ("USART3_RX", "流1 / 通道4", "—"), ("USART3_TX", "流3 / 通道4", "—"),
    ("USART6_RX", "—", "流1 / 通道5"), ("USART6_TX", "—", "流6 / 通道5"),
    ("UART4_RX", "流2 / 通道4", "—"), ("UART4_TX", "流4 / 通道4", "—"),
    ("UART5_RX", "流0 / 通道4", "—"), ("UART5_TX", "流7 / 通道4", "—"),
    ("SPI1_RX", "—", "流0 / 通道3"), ("SPI1_TX", "—", "流3 / 通道3"),
    ("SPI2_RX", "流3 / 通道0", "—"), ("SPI2_TX", "流4 / 通道0", "—"),
    ("SPI3_RX", "流0 / 通道0", "—"), ("SPI3_TX", "流5 / 通道0", "—"),
    ("I2C1_RX", "流0 / 通道1", "—"), ("I2C1_TX", "流6 / 通道1", "—"),
    ("I2C2_RX", "流2 / 通道7", "—"), ("I2C2_TX", "流7 / 通道7", "—"),
    ("I2C3_RX", "流2 / 通道3", "—"), ("I2C3_TX", "流4 / 通道3", "—"),
    ("ADC1", "—", "流0 / 通道0"), ("ADC2", "—", "流2 / 通道1"), ("ADC3", "—", "流0 / 通道2"),
    ("DAC1", "流5 / 通道7", "—"), ("DAC2", "流6 / 通道7", "—"),
    ("SDIO", "—", "流3 / 通道4（也见流6）"),
    ("TIM1_CH1", "—", "流1 / 通道6"), ("TIM1_CH2", "—", "流2 / 通道6"),
    ("TIM1_CH3", "—", "流6 / 通道6"), ("TIM1_CH4", "—", "流4 / 通道6"), ("TIM1_UP", "—", "流5 / 通道6"),
    ("TIM2_CH1", "流5 / 通道3", "—"), ("TIM2_CH2", "流6 / 通道3", "—"),
    ("TIM2_CH3 / TIM2_UP", "流1 / 通道3", "—"), ("TIM2_CH4", "流7 / 通道3", "—"),
    ("TIM3_CH1", "流4 / 通道5", "—"), ("TIM3_CH2", "流5 / 通道5", "—"),
    ("TIM3_CH3", "流7 / 通道5", "—"), ("TIM3_CH4 / TIM3_UP", "流2 / 通道5", "—"),
    ("TIM4_CH1", "流0 / 通道2", "—"), ("TIM4_CH2", "流3 / 通道2", "—"),
    ("TIM4_CH3", "流7 / 通道2", "—"), ("TIM4_CH4", "流4 / 通道2", "—"), ("TIM4_UP", "流6 / 通道2", "—"),
    ("DCMI", "—", "流1 / 通道1"),
]
rows_dma = [[a, b, c] for a, b, c in dma]

# ---------------- 6) 中断宏表 / 7) 标志宏表 ----------------
alltxt = "\n".join(io.open(hf, encoding="utf-8", errors="ignore").read()
                    for hf in glob.glob(os.path.join(DFP, "StdPeriph_Driver", "inc", "*.h")))
its, flags = {}, {}
seen_m = set()
for mm in re.finditer(r"#define\s+([A-Za-z0-9_]+)\s+\(\((uint(?:8|16|32)_t)\)(0x[0-9A-Fa-f]+)\)", alltxt):
    name, val = mm.group(1), mm.group(3)
    if name in seen_m:
        continue
    seen_m.add(name)
    if re.search(r"_IT_[A-Za-z0-9_]+$", name):
        its[name] = val
    elif "_FLAG_" in name:
        flags[name] = val

IT_USE = {
    "ADC_IT_AWD": "模拟看门狗：转换值超出设定阈值", "ADC_IT_EOC": "规则组转换完成（转完一个置一次）",
    "ADC_IT_JEOC": "注入组转换完成", "ADC_IT_OVR": "转换溢出（上次数据没读走）",
    "CAN_IT_BOF": "总线关闭（Bus-Off）", "CAN_IT_EPV": "错误被动（错误计数 >127）", "CAN_IT_ERR": "错误中断汇总开关",
    "CAN_IT_EWG": "错误警告（错误计数 >95）", "CAN_IT_FF0": "接收 FIFO0 满", "CAN_IT_FF1": "接收 FIFO1 满",
    "CAN_IT_FMP0": "接收 FIFO0 非空（收到报文）", "CAN_IT_FMP1": "接收 FIFO1 非空", "CAN_IT_FOV0": "接收 FIFO0 溢出",
    "CAN_IT_FOV1": "接收 FIFO1 溢出", "CAN_IT_LEC": "最后错误码（具体错误类型）", "CAN_IT_SLK": "进入睡眠确认",
    "CAN_IT_TME": "任一发送邮箱空（可发下一帧）", "CAN_IT_WKU": "唤醒（总线活动）",
    "CRYP_IT_INI": "输入 FIFO 服务（可喂数据）", "CRYP_IT_OUTI": "输出 FIFO 服务（可取结果）",
    "DAC_IT_DMAUDR": "DMA 下溢（DMA 没及时供数据）",
    "DCMI_IT_ERR": "同步错误", "DCMI_IT_FRAME": "帧结束（抓完一帧）", "DCMI_IT_LINE": "行结束",
    "DCMI_IT_OVF": "FIFO 溢出", "DCMI_IT_VSYNC": "VSYNC 跳变",
    "DMA_IT_TC": "传输完成（通用位：任一次传输完成）", "DMA_IT_HT": "传输过半（半缓冲）", "DMA_IT_TE": "传输错误",
    "DMA_IT_DME": "直接模式错误", "DMA_IT_FE": "FIFO 错误（溢出/欠载）",
    "FLASH_IT_EOP": "操作结束（写完/擦完）", "FLASH_IT_ERR": "操作错误（对齐/顺序/写保护等）",
    "FMC_IT_FallingEdge": "等待信号（NWAIT）下降沿", "FMC_IT_Level": "等待信号电平", "FMC_IT_Refresh": "SDRAM 刷新事件", "FMC_IT_RisingEdge": "等待信号上升沿",
    "FSMC_IT_FallingEdge": "等待信号下降沿", "FSMC_IT_Level": "等待信号电平", "FSMC_IT_RisingEdge": "等待信号上升沿",
    "I2C_IT_ADD10": "收到 10 位地址第一字节", "I2C_IT_ADDR": "地址匹配（主/从地址已识别）", "I2C_IT_AF": "应答失败（收 NACK）",
    "I2C_IT_ARLO": "仲裁丢失（多主机冲突）", "I2C_IT_BERR": "总线错误（起停位异常）", "I2C_IT_BTF": "字节传输完成",
    "I2C_IT_BUF": "缓冲中断（使能它 TXE/RXNE 才能进中断）", "I2C_IT_ERR": "错误中断汇总开关", "I2C_IT_EVT": "事件中断汇总开关",
    "I2C_IT_OVR": "上溢（从模式）", "I2C_IT_PECERR": "PEC 校验错", "I2C_IT_RXNE": "收到一个字节", "I2C_IT_SB": "起始位已发出",
    "I2C_IT_SMBALERT": "SMBus 报警", "I2C_IT_STOPF": "检测到停止位", "I2C_IT_TIMEOUT": "SMBus 超时", "I2C_IT_TXE": "发送寄存器空",
    "I2S_IT_UDR": "下溢（发送速度跟不上）",
    "RCC_IT_CSS": "时钟安全系统（HSE 停振）", "RCC_IT_HSERDY": "HSE 就绪", "RCC_IT_HSIRDY": "HSI 就绪", "RCC_IT_LSERDY": "LSE 就绪",
    "RCC_IT_LSIRDY": "LSI 就绪", "RCC_IT_PLLRDY": "主 PLL 锁定", "RCC_IT_PLLI2SRDY": "PLLI2S 锁定", "RCC_IT_PLLSAIRDY": "PLLSAI 锁定",
    "RNG_IT_CEI": "时钟错误", "RNG_IT_SEI": "种子错误",
    "RTC_IT_ALRA": "闹钟 A", "RTC_IT_ALRB": "闹钟 B", "RTC_IT_TAMP": "侵入检测（兼容汇总位）", "RTC_IT_TAMP1": "侵入 1 检测",
    "RTC_IT_TS": "时间戳", "RTC_IT_WUT": "周期唤醒",
    "SDIO_IT_CCRCFAIL": "命令响应 CRC 失败", "SDIO_IT_CEATAEND": "CE-ATA 命令结束", "SDIO_IT_CMDACT": "命令传输中",
    "SDIO_IT_CMDREND": "命令响应已收到", "SDIO_IT_CMDSENT": "命令已发送", "SDIO_IT_CTIMEOUT": "命令响应超时",
    "SDIO_IT_DATAEND": "数据传输结束", "SDIO_IT_DBCKEND": "数据块 CRC 校验成功", "SDIO_IT_DCRCFAIL": "数据 CRC 失败",
    "SDIO_IT_DTIMEOUT": "数据超时", "SDIO_IT_RXACT": "接收中", "SDIO_IT_RXDAVL": "接收 FIFO 有数据",
    "SDIO_IT_RXFIFOE": "接收 FIFO 空", "SDIO_IT_RXFIFOF": "接收 FIFO 满", "SDIO_IT_RXFIFOHF": "接收 FIFO 半满",
    "SDIO_IT_RXOVERR": "接收溢出", "SDIO_IT_SDIOIT": "SDIO 卡中断（组合卡）", "SDIO_IT_STBITERR": "起始位错误",
    "SDIO_IT_TXACT": "发送中", "SDIO_IT_TXDAVL": "发送 FIFO 有数据", "SDIO_IT_TXFIFOE": "发送 FIFO 空",
    "SDIO_IT_TXFIFOF": "发送 FIFO 满", "SDIO_IT_TXFIFOHE": "发送 FIFO 半空", "SDIO_IT_TXUNDERR": "发送下溢",
    "SPI_I2S_IT_ERR": "错误汇总开关", "SPI_I2S_IT_OVR": "溢出", "SPI_I2S_IT_RXNE": "收到数据可读",
    "SPI_I2S_IT_TIFRFE": "TI 帧格式错误", "SPI_I2S_IT_TXE": "发送寄存器空",
    "SPI_IT_CRCERR": "CRC 校验错", "SPI_IT_MODF": "模式错误（NSS 冲突）",
    "TIM_IT_Break": "刹车（急停输入触发）", "TIM_IT_CC1": "通道 1 捕获/比较", "TIM_IT_CC2": "通道 2 捕获/比较",
    "TIM_IT_CC3": "通道 3 捕获/比较", "TIM_IT_CC4": "通道 4 捕获/比较", "TIM_IT_COM": "换向（COM）事件",
    "TIM_IT_Trigger": "触发事件", "TIM_IT_Update": "更新事件（溢出/重装）",
    "USART_IT_CTS": "CTS 电平变化", "USART_IT_ERR": "错误汇总开关（EIE 控制）", "USART_IT_FE": "帧错误（停止位无效）",
    "USART_IT_IDLE": "总线空闲（一帧结束，不定长接收常用）", "USART_IT_LBD": "LIN 断帧检测", "USART_IT_NE": "噪声错误",
    "USART_IT_ORE_ER": "上溢（EIE 使能时归到 ER 组）", "USART_IT_ORE_RX": "上溢（RXNEIE 使能时归到 RXNE 组）",
    "USART_IT_PE": "校验错误", "USART_IT_RXNE": "收到数据（每字节一次）", "USART_IT_TC": "整帧发送完成", "USART_IT_TXE": "发送寄存器空（可塞下一字节）",
}

FLAG_USE = {
    "ADC_FLAG_AWD": "模拟看门狗（超出阈值）", "ADC_FLAG_EOC": "规则组转换完成", "ADC_FLAG_JEOC": "注入组转换完成",
    "ADC_FLAG_JSTRT": "注入组已启动", "ADC_FLAG_OVR": "转换溢出", "ADC_FLAG_STRT": "规则组已启动",
    "CAN_FLAG_BOF": "总线关闭", "CAN_FLAG_EPV": "错误被动", "CAN_FLAG_EWG": "错误警告",
    "CAN_FLAG_FF0": "接收 FIFO0 满", "CAN_FLAG_FF1": "接收 FIFO1 满", "CAN_FLAG_FMP0": "接收 FIFO0 非空",
    "CAN_FLAG_FMP1": "接收 FIFO1 非空", "CAN_FLAG_FOV0": "接收 FIFO0 溢出", "CAN_FLAG_FOV1": "接收 FIFO1 溢出",
    "CAN_FLAG_LEC": "最后错误码", "CAN_FLAG_RQCP0": "邮箱 0 请求完成", "CAN_FLAG_RQCP1": "邮箱 1 请求完成",
    "CAN_FLAG_RQCP2": "邮箱 2 请求完成", "CAN_FLAG_SLAK": "睡眠确认", "CAN_FLAG_WKU": "唤醒",
    "CRYP_FLAG_BUSY": "加密单元忙", "CRYP_FLAG_IFEM": "输入 FIFO 空", "CRYP_FLAG_IFNF": "输入 FIFO 未满",
    "CRYP_FLAG_INRIS": "输入中断状态", "CRYP_FLAG_OFFU": "输出 FIFO 满", "CRYP_FLAG_OFNE": "输出 FIFO 非空", "CRYP_FLAG_OUTRIS": "输出中断状态",
    "DAC_FLAG_DMAUDR": "DMA 下溢",
    "DCMI_FLAG_ERRMI": "同步错误（中断位）", "DCMI_FLAG_ERRRI": "同步错误（原始位）", "DCMI_FLAG_FNE": "FIFO 非空",
    "DCMI_FLAG_FRAMEMI": "帧结束（中断位）", "DCMI_FLAG_FRAMERI": "帧结束（原始位）",
    "DCMI_FLAG_HSYNC": "HSYNC 当前电平", "DCMI_FLAG_LINEMI": "行结束（中断位）", "DCMI_FLAG_LINERI": "行结束（原始位）",
    "DCMI_FLAG_OVFMI": "溢出（中断位）", "DCMI_FLAG_OVFRI": "溢出（原始位）", "DCMI_FLAG_VSYNC": "VSYNC 当前电平",
    "DCMI_FLAG_VSYNCMI": "VSYNC 跳变（中断位）", "DCMI_FLAG_VSYNCRI": "VSYNC 跳变（原始位）",
    "FLASH_FLAG_BSY": "Flash 忙", "FLASH_FLAG_EOP": "操作结束", "FLASH_FLAG_OPERR": "操作错",
    "FLASH_FLAG_PGAERR": "编程对齐错", "FLASH_FLAG_PGPERR": "并行位数错", "FLASH_FLAG_PGSERR": "编程顺序错",
    "FLASH_FLAG_RDERR": "读保护错误", "FLASH_FLAG_WRPERR": "写保护错误",
    "FMC_FLAG_FEMPT": "FIFO 空", "FMC_FLAG_FallingEdge": "等待信号下降沿", "FMC_FLAG_Level": "等待信号电平", "FMC_FLAG_RisingEdge": "等待信号上升沿",
    "FSMC_FLAG_FEMPT": "FIFO 空", "FSMC_FLAG_FallingEdge": "等待信号下降沿", "FSMC_FLAG_Level": "等待信号电平", "FSMC_FLAG_RisingEdge": "等待信号上升沿",
    "I2C_FLAG_ADD10": "收到 10 位地址首字节", "I2C_FLAG_ADDR": "地址匹配", "I2C_FLAG_AF": "应答失败（NACK）",
    "I2C_FLAG_ARLO": "仲裁丢失", "I2C_FLAG_BERR": "总线错误", "I2C_FLAG_BTF": "字节传输完成",
    "I2C_FLAG_BUSY": "总线忙（卡死时看它）", "I2C_FLAG_DUALF": "双地址匹配", "I2C_FLAG_GENCALL": "收到通用呼叫",
    "I2C_FLAG_MSL": "当前是主模式", "I2C_FLAG_OVR": "上溢", "I2C_FLAG_PECERR": "PEC 错",
    "I2C_FLAG_RXNE": "收到字节可读", "I2C_FLAG_SB": "起始位已发出", "I2C_FLAG_SMBALERT": "SMBus 报警",
    "I2C_FLAG_SMBDEFAULT": "SMBus 默认地址", "I2C_FLAG_SMBHOST": "SMBus 主机头", "I2C_FLAG_STOPF": "检测到停止位",
    "I2C_FLAG_TIMEOUT": "SMBus 超时", "I2C_FLAG_TRA": "收发方向（1=发送）", "I2C_FLAG_TXE": "发送寄存器空",
    "I2S_FLAG_CHSIDE": "当前声道（0 左 / 1 右）", "I2S_FLAG_UDR": "下溢",
    "IWDG_FLAG_PVU": "预分频更新中（改参数前等它清零）", "IWDG_FLAG_RVU": "重装值更新中",
    "RCC_FLAG_BORRST": "欠压复位", "RCC_FLAG_HSERDY": "HSE 就绪", "RCC_FLAG_HSIRDY": "HSI 就绪",
    "RCC_FLAG_IWDGRST": "独立看门狗复位", "RCC_FLAG_LPWRRST": "低功耗复位", "RCC_FLAG_LSERDY": "LSE 就绪",
    "RCC_FLAG_LSIRDY": "LSI 就绪", "RCC_FLAG_PINRST": "引脚复位", "RCC_FLAG_PLLI2SRDY": "PLLI2S 锁定",
    "RCC_FLAG_PLLRDY": "主 PLL 锁定", "RCC_FLAG_PLLSAIRDY": "PLLSAI 锁定", "RCC_FLAG_PORRST": "上电/掉电复位",
    "RCC_FLAG_SFTRST": "软件复位", "RCC_FLAG_WWDGRST": "窗口看门狗复位",
    "RNG_FLAG_CECS": "时钟错误", "RNG_FLAG_DRDY": "随机数就绪", "RNG_FLAG_SECS": "种子错误",
    "RTC_FLAG_ALRAF": "闹钟 A 触发", "RTC_FLAG_ALRAWF": "闹钟 A 可写（改前等它）", "RTC_FLAG_ALRBF": "闹钟 B 触发",
    "RTC_FLAG_ALRBWF": "闹钟 B 可写", "RTC_FLAG_INITF": "初始化模式中", "RTC_FLAG_INITS": "初始化模式状态位",
    "RTC_FLAG_RECALPF": "重校准挂起", "RTC_FLAG_RSF": "寄存器已同步（读时间前等它）", "RTC_FLAG_SHPF": "影子寄存器旁路挂起",
    "RTC_FLAG_TAMP1F": "侵入 1 检测", "RTC_FLAG_TSF": "时间戳已锁存", "RTC_FLAG_TSOVF": "时间戳溢出",
    "RTC_FLAG_WUTF": "周期唤醒触发", "RTC_FLAG_WUTWF": "唤醒计数器可写",
    "SPI_FLAG_CRCERR": "CRC 校验错", "SPI_FLAG_MODF": "模式错误（NSS 冲突）", "SPI_I2S_FLAG_BSY": "忙",
    "SPI_I2S_FLAG_OVR": "溢出", "SPI_I2S_FLAG_RXNE": "收到数据可读", "SPI_I2S_FLAG_TIFRFE": "TI 帧格式错误", "SPI_I2S_FLAG_TXE": "发送寄存器空",
    "TIM_FLAG_Break": "刹车事件", "TIM_FLAG_CC1": "通道 1 捕获/比较", "TIM_FLAG_CC1OF": "通道 1 捕获溢出",
    "TIM_FLAG_CC2": "通道 2 捕获/比较", "TIM_FLAG_CC2OF": "通道 2 捕获溢出", "TIM_FLAG_CC3": "通道 3 捕获/比较",
    "TIM_FLAG_CC3OF": "通道 3 捕获溢出", "TIM_FLAG_CC4": "通道 4 捕获/比较", "TIM_FLAG_CC4OF": "通道 4 捕获溢出",
    "TIM_FLAG_COM": "换向事件", "TIM_FLAG_Trigger": "触发事件", "TIM_FLAG_Update": "更新事件（溢出/重装）",
    "USART_FLAG_CTS": "CTS 电平（1=CTS 有效）", "USART_FLAG_FE": "帧错误", "USART_FLAG_IDLE": "总线空闲",
    "USART_FLAG_LBD": "LIN 断帧检测", "USART_FLAG_NE": "噪声错误", "USART_FLAG_ORE": "上溢（数据没读走）",
    "USART_FLAG_PE": "校验错误", "USART_FLAG_RXNE": "收到数据可读", "USART_FLAG_TC": "整帧发送完成", "USART_FLAG_TXE": "发送寄存器空",
}

DMA_KIND = {"TC": "传输完成", "HT": "传输过半", "TE": "传输错误", "DME": "直接模式错误", "FE": "FIFO 错误"}
def dma_fill(name):
    m = re.match(r"DMA_IT_(TC|HT|TE|DME|FE)IF([0-7])$", name)
    if m:
        return "数据流 %s：%s中断（NVIC 配 DMAx_Stream%s_IRQn）" % (m.group(2), DMA_KIND[m.group(1)], m.group(2))
    m = re.match(r"DMA_FLAG_(TC|HT|TE|DME|FE)IF([0-7])$", name)
    if m:
        return "数据流 %s：%s标志" % (m.group(2), DMA_KIND[m.group(1)])
    return ""

MOD_ORDER = ["USART", "I2C", "SPI", "I2S", "ADC", "DAC", "TIM", "DMA", "CAN", "RTC", "FLASH", "RCC",
             "PWR", "SDIO", "DCMI", "FMC", "FSMC", "CRYP", "HASH", "RNG", "IWDG", "WWDG", "LTDC", "DMA2D", "SAI"]
def mod_key(nm):
    p = nm.split("_")[0]
    return (MOD_ORDER.index(p) if p in MOD_ORDER else 99, nm)

rows_it = [[n.split("_")[0], n, v, IT_USE.get(n) or dma_fill(n)] for n, v in sorted(its.items(), key=lambda kv: mod_key(kv[0]))]
rows_flag = [[n.split("_")[0], n, v, FLAG_USE.get(n) or dma_fill(n)] for n, v in sorted(flags.items(), key=lambda kv: mod_key(kv[0]))]

# ---------------- 8) EXTI 中断线表 / 9) NVIC 分组表 / 10) RTC 唤醒时钟表 ----------------
rows_exti = [
    ["EXTI_Line0 ~ EXTI_Line15", "接引脚：任意端口的“同号”引脚（PinSource0~15）",
     "同号引脚共用一条线：PA5 / PB5 / PK5…只能有一个绑到 EXTI5（用 SYSCFG_EXTILineConfig 选端口）；触发引脚先配成 GPIO 输入"],
    ["EXTI_Line16", "PVD 电源电压监测输出", "电压低于阈值时触发（配 PWR_PVDCmd）"],
    ["EXTI_Line17", "RTC 闹钟（Alarm A/B）", "闹钟到时触发；NVIC 用 RTC_Alarm_IRQn；配 RTC_SetAlarm + RTC_AlarmCmd + RTC_ITConfig(RTC_IT_ALRA)"],
    ["EXTI_Line18", "USB OTG FS 唤醒", "USB 设备唤醒事件"],
    ["EXTI_Line19", "以太网唤醒（带 ETH 的型号）", "网线插拔 / 魔术包唤醒"],
    ["EXTI_Line20", "USB OTG HS 唤醒", "USB HS 唤醒事件"],
    ["EXTI_Line21", "RTC 侵入 / 时间戳（TAMP_STAMP）", "NVIC 用 TAMP_STAMP_IRQn；配 RTC_TamperCmd / RTC_TimeStampCmd"],
    ["EXTI_Line22", "RTC 周期唤醒（WakeUp Timer）", "每秒（按分频）触发；NVIC 用 RTC_WKUP_IRQn；RTC_ITConfig(RTC_IT_WUT) + EXTI_Line22 + RTC_WKUP_IRQn"],
]
rows_nvic = [
    ["NVIC_PriorityGroup_0", "0 位抢 / 4 位子", "抢占 1 级、子 16 级", "极少用"],
    ["NVIC_PriorityGroup_1", "1 位抢 / 3 位子", "抢占 2 级、子 8 级", ""],
    ["NVIC_PriorityGroup_2", "2 位抢 / 2 位子", "抢占 4 级（0~3）、子 4 级（0~3）", "裸机常用；抢占 3 / 子 3 在此范围内"],
    ["NVIC_PriorityGroup_3", "3 位抢 / 1 位子", "抢占 8 级、子 2 级", ""],
    ["NVIC_PriorityGroup_4", "4 位抢 / 0 位子", "抢占 16 级、子 1 级", "FreeRTOS 必须用（抢占式内核）"],
]
rows_rtcwk = [
    ["RTC_WakeUpClock_RTCCLK_Div16", "2 Hz（32768÷16）", "周期 = (计数器+1) ÷ 2 秒"],
    ["RTC_WakeUpClock_RTCCLK_Div8", "4 Hz", "周期 = (计数器+1) ÷ 4 秒"],
    ["RTC_WakeUpClock_RTCCLK_Div4", "8 Hz", "周期 = (计数器+1) ÷ 8 秒"],
    ["RTC_WakeUpClock_RTCCLK_Div2", "16 Hz", "周期 = (计数器+1) ÷ 16 秒"],
    ["RTC_WakeUpClock_CK_SPRE_16bits", "1 Hz（秒信号）", "周期 = (计数器+1) 秒：0x0000→1 秒、0x0009→10 秒（RTC 工程常用这档）"],
    ["RTC_WakeUpClock_CK_SPRE_17bits", "1 Hz（17 位计数）", "同 1 秒/步，但计数值从 0x10000 起（需要超过 65535 秒时用）"],
]

# ---------------- 组装输出 ----------------
tables = [
    dict(m="查找表", t="table", n="中断向量表（IRQn ↔ 处理函数名）",
         p="写中断服务函数用“处理函数名”列（如 RTC_WKUP_IRQHandler）；NVIC_Init 的 NVIC_IRQChannel 填“中断名”列。来自 DFP 头文件与启动文件。",
         d="中断向量表：搜中断函数名（如 RTC_WKUP_IRQHandler）或外设名直接定位；负编号=内核异常。",
         h=["中断名（IRQn）", "处理函数名", "编号", "用途 / 触发源"], a=rows_irq, k="中断 向量表 IRQn NVIC IRQHandler 中断函数名"),
    dict(m="查找表", t="table", n="时钟使能表（RCC 外设对应）",
         p="每个外设挂在哪条总线、用哪个宏开时钟；用位或 | 可一次开多个（来自 DFP stm32f4xx_rcc.h）",
         d="时钟使能总表：用外设前先 RCC_xxxPeriphClockCmd(宏, ENABLE) 开时钟。外设所属总线见表。",
         h=["总线（频率上限）", "外设", "时钟使能宏"], a=rows_clk, k="时钟 使能 外设 总线 AHB APB"),
    dict(m="查找表", t="table", n="复用功能表（GPIO_AF 编号）",
         p="GPIO_PinAFConfig 的 GPIO_AF 参数用这里的宏；同一引脚能复用的功能以数据手册“复用功能映射表”为准",
         d="复用功能编号表：引脚配成 GPIO_Mode_AF 后，用 GPIO_PinAFConfig(端口, GPIO_PinSourceN, 这里的宏) 选外设。",
         h=["复用号", "功能宏", "对应外设"], a=rows_af, k="复用 AF 映射 引脚"),
    dict(m="查找表", t="table", n="FLASH 扇区表（F407）",
         p="F407ZE=512KB（仅 Sector 0~7）；F407ZG=1MB（Sector 0~11）。",
         d="Flash 扇区地址表：STM32F4 按“扇区”擦除（不能按字节擦）；存参数选空闲的大扇区。",
         h=["扇区宏", "地址范围", "大小", "说明"], a=rows_flash, k="Flash 扇区 地址 擦除 参数"),
    dict(m="查找表", t="table", n="DMA 请求映射表（常用外设）",
         p="格式：流 / 通道；DMA_Init 的 DMA_Channel 和选哪条数据流按此表（未列出外设见 RM0090 表 42/43）",
         d="DMA 请求映射：每个外设固定占用某条数据流的某个通道，选错会导致收发失败。",
         h=["外设请求", "DMA1", "DMA2"], a=rows_dma, k="DMA 映射 数据流 通道 Stream Channel"),
    dict(m="查找表", t="table", n="中断宏表（XXX_IT_*，共 %d 个）" % len(rows_it),
         p="开中断源：xxx_ITConfig(外设, 中断宏, ENABLE)；判断触发：xxx_GetITStatus(外设, 中断宏)。宏与编码来自 DFP 头文件。",
         d="中断宏总表：按外设查“开哪个中断用哪个宏”（搜关键词如 RXNE / Update / 空闲）：",
         h=["外设", "中断宏", "编码", "用途 / 触发时机"], a=rows_it, k="中断 宏 IT ITConfig RXNE TXE Update IDLE 空空闲"),
    dict(m="查找表", t="table", n="标志宏表（XXX_FLAG_*，共 %d 个）" % len(rows_flag),
         p="读状态：xxx_GetFlagStatus(外设, 标志宏)；清除：xxx_ClearFlag(外设, 标志宏)。宏与编码来自 DFP 头文件。",
         d="标志宏总表：按外设查“读哪个状态用哪个宏”（搜关键词如 RXNE / TXE / BUSY / RDY）：",
         h=["外设", "标志宏", "编码", "用途 / 含义"], a=rows_flag, k="标志 宏 FLAG GetFlagStatus RXNE TXE BUSY RDY"),
    dict(m="查找表", t="table", n="EXTI 中断线表（外部中断）",
         p="EXTI_Init 的 EXTI_Line 填哪条线？0~15=引脚线（同号引脚、端口任选），16~22=内部特殊源；映射用 SYSCFG_EXTILineConfig",
         d="EXTI 线总表：引脚类按“同号规则”，特殊源（RTC 唤醒 / 闹钟、USB、PVD）见表。",
         h=["中断线", "对应源", "说明 / 备注"], a=rows_exti, k="EXTI 中断线 Line 外部中断 RTC 唤醒 闹钟 PVD 按键"),
    dict(m="查找表", t="table", n="NVIC 优先级分组表",
         p="全工程先 NVIC_PriorityGroupConfig 定一次分组，再决定各中断的抢占/子优先级取值（取值范围取决于分组）",
         d="优先级分组：决定“几位管抢占、几位管子优先级”，分组与数值须匹配。",
         h=["分组宏", "抢占/子 位数", "可用级数", "说明"], a=rows_nvic, k="NVIC 优先级 分组 Group 抢占 子优先级"),
    dict(m="查找表", t="table", n="RTC 唤醒时钟分频表（WakeUp Timer）",
         p="周期唤醒步骤：RTC_WakeUpCmd(DISABLE) → 等 RTC_ISR_WUTWF → 选时钟 + 设计数器 → RTC_ITConfig(RTC_IT_WUT) → RTC_WakeUpCmd(ENABLE)",
         d="RTC 唤醒时钟表：唤醒周期 = (计数器+1) ÷ 时钟频率。",
         h=["唤醒时钟宏", "时钟频率", "唤醒周期计算"], a=rows_rtcwk, k="RTC 唤醒 周期 WakeUp WUT 秒 闹钟"),
]

lines = ["/* =============================================================",
         " *  data_tables.js —— 速查工具「查找表」模块",
         " *  由 gen_tables.py 从 Keil DFP 自动生成（IRQn / 时钟宏 / AF 宏为提取；",
         " *  FLASH 扇区与 DMA 映射为静态整理表）。勿手改，重新生成即可。",
         " * ============================================================= */",
         "const STD_TABLES = ["]
for t in tables:
    lines.append("{m:\"%s\", t:\"table\", n:\"%s\", p:\"%s\", d:\"%s\", h:[%s], a:[%s], k:\"%s\"}," % (
        t["m"], esc(t["n"]), esc(t["p"]), esc(t["d"]),
        ",".join('"%s"' % esc(c) for c in t["h"]),
        ",".join("[" + ",".join('"%s"' % esc(str(v)) for v in row) + "]" for row in t["a"]),
        esc(t["k"])))
lines.append("];")
lines.append("")

io.open(OUT, "w", encoding="utf-8", newline="").write("\n".join(lines))
print("已生成:", OUT)
print("  中断向量表: %d 行" % len(rows_irq))
print("  时钟使能表: %d 行" % len(rows_clk))
print("  复用功能表: %d 行" % len(rows_af))
print("  FLASH 扇区表: %d 行" % len(rows_flash))
print("  DMA 映射表: %d 行" % len(rows_dma))
print("  中断宏表: %d 行" % len(rows_it))
print("  标志宏表: %d 行" % len(rows_flag))
print("  EXTI 线表: %d 行" % len(rows_exti))
print("  NVIC 分组表: %d 行" % len(rows_nvic))
print("  RTC 唤醒时钟表: %d 行" % len(rows_rtcwk))
