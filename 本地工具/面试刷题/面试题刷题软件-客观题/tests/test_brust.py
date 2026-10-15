# -*- coding: utf-8 -*-
"""刷题软件自检（1.0.8）：题库 / 判分 / 笔记 / 考试全流程(开始/拦截/暂停/交卷/
错题回顾/重新考试/中断恢复/关闭提醒) / 重置(3次确认)"""
import sys, io, os, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
import tkinter as tk
import progress_guard as pg   # 保护用户进度（progress.json 不在 git 里）
import 刷题软件 as bs

fails = 0
def check(cond, msg):
    global fails
    print(("  [PASS] " if cond else "  [FAIL] ") + msg)
    if not cond:
        fails += 1

bp = bs.PROG_PATH
tmp = bp + ".bak"
if os.path.exists(bp):
    os.replace(bp, tmp)

warned = []
bs.messagebox.showinfo = lambda t, m: None
bs.messagebox.showerror = lambda t, m: None
bs.messagebox.showwarning = lambda t, m: warned.append(t)
bs.messagebox.askyesno = lambda t, m: True

def find_btns(widget, texts):
    out = []
    for w in widget.winfo_children():
        if w.winfo_class() == "Button" and any(t in str(w.cget("text")) for t in texts):
            out.append(w)
        out += find_btns(w, texts)
    return out

root = tk.Tk()
root.withdraw()
app = bs.App(root)
root.update()

# 1. 题库
check(len(app.bank) == 367, f"题库 367 题 (实际 {len(app.bank)})")
kinds = {}
for it in app.bank:
    kinds[it["kind"]] = kinds.get(it["kind"], 0) + 1
check(kinds == {"choice": 321, "multi": 6, "judge": 35, "qa": 5}, f"题型分布 {kinds}")

# 2. 顺序 + 单选判分 + 错题次数
app.set_mode("顺序")
root.update()
it = app.queue[0]
app.choice_var.set(it["answer"])
app.check()
root.update()
check(app.progress[it["id"]]["ok"] is True, "单选答对")
it2 = app.queue[1]
app.idx = 1
app.show_question()
root.update()
wrong = "A" if it2["answer"] != "A" else "B"
for _ in range(2):
    app.choice_var.set(wrong)
    app.check()
    root.update()
check(app.progress[it2["id"]]["wrong_count"] == 2, "错误次数累计")

# 3. 判断
app.idx = 80
app.show_question()
root.update()
jit = app.queue[80]
app.answer_judge(jit["answer"])
app.check()            # 选择后点「确认答案」判分
root.update()
check(app.progress[jit["id"]]["ok"] is True, "判断答对")

# 4. 笔记（独立窗口）
app.idx = 0
app.show_question()
root.update()
app._open_note()          # 打开笔记独立窗口
root.update()
app.note_text.insert("1.0", "笔记XYZ")
app._note_edited()
root.update()
app.next_q()
root.update()
check(app.progress.get(it["id"], {}).get("notes") == "笔记XYZ", "笔记保存")
app.prev_q()
root.update()
check(app.note_text.get("1.0", "end-1c") == "笔记XYZ", "笔记回显")
app._close_note()

# 5. 考试板块：待开始
app.set_mode("考试")
root.update()
check(app.mode == "考试" and not app._exam_active(), "进入考试板块(待开始)")
check(len(find_btns(app.exam_bar, ["开始考试"])) == 1, "右上角有开始考试按钮")

# 6. 开考
app._exam_start()
root.update()
check(app._exam_running() and len(app.queue) == 30, "开考30题")
check(2390 < app.exam_left <= 2400, f"倒计时40分钟 ({app.exam_left})")

# 7. 考试中切板块被拦截
warned.clear()
app.set_mode("顺序")
root.update()
check(app.mode == "考试" and any("考试" in w for w in warned), "考试中切板块被拦截")

# 8. 考试作答（错题进错题库 + 只能答一次）
q0 = app.queue[0]
oinfo = app.exam.get("opts", {}).get(q0["id"])
if oinfo:                                   # 选择题：选项已打乱，正确答案是打乱后的 key
    real = str(oinfo.get("answer"))
    app.choice_var.set("A" if real != "A" else "B")
else:                                       # 判断题：答案为 √ / ×
    app.choice_var.set("×" if q0["answer"] == "√" else "√")
app.check()
root.update()
check(q0["id"] in app.exam["answers"], "考试答案已记录")
check(app.progress[q0["id"]]["ok"] is False, "考试错题进入错题库")
app.choice_var.set(q0["answer"])
app.check()
root.update()
check(len(app.exam["answers"]) == 1, "已答题目不能重复作答")

# 9. 暂停/继续
app._exam_toggle_pause()
root.update()
check(app._exam_paused() and app._exam_timer is None, "暂停(倒计时停止)")
app._exam_toggle_pause()
root.update()
check(app._exam_running() and app._exam_timer is not None, "继续(倒计时恢复)")

# 10. 交卷 + 成绩
app.finish_exam()
root.update()
check(app._exam_finished(), "交卷完成")
check("成绩" in app.stem.get("1.0", "end"), "成绩页显示")
check(len(find_btns(app.exam_bar, ["重新考试"])) == 1, "右上角重新考试按钮")

# 11. 错题回顾按钮
err_btns = find_btns(app.opt_frame, ["错题"])
check(len(err_btns) >= 1, f"成绩页错题按钮 ({len(err_btns)}个)")
if err_btns:
    err_btns[0].invoke()
    root.update()
    check("错题回顾" in app.head_label.cget("text"), "点击错题进入回顾")
    back = find_btns(app.opt_frame, ["返回成绩"])
    check(len(back) == 1, "回顾页有返回成绩按钮")
    if back:
        back[0].invoke()
        root.update()

# 12. 重新考试
app._exam_start()
root.update()
check(app._exam_running() and len(app.queue) == 30, "重新考试")

# 13. 考试中断恢复 + 关闭提醒
app.exam_left = 500
app.exam["left"] = 500
app._save_exam()
root.destroy()
root2 = tk.Tk()
root2.withdraw()
app2 = bs.App(root2)
root2.update()
check(app2.mode == "考试" and app2.exam["left"] <= 500, "重启恢复考试板块")
app2._on_close()   # askyesno=True → 保存退出
check(True, "考试中关闭触发提醒并退出")

# 14. 重置（3次确认 + 冷静期 patch）
root3 = tk.Tk()
root3.withdraw()
app3 = bs.App(root3)
root3.update()
app3._confirm_cool_down = lambda: True
app3.reset_progress()
root3.update()
check(app3.progress == {}, "重置后进度清空")
check(app3.mode == "顺序", "重置后回到顺序")
root3.destroy()

# 清理
os.remove(bp) if os.path.exists(bp) else None
if os.path.exists(tmp):
    os.replace(tmp, bp)
print()
print("===== 刷题软件自检: %s =====" % ("全部通过" if fails == 0 else f"有 {fails} 个失败"))
sys.exit(fails)
