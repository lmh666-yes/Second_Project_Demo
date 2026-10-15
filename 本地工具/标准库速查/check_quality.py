# -*- coding: utf-8 -*-
"""
check_quality.py —— 速查工具「内容质量」自检脚本

与 check_std_lib_data.py 的分工：
    check_std_lib_data.py  —— 查「名字写错没有」（标识符是否真实存在）
    check_quality.py       —— 查「内容写够没有」（空壳、缺表、无示例、解释太短）

检查项（每一项都是"翻数据手册时真的会卡住用户"的毛病）：
    Q1 空壳示例       e 字段存在但不是可编译代码（占位符/一句话/没有函数调用）
    Q2 缺参数表       Init/Config/Cmd/Set 类函数却没有 a 数组
    Q3 字段为空       n / m / d / k 等必填字段为空或只有空白
    Q4 说明太短       d（中文用途）少于 8 个汉字 —— 起不到科普作用
    Q5 关键词缺失     k 为空（会导致中文搜不到）
    Q6 数据手册卡     _t=datasheet 必须齐 h/a/s/r/k
    Q7 引导向导卡     _t=guide 必须 ≥5 步且每步字段齐
    Q8 查找表卡       _t=table 必须齐 h/a 且列数对齐
    Q9 空壳参数说明   a 里某一行的"说明"列是空的

用法：
    python check_quality.py            # 只打印汇总 + 问题明细
    python check_quality.py -v         # 额外列出每个问题条目的名字
    python check_quality.py --json     # 输出 JSON（给别的脚本用）

退出码：0 = 没有问题；1 = 有问题（方便接进批处理）
"""
import re, os, sys, json, argparse

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

ROOT = os.path.dirname(os.path.abspath(__file__))

# 参与检查的数据文件（顺序即报告里的分组顺序）
FILES = [
    ("手写外设函数", "data_std_lib.js", "STD_LIB"),
    ("详解扩展包",   "data_extra.js",   "STD_EXTRA"),
    ("流程卡",       "data_flow.js",    "STD_FLOW"),
    ("引导向导",     "data_guide.js",   "STD_GUIDE"),
    ("项目引导",     "data_build.js",   "STD_BUILD_GUIDE"),
    ("项目引导-板1", "data_build_b1.js", "STD_BUILD_B1"),
    ("项目引导-板2", "data_build_b2.js", "STD_BUILD_B2"),
    ("项目引导-Qt",  "data_build_qt.js", "STD_BUILD_QT"),
    ("数据手册卡",   "data_datasheet.js", "STD_DATASHEET"),
    ("查找表",       "data_tables.js",  "STD_TABLES"),
]

# 这些字段是字符串字面量
STR_FIELDS = ("m", "n", "p", "d", "r", "e", "k", "s")
TAG_FIELDS = ("_t",)


# ----------------------------------------------------------------------
# 极简 JS 数组解析器
#   这些数据文件的格式非常规整：顶层是 `const NAME = [` … `];`，
#   元素是 `{k:"v", k2:[...], ...}`。用正则做「括号配对扫描」比引完整
#   JS 引擎稳（本机 node 在解析这些大文件时会崩）。
# ----------------------------------------------------------------------
def read_text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def extract_array(text, varname):
    """从 text 里取出 `const varname = [ ... ];` 的数组体（不含最外层方括号）。"""
    m = re.search(r'(?:const|var|let)\s+' + re.escape(varname) + r'\s*=\s*\[', text)
    if not m:
        return None
    i = m.end()               # 指向 '[' 之后
    depth = 1
    start = i
    n = len(text)
    while i < n and depth:
        ch = text[i]
        if ch == '"' or ch == "'" or ch == "`":
            q = ch
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == q:
                    break
                i += 1
        elif ch == "[" or ch == "{":
            depth += 1
        elif ch == "]" or ch == "}":
            depth -= 1
            if depth == 0:
                return text[start:i]
        i += 1
    return text[start:]


