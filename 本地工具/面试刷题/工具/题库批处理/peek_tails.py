# -*- coding: utf-8 -*-
"""查看两份 03_编程画图大题.md 的结尾与统计行，并核对两文件是否一致"""
import sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

FILES = [
    r"D:\github\面试刷题\面试题刷题软件-主观题\题库\03_编程画图大题.md",
    r"D:\github\面试刷题\面试题刷题软件-大题\题库\03_编程画图大题.md",
]
for p in FILES:
    lines = open(p, encoding="utf-8").read().splitlines()
    print("=" * 20, p, "总行数", len(lines))
    for i, l in enumerate(lines[-40:], start=len(lines) - 39):
        print(i, l[:150])

a = open(FILES[0], encoding="utf-8").read()
b = open(FILES[1], encoding="utf-8").read()
print("两文件一致:", a == b, "| len:", len(a), len(b))
