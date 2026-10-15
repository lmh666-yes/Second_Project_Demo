# -*- coding: utf-8 -*-
"""大题批次1 改造自检：逐题核对 题目解析/标准答案/得分点 标记与残留。"""
import io, re, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

P = r"D:\github\面试刷题\面试题刷题软件-大题\题库\03_编程画图大题.md"
NEW = [6, 7, 8, 29, 32, 33, 50, 51, 58, 75, 124, 125, 131, 132, 137, 139, 147,
       179, 180, 196, 197, 200, 201, 208, 209, 264, 334, 362, 363, 365, 379,
       380, 386]
lines = io.open(P, encoding="utf-8").read().split("\n")

# 按 **N.** 行定位各题块（跳过代码块）
idx = {}
in_code = False
for i, ln in enumerate(lines):
    s = ln.rstrip()
    if s.strip().startswith("```"):
        in_code = not in_code
    if in_code:
        continue
    m = re.match(r"^\*\*(\d+)\.\*\*", s)
    if m:
        idx.setdefault(int(m.group(1)), i)

def block(num):
    start = idx[num]
    end = len(lines)
    in_code = False
    for j in range(start + 1, len(lines)):
        s = lines[j].rstrip()
        if s.strip().startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            continue
        if s.strip() == "---" or s.startswith("#") or re.match(r"^\*\*\d+\.\*\*", s):
            end = j
            break
    return lines[start:end]

bad = []
for n in NEW:
    blk = block(n)
    a = [i for i, l in enumerate(blk) if l.startswith("**题目解析：**")]
    b = [i for i, l in enumerate(blk) if l.startswith("**标准答案：**")]
    c = [i for i, l in enumerate(blk) if l.startswith("**得分点：**")]
    r = [i for i, l in enumerate(blk) if l.startswith("**参考答案：**")]
    if len(a) != 1: bad.append((n, "题目解析数", len(a)))
    if len(b) != 1: bad.append((n, "标准答案数", len(b)))
    if len(c) != 1: bad.append((n, "得分点数", len(c)))
    if r: bad.append((n, "参考答案残留", len(r)))
    if a and b and b[0] != a[0] + 1: bad.append((n, "解析与答案不相邻", a, b))
    if a and len(blk[a[0]]) == len("**题目解析：**"): bad.append((n, "解析为空"))
    if b and len(blk[b[0]]) == len("**标准答案：**"): bad.append((n, "答案为空"))

# 围栏成对检查（全文件）
fence = sum(1 for l in lines if l.strip().startswith("```"))
print("全文件围栏行数:", fence, "(应为偶数)" if fence % 2 == 0 else "(!! 奇数，可能有未闭合围栏)")
print("旧参考答案标记全文件计数:", sum(1 for l in lines if re.match(r"^\*\*参考答案：\*\*", l)))
print("本批 33 题检查:", "全部通过" if not bad else bad)