def split_top(body, sep=","):
    """按 sep 在顶层（不在字符串/括号里）切分。"""
    out, buf, depth, i, n = [], [], 0, 0, len(body)
    while i < n:
        ch = body[i]
        if ch in "\"'`":
            q = ch
            buf.append(ch)
            i += 1
            while i < n:
                buf.append(body[i])
                if body[i] == "\\":
                    if i + 1 < n:
                        buf.append(body[i + 1])
                        i += 1
                elif body[i] == q:
                    break
                i += 1
        elif ch in "[{(":
            depth += 1
            buf.append(ch)
        elif ch in "]})":
            depth -= 1
            buf.append(ch)
        elif ch == sep and depth == 0:
            out.append("".join(buf))
            buf = []
        else:
            buf.append(ch)
        i += 1
    if "".join(buf).strip():
        out.append("".join(buf))
    return out


def find_unescaped(body, pat, start=0):
    """找顶层（不在字符串里）的第一个 pat。"""
    i, n, depth = start, len(body), 0
    while i < n:
        ch = body[i]
        if ch in "\"'`":
            q = ch
            i += 1
            while i < n:
                if body[i] == "\\":
                    i += 2
                    continue
                if body[i] == q:
                    break
                i += 1
        elif ch in "[{(":
            depth += 1
        elif ch in "]})":
            depth -= 1
        elif depth == 0 and body.startswith(pat, i):
            return i
        i += 1
    return -1


def unquote(s):
    s = s.strip()
    if len(s) >= 2 and s[0] in "\"'`" and s[-1] == s[0]:
        s = s[1:-1]
    # 还原常见转义
    return (s.replace("\\n", "\n").replace("\\t", "\t")
             .replace('\\"', '"').replace("\\'", "'").replace("\\\\", "\\"))


