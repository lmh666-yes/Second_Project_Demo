"""最终定稿 v3（本次成功版）。

根因（v2 为什么坏）：
    两个文本源的三个字符串用的是【不同】的 Python 引号风格，
       add_cards.py        : FLOW_NEW  = r\"\"\"...\"\"\"   ← raw，里面的 \" 就是字面的 反斜杠+引号
       data_extra_cards.py : FLOW_DAC  = \"\"\"...\"\"\"     ← 非 raw，Python 已经把 \" 变成 "、\\n 变成真换行
    而 v2 对两者都套了一层 unescape()，等于把 add_cards.py 里本来就正确的
    JS 转义（\" 与 \\n）又解了一遍 —— 结果是写进 data_*.js 后字符串提前闭合，
    且键名被写成 m:\\"流程\\" 这种双反斜杠形态，卡片计数还是 12。

正确做法：把两个源都当成【一段 JS 字面量文本】，原样拼接、绝不 unescape，
    只补一个逗号分隔符，然后直接交给 node 自己去解析验证。
    这样 add_cards.py 的 \" \\n（已是合法 JS 转义）与
    data_extra_cards.py 经 Python 解析后剩下的真换行（由 normalize 转成 \\n）
    都能自洽。

用法：
    python _gen3.py --check     只自检（用 node 真解析每个卡对象）
    python _gen3.py             去掉上次生成区 -> 重新插入 -> node --check

生成区就写在数组末尾，重复运行时先按本次文本把它整段去掉再插回去，
因此不需要再额外保存一份只有基线卡的 .bak 干净副本。
"""
import io, os, re, sys, subprocess, glob
sys.stdout.reconfigure(encoding="utf-8")
HERE = os.path.dirname(os.path.abspath(__file__))


def body(path, name):
    t = io.open(path, encoding="utf-8", newline="").read()
    m = re.search(r"^" + name + r" = r?\"\"\"", t, re.M)
    if not m:
        raise SystemExit("找不到 " + name + " in " + path)
    st = m.end()
    return t[st:t.find('"""', st)]


AP = os.path.join(HERE, "add_cards.py")
EX = os.path.join(HERE, "data_extra_cards.py")

def _piece(txt):
    """去掉片段结尾的空白与逗号。

    data_extra_cards.py 里的每张卡结尾本来就带 "},\\n"，
      如果直接拼 ",", 就会得到 "},\\n,\\n{..." —— JS 里这是【数组空洞】
      （稀疏数组），长度会虚增、for 循环里拿到 undefined。
    """
    t = txt.rstrip()
    while t.endswith(","):
        t = t[:-1].rstrip()
    return t


def fix_ex(txt):
    """修补 data_extra_cards.py 的换行（2026-10 发现的真 bug）。

    现象：DAC / CAN / RTC 三张卡 + 对应三个向导，在页面上的代码框整段没有换行，
    看到的是一长条带反斜杠 n 的文字。

    根因：本脚本是按【原始文本】直接拼接的，从不做 Python 解析；而
    data_extra_cards.py 偏偏写成【非 raw 三引号 + 双反斜杠 n】—— 那是「指望
    Python 帮我解析一层」的写法。两边假设不一致，于是双反斜杠 n 被原样
    写进 .js，JS 解出来就是字面的「反斜杠 + n」，而不是换行。

    证据：data_flow.js 里 ⑮DAC / ⑯CAN / ⑰RTC 的 e 字段分别有 61 / 115 / 117
    处字面反斜杠 n，而 ①~⑭ 全是真换行（add_cards.py 用 raw 三引号 + 单反斜杠 n，
    本来就对，所以绝不能在它身上做同样的事）。

    做法：只把「双反斜杠 n」降一级成「单反斜杠 n」（= JS 的真换行）。
    C 源码里想显示字面 CRLF 的那 3 处「双反斜杠 r 双反斜杠 n」先换出、最后还原，
    否则会被降级成真回车换行。
    """
    ph = "\x01CRLF\x01"
    t = txt.replace("\\\\r\\\\n", ph)
    t = t.replace("\\\\n", "\\n")
    return t.replace(ph, "\\\\r\\\\n")


# ---------- 本项目 FWLIB 模块的卡（⑱ 起）----------
# 文件名形如 data_proj_cards*.py，里面的 FLOW_xxx / GUIDE_xxx 常量会被自动发现，
# 所以以后加新文件、加新卡片都不用再改本脚本。写法统一照 add_cards.py：
# raw 三引号 + 单反斜杠 n。校验用 `python _proj_check.py`。
PJ_FILES = sorted(glob.glob(os.path.join(HERE, "data_proj_cards*.py")))


def pj_consts():
    flows, guides = [], []
    for path in PJ_FILES:
        t = io.open(path, encoding="utf-8", newline="").read()
        for m in re.finditer(r'^(FLOW_\w+|GUIDE_\w+) = r?"""', t, re.M):
            name = m.group(1)
            st = m.end()
            en = t.find('"""', st)
            if en < 0:
                raise SystemExit("%s 的 %s 三引号没闭合" % (path, name))
            (flows if name.startswith("FLOW_") else guides).append(t[st:en])
    return flows, guides


PJ_FLOW, PJ_GUIDE = pj_consts()


# ---------- 组文本：全部当字面 JS 文本 ----------
FLOW = ",\n".join(_piece(body(AP, n)) for n in ("FLOW_NEW",)) + ",\n" \
       + ",\n".join(_piece(fix_ex(body(EX, n))) for n in ("FLOW_DAC", "FLOW_CAN", "FLOW_RTC_ALARM"))
GUIDE = ",\n".join(_piece(body(AP, n)) for n in ("GUIDE_NEW",)) + ",\n" \
        + ",\n".join(_piece(fix_ex(body(EX, n))) for n in ("GUIDE_DAC", "GUIDE_CAN", "GUIDE_RTC_ALARM"))
