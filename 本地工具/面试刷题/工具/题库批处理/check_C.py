# -*- coding: utf-8 -*-
import json, io, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
P = r"D:\github\面试刷题\面试题刷题软件-主观题\题库.json"
bank = json.load(open(P, encoding="utf-8"))
nums = [114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 126, 127, 128, 129, 130,
        133, 134, 135, 136, 138, 140, 141, 142, 143, 144, 145, 146, 148, 149, 150,
        151, 152, 153, 154, 155, 156, 157, 158, 159, 160]
by = {q.get("num"): q for q in bank}
problems = []
for n in nums:
    q = by.get(n)
    if not q:
        problems.append((n, "no-q"))
        continue
    a = (q.get("analysis") or "").strip()
    s = (q.get("answer") or "").strip()
    e = (q.get("explain") or "").strip()
    bad = []
    if not a:
        bad.append("analysis")
    if not s:
        bad.append("answer")
    if not e:
        bad.append("explain")
    if "参考答案" in a or "参考答案" in s:
        bad.append("old-label")
    if bad:
        problems.append((n, bad))
print("checked", len(nums), "problems", problems)
print("141 bs0:", "\\0" in by[141]["answer"], "142 bs0:", "\\0" in by[142]["answer"])
print("sample120 answer:", by[120]["answer"][:30])
print("sample160 analysis len:", len(by[160]["analysis"]), "answer len:", len(by[160]["answer"]))
