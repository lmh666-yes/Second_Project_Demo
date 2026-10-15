# -*- coding: utf-8 -*-
"""gen_demo.py —— 为自动收录的函数/结构体生成调用示例（data_demo.js）

示例来源（优先级从高到低）：
  1) 你四个工程里写过的真实调用行（附 工程/文件:行号）—— 直接可抄、宏名有效
  2) 专用模板：DeInit / Cmd / StructInit / Unlock·Lock /
     GetFlagStatus / GetITStatus / ClearFlag / ClearITPendingBit / ITConfig
     （标志·中断宏从 Keil DFP 头文件真实提取，只选官方存在的宏）
  3) 通用兜底：参数能全映射（实例 + State→ENABLE + 标志·中断宏）的函数
结构体条目：生成“声明 → 逐项填 → 传给初始化函数”的毛坯用法。
（库源码内部的形参转发调用不采用——复制过去编译不过，宁可没有。）
用法：python gen_demo.py（工程写了新代码后重跑）
"""
import re, os, io, glob, sys, collections
try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "data_demo.js")


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


REPO = _find_repo(HERE)

# 前两个是同系列板卡库/参考工程（纯参考，不在本机就自动跳过）；
# 后两个是本项目两个板端，由仓库根拼出来（原来四处都写死盘符）。
ROOTS = [
    ("04ST32",  r"d:\yq\04ST32\01Project"),
    ("天马库",  r"d:\yq\05天马普中科技板子\01Project"),
    ("S1检测端", os.path.join(REPO, os.path.join("EnvMonitor", "Project_1_检测端", "Project"))),
    ("S2网关",  os.path.join(REPO, os.path.join("EnvMonitor", "Project_2_数据通信端", "Project"))),
]

# Keil DFP 头文件目录：原来写死 E:\k5_v5，换机器/换盘就取不到"官方标志·中断宏"。
DFP_INC = ''
for _r in (r'E:\k5_v5\ARM\PACK\Keil\STM32F4xx_DFP',
           r'C:\Keil_v5\ARM\PACK\Keil\STM32F4xx_DFP',
           r'D:\Keil_v5\ARM\PACK\Keil\STM32F4xx_DFP',
           r'E:\Keil_v5\ARM\PACK\Keil\STM32F4xx_DFP'):
    _h = sorted(glob.glob(_r + r'\*\Device\StdPeriph_Driver\inc'))
    if _h:
        DFP_INC = _h[-1]
        break
if not DFP_INC:
    print('[警告] 找不到 Keil STM32F4xx_DFP（找过 E:\\k5_v5、C:\\Keil_v5 等），'
          '标志·中断宏模板会退化。')
skip_parts = ("\\.git\\", "\\objects\\", "\\listings\\", "\\build\\", "\\debug\\", "\\release\\",
              "\\html\\", "\\rte\\", "\\android\\", "\\gradle\\", "\\pack\\")

# ---------- 1) 读 data_full.js：函数原型 + 结构体（含首字段） ----------
src = io.open(os.path.join(HERE, "data_full.js"), encoding="utf-8").read()
cards = re.findall(r'\{m:"([^"]+)",\s*n:"([^"]+)"(.*?)\}(?=,\n|\n\])', src, re.S)
protos = {}; funcs = []; structs = {}; struct_first_field = {}
for m, n, rest in cards:
    if 'f:[' in rest:
        structs[n] = m
        fm = re.search(r'f:\[\s*\["([^"]+)"', rest)
        struct_first_field[n] = fm.group(1) if fm else None
    else:
        funcs.append(n)
        pm = re.search(r'p:"((?:[^"\\]|\\.)*)"', rest)
        protos[n] = pm.group(1) if pm else ""

# 已手写详解的函数（同名自动卡不显示，不需生成示例）
hand = set()
for f in ("data_std_lib.js", "data_extra.js"):
    hand.update(re.findall(r'n:"([A-Z][A-Za-z0-9_]*)', io.open(os.path.join(HERE, f), encoding="utf-8").read()))

