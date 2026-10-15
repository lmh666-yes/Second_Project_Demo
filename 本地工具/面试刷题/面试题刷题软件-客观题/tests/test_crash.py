# -*- coding: utf-8 -*-
"""C刷题软件 · 崩溃测试（1.0.8）
覆盖：题库损坏JSON / 空题库 / 缺字段题目 / progress损坏 / 题库缺失 / 极端题干 /
考试状态损坏(非dict / 失效id)"""
import sys, io, os, json, shutil, faulthandler, tkinter as tk
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
faulthandler.dump_traceback_later(240, exit=True)

import importlib.util
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
spec = importlib.util.spec_from_file_location("bs", os.path.join(ROOT, "core", "刷题软件.py"))
bs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bs)

BANK = os.path.join(ROOT, "题库.json")
PROG = os.path.join(ROOT, "progress.json")
BAK_BANK = os.path.join(HERE, "题库.json.bak")
BAK_PROG = os.path.join(HERE, "progress.json.bak")
SNAP_BANK = os.path.join(HERE, "题库.good.json")   # 上次跑动的干净题库快照（硬超时时兜底）

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

_msgs = []
bs.messagebox.showinfo = lambda t, m: _msgs.append((t, m))
bs.messagebox.showerror = lambda t, m: _msgs.append((t, m))
bs.messagebox.showwarning = lambda t, m: _msgs.append((t, m))
bs.messagebox.askyesno = lambda t, m: True

def write_bank(data):
    with open(BANK, "w", encoding="utf-8") as f:
        f.write(data)

def write_prog(data):
    with open(PROG, "w", encoding="utf-8") as f:
        f.write(data)

def make_app(root):
    try:
        app = bs.App(root)
        root.update()
        return app, None
    except Exception as e:
        return None, e

def test_normal():
    print("— 场景1: 正常题库 —")
    write_bank(open(BAK_BANK, encoding="utf-8").read())
    root = tk.Tk()
    root.geometry("1180x720")
    app, err = make_app(root)
    ok("正常题库启动不崩溃", err is None, str(err) if err else "")
    ok("题库367题", app is not None and len(app.bank) == 367,
       str(len(app.bank)) if app else "N/A")
    root.destroy()

def test_broken_json():
    print("— 场景2: 题库为损坏JSON —")
    write_bank('{"bad": json 截断!!!')
    root = tk.Tk()
    root.geometry("1180x720")
    app, err = make_app(root)
    ok("损坏JSON不崩溃(降级为空)", err is None, str(err) if err else "")
    ok("给出错误提示", any("题库" in t for t, _ in _msgs))
    root.destroy()

def test_empty_list():
    print("— 场景3: 空题库 [] —")
    write_bank("[]")
    root = tk.Tk()
    root.geometry("1180x720")
    app, err = make_app(root)
    ok("空题库不崩溃", err is None, str(err) if err else "")
    root.destroy()

def test_not_list():
    print("— 场景4: 题库为对象/字符串 —")
    write_bank('{"a": 1}')
    root = tk.Tk()
    root.geometry("1180x720")
    app, err = make_app(root)
    ok("非列表题库不崩溃", err is None, str(err) if err else "")
    root.destroy()

def test_missing_fields():
    print("— 场景5: 题目缺字段 —")
    bad = [
        {"kind": "choice", "id": "x1", "num": 1, "stem": "缺options/answer的单选题"},
        {"kind": "judge", "id": "x2", "num": 2},
        {"id": "x4"},
        {},
    ]
    write_bank(json.dumps(bad, ensure_ascii=False))
    root = tk.Tk()
    root.geometry("1180x720")
    app, err = make_app(root)
    ok("缺字段题库启动不崩溃", err is None, str(err) if err else "")
    err2 = 0
    if app:
        for i in range(len(app.bank)):
            try:
                app.idx = i
                app.show_question()
                app.check()
                app.answer_judge("√")
            except Exception as e:
                err2 += 1
    ok("缺字段题目作答不崩溃", err2 == 0, f"err={err2}")
    root.destroy()

def test_huge_stem():
    print("— 场景6: 极端超长题干/选项 —")
    huge = "很长的题干" * 2000
    q = {"kind": "choice", "id": "big", "num": 1, "stem": huge,
         "options": [{"key": "A", "text": "选项" * 1000}], "answer": "A"}
    write_bank(json.dumps([q] * 12, ensure_ascii=False))
    root = tk.Tk()
    root.geometry("1180x720")
    app, err = make_app(root)
    ok("超长题干不崩溃", err is None, str(err) if err else "")
    err2 = 0
    if app:
        for i in range(12):
            try:
                app.idx = i
                app.show_question()
                app.check()
            except Exception as e:
                err2 += 1
    ok("超长题作答不崩溃", err2 == 0, f"err={err2}")
    root.destroy()

