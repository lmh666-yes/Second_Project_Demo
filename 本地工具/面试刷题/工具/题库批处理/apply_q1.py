# -*- coding: utf-8 -*-
"""面试题1（2026面试题-1 C 语言试题）核对落地：
- 02 主观 md：#250~258 补 9 条来源行
- 03 大题 md：#453/#454 补 2 条来源行（两份副本同步）
- 01 客观 md：#112 已有引用（2026面试题-1 第 6 题），无需修改
- 重建三个题库
"""
import io, sys, os, re, subprocess, hashlib, shutil
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', line_buffering=True)
B = r"D:\github\面试刷题"
SMD = B + r"\面试题刷题软件-主观题\题库\02_主观题.md"
DMD_A = B + r"\面试题刷题软件-大题\题库\03_编程画图大题.md"
DMD_B = B + r"\面试题刷题软件-主观题\题库\03_编程画图大题.md"
OMD = B + r"\面试题刷题软件-客观题\题库\01_客观题_选择题.md"

# ---- 0) 改前副本一致性 ----
a = open(DMD_A, 'rb').read()
b = open(DMD_B, 'rb').read()
assert a == b, "03 副本改前不一致，中止"
print("03 副本改前一致 ✓", hashlib.md5(a).hexdigest())

def bounds(t, nl, num):
    i = t.find('**%d.**' % num)
    assert i >= 0, f"#{num} 未找到"
    cands = []
    m1 = re.search(re.escape(nl) + re.escape(nl) + '---', t[i:])
    if m1:
        cands.append(i + m1.start())
    m2 = re.search(r'(?:\r?\n){2}\*\*\d+\.\*\*', t[i + 10:])
    if m2:
        cands.append(i + 10 + m2.start())
    assert cands, f"#{num} 找不到边界"
    return i, min(cands)

def section_of(t, i):
    j = t.rfind('\n###', 0, i)
    if j < 0:
        return '(无小节)'
    line = t[j + 1:t.find('\n', j + 1)]
    return line[:80]

# ---- 1) 02 主观 ----
M_S = {num: f"2026面试题-1 C 语言试题第 {k} 题" for num, k in
       [(250, 1), (251, 2), (252, 3), (253, 4), (254, 5), (255, 7), (256, 8), (257, 9), (258, 10)]}
KEY_S = {250: 'bufa', 251: 'work', 252: 'test1', 253: 'PT', 254: '++a',
         255: 'fun', 256: 'pstr', 257: 'Switch', 258: 'aa'}

t = open(SMD, 'rb').read().decode('utf-8')
nl = '\r\n' if '\r\n' in t[:5000] else '\n'
print("02 md nl =", repr(nl))
for num, k in [(250, 1)]:
    i, end = bounds(t, nl, num)
    print("  #250 所在小节:", section_of(t, i))
for num in sorted(M_S, reverse=True):
    i, end = bounds(t, nl, num)
    seg = t[i:end]
    assert KEY_S[num] in seg, f"主观 #{num} 内容关键词不匹配"
    assert '**来源：**' not in seg, f"主观 #{num} 已有来源行，中止"
    assert len(seg) < 3000, f"主观 #{num} 边界异常过长({len(seg)})"
    t = t[:end] + nl + '**来源：** %s' % M_S[num] + t[end:]
open(SMD, 'wb').write(t.encode('utf-8'))
t2 = open(SMD, encoding='utf-8').read()
for num, s in M_S.items():
    i, end = bounds(t2, nl, num)
    assert s in t2[i:end], f"主观 #{num} 复验失败"
print(f"OK 主观：{len(M_S)} 条来源行已插入并复验")

# ---- 2) 03 大题（改大题副本 → 同步主观副本）----
M_D = {453: "2026面试题-1 C 语言试题第 11 题", 454: "2026面试题-1 C 语言试题第 12 题"}
KEY_D = {453: '拼接', 454: '括号'}
t = open(DMD_A, 'rb').read().decode('utf-8')
nl = '\r\n' if '\r\n' in t[:5000] else '\n'
print("03 md nl =", repr(nl))
i, end = bounds(t, nl, 453)
print("  #453 所在小节:", section_of(t, i))
for num in sorted(M_D, reverse=True):
    i, end = bounds(t, nl, num)
    seg = t[i:end]
    assert KEY_D[num] in seg, f"大题 #{num} 内容关键词不匹配"
    assert '**来源：**' not in seg, f"大题 #{num} 已有来源行，中止"
    assert len(seg) < 8000, f"大题 #{num} 边界异常过长({len(seg)})"
    t = t[:end] + nl + '**来源：** %s' % M_D[num] + t[end:]
open(DMD_A, 'wb').write(t.encode('utf-8'))
t2 = open(DMD_A, encoding='utf-8').read()
for num, s in M_D.items():
    i, end = bounds(t2, nl, num)
    assert s in t2[i:end], f"大题 #{num} 复验失败"
shutil.copyfile(DMD_A, DMD_B)
h1 = hashlib.md5(open(DMD_A, 'rb').read()).hexdigest()
h2 = hashlib.md5(open(DMD_B, 'rb').read()).hexdigest()
assert h1 == h2
print(f"OK 大题：{len(M_D)} 条来源行已插入；副本已同步（{h1}）")

# ---- 3) 01 客观 #112 ----
t = open(OMD, encoding='utf-8').read()
assert '2026面试题-1 第 6 题' in t, "客观 #112 未找到既有引用"
print("OK 客观：#112 已引用 2026面试题-1 第 6 题，无需修改")

# ---- 4) 重建三个题库 ----
print()
for folder in [r"面试题刷题软件-主观题", r"面试题刷题软件-大题", r"面试题刷题软件-客观题"]:
    cwd = os.path.join(B, folder)
    r = subprocess.run([sys.executable, 'core\\build_bank.py'], cwd=cwd,
                       capture_output=True, text=True, encoding='utf-8', errors='replace')
    out = (r.stdout or '').strip().splitlines()
    print(f"[{folder}]", ' | '.join(out) if out else f"EXIT {r.returncode} {r.stderr[:200]}")
print("全部完成")
