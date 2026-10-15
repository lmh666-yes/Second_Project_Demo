# -*- coding: utf-8 -*-
"""打印主观题样例题 + 两个题库字段长度统计"""
import json, sys
sys.stdout.reconfigure(encoding="utf-8")
subj = json.load(open(r"D:\github\面试刷题\面试题刷题软件-主观题\题库.json", encoding="utf-8"))
big = json.load(open(r"D:\github\面试刷题\面试题刷题软件-大题\题库.json", encoding="utf-8"))

def show(bank, nums, n=900):
    for it in bank:
        if it["num"] in nums:
            print("-" * 60)
            print(f"#{it['num']}")
            print("[题干]", it["stem"][:200].replace("\n", " / "))
            print("[答案]", it["answer"][:n])
            print("[得分点]", it["explain"][:400])

print("=" * 30, "主观题样本")
show(subj, [1, 78, 274, 353, 491])

print()
for name, bank in (("主观题", subj), ("大题", big)):
    la = [len(it["answer"]) for it in bank]
    le = [len(it["explain"]) for it in bank]
    print(f"{name}: 答案长度 中位 {sorted(la)[len(la)//2]} 均值 {sum(la)//len(la)} | "
          f"得分点长度 中位 {sorted(le)[len(le)//2]} 均值 {sum(le)//len(le)}")