def test_broken_progress():
    print("— 场景7: progress.json 损坏 —")
    write_bank(open(BAK_BANK, encoding="utf-8").read())
    write_prog("{ not valid !!")
    root = tk.Tk()
    root.geometry("1180x720")
    app, err = make_app(root)
    ok("损坏progress不崩溃(降级空)", err is None, str(err) if err else "")
    ok("进度被重置为空", app is not None and app.progress == {})
    root.destroy()

def test_exam_corrupt():
    print("— 场景8: 考试状态损坏 —")
    write_bank(open(BAK_BANK, encoding="utf-8").read())
    # exam 非 dict
    write_prog(json.dumps({"exam": "corrupted-string"}, ensure_ascii=False))
    root = tk.Tk()
    root.geometry("1180x720")
    app, err = make_app(root)
    ok("exam非dict不崩溃", err is None, str(err) if err else "")
    root.destroy()
    # exam 引用失效 id
    write_prog(json.dumps({"exam": {"active": True, "finished": False, "paused": False,
                                    "ids": ["nonexist-1", "nonexist-2"], "idx": 0,
                                    "left": 100, "answers": {"nonexist-1": "A"}}},
                          ensure_ascii=False))
    root = tk.Tk()
    root.geometry("1180x720")
    app, err = make_app(root)
    ok("exam失效id不崩溃", err is None, str(err) if err else "")
    if app:
        try:
            app._exam_toggle_pause()
            root.update()
            app.finish_exam()
            root.update()
        except Exception as e:
            ok("失效id考试操作不崩溃", False, str(e))
        else:
            ok("失效id考试操作不崩溃", True)
    root.destroy()

def test_missing_bank():
    print("— 场景9: 题库文件缺失 —")
    if os.path.exists(BANK):
        os.remove(BANK)
    root = tk.Tk()
    root.geometry("1180x720")
    app, err = make_app(root)
    ok("题库缺失不崩溃", err is None, str(err) if err else "")
    ok("给出错误提示", any("题库" in t for t, _ in _msgs))
    root.destroy()

def restore():
    if os.path.exists(BAK_BANK):
        shutil.copy(BAK_BANK, BANK)
    if os.path.exists(BAK_PROG):
        shutil.copy(BAK_PROG, PROG)
    for p in (BAK_BANK, BAK_PROG):
        if os.path.exists(p):
            os.remove(p)

def main():
    # 题库快照防污染：优先用当前题库；当前题库异常时回退到上次快照（防硬超时残留）
    def _valid_bank(p):
        try:
            with open(p, encoding="utf-8") as f:
                d = json.load(f)
            return isinstance(d, list) and len(d) >= 100
        except Exception:
            return False
    if os.path.exists(BANK) and _valid_bank(BANK):
        shutil.copy(BANK, SNAP_BANK)
    elif os.path.exists(SNAP_BANK) and _valid_bank(SNAP_BANK):
        shutil.copy(SNAP_BANK, BANK)
        print("  （当前题库异常，已从上次快照恢复）")
    else:
        print("题库文件异常且无可用快照，请先运行 core/build_bank.py 重建题库！")
        sys.exit(1)
    if os.path.exists(BANK):
        shutil.copy(BANK, BAK_BANK)
    if os.path.exists(PROG):
        shutil.copy(PROG, BAK_PROG)
    try:
        # 每个场景从干净的 progress.json 开始（避免上次测试遗留的考试状态
        # 导致 App 自动进入考试板块、queue 为空而报 IndexError）
        for fn in (test_normal, test_broken_json, test_empty_list, test_not_list,
                   test_missing_fields, test_huge_stem, test_broken_progress,
                   test_exam_corrupt, test_missing_bank):
            write_prog("{}")
            fn()
    finally:
        restore()
        if _valid_bank(BANK):          # 成功收尾后刷新快照
            shutil.copy(BANK, SNAP_BANK)
    print(f"\n===== 崩溃测试结果: PASS={PASS} FAIL={FAIL} =====")
    sys.exit(1 if FAIL else 0)

if __name__ == "__main__":
    main()
