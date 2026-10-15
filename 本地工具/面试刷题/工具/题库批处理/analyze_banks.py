# -*- coding: utf-8 -*-
"""分析客观题库与大题题库：解析结构 / 代码完整性 / 排序查找覆盖"""
import json, re, sys
sys.stdout.reconfigure(encoding="utf-8")

OK = r"D:\github\面试刷题\面试题刷题软件-客观题\题库.json"
BIG = r"D:\github\面试刷题\面试题刷题软件-大题\题库.json"

obj = json.load(open(OK, encoding="utf-8"))
big = json.load(open(BIG, encoding="utf-8"))

print("=" * 30, "客观题", len(obj), "题")
# 结构标记统计
pat = {
    "考点定位": 0, "正确项依据": 0, "干扰项分析": 0, "易错提醒/拓展": 0,
    "四段全": 0, "无标记": 0,
}
samples_no = []
for it in obj:
    ex = it.get("explain", "")
    hits = {k: (k in ex) for k in ("考点定位", "正确项依据", "干扰项分析", "易错提醒")}
    for k in ("考点定位", "正确项依据", "干扰项分析"):
        if hits[k]:
            pat[k] += 1
    if "易错提醒" in ex:
        pat["易错提醒/拓展"] += 1
    if hits["考点定位"] and hits["正确项依据"] and hits["干扰项分析"]:
        pat["四段全"] += 1
    else:
        pat["无标记"] += 1
        if len(samples_no) < 8:
            samples_no.append(it["num"])
print(pat)
print("无四段标记的题号样本:", samples_no)
print("answer 样例:", [(it["num"], it["answer"][:20]) for it in obj[:5]])
print("answer 长的样例(>15字):", [(it["num"], it["answer"][:40]) for it in obj if len(it.get("answer", "")) > 15][:8])
lens = sorted(len(it.get("explain", "")) for it in obj)
print("explain 长度: min", lens[0], "中位", lens[len(lens)//2], "max", lens[-1])
print()
print("--- 样例 1 (第一题完整 explain) ---")
print(obj[0]["num"], obj[0]["answer"], "\n", obj[0]["explain"][:600])
print()
print("--- 样例 2 (一个四段完整例) ---")
for it in obj:
    ex = it["explain"]
    if all(k in ex for k in ("考点定位", "正确项依据", "干扰项分析", "易错")):
        print(it["num"], it["answer"], "\n", ex[:800])
        break

print()
print("=" * 30, "大题", len(big), "题")
code_stem = sum(1 for it in big if "```" in it.get("stem", ""))
code_ans = sum(1 for it in big if "```" in it.get("answer", ""))
code_both = sum(1 for it in big if "```" in it.get("stem", "") and "```" in it.get("answer", ""))
print("题干含代码块:", code_stem, "| 答案含代码块:", code_ans, "| 两者都有:", code_both)
lens_a = sorted(len(it.get("answer", "")) for it in big)
print("答案长度: min", lens_a[0], "中位", lens_a[len(lens_a)//2], "max", lens_a[-1])

# 编程题判定：题干含 写/实现/设计/编写/函数/程序/代码
verbs = re.compile(r"写|实现|设计|编写|补全|函数|程序|代码|宏|Makefile")
prog = [it for it in big if verbs.search(it.get("stem", ""))]
print("疑似编程/设计题:", len(prog), "题")
print("其中答案无代码块的题号:", [it["num"] for it in prog if "```" not in it.get("answer", "")][:60])

# 排序/查找专题
topics = {
    "冒泡": r"冒泡", "插入排序": r"插入排序", "选择排序": r"选择排序", "快速排序": r"快速排序|快排",
    "希尔": r"希尔", "归并": r"归并", "堆排序": r"堆排序", "二分查找": r"二分查找|折半查找|二分法",
    "哈希查找": r"哈希|散列", "strstr/子串查找": r"strstr|子串|查找字符串",
    "qsort": r"qsort", "排序(泛)": r"排序", "查找(泛)": r"查找",
}
for name, rx in topics.items():
    r = re.compile(rx)
    hits = [it["num"] for it in big if r.search(it.get("stem", "")) or r.search(it.get("answer", ""))]
    print(f"{name}: {len(hits)} 题 -> {hits[:14]}")
