# -*- coding: utf-8 -*-
"""主观版考试抽题覆盖验证（环形算法）：
- 150 场单场无重复
- 从零开始连续若干场（= ceil(题数/每场题数)+3）覆盖全部题目
- 出现次数均匀（差 ≤2）
"""
import sys, os, json
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
import 主观题软件 as B

import progress_guard as pg   # 保护用户进度（progress.json 不在 git 里）
HERE = os.path.dirname(os.path.abspath(__file__))
bank = json.load(open(os.path.join(HERE, "..", "题库.json"), encoding="utf-8"))
ids = [it["id"] for it in bank]
fails = 0
def check(c, m):
    global fails
    print(("  [PASS] " if c else "  [FAIL] ") + m)
    if not c:
        fails += 1

progress = {"_exam_plan": {}}
seen_all, dup = {}, 0
for t in range(150):
    picked = B.plan_take(ids, progress["_exam_plan"], "sub", "cur", B.EXAM_NUM)
    if len(picked) != len(set(picked)):
        dup += 1
    for x in picked:
        seen_all[x] = seen_all.get(x, 0) + 1
check(dup == 0, "150 场考试单场无重复")
check(len(seen_all) == len(ids), f"150 场覆盖全部 {len(ids)} 题")

progress2 = {"_exam_plan": {}}
seen, cover_at = set(), None
need = (len(ids) + B.EXAM_NUM - 1) // B.EXAM_NUM + 3   # 每场 EXAM_NUM 题，环形抽题的理论下限 + 3 轮富余
for t in range(1, need + 10):
    picked = B.plan_take(ids, progress2["_exam_plan"], "sub", "cur", B.EXAM_NUM)
    seen |= set(picked)
    if cover_at is None and len(seen) == len(ids):
        cover_at = t
check(cover_at is not None and cover_at <= need,
      f"从零开始连续 {cover_at} 场即覆盖全部题目（≤{need}）")
vals = sorted(seen_all.values())
check(vals[-1] - vals[0] <= 2, f"出现次数均匀：最少 {vals[0]} 最多 {vals[-1]}（差 ≤2）")

print()
print("结果：" + ("全部通过" if fails == 0 else f"{fails} 项失败"))
sys.exit(1 if fails else 0)