if PJ_FLOW:
    FLOW += ",\n" + ",\n".join(_piece(x) for x in PJ_FLOW)
if PJ_GUIDE:
    GUIDE += ",\n" + ",\n".join(_piece(x) for x in PJ_GUIDE)
print("项目模块卡来源: %s（流程 %d / 引导 %d）" % (
    ", ".join(os.path.basename(p) for p in PJ_FILES) or "无", len(PJ_FLOW), len(PJ_GUIDE)))


def normalize(text):
    """只把【字符串内部】的裸换行/回车转成 \\n / \\r。

    data_extra_cards.py 是非 raw 字符串，Python 已经把 \\n 变成真换行；
    直接写进 JS 会导致字符串跨行。这里逐字符扫描，只在未转义的 " 之间替换，
    不动字符串外的排版换行。add_cards.py 的部分本来就是 \\n，扫不到裸换行，无损。
    """
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


FLOW = normalize(FLOW)
GUIDE = normalize(GUIDE)

for tag, s in (("FLOW", FLOW), ("GUIDE", GUIDE)):
    print("%-6s %6d 字符  m:计数=%d" % (tag, len(s), s.count('m:"')))


def node_parse(text, label):
    """把这段文本包成数组交给 node 真解析，并检查每个对象的关键字段。"""
    tmp = os.path.join(HERE, "_chk_%s.js" % label)
    io.open(tmp, "w", encoding="utf-8", newline="").write(
        "const A=[\n" + text + "\n];\n"
        "let e=0;\n"
        "A.forEach((o,i)=>{['m','t','n','d','k'].forEach(key=>{"
        "if(o[key]===undefined){console.log('缺少 '+key+' @'+i+' '+o.n);e++;}});"
        "if(o.t==='flow'&&!Array.isArray(o.f)){console.log('flow 缺 f @'+i);e++;}"
        "if(o.t==='guide'&&!Array.isArray(o.f)){console.log('guide 缺 f @'+i);e++;}"
        "if(o.t==='guide'&&o.f.some(r=>r.length!==7)){console.log('guide 步骤不是7元组 @'+i);e++;}"
        "});\n"
        "console.log(A.length+' 个对象, 错误 '+e);\n")
    r = subprocess.run(["node", tmp], capture_output=True, text=True, encoding="utf-8")
    print("  [node] " + label + ": " + (r.stdout or "").strip().replace("\n", "\n  [node] "))
    if r.returncode != 0:
        print("  [node] stderr: " + r.stderr.strip()[:600])
    os.remove(tmp)
    return r.returncode == 0 and "错误 0" in (r.stdout or "")


ok_f = node_parse(FLOW, "flow")
ok_g = node_parse(GUIDE, "guide")
if not (ok_f and ok_g):
    raise SystemExit("文本本身不合法，先修文本源，再往下插")
print("文本自检通过（node 真解析每个卡对象）")

if "--check" in sys.argv:
    raise SystemExit(0)

# ---------- 去掉上次的生成区，回到基线卡 ----------
BASE_CARDS = 12          # 基线卡数（①~⑫）；旧版脚本是按这个数校验 .bak 的


def strip_generated(text, new_text):
    """把上一次插入的生成区整段去掉，返回只剩基线卡的文本。

    这次要插入的内容原样写在数组末尾，取它开头 200 个字符做探针，
    rfind 命中处就是生成区的起点；探针找不到说明是第一次运行，原样返回。
    """
    probe = new_text.strip()[:200]
    p = text.rfind(probe)
    if p < 0:
        return text
    head = text[:p]
    head = head[:head.rfind("\n") + 1].rstrip()
    while head.endswith(","):
        head = head[:-1].rstrip()
    return head + "\n];\n"


def insert(path, new_text):
    t = io.open(path, encoding="utf-8", newline="").read()
    # 只认行首的 ]; —— 卡里的代码示例含大量 "];"（如 uint8_t buf[8];），
    #   用 rfind("];") 会命中字符串里那个，把新卡插到数组外面（v1 的 bug）。
    p = t.rfind("\n];")
    if p < 0:
        raise SystemExit("找不到数组结尾: " + path)
    head = t[:p].rstrip()
    if not head.endswith(","):
        head += ","
    io.open(path, "w", encoding="utf-8", newline="").write(head + "\n" + new_text + t[p:])
    return len(t)


for name, txt in (("data_flow.js", FLOW), ("data_guide.js", GUIDE)):
    p = os.path.join(HERE, name)
    t0 = io.open(p, encoding="utf-8", newline="").read()
    t1 = strip_generated(t0, txt)
    n_base = t1.count('m:"')
    if n_base != BASE_CARDS:
        raise SystemExit("%s 基线卡数 %d，不是 %d，先手工确认数组内容" % (name, n_base, BASE_CARDS))
    io.open(p, "w", encoding="utf-8", newline="").write(t1)
    old = insert(p, txt)
    t = io.open(p, encoding="utf-8", newline="").read()
    want = BASE_CARDS + txt.count('m:"')
    if t.count('m:"') != want:
        raise SystemExit("%s 插入后卡数 %d，期望 %d" % (name, t.count('m:"'), want))
    print("%-15s %d -> %d 字节   基线 %d + 生成 %d = %d 条目" % (
        name, old, len(t), BASE_CARDS, txt.count('m:"'), t.count('m:"')))

for name in ("data_flow.js", "data_guide.js"):
    r = subprocess.run(["node", "--check", os.path.join(HERE, name)],
                       capture_output=True, text=True, encoding="utf-8")
    print("node --check %-15s exit=%d %s" % (name, r.returncode, (r.stderr or "").strip()[:300]))
