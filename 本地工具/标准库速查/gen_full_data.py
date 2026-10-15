# -*- coding: utf-8 -*-
"""
gen_full_data.py —— 从 Keil DFP 提取「STM32F4 标准外设库」全部函数原型与结构体，
生成 data_full.js（供「标准库函数速查」网页兜底显示：数量全、原型准确）。

用法：python gen_full_data.py
"""
import re, os, glob, sys
try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
DFP  = r"E:\k5_v5\ARM\PACK\Keil\STM32F4xx_DFP\1.0.8\Device\StdPeriph_Driver\inc"
OUT  = os.path.join(HERE, "data_full.js")

MODMAP = {
    "misc": "NVIC", "stm32f4xx_adc": "ADC", "stm32f4xx_can": "CAN", "stm32f4xx_crc": "CRC",
    "stm32f4xx_cryp": "CRYP", "stm32f4xx_dac": "DAC", "stm32f4xx_dbgmcu": "DBGMCU",
    "stm32f4xx_dcmi": "DCMI", "stm32f4xx_dma": "DMA", "stm32f4xx_dma2d": "DMA2D",
    "stm32f4xx_exti": "EXTI", "stm32f4xx_flash": "FLASH", "stm32f4xx_fmc": "FMC",
    "stm32f4xx_fsmc": "FSMC", "stm32f4xx_gpio": "GPIO", "stm32f4xx_hash": "HASH",
    "stm32f4xx_i2c": "I2C", "stm32f4xx_iwdg": "IWDG", "stm32f4xx_ltdc": "LTDC",
    "stm32f4xx_pwr": "PWR", "stm32f4xx_rcc": "RCC", "stm32f4xx_rng": "RNG",
    "stm32f4xx_rtc": "RTC", "stm32f4xx_sai": "SAI", "stm32f4xx_sdio": "SDIO",
    "stm32f4xx_spi": "SPI", "stm32f4xx_syscfg": "SYSCFG", "stm32f4xx_tim": "TIM",
    "stm32f4xx_usart": "USART", "stm32f4xx_wwdg": "WWDG",
}

def module_of(fname):
    base = os.path.basename(fname)[:-2]
    return MODMAP.get(base, base.replace("stm32f4xx_", "").upper())

funcs, structs = [], []
fdoc = {}   # 函数名 -> 官方注释（英文）

def clean_doc(s):
    s = " ".join(s.split()).strip(" *")
    if not s or s.startswith("@") or "----" in s or s.startswith("Exported"):
        return ""
    if set(s) <= set("* -/"):
        return ""
    return s

