# -*- coding: utf-8 -*-
"""记忆模式测试：间隔重复调度（纯函数）+ 提示 / 自评 / 复习板块（UI 流程）。

用法：python test_memory.py
"""
import sys, io, os, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
import tkinter as tk
import progress_guard as pg
import 主观题软件 as ss

fails = 0
def check(cond, msg, extra=""):
    global fails
    print(("  [PASS] " if cond else "  [FAIL] ") + msg + (("  " + str(extra)) if extra else ""))
    if not cond:
        fails += 1

msgs = []
ss.messagebox.showinfo = lambda t, m: msgs.append(("info", t, m))
ss.messagebox.showerror = lambda t, m: msgs.append(("error", t, m))
ss.messagebox.showwarning = lambda t, m: msgs.append(("warn", t, m))
ss.messagebox.askyesno = lambda t, m: (msgs.append(("ask", t, m)), True)[1]

bp = ss.PROG_PATH
had = pg.backup(bp)

# ---------- 1. 纯函数：间隔重复调度 ----------
today = ss.today_ord()
r = {"box": 0, "due": 0, "reps": 0, "lapses": 0, "last": 0}
r1 = ss.schedule(r, "good", today)
check(r1["box"] == 1 and r1["due"] == today + ss.INTERVALS[1] and r1["reps"] == 1,
      "记住 → 盒 +1、到期 = 今天 + 该盒间隔", r1)
r2 = ss.schedule(r1, "good", today)
check(r2["box"] == 2 and r2["due"] == today + ss.INTERVALS[2], "继续记住 → 盒继续推进", r2)
r3 = ss.schedule(r2, "hard", today)
check(r3["box"] == 1, "模糊 → 盒退一级", r3)
r4 = ss.schedule(r3, "again", today)
check(r4["box"] == 0 and r4["lapses"] == 1 and r4["due"] == today + 1,
      "忘了 → 回盒 1、明天再来、遗忘 +1", r4)
r5 = ss.schedule({"box": len(ss.INTERVALS) - 1, "reps": 9}, "good", today)
check(r5["box"] == len(ss.INTERVALS) - 1, "盒级封顶不越界", r5)
check(ss.mem_rec({}, "x") == {"box": 0, "due": 0, "reps": 0, "lapses": 0, "last": 0},
      "未学过的题默认盒 0、未排期")
check(ss.mem_rec({"x": {"mem": {"box": "99", "due": "abc"}}}, "x")["box"] == len(ss.INTERVALS) - 1,
      "脏数据（越界盒级 / 非数字）被夹回")
check(ss.ord_text(0) == "-" and len(ss.ord_text(today)) == 10, "到期日可读文本", ss.ord_text(today))

bank = [{"id": "a"}, {"id": "b"}, {"id": "c"}]
prog = {"a": {"mem": {"box": 0, "due": today - 1, "reps": 1, "lapses": 0, "last": today}},
        "b": {"mem": {"box": 5, "due": today + 30, "reps": 3, "lapses": 0, "last": today - 5}},
        "c": {"mem": {"box": 2, "due": today + 5, "reps": 2, "lapses": 0, "last": today - 3}}}
check(ss.due_ids(bank, prog) == ["a"], "只取到期且已进记忆的题", ss.due_ids(bank, prog))
check(ss.due_ids(bank, prog, today + 31) == ["a", "c", "b"], "时间推移后到期题陆续加入（按到期日、盒级排序）",
      ss.due_ids(bank, prog, today + 31))
check(ss.due_ids(bank, {}) == [], "没有记忆记录时不排复习")
check(ss.mem_stats(bank, prog)[:3] == (1, 1, 2), "统计 待复习 / 已掌握 / 学习中",
      ss.mem_stats(bank, prog))
weak = ss.weak_ids([{"id": "n"}] + bank, prog, 2)
check(len(weak) == 2 and weak[0] == "n", "最不熟优先：没进记忆的排最前", weak)
check(ss.cue_text("①②③④⑤⑥⑦⑧⑨⑩") == "①②③④⑤⑥⑦⑧…", "得分点线索只留首字",
      ss.cue_text("①②③④⑤⑥⑦⑧⑨⑩"))
check(ss.cue_text("短") == "短" and ss.cue_text("") == "", "短行原样保留")

# ---------- 2. UI：提示 → 对答案 → 自评 → 复习板块 ----------
root = tk.Tk()
root.withdraw()
app = ss.App(root)
root.update()
it = app.queue[0]
qid = it["id"]

