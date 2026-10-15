# -*- coding: utf-8 -*-
"""从 new_big_506_515.py 生成 10 个临时 .c 文件到 tmp_506_515/ 并做数据自检。"""
import os, sys
sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)
sys.path.insert(0, r"D:\github\面试刷题\工具\题库批处理")
from new_big_506_515 import QUESTIONS

OUT = r"D:\github\面试刷题\工具\题库批处理\tmp_506_515"
os.makedirs(OUT, exist_ok=True)

nums = [q["num"] for q in QUESTIONS]
assert nums == list(range(506, 516)), nums

for q in QUESTIONS:
    for k in ("title", "analysis", "yaodian", "defen", "code"):
        assert "**" not in q[k], f"q{q['num']} {k} contains **"
        assert "\r" not in q[k], f"q{q['num']} {k} contains CR"
    path = os.path.join(OUT, f"q{q['num']}.c")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(q["code"].rstrip("\n") + "\n")
    print("gen", path)

print("total:", len(QUESTIONS), "questions OK")