# ---------- 2) 读 data_rel.js：结构体 → 初始化函数 ----------
rel_src = io.open(os.path.join(HERE, "data_rel.js"), encoding="utf-8").read()
def rel_init(struct_name):
    m = re.search(r'"%s":\s*\[(.*?)\n' % re.escape(struct_name), rel_src)
    if not m:
        return None
    for tgt in re.findall(r'\["([^"]+)","[^"]*"\]', m.group(1)):
        if tgt.endswith("Init") or tgt.endswith("Config"):
            return tgt
    return None

SPECIAL_INIT = {"RCC_ClocksTypeDef": "RCC_GetClocksFreq", "HASH_MsgDigest": "HASH_GetDigest",
                "HASH_Context": "HASH_SaveContext", "RTC_AlarmTypeDef": "RTC_SetAlarm"}

def guess_init(sname):
    """结构体 → 配套初始化/使用函数：先查联动表，再按名字规律推，最后特例表"""
    r = rel_init(sname)
    if r:
        return r
    if sname in SPECIAL_INIT:
        return SPECIAL_INIT[sname]
    all_names = set(funcs) | hand
    for cand in (sname.replace("InitTypeDef", "Init"),
                 sname.replace("InitTypeDef", "Config"),
                 sname.replace("TypeDef", "Init"),
                 sname.replace("TypeDef", "Config")):
        if cand in all_names:
            return cand
    return None

# ---------- 3) 扫 DFP 头文件：收集各模块真实的标志/中断宏 ----------
macros_by_mod = {}
for h in glob.glob(os.path.join(DFP_INC, "*.h")):
    try:
        t = io.open(h, encoding="utf-8", errors="ignore").read()
    except Exception:
        continue
    for mac in set(re.findall(r'#define\s+([A-Za-z_][A-Za-z0-9_]+)\b', t)):
        if len(mac) >= 5 and "_" in mac and not mac.startswith("_"):
            macros_by_mod.setdefault(mac.split("_")[0], set()).add(mac)
macros_by_mod.setdefault("I2S", set()).update(macros_by_mod.get("SPI", set()))
ALL_MACROS = set().union(*macros_by_mod.values()) if macros_by_mod else set()
FLAG_PREF = ("EOC", "RXNE", "TXE", "TC", "EOP", "CC1", "Update", "RDY", "BSY")
IT_PREF   = ("RXNE", "TC", "Update", "ALRA", "EOC", "CC1", "TXE")

def pick_macro(pool, prefer, param=None):
    if not pool:
        return None
    if param:
        hits = sorted(m for m in pool if m.startswith(param + "_"))
        if not hits:
            return None
        for w in tuple(prefer) + ("Enable", "Normal", "Set", "Active", "PWM1", "None", "Rising"):
            for h in hits:
                if w in h:
                    return h
        return hits[0]
    for w in prefer:
        hit = sorted(m for m in pool if w in m)
        if hit:
            return hit[0]
    if len(pool) == 1:
        return next(iter(pool))
    return None

# ---------- 4) 参数工具 ----------
def proto_args(name):
    m = re.search(r'\(([^)]*)\)', protos.get(name, ""))
    if not m:
        return []
    s = m.group(1).strip()
    if s in ("", "void"):
        return []
    return [a.strip() for a in s.split(",")]

def arg_name(a):
    m = re.search(r'([A-Za-z_]\w*)\s*$', a)
    return m.group(1) if m else ""

def ret_type(name):
    p = protos.get(name, "")
    m = re.match(r'^\s*([A-Za-z_][A-Za-z0-9_ ]*?)\s+' + re.escape(name) + r'\s*\(', p)
    return m.group(1).strip() if m else None

def inst_of(argname):
    if not argname:
        return None
    special = {"GPIOx": "GPIOA", "DMAy_Streamx": "DMA1_Stream0",
               "FSMC_Bank": "FSMC_Bank1_NORSRAM1", "FMC_Bank": "FMC_Bank1"}
    if argname in special:
        return special[argname]
    m = re.match(r'^([A-Z][A-Za-z0-9]*)x$', argname)
    if m and not m.group(1).endswith("_"):
        return m.group(1) + "1"
    return None

