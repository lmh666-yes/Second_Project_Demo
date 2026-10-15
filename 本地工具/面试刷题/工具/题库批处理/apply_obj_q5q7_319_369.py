# -*- coding: utf-8 -*-
"""把 附加面试题2.0·面试题5（恒扬 40 题 + 数控 6 题）与 面试题7（5 题）并入客观题库。

输入：素材\\附加面试题2.0\\_客观题批次_恒扬.py / _客观题批次_数控.py / _客观题批次_面试题7.py
输出：面试题刷题软件-客观题\\题库\\01_客观题_选择题.md（追加 319~369 并更新「附：统计」）

写法沿用仓库既有批次脚本：断言校验 → 追加 → 二进制写 UTF-8（无 BOM、LF）→ 回读校验。
用法：python apply_obj_q5q7_319_369.py
"""
import sys, io, os, importlib.util, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))            # 面试刷题/
DESK = os.path.join(ROOT, "面试题刷题软件-客观题")
BANK = os.path.join(DESK, "题库", "01_客观题_选择题.md")
DATA = [
    (os.path.join(ROOT, "素材", "附加面试题2.0", "_客观题批次_恒扬.py"), 319, 358),
    (os.path.join(ROOT, "素材", "附加面试题2.0", "_客观题批次_数控.py"), 359, 364),
    (os.path.join(ROOT, "素材", "附加面试题2.0", "_客观题批次_面试题7.py"), 365, 369),
]

SEC_TITLE = "### 2.6 附加面试题2.0·面试题5 / 面试题7（共 51 题，编号 319~369，2026-10-13）"
CUBE_NUM = 327                       # 恒扬选择题第 9 题：立方体展开推理，需配图
CUBE_IMG = "../imgs/fig-q5-cube.png"


