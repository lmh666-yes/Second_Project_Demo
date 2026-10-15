# -*- coding: utf-8 -*-
"""验证 2026_2p0_q3 数据中 BIGQ #527~530 的 ```c 代码：gcc -Wall 编译并运行"""
import sys, io, os, re, subprocess
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, r"D:\github\面试刷题\工具\题库批处理")
from new_2026_2p0_q3 import BIGQ

TMP = r"D:\github\面试刷题\工具\题库批处理\_v527"
os.makedirs(TMP, exist_ok=True)
fail = 0
for num in sorted(BIGQ):
    ans = BIGQ[num]["ans"]
    blocks = re.findall(r"```c\n(.*?)```", ans, re.S)
    if not blocks:
        print(f"[{num}] 没有代码块！"); fail += 1; continue
    for bi, code in enumerate(blocks):
        src = os.path.join(TMP, f"q{num}.c")
        exe = os.path.join(TMP, f"q{num}.exe")
        open(src, "w", encoding="utf-8").write(code)
        cp = subprocess.run(["gcc", "-Wall", "-o", exe, src],
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
        if cp.returncode != 0 or cp.stderr.strip():
            print(f"[{num}] 编译问题：\n{cp.stderr}"); fail += 1; continue
        rp = subprocess.run([exe], capture_output=True, text=True, encoding="utf-8", errors="replace")
        out = rp.stdout.strip().replace("\n", " | ")
        print(f"[{num}] OK 输出：{out}")
print("==== 编译验证结束，问题数 =", fail, "====")
