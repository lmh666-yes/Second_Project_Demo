# -*- coding: utf-8 -*-
"""解析面试题库 md 文件（如 02_主观题.md）为 题库.json 数据。

格式：**N.** 题干 → **题目解析：** → **标准答案：**（兼容旧 **参考答案：**/**参考译文：**）→ **得分点：** → ---
每题生成 {id, kind:'subjective', num, stem, options:[], answer, explain, analysis, hot}：
  · analysis = 题目解析（新格式；旧格式为空）
  · answer   = 标准答案（新格式；旧格式取 参考答案/参考译文）
  · explain  = 得分点
  · hot      = 热点题标记（**热点：** 是）
"""
import os
import re
import shutil


def _split_blocks(lines):
    """按 '**N.**' 题号行切块；代码块（```）内的行不切。
    同时记录每道题所属分区：sec_group（一/二级标题）与 sec（更深标题，如试卷名）"""
    blocks, cur, in_code = [], None, False
    sec_group, sec = "", ""
    for ln in lines:
        s = ln.rstrip()
        if s.strip().startswith("```"):
            in_code = not in_code
        if not in_code:
            if s.startswith("#"):
                lvl = len(s) - len(s.lstrip("#"))
                title = s.lstrip("#").strip()
                if lvl <= 2:
                    sec_group, sec = title, title
                else:
                    sec = title
                if cur is not None:          # 标题行结束当前块（与块内遇 # 截断等价）
                    blocks.append(cur)
                    cur = None
                continue
            m = re.match(r"^\*\*(\d+)\.\*\*\s*(.*)$", s)
            if m:
                if cur is not None:
                    blocks.append(cur)
                cur = {"num": int(m.group(1)), "first": m.group(2), "lines": [],
                       "sec": sec, "sec_group": sec_group}
                continue
        if cur is not None:
            cur["lines"].append(s)
    if cur is not None:
        blocks.append(cur)
    return blocks


def _norm_text(text):
    t = text.replace("**", "")
    t = re.sub(r"\n{3,}", "\n\n", t)
    return t.strip()


def _extract_src_links(line):
    """从一行文本中提取 [图...](path) 链接 → [相对路径, ...]"""
    return [m.group(1) for m in re.finditer(r"\[图[^\]]*\]\(([^)]+)\)", line)]


def _sync_images(md_path, bank, problems):
    """把 md 中引用的配图同步到软件 imgs/ 目录（已存在则跳过；找不到源文件告警）"""
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    imgdir = os.path.join(root, "imgs")
    rel_map = {}
    with open(md_path, encoding="utf-8") as f:
        for ln in f:
            for rel in _extract_src_links(ln):
                rel_map[os.path.basename(rel)] = rel
    for it in bank:
        for name in it.get("imgs", []):
            dst = os.path.join(imgdir, name)
            if os.path.exists(dst):
                continue
            rel = rel_map.get(name)
            src = os.path.normpath(os.path.join(os.path.dirname(md_path), rel)) if rel else None
            if src and os.path.exists(src):
                os.makedirs(imgdir, exist_ok=True)
                shutil.copy(src, dst)
            else:
                problems.append(f"配图未找到: {name}（请把 面试题md 文件夹与软件放在同一位置）")


def _parse_block(b):
    """解析单个主观题块 → (item, warn)
    字段：**题目解析：** / **标准答案：**（兼容旧 **参考答案：**/**参考译文：**）/ **得分点：** / **热点：**"""
    num, first = b["num"], b["first"]
    stem_lines = [first] if first.strip() else []
    ana_lines, ans_lines, exp_lines, srcs = [], [], [], []
    hot = False
    phase = "stem"          # stem → analysis → ans → explain
    in_code = False
    for ln in b["lines"]:
        if ln.strip().startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            if phase == "stem":
                stem_lines.append(ln)
            elif phase == "analysis":
                ana_lines.append(ln)
            elif phase == "ans":
                ans_lines.append(ln)
            elif phase == "explain":
                exp_lines.append(ln)
            continue
        if ln.strip() == "---" or ln.startswith("#"):
            break
        m_src = re.match(r"^\*\*来源：\*\*\s*(.*)$", ln)
        if m_src:
            srcs += _extract_src_links(m_src.group(1))
            break
        m_f = re.match(r"^\*\*(题目解析|标准答案|参考答案|参考译文|得分点|热点)：\*\*\s*(.*)$", ln)
        if m_f:
            key, val = m_f.group(1), m_f.group(2).strip()
            if key == "题目解析":
                phase = "analysis"
                if val:
                    ana_lines.append(val)
            elif key in ("标准答案", "参考答案", "参考译文"):
                phase = "ans"
                if val:
                    ans_lines.append(val)
            elif key == "得分点":
                phase = "explain"
                if val:
                    exp_lines.append(val)
            elif key == "热点":
                hot = val not in ("", "否", "无")
            continue
        if ln.strip():
            if phase == "analysis":
                ana_lines.append(ln)
            elif phase == "ans":
                ans_lines.append(ln)
            elif phase == "explain":
                exp_lines.append(ln)
            else:
                stem_lines.append(ln)

    item = {"id": f"sub-{num}", "kind": "subjective", "kind_name": "主观题",
            "num": num, "stem": _norm_text("\n".join(stem_lines)),
            "options": [], "answer": _norm_text("\n".join(ans_lines)),
            "explain": _norm_text("\n".join(exp_lines)),
            "analysis": _norm_text("\n".join(ana_lines)),
            "hot": hot,
            "sec": b.get("sec", ""), "sec_group": b.get("sec_group", ""),
            "imgs": [os.path.basename(p) for p in srcs]}
    warn = ""
    if not item["answer"]:
        warn = f"#{num} 缺标准答案"
    elif not item["explain"]:
        warn = f"#{num} 缺得分点"
    return item, warn


def parse_md(path):
    """解析 md 文件 → (题库列表, 告警列表)"""
    if not os.path.exists(path):
        return [], ["文件不存在: " + path]
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().split("\n")
    except Exception as e:
        return [], [f"无法读取文件：{e}"]
    bank, problems = [], []
    for b in _split_blocks(lines):
        item, warn = _parse_block(b)
        if item is not None and item["stem"]:
            bank.append(item)
        if warn:
            problems.append(warn)
    if any(it.get("imgs") for it in bank):
        _sync_images(path, bank, problems)
    return bank, problems


def count(bank):
    """统计题型分布"""
    c = {}
    for it in bank:
        c[it["kind"]] = c.get(it["kind"], 0) + 1
    return c
