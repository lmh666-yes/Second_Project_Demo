# -*- coding: utf-8 -*-
src = open(r"D:\github\面试刷题\工具\题库批处理\append_obj_295_314.py", encoding="utf-8").read()
lines = src.split("\n")
out = []
n = 0
for i, ln in enumerate(lines, 1):
    if "来源" in ln:
        n += 1
        out.append(f"#{n} L{i}: {repr(ln)}")
out.append(f"total source-ish lines = {n}")
ref = "**来源：** 查漏补缺补充题（数据结构与算法，2026-10-05）"
out.append(f"NEW-style match count in whole file = {src.count(ref)}")
open(r"D:\github\面试刷题\工具\题库批处理\_diag_src_out.txt", "w", encoding="utf-8").write("\n".join(out))
print("done")
