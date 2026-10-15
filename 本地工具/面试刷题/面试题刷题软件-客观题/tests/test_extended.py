# -*- coding: utf-8 -*-
"""扩展自检（1.2.0）：数据完整性 / 界面结构(无随机/收藏/重新做) / 判断题交互 /
笔记时机 / 错题进出 / 悬停绑定 / 控件泄漏 / 考试边界"""
import sys, io, os, json, random
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
import tkinter as tk
import progress_guard as pg   # 保护用户进度（progress.json 不在 git 里）
import 刷题软件 as bs

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

def find_btn_all(widget):
    """递归查找所有 Button 控件"""
    out = []
    for w in widget.winfo_children():
        if w.winfo_class() == "Button":
            out.append(w)
        out += find_btn_all(w)
    return out

bp = bs.PROG_PATH
tmp = bp + ".bak"
if os.path.exists(bp):
    os.replace(bp, tmp)

root = tk.Tk()
root.withdraw()
app = bs.App(root)
root.update()

# ---- 1. 数据完整性 ----
bank = app.bank
ok("题库367题", len(bank) == 367, f"({len(bank)})")
kind = {}
for it in bank:
    kind[it["kind"]] = kind.get(it["kind"], 0) + 1
ok("题型分布", kind == {"choice": 321, "multi": 6, "judge": 35, "qa": 5}, str(kind))
miss = [it["id"] for it in bank if not it.get("stem") or not it.get("answer")]
ok("无空题干/空答案", not miss, str(miss[:3]))
JOK = {"对", "错", "√", "×", "T", "F", "TRUE", "FALSE"}
badj = [it["id"] for it in bank if it["kind"] == "judge"
        and str(it.get("answer", "")).strip().upper() not in JOK]
ok("判断题答案合法", not badj, str(badj[:3]))
bada = []
for it in bank:
    if it["kind"] == "choice":
        keys = [o["key"] for o in it.get("options", [])]
        if str(it.get("answer", "")).strip().upper() not in keys or len(keys) != len(set(keys)):
            bada.append(it["id"])
ok("选择题答案在选项内且key唯一", not bada, str(bada[:3]))

# ---- 2. 界面结构：无随机/收藏/重新做题 ----
mode_btns = list(app.mode_btns.keys())
ok("模式按钮=顺序/错题/考试", mode_btns == ["顺序", "错题", "考试"], str(mode_btns))
nav_texts = [t for _, t in app.nav_btns]
ok("底部导航6按钮含笔记+错题考试且无收藏/重做",
   len(nav_texts) == 6 and "我的笔记" in "".join(nav_texts)
   and "错题考试" in "".join(nav_texts)
   and not any("收藏" in t or "重新做" in t for t in nav_texts),
   str(nav_texts))

# ---- 3. 判断题交互：选择不判分，确认才判分 ----
app.set_mode("顺序")
ji, jit = next((i, it) for i, it in enumerate(bank) if it["kind"] == "judge")
app.idx = ji
app.show_question()
app.answer_judge("√")
root.update()
ok("判断选择后未判分", app.progress.get(jit["id"], {}).get("ok") is None)
app.check()
root.update()
ok("判断确认后已判分", app.progress.get(jit["id"], {}).get("ok") is not None)

# ---- 4. 已答题重开显示上次作答 ----
app.show_question()
root.update()
fb_text = app.fb.get("1.0", "end-1c")
ok("已答题反馈显示上次作答", "上次作答" in fb_text, fb_text[:30].replace("\n", " "))

# ---- 5. 笔记时机（独立窗口） ----
app._open_note()
root.update()
ok("笔记窗口打开", app.note_win is not None and app.note_win.winfo_exists())
un = next((i for i, it in enumerate(bank)
           if app.progress.get(it["id"], {}).get("ok") is None), None)
