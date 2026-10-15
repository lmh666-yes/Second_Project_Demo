# -*- coding: utf-8 -*-
"""大题版 UI 长内容专项测试：
1) 超长题干 → 封顶 14 行 + 滚动条
2) 超长参考答案/得分点 → 参考答案区出现滚动条 + 拖动分隔条可放大该区域
3) 短内容 → 无滚动条
4) 窗口缩放无异常
5) 考试全流程无异常
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
import tkinter as tk
import progress_guard as pg   # 保护用户进度（progress.json 不在 git 里）
import 大题软件 as bs

fails = 0
def check(cond, msg):
    global fails
    print(("  [PASS] " if cond else "  [FAIL] ") + msg)
    if not cond:
        fails += 1

bs.messagebox.showinfo = lambda t, m: None
bs.messagebox.showerror = lambda t, m: None
bs.messagebox.showwarning = lambda t, m: None
bs.messagebox.askyesno = lambda t, m: True

bp = bs.PROG_PATH
had = pg.backup(bp)

root = tk.Tk()
app = bs.App(root)
root.update()

# 1. 超长题干
long_stem = "\n".join(["很长的大题题干内容测试，" * 6] * 30)
app.set_mode("顺序")
fake = {"id": "T-long", "kind": "bigq", "stem": long_stem,
        "answer": "参考答案内容。" * 200, "explain": "得分点内容。" * 200}
app.queue = [fake]
app.idx = 0
app._render_images({})      # 清掉题干配图（大题第 1 题带图，会占用高度影响后续断言）
root.update()
app._set_readonly(app.stem, long_stem, 4, 14)
root.update()
root.update_idletasks()
check(int(app.stem.cget("height")) == 14,
      f"超长题干封装顶 14 行（实际 {app.stem.cget('height')}）")
check(app.stem_sb.winfo_manager() == "grid", "超长题干出现滚动条")

# 2. 超长参考答案 ⇒ fb
app.fb.config(state=tk.NORMAL)
app.fb.delete("1.0", "end")
app.fb.insert(tk.END, "✅ 参考答案\n", ("h_ans",))
app.fb.insert(tk.END, fake["answer"] + "\n\n")
app.fb.insert(tk.END, "🎯 得分点\n", ("h_exp",))
app.fb.insert(tk.END, fake["explain"])
app.fb.config(state=tk.DISABLED)
app._fit_fb_view()
root.update()
root.update_idletasks()
check(app.fb_sb.winfo_manager() == "grid", "超长参考答案区出现滚动条（内容不丢）")
h_before = app.fb.winfo_height()
try:
    app.split.sashpos(0, max(150, app.split.winfo_height() - 400))     # ttk 分栏
except Exception:
    try:
        app.split.sash_place(0, 0, max(150, app.split.winfo_height() - 400))
    except Exception:
        pass
root.update()
root.update_idletasks()
h_after = app.fb.winfo_height()
check(h_after > h_before, f"拖动分隔条可放大参考答案区（{h_before} → {h_after}）")

# 3. 短内容
app._set_readonly(app.stem, "短题干", 4, 14)
app._clear_fb()
root.update()
root.update_idletasks()
check(int(app.stem.cget("height")) == 4, f"短题干高度 4（实际 {app.stem.cget('height')}）")
check(app.stem_sb.winfo_manager() != "grid", "短题干无滚动条")
check(app.fb_sb.winfo_manager() != "grid",
      "短反馈无滚动条")

# 4. 窗口缩放
root.geometry("920x640")
root.update()
root.update_idletasks()
root.geometry("1180x820")
root.update()
root.update_idletasks()
check(True, "窗口缩放无异常")

# 5. 考试全流程
app.set_mode("考试")
app._exam_start()
root.update()
total = len(app.queue)
check(total == 10, f"考试 10 题（实际 {total}）")
for i in range(total):
    app.idx = i
    app.ans_text.delete("1.0", "end")
    app.ans_text.insert("1.0", f"我的作答 {i}")
    app._save_answer()
root.update()
app.finish_exam()
root.update()
check(app.exam.get("finished") is True, "交卷完成")
app._exam_show_result_page()
root.update()
check(True, "成绩页渲染无异常")

root.destroy()
pg.restore()
if not had and os.path.exists(bp):
    os.remove(bp)

print()
print("结果：" + ("全部通过" if fails == 0 else f"{fails} 项失败"))
sys.exit(1 if fails else 0)
