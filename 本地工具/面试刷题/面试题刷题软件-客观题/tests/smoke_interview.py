# -*- coding: utf-8 -*-
"""面试题库版冒烟测试：题库加载 / 单选·多选·判断判分 / 多选交互 /
qa 题展示 / 题型标注 / 考试计划式抽题覆盖 / 错题本 / 考试多选判分。

用法：python smoke_interview.py
"""
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

# 备份用户进度，测试结束恢复
bp = bs.PROG_PATH
had = pg.backup(bp)

bs.messagebox.showinfo = lambda t, m: None
bs.messagebox.showerror = lambda t, m: None
bs.messagebox.showwarning = lambda t, m: None
bs.messagebox.askyesno = lambda t, m: True

root = tk.Tk()
root.withdraw()
app = bs.App(root)
root.update()

# 1. 题库
kinds = {}
for it in app.bank:
    kinds[it["kind"]] = kinds.get(it["kind"], 0) + 1
check(len(app.bank) == 367, f"题库 367 题（实际 {len(app.bank)}）")
check(kinds == {"choice": 321, "multi": 6, "judge": 35, "qa": 5}, f"题型分布 {kinds}")

# 2. 单选判分
app.set_mode("顺序")
root.update()
c = app._by_id("choice-1")
app.queue = [c]; app.idx = 0; app.show_question()
root.update()
app.choice_var.set("A" if c["answer"] != "A" else "B")
app.check()
root.update()
check(app.progress.get("choice-1", {}).get("ok") is False, "单选答错记为错误")
app.choice_var.set(c["answer"])
app.check()
root.update()
check(app.progress.get("choice-1", {}).get("ok") is True, "单选答对记为正确")

# 3. 多选题交互 + 判分
m = app._by_id("multi-72")        # 答案 BC
app.queue = [m]; app.idx = 0; app.show_question()
root.update()
check("多选题" in app.head_label.cget("text"), "题干上方标注「多选题」")
app._select_option("B")
app._select_option("C")
root.update()
check(app.choice_var.get() == "BC", f"多选可同时选中两项（实际 {app.choice_var.get()!r}）")
app._select_option("B")           # 再点一次取消
check(app.choice_var.get() == "C", f"再点取消选中（实际 {app.choice_var.get()!r}）")
app._select_option("B")
app.check()
root.update()
check(app.progress.get("multi-72", {}).get("ok") is True, "多选 BC 判为正确")
app.choice_var.set("B")
app.check()
root.update()
check(app.progress.get("multi-72", {}).get("ok") is False, "多选漏选判为错误")
app.choice_var.set("CB")          # 乱序也应对
app.check()
root.update()
check(app.progress.get("multi-72", {}).get("ok") is True, "多选乱序 CB 判为正确")
app.choice_var.set("B")           # 漏选，保留为错误状态供错题本检查
app.check()
root.update()
check(app.progress.get("multi-72", {}).get("ok") is False, "多选漏选保持为错误状态")

# 4. 判断题
j = app._by_id("judge-159")
app.queue = [j]; app.idx = 0; app.show_question()
root.update()
app.choice_var.set("×" if j["answer"] == "√" else "√")
app.check()
root.update()
check(app.progress.get(j["id"], {}).get("ok") is False, "判断答错")
app.choice_var.set(j["answer"])
app.check()
root.update()
check(app.progress.get(j["id"], {}).get("ok") is True, "判断答对")

# 5. qa 特殊题（112：有选项、存疑）
q = app._by_id("qa-112")
app.queue = [q]; app.idx = 0; app.show_question()
root.update()
app.reveal_qa()
root.update()
fb = app.fb.get("1.0", "end")
check("存疑" in fb or "答案" in fb, "qa 题查看答案有内容")
check("本题选项" in " ".join(str(w.cget("text")) for w in app.opt_frame.winfo_children()
                          if w.winfo_class() == "Label"), "qa 题展示原卷选项")

# 6. 错题板块包含答错的题
app.set_mode("错题")
root.update()
wrong_ids = {it["id"] for it in app.queue}
check("multi-72" in wrong_ids, f"错题本收录答错题（{len(wrong_ids)} 题）")
# 6b. 新规则：顺序答对不清除错题；底部有错题考试入口
check(any("错题考试" in t for _, t in app.nav_btns), "底部有「错题考试」按钮")
app.set_mode("顺序")
app.queue = [m]
app.idx = 0
app.show_question()
app.choice_var.set("BC")
app.check()
root.update()
check(app.progress["multi-72"].get("in_wrong") is True, "顺序答对不清除错题（新规则）")
check(app.progress["multi-72"].get("life", 0) >= 1,
      f"错题寿命记录（life={app.progress['multi-72'].get('life')}）")

