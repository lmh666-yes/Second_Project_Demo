# -*- coding: utf-8 -*-
"""批次F收尾抽查：md 区块标记 + JSON 字段非空。"""
import io, json, re, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

NUMS = [254, 255, 256, 257, 258, 259, 260, 261, 262, 263,
        265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
        275, 276, 277, 278, 279, 280, 281, 282, 283, 284,
        285, 286, 287, 288, 289, 290, 291, 292, 293, 294]

md = io.open(r"D:\github\面试刷题\面试题刷题软件-主观题\题库\02_主观题.md", encoding="utf-8").read()
lines = md.split("\n")

def block_of(num):
    in_code = False
    start = None
    for i, ln in enumerate(lines):
        s = ln.rstrip()
        if s.strip().startswith("```"):
            in_code = not in_code
        if in_code:
            continue
        if start is None:
            if re.match(r"^\*\*" + str(num) + r"\.\*\*", s):
                start = i
        else:
            if s.strip() == "---" or s.startswith("#") or re.match(r"^\*\*\d+\.\*\*", s):
                return lines[start:i]
    return lines[start:] if start is not None else []

fails = []
for n in NUMS:
    blk = "\n".join(block_of(n))
    if "**参考答案：**" in blk or "**参考译文：**" in blk:
        fails.append((n, "残留参考答案"))
    if "**题目解析：**" not in blk:
        fails.append((n, "缺题目解析"))
    if "**标准答案：**" not in blk:
        fails.append((n, "缺标准答案"))
    if "**得分点：**" not in blk:
        fails.append((n, "缺得分点"))
print("md 区块检查:", "OK，40 题标记齐全" if not fails else fails)

data = json.load(io.open(r"D:\github\面试刷题\面试题刷题软件-主观题\题库.json", encoding="utf-8"))
if isinstance(data, dict):
    items = data.get("items") or data.get("questions") or []
    if not items:
        items = [v for v in data.values() if isinstance(v, dict)]
else:
    items = data

found = {}
for it in items:
    if not isinstance(it, dict):
        continue
    num = it.get("num")
    if num is None:
        m = re.search(r"(\d+)$", str(it.get("id", "")))
        if m:
            num = int(m.group(1))
    if num in NUMS:
        found[num] = it
missing = [n for n in NUMS if n not in found]
empty = [(n, k) for n in NUMS for k in ("analysis", "answer")
         if not str(found.get(n, {}).get(k, "")).strip()]
print("JSON 检查:", "OK，40 题 analysis/answer 均非空" if not missing and not empty
      else ("缺失: " + str(missing) + " 空字段: " + str(empty)))
s = found.get(254, {})
print("样例254 解析前30字:", str(s.get("analysis", ""))[:30])
print("样例254 答案前30字:", str(s.get("answer", ""))[:30])
