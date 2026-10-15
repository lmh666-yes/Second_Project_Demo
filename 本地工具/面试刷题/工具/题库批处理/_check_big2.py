# -*- coding: utf-8 -*-
"""批2（大题 401~434）自检：md 区间格式抽查 + JSON 字段抽查 + 代码编译/运行验证。"""
import importlib.util, os, re, subprocess, sys, io, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

BASE = r"D:\github\面试刷题\工具\题库批处理\_check_big2"
MD = r"D:\github\面试刷题\面试题刷题软件-大题\题库\03_编程画图大题.md"
JSON = r"D:\github\面试刷题\面试题刷题软件-大题\题库.json"

# ---------- 1) md 区间抽查 ----------
md = open(MD, encoding="utf-8").read()
seg = md[md.index("**401.**"):md.index("**435.**")]
print("[md] 区间内 题目解析=%d 标准答案=%d 参考答案残留=%d"
      % (seg.count("**题目解析：**"), seg.count("**标准答案：**"), seg.count("**参考答案：**")))

# ---------- 2) JSON 抽查 ----------
data = json.load(open(JSON, encoding="utf-8"))
def iter_dicts(o):
    if isinstance(o, dict):
        yield o
        for v in o.values():
            yield from iter_dicts(v)
    elif isinstance(o, list):
        for v in o:
            yield from iter_dicts(v)

cand = [d for d in iter_dicts(data) if "answer" in d]
print("[json] 含 answer 条目数:", len(cand), "| 样例键:", sorted(cand[0].keys())[:12] if cand else "-")
qmap = {}
for d in cand:
    qid = d.get("id")
    if isinstance(qid, str) and qid.isdigit():
        qid = int(qid)
    if isinstance(qid, int):
        qmap[qid] = d
miss = []
for n in [401,402,403,404,405,406,407,408,409,410,411,412,413,414,415,416,417,418,419,420,421,423,424,425,426,427,428,429,430,431,432,433,434]:
    d = qmap.get(n)
    if d is None:
        miss.append((n, "缺条目"))
        continue
    a, s = d.get("analysis", ""), d.get("answer", "")
    if not a or not s:
        miss.append((n, "空字段"))
    elif n in (401,402,403,405,406,407,409,410,411,412,413,416,417,418,421,423,424,425,426,427,429,430,431,432,433,434) and "```" not in s:
        miss.append((n, "answer 无代码"))
print("[json] 本批 33 题缺失/异常:", miss if miss else "无", "| 命中 qmap:", len([n for n in qmap if 400 < n < 435]))

# ---------- 3) 代码编译/运行验证 ----------
spec = importlib.util.spec_from_file_location("bd", r"D:\github\面试刷题\工具\题库批处理\batch_big_2_data.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
os.makedirs(BASE, exist_ok=True)

run_cases = {405: None, 406: None, 409: "3\n3 1 2\n", 411: None, 416: None,
             425: "abcaadc\n", 426: "0\n", 429: "12321\n", 430: "1234\n",
             431: None, 433: None, 434: None}
expect = {405: ["m = 3, n = 2"], 406: ["1*1=1", "9*9=81"], 409: ["1 2 3"],
          411: ["7", "-1"], 416: ["Anyka is a hi-tech company."], 425: ["abcd"],
          426: ["2000年01月01日 00时00分00秒"], 429: ["是对称数"],
          430: ["0x12", "0x34"], 431: ["0x3412"], 433: ["0x2C48"],
          434: ["0x0F -> 0", "0x07 -> 1"]}

compile_fail, run_fail, skipped, ran_ok = [], [], [], []
for n, (ana, ans) in sorted(mod.NEW.items()):
    for k, (lang, code) in enumerate(re.findall(r"```([a-zA-Z]*)\n(.*?)\n```", ans, re.S)):
        lang = lang.lower()
        if lang == "c":
            fp = os.path.join(BASE, "%d_%d.c" % (n, k))
            open(fp, "w", encoding="utf-8").write(code + "\n")
            if n in run_cases and k == 0 and "main" in code:
                exe = fp + ".exe"
                r = subprocess.run(["gcc", "-std=gnu99", "-Wall", "-o", exe, fp],
                                   capture_output=True, text=True, encoding="utf-8", errors="replace")
                if r.returncode != 0:
                    compile_fail.append((n, k, r.stderr.strip()[:400]))
                else:
                    rr = subprocess.run([exe], input=run_cases[n], capture_output=True,
                                        text=True, encoding="utf-8", errors="replace", timeout=10)
                    out = rr.stdout
                    bad = [s for s in expect.get(n, []) if s not in out]
                    if bad:
                        run_fail.append((n, bad, out[:200]))
                    else:
                        ran_ok.append(n)
            else:
                r = subprocess.run(["gcc", "-std=gnu99", "-Wall", "-c", "-o", fp + ".o", fp],
                                   capture_output=True, text=True, encoding="utf-8", errors="replace")
                if r.returncode != 0:
                    compile_fail.append((n, k, r.stderr.strip()[:400]))
        elif lang == "cpp":
            fp = os.path.join(BASE, "%d_%d.cpp" % (n, k))
            open(fp, "w", encoding="utf-8").write(code + "\n")
            r = subprocess.run(["g++", "-std=c++11", "-Wall", "-c", "-o", fp + ".o", fp],
                               capture_output=True, text=True, encoding="utf-8", errors="replace")
            if r.returncode != 0:
                compile_fail.append((n, k, r.stderr.strip()[:400]))
        else:
            skipped.append((n, k, lang or "(无标注)"))

print("[gcc] 编译失败:", compile_fail if compile_fail else "无")
print("[gcc] 运行失败:", run_fail if run_fail else "无", "| 运行通过:", ran_ok)
print("[gcc] 跳过(非 C/C++):", skipped)
