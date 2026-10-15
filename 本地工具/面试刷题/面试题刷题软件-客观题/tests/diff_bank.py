# -*- coding: utf-8 -*-
"""对比新旧题库差异"""
import sys, io, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

old = json.load(open(r"D:\github\C刷题软件\tests\_old_bank_backup.json", encoding="utf-8"))
new = json.load(open(r"D:\github\C刷题软件\题库.json", encoding="utf-8"))
print("旧:", len(old), "题  新:", len(new), "题")
d = dict((x["id"], x) for x in old)
d2 = dict((x["id"], x) for x in new)

def keyf(qid):
    k, n = qid.split("-")
    return (k, int(n))

diff = []
for qid in sorted(set(d) & set(d2), key=keyf):
    a, b = d[qid], d2[qid]
    if a["stem"] != b["stem"] or a.get("options") != b.get("options") or str(a["answer"]) != str(b["answer"]):
        diff.append(qid)
only_new = sorted(set(d2) - set(d), key=keyf)
only_old = sorted(set(d) - set(d2), key=keyf)
print("内容有差异的题数:", len(diff))
for q in diff:
    a, b = d[q], d2[q]
    print(f"  {q}:")
    print(f"    旧题干: {a['stem'][:60]!r}")
    print(f"    新题干: {b['stem'][:60]!r}")
    if str(a["answer"]) != str(b["answer"]):
        print(f"    旧答案={a['answer']}  新答案={b['answer']}")
    if a.get("options") != b.get("options"):
        print(f"    旧选项: {[o['text'][:18] for o in a.get('options', [])]}")
        print(f"    新选项: {[o['text'][:18] for o in b.get('options', [])]}")
print("仅新增题:", only_new, "  仅旧有题:", only_old)