def load(path):
    spec = importlib.util.spec_from_file_location("batch", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.ITEMS


def render(it):
    stem = it["stem"]
    if stem.startswith("```"):
        # 题干以代码块开头时必须换行，否则 ``` 会跟题号挤在同一行，
        # 解析器的围栏配对会错位、把后面所有内容吞进代码块
        head = "**%d.**\n\n%s" % (it["num"], stem)
    else:
        head = "**%d.** %s" % (it["num"], stem)
    out = [head, ""]
    for o in it["opts"]:
        out.append("- " + o)
    if it["opts"]:
        out.append("")
    src = it["source"]
    if it["num"] == CUBE_NUM:
        src = src + "（[图](%s)）" % CUBE_IMG
    out += ["**答案：%s**" % it["answer"],
            "**解析：** %s" % it["analysis"],
            "**来源：** %s" % src,
            "", "---", ""]
    return "\n".join(out)


def main():
    items = []
    for path, lo, hi in DATA:
        got = load(path)
        nums = [it["num"] for it in got]
        assert nums == list(range(lo, hi + 1)), (path, nums[:3], nums[-3:])
        items += got
    nums = [it["num"] for it in items]
    assert nums == list(range(319, 370)), nums
    for it in items:
        assert set(it) == {"num", "kind", "stem", "opts", "answer", "analysis", "source"}, it["num"]
        for k in ("stem", "answer", "analysis", "source"):
            assert it[k].strip(), (it["num"], k)
            assert "**" not in it[k], (it["num"], k)
        for o in it["opts"]:
            # 只有第 341 题（恒扬第 23 题）的 B 项原卷就是连续两个星号，按原样保留
            if "**" in o:
                assert it["num"] == 341 and o == "B. **a", (it["num"], o)
            assert not o.startswith("- "), it["num"]

    raw = io.open(BANK, "rb").read().decode("utf-8")
    assert raw[0] != "\ufeff" and "\r" not in raw
    assert "**318.**" in raw and "**319.**" not in raw
    assert SEC_TITLE not in raw
    assert "### 2.4 面试题6" in raw

    # 1) 末尾追加（文件当前以 318 的 --- 收尾）
    body = raw.rstrip("\n") + "\n\n" + SEC_TITLE + "\n\n" + "\n".join(render(it) for it in items)
    body = body.rstrip("\n") + "\n"

    # 2) 更新「附：统计」
    body, n1 = re.subn(r"- 总题量：\*\*\d+ 题\*\*（其中选择题 \d+ 题、判断题 \d+ 题、多选/双选 \d+ 题、特殊题 \d+ 题）",
                       "- 总题量：**367 题**（其中选择题 321 题、判断题 35 题、多选/双选 6 题、特殊题 5 题）",
                       body, count=1)
    assert n1 == 1, "总题量行未替换"

    old_scope = "；第 315~318 题为附加面试题2.0·面试题4（中山志源达翔 嵌入式工程师笔试题第 1~4 题，2026-10-13 新增）"
    new_scope = ("；第 315~318 题为附加面试题2.0·面试题4（中山志源达翔 嵌入式工程师笔试题第 1~4 题）；"
                 "第 319~358 题为附加面试题2.0·面试题5（深圳市恒扬数据 软件开发卷 选择题第 1~40 题）、"
                 "第 359~364 题为同卷「数控公司的面试题」选择题（原卷第 1、5 题与第 67、83 题为同题，已合并跳过）；"
                 "第 365~369 题为附加面试题2.0·面试题7 选择题第 1、2、3、5、6 题（原卷本身缺第 4 题）（以上 2026-10-13 新增）")
    assert old_scope in body
    body = body.replace(old_scope, new_scope, 1)

    old_susp = "- 标注“存疑/多种口径”的题目（第 92、112、140、150、213、316、317 题）"
    new_susp = "- 标注“存疑/多种口径”的题目（第 92、112、140、150、213、316、317、322、334、362 题）"
    assert old_susp in body
    body = body.replace(old_susp, new_susp, 1)

    old_dedup = "- **去重说明（2026-09-16 全库查重）**："
    new_dedup = ("- **去重说明（2026-10-13 追加批次）**：附加面试题2.0·面试题5「数控公司的面试题」选择题第 1 题与第 67 题、"
                 "第 5 题与第 83 题为同一道题，已合并跳过，编号不留空号（新题自 319 起连续顺排）。\n"
                 + old_dedup)
    assert old_dedup in body
    body = body.replace(old_dedup, new_dedup, 1)

    # 3) 给被合并的两条原题补来源
    body = body.replace("**来源：** 2024面试题-3 试卷三第 6 题",
                        "**来源：** 2024面试题-3 试卷三第 6 题（重复题：附加面试题2.0·面试题5 数控公司第 1 题，已合并）", 1)
    body = body.replace("**来源：** 2025面试题-3 卷1第 2 题（同卷：2025面试题-11）",
                        "**来源：** 2025面试题-3 卷1第 2 题（同卷：2025面试题-11）（重复题：附加面试题2.0·面试题5 数控公司第 5 题，已合并）", 1)
    body = body.replace("**来源：** 2024面试题-3 试卷三第 7 题",
                        "**来源：** 2024面试题-3 试卷三第 7 题（变体题：附加面试题2.0·面试题5 数控公司第 6 题，原卷漏印 if 后的「!」，两题互为变体，见第 362 题）", 1)

    io.open(BANK, "wb").write(body.encode("utf-8"))

    # 4) 回读校验
    chk = io.open(BANK, "rb").read()
    assert chk[:3] != b"\xef\xbb\xbf" and chk.count(b"\r") == 0
    t = chk.decode("utf-8")
    for n in (319, 327, 358, 359, 362, 365, 369):
        assert ("**%d.**" % n) in t, n
    assert t.count(SEC_TITLE) == 1
    assert t.count("**答案：") >= 367
    print("OK 写入 %d 字节，题号 319~369 共 %d 条" % (len(chk), len(items)))


if __name__ == "__main__":
    main()
