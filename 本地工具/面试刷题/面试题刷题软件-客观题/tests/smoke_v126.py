# -*- coding: utf-8 -*-
"""1.2.2 冒烟：hover修复 / 笔记独立窗口 / 考试改革(30题40分150分) / 考试禁笔记"""
import sys, io, os, tkinter as tk
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
import 刷题软件 as bs
import progress_guard as pg   # 保护用户进度（progress.json 不在 git 里）
bs.messagebox.showinfo = lambda t, m: None
bs.messagebox.showwarning = lambda t, m: None
bs.messagebox.showerror = lambda t, m: None
bs.messagebox.askyesno = lambda t, m: True

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

# ---- 1. hover 修复：鼠标悬停只高亮所在行 ----
app.idx = 0
app.show_question()
root.update()
rows = app._choice_rows
ok("选项行数4", len(rows) == 4, str(len(rows)))
rows[0]["frame"].event_generate("<Enter>")
root.update()
ok("悬停行0仅行0高亮", rows[0]["frame"].cget("bg") == "#eaf2f8"
   and rows[1]["frame"].cget("bg") == "#ffffff"
   and rows[2]["frame"].cget("bg") == "#ffffff"
   and rows[3]["frame"].cget("bg") == "#ffffff",
   f"bg={[r['frame'].cget('bg') for r in rows]}")
rows[2]["frame"].event_generate("<Enter>")
root.update()
rows[0]["frame"].event_generate("<Leave>")   # 模拟鼠标从行0移开
root.update()
ok("悬停行2仅行2高亮", rows[2]["frame"].cget("bg") == "#eaf2f8"
   and rows[0]["frame"].cget("bg") == "#ffffff"
   and rows[1]["frame"].cget("bg") == "#ffffff"
   and rows[3]["frame"].cget("bg") == "#ffffff",
   f"bg={[r['frame'].cget('bg') for r in rows]}")
rows[2]["frame"].event_generate("<Leave>")
root.update()
ok("悬停离开恢复白色", rows[2]["frame"].cget("bg") == "#ffffff")

# ---- 2. 笔记独立窗口 ----
ok("初始无笔记窗口", app.note_win is None)
app._open_note()
root.update()
ok("打开笔记窗口", app.note_win is not None and app.note_win.winfo_exists())
app.note_text.insert("1.0", "我的测试笔记")
app._note_edited()
root.update()
app.next_q()
root.update()
app.prev_q()
root.update()
ok("切题后笔记回显", app.note_text.get("1.0", "end-1c") == "我的测试笔记",
   repr(app.note_text.get("1.0", "end-1c")))
ok("笔记已保存到进度", app.progress.get(app.queue[0]["id"], {}).get("notes") == "我的测试笔记")
app._close_note()
ok("关闭笔记窗口", app.note_win is None and app.note_text is None)

# ---- 3. 考试改革 ----
app._exam_start()
root.update()
kinds = {}
for it in app.queue:
    kinds[it["kind"]] = kinds.get(it["kind"], 0) + 1
ok("考试30题", len(app.queue) == 30, str(len(app.queue)))
ok("20选择+10判断", kinds.get("choice") == 20 and kinds.get("judge") == 10, str(kinds))
ok("考试40分钟", 2390 < app.exam_left <= 2400, str(app.exam_left))
ok("满分150", bs.EXAM_NUM * bs.EXAM_SCORE == 150, str(bs.EXAM_NUM * bs.EXAM_SCORE))

# ---- 4. 考试中禁开笔记 ----
ok("考试中note_win为None", app.note_win is None)
app._open_note()
root.update()
ok("考试中禁开笔记", app.note_win is None, "已拦截")

app.finish_exam()
root.update()
ok("交卷完成", app.exam.get("finished") is True)
ok("成绩页满分150", "150" in app.stem.get("1.0", "end-1c") or "150" in str(app.head_label.cget("text")))

# 清理
if app.note_win is not None and app.note_win.winfo_exists():
    app._close_note()
root.destroy()
print(f"\n===== 冒烟: PASS={P} FAIL={F} =====")
sys.exit(1 if F else 0)
