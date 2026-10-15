# -*- coding: utf-8 -*-
"""C刷题软件 · 压力测试（1.0.8）
覆盖：快速翻题 / 全题库作答 / 模式切换 / 笔记 / 考试循环(开考/答题/暂停/继续/交卷/
重新考试) / 随机混合 / 控件泄漏检测 / 定时器零残留"""
import sys, io, os, random, faulthandler, tkinter as tk
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
faulthandler.dump_traceback_later(600, exit=True)   # 本测试重负载（5000翻题/50场考试/3000随机），保留较长余量
import importlib.util
spec = importlib.util.spec_from_file_location("bs", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core", "刷题软件.py"))
bs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bs)
import progress_guard as pg   # 保护用户进度（progress.json 不在 git 里）

PASS = 0
FAIL = 0
def ok(name, cond, extra=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name} {extra}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name} {extra}")

bs.messagebox.showinfo = lambda t, m: None
bs.messagebox.showerror = lambda t, m: None
bs.messagebox.showwarning = lambda t, m: None
bs.messagebox.askyesno = lambda t, m: True

def main():
    # 自包含：清掉上次遗留的进度/考试状态，避免 App 自动进入考试板块、queue 为空
    pg.backup(bs.PROG_PATH)     # 保护用户进度：测试结束时自动恢复
    root = tk.Tk()
    root.geometry("1180x720")
    app = bs.App(root)
    root.update()
    N = len(app.bank)
    ok("题库加载", N == 367, f"({N})")

    # ---- 1. 快速翻题 5000 次 ----
    max_n, nav_lost = 0, 0
    for k in range(5000):
        if k % 2 == 0:
            app.next_q()
        else:
            app.prev_q()
        if k % 50 == 0:
            root.update_idletasks()
            root.update()
            max_n = max(max_n, len(app.opt_frame.winfo_children()))
            for b, _ in app.nav_btns:
                if not b.winfo_ismapped():
                    nav_lost += 1
                    break
    ok("5000次快速翻题", True)
    ok("选项区控件稳定(<=9)", max_n <= 9, f"max={max_n}")
    ok("导航全程可见", nav_lost == 0, f"lost={nav_lost}")

    # ---- 2. 全题库作答 ----
    app.set_mode("顺序")
    err = 0
    for i in range(N):
        app.idx = i
        app.show_question()
        it = app.queue[i]
        try:
            if it["kind"] == "choice":
                app.choice_var.set(it["options"][0]["key"])
                app.check()
            else:
                app.answer_judge("√")
                app.check()
        except Exception as e:
            err += 1
    ok("全题库作答", err == 0, f"err={err}")

    # ---- 3. 500 次模式切换（非考试） ----
    err = 0
    for _ in range(500):
        try:
            app.set_mode(random.choice(["顺序", "错题"]))
            root.update_idletasks()
        except Exception as e:
            err += 1
    ok("500次模式切换", err == 0, f"err={err}")

    # ---- 4. 笔记压力（独立窗口） ----
    app.set_mode("顺序")
    app._open_note()
    err = 0
    for i in range(N):
        try:
            app.idx = i
            app.show_question()
            app.note_text.delete("1.0", "end")
            app.note_text.insert("1.0", f"n{i}")
            app._note_edited()
            app.next_q()
        except Exception as e:
            err += 1
    app.idx = 0
    app.show_question()
    ok("笔记连续写入/回显", err == 0 and app.note_text.get("1.0", "end-1c") == "n0",
       f"err={err}")

    # ---- 5. 50 次完整考试循环 ----
    err = 0
    for _ in range(50):
        try:
            app.set_mode("考试")
            app._exam_start()
            root.update_idletasks()
            for i in range(len(app.queue)):
                app.idx = i
                app._exam_render_q()
                q = app.queue[i]
                if q["kind"] == "choice":
                    app.choice_var.set(q["options"][0]["key"])
                    app.check()
                else:
                    app.answer_judge("√")
                    app.check()
            app.finish_exam()
            root.update_idletasks()
            app._exam_toggle_pause()
            app._exam_toggle_pause()
        except Exception as e:
            err += 1
            if err <= 3:
                print("    ERR exam:", e)
    ok("50次完整考试循环", err == 0, f"err={err}")
    ok("交卷后无残留定时器", app._exam_timer is None)
    ok("交卷后可重新考试", app._exam_finished())

    # ---- 6. 3000 次随机混合 ----
    app.set_mode("顺序")
    ops = ["next", "prev", "check", "judge", "note"]
    err = 0
    for _ in range(3000):
        op = random.choice(ops)
        try:
            if op == "next":
                app.next_q()
            elif op == "prev":
                app.prev_q()
            elif op == "check":
                app.choice_var.set(random.choice(["A", "B", "C", "D"]))
                app.check()
            elif op == "judge":
                app.answer_judge(random.choice(["√", "×"]))
                app.check()
            else:
                app.note_text.insert("end", "x")
                app._note_edited()
        except Exception as e:
            err += 1
            if err <= 5:
                print("    ERR op:", op, e)
        if random.random() < 0.02:
            root.update_idletasks()
    ok("3000次随机混合", err == 0, f"err={err}")

    # ---- 7. 最终检查 ----
    root.update_idletasks()
    root.update()
    mapped = [b.winfo_ismapped() for b, _ in app.nav_btns]
    ok("最终控件数量正常", len(app.opt_frame.winfo_children()) <= 9,
       f"n={len(app.opt_frame.winfo_children())}")
    ok("最终导航可见", all(mapped), f"{mapped}")
    ok("最终无残留定时器", app._exam_timer is None)
    root.destroy()
    print(f"\n===== 压力测试结果: PASS={PASS} FAIL={FAIL} =====")
    sys.exit(1 if FAIL else 0)

if __name__ == "__main__":
    main()
