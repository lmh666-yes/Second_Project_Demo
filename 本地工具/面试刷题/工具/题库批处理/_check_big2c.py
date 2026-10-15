# -*- coding: utf-8 -*-
"""批2 终检：JSON 代码关键词抽查 + md 区间得分点数/异常行首检查。"""
import json, re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

data = json.load(open(r"D:\github\面试刷题\面试题刷题软件-大题\题库.json", encoding="utf-8"))
qmap = {}
for d in data:
    if isinstance(d, dict) and "answer" in d:
        qmap[d.get("num")] = d

keys = {401: ["DrawLine", "CircleCenter"], 402: ["module Serial2Parallel"], 403: ["SysSetBit"],
        405: ["m = m ^ n"], 406: ["printf"], 407: ["my_memcpy"], 409: ["bubble_sort"],
        410: ["EPRINTF"], 411: ["CalcOps"], 412: ["FsNode"], 413: ["TxRegistry"],
        414: ["LEAD("], 415: ["tr 'A-Z'"], 416: ["GetMemory"], 417: ["char *strcpy"],
        418: ["pTmp->next"], 421: ["Singleton"], 423: ["String::operator="], 424: ["LifeState"],
        425: ["dedup"], 426: ["sec2time"], 427: ["SET_LED"], 429: ["is_palindrome"],
        430: ["str_to_hex"], 431: ["swap_bytes"], 432: ["LED_REG"], 433: ["reverse_bits"],
        434: ["parity8"]}
bad = []
for n, ks in keys.items():
    a = qmap.get(n, {}).get("answer", "")
    for k in ks:
        if k not in a:
            bad.append((n, k))
print("[json] 代码关键词缺失:", bad if bad else "无")
print("[json] analysis 非空连续:", all(qmap[n]["analysis"] for n in qmap if 400 < n < 435))

md = open(r"D:\github\面试刷题\面试题刷题软件-大题\题库\03_编程画图大题.md", encoding="utf-8").read()
seg = md[md.index("**401.**"):md.index("**435.**")]
print("[md] 区间得分点行数 =", seg.count("**得分点：**"), "（预期 33）")
ok_pat = re.compile(r"^\*\*(\d+\.|题目解析：|标准答案：|得分点：|来源：|热点：)")
suspect = [ln.strip()[:60] for ln in seg.split("\n") if ln.startswith("**") and not ok_pat.match(ln)]
print("[md] 异常行首 ** 的行:", suspect if suspect else "无")
