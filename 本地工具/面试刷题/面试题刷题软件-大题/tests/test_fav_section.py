# -*- coding: utf-8 -*-
"""大题桌面版：跳题修复 + 收藏 + 分区跳转 专项测试"""
import sys
import io
import os

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, r'D:\github\面试刷题\面试题刷题软件-大题\core')
sys.path.insert(0, r'D:\github\面试刷题\面试题刷题软件-大题\tests')
import tkinter as tk
import progress_guard as pg
import importlib

ss = importlib.import_module('大题软件')

fails = 0
def check(cond, msg, extra=""):
    global fails
    line = ("  [PASS] " if cond else "  [FAIL] ") + msg
    if extra:
        line += " " + str(extra)
    print(line)
    if not cond:
        fails += 1

infos = []
ss.messagebox.showinfo = lambda t, m: infos.append((t, m))
ss.messagebox.showwarning = lambda t, m: None
ss.messagebox.askyesno = lambda t, m: True

bp = ss.PROG_PATH
had = pg.backup(bp)

root = tk.Tk()
root.withdraw()
app = ss.App(root)
root.update()

nums = [it['num'] for it in app.bank]
max_num = max(nums)
print('题库', len(app.bank), '题，题号范围含空档；最大题号', max_num)

# ---- 1. 跳题：按题号 ----
target_num = next(n for n in (430, 401, 34, 6) if n in nums)
app.jump_var.set(str(target_num))
app._jump()
root.update()
check(app.queue[app.idx]['num'] == target_num, f"按题号跳转（输入 {target_num}）")

# ---- 2. 跳题：空档号 → 按“第 N 题”位置兜底 ----
gap = next(n for n in range(2, len(app.queue) + 1) if n not in nums)
app.jump_var.set(str(gap))
app._jump()
root.update()
check(app.queue[app.idx]['num'] == app.queue[gap - 1]['num'],
      f"空档号 {gap} 按第 {gap} 题位置跳转（落到题号 {app.queue[app.idx]['num']}）")

# ---- 3. 跳题：越界 → 提示（含真实范围） ----
infos.clear()
app.jump_var.set(str(max_num + 500))
app._jump()
root.update()
check(infos and "未找到" in infos[-1][0] and str(max_num) in infos[-1][1],
      "越界提示含真实最大题号", infos[-1][1][:50] if infos else "无提示")

# ---- 4. 收藏：加入 + 已收藏提示 ----
qid0 = app.bank[0]['id']
app.set_mode("顺序")
app.idx = 0
app.show_question()
app.fav_add()
root.update()
check(qid0 in app.progress.get("_favs", []), "收藏加入 _favs")
infos.clear()
app.fav_add()
root.update()
check(infos and "已在收藏" in infos[-1][1], "重复收藏有提示")

# ---- 5. 收藏板块 ----
app.set_mode("收藏")
root.update()
check(app.mode == "收藏" and [it['id'] for it in app.queue] == [qid0],
      f"收藏板块队列（{[it['num'] for it in app.queue]}）")

# ---- 6. 取消收藏（弹窗确认 True）----
app.unfav_cur()
root.update()
check(qid0 not in app.progress.get("_favs", []), "取消收藏移除")
check(app.mode == "顺序", "收藏清空后回到顺序板块")

# ---- 7. 重置保留收藏 ----
app.set_mode("顺序")
app.idx = 1
app.show_question()
app.fav_add()
root.update()
fav_before = list(app.progress.get("_favs", []))
app.progress[app.bank[2]['id']] = {"wrote": "xxx", "revealed": True}
app._confirm_cool_down = lambda seconds=5: True
app.reset_progress()
root.update()
check(app.progress.get("_favs") == fav_before, f"重置后收藏保留（{fav_before}）")
check(not app.progress.get(app.bank[2]['id']), "重置清空作答")

# ---- 8. 分区跳转窗口 ----
app._open_sections()
root.update()
wins = [w for w in root.winfo_children() if isinstance(w, tk.Toplevel)]
check(bool(wins), "分区窗口已打开")
if wins:
    win = wins[-1]
    import tkinter.ttk as ttk

    def find_tree(w):
        for child in w.winfo_children():
            if isinstance(child, ttk.Treeview):
                return child
            got = find_tree(child)
            if got is not None:
                return got
        return None

    tree = find_tree(win)
    check(tree is not None, "分区树存在")
    if tree is not None:
        groups = tree.get_children()
        secs = []
        for g in groups:
            secs += list(tree.get_children(g))
        check(len(groups) >= 3, f"大区数 {len(groups)}")
        check(len(secs) >= 25, f"分区数 {len(secs)}")
    win.destroy()

# ---- 9. _jump_to_qid：分区跳转落到正确题目 ----
app.set_mode("顺序")
q = app.bank[100]
app._jump_to_qid(q['id'])
root.update()
check(app.queue[app.idx]['id'] == q['id'], f"跳转到题号 {q['num']}")

root.destroy()
pg.restore()
if not had and os.path.exists(bp):
    os.remove(bp)
print()
print(f"结果：{'全部通过 ✔' if fails == 0 else f'{fails} 项失败 ✘'}")
sys.exit(1 if fails else 0)