def struct_of(name):
    if not name.endswith("StructInit"):
        return None
    pref = name[:-len("StructInit")].rstrip("_")
    cands = [s for s in structs if s.startswith(pref) and s.endswith("TypeDef")]
    cands.sort(key=lambda s: (not s.endswith("InitTypeDef"), len(s)))
    return cands[0] if cands else None

def mod_pools(name):
    mod = name.split("_")[0]
    pool = macros_by_mod.get(mod, set())
    return ([x for x in pool if "_FLAG_" in x], [x for x in pool if "_IT_" in x])

# ---------- 5) 专用模板 ----------
def make_special(name):
    args = proto_args(name)
    argsn = [arg_name(a) for a in args]
    pool_all = ALL_MACROS

    if name.endswith("_DeInit"):
        if len(args) == 0:
            return "%s();                       /* 复位回默认状态（重新配置前调用） */" % name
        if len(args) == 1:
            iv = inst_of(argsn[0])
            if iv:
                return "%s(%s);                  /* 复位回默认状态（重新配置前调用） */" % (name, iv)
        return None

    if name.endswith("_Cmd"):
        if len(args) == 1 and "state" in args[0].lower():
            return "%s(ENABLE);   /* ENABLE = 开 / DISABLE = 关 */" % name
        if len(args) == 2 and "state" in args[1].lower():
            iv = inst_of(argsn[0])
            if iv:
                return "%s(%s, ENABLE);   /* ENABLE = 开 / DISABLE = 关 */" % (name, iv)
        return None

    if name.endswith("StructInit"):
        st = struct_of(name)
        if st:
            return "%s s;\n%s(&s);    /* 先填默认值，改完字段再传给对应 Init */" % (st, name)
        return None

    if name.endswith("Unlock") or name.endswith("Lock"):
        if len(args) == 0:
            return "%s();   /* %s */" % (name, "操作前解锁" if name.endswith("Unlock") else "操作后锁上")
        return None

    if name.endswith("GetFlagStatus"):
        f = pick_macro(pool_all, FLAG_PREF, argsn[-1] if argsn else None)
        if not f or f.endswith("PVU") or f.endswith("RVU"):
            return None
        if len(args) == 1:
            return "while (%s(%s) == RESET) { }   /* 轮询等待该标志 */" % (name, f)
        if len(args) == 2:
            iv = inst_of(argsn[0])
            if iv:
                return "while (%s(%s, %s) == RESET) { }   /* 轮询等待该标志 */" % (name, iv, f)
        return None

    if name.endswith("GetITStatus"):
        f = pick_macro(pool_all, IT_PREF, argsn[-1] if argsn else None)
        if not f:
            return None
        if len(args) == 1:
            return "if (%s(%s) == SET) { /* 本中断源触发：处理… */ }" % (name, f)
        if len(args) == 2:
            iv = inst_of(argsn[0])
            if iv:
                return "if (%s(%s, %s) == SET) { /* 本中断源触发：处理… */ }" % (name, iv, f)
        return None

    if name.endswith("ClearFlag") or name.endswith("ClearITPendingBit"):
        is_flag = name.endswith("ClearFlag")
        f = pick_macro(pool_all, FLAG_PREF if is_flag else IT_PREF, argsn[-1] if argsn else None)
        if not f:
            return None
        note = "清标志（防误触发）" if is_flag else "清中断挂起位（中断处理完要清）"
        if len(args) == 1:
            return "%s(%s);   /* %s */" % (name, f, note)
        if len(args) == 2:
            iv = inst_of(argsn[0])
            if iv:
                return "%s(%s, %s);   /* %s */" % (name, iv, f, note)
        return None

    if name.endswith("ITConfig"):
        f = pick_macro(pool_all, IT_PREF, argsn[-2] if len(argsn) >= 2 else None)
        if not f:
            return None
        if len(args) == 2:
            return "%s(%s, ENABLE);   /* 开中断源（还要 NVIC_Init 使能对应 IRQn） */" % (name, f)
        if len(args) == 3:
            iv = inst_of(argsn[0])
            if iv:
                return "%s(%s, %s, ENABLE);   /* 开中断源（还要 NVIC_Init 使能对应 IRQn） */" % (name, iv, f)
        return None

    return None

