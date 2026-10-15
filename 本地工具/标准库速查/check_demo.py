# -*- coding: utf-8 -*-
"""_check_demo.py —— 校验 data_demo.js 自动生成示例里的每个标识符都能被解释

通过条件（任一）：
  1) 是 Keil DFP 头文件里真实存在的 #define 宏
  2) 是速查收录的函数名 / 结构体类型名
  3) C 关键字 / 生成器用到的变量名 / 常见模块词白名单
  4) 外设实例名模式（ADC1、DMA1_Stream0、GPIOA、FSMC_Bank1_NORSRAM1…）
"""
import os, re, io, glob, sys
try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

def _find_repo(start):
    """向上找含 .git 或 EnvMonitor 的目录作仓库根——脚本挪到哪一层都能用，不写死盘符。"""
    d = os.path.abspath(start)
    while True:
        if os.path.isdir(os.path.join(d, '.git')) or os.path.isdir(os.path.join(d, 'EnvMonitor')):
            return d
        p = os.path.dirname(d)
        if p == d:
            return os.path.abspath(start)
        d = p


BASE = os.path.dirname(os.path.abspath(__file__))        # 本目录：标准库速查
# Keil DFP 头文件目录：原来写死 E:\k5_v5，换机器/换盘就找不到。
# 按候选列表找，找不到时下面会把"少了一批宏"的原因直接说清楚，不静默变成一堆误报。
_DFP_CANDS = [r'E:\k5_v5\ARM\PACK\Keil\STM32F4xx_DFP',
              r'C:\Keil_v5\ARM\PACK\Keil\STM32F4xx_DFP',
              r'D:\Keil_v5\ARM\PACK\Keil\STM32F4xx_DFP',
              r'E:\Keil_v5\ARM\PACK\Keil\STM32F4xx_DFP']
DFP_INC = ''
for _root in _DFP_CANDS:
    _hits = sorted(glob.glob(_root + r'\*\Device\StdPeriph_Driver\inc'))
    if _hits:
        DFP_INC = _hits[-1]                              # 取版本最高的那个
        break
DFP_INC2 = DFP_INC.replace(r'\StdPeriph_Driver\inc', r'\Include') if DFP_INC else ''
if not DFP_INC:
    print('[警告] 找不到 Keil STM32F4xx_DFP（找过 E:\\k5_v5、C:\\Keil_v5 等）；')
    print('       #define 宏这一类会查不全，下面的"未识别"里可能有误报。')

ALLMAC = set()
for incdir in (DFP_INC, DFP_INC2):
    if not incdir:
        continue
    for h in glob.glob(incdir + r"\*.h"):
        t = io.open(h, encoding="utf-8", errors="ignore").read()
        ALLMAC |= set(re.findall(r'#define\s+([A-Za-z_][A-Za-z0-9_]*)', t))

names = set()
for f in ("data_std_lib.js", "data_extra.js", "data_full.js"):
    s = io.open(BASE + "\\" + f, encoding="utf-8", errors="ignore").read()
    names |= set(re.findall(r'n:"([A-Z][A-Za-z0-9_]*)', s))

# 结构体字段名（s.字段 = … 合法）
full_src = io.open(BASE + r"\data_full.js", encoding="utf-8", errors="ignore").read()
FIELDS = set()
for m in re.finditer(r'f:\[(.*?)\]\s*\}', full_src, re.S):
    FIELDS |= set(re.findall(r'\["([A-Za-z_]\w*)', m.group(1)))

WORDS = {
    "void", "int", "char", "float", "double", "long", "short", "unsigned", "signed",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t",
    "if", "else", "while", "for", "return", "static", "const",
    "FlagStatus", "ITStatus", "ErrorStatus", "FunctionalState",
    "ENABLE", "DISABLE", "SET", "RESET", "NULL",
    "s", "v", "d", "t", "r", "c", "g", "a", "i", "u", "buf", "count", "resp",
    "ADC", "DAC", "GPIO", "SPI", "I2C", "I2S", "TIM", "UART", "USART", "DMA",
    "RTC", "PWM", "CAN", "SDIO", "DCMI", "CRC", "RNG", "CRYP", "HASH", "HMAC",
    "LTDC", "SAI", "DMA2D", "FMC", "FSMC", "IWDG", "WWDG", "PWR", "EXTI",
    "NVIC", "SYSCFG", "FLASH", "DBGMCU", "STM32", "STM", "PLL", "HSE", "LSE",
    "HSI", "LSI", "MII", "RMII", "MD5", "SHA", "AES", "DES", "TDES", "GCM",
    "CCM", "CBC", "CTR", "ECB", "PEC", "SMBus", "IrDA", "LIN", "GPIOA", "GPIOB",
    "GPIOC", "GPIOD", "GPIOE", "GPIOF", "GPIOG", "GPIOH", "GPIOI",
    "AWD", "IRQn", "Init", "buf1", "crc", "msg", "FLASH_Status",
    "CRC_ResetDR", "Bootloader", "FIFO", "ETR", "TRGO", "MCO", "PLLP",
}
INST = re.compile(r'^([A-Z]+[0-9]+(_Stream[0-9]+|_Block_[AB])?|GPIO[A-K]|FSMC_Bank1_NORSRAM1|FMC_Bank1)$')

txt = io.open(BASE + r"\data_demo.js", encoding="utf-8").read()
items = re.findall(r'"([^"]+)": "((?:[^"\\]|\\.)*)"', txt)
print("示例总数:", len(items))

def unesc(s):
    return s.replace("\\n", "\n").replace('\\"', '"')

bad = []
for k, v in items:
    if v.startswith("/* 你的工程"):
        continue
    for w in set(re.findall(r'(?<![A-Za-z0-9_])[A-Za-z_][A-Za-z0-9_]*(?![A-Za-z0-9_])', unesc(v))):
        if len(w) <= 2 or w in ALLMAC or w in names or w in FIELDS or w in WORDS or INST.match(w):
            continue
        bad.append((k, w, v[:70]))

print("无法解释的标识符: %d" % len(bad))
for k, w, v in bad[:100]:
    print("  [%s] ?%s  | %s" % (k, w, v))
