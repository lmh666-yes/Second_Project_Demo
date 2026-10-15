# -*- coding: utf-8 -*-
"""抽查 题库.json：506~515 的 analysis/answer/explain 非空、answer 含代码、分区正确、无重复号。"""
import json, sys
from collections import Counter
sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)

BANK = r"D:\github\面试刷题\面试题刷题软件-大题\题库.json"
bank = json.load(open(BANK, encoding="utf-8"))
by = {it["num"]: it for it in bank}
print("total:", len(bank))

ok = True
for n in range(506, 516):
    it = by.get(n)
    if it is None:
        print(n, "MISSING ITEM"); ok = False; continue
    bad = []
    if not it["analysis"].strip(): bad.append("analysis")
    if not it["answer"].strip():   bad.append("answer")
    if not it["explain"].strip():  bad.append("explain")
    if "#include" not in it["answer"]: bad.append("no-#include")
    if it["sec"] != "四、查漏补缺补充题（数据结构 / 排序 / 查找 10 题，编号 506~515）":
        bad.append("sec")
    print(n, "OK" if not bad else ("PROBLEM: " + ",".join(bad)),
          "| analysis", len(it["analysis"]), "| answer", len(it["answer"]))
    if bad: ok = False

c = Counter(it["num"] for it in bank)
dups = [n for n, k in c.items() if k > 1]
print("dup nums:", dups, "| max num:", max(c))
print("has 505:", 505 in by, "| has 409:", 409 in by, "| has 141?:", len(bank) == 141)

# 展示 506 的 answer 头部，确认代码进入 answer
a = by[506]["answer"]
print("---- q506 answer head ----")
print(a[:200].replace("\n", "\\n"))
print("---- q506 answer tail ----")
print(a[-120:].replace("\n", "\\n"))
print("ALL OK" if ok and not dups else "PROBLEM")