# ---------- 6) 通用兜底（参数全可映射才生成） ----------
def make_generic(name):
    args = proto_args(name)
    argsn = [arg_name(a) for a in args]
    pool_all = ALL_MACROS
    vals = []
    decl = None
    for a, an in zip(args, argsn):
        al = a.lower()
        if "functionalstate" in al:
            vals.append("ENABLE"); continue
        if "flag" in an.lower():
            f = pick_macro(pool_all, FLAG_PREF, an)
            if not f: return None
            vals.append(f); continue
        if an.lower().endswith("it"):
            f = pick_macro(pool_all, IT_PREF, an)
            if not f: return None
            vals.append(f); continue
        iv = inst_of(an)
        if iv:
            vals.append(iv); continue
        tm = re.match(r'([A-Za-z_]\w*)\s*\*', a)
        if tm and tm.group(1) in structs and decl is None:
            decl = tm.group(1); vals.append("&s"); continue
        if an in ("Rank", "Number"):
            vals.append("1"); continue
        f = pick_macro(pool_all, (), an)
        if f:
            vals.append(f); continue
        return None
    rt = ret_type(name)
    call_args = ", ".join(vals)
    if rt in ("void", None):
        if not args:
            call = "%s();" % name
        else:
            call = "%s(%s);" % (name, call_args)
    elif rt in ("uint8_t", "uint16_t", "uint32_t"):
        call = "%s v = %s(%s);   /* 读回结果 */" % (rt, name, call_args)
    else:
        if not args:
            call = "%s();   /* %s 返回值，按“中文说明”判断处理 */" % (name, rt)
        else:
            call = "%s(%s);   /* %s 返回值，按“中文说明”判断处理 */" % (name, call_args, rt)
    if decl:
        call = "%s s;               /* 字段见“字段说明”表，逐项填好 */\n" % decl + call
    return call

# ---------- 7) 扫描 .c：抓真实调用行（保行号去注释） ----------
func_set = set(f for f in funcs if f not in hand)
call_re = re.compile(r"\b([A-Z][A-Za-z0-9_]{2,})\s*\(")
defline_re = re.compile(
    r'^(static\s+|extern\s+|inline\s+)*\s*'
    r'(void|char|short|int|long|float|double|u?int(8|16|32|64)_t|'
    r'FlagStatus|ITStatus|ErrorStatus|FunctionalState)\s')
demo = {}