for f in sorted(glob.glob(os.path.join(DFP, "*.h"))):
    mod = module_of(f)
    raw = open(f, encoding="utf-8", errors="ignore").read()
    text = raw
    # 去注释（块注释 + 行注释）
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    # 合并宏续行 + 删掉预处理指令行：否则 #define 群（无分号）会和后面
    # 第一个函数声明粘成一个 chunk，整个被 "#" 过滤误伤（如 RCC_DeInit）。
    text = re.sub(r"\\\s*\n", " ", text)
    text = re.sub(r"^\s*#.*$", "", text, flags=re.M)

    # ---- 结构体（含官方字段注释 /*!< ... */） ----
    for m in re.finditer(r"typedef\s+struct\s*\{([^}]*)\}\s*([A-Za-z_]\w*)\s*;", text):
        body, name = m.group(1), m.group(2)
        # 在原文里抓这个结构体的字段注释
        fmap = {}
        mb = re.search(r"typedef\s+struct\s*\{[^}]*\}\s*" + re.escape(name) + r"\s*;", raw, re.S)
        if mb:
            for fm in re.finditer(r"([A-Za-z_]\w*)(\[\d+\])?\s*;\s*/\*!<(.*?)\*/", mb.group(0), re.S):
                fmap[fm.group(1)] = " ".join(fm.group(3).split())
        fields = []
        for line in body.split(";"):
            line = " ".join(line.split())
            if not line:
                continue
            parts = line.rsplit(" ", 1)
            if len(parts) == 2:
                ftype, fname = parts
                fname = fname.strip()
                if re.match(r"^[A-Za-z_]\w*(\[\d+\])?$", fname):
                    fields.append((fname, ftype, fmap.get(fname, "")))
        if fields:
            structs.append((mod, name, fields))

    # ---- 函数原型 ----
    for chunk in text.split(";"):
        ch = " ".join(chunk.split())
        if not ch or "{" in ch or "#" in ch or "typedef" in ch or "}" in ch:
            continue
        mm = re.match(r"^([A-Za-z_][\w \t\*]*?)\s+([A-Z][A-Za-z0-9_]*)\s*\(([^()]*)\)$", ch)
        if mm:
            ret, name, args = mm.group(1).strip(), mm.group(2), mm.group(3).strip()
            proto = "%s %s(%s);" % (ret, name, args)
            funcs.append((mod, name, proto))

    # ---- 函数官方注释：一次扫描「注释块 → 紧随其后的函数名」（高效，不做逐函数搜索） ----
    for cm in re.finditer(r"/\*\s*(?!\!)(.*?)\*/((?:(?!\*/|;).){0,300})", raw, re.S):
        c = clean_doc(cm.group(1))
        if not c:
            continue
        nm2 = re.search(r"\b([A-Z][A-Za-z0-9_]*)\s*\(", cm.group(2))
        if nm2 and nm2.group(1) not in fdoc:
            fdoc[nm2.group(1)] = c

# 去重（按函数名）
seen, ufuncs = set(), []
for mod, name, proto in funcs:
    if name in seen:
        continue
    seen.add(name)
    ufuncs.append((mod, name, proto))

# 输出 JS
lines = []
lines.append("/* =============================================================")
lines.append(" *  data_full.js —— STM32F4 标准外设库：全部函数原型 + 结构体字段")
lines.append(" *  由 gen_full_data.py 从 Keil DFP 自动生成（原型精确；d 字段 = 官方英文注释）")
lines.append(" *  手写详解见 data_std_lib.js；本文件勿手改，重新生成即可。")
lines.append(" * ============================================================= */")
lines.append("")
lines.append("const STD_FULL = [")
for mod, name, proto in ufuncs:
    p = proto.replace('"', '\\"')
    d = fdoc.get(name, "").replace('"', '\\"')
    if d:
        lines.append('{m:"%s", n:"%s", p:"%s", d:"%s"},' % (mod, name, p, d))
    else:
        lines.append('{m:"%s", n:"%s", p:"%s"},' % (mod, name, p))
lines.append("];")
lines.append("")
lines.append("const STD_STRUCTS = [")
for mod, name, fields in structs:
    fs = ",".join('["%s","%s","%s"]' % (a, b.replace('"', '\\"'), c.replace('"', '\\"')) for a, b, c in fields)
    lines.append('{m:"%s", n:"%s", f:[%s]},' % (mod, name, fs))
lines.append("];")
lines.append("")

open(OUT, "w", encoding="utf-8").write("\n".join(lines))

# 统计
from collections import Counter
cnt = Counter(m for m, _, _ in ufuncs)
print("函数总数:", len(ufuncs))
for m, c in sorted(cnt.items()):
    print("  %-6s %d" % (m, c))
print("结构体总数:", len(structs))
n_doc = sum(1 for _, n, _ in ufuncs if fdoc.get(n))
n_fc = sum(1 for _, _, flds in structs for _, _, c in flds if c)
n_ft = sum(len(flds) for _, _, flds in structs)
print("含官方注释的函数:", n_doc, "/", len(ufuncs))
print("含官方注释的字段:", n_fc, "/", n_ft)
print("已生成:", OUT)
