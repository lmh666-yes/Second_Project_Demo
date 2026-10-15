# -*- coding: utf-8 -*-
"""gen_usage.py —— 扫描本机工程源码，生成 data_usage.js（函数 → 本项目使用位置）。

用法：python gen_usage.py   （工程源码更新后重跑即可）

扫描范围（tags 顺序即"本项目优先"顺序）：
    S1检测端  d:\\GitHub_Project\\EnvMonitor\\Project_1_检测端\\Project   ← 本项目·采集端
    S2网关    d:\\GitHub_Project\\EnvMonitor\\Project_2_数据通信端\\Project ← 本项目·通信端
    天马库    d:\\yq\\05天马普中科技板子\\01Project                      ← 同系列板卡库
    04ST32    d:\\yq\\04ST32\\01Project                                 ← 参考工程

每条记录格式：{"tag":"S1检测端","f":"FWLIB/src/sys_i2c.c","l":123,"p":"调用"|"定义"|"声明"}
  简化形式：{"tag":"S1检测端","f":"...","l":123}  表示"调用"（保持向后兼容）

三处关键设计（都是以前"看不到本项目"的原因）：
  1) 【每个工程必占名额】按 tags 顺序轮转挑选，S1/S2 优先拿满 3 个位置，
     而不是被先扫到的 04ST32 全部占满（旧版就是被 04ST32 吃光了 3 个名额）。
  2) 【调用优先】.h 里的函数原型行（声明）降到最后，优先显示真正被调用的地方；
     .c 里的定义排第二。旧版把声明和头文件行也当"使用位置"，噪音很大。
  3) 【路径缩短】只保留从 Project 往下的相对路径（如 FWLIB/src/sys_i2c.c），
     而不是 "d:\\GitHub_Project\\EnvMonitor\\Project_1_检测端\\Project\\FWLIB\\src\\..." 这种全路径。
     多条记录之间自动保证后缀唯一，不会出现两条都显示 "sys_i2c.c" 分不清的情况。
"""
import re
import os
import io
import glob
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "data_usage.js")


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

# tags 顺序 = 本项目优先顺序（打平局时排前面的优先）
# 前两个（本项目两个板端）由仓库根拼出来；后两个是同系列板卡库/参考工程，
# 纯参考、不在本机就自动跳过（原来两处都写死盘符，换机器就跑不起来）。
ROOTS = [
    ("S1检测端", os.path.join(REPO, os.path.join("EnvMonitor", "Project_1_检测端", "Project"))),
    ("S2网关",   os.path.join(REPO, os.path.join("EnvMonitor", "Project_2_数据通信端", "Project"))),
    ("天马库",   r"d:\yq\05天马普中科技板子\01Project"),
    ("04ST32",  r"d:\yq\04ST32\01Project"),
]

MAX_REC = 3          # 每个函数最多收录几处
MAX_PER_FILE = 1     # 同一个文件最多收录几处
PRIORITY = {"调用": 0, "定义": 1, "声明": 2}

# 标准库/内核文件（stm32f4xx_*.c、core_cm4.h…）：这些地方虽然也在"调用"库里函数，
# 但对"我在本项目哪里用过它"没有参考价值，排到所有用户代码之后。
LIB_HINT = ("stm32f4xx_", "stm32f4x7_", "core_cm", "system_stm32", "misc.c", "startup_")


def tier(x):
    """重要度分层：数字越小越优先显示。

    0 = 本项目代码里的真实调用        ← 最有用
    1 = 本项目代码里的定义
    2 = 参考工程里的调用
    3 = 库/内核文件里的引用
    4 = .h 原型声明（基本没参考价值，兜底）
    """
    if x["p"] == "声明":
        return 4
    base = os.path.basename(x["f"]).lower()
    if any(h in base for h in LIB_HINT):
        return 3
    if x["p"] == "定义":
        return 1 if x["tag"] in ("S1检测端", "S2网关") else 2
    return 0 if x["tag"] in ("S1检测端", "S2网关") else 2

# 路径缩短的锚点：从最后一个命中的锚点往下保留
ANCHORS = ["Project_1_检测端", "Project_2_数据通信端", "Project", "FWLIB", "User",
           "Core", "Drivers", "Hardware", "Src", "Inc"]

SKIP_PARTS = ("\\.git\\", "\\objects\\", "\\listings\\", "\\build\\", "\\debug\\",
              "\\release\\", "\\html\\", "\\output\\")