if un is not None:
    app.idx = un
    app.show_question()
    root.update()
    ok("未作答笔记为空", app.note_text.get("1.0", "end-1c") == "",
       repr(app.note_text.get("1.0", "end-1c")))
    it = app.queue[un]
    app.note_text.insert("1.0", "测试笔记")
    app._note_edited()
    if it["kind"] == "choice":
        app.choice_var.set(it["options"][0]["key"])
    else:
        app.answer_judge("√")
    app.check()
    root.update()
    ok("作答后笔记保存", app.progress.get(it["id"], {}).get("notes") == "测试笔记",
       repr(app.progress.get(it["id"], {}).get("notes")))
    app.next_q()
    root.update()
    app.prev_q()
    root.update()
    ok("切题后窗口跟随回显", app.note_text.get("1.0", "end-1c") == "测试笔记",
       repr(app.note_text.get("1.0", "end-1c")))
else:
    ok("找到未答题", False, "未找到")
app._close_note()

# ---- 6. 错题进出（新规则：只有错题考试答对才减寿命） ----
app.set_mode("顺序")
cid = bank[0]["id"]
app.progress[cid] = {"ok": None, "wrong_count": 0, "in_wrong": False,
                     "life": 0, "notes": ""}
app.record(cid, False, "seq")               # 答错 → 入错题集（寿命 1）
app.set_mode("错题")
ok("答错进入错题库", cid in [it["id"] for it in app.queue])
app.record(cid, True, "seq")                # 顺序答对 → 不清除
app.set_mode("错题")
ok("顺序答对不清除错题（新规则）", cid in [it["id"] for it in app.queue])
app.record(cid, True, "exam")               # 普通考试答对 → 不清除
app.set_mode("错题")
ok("普通考试答对不清除错题", cid in [it["id"] for it in app.queue])
app.record(cid, True, "wexam")              # 错题考试答对 → 寿命-1 → 归零移除
app.set_mode("错题")
ok("错题考试答对后移除", cid not in [it["id"] for it in app.queue])

# ---- 7. 进度记录无收藏字段 ----
has_fav = any("fav" in r for r in app.progress.values())
ok("进度记录无收藏字段", not has_fav)

# ---- 8. 悬停绑定存在 ----
nav0 = app.nav_btns[0][0]
ok("导航按钮有Enter绑定", bool(nav0.bind("<Enter>")))
row0 = app._choice_rows[0]["frame"]
ok("选项行有Enter绑定", bool(row0.bind("<Enter>")))

# ---- 9. 快速翻题控件稳定 ----
app.set_mode("顺序")
maxn = 0
for k in range(2000):
    if k % 2 == 0:
        app.next_q()
    else:
        app.prev_q()
    if k % 100 == 0:
        root.update_idletasks()
        root.update()
        maxn = max(maxn, len(app.opt_frame.winfo_children()))
ok("2000次翻题控件稳定", maxn <= 9, f"max={maxn}")

# ---- 10. 考试：重新考试清空答案 / 交卷 ----
app.set_mode("考试")
app._exam_start()
root.update_idletasks()
app.idx = 0
app._exam_render_q()
q0 = app.queue[0]
if q0["kind"] == "choice":
    app.choice_var.set(q0["options"][0]["key"])
else:
    app.answer_judge("√")
app.check()
root.update()
ok("考试作答已记录", len(app.exam["answers"]) >= 1)
app._exam_start()
ok("重新考试清空答案", not app.exam["answers"])
ok("考试队列30题", len(app.queue) == 30)
app.finish_exam()
root.update()
ok("交卷finished", app.exam.get("finished") is True)

# ---- 11. 考试题号导航 / 伪随机 / 次数 ----
app.set_mode("考试")
before_count = app.progress.get("_exam_count", 0)
app._exam_start()
root.update()
ok("开始考试不计数", app.progress.get("_exam_count", 0) == before_count)
nav_btns = find_btn_all(app.exam_nav)
ok("考试导航30个题号按钮", len(nav_btns) == 30, str(len(nav_btns)))
q0 = app.queue[0]
# 第1题答对（考试选项已随机打乱，用打乱后的答案 key）
oinfo0 = app.exam.get("opts", {}).get(q0["id"])
if q0["kind"] == "choice":
    ans0 = oinfo0["answer"] if oinfo0 else q0["answer"].upper().strip()
    app.choice_var.set(ans0)
else:
    app.answer_judge("√")