# 7. 考试：覆盖全部题目 + 多选判分（用打乱后答案）
#    环形算法保证连续若干场必覆盖全部题：轮数 = ceil(可抽题目数/20) + 3 富余
app.progress.pop("_exam_plan", None)
app.set_mode("考试")
root.update()
covered = set()
multi_hit = False
all_c = {it["id"] for it in app.bank if it["kind"] in ("choice", "multi")}
all_j = {it["id"] for it in app.bank if it["kind"] == "judge"}
rounds = (len(all_c | all_j) + 19) // 20 + 3
for i in range(rounds):
    app._exam_start()
    root.update()
    check_ids = [it["id"] for it in app.queue]
    covered.update(check_ids)
    if any(it["kind"] == "multi" for it in app.queue):
        multi_hit = True
    app.finish_exam()
    root.update()
check(all_c <= covered, f"{rounds} 次考试覆盖全部选择题 {len(all_c)} 道（覆盖 {len(all_c & covered)}）")
check(all_j <= covered, f"{rounds} 次考试覆盖全部判断题 {len(all_j)} 道（覆盖 {len(all_j & covered)}）")
check(multi_hit, "考试题目中抽到过多选题")

# 8. 考试多选：乱序选项后判分正确
app.exam = {"active": True, "finished": False, "paused": False, "ids": [], "idx": 0,
            "left": 600, "answers": {}}
app.queue = [m]
oinfo = bs.build_exam_opts(app.queue)["multi-72"]
app.exam["opts"] = {"multi-72": oinfo}
ok = app._exam_is_ok(m, oinfo["answer"])
bad = app._exam_is_ok(m, oinfo["answer"][:1])
check(ok, f"考试多选打乱后按新答案判对（新答案 {oinfo['answer']}）")
check(not bad, "考试多选漏选判错")

# 9. 第 13 次考试进入新一轮不崩溃
app._exam_start()
root.update()
check(True, "新一轮考试正常启动")
app.finish_exam()

# 10. 进度记忆：跳到"连续完成题目的最后一题"（1、2、3、4 做了，跳过 5 做了 6、7、8 → 回到第 4 题）
def simulate_reopen():
    """模拟重新打开软件：尚无会话位置（作答记录来自 progress.json）"""
    app._seq_saved_idx = None

app.progress = {}
for i in range(4):
    app.progress[app.bank[i]["id"]] = {"ok": True, "wrong_count": 0, "notes": ""}
for i in (5, 6, 7):
    app.progress[app.bank[i]["id"]] = {"ok": False, "wrong_count": 1, "notes": ""}
simulate_reopen()
app.set_mode("顺序")
root.update()
check(app.idx == 3, f"记忆位置=连续完成的最后一题（做了 1~4 + 6,7,8 → 第 {app.idx + 1} 题，应为第 4 题）")
check("已回到上次进度" in app.head_label.cget("text"), "题头提示已回到上次进度")
# 答错的题也算做过
app.progress = {}
app.progress[app.bank[0]["id"]] = {"ok": False, "wrong_count": 1, "notes": ""}
simulate_reopen()
app.set_mode("顺序")
root.update()
check(app.idx == 0, "答错也算完成（回到第 1 题）")
# 没做过任何题 → 回到第 1 题
app.progress = {}
simulate_reopen()
app.set_mode("顺序")
root.update()
check(app.idx == 0, "无记录时回到第 1 题")
# 切到错题/考试板块再回顺序板块 → 回到离开时的位置
simulate_reopen()
app.set_mode("顺序")
app.idx = 15
app.set_mode("考试")
app.set_mode("顺序")
root.update()
check(app.idx == 15, f"切板块返回仍是离开时的位置（第 {app.idx + 1} 题）")

root.destroy()

# 恢复用户进度
pg.restore()
if not had and os.path.exists(bp):
    os.remove(bp)

print()
print(f"结果：{'全部通过 ✔' if fails == 0 else f'{fails} 项失败 ✘'}")
sys.exit(1 if fails else 0)
