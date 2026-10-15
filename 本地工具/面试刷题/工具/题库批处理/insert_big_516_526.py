# -*- coding: utf-8 -*-
"""把 516~526 追加进两份 03_编程画图大题.md（大题软件 + 主观题软件的副本），并更新统计"""
import sys, io, os
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, r"D:\github\面试刷题\工具\题库批处理")
from new_big_516_526 import NEW

FILES = [
    r"D:\github\面试刷题\面试题刷题软件-大题\题库\03_编程画图大题.md",
    r"D:\github\面试刷题\面试题刷题软件-主观题\题库\03_编程画图大题.md",
]
SECTION_TITLE = "## 五、排序 / 查找 / 数据结构函数专项（11 题，编号 516~526）"
STAT_NEW = "- 排序 / 查找 / 数据结构函数专项：**11 题** —— 编号 516~526（C 排序/查找与数据结构函数写法，2026-10-05 新增）"

def build_section(nl):
    parts = [SECTION_TITLE, ""]
    for num in sorted(NEW):
        q = NEW[num]
        parts.append(f"**{num}.** {q['stem']}")
        parts.append("")
        parts.append(f"**题目解析：** {q['ana']}")
        parts.append(f"**标准答案：** {q['ans']}")
        if q["hot"]:
            parts.append("**热点：** 是")
        parts.append(f"**得分点：** {q['exp']}")
        parts.append("")
        parts.append("---")
        parts.append("")
    return nl.join(parts)

for path in FILES:
    raw = open(path, "rb").read().decode("utf-8")
    nl = "\r\n" if "\r\n" in raw[:4000] else "\n"
    if "**516.**" in raw:
        print("!! 已存在 516，跳过:", path)
        continue
    assert raw.count("## 附：统计") == 1, ("统计节不唯一", path)
    assert raw.count("- 合计：**141 题**") == 1, ("合计行不匹配", path)
    old_bullet = "- 查漏补缺补充题：**10 题** —— 编号 506~515（数据结构 / 排序 / 查找，2026-10-05 新增）"
    assert raw.count(old_bullet) == 1, ("506~515 统计行不匹配", path)

    section = build_section(nl)
    raw = raw.replace("## 附：统计", section + nl + "## 附：统计", 1)
    raw = raw.replace(old_bullet, old_bullet + nl + STAT_NEW, 1)
    raw = raw.replace("- 合计：**141 题**", "- 合计：**152 题**", 1)

    open(path, "wb").write(raw.encode("utf-8"))
    print("OK 已插入:", path, "| 章节长度", len(section), "| 换行", repr(nl))

# 两文件一致性
a = open(FILES[0], "rb").read()
b = open(FILES[1], "rb").read()
print("两文件一致:", a == b)
