# -*- coding: utf-8 -*-
"""检查目标 md 的行尾格式与插入锚点/统计行的唯一性。"""
import sys
sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)

MD = r"D:\github\面试刷题\面试题刷题软件-大题\题库\03_编程画图大题.md"
data = open(MD, "rb").read()
text = data.decode("utf-8")
print("bytes:", len(data))
print("crlf:", data.count(b"\r\n"), "lf:", data.count(b"\n"))
print("ends with newline:", data.endswith(b"\n"))
print("anchor count:", text.count("## 附：统计"))
print("stat131 count:", text.count("- 合计：**131 题**"))
print("part4 count:", text.count("- 第四部分（附加面试题·面试题5/6/7）：**10 题** —— 编号 496~505"))
print("big505 count:", text.count("**505.**"))
i = text.find("## 附：统计")
print("tail before anchor:", repr(text[i - 30:i]))
