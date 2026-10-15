# -*- coding: utf-8 -*-
"""应用 附加面试题2.0·面试题3 批次：
1) 02_主观题.md：插入 #492~496（新小节）+ 统计 449→454
2) 03_编程画图大题.md：插入 #527~530（新小节 六、）+ 统计 152→156
3) 对既有题追加互标/合并说明（NOTES）
"""
import sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, r"D:\github\面试刷题\工具\题库批处理")
from new_2026_2p0_q3 import SUBJ, BIGQ, NOTES

SUBJ_FILE = r"D:\github\面试刷题\面试题刷题软件-主观题\题库\02_主观题.md"
BIGQ_FILE = r"D:\github\面试刷题\面试题刷题软件-大题\题库\03_编程画图大题.md"

def block(nl, num, q):
    parts = [f"**{num}.** {q['stem']}", "",
             f"**题目解析：** {q['ana']}",
             f"**标准答案：** {q['ans']}"]
    if q["hot"]:
        parts.append("**热点：** 是")
    parts += [f"**得分点：** {q['exp']}",
              f"**来源：** {q['src']}", "", "---", ""]
    return nl.join(parts)

def build_section(nl, title, items):
    out = [title, ""]
    for num in sorted(items):
        out.append(block(nl, num, items[num]))
    return nl.join(out)

def norm(s, nl):
    return s.replace("\r\n", "\n").replace("\n", nl)

# ---------- 1) 主观 ----------
raw = open(SUBJ_FILE, "rb").read().decode("utf-8")
nl = "\r\n" if "\r\n" in raw[:4000] else "\n"
assert "**492.**" not in raw, "492 已存在"
section = norm(build_section("\n", "### 附加面试题2.0 · 面试题3（基础篇）", SUBJ), nl)
assert raw.count("## 附：统计") == 1
raw = raw.replace("## 附：统计", section + nl + "## 附：统计", 1)
old = "- 总题量：**449 题**（与文件 1/3 重复的题目已合并，仅保留一处并标注来源）"
assert raw.count(old) == 1
raw = raw.replace(old, old + nl + "- 第 492~496 题为附加面试题2.0·面试题3（基础篇：变量存储区域 / 条件表达式 / 变量定义简版 / 固件异常调试 / 提高稳定性，2026-10-12 新增）。", 1)
open(SUBJ_FILE, "wb").write(raw.encode("utf-8"))
print("主观 OK：+5 题，统计 449→454")

# ---------- 2) 大题 ----------
raw = open(BIGQ_FILE, "rb").read().decode("utf-8")
nl = "\r\n" if "\r\n" in raw[:4000] else "\n"
assert "**527.**" not in raw, "527 已存在"
section = norm(build_section("\n", "## 六、附加面试题2.0 · 面试题3（编程篇 4 题，编号 527~530）", BIGQ), nl)
assert raw.count("## 附：统计") == 1
raw = raw.replace("## 附：统计", section + nl + "## 附：统计", 1)
ack = "- 排序 / 查找 / 数据结构函数专项：**11 题** —— 编号 516~526（C 排序/查找与数据结构函数写法，2026-10-05 新增）"
assert raw.count(ack) == 1
raw = raw.replace(ack, ack + nl + "- 附加面试题2.0 · 面试题3（编程篇）：**4 题** —— 编号 527~530（100~400 筛选 / 篮球反弹 / 字节数组转十六进制 / 学生成绩统计，2026-10-12 新增）", 1)
assert raw.count("- 合计：**152 题**") == 1
raw = raw.replace("- 合计：**152 题**", "- 合计：**156 题**", 1)
open(BIGQ_FILE, "wb").write(raw.encode("utf-8"))
print("大题 OK：+4 题，统计 152→156")

# ---------- 3) 互标/合并说明 ----------
for note in NOTES:
    raw = open(note["file"], "rb").read().decode("utf-8")
    nl = "\r\n" if "\r\n" in raw[:4000] else "\n"
    anchor = note["anchor"].replace("\r\n", "\n").replace("\n", nl)
    cnt = raw.count(anchor)
    assert cnt == 1, (note["file"], "锚点不唯一：", cnt, anchor[:60])
    raw = raw.replace(anchor, anchor + norm(note["add"], nl), 1)
    open(note["file"], "wb").write(raw.encode("utf-8"))
    print("NOTE ok:", note["file"].split("\\")[-1], "->", note["add"][:28], "...")
print("全部完成")
