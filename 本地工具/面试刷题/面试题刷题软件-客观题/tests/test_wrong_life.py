# -*- coding: utf-8 -*-
"""错题寿命机制 + 错题考试 专项测试（客观题桌面版）

规则（2.7.0）：
  · 累计错误次数永久记录；答错即入错题集（寿命初始 1）
  · 错误次数达到 3/5/7/9 或超过 10（>10 后每错一次）→ 寿命 +1
  · 只有「错题考试」答对才寿命 -1；归零后移出错题集
  · 顺序/普通考试答对不影响错题状态
用法：python tests/test_wrong_life.py
"""
import sys
import io
import os

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
import tkinter as tk
import progress_guard as pg
import importlib

bs = importlib.import_module("刷题软件")

fails = 0


def check(cond, msg, extra=""):
    global fails
    line = ("  [PASS] " if cond else "  [FAIL] ") + msg
    if extra:
        line += " " + str(extra)
    print(line)
    if not cond:
        fails += 1


bs.messagebox.showinfo = lambda t, m: None
bs.messagebox.showwarning = lambda t, m: None
bs.messagebox.askyesno = lambda t, m: True

# ---- 0. 旧数据迁移（纯函数）----
prog, ch = bs.migrate_progress({"q1": {"ok": False, "wrong_count": 2},
                                "q2": {"ok": True, "wrong_count": 5}})
check(prog["q1"]["in_wrong"] is True and prog["q1"]["life"] == 1, "迁移：旧错题补 in_wrong/寿命1")
check(prog["q2"]["in_wrong"] is False and prog["q2"]["life"] == 0, "迁移：旧答对题不入错题集")

bp = bs.PROG_PATH
had = pg.backup(bp)

root = tk.Tk()
root.withdraw()
app = bs.App(root)
root.update()

# ---- 1. 阈值规则：连错 12 次 ----
qid = app.bank[0]["id"]
app.progress[qid] = {"ok": None, "wrong_count": 0, "in_wrong": False, "life": 0, "notes": ""}
lifes = []
for i in range(12):
    app.record(qid, False, "seq")
    lifes.append(app.progress[qid]["life"])
check(lifes == [1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7], f"寿命轨迹（{lifes}）")

# ---- 2. 顺序/普通考试答对不清除 ----
app.record(qid, True, "seq")
app.record(qid, True, "exam")
check(app.progress[qid]["in_wrong"] is True and app.progress[qid]["life"] == 7,
      "顺序+考试答对：错题保留、寿命不变")

# ---- 3. 错题考试答对：寿命 -1 直至归零移除 ----
for _ in range(6):
    app.record(qid, True, "wexam")
check(app.progress[qid]["in_wrong"] is True and app.progress[qid]["life"] == 1,
      "6 次错题考试答对：寿命 1、仍在错题集")
app.record(qid, True, "wexam")
check(app.progress[qid]["in_wrong"] is False and app.progress[qid]["life"] == 0,
      "第 7 次答对：归零移除")

# ---- 4. 清除后再错：重新入集，>10 规则继续生效 ----
app.record(qid, False, "seq")
rec = app.progress[qid]
check(rec["in_wrong"] is True and rec["wrong_count"] == 13 and rec["life"] == 2,
      f"清除后再错：wc=13 重新入集寿命 2（{rec['wrong_count']}/{rec['life']}）")

# ---- 5. 错题考试流程 ----
ids = [it["id"] for it in app.bank[1:4]]
for i in ids:
    app.progress[i] = {"ok": None, "wrong_count": 0, "in_wrong": False, "life": 0, "notes": ""}
    app.record(i, False, "seq")
app.set_mode("考试")
app._exam_start(kind="wrong")
check(app.exam.get("kind") == "wrong" and len(app.exam["ids"]) == 4,
      f"错题考试抽题（{len(app.exam['ids'])} 题 ≤10，含此前清除后又错的题）")
q0 = app.queue[0]
life0 = app.progress[q0["id"]]["life"]
app._exam_answer(q0, app.exam["opts"][q0["id"]]["answer"])
r0 = app.progress[q0["id"]]
check(r0["life"] == life0 - 1, f"错题考试答对：寿命 {life0} → {r0['life']}")
q1 = app.queue[1]
oinfo1 = app.exam["opts"].get(q1["id"]) or {}
life1 = app.progress[q1["id"]]["life"]
if oinfo1.get("opts"):
    wsel = next(o["key"] for o in oinfo1["opts"] if o["key"] != oinfo1["answer"])
else:
    a = str(q1.get("answer", "")).strip()
    wsel = "×" if a in ("对", "T", "TRUE", "√", "✅ 对") else "√"
app._exam_answer(q1, wsel)
check(app.progress[q1["id"]]["life"] == life1 and app.progress[q1["id"]]["in_wrong"] is True,
      "错题考试答错：寿命不变、留在错题集")
app.finish_exam()
root.update()
check("错题考试" in app.head_label.cget("text"), "成绩页标注「错题考试」")

# ---- 6. 普通考试不受影响 ----
app._exam_start(kind="normal")
check(len(app.exam["ids"]) == bs.EXAM_NUM, f"普通考试仍为 {bs.EXAM_NUM} 题")
app.finish_exam()
root.update()

root.destroy()
pg.restore()
if not had and os.path.exists(bp):
    os.remove(bp)

print()
print(f"结果：{'全部通过 ✔' if fails == 0 else f'{fails} 项失败 ✘'}")
sys.exit(1 if fails else 0)
