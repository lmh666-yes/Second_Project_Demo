# -*- coding: utf-8 -*-
"""侦察客观题题库文件：行尾格式 / 末尾内容 / 待替换文本 / 编号与标签统计"""
P = r"D:\github\面试刷题\面试题刷题软件-客观题\题库\01_客观题_选择题.md"
out = []
data = open(P, 'rb').read()
crlf = data.count(b'\r\n'); lf = data.count(b'\n'); cr = data.count(b'\r')
out.append(f"bytes={len(data)} CRLF={crlf} LF={lf} CR={cr}")
txt = data.decode('utf-8')
out.append("TAIL_REPR=" + repr(txt[-260:]))
t1 = "- 总题量：**292 题**（其中选择题 250 题、判断题 35 题、多选/双选 5 题、特殊题 2 题）"
t2a = "  - 第 287 题由原 `02_主观题.md` 第 164 题迁入（题干自带 ABCD 选项，属选择题）。"
t2b = "- 第 287 题由原 `02_主观题.md` 第 164 题迁入（题干自带 ABCD 选项，属选择题）。"
out.append(f"t1={txt.count(t1)}  t2a(2space)={txt.count(t2a)}  t2b(0space)={txt.count(t2b)}")
for n in (293, 294, 295, 296, 300, 310, 314):
    out.append(f"num {n} count={txt.count(f'**{n}.**')}")
for tag in ("（基础题）", "（中等难度）", "（较难）", "（困难）", "（易）", "（易错）"):
    out.append(f"tag {tag} = {txt.count(tag)}")
out.append("HEAD_REPR=" + repr(txt[:160]))
open(r"D:\github\面试刷题\工具\题库批处理\_check_obj_out.txt", "w", encoding="utf-8").write("\n".join(out))
print("check done")
