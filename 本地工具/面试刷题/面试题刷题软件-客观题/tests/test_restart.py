# -*- coding: utf-8 -*-
"""专项测试: 重新考试先回到待开始页(不直接开考)
验证: ①交卷后点「重新考试」→ 回到待开始页(非直接开考) ②待开始页显示「开始考试」按钮
③此时无活跃考试会话 ④点「开始考试」才正式开考 ⑤次数在交卷时才+1(重新考试本身不+1)
⑥退出/再次进入考试板块仍显示待开始页"""
import sys, io, os, tkinter as tk
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
import 刷题软件 as bs
import progress_guard as pg   # 保护用户进度（progress.json 不在 git 里）
bs.messagebox.showinfo = lambda t, m: None
bs.messagebox.showwarning = lambda t, m: None
bs.messagebox.showerror = lambda t, m: None
bs.messagebox.askyesno = lambda t, m: True

# 保证独立环境: 清掉上次遗留的进度/考试状态(避免被前序测试污染)
pg.backup(bs.PROG_PATH)     # 保护用户进度：测试结束时自动恢复

P = 0
F = 0
def ok(name, cond, extra=""):
    global P, F
    if cond:
        P += 1
        print(f"  [PASS] {name} {extra}")
    else:
        F += 1
        print(f"  [FAIL] {name} {extra}")

root = tk.Tk()
root.geometry("1180x720")
app = bs.App(root)
root.update()

def find_btn_text(text):
    from tkinter import Button
    for w in app.exam_bar.winfo_children():
        if isinstance(w, Button) and text in w.cget("text"):
            return w
    for w in app.opt_frame.winfo_children():
        if isinstance(w, Button) and text in w.cget("text"):
            return w
    return None

# 1. 进入考试 → 开始 → 答几题 → 交卷
app.set_mode("考试")
root.update()
ok("待开始页有开始按钮", find_btn_text("开始考试") is not None)
app._exam_start()
root.update()
ok("开考后 active", app._exam_active() and not app._exam_finished())
# 答前2题
for i in range(2):
    app.idx = i
    app._exam_render_q()
    it = app.queue[i]
    if it["kind"] == "choice":
        oinfo = app.exam.get("opts", {}).get(it["id"])
        k = oinfo["answer"] if oinfo else it["answer"].upper()
        app.choice_var.set(k)
    else:
        app.answer_judge("√")
    app._exam_answer(it, app.choice_var.get() or "√")
    root.update()
before = app.progress.get("_exam_count", 0)
app.finish_exam()
root.update()
ok("交卷后计数+1", app.progress.get("_exam_count", 0) == before + 1)
ok("交卷后显示成绩页", "考试结束" in app.head_label.cget("text"))

# 2. 点「重新考试」→ 应回到待开始页
btn = find_btn_text("重新考试")
ok("右上角有重新考试按钮", btn is not None)
btn.invoke()
root.update()
ok("回到待开始页(有开始考试按钮)", find_btn_text("开始考试") is not None)
ok("待开始页标题", "模拟考试" in app.head_label.cget("text"))
ok("无活跃考试会话", not app._exam_active())
ok("次数未因重新考试+1", app.progress.get("_exam_count", 0) == before + 1)
ok("待开始页说明含开始提示",
   "点击右上角「▶ 开始考试」按钮开始" in app.stem.get("1.0", "end"))

# 3. 此时切走再回考试板块 → 仍是待开始页(不会自动开考/显示成绩)
app.set_mode("顺序")
root.update()
app.set_mode("考试")
root.update()
ok("切回考试板块仍是待开始页", find_btn_text("开始考试") is not None and not app._exam_active())

# 4. 点「开始考试」才开考
btn2 = find_btn_text("开始考试")
btn2.invoke()
root.update()
ok("点开始考试后才开考", app._exam_active() and not app._exam_finished())
ok("开考计数不变", app.progress.get("_exam_count", 0) == before + 1)

# 5. 交卷后再走一次重新考试流程,确认稳定
app.finish_exam()
root.update()
btn3 = find_btn_text("重新考试")
btn3.invoke()
root.update()
ok("第二次重新考试也回待开始页", find_btn_text("开始考试") is not None and not app._exam_active())

root.destroy()
print(f"\n===== 重新考试回待开始页测试: PASS={P} FAIL={F} =====")
sys.exit(1 if F else 0)
