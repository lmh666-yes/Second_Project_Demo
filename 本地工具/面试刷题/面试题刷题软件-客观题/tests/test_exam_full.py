# -*- coding: utf-8 -*-
"""考试逻辑 + 题库数据全面自检（V2 环形算法版）：
1) 题库完整性：答案 key 有效性 / 判断题答案格式 / 解析覆盖 / 图片文件存在
2) 抽题：600 场模拟——单场无重复、题数构成、均匀性（次数差/最大间隔/连续 N 场覆盖率，阈值随题库规模自适应）
3) 选项乱序：内容一一对应、答案跟随正确、多次洗牌后位置全覆盖
4) 队列顺序：保持抽题打乱顺序
5) 计划持久化与题库变更重置
报告写入 test_exam_full.report.txt（utf-8）
"""
import sys, os, json, random, statistics, importlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "core"))
APP = importlib.import_module("刷题软件")

LOG = open(os.path.join(HERE, "test_exam_full.report.txt"), "w", encoding="utf-8")
def out(*a):
    print(*a)
    print(*a, file=LOG)

FAIL, OK = [], []
def check(cond, msg):
    (OK if cond else FAIL).append(msg)

bank = json.load(open(os.path.join(ROOT, "题库.json"), encoding="utf-8"))
check(isinstance(bank, list) and len(bank) > 0, f"[1] 题库加载：{len(bank)} 题")

# ---------- 1. 数据完整性 ----------
ids = [it.get("id") for it in bank]
check(len(set(ids)) == len(ids), f"[1] id 唯一（{len(ids)} 个）")
kinds = {}
for it in bank:
    kinds[it.get("kind")] = kinds.get(it.get("kind"), 0) + 1
out("题型分布:", kinds)

bad_ans, no_explain, no_img = [], [], []
for it in bank:
    k = it.get("kind")
    ans = str(it.get("answer", "")).strip()
    if k in ("choice", "multi"):
        opts = it.get("options") or []
        keys = [str(o.get("key", "")).upper() for o in opts]
        letters = [c for c in ans.upper() if c.isalpha()]
        if not letters or any(c not in keys for c in letters):
            bad_ans.append(it.get("id") + ":" + ans)
        if k == "choice" and len(letters) != 1:
            bad_ans.append(it.get("id") + "(单选非单答案:" + ans + ")")
        if k == "multi" and len(letters) < 2:
            bad_ans.append(it.get("id") + "(多选答案<2:" + ans + ")")
    else:
        if not ans:
            bad_ans.append(it.get("id") + "(答案为空)")
    if not it.get("explain"):
        no_explain.append(it.get("id"))
    for name in (it.get("imgs") or []):
        if not os.path.exists(os.path.join(ROOT, "imgs", name)):
            no_img.append((it.get("id"), name))
check(not bad_ans, f"[1] 答案格式全部有效（异常 {len(bad_ans)} 题：{bad_ans[:8]}）")
check(not no_img, f"[1] 配图文件全部存在（缺失 {len(no_img)}）")
jset = set(str(it.get("answer", "")).strip() for it in bank if it.get("kind") == "judge")
out("判断题答案取值:", sorted(jset))
out(f"解析缺失 {len(no_explain)} 题" + (f"（如 {no_explain[:5]}）" if no_explain else ""))

