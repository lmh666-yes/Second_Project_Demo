# -*- coding: utf-8 -*-
"""专项测试: 考试选择题选项随机化
验证: ①考试中选项顺序被打乱 ②答案跟随内容 ③两次考试顺序不同 ④非考试板块保持原序"""
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

# 找一道 choice 题(顺序模式第1题)
def start_exam():
    app.set_mode("考试")
    app._exam_start()
    root.update()

def first_choice():
    for q in app.queue:
        if q["kind"] == "choice":
            return q
    return None

def check_one_exam(q, oinfo):
    """对一次考试的第一道单选题做自校验, 返回其打乱后选项text列表"""
    src = q["options"]
    orig_texts = [o["text"] for o in src]
    orig_keys = [o["key"] for o in src]
    texts = [o["text"] for o in oinfo["options"]]
    keys = [o["key"] for o in oinfo["options"]]
    ok("选项内容完整(4项)", sorted(texts) == sorted(orig_texts), str(texts))
    # 答案跟随内容: 原答案内容的新位置 key 应等于打乱后 answer
    ans_text = next(o["text"] for o in src if o["key"] == str(q["answer"]).upper())
    new_key = next(o["key"] for o in oinfo["options"] if o["text"] == ans_text)
    ok("答案跟随内容", oinfo["answer"] == new_key,
       f"原答案={q['answer']} 内容={ans_text[:10]} 新答案={oinfo['answer']}")
    ok("key重新分配为ABCD", sorted(keys) == ["A", "B", "C", "D"], str(keys))
    return texts

def shuffled_ratio():
    """统计本场考试所有单选题的打乱率(至少一道被打乱则随机化生效)"""
    total = shuffled = 0
    for q in app.queue:
        if q["kind"] != "choice":
            continue
        oinfo = app.exam.get("opts", {}).get(q["id"])
        if not oinfo:
            continue
        total += 1
        orig = [o["text"] for o in q["options"]]
        now = [o["text"] for o in oinfo["options"]]
        if now != orig:
            shuffled += 1
    return total, shuffled

# 考试1
start_exam()
q1 = first_choice()
ok("考试队列含单选题", q1 is not None)
o1 = app.exam.get("opts", {}).get(q1["id"])
ok("考试有打乱选项", o1 is not None)
texts1 = check_one_exam(q1, o1) if o1 else []
t1, s1 = shuffled_ratio()
ok("全卷单选已随机打乱(至少1道变化)", t1 >= 1 and s1 >= 1, f"打乱 {s1}/{t1}")
# 用打乱后选项作答, 答案正确应判对
app.choice_var.set(o1["answer"])
app._exam_answer(q1, o1["answer"])
root.update()
ok("选正确答案内容判对", app.exam["answers"][q1["id"]] == o1["answer"])
app.finish_exam()
root.update()

# 考试2: 选项顺序应再次随机(大概率不同)
start_exam()
q2 = first_choice()
o2 = app.exam.get("opts", {}).get(q2["id"])
texts2 = [o["text"] for o in o2["options"]]
t2, s2 = shuffled_ratio()
ok("两次考试选项顺序不同(随机)",
   texts2 != texts1 or texts2 != [o["text"] for o in q2["options"]],
   f"e1={texts1[:2]} e2={texts2[:2]} · 两场打乱 {s1}/{t1} 与 {s2}/{t2}")
app.finish_exam()
root.update()

# 判断题: 不受打乱影响
start_exam()
judge_q = next((qq for qq in app.queue if qq["kind"] == "judge"), None)
ok("考试队列含判断题", judge_q is not None)
if judge_q:
    ok("判断题无打乱选项", app.exam.get("opts", {}).get(judge_q["id"]) is None)
    app.answer_judge("√")
    app._exam_answer(judge_q, "√")
    root.update()
    ok("判断题正常判分", app.exam["answers"][judge_q["id"]] == "√")
app.finish_exam()
root.update()

# 非考试板块(顺序)保持原序
app.set_mode("顺序")
app.idx = 0
app.show_question()
root.update()
ok("顺序板块选项原序", [o["key"] for o in app.queue[0]["options"]] == ["A", "B", "C", "D"])

root.destroy()
print(f"\n===== 选项随机化测试: PASS={P} FAIL={F} =====")
sys.exit(1 if F else 0)
