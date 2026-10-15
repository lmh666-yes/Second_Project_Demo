# -*- coding: utf-8 -*-
"""抽查大题代码题完整性 + 主观题再抽样"""
import json, sys
sys.stdout.reconfigure(encoding="utf-8")
big = json.load(open(r"D:\github\面试刷题\面试题刷题软件-大题\题库.json", encoding="utf-8"))
subj = json.load(open(r"D:\github\面试刷题\面试题刷题软件-主观题\题库.json", encoding="utf-8"))

def show(bank, nums, n=700, tag=""):
    for it in bank:
        if it["num"] in nums:
            print("-" * 60)
            print(f"[{tag}#{it['num']}]", it["stem"][:120].replace("\n", " / "))
            print("──答案──")
            print(it["answer"][:n])
            print("──得分点──", it["explain"][:200])

show(big, [402, 407, 441, 442, 496, 500], tag="大题")
print()
show(subj, [88, 400, 466], n=500, tag="主观")
