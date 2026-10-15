# -*- coding: utf-8 -*-
"""抽查 295~314 题的 answer/explain 是否非空、选项数、题型"""
import json

bank = json.load(open(r"D:\github\面试刷题\面试题刷题软件-客观题\题库.json", encoding="utf-8"))
by_id = {it["id"]: it for it in bank}
out, bad = [], []
for n in range(295, 315):
    it = by_id.get(f"choice-{n}")
    if not it:
        bad.append(f"#{n} MISSING")
        continue
    ans = (it.get("answer") or "").strip()
    exp = (it.get("explain") or "").strip()
    out.append(f"#{n} kind={it['kind']} ans={ans!r} opts={len(it['options'])} "
               f"stem_len={len(it['stem'])} exp_len={len(exp)}")
    if not ans or not exp or len(it["options"]) != 4:
        bad.append(f"#{n} PROBLEM ans='{ans}' exp_len={len(exp)} opts={len(it['options'])}")
out.append("BAD = " + repr(bad))
out.append(f"total={len(bank)} max_num={max(it['num'] for it in bank)}")
open(r"D:\github\面试刷题\工具\题库批处理\_verify295_out.txt", "w", encoding="utf-8").write("\n".join(out))
print("verify done, bad:", len(bad))
