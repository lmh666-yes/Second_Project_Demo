# -*- coding: utf-8 -*-
import sys, json, re
sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)
MD = r"D:\github\面试刷题\面试题刷题软件-主观题\题库\02_主观题.md"
lines = open(MD, encoding="utf-8").read().split("\n")

def block(num):
    in_code = False; start = None
    for i, s0 in enumerate(lines):
        s = s0.rstrip()
        if s.strip().startswith("```"): in_code = not in_code
        if in_code: continue
        if start is None:
            if re.match(rf"^\*\*{num}\.\*\*", s): start = i
        else:
            if s.strip() == "---" or s.startswith("#") or re.match(r"^\*\*\d+\.\*\*", s):
                return lines[start:i]
    return lines[start:] if start is not None else []

bad = []
for n in range(466, 492):
    txt = "\n".join(block(n))
    r = ("**题目解析：**" in txt, "**标准答案：**" in txt, "**得分点：**" in txt, "**参考答案：**" in txt)
    if not (r[0] and r[1] and r[2]) or r[3]:
        bad.append((n,) + r)
print("md 466-491 checked 26, bad:", bad)

bank = json.load(open(r"D:\github\面试刷题\面试题刷题软件-主观题\题库.json", encoding="utf-8"))
by = {it["num"]: it for it in bank}
bad2 = []
for n in range(466, 492):
    it = by.get(n)
    if not it: bad2.append((n, "missing")); continue
    if not it.get("analysis"): bad2.append((n, "no-analysis"))
    if not it.get("answer"): bad2.append((n, "no-answer"))
    if not it.get("explain"): bad2.append((n, "no-explain"))
print("json 466-491 checked 26, bad:", bad2)
print("473 has 1064:", "1064" in by[473]["answer"])
print("481 answer:", by[481]["answer"][:30])
print("484 has 7744:", "7744" in by[484]["answer"])
print("491 has bad_alloc:", "bad_alloc" in by[491]["answer"])
print("478 answer:", by[478]["answer"][:40])
print("466 analysis len:", len(by[466]["analysis"]), "| answer len:", len(by[466]["answer"]))
