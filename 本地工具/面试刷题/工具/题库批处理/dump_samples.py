# -*- coding: utf-8 -*-
"""打印样例题的完整内容（客观 + 大题）"""
import json, sys
sys.stdout.reconfigure(encoding="utf-8")
obj = json.load(open(r"D:\github\面试刷题\面试题刷题软件-客观题\题库.json", encoding="utf-8"))
big = json.load(open(r"D:\github\面试刷题\面试题刷题软件-大题\题库.json", encoding="utf-8"))

def show(bank, nums, title, n=1200):
    print("=" * 40, title)
    for it in bank:
        if it["num"] in nums:
            print("-" * 60)
            print(f"#{it['num']} kind={it['kind']}")
            print("[题干]", it["stem"][:300].replace("\n", " / "))
            print("[答案]", it["answer"][:n])
            print("[解析]", it["explain"][:n])

# 客观：找多选题、判断题、存疑题各一个
objs = []
for it in obj:
    if it["kind"] == "multi" and len(objs) < 1:
        objs.append(it["num"])
    if it["kind"] == "judge" and len(objs) < 2:
        objs.append(it["num"])
objs += [112, 287]
show(obj, objs, "客观样本（多选/判断/存疑等）")

# 大题样本：无代码题的题号（用正则在 JSON 里找代码特征）
import re
codeish = re.compile(r"^\s*(#include|int |void |char |for\s*\(|while\s*\(|struct |printf|return )", re.M)
nocode = [it["num"] for it in big if not codeish.search(it.get("answer", ""))]
print("大题答案中没有明显代码特征的行首统计：无代码题数 =", len(nocode))
print("无代码题号:", nocode)
print()
withcode = [it["num"] for it in big if codeish.search(it.get("answer", ""))]
print("有代码特征题数:", len(withcode), "样本:", withcode[:30])
show(big, [6, 29, 409, 429, 449, 492, 460], "大题样本")
