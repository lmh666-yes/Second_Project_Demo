import io, os, re, sys
sys.stdout.reconfigure(encoding="utf-8")
H = os.path.dirname(os.path.abspath(__file__))

def rd(f):
    return io.open(os.path.join(H, f), encoding="utf-8").read()

# 1) 收集所有条目的 n 字段（这些文件里 n 后面一律是 " 字符串）
files = ["data_std_lib.js", "data_extra.js", "data_full.js", "data_tables.js", "data_tables2.js",
         "data_demo.js", "data_flow.js", "data_guide.js", "data_datasheet.js", "data_doc.js",
         "data_build.js", "data_build_b1.js", "data_build_b2.js", "data_build_qt.js"]
names = set()
for f in files:
    t = rd(f)
    got = re.findall(r'\bn:\s*"((?:[^"\\]|\\.)*)"', t)
    names |= set(got)
    print("%-22s n 条目 %4d" % (f, len(got)))
print("全部条目名去重后:", len(names))

# 2) 解析 CARD_REL：key -> [[目标, 说明], ...]
t = rd("data_rel.js")
m = re.search(r"const\s+CARD_REL\s*=\s*\{", t)
if not m:
    sys.exit("没找到 CARD_REL 定义")
# 找到配对的 }
i = m.end(); depth = 1; instr = None; esc = False
while i < len(t) and depth:
    c = t[i]
    if instr:
        if esc: esc = False
        elif c == "\\": esc = True
        elif c == instr: instr = None
    else:
        if c in "\"'": instr = c
        elif c == "{": depth += 1
        elif c == "}": depth -= 1
        elif c == "/" and t[i:i+2] == "//":
            j = t.find("\n", i); i = j if j > 0 else len(t); continue
    i += 1
body = t[m.end():i-1]

# 逐个顶层 key
keys = re.findall(r'"((?:[^"\\]|\\.)*)"\s*:\s*\[', body)
print("\nCARD_REL 顶层 key 数:", len(keys))

# 3) 校验：key 必须在 names 里；每对的目标也必须在 names 里
pairs = re.findall(r'\[\s*"((?:[^"\\]|\\.)*)"\s*,\s*"', body)
missing_key = [k for k in keys if k not in names]
missing_tgt = [p for p in pairs if p not in names]
print("key 不在条目表里的:", len(missing_key))
for k in missing_key[:10]: print("   key漏:", k)
print("联动目标不在条目表里的:", len(missing_tgt))
for k in missing_tgt[:15]: print("   目标漏:", k)
print("联动对（有说明文字的）总数:", len(pairs))
