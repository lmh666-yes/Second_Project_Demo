# -*- coding: utf-8 -*-
import sys, json
sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)
p = r"D:\github\面试刷题\面试题刷题软件-主观题\题库.json"
bank = json.load(open(p, encoding="utf-8"))
ids = [27, 28, 30, 31, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 52, 53, 54, 55, 56, 57, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72]
by = {it["num"]: it for it in bank}
bad = []
for n in ids:
    it = by.get(n)
    if not it:
        bad.append((n, "missing")); continue
    if not it.get("analysis"): bad.append((n, "no-analysis"))
    if not it.get("answer"): bad.append((n, "no-answer"))
    if not it.get("explain"): bad.append((n, "no-explain"))
print("checked:", len(ids), "bad:", bad)
exp = {27: "10、9、4、9", 30: "面试题7 第 17 题", 44: "合并说明", 55: "2023面试题-3 第 14 题"}
for n, kw in exp.items():
    print(n, "explain-keeps-merge-note:", kw in by[n]["explain"])
print("56-answer-starts:", by[56]["answer"][:14])
print("72-analysis-len:", len(by[72]["analysis"]))
