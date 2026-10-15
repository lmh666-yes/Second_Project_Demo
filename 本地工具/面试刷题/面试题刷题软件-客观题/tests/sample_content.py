# -*- coding: utf-8 -*-
"""抽样输出题库内容供人工核对（题目/选项/答案/解析一致性 + 配图题）"""
import json, os, random

HERE = os.path.dirname(os.path.abspath(__file__))
bank = json.load(open(os.path.join(HERE, "..", "题库.json"), encoding="utf-8"))
out = open(os.path.join(HERE, "content_sample.txt"), "w", encoding="utf-8")

random.seed(7)
sample = bank[:8] + random.sample(bank, 28)
for it in sample:
    out.write(f"### [{it.get('id')}] {it.get('kind_name')} 第{it.get('num')}题\n")
    out.write("题干: " + str(it.get("stem", ""))[:260] + "\n")
    for o in (it.get("options") or []):
        out.write(f"   {o.get('key')}. {str(o.get('text'))[:90]}\n")
    out.write("答案: " + str(it.get("answer")) + "\n")
    out.write("解析: " + str(it.get("explain", ""))[:260] + "\n\n")

out.write("\n===== 配图题 =====\n")
for it in bank:
    if it.get("imgs"):
        out.write(f"[{it.get('id')}] 第{it.get('num')}题 图: {it.get('imgs')}\n")
        out.write("题干: " + str(it.get("stem", ""))[:260] + "\n")
        out.write("答案: " + str(it.get("answer")) + "\n\n")
out.close()
print("done")
