# -*- coding: utf-8 -*-
"""
check_std_lib_data.py —— 核验「data_std_lib.js + data_extra.js」里引用的
函数名 / 宏名 / 类型名 是否真实存在于：
  1) Keil DFP 包（STM32F4xx_DFP）—— ST 标准库语料
  2) 仓库 EnvMonitor 工程树（含 FreeRTOS 头文件）—— FreeRTOS 语料

用途：速查表数据维护后跑一遍，防止名称写错（裁剪版 SPL 可能缺宏）。
用法：python check_std_lib_data.py
"""
import re, os, glob, sys
try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

ROOT = os.path.dirname(os.path.abspath(__file__))   # 本脚本所在目录
DFP  = r"E:\k5_v5\ARM\PACK\Keil\STM32F4xx_DFP\1.0.8\Device"

# ============================================================================
# 白名单：这些标识符**故意写成这样**，不是笔误，不该报"幽灵"。
# 每条都写清理由，以后看到报告里少了一项能知道是为什么。
# ============================================================================
ALLOWLIST = {
    # ---- A. 不是代码标识符：芯片型号 / 封装 / 板名 / 文件名 ----
    "F407ZET6": "芯片型号（板1 MCU）",
    "ZET6":     "芯片型号简称",
    "ZGT6":     "芯片型号简称（板2 MCU）",
    "LQFP144":  "封装名",
    "F415":     "STM32 系列名（说明 CRYP/HASH 属该系列）",
    "F427":     "STM32 系列名（说明 LTDC/SAI 属该系列）",
    "EMI":      "电磁干扰，普通缩写不是宏",
    "Zeller":   "蔡勒公式（日期算星期），人名",
    "STD_LIB":     "本工具数据文件自身的变量名",
    "STD_EXTRA":   "本工具数据文件自身的变量名",

    # ---- B. 官方原型里的形参名（不在 #define 里，但就是标准库原文） ----
    "PriorityGroup":   "NVIC_EncodePriority 等函数的形参名",
    "PreemptPriority": "NVIC_EncodePriority 的形参名",
    "SubPriority":     "NVIC_EncodePriority 的形参名",

    # ---- C. 真实 API 名，只是不在本脚本的语料里 ----
    #      语料只有 DFP 的 .c/.h + 工程树；启动文件里的向量表符号、
    #      约定俗成的中断函数名都查不到。
    "WWDG_IRQHandler":      "启动文件向量表里的中断函数名",
    "TIM_FLAG_OverCapture": "标准库头文件里确实有这个宏，语料未覆盖",

    # ---- D. 通配/占位写法（教学示意，"x" 是故意留的） ----
    "DMA1_Streamx":       "DMA 数据流通配写法（Stream0~7）",
    "DMA2_Streamx":       "DMA 数据流通配写法",
    "DMAy_Streamx_IRQn":  "DMA 中断通道通配写法",
    "EXTIx_IRQn":         "EXTI 中断通道通配写法",
    "GPIO_AF_TIMx":       "复用功能编号通配写法",
    "TIM2_ETR":           "引脚复用名（外部触发输入），不是库宏",
    "TIM3_CH1":           "引脚复用名，不是库宏",
    "BIT_x":              "文中的否定说法：FreeRTOS 并没有 BIT_x 这样的宏",
    "TmrCb":              "软件定时器回调的示意函数名",
    "ReportCb":           "软件定时器回调的示意函数名（与 TmrCb 同类，正文举例用）",
    "App_Init":           "初始化函数的示意名（正文示例里「先建句柄、再建任务」的那一步）",
    "XxxHandle_t":        "通配写法：正文「static XxxHandle_t s_xxx = NULL;」泛指任一句柄类型",
    "SensorTask":         "本项目的任务名（示意）",
    "SensorData_t":       "本项目的结构体名（示意）",
    "W25Qxx":             "Flash 型号通配写法",
    "Group_4":            "NVIC_PriorityGroup_4 的口语简写（正文说明用）",
    "CC1":                "出现在 TIM_IT_CC1 / TIM_FLAG_CC1 等宏名里",
    "CC4":                "出现在 TIM_IT_CC4 / TIM_FLAG_CC4 等宏名里",
    "TIM_OC2":            "正文里 TIM_OC2/3/4PreloadConfig 的简写（真实名是 TIM_OC2PreloadConfig 等）",
    "Asynch":             "RTC_AsynchPrediv 的后半截：正文公式 «(Asynch+1)x(Synch+1)=32768» 的简写",
    "Synch":              "RTC_SynchPrediv 的后半截：同上",
    "Comm":               "从真实 API 名 ADC_CommonInit / ADC_CommonInitTypeDef 切出来的碎片（不是笔误）",

    # ---- E. 正文里拆开写的标识符后半截 / 普通英文词（不是笔误）----
    #      写成「xxx*」「A / B」这类并列写法时，正则会把后半截单独当词元收进来；
    #      它们本身不是独立标识符，不需要核对。
    "App":            "正文里的普通英文词：指“应用程序”（Bootloader 跳转到 App）",
    "CRC32":          "通用校验算法名（正文对比 ST 硬件 CRC 与 zlib 的 CRC32）",
    "EOP":            "FLASH_FLAG_EOP 的后半截（正文里拆开列举）",
    "OPERR":          "FLASH_FLAG_OPERR 的后半截",
    "WRPERR":         "FLASH_FLAG_WRPERR 的后半截",
    "PGAERR":         "FLASH_FLAG_PGAERR 的后半截",
    "PGPERR":         "FLASH_FLAG_PGPERR 的后半截",
    "PGSERR":         "FLASH_FLAG_PGSERR 的后半截",
    "FLASH_TIMEOUT":  "真实返回值名，标准库头文件里确实有，本脚本语料未覆盖",
    "GetDate":        "RTC_GetDate 的后半截（正文里 RTC_GetDate / RTC_GetTime 并列写）",
    "GetTime":        "RTC_GetTime 的后半截，同上",
    "WaitBits":       "xEventGroupWaitBits 的后半截（正文「WaitBits 传 xClearOnExit=pdTRUE」）",
    "Hello":          "串口 / printf 示例里的普通字符串内容",
    "IAP":            "通用缩写：In-Application Programming（正文说明用）",
    "OTA":            "通用缩写：Over-The-Air（正文说明用）",
    "OTA_APP_ADDR":   "本速查 OTA 示例里自定义的宏（引导里让读者自己在 ota.h 里定义）",
    "OTA_BACKUP_ADDR":"本速查 OTA 示例里自定义的宏，同上",
    "OcInitIT":       "SYS_TIM_OcInitIT 的后半截",
    "OnTick1ms":      "示例回调函数名（正文举例）",
    "SYS_SPI_":       "正文“SYS_SPI_*”的前缀（表示一族函数）",
    "SYS_TIM_Oc":     "正文“SYS_TIM_Oc*”的前缀，同上",
    "TIM2_CH3":       "引脚复用名（定时器 2 通道 3），不是库宏",
    "W25QXX_Log":     "正文“W25QXX_Log*”的前缀",
    "W25QXX_":        "正文“W25QXX_*”这种前缀写法（表示一族函数）的碎片，同上",
    "WIP":            "SPI Flash 状态寄存器位名（Write In Progress，芯片还在忙）",
}


