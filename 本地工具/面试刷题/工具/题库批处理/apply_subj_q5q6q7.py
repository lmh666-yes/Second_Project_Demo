# -*- coding: utf-8 -*-
"""把《附加面试题2.0·面试题5/6/7》的主观题与大题数据文件插入题库。

- 主观题写入 面试题刷题软件-主观题/题库/02_主观题.md（新号从 504 起）
- 大题写入 面试题刷题软件-大题/题库/03_编程画图大题.md 与 面试题刷题软件-主观题/题库/03_编程画图大题.md（两份必须字节一致，新号从 532 起）
数据文件：素材/附加面试题2.0/_主观_*.py、_大题_*.py，每个含 ITEMS / DUPS / FIGURES 三个列表。
只插库、只改 md；不跑建库、不跑测试（那两步单独做）。
"""
import glob
import importlib.util
import io
import os
import re
import sys

BASE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# 本文件在 <仓库>/本地工具/面试刷题/工具/题库批处理/ 下，上溯三层即"面试刷题"目录。
# （原来写死 D:\GitHub_Project\...，换机器/换目录就跑不起来。）
MAT = os.path.join(BASE, "素材", "附加面试题2.0")
SUBJ = os.path.join(BASE, "面试题刷题软件-主观题", "题库", "02_主观题.md")
BIG_A = os.path.join(BASE, "面试题刷题软件-大题", "题库", "03_编程画图大题.md")
BIG_B = os.path.join(BASE, "面试题刷题软件-主观题", "题库", "03_编程画图大题.md")
SUBJ_START = 504
BIG_START = 532
SUBJ_TITLE = "### 附加面试题2.0 · 面试题5 / 面试题6 / 面试题7（主观题 {n} 题，编号 {a}~{b}，2026-10-13）"
BIG_TITLE = "## 八、附加面试题2.0 · 面试题5 / 面试题6 / 面试题7（编程 / 画图 {n} 题，编号 {a}~{b}）"
REQUIRED = ("kind", "stem", "analysis", "answer", "points", "hot", "source")


