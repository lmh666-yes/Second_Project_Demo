# -*- coding: utf-8 -*-
"""把 506~515 十道题插入 03_编程画图大题.md 的「## 附：统计」之前，并更新统计数字。
保证：CRLF 行尾不变；除新增块与统计两处外，其余内容逐字节不变。"""
import sys
sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)
sys.path.insert(0, r"D:\github\面试刷题\工具\题库批处理")
from new_big_506_515 import QUESTIONS

MD = r"D:\github\面试刷题\面试题刷题软件-大题\题库\03_编程画图大题.md"

with open(MD, "rb") as f:
    raw = f.read()
text = raw.decode("utf-8")
assert "\r\n" in text

ANCHOR = "## 附：统计"
assert text.count(ANCHOR) == 1
pos = text.find(ANCHOR)

lines = []
lines.append("## 四、查漏补缺补充题（数据结构 / 排序 / 查找 10 题，编号 506~515）")
lines.append("")
for q in QUESTIONS:
    lines.append(f"**{q['num']}.** {q['title']}（查漏补缺补充题，2026-10-05）")
    lines.append("")
    lines.append(f"**题目解析：** {q['analysis']}")
    lines.append("**标准答案：** 完整代码如下：")
    lines.append("```c")
    lines.append(q["code"])
    lines.append("```")
    lines.append(f"要点：{q['yaodian']}")
    lines.append(f"**得分点：** {q['defen']}")
    lines.append("")
    lines.append("---")
    lines.append("")
block = ("\n".join(lines) + "\n").replace("\n", "\r\n")

new_text = text[:pos] + block + text[pos:]

old_stat = "- 合计：**131 题**"
assert new_text.count(old_stat) == 1
new_text = new_text.replace(old_stat, "- 合计：**141 题**")

old_line = "- 第四部分（附加面试题·面试题5/6/7）：**10 题** —— 编号 496~505"
assert new_text.count(old_line) == 1
new_line = old_line + "\r\n- 查漏补缺补充题：**10 题** —— 编号 506~515（数据结构 / 排序 / 查找，2026-10-05 新增）"
new_text = new_text.replace(old_line, new_line)

# ---- 校验 1：每题号只出现一次、顺序正确 ----
for n in range(506, 516):
    assert new_text.count(f"**{n}.**") == 1, f"tag {n}"
assert new_text.count("**505.**") == 1
assert new_text.count("- 合计：**141 题**") == 1
idx = [new_text.find(f"**{n}.**") for n in range(506, 516)]
assert idx == sorted(idx)

# ---- 校验 2：撤销插入块与统计两处后，应与原文逐字符一致 ----
d = new_text.replace(block, "", 1)
d = d.replace("- 合计：**141 题**", old_stat)
d = d.replace(new_line, old_line)
assert d == text, "unexpected changes outside the insert block!"

with open(MD, "wb") as f:
    f.write(new_text.encode("utf-8"))

print("OK: inserted 10 questions (506~515)")
print("lines:", new_text.count("\r\n") + 1)
print("bytes:", len(raw), "->", len(new_text.encode("utf-8")))