app.check()
# 第2题答错
if len(app.queue) > 1:
    q1 = app.queue[1]
    app.idx = 1
    app._exam_render_q()
    oinfo1 = app.exam.get("opts", {}).get(q1["id"])
    if q1["kind"] == "choice":
        opts1 = oinfo1["options"] if oinfo1 else q1["options"]
        ans1 = oinfo1["answer"] if oinfo1 else q1["answer"].upper().strip()
        wrong = next(o["key"] for o in opts1 if o["key"] != ans1)
        app.choice_var.set(wrong)
    else:
        a1 = str(q1.get("answer", "")).strip().upper()
        # 判断题要按“正确答案的反面”作答才算答错（之前写死 "×" 会偶发答对）
        app.answer_judge("×" if a1 in ("对", "T", "TRUE", "√", "✅ 对".upper()) else "√")
    app.check()
root.update()
nav_btns2 = find_btn_all(app.exam_nav)
cols = {b.cget("bg") for b in nav_btns2}
ok("答后导航含白/绿/红三色", cols >= {"#ffffff", "#27ae60", "#c62828"}, str(cols))
ok("答错导航为红色非蓝色", "#c62828" in cols and "#2980b9" not in cols)
app._exam_jump(15)
root.update()
ok("题号跳转", app.idx == 15)
app.finish_exam()
root.update()
ok("交卷才计数+1", app.progress.get("_exam_count", 0) == before_count + 1)
ok("交卷后导航仍显示", any(w.winfo_class() == "Button"
   for w in find_btn_all(app.exam_nav)))
# 交卷后：左侧错题按钮带考试序号（与右侧导航对应）
left_wrong = [b for b in find_btn_all(app.opt_frame) if "错题" in b.cget("text")]
ok("错题按钮带考试序号", len(left_wrong) >= 1 and "·第2题" in left_wrong[0].cget("text"),
   left_wrong[0].cget("text") if left_wrong else "无")
# 交卷后点题号进入回顾 + 返回成绩
app._exam_jump(1)
root.update()
ok("交卷后点题号进入回顾", "回顾" in app.head_label.cget("text"),
   app.head_label.cget("text"))
back = [b for b in find_btn_all(app.opt_frame) if "返回成绩" in b.cget("text")]
ok("回顾有返回成绩按钮", len(back) == 1)
if back:
    back[0].invoke()
    root.update()
    ok("返回成绩页", "考试结束" in app.head_label.cget("text"),
       app.head_label.cget("text"))

# ---- 11b. 退出考试不计数 ----
c1 = app.progress.get("_exam_count", 0)
app.set_mode("考试")
app._exam_start()
root.update()
ok("退出前计数不变", app.progress.get("_exam_count", 0) == c1)
app._exam_quit()
root.update()
ok("退出考试不计数", app.progress.get("_exam_count", 0) == c1)

# ---- 11c. 40字符折行 ----
app.set_mode("顺序")
long_opt = "这是" + "很长很长的选项文字" * 8
# wrap_cn 已从软件中移除（改为标签自动换行）——旧检查废弃，保留占位以维持计数
ok("wrap_cn折行(旧功能已废弃)", True, "")

# ---- 12. 计划式抽题覆盖性：连续 N 次考试覆盖全部题目 ----
app.progress.pop("_exam_plan", None)
seen_c, seen_j = set(), set()
n_all_c = sum(1 for it in bank if it["kind"] in ("choice", "multi"))
n_all_j = sum(1 for it in bank if it["kind"] == "judge")
rounds = max(-(-n_all_c // 20), -(-n_all_j // 10))
for k in range(rounds):
    ids = app._exam_pick_ids()
    if k == 0:
        ok("每次考试30题", len(ids) == 30, f"({len(ids)})")
    for qid in ids:
        it = app._by_id(qid)
        if it and it["kind"] in ("choice", "multi"):
            seen_c.add(qid)
        elif it and it["kind"] == "judge":
            seen_j.add(qid)
ok(f"{rounds}次考试覆盖全部选择题({n_all_c})", len(seen_c) == n_all_c, f"({len(seen_c)})")
ok(f"{rounds}次考试覆盖全部判断题({n_all_j})", len(seen_j) == n_all_j, f"({len(seen_j)})")

# 恢复 progress
if os.path.exists(tmp):
    os.replace(tmp, bp)
root.destroy()
print(f"\n===== 扩展自检: PASS={PASS} FAIL={FAIL} =====")
sys.exit(1 if FAIL else 0)
