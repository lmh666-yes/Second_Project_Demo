# -*- coding: utf-8 -*-
"""大题批次1：题库.json 字段与代码完整性抽查。"""
import json, io, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

p = r"D:\github\面试刷题\面试题刷题软件-大题\题库.json"
bank = json.load(io.open(p, encoding="utf-8"))
d = {it["num"]: it for it in bank}
need = [6, 7, 8, 29, 32, 33, 50, 51, 58, 75, 124, 125, 131, 132, 137, 139, 147,
        179, 180, 196, 197, 200, 201, 208, 209, 264, 334, 362, 363, 365, 379,
        380, 386]
code_nums = {6, 29, 32, 33, 50, 51, 75, 147, 208, 209, 264, 334, 386}

bad = []
for n in need:
    it = d.get(n)
    if not it:
        bad.append((n, "missing")); continue
    if not it["analysis"]: bad.append((n, "analysis empty"))
    if not it["answer"]: bad.append((n, "answer empty"))
    if not it["explain"]: bad.append((n, "explain empty"))
    if "\n" in it["analysis"]: bad.append((n, "analysis multiline"))
print("字段检查:", "OK（33 题 analysis/answer/explain 非空、解析单行）" if not bad else bad)

print("--- 含代码题 answer 摘要 ---")
for n in sorted(code_nums):
    a = d[n]["answer"]
    print(f"#{n} len={len(a)} 头[{a[:60].replace(chr(10), ' ')}] 尾[{a[-60:].replace(chr(10), ' ')}]")

print("--- 样例全文（#147 / #264） ---")
print("#147:", d[147]["answer"])
print("#264:", d[264]["answer"])