check(app.mode == "顺序" and app._stage == 0, "启动进入顺序板块、阶段 0")
check("🧠 记忆模式" in app.mem_label.cget("text") and "待复习" in app.mem_label.cget("text")
      and "已掌握" in app.mem_label.cget("text"),
      "记忆状态条显示复习计划", app.mem_label.cget("text"))
check(all("复习" in t for t in ("复习",)) and "复习" in app.mode_btns,
      "顶栏出现「复习」板块按钮", sorted(app.mode_btns))

# 空记忆状态：提示去顺序板块自评
saved_prog = app.progress
app.progress = {}
msgs.clear()
app.set_mode("复习")
check(app.mode == "顺序" and msgs and "还没有进入记忆的题目" in msgs[-1][2],
      "没有记忆记录时，复习板块给出引导提示且不切板块", msgs[-1][:2] if msgs else None)
app.progress = saved_prog

# 一级提示
app.show_hint()
root.update()
fb = app.fb.get("1.0", "end")
check("提示" in fb and "关键词线索" in fb and app._stage == 1,
      "「💡 提示」只给解析思路 + 得分点首字线索（阶段 1）")
check("标准答案" not in fb, "提示阶段不泄露标准答案")
check(app.progress.get(qid, {}).get("revealed") is not True, "看提示不算已看答案")

# 对答案 → 出现自评按钮
app.toggle_reveal()
root.update()
nav_txt = [w.cget("text") for w in app.nav.winfo_children()]
check(app._stage == 2, "看答案后阶段 2")
check(any("😖" in t for t in nav_txt) and any("😎" in t for t in nav_txt),
      "看答案后出现自评按钮", nav_txt)
check("收起" in app.reveal_btn.cget("text"), "按钮可收起答案")

# 自评：记住
app.rate("good")
root.update()
rec = app.progress.get(qid, {}).get("mem")
check(isinstance(rec, dict) and rec["box"] == 1 and rec["due"] == today + ss.INTERVALS[1],
      "自评「记住」写入记忆计划（盒 1）", rec)
check("😎 记住" in app.head_label.cget("text"), "题头反馈自评结果", app.head_label.cget("text"))
check(app._stage == 0, "自评后自动进入下一题（阶段归零）")

# 快捷键
check(bool(root.bind("<Key-1>")) and bool(root.bind("<Key-2>")) and bool(root.bind("<Key-3>"))
      and bool(root.bind("<Key-h>")), "自评 1/2/3 与提示 h 快捷键已绑定")

# 到期题进入复习板块
app.progress.setdefault(qid, {})["mem"] = {"box": 0, "due": today - 1, "reps": 1,
                                           "lapses": 0, "last": today - 2}
app.set_mode("复习")
root.update()
check(app.mode == "复习" and app.queue and app.queue[0]["id"] == qid,
      "到期题出现在复习板块队首", (app.mode, len(app.queue)))
check("🔁 复习" in app.head_label.cget("text") and "盒" in app.head_label.cget("text"),
      "复习题头显示盒级", app.head_label.cget("text"))
check("复习" not in [w.cget("text") for w in app.nav.winfo_children()],
      "复习板块不显示收藏按钮", [w.cget("text") for w in app.nav.winfo_children()])

# 复习里「忘了」→ 排到本次队尾再来一遍
app.toggle_reveal()
root.update()
before = len(app.queue)
app.rate("again")
root.update()
ids = [x["id"] for x in app.queue]
check(len(app.queue) == before + 1 and ids.count(qid) >= 2,
      "复习中「忘了」→ 本题排到本次队尾重来", (before, len(app.queue), ids.count(qid)))
check(app.progress[qid]["mem"]["box"] == 0, "「忘了」把记忆盒打回 0")

# 收起 → 回到阶段 0（先看一次答案，再收起）
app.toggle_reveal()
root.update()
check(app._stage == 2 and app.revealed is True, "重排后的题仍可看答案（阶段 2）")
app.toggle_reveal()
root.update()
check(app._stage == 0 and app.revealed is False, "收起答案回到阶段 0")

app._save_progress()
pg.restore()
if not had and os.path.exists(ss.PROG_PATH):
    os.remove(ss.PROG_PATH)
print("\n结论：" + ("全部通过" if fails == 0 else f"{fails} 项未通过"))
sys.exit(1 if fails else 0)