def load(path):
    spec = importlib.util.spec_from_file_location(os.path.basename(path)[:-3], path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def check_item(it, where):
    for k in REQUIRED:
        assert k in it, "%s 缺字段 %s" % (where, k)
    assert it["kind"] in ("主观", "大题"), "%s kind 非法：%r" % (where, it["kind"])
    for k in ("stem", "analysis", "answer", "points", "source"):
        v = it.get(k) or ""
        assert isinstance(v, str) and v.strip(), "%s 字段 %s 为空" % (where, k)
        assert "**" not in v, "%s 字段 %s 含连续星号" % (where, k)
    assert it["stem"].count("```") % 2 == 0, "%s 题干代码围栏不成对" % where
    assert it["answer"].count("```") % 2 == 0, "%s 答案代码围栏不成对" % where


def render(num, it, eol="\n"):
    out = ["**%d.** %s" % (num, it["stem"]), ""]
    out.append("**题目解析：** %s" % it["analysis"])
    out.append("**标准答案：** %s" % it["answer"])
    out.append("**得分点：** %s" % it["points"])
    if it.get("hot"):
        out.append("**热点：** 是")
    out.append("**来源：** %s" % it["source"])
    for path in re.findall(r"\[[^\]]*\]\((\.\./imgs/[^)]+)\)", it["source"]):
        out.append("![配图](%s)" % path)   # 03 库既有约定：来源行后紧跟配图行
    out.append("")
    out.append("---")
    out.append("")
    return eol.join(out)


def insert_before(text, anchor, chunk):
    assert text.count(anchor) == 1, "锚点 %r 命中 %d 次" % (anchor, text.count(anchor))
    return text.replace(anchor, chunk + anchor)


def read_text(path):
    """保留原文换行符读取（newline="" 不做 CRLF->LF 转换）。"""
    with io.open(path, "r", encoding="utf-8", newline="") as fh:
        return fh.read()


def eol_of(text):
    crlf = text.count("\r\n")
    lf = text.count("\n") - crlf
    return "\r\n" if crlf >= lf else "\n"


def write_text(path, text):
    with io.open(path, "wb") as fh:
        fh.write(text.encode("utf-8"))


def is_cross_bank(d):
    """existing_bank 明确写了 01/客观 的，属跨库重复，不在 02/03 里找题号打标。"""
    bank = str(d.get("existing_bank") or "")
    return ("01" in bank) or ("客观" in bank)


def apply_source_merges(items, dups):
    """处理 existing_after：把「同 <source>，重复题已合并」追到指定题目的来源行（先改数据再渲染）。"""
    done = []
    for d in dups:
        key = d.get("existing_after")
        if not key:
            continue
        hit = [it for it in items if key in it["source"]]
        assert hit, "existing_after 未命中任何题目：%r" % key
        for it in hit:
            if "已合并" in it["source"]:
                continue
            it["source"] = it["source"] + "（同 %s，重复题已合并）" % d["source"]
            done.append((key, it["source"]))
    return done


def dup_num(d):
    """existing 可能是 411、\"02_主观题 #11\" 这类写法，统一取出题号。"""
    raw = str(d.get("existing") or d.get("existing_num") or "")
    m = re.search(r"#\s*(\d+)", raw) or re.search(r"(\d+)", raw)
    assert m, "无法解析 existing=%r" % raw
    return int(m.group(1))


def _norm(s):
    return re.sub(r"[\s`（）()。，,.、:：;；!！?？]+", "", s or "")


def _item_body(lines, start):
    body = [lines[start]]   # 题干与题号同行，必须把它算进来
    for j in range(start + 1, min(start + 120, len(lines))):
        if re.match(r"\*\*\d+\.\*\*", lines[j]):
            break
        body.append(lines[j])
    return "".join(body)


def apply_dups(text, dups, bank_tag):
    """按行处理，保留每行原有行尾；找不到或已标注的题跳过。

    命中题号后还会用 existing_stem 校验题干是否真是同一道题（02/03 两个库题号空间重叠，
    光看题号会把另一个库里的同号题改错），校验不过就跳过。
    """
    lines = text.splitlines(True)
    applied = []
    for d in dups:
        if not (d.get("existing") or d.get("existing_num")):
            continue   # 只做 existing_after 互标的条目，不在库里打标
        num = dup_num(d)
        head = "**%d.**" % num
        start = None
        for i, ln in enumerate(lines):
            if ln.startswith(head):
                start = i
                break
        if start is None:
            applied.append((num, "找不到第 %d 题，跳过" % num))
            continue
        want = _norm(d.get("existing_stem") or d.get("stem") or "")
        body = _norm(_item_body(lines, start))
        if want and body and len(want) >= 6:
            probe = want[:16]
            if probe not in body and body[:16] not in want:
                applied.append((num, "第 %d 题题干对不上（可能属另一个库），跳过" % num))
                continue
        idx = None
        for j in range(start + 1, min(start + 80, len(lines))):
            if lines[j].startswith("**来源：**"):
                idx = j
                break
        if idx is None:
            applied.append((num, "第 %d 题找不到来源行，跳过" % num))
            continue
        raw = lines[idx]
        for ending in ("\r\n", "\n"):
            if raw.endswith(ending):
                body2, tail = raw[: -len(ending)], ending
                break
        else:
            body2, tail = raw, ""
        if "已合并" in body2:
            applied.append((num, "已有合并标注，跳过"))
            continue
        note = "（同 %s，重复题已合并）" % d["source"]
        if body2.rstrip().endswith("）"):
            body2 = body2.rstrip()[:-1] + note + "）"
        else:
            body2 = body2.rstrip() + note
        lines[idx] = body2 + tail
        applied.append((num, "已标注"))
    return "".join(lines), applied


def main():
    dry = "--dry" in sys.argv
    files = sorted(glob.glob(os.path.join(MAT, "_主观_*.py"))) + sorted(glob.glob(os.path.join(MAT, "_大题_*.py")))
    assert files, "没有找到数据文件"
    subj, big, dups, figs = [], [], [], []
    for p in files:
        mod = load(p)
        items = getattr(mod, "ITEMS", [])
        print("%-46s ITEMS=%2d DUPS=%d FIGURES=%d" % (os.path.basename(p), len(items), len(getattr(mod, "DUPS", [])), len(getattr(mod, "FIGURES", []))))
        for it in items:
            check_item(it, os.path.basename(p))
            (subj if it["kind"] == "主观" else big).append(it)
        dups += list(getattr(mod, "DUPS", []))
        figs += list(getattr(mod, "FIGURES", []))

    subj_nums = list(range(SUBJ_START, SUBJ_START + len(subj)))
    big_nums = list(range(BIG_START, BIG_START + len(big)))
    # 02 与 03 是两个独立的题库文件，编号空间各自独立（历史上就有同号，如 503），允许重叠；
    # 只要各自内部不重号即可。
    assert len(set(subj_nums)) == len(subj_nums)
    assert len(set(big_nums)) == len(big_nums)

    merged = apply_source_merges(subj + big, dups)
    for key, src in merged:
        print("   跨文件合并标注 -> %s | %s" % (key, src[-70:]))
    cross = [d for d in dups if is_cross_bank(d)]
    inbank = [d for d in dups if not is_cross_bank(d)]
    for d in cross:
        print("   跨库重复（不在 02/03 打标，需人工核对其它库）：%s <-> %s" % (d.get("existing_num") or d.get("existing"), d.get("source")))

    # ---- 02 主观题 ----
    text = read_text(SUBJ)
    eol = eol_of(text)
    chunks = "".join(render(n, it, eol) for n, it in zip(subj_nums, subj))
    title = SUBJ_TITLE.format(n=len(subj), a=subj_nums[0], b=subj_nums[-1]) if subj else ""
    if subj:
        text = insert_before(text, "## 附：统计", title + eol + eol + chunks)
    old_total = "总题量：**461 题**"
    assert text.count(old_total) == 1, "02 统计锚点未命中"
    text = text.replace(old_total, "总题量：**%d 题**" % (461 + len(subj)))
    if subj:
        line = "- 第 %d~%d 题为附加面试题2.0·面试题5 / 面试题6 / 面试题7（主观题 %d 题，2026-10-13 新增；同题已按来源合并，详见各题来源行）。" % (subj_nums[0], subj_nums[-1], len(subj)) + eol
        text = insert_before(text, "- 说明：" + eol, line)
    text, applied_s = apply_dups(text, inbank, "02")
    if not dry:
        write_text(SUBJ, text)
    print("02_主观题.md -> %d bytes (eol=%r), 新增 %d 条（%s）" % (len(text.encode("utf-8")), eol, len(subj), "、".join(map(str, subj_nums)) or "无"))

    # ---- 03 大题（两份副本） ----
    text_b = read_text(BIG_A)
    eol_b = eol_of(text_b)
    chunks_b = "".join(render(n, it, eol_b) for n, it in zip(big_nums, big))
    if big:
        text_b = insert_before(text_b, "## 附：统计", BIG_TITLE.format(n=len(big), a=big_nums[0], b=big_nums[-1]) + eol_b + eol_b + chunks_b)
    old_total_b = "合计：**157 题**"
    assert text_b.count(old_total_b) == 1, "03 统计锚点未命中"
    text_b = text_b.replace(old_total_b, "合计：**%d 题**" % (157 + len(big)))
    if big:
        line_b = "- 附加面试题2.0 · 面试题5 / 面试题6 / 面试题7（编程 / 画图）：**%d 题** —— 编号 %d~%d（2026-10-13 新增）" % (len(big), big_nums[0], big_nums[-1]) + eol_b
        text_b = insert_before(text_b, "- 说明：" + eol_b, line_b)
    text_b, applied_b = apply_dups(text_b, inbank, "03")
    if not dry:
        for path in (BIG_A, BIG_B):
            write_text(path, text_b)
    print("03_编程画图大题.md -> %d bytes (eol=%r), 新增大题 %d 条（%s）" % (len(text_b.encode("utf-8")), eol_b, len(big), "、".join(map(str, big_nums)) or "无"))

    print("\n02 合并标注：")
    for num, msg in applied_s:
        print("   #%s %s" % (num, msg))
    print("03 合并标注（已同步两份副本）：")
    for num, msg in applied_b:
        print("   #%s %s" % (num, msg))
    if figs:
        print("\n待裁切配图（主理人处理）：")
        for f in figs:
            print("   %s" % f)


if __name__ == "__main__":
    sys.exit(main())