# ---------------------------------------------------------------- 1) 收集函数名
def collect_names():
    """从速查的三个手写/自动数据文件里取出所有已收录的函数名。"""
    names = set()
    for f in ("data_std_lib.js", "data_extra.js", "data_full.js"):
        p = os.path.join(HERE, f)
        if os.path.exists(p):
            s = io.open(p, encoding="utf-8", errors="ignore").read()
            names.update(re.findall(r'n:"([A-Z][A-Za-z0-9_]*)', s))
    return names


# ---------------------------------------------------------------- 2) 扫描源码
CALL_RE = re.compile(r"\b([A-Z][A-Za-z0-9_]{2,})\s*\(")
DEF_RE = re.compile(r"^\s*(?:[A-Za-z_][A-Za-z0-9_ \*]*\s+)?(?:\*+\s*)?([A-Z][A-Za-z0-9_]{2,})\s*\(")


def classify(line, nm, is_header, def_start=False):
    """判断这一行对函数 nm 来说属于：调用 / 定义 / 声明。

    判定要点：
      - .c 文件里 "xxx(...);" 绝大多数是【调用】，不是声明。
        旧版把所有以 ");" 结尾的行都当声明，结果 GPIO_Init/NVIC_Init 这些
        明明在代码里被调用的函数，全都显示成 .c 文件里的"声明"，看不出真用途。
      - 只有 .h 文件里的原型行（或 extern 声明）才是真"声明"。
      - "定义"：本工程 325/372 的函数是大括号另起一行写的，所以不能只看本行——
        由调用方传 def_start（本行是定义首行 且 下一行以 { 开头）。
    """
    s = line.strip()
    if s.endswith(";") and "{" not in line:
        if is_header:
            return "声明"
        if s.startswith("extern"):
            return "声明"
        return "调用"
    if "{" in line:
        m = DEF_RE.match(line)
        if m and m.group(1) == nm:
            return "定义"
    if def_start:
        return "定义"
    return "调用"


def scan(root, names, tag):
    """扫描一个工程，返回 {函数名: [{tag,f,l,p}, ...]}。"""
    out = {}
    if not os.path.isdir(root):
        print("  [跳过] 目录不存在:", root)
        return out
    files = (glob.glob(os.path.join(root, "**", "*.c"), recursive=True) +
             glob.glob(os.path.join(root, "**", "*.h"), recursive=True))
    files.sort()
    used = 0
    for p in files:
        low = p.lower()
        if any(s in low for s in SKIP_PARTS):
            continue
        try:
            text = io.open(p, encoding="utf-8", errors="ignore").read()
        except Exception:
            continue
        # 去注释（避免把注释里的示例调用当成真调用）
        # 把注释内容替换成等量空格而不是直接删掉，否则行号会整体错位
        #   （旧版 NVIC_Init 被报成 sys_dma.c:111，实际是 134 行——就是因为注释被吃掉后行数变了）
        text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
        # 行注释同样用空格顶掉、保留换行，避免把跨行语句粘成一行
        text = re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), text)
        rel = shorten_path(p)
        is_header = p.lower().endswith(".h")
        lines = text.split("\n")
        for i, line in enumerate(lines, 1):
            # 本行是不是"函数定义首行"（大括号在下一行）——本工程主流写法（325/372）
            nxt = lines[i].strip() if i < len(lines) else ""
            def_name = None
            if not is_header and nxt.startswith("{") and not line.strip().endswith(";"):
                dm = DEF_RE.match(line)
                if dm:
                    def_name = dm.group(1)
            for m in CALL_RE.finditer(line):
                nm = m.group(1)
                if nm not in names:
                    continue
                out.setdefault(nm, []).append(
                    {"tag": tag, "f": rel, "l": i,
                     "p": classify(line, nm, is_header, def_name == nm)})
                used += 1
    print("  扫描完成: %-8s %4d 个文件, %5d 处引用" % (tag, len(files), used))
    return out


def shorten_path(p):
    """d:\\...\\Project_1_检测端\\Project\\FWLIB\\src\\sys_i2c.c → FWLIB/src/sys_i2c.c"""
    p = os.path.normpath(p).replace("\\", "/")
    parts = p.split("/")
    best = -1
    for i, seg in enumerate(parts[:-1]):
        if seg in ANCHORS:
            best = i
    if best < 0:
        return parts[-1]
    # 锚点本身是 Project 这类通用名时不再往上一层；是板卡名时保留其下一层
    rel = "/".join(parts[best:])
    if parts[best] == "Project" and best > 0 and parts[best - 1] in ("Project_1_检测端", "Project_2_数据通信端"):
        rel = "/".join(parts[best:])
    return rel


