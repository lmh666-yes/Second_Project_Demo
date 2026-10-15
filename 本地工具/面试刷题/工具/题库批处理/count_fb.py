# -*- coding: utf-8 -*-
"""全库标记统计：题目解析/标准答案/残留参考答案。"""
import io, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
md = io.open(r"D:\github\面试刷题\面试题刷题软件-主观题\题库\02_主观题.md", encoding="utf-8").read()
lines = md.split("\n")
import re
n_ana = sum(1 for s in lines if s.startswith("**题目解析：**"))
n_std = sum(1 for s in lines if s.startswith("**标准答案：**"))
refs = [(i, s) for i, s in enumerate(lines)
        if s.startswith("**参考答案：**") or s.startswith("**参考译文：**")]
print("题目解析行:", n_ana, "| 标准答案行:", n_std, "| 残留参考答案行:", len(refs))

cur = None
by_block = {}
for i, s in enumerate(lines):
    m = re.match(r"^\*\*(\d+)\.\*\*", s)
    if m:
        cur = m.group(1)
    if s.startswith("**参考答案：**") or s.startswith("**参考译文：**"):
        by_block.setdefault(cur, []).append(i + 1)
dups = {k: v for k, v in by_block.items() if len(v) > 1}
print("题目数(残留):", len(by_block), "| 多标记题目:", dups)
