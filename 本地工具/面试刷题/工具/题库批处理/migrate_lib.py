# -*- coding: utf-8 -*-
"""题库内容改造库：把指定题目的「参考答案/参考译文」段替换为「题目解析 + 标准答案」。
（得分点、题干等其余内容一律不动；镜像解析器的边界规则，代码块内不切）"""
import re


def find_block_range(lines, num):
    in_code = False
    start = None
    for i, ln in enumerate(lines):
        s = ln.rstrip()
        if s.strip().startswith("```"):
            in_code = not in_code
        if in_code:
            continue
        if start is None:
            if re.match(rf"^\*\*{num}\.\*\*", s):
                start = i
        else:
            if s.strip() == "---" or s.startswith("#") or re.match(r"^\*\*\d+\.\*\*", s):
                return start, i
    return (start, len(lines)) if start is not None else (None, None)


def apply(PATH, NEW, dry=False):
    with open(PATH, encoding="utf-8") as f:
        lines = f.read().split("\n")
    fails, oks = [], []
    for num, pair in NEW.items():
        ana, std = pair
        start, end = find_block_range(lines, num)
        if start is None:
            fails.append((num, "未找到题块"))
            continue
        blk = lines[start:end]
        j_ans = next((i for i in range(len(blk))
                      if re.match(r"^\*\*参考(答案|译文)：\*\*", blk[i])), None)
        j_score = next((i for i in range(len(blk))
                        if re.match(r"^\*\*得分点：\*\*", blk[i])), None)
        if j_ans is None or j_score is None or j_score < j_ans:
            fails.append((num, f"标记异常 ans={j_ans} score={j_score}"))
            continue
        new_blk = blk[:j_ans] + [f"**题目解析：** {ana}", f"**标准答案：** {std}"] + blk[j_score:]
        lines[start:end] = new_blk
        oks.append(num)
    if fails:
        print("失败:", fails)
        return False
    if not dry:
        with open(PATH, "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
    print("成功改造", len(oks), "题:", oks)
    return True
