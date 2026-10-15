# -*- coding: utf-8 -*-
"""热点标记：在指定题目的 **得分点：** 行前插入 **热点：** 是（幂等：已标记则跳过）"""
import re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, r"D:\github\面试刷题\工具\题库批处理")
from migrate_lib import find_block_range


def mark(path, nums):
    lines = open(path, encoding="utf-8").read().split("\n")
    ok = 0
    for num in nums:
        start, end = find_block_range(lines, num)
        if start is None:
            print("未找到", num); continue
        blk = lines[start:end]
        if any(re.match(r"^\*\*热点：\*\*", l) for l in blk):
            continue                       # 已标记
        j = next((i for i in range(len(blk)) if re.match(r"^\*\*得分点：\*\*", blk[i])), None)
        if j is None:
            print("缺得分点", num); continue
        blk.insert(j, "**热点：** 是")
        lines[start:end] = blk
        ok += 1
    open(path, "w", encoding="utf-8").write("\n".join(lines))
    print(f"已标记 {ok} 题 →", path)


if __name__ == "__main__":
    import importlib.util
    spec = importlib.util.spec_from_file_location("d", sys.argv[1])
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    mark(sys.argv[2], m.NUMS)