def strip_comments_keep_lines(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text

file_cnt = 0
for tag, root in ROOTS:
    if not os.path.isdir(root):
        continue
    for p in glob.glob(os.path.join(root, "**", "*.c"), recursive=True):
        low = p.lower()
        if any(s in low for s in skip_parts):
            continue
        base = os.path.basename(p).lower()
        if base.startswith("stm32f4xx_") or base.startswith("system_"):
            continue
        file_cnt += 1
        try:
            text = strip_comments_keep_lines(io.open(p, encoding="utf-8", errors="ignore").read())
        except Exception:
            continue
        for i, line in enumerate(text.split("\n"), 1):
            s = line.strip()
            if not s or s.startswith("#") or defline_re.match(s):
                continue
            for mo in call_re.finditer(s):
                nm = mo.group(1)
                if nm in func_set and nm not in demo:
                    demo[nm] = ("%s/%s:%d" % (tag, os.path.basename(p), i), s)
    print("  扫描完成:", tag)
print("用户代码文件数: %d，抓到真实调用: %d" % (file_cnt, len(demo)))

# ---------- 8) 骨架补全 ----------
# 人工精写（覆盖自动骨架；不会覆盖“真实调用”）
MANUAL = {
"RCC_PLLConfig": 'RCC_PLLConfig(RCC_PLLSource_HSE, 8, 336, 2, 7);   /* PLLP 直接填数字 2/4/6/8；8MHz HSE → 168MHz 标准参数 */',
"FLASH_ProgramDoubleWord": 'FLASH_ProgramDoubleWord(0x080E0000, 0x1122334455667788);   /* 推荐写法；地址需已擦除，板1（ZE）用 ≤0x0807FFFF */',
"FLASH_ProgramHalfWord": 'FLASH_ProgramHalfWord(0x080E0010, 0x1234);   /* 地址需已擦除；板1（ZE）用 ≤0x0807FFFF */',
"FLASH_ProgramByte": 'FLASH_ProgramByte(0x080E0010, 0x5A);   /* 地址需已擦除（能用字/双字就别用字节） */',
"NVIC_SetVectorTable": 'NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x00);   /* 默认向量表位置（Bootloader 场景才改） */',
"NVIC_SystemLPConfig": 'NVIC_SystemLPConfig(NVIC_LP_SLEEPONEXIT, ENABLE);   /* 退出中断后直接睡眠 */',
"GPIO_Write": 'GPIO_Write(GPIOF, 0x0101);   /* 一次写整端口 16 位（位 0/8 置 1） */',
"EXTI_ClearFlag": 'EXTI_ClearFlag(EXTI_Line0);   /* 清标志（轮询方式处理外部事件） */',
"I2C_DigitalFilterConfig": 'I2C_DigitalFilterConfig(I2C1, 0);   /* 滤波强度 0~15（数字）；干扰大就调大 */',
"I2C_OwnAddress2Config": 'I2C_OwnAddress2Config(I2C1, 0x36);   /* 第二个自身地址（需先开双地址 I2C_DualAddressCmd） */',
"SDIO_GetResponse": 'uint32_t resp = SDIO_GetResponse(SDIO_RESP1);   /* 读响应寄存器 1~4 */',
"SDIO_WriteData": 'SDIO_WriteData(0x12345678);   /* 往数据 FIFO 写一个字 */',
"TIM_PrescalerConfig": 'TIM_PrescalerConfig(TIM2, 83, TIM_PSCReloadMode_Immediate);   /* 84MHz/(83+1)=1MHz，立即生效 */',
"TIM_DMACmd": 'TIM_DMACmd(TIM2, TIM_DMA_Update, ENABLE);   /* 更新事件触发 DMA 请求 */',
"TIM_SelectInputTrigger": 'TIM_SelectInputTrigger(TIM2, TIM_TS_ITR1);   /* 选输入触发源（级联/从模式；TIM_TS_ITR0~3、TIM_TS_TI1F_ED 等） */',
"TIM_ITRxExternalClockConfig": 'TIM_ITRxExternalClockConfig(TIM2, TIM_TS_ITR1);   /* 用 TIM1 的 TRGO 做 TIM2 时钟（级联） */',
"TIM_ETRConfig": 'TIM_ETRConfig(TIM2, TIM_ExtTRGPSC_OFF, TIM_ExtTRGPolarity_NonInverted, 0x0);   /* ETR 引脚预分频/极性/滤波 */',
"USART_SetAddress": 'USART_SetAddress(USART1, 0x05);   /* 多机通信时本机地址 */',
"CRC_CalcBlockCRC": 'uint32_t crc = CRC_CalcBlockCRC(buf, count);   /* buf 为 uint32_t 数组（先 CRC_ResetDR 复位） */',
"CRC_CalcCRC": 'uint32_t crc = CRC_CalcCRC(0x12345678);   /* 逐字计算（起始先调 CRC_ResetDR） */',
"CRC_SetIDRegister": 'CRC_SetIDRegister(0x5A5A5A5A);   /* 独立寄存器当小存储槽 */',
"DBGMCU_Config": 'DBGMCU_Config(DBGMCU_SLEEP, ENABLE);   /* 调试器挂起时睡眠模式也保持（调试低功耗用） */',
"DBGMCU_APB1PeriphConfig": 'DBGMCU_APB1PeriphConfig(DBGMCU_TIM2_STOP, ENABLE);   /* 断点时暂停 TIM2 计数 */',
"DBGMCU_APB2PeriphConfig": 'DBGMCU_APB2PeriphConfig(DBGMCU_TIM1_STOP, ENABLE);   /* 断点时暂停 TIM1 计数 */',
"CAN_SlaveStartBank": 'CAN_SlaveStartBank(14);   /* CAN2 过滤器起始编号（双 CAN 系统） */',
"DMA_MemoryTargetConfig": 'DMA_MemoryTargetConfig(DMA2_Stream5, DMA_Memory_1);   /* 双缓冲模式切换当前目标 */',
"DMA_DoubleBufferModeConfig": 'DMA_DoubleBufferModeConfig(DMA2_Stream5, (uint32_t)buf1, DMA_Memory_0);   /* 双缓冲：一边收一边处理 */',
"ADC_InjectedSequencerLengthConfig": 'ADC_InjectedSequencerLengthConfig(ADC1, 1);   /* 注入组序列长度 1~4 */',
"ADC_SetInjectedOffset": 'ADC_SetInjectedOffset(ADC1, ADC_InjectedChannel_1, 100);   /* 结果 = 转换值 - 偏移 */',
"ADC_AnalogWatchdogThresholdsConfig": 'ADC_AnalogWatchdogThresholdsConfig(ADC1, 3000, 100);   /* 高/低阈值（超区间触发 AWD） */',
"RTC_GetTimeStamp": 'RTC_TimeTypeDef t; RTC_DateTypeDef d;\nRTC_GetTimeStamp(RTC_Format_BIN, &t, &d);   /* 读时间戳锁存的时间 */',
}
for f, s in MANUAL.items():
    if f in func_set and f not in demo:
        demo[f] = (None, s)

real_cnt = len(demo)
for f in sorted(func_set):
    if f in demo:
        continue
    s = make_special(f) or make_generic(f)
    if s:
        demo[f] = (None, s)
print("真实调用: %d；生成示例后合计: %d / %d（还需处理的 %d 条）"
      % (real_cnt, len(demo), len(func_set), len(func_set) - len(demo)))

missing = collections.defaultdict(list)
for f in func_set:
    if f not in demo:
        missing[f.split("_")[0]].append(f)
print("没示例的按模块:")
for k in sorted(missing):
    print("  [%s] %d: %s" % (k, len(missing[k]), " ".join(missing[k][:16])))

# ---------- 9) 结构体毛坯示例 ----------
st_cnt = 0
for sname in structs:
    if sname in hand:
        continue
    initf = guess_init(sname)
    ff = struct_first_field.get(sname)
    if initf and ff:
        demo["\x01" + sname] = (None,
            "/* 用法：声明 → 按“字段说明”逐项填 → 传给 %s（详见“相关与联动”） */\n"
            "%s s;\n"
            "s.%s = ...;                 /* ← 其余字段照上表逐项填 */" % (initf, sname, ff))
        st_cnt += 1
print("结构体示例: %d 个" % st_cnt)

# ---------- 10) 输出 ----------
def js_escape(s):
    return (s.replace("\\", "\\\\").replace('"', '\\"')
            .replace("\r", "").replace("\t", "    ").replace("\n", "\\n"))

lines = ["/* =============================================================",
         " *  data_demo.js —— 自动收录条目的调用示例（gen_demo.py 自动生成）",
         " *  来源：① 你四个工程里的真实调用行（附位置，可去 IDE 核对）",
         " *        ② 按函数模式生成的骨架（标志/中断宏取自 Keil DFP 真实定义）",
         " *        ③ 结构体的毛坯用法。勿手改，重跑 gen_demo.py 即可。",
         " * ============================================================= */",
         "const CN_DEMO = {"]
for k in sorted(demo):
    loc, body = demo[k]
    value = js_escape(body)
    if loc:
        value = "/* 你的工程 \\u00b7 %s */\\n" % loc + value
    key = k.lstrip("\x01")
    lines.append('"%s": "%s",' % (key, value))
lines.append("};")
lines.append("")
io.open(OUT, "w", encoding="utf-8", newline="").write("\n".join(lines))
print("已生成:", OUT)
