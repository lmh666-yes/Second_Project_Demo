# -*- coding: utf-8 -*-
"""「📂 分区」窗口布局回归测试（挤压修复）：
1) 窗口有最小尺寸 860x560，窄窗不再裁切内容
2) 左右两栏为可拖动 PanedWindow（4:6），各自带纵向滚动条
3) 选大区/分区 → 右侧清单正确刷新；缩放窗口各控件仍有合理宽高
4) 全屏截图 sec2_*.png 供人工查看（不参与断言）
"""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
try:                                    # DPI 感知，截图坐标才准
    import ctypes
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass
import tkinter as tk
from tkinter import ttk
import progress_guard as pg
import 大题软件 as bs
from PIL import ImageGrab

SNAP = "--snap" in sys.argv
fails = 0
def check(cond, msg):
    global fails
    print(("  [PASS] " if cond else "  [FAIL] ") + msg)
    if not cond:
        fails += 1

bp = bs.PROG_PATH
had = pg.backup(bp)
bs.messagebox.showinfo = lambda t, m: None
bs.messagebox.showerror = lambda t, m: None
bs.messagebox.showwarning = lambda t, m: None
bs.messagebox.askyesno = lambda t, m: False

root = tk.Tk()
app = bs.App(root)
root.update()

# 打开分区窗口
app._open_sections()
root.update()
win = next((w for w in root.winfo_children() if isinstance(w, tk.Toplevel)), None)
check(win is not None, "分区窗口已打开")
if win is None:
    root.destroy(); pg.restore(); sys.exit(1)
win.update_idletasks(); win.update()

def walk(w):
    out = [w]
    for c in w.winfo_children():
        out += walk(c)
    return out

widgets = walk(win)
def bycls(cls):
    return [w for w in widgets if w.winfo_class() == cls]

tree = bycls("Treeview")
lst = bycls("Listbox")
pane = bycls("TPanedwindow")
sbs = bycls("TScrollbar")
check(win.minsize() == (860, 560), f"最小尺寸 {win.minsize()}")
check(len(pane) == 1, f"存在可拖动分栏 PanedWindow（{len(pane)}）")
check(len(tree) == 1 and tree[0].winfo_manager() == "grid", "左树 grid 布局")
check(len(lst) == 1 and lst[0].winfo_manager() == "grid", "右清单 grid 布局")
check(len(sbs) == 2 and all(s.winfo_manager() == "grid" for s in sbs),
      f"两栏各有纵向滚动条（{len(sbs)}）")

tv, lb = tree[0], lst[0]
groups = tv.get_children()
check(len(groups) > 0, f"分区树已建（{len(groups)} 个大区）")
first_gid = groups[0]
tv.selection_set(first_gid)
win.update()
n_group = len(getattr(lb, "_items", []))
check(n_group > 0 and lb.size() == n_group, f"选大区后清单刷新（{lb.size()} 题）")
# 选一个子分区
kids = tv.get_children(first_gid)
if kids:
    tv.selection_set(kids[0])
    win.update()
    n_sec = len(getattr(lb, "_items", []))
    check(n_sec > 0 and lb.size() == n_sec, f"选子分区后清单刷新（{n_sec} 题）")
    check(n_sec <= n_group, "子分区题数 ≤ 大区题数")

# 缩放测试
for wh in ((860, 560), (1400, 900), (900, 620)):
    win.geometry(f"{wh[0]}x{wh[1]}")
    root.update(); win.update_idletasks(); win.update()
    tw, lw = tv.winfo_width(), lb.winfo_width()
    check(tw > 200 and lw > 250, f"{wh} 下两栏宽度正常（树{tw}/清单{lw}）")

if SNAP:
    win.attributes("-topmost", True)
    win.lift()
    win.geometry("1000x680")
    root.update(); time.sleep(0.6)
    ImageGrab.grab(all_screens=True).save("sec2_full.png")
    print("  截图 sec2_full.png")

win.destroy()
root.destroy()
pg.restore()
print(f"\n===== 分区窗口测试: FAIL={fails} =====")
sys.exit(1 if fails else 0)