# ---------------------------------------------------------------- 3) 路径消歧
def split_parts(s):
    return s.split("/")


def shares_suffix(a, b, k):
    """a、b 是否共享末尾 k 段（且末尾段同名）。"""
    pa, pb = split_parts(a), split_parts(b)
    if pa[-1] != pb[-1] or k > len(pa) or k > len(pb):
        return False
    return pa[-k:] == pb[-k:]


def choose(cands):
    """从候选里挑 ≤MAX_REC 条：调用优先、每工程必占名额、路径唯一且尽量短。"""
    recs = []
    # ① 每个工程各自按 (重要度分层, 路径, 行号) 排好
    for tag, _ in ROOTS:
        c = [x for x in cands if x["tag"] == tag]
        c.sort(key=lambda x: (tier(x), x["f"], x["l"]))
        recs.append(c)
    # ② 轮转挑选：第 1 轮每个工程各拿 1 个名额（保证本项目出现）
    ordered = []
    round_i = 0
    while len(ordered) < MAX_REC:
        added = False
        for c in recs:
            if round_i < len(c) and len(ordered) < MAX_REC:
                ordered.append(c[round_i])
                added = True
        if not added:
            break
        round_i += 1
    # ③ 同文件只留一条
    picked, seen = [], set()
    for r in ordered:
        key = (r["tag"], r["f"])
        if key in seen:
            continue
        seen.add(key)
        picked.append(r)
    # ④ 路径消歧：对每条，找到与"已选定"不冲突的最短后缀
    out = []
    for r in picked:
        parts = split_parts(r["f"])
        chosen = r["f"]
        for k in range(1, len(parts) + 1):
            cand = "/".join(parts[-k:])
            if not any(shares_suffix(cand, o["f"], k) for o in out):
                chosen = cand
                break
        out.append({"tag": r["tag"], "f": chosen, "l": r["l"], "p": r["p"]})
    return out


# ---------------------------------------------------------------- 4) 输出 JS
def main():
    names = collect_names()
    print("速查收录函数名:", len(names))

    allc = {}
    for tag, root in ROOTS:
        for nm, lst in scan(root, names, tag).items():
            allc.setdefault(nm, []).extend(lst)

    usage = {nm: choose(c) for nm, c in allc.items()}

    lines = [
        "/* =============================================================",
        " *  data_usage.js —— 各函数在本项目/参考工程里的使用位置",
        " *  （gen_usage.py 自动生成，勿手改；工程源码更新后重跑 gen_usage.py 即可）",
        " * -------------------------------------------------------------",
        " *  记录格式：{tag:工程标签, f:相对路径, l:行号, p:调用|定义|声明}",
        " *  标签含义：S1检测端 / S2网关 = 本项目两个工程（优先收录）",
        " *            天马库 / 04ST32    = 参考工程",
        " *  每条最多 3 处，调用优先，每个工程保证占名额。",
        " * ============================================================= */",
        "const STD_USAGE = {",
    ]
    for k in sorted(usage):
        vs = ",".join(
            '{"tag":"%s","f":"%s","l":%d,"p":"%s"}'
            % (r["tag"], r["f"].replace("\\", "/"), r["l"], r["p"])
            for r in usage[k]
        )
        lines.append('"%s":[%s],' % (k, vs))
    lines.append("};")
    lines.append("")
    io.open(OUT, "w", encoding="utf-8", newline="").write("\n".join(lines))

    # -------- 自检：本项目两个工程必须能出现
    n_tag = {}
    for v in usage.values():
        for r in v:
            n_tag[r["tag"]] = n_tag.get(r["tag"], 0) + 1
    print("含使用位置的函数: %d → %s" % (len(usage), OUT))
    print("各工程收录条数:", ", ".join("%s=%d" % (t, n_tag.get(t, 0)) for t, _ in ROOTS))
    if n_tag.get("S1检测端", 0) == 0 or n_tag.get("S2网关", 0) == 0:
        print("!! 警告：本项目工程收录为 0，请检查 ROOTS 路径是否正确")
        return 1
    n_call = sum(1 for v in usage.values() for r in v if r["p"] == "调用")
    print("其中“调用”类目 %d 条（优先展示）" % n_call)
    return 0


if __name__ == "__main__":
    sys.exit(main())
