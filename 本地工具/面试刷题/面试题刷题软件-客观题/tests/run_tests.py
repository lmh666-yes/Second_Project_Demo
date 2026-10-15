# -*- coding: utf-8 -*-
"""运行指定测试脚本并把输出写到文件(规避终端回显问题)"""
import subprocess, sys, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
for t in sys.argv[1:]:
    out = open(t + ".run.txt", "w", encoding="utf-8")
    try:
        r = subprocess.run([sys.executable, "-u", t], capture_output=True,
                           text=True, encoding="utf-8", errors="replace",
                           timeout=300)
        out.write(r.stdout)
        out.write("\n---STDERR---\n")
        out.write(r.stderr[-4000:])
        out.write(f"\n---EXIT {r.returncode}---\n")
    except subprocess.TimeoutExpired as e:
        out.write("---TIMEOUT---\n")
        out.write((e.stdout or ""))
        out.write("\n---STDERR---\n")
        out.write((e.stderr or "")[-4000:])
    finally:
        out.close()
    print(t, "done")