def strip_comments(src):
    """去掉 /* */ 与 // 注释（保留字符串里的内容，长度不变以便定位不漂移）。

    必须在 parse_card 之前做：这些文件里每个 `{...}` 元素前都有一行
    `/* ===== 分区标题 ===== */`，不剥掉的话元素会以注释开头，被当成
    "不是 { 开头" 而整个丢弃。
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        ch = src[i]
        if ch in "\"'`":
            q = ch
            out.append(ch)
            i += 1
            while i < n:
                out.append(src[i])
                if src[i] == "\\":
                    if i + 1 < n:
                        out.append(src[i + 1])
                        i += 2
                        continue
                elif src[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if ch == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))   # 保留换行，行号不飘
            i = j
            continue
        if ch == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def parse_card(src):
    """解析一个 `{...}` 元素 → dict。值是 str / list / dict。"""
    src = src.strip()
    if src.startswith("{"):
        src = src[1:]
    if src.endswith("}"):
        src = src[:-1]
    card = {}
    for item in split_top(src):
        item = item.strip()
        if not item:
            continue
        c = find_unescaped(item, ":")
        if c < 0:
            continue
        key = item[:c].strip()
        val = item[c + 1:].strip()
        if not key:
            continue
        if val.startswith("["):
            card[key] = [parse_row(r) for r in split_top(val[1:-1] if val.endswith("]") else val[1:])]
        elif val.startswith("{"):
            card[key] = parse_card(val)
        elif val[:1] in "\"'`":
            card[key] = unquote(val)
        else:
            card[key] = val      # 数字/裸量
    return card


def parse_row(src):
    src = src.strip()
    if src.startswith("["):
        src = src[1:-1] if src.endswith("]") else src[1:]
    return [parse_cell(c) for c in split_top(src)]


def parse_cell(src):
    src = src.strip()
    if src[:1] in "\"'`":
        return unquote(src)
    if src.startswith("["):
        return parse_row(src)
    return src


def load_cards(path, varname):
    body = extract_array(read_text(path), varname)
    if body is None:
        return None
    body = strip_comments(body)
    cards = []
    for chunk in split_top(body):
        chunk = chunk.strip()
        if chunk.startswith("{"):
            cards.append(parse_card(chunk))
    return cards


# ----------------------------------------------------------------------
# 检查项
# ----------------------------------------------------------------------
CJK = re.compile(r'[\u4e00-\u9fff]')
CALL_RE = re.compile(r'[A-Za-z_]\w*\s*\(')          # 形如 Foo( 的函数调用
STUB_RE = re.compile(r'省略|待补|\bTODO\b|\bTBD\b|同前|自行填|\bxxx\b|\bXXX\b')
# 参数表里的"占位行"：参数名写成这些词说明不是真参数
NOPARAM_NAME = re.compile(r'^(无参数|无|none|n/?a|-|—|－)$', re.I)
# 参数名为空 + 说明为空的组合判定：值列若是"无参数"类词才算合格
EMPTY_VAL = re.compile(r'^\s*[-—－]?\s*$')


def cjk_len(s):
    return len(CJK.findall(s or ""))


def _param_note_ok(row):
    """判断「参数说明为空」的这一行是否其实可以接受。

    数据文件里有三种正当写法：
      1) ["无参数", "-", ""]        —— 函数无参数，第三列空是合理的
      2) ["xTask","句柄",""]         —— 说明写在第二列（值列）里已经够清楚
      3) 说明列为空但整行只有两列   —— 不是参数表格式，不算问题
    真正要修的是：参数名是真实标识符、值列也只是 "-" 或空。
    """
    name = str(row[0]).strip()
    if NOPARAM_NAME.match(name):
        return True
    vals = [str(x).strip() for x in row[1:]]
    return any(v and not EMPTY_VAL.match(v) for v in vals)


def issues_of(card):
    """返回该卡片的问题列表 [(代号, 说明)]。"""
    out = []
    t = card.get("_t", "")
    n = card.get("n", "?")

    # Q3 必填字段
    for f in ("m", "n", "d"):
        if not str(card.get(f, "")).strip():
            out.append(("Q3", f"字段 {f} 为空"))

    # Q4 说明太短
    if card.get("d") and cjk_len(card["d"]) < 8:
        out.append(("Q4", f"用途说明只有 {cjk_len(card['d'])} 个汉字：{card['d'][:20]}"))

    # Q5 关键词
    if not str(card.get("k", "")).strip():
        out.append(("Q5", "关键词 k 为空（中文搜索会搜不到）"))

    a = card.get("a")
    a = a if isinstance(a, list) else []

    if t == "guide":
        # Q7 引导向导
        steps = card.get("f")
        steps = steps if isinstance(steps, list) else []
        if len(steps) < 5:
            out.append(("Q7", f"向导只有 {len(steps)} 步（应 ≥5）"))
        for i, st in enumerate(steps, 1):
            st = st if isinstance(st, list) else []
            if len(st) < 5:
                out.append(("Q7", f"第 {i} 步只有 {len(st)} 个字段（应 ≥5：标题/为什么/做什么/代码/验证）"))
            elif any(not str(x).strip() for x in st[:5]):
                out.append(("Q7", f"第 {i} 步有字段为空"))
        if not str(card.get("e", "")).strip():
            out.append(("Q7", "向导没有整套代码 e"))
    elif t == "datasheet":
        # Q6 数据手册卡
        for f in ("h", "a", "s", "r"):
            v = card.get(f)
            if v is None or (isinstance(v, (list, str)) and len(v) == 0):
                out.append(("Q6", f"数据手册卡缺 {f}"))
        h = card.get("h")
        h = h if isinstance(h, list) else []
        if h and a:
            off = [i + 1 for i, r in enumerate(a) if not isinstance(r, list) or len(r) != len(h)]
            if off:
                out.append(("Q6", f"表格列数与表头不齐（表头 {len(h)} 列，异常行 {off}）"))
    elif t == "table":
        # Q8 查找表
        for f in ("h", "a"):
            v = card.get(f)
            if v is None or (isinstance(v, list) and len(v) == 0):
                out.append(("Q8", f"查找表缺 {f}"))
        h = card.get("h")
        h = h if isinstance(h, list) else []
        if h and a:
            off = [i + 1 for i, r in enumerate(a) if not isinstance(r, list) or len(r) != len(h)]
            if off:
                out.append(("Q8", f"表格列数与表头不齐（表头 {len(h)} 列，异常行 {off}）"))
    else:
        # Q2 缺参数表
        if re.search(r'(Init|Config|Cmd|Set|Enable|Open|Start)$', n) and not a:
            out.append(("Q2", "这类函数应该有参数表 a，但为空"))

        # Q1 空壳示例
        e = card.get("e")
        if e is not None and str(e).strip():
            e = str(e)
            if not CALL_RE.search(e):
                out.append(("Q1", f"示例里没有任何函数调用：{e[:40]}"))
            elif STUB_RE.search(e):
                out.append(("Q1", f"示例含占位符（省略/待补/自行…）：{e[:40]}"))
            elif len(e.strip()) < 12:
                out.append(("Q1", f"示例太短（{len(e.strip())} 字符）：{e[:40]}"))

    # Q9 参数说明列为空（只对 a 里"行"是数组的情形）
    if a and t not in ("table", "datasheet"):
        for i, r in enumerate(a, 1):
            if not isinstance(r, list) or len(r) < 3:
                continue
            if str(r[2]).strip():
                continue
            if _param_note_ok(r):
                continue
            out.append(("Q9", f"第 {i} 个参数「{r[0]}」没有说明文字（值列也是空的）"))

    return out


# ----------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", "--verbose", action="store_true", help="列出问题条目名")
    ap.add_argument("--json", action="store_true", help="输出 JSON")
    ap.add_argument("--limit", type=int, default=30, help="每个检查项最多列几条（配合 -v）")
    args = ap.parse_args()

    all_issues = {}       # 代号 -> [(组, 卡片名, 说明)]
    groups = []           # (组名, 卡数)
    total = 0

    for group, fname, var in FILES:
        path = os.path.join(ROOT, fname)
        if not os.path.exists(path):
            groups.append((group, -1))
            continue
        cards = load_cards(path, var)
        if cards is None:
            groups.append((group, -2))
            continue
        groups.append((group, len(cards)))
        total += len(cards)
        for c in cards:
            for code, msg in issues_of(c):
                all_issues.setdefault(code, []).append((group, c.get("n", "?"), msg))

    CODES = {
        "Q1": "空壳示例（不是可编译代码）",
        "Q2": "Init/Config 类函数缺参数表",
        "Q3": "必填字段为空",
        "Q4": "中文用途说明太短（<8 汉字）",
        "Q5": "关键词 k 为空（中文搜不到）",
        "Q6": "数据手册卡字段不全",
        "Q7": "引导向导步骤不全",
        "Q8": "查找表字段/列数不齐",
        "Q9": "参数说明列为空",
    }

    if args.json:
        print(json.dumps({
            "groups": groups, "total": total,
            "issues": {k: [list(x) for x in v] for k, v in all_issues.items()},
        }, ensure_ascii=False, indent=2))
        return 1 if all_issues else 0

    print("=" * 66)
    print("速查工具内容质量自检")
    print("=" * 66)
    print("\n各数据文件卡片数：")
    for group, cnt in groups:
        label = {0: "", -1: "（文件不存在）", -2: "（解析失败）"}.get(cnt if cnt <= 0 else 1, "")
        if cnt < 0:
            print(f"  {group:<14} {label}")
        else:
            print(f"  {group:<14} {cnt} 张")
    print(f"  {'合计':<14} {total} 张")

    print("\n检查结果：")
    for code in sorted(CODES):
        lst = all_issues.get(code, [])
        mark = "✔" if not lst else "✗"
        print(f"  {mark} {code} {CODES[code]:<28} {len(lst)} 处")

    if all_issues:
        print("\n" + "-" * 66)
        print("问题明细")
        print("-" * 66)
        for code in sorted(all_issues):
            lst = all_issues[code]
            print(f"\n[{code}] {CODES[code]} —— 共 {len(lst)} 处")
            if args.verbose:
                for group, name, msg in lst[:args.limit]:
                    print(f"   · [{group}] {name}: {msg}")
                if len(lst) > args.limit:
                    print(f"   … 还有 {len(lst) - args.limit} 处（用 --limit 调整）")
            else:
                byname = {}
                for group, name, msg in lst:
                    byname.setdefault(group, 0)
                    byname[group] += 1
                for g, c in byname.items():
                    print(f"   · {g}: {c} 处")
                print("   （加 -v 看具体是哪些条目）")

    print("\n" + "=" * 66)
    if all_issues:
        n = sum(len(v) for v in all_issues.values())
        print(f"结论：发现 {n} 处可改进项，涉及 {len(all_issues)} 类问题。")
        print("提示：本脚本查的是「内容够不够」，名字对不对请跑 check_std_lib_data.py")
        return 1
    print("结论：✔ 未发现内容质量问题。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
