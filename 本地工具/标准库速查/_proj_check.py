# -*- coding: utf-8 -*-
"""_proj_check.py —— 单独校验 data_proj_cards*.py（本项目 FWLIB 模块的卡）。

为什么不直接用 _gen3.py：
    _gen3.py 把【所有】文本源拼在一起才做 node 自检，任何一个人写坏都会把别人
    一起卡住。写卡的人要能只校验自己那个文件，所以有了这个脚本。

用法：
    python _proj_check.py                      # 校验全部 data_proj_cards*.py
    python _proj_check.py data_proj_cards_b.py # 只校验指定文件

检查内容：
  1. 每个 FLOW_xxx / GUIDE_xxx 常量必须能被 node 真解析（包成 const A=[...] 再看）；
  2. 每个对象必须有 m / t / n / d / k 五个键；
  3. t:"flow" 与 t:"guide" 必须有 f 数组；guide 的每一步必须是 7 元组；
  4. 代码字段里不能出现【字面的】反斜杠 n —— 那是换行没写对，
     页面上代码框会整段挤成一行（data_extra_cards.py 上出现过一次）。
     C 源码里想显示字面 CRLF 用「双反斜杠 r 双反斜杠 n」，不被这条判为错误。
"""
import io, os, re, sys, glob, subprocess

sys.stdout.reconfigure(encoding="utf-8")
HERE = os.path.dirname(os.path.abspath(__file__))

# --- 从 _gen3.py 抄来的同一套 normalize（保证两边判定一致） ---
def normalize(text):
    out, i, n, instr = [], 0, len(text), False
    while i < n:
        c = text[i]
        if not instr:
            if c == '"':
                instr = True
            out.append(c)
            i += 1
            continue
        if c == "\\":
            out.append(text[i:i + 2])
            i += 2
            continue
        if c == '"':
            instr = False
            out.append(c)
            i += 1
            continue
        if c == "\n":
            out.append("\\n"); i += 1; continue
        if c == "\r":
            out.append("\\r"); i += 1; continue
        out.append(c)
        i += 1
    return "".join(out)


def consts(path):
    t = io.open(path, encoding="utf-8", newline="").read()
    got = []
    for m in re.finditer(r'^(FLOW_\w+|GUIDE_\w+) = (r?)"""', t, re.M):
        name, raw = m.group(1), m.group(2)
        st = m.end()
        en = t.find('"""', st)
        if en < 0:
            raise SystemExit("%s: %s 三引号没闭合" % (path, name))
        got.append((name, raw, t[st:en]))
    return got


def node_parse(text, label):
    tmp = os.path.join(HERE, "_chk_%s.js" % label)
    io.open(tmp, "w", encoding="utf-8", newline="").write(
        "const A=[\n" + text + "\n];\n"
        "let e=0;\n"
        "A.forEach((o,i)=>{['m','t','n','d','k'].forEach(key=>{"
        "if(o[key]===undefined){console.log('缺少 '+key+' @'+i+' '+o.n);e++;}});"
        "if(o.t==='flow'&&!Array.isArray(o.f)){console.log('flow 缺 f @'+i+' '+o.n);e++;}"
        "if(o.t==='guide'&&!Array.isArray(o.f)){console.log('guide 缺 f @'+i+' '+o.n);e++;}"
        "if(o.t==='guide'&&o.f.some(r=>!Array.isArray(r)||r.length!==7))"
        "{console.log('guide 步骤不是7元组 @'+i+' '+o.n);e++;}"
        "if(o.t==='flow'&&!Array.isArray(o.a)){console.log('flow 缺 a @'+i+' '+o.n);e++;}"
        "});\n"
        "console.log(A.length+' 个对象, 错误 '+e);\n")
    r = subprocess.run(["node", tmp], capture_output=True, text=True, encoding="utf-8")
    print("  [node] " + (r.stdout or "").strip().replace("\n", "\n  [node] "))
    if r.returncode != 0:
        print("  [node] stderr: " + (r.stderr or "").strip()[:800])
    os.remove(tmp)
    return r.returncode == 0 and "错误 0" in (r.stdout or "")


def scan_literal_n(txt, name):
    """找出【字面的】反斜杠 n（换行没写对）。C 源码里的双反斜杠 r 双反斜杠 n 放过。"""
    ph = "\x01CRLF\x01"
    t = txt.replace("\\\\r\\\\n", ph)
    hits = [m.start() for m in re.finditer(r"\\\\n", t)]
    return hits


files = sys.argv[1:] or [os.path.basename(p) for p in
                         sorted(glob.glob(os.path.join(HERE, "data_proj_cards*.py")))]
if not files:
    raise SystemExit("没找到任何 data_proj_cards*.py")

all_ok = True
for f in files:
    path = f if os.path.isabs(f) else os.path.join(HERE, f)
    print("=== %s" % os.path.basename(path))
    items = consts(path)
    if not items:
        print("  里面没有 FLOW_xxx / GUIDE_xxx 常量")
        all_ok = False
        continue
    flows, guides = [], []
    for name, raw, txt in items:
        if not raw:
            print("  %s 用的是【非 raw】三引号，请改成 r\"\"\"，否则很容易写成双反斜杠 n"
                  % name)
        bad = scan_literal_n(txt, name)
        if bad:
            all_ok = False
            print("  ✗ %s 里有 %d 处【字面的】反斜杠 n（换行写错了）：" % (name, len(bad)))
            for p in bad[:3]:
                print("      ..." + repr(txt[max(0, p - 50):p + 20]))
        n = len(re.findall(r'\{m:', txt))
        print("  %-28s %7d 字符  对象=%d" % (name, len(txt), n))
        (flows if name.startswith("FLOW_") else guides).append(txt)
    if flows and not node_parse(normalize(",\n".join(flows)), "pflow"):
        all_ok = False
    if guides and not node_parse(normalize(",\n".join(guides)), "pguide"):
        all_ok = False

print()
print("全部通过" if all_ok else "有问题，先修好再往下走")
sys.exit(0 if all_ok else 1)