# ---------- 2. 抽题模拟 ----------
progress = {}
ch_set = set(it["id"] for it in bank if it.get("kind") in ("choice", "multi"))
j_set = set(it["id"] for it in bank if it.get("kind") == "judge")
N = 600
# 阈值随题库规模自适应：把选择题全部轮到一遍需要的场数
COVER_EXAMS = max(1, -(-len(ch_set) // APP.EXAM_CHOICE_NUM))       # 向上取整
MAX_GAP = COVER_EXAMS + 1
dup_in_exam, wrong_len = [], []
ch_counts, j_counts, last_seen, c_intervals = {}, {}, {}, []
window_c = []
min_cov_win = 10 ** 9
for t in range(N):
    picked = APP.exam_pick_ids(bank, progress)
    c = [i for i in picked if i in ch_set]
    j = [i for i in picked if i in j_set]
    if len(picked) != len(set(picked)):
        dup_in_exam.append(t)
    if len(c) != APP.EXAM_CHOICE_NUM or len(j) != APP.EXAM_JUDGE_NUM:
        wrong_len.append(t)
    for qid in picked:
        if qid in last_seen and qid in ch_set:
            c_intervals.append(t - last_seen[qid])
        last_seen[qid] = t
        if qid in ch_set:
            ch_counts[qid] = ch_counts.get(qid, 0) + 1
        else:
            j_counts[qid] = j_counts.get(qid, 0) + 1
    window_c.append(set(c))
    if len(window_c) >= COVER_EXAMS:
        cov = set().union(*window_c[-COVER_EXAMS:])
        min_cov_win = min(min_cov_win, len(cov))
check(not dup_in_exam, f"[2] {N} 场考试单场题无重复（异常 {len(dup_in_exam)} 场）")
check(not wrong_len, f"[2] 每场题数构成正确：{APP.EXAM_CHOICE_NUM} 选择 + {APP.EXAM_JUDGE_NUM} 判断")
cc = sorted(ch_counts.values())
jj = sorted(j_counts.values())
check(cc[-1] - cc[0] <= 2, f"[2] 选择题出现次数均匀：最少 {cc[0]} / 最多 {cc[-1]}（差 ≤2）")
check(jj[-1] - jj[0] <= 4, f"[2] 判断题出现次数均匀：最少 {jj[0]} / 最多 {jj[-1]}（差 ≤4）")
check(max(c_intervals) <= MAX_GAP, f"[2] 选择题相邻出现最大间隔 {max(c_intervals)} 场（≤{MAX_GAP}，随题库 {len(ch_set)} 题自适应）")
check(min_cov_win == len(ch_set), f"[2] 任意连续 {COVER_EXAMS} 场选择题全覆盖：{min_cov_win}/{len(ch_set)}")
out(f"选择题 {len(ch_set)} 个 · 判断题 {len(j_set)} 个")
out(f"{N} 场中选择题出现次数：最少 {cc[0]} 次 最多 {cc[-1]} 次；判断题：最少 {jj[0]} 次 最多 {jj[-1]} 次")
out(f"选择题相邻出现间隔：平均 {statistics.mean(c_intervals):.2f} 场 · 最大 {max(c_intervals)} 场")
out(f"理论轮次：选择题每 {len(ch_set)/APP.EXAM_CHOICE_NUM:.1f} 场一轮，判断每 {len(j_set)/APP.EXAM_JUDGE_NUM:.1f} 场一轮")

# ---------- 3. 选项乱序 ----------
random.seed(42)
sample = [it for it in bank if it.get("kind") in ("choice", "multi")]
opt_bad, pos_total = [], {}
for it in sample:
    src = it["options"]
    before = {str(o.get("key", "")).upper(): o.get("text") for o in src}
    ans_before = set(c for c in str(it.get("answer", "")).upper() if c.isalpha())
    ans_texts = set(before[k] for k in before if k in ans_before)
    per_pos = set()
    for _ in range(60):
        oinf = APP.build_exam_opts([it])[it["id"]]
        after = {str(o.get("key", "")).upper(): o.get("text") for o in oinf["options"]}
        if sorted(after.values()) != sorted(before.values()):
            opt_bad.append(it["id"] + "(选项内容变化)")
            break
        ans_after = set(oinf["answer"])
        if set(after[k] for k in ans_after) != ans_texts:
            opt_bad.append(it["id"] + "(答案未跟随内容)")
            break
        per_pos |= ans_after
        for k in ans_after:
            pos_total[k] = pos_total.get(k, 0) + 1
    if per_pos != set(before.keys()):
        opt_bad.append(it["id"] + "(答案位置未覆盖全部选项)")
check(not opt_bad, f"[3] 全部 {len(sample)} 道选择题选项乱序验证通过（异常 {len(opt_bad)}）")
out("全样本洗牌后正确答案落位总分布:", dict(sorted(pos_total.items())))

# ---------- 4. 队列顺序 ----------
bad_order = 0
for _ in range(100):
    picked = APP.exam_pick_ids(bank, progress)
    q = APP.build_exam_queue(bank, picked)
    if [x["id"] for x in q] != picked:
        bad_order += 1
check(bad_order == 0, "[4] 考试队列严格保持抽题时的打乱顺序")

# ---------- 5. 计划持久化 ----------
p2 = {}
APP.exam_pick_ids(bank, p2)
has_plan = isinstance(p2.get("_exam_plan"), dict) and isinstance(p2["_exam_plan"].get("c"), list)
fake = [it for it in bank if it.get("kind") in ("choice", "multi")][:10] + \
       [it for it in bank if it.get("kind") == "judge"][:10]
p3 = {"_exam_plan": {"sig": "old", "c": [], "cc": 999}}
APP.exam_pick_ids(fake, p3)
sig_new = p3["_exam_plan"].get("sig", "")
check(has_plan, "[5] 抽题计划已持久化到 progress（跨考试不重复）")
check(sig_new != "old", "[5] 题库变化时计划签名自动重置")

out("")
out("==== 结果 ====")
for m in OK:
    out("[OK]", m)
for m in FAIL:
    out("[FAIL]", m)
out("全部通过" if not FAIL else "存在失败项")

LOG.close()
sys.exit(1 if FAIL else 0)