def is_allowed(t):
    if t in ALLOWLIST:
        return True
    # 大小写不敏感兜底（F415/f415 之类）
    return t.upper() in {k.upper() for k in ALLOWLIST}


# 待核验的数据文件（手写的两个）
js_files = [os.path.join(ROOT, "data_std_lib.js"), os.path.join(ROOT, "data_extra.js")]
data = "\n".join(open(f, encoding="utf-8").read() for f in js_files if os.path.exists(f))

# ---------- 语料 1：DFP 全部头文件/源文件 ----------
paths = []
for ext in ("*.h", "*.c"):
    paths += glob.glob(os.path.join(DFP, "**", ext), recursive=True)

# ---------- 语料 2：仓库工程树（FreeRTOS + 工程头文件/源文件） ----------
proj = None
probe = ROOT
for _ in range(5):
    probe = os.path.dirname(probe)
    cand = os.path.join(probe, "EnvMonitor")
    if os.path.isdir(cand):
        proj = cand
        break
if proj:
    for ext in ("*.h", "*.c"):
        paths += glob.glob(os.path.join(proj, "**", ext), recursive=True)

parts = []
for p in paths:
    try:
        parts.append(open(p, encoding="utf-8", errors="ignore").read())
    except Exception:
        pass
blob = "\n".join(parts)
print(f"语料文件 {len(paths)} 个，共 {len(blob)} 字符（DFP + 工程树）")

# 1) 函数名核对（函数查 "名("；结构体查 "}名"（花括号后可有空白）；纯 ASCII 才查）
funcs = re.findall(r'n:\s*"([A-Za-z_][A-Za-z0-9_]*)"', data)
miss_f = [f for f in funcs if (f + "(") not in blob and not re.search(r'}\s*' + re.escape(f) + r'\b', blob)]
print(f"\n数据里函数/结构体 {len(funcs)} 个；在语料中找不到定义的: {len(miss_f)}")
for f in miss_f:
    print("   [缺] ", f)

# 2) 大写标识符核对（宏 / 枚举值 / 类型名）——先一次性收集语料词元（9MB 语料上逐个 re.search 太慢）
words_in_blob = set(re.findall(r'[A-Za-z_]\w*', blob))
defs_in_blob = set(re.findall(r'#define\s+([A-Za-z_]\w*)', blob))
tokens = sorted({t for t in re.findall(r'\b([A-Z][A-Za-z0-9_]{2,})\b', data)})
no_def, ghost = [], []
for t in tokens:
    if t in defs_in_blob:
        continue
    no_def.append(t)
    if t not in words_in_blob:
        ghost.append(t)

print(f"\n大写标识符共 {len(tokens)} 个；无 #define 的 {len(no_def)} 个；")
ghost_real = [t for t in ghost if not is_allowed(t)]
ghost_ok = [t for t in ghost if is_allowed(t)]
print(f"语料里完全找不到的 {len(ghost)} 个，其中 {len(ghost_real)} 个需要人工确认、"
      f"{len(ghost_ok)} 个已在白名单里（型号/形参名/通配写法，不是笔误）")

print("\n== 需要确认的（真的写错了？）==")
if ghost_real:
    for t in ghost_real:
        print("   [幽灵] ", t)
    print("   ↑ 确认是笔误就改数据文件；确认没问题就把名字加进本脚本的 ALLOWLIST 并写理由")
else:
    print("   （无）✔")

if ghost_ok:
    print("\n== 已白名单（不算问题）==")
    for t in ghost_ok:
        print(f"   [已知] {t:<24} {ALLOWLIST.get(t) or ALLOWLIST.get(t.upper(), '')}")

print("\n== 无 #define 的（类型名/示意写法，人工过目即可，共 "
      f"{len([t for t in no_def if t not in ghost])} 个）==")
print("   这些名字在语料里出现过（说明是真实用过的写法），只是没有 #define，")
print("   多为函数名/结构体类型名/形参名，不必逐条核对。")

print("\n提示：FreeRTOS 的小写开头宏（pdTRUE、portMAX_DELAY 等）不在本检查范围。")

# 退出码：还有需要确认的就返回 1，方便接批处理
sys.exit(1 if ghost_real else 0)
