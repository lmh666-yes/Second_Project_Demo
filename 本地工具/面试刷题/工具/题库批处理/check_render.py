# -*- coding: utf-8 -*-
"""检查渲染效果 + 输出未改造题号"""
import sys, io, os, json, importlib
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, r"D:\github\面试刷题\面试题刷题软件-主观题\core")
sys.path.insert(0, r"D:\github\面试刷题\面试题刷题软件-主观题\tests")
import tkinter as tk
import progress_guard as pg

ss = importlib.import_module("主观题软件")
ss.messagebox.showinfo = lambda *a: None
ss.messagebox.showwarning = lambda *a: None
ss.messagebox.showerror = lambda *a: None

bp = ss.PROG_PATH
had = pg.backup(bp)
root = tk.Tk(); root.withdraw()
app = ss.App(root); root.update()

# #1（已改造）
app.idx = 0
app.show_question()
app.toggle_reveal()
root.update()
print("====== #1 fb ======")
print(app.fb.get("1.0", "end")[:900])

# 一个未改造的题（找出第一个 analysis 为空的）
app.toggle_reveal()  # 收起
legacy = next(it for it in app.bank if not it.get("analysis"))
app.queue = [legacy]; app.idx = 0
app.show_question(); app.toggle_reveal(); root.update()
print("====== #%s fb（未改造）======" % legacy["num"])
print(app.fb.get("1.0", "end")[:300])

root.destroy()
pg.restore()
if not had and os.path.exists(bp):
    os.remove(bp)

bank = json.load(open(r"D:\github\面试刷题\面试题刷题软件-主观题\题库.json", encoding="utf-8"))
todo = [it["num"] for it in bank if not it.get("analysis")]
print()
print("未改造数:", len(todo))
print("未改造题号:", todo)
