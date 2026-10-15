# -*- coding: utf-8 -*-
"""批2 二次自检：JSON(num 字段) + #418 上下文编译 + #410 宏展开运行测试。"""
import json, subprocess, os, sys, io, importlib.util
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

BASE = r"D:\github\面试刷题\工具\题库批处理\_check_big2"
os.makedirs(BASE, exist_ok=True)

# ---------- JSON（用 num 字段）----------
data = json.load(open(r"D:\github\面试刷题\面试题刷题软件-大题\题库.json", encoding="utf-8"))
def iter_dicts(o):
    if isinstance(o, dict):
        yield o
        for v in o.values():
            yield from iter_dicts(v)
    elif isinstance(o, list):
        for v in o:
            yield from iter_dicts(v)

cand = [d for d in iter_dicts(data) if "answer" in d]
qmap = {}
for d in cand:
    n = d.get("num")
    if isinstance(n, str) and n.isdigit():
        n = int(n)
    if isinstance(n, int):
        qmap[n] = d

nums = [401,402,403,404,405,406,407,408,409,410,411,412,413,414,415,416,417,418,419,420,421,
        423,424,425,426,427,428,429,430,431,432,433,434]
miss = []
for n in nums:
    d = qmap.get(n)
    if d is None:
        miss.append((n, "缺条目")); continue
    a, s = d.get("analysis", ""), d.get("answer", "")
    if not a or not s:
        miss.append((n, "空字段"))
    elif n in (401,402,403,405,406,407,409,410,411,412,413,416,417,418,421,423,424,425,426,427,429,430,431,432,433,434) and "```" not in s:
        miss.append((n, "answer 无代码"))
print("[json] 本批 33 题缺失/异常:", miss if miss else "无")

# ---------- #418：两行片段补上下文编译 ----------
ctx418 = '''/* 补上下文验证：两行插入语句的语法与语义 */
struct node { struct node *next; int v; };

void insert_after(struct node *p, struct node *pTmp)
{
    pTmp->next = p->next;    /* ① 先接 */
    p->next = pTmp;          /* ② 后断 */
}
int main(void) { return 0; }
'''
fp418 = os.path.join(BASE, "418_ctx.c")
open(fp418, "w", encoding="utf-8").write(ctx418)
r = subprocess.run(["gcc", "-std=gnu99", "-Wall", "-o", fp418 + ".exe", fp418],
                   capture_output=True, text=True, encoding="utf-8", errors="replace")
print("[418] 上下文编译:", "OK" if r.returncode == 0 else r.stderr.strip()[:300])

# ---------- #410：宏展开运行测试 ----------
spec = importlib.util.spec_from_file_location("bd", r"D:\github\面试刷题\工具\题库批处理\batch_big_2_data.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
import re
code410 = re.findall(r"```c\n(.*?)\n```", mod.NEW[410][1], re.S)[0]
test410 = ('#include <stdio.h>\n'
           'static const char *get_local_time(void) { return "10:00:00"; }\n'
           + code410 + '\n'
           'int main(void) {\n'
           '    EPRINTF(add, "%d", 1);\n'
           '    WHEN(1, printf("act\\n"));\n'
           '    return 0;\n'
           '}\n')
fp410 = os.path.join(BASE, "410_test.c")
open(fp410, "w", encoding="utf-8").write(test410)
r = subprocess.run(["gcc", "-std=gnu99", "-Wall", "-o", fp410 + ".exe", fp410],
                   capture_output=True, text=True, encoding="utf-8", errors="replace")
if r.returncode != 0:
    print("[410] 编译失败:", r.stderr.strip()[:400])
else:
    rr = subprocess.run([fp410 + ".exe"], capture_output=True, text=True,
                        encoding="utf-8", errors="replace", timeout=10)
    out = rr.stdout
    ok = ("add, 1" in out) and ("act" in out)
    print("[410] 运行输出:", out.strip()[:160], "| 判定:", "OK" if ok else "异常")
