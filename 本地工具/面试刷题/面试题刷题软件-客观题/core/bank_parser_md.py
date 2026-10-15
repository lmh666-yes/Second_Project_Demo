# -*- coding: utf-8 -*-
"""解析面试题库 md 文件（如 01_客观题_选择题.md）为 题库.json 数据。

题型：
  · 单选（答案：A）          → kind=choice
  · 多选（答案：B、C）        → kind=multi
  · 判断（答案：✅ 对 / ❌ 错）→ kind=judge
  · 特殊（无固定答案，如存疑题）→ kind=qa（软件中显示选项/查看答案，不判分）

兼容格式：
  · 选项 - A. xx / - A) xx / - A、xx / - A xx / - (A) xx，支持 a-e 小写、A-E 五选项
  · 题干可含多行代码（``` 围栏自动去除，内容保留）
  · 答案带括号注（如“C（存疑）”）时取字母；纯存疑文本自动归为 qa 题
"""
import os
import re
import shutil


def _split_blocks(lines):
    """按 '**N.**' 题号行切块；代码块（```）内的行不切。"""
    blocks, cur, in_code = [], None, False
    for ln in lines:
        s = ln.rstrip()
        if s.strip().startswith("```"):
            in_code = not in_code
        if not in_code:
            m = re.match(r"^\*\*(\d+)\.\*\*\s*(.*)$", s)
            if m:
                if cur is not None:
                    blocks.append(cur)
                cur = {"num": int(m.group(1)), "first": m.group(2), "lines": []}
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


def _classify_answer(ans_raw):
    """→ (kind, answer)；无法判分时返回 ('qa', 原文)"""
    a = ans_raw.strip()
    if "✅" in a or a in ("对", "√", "T", "TRUE"):
        return "judge", "√"
    if "❌" in a or a in ("错", "×", "F", "FALSE"):
        return "judge", "×"
    if a.startswith("存疑") or a.startswith("本题存疑"):
        return "qa", a
    m = re.match(r"^([A-Da-d])((?:\s*[、,，]\s*[A-Da-d])*)\s*(?:[（(].*[）)])?\s*$", a)
    if m:
        letters = "".join(sorted(set(
            (m.group(1) + (m.group(2) or "")).upper().replace("、", "").replace(",", "")
            .replace("，", "").replace(" ", ""))))
        return ("choice" if len(letters) == 1 else "multi"), letters
    return "qa", a


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
    """解析单个题块 → (item, warn)"""
    num, first = b["num"], b["first"]
    stem_lines = [first] if first.strip() else []
    opts, ans_raw, explain_lines, srcs = [], None, [], []
    phase = "stem"          # stem → opts → ans → explain → done
    in_code = False
    for ln in b["lines"]:
        if ln.strip().startswith("```"):
            in_code = not in_code
            continue                        # 去掉代码围栏，保留代码文本
        if in_code:
            if phase == "stem":
                stem_lines.append(ln)
            elif phase == "explain":
                explain_lines.append(ln)
            continue
        if ln.strip() == "---" or ln.startswith("#"):
            break
        m_ans = re.match(r"^\*\*答案：(.*?)\*\*\s*$", ln)
        if m_ans and phase in ("stem", "opts"):
            ans_raw = m_ans.group(1).strip()
            phase = "ans"
            continue
        m_src = re.match(r"^\*\*来源：\*\*\s*(.*)$", ln)
        if m_src:
            srcs += _extract_src_links(m_src.group(1))
            break
        m_exp = re.match(r"^\*\*解析：\*\*\s*(.*)$", ln)
        if m_exp:
            explain_lines.append(m_exp.group(1))
            phase = "explain"
            continue
        if phase == "explain":
            if ln.strip():
                explain_lines.append(ln)
            continue
        m_opt = re.match(r"^- (?:[（(]([A-Ea-e])[）)]|([A-Ea-e])[\.\)、]|([A-Ea-e])\s)\s*(.*)$", ln)
        if m_opt and phase in ("stem", "opts"):
            key = (m_opt.group(1) or m_opt.group(2) or m_opt.group(3)).upper()
            opts.append({"key": key, "text": m_opt.group(4).strip()})
            phase = "opts"
            continue
        if phase == "stem" and ln.strip():
            stem_lines.append(ln)

    stem = _norm_text("\n".join(stem_lines))
    explain = _norm_text("\n".join(explain_lines))
    if ans_raw is None:
        return None, f"#{num} 缺少答案行"
    kind, answer = _classify_answer(ans_raw)
    kind_name = {"choice": "单选题", "multi": "多选题",
                 "judge": "判断题", "qa": "特殊题"}[kind]
    warn = ""
    if kind in ("choice", "multi") and len(opts) < 2:
        warn = f"#{num} {kind} 选项数={len(opts)}"
    item = {"id": f"{kind}-{num}", "kind": kind, "kind_name": kind_name,
            "num": num, "stem": stem, "options": opts,
            "answer": answer, "explain": explain,
            "imgs": [os.path.basename(p) for p in srcs]}
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
        if item is None:
            problems.append(warn or f"#{b['num']} 解析失败")
            continue
        if warn:
            problems.append(warn)
        bank.append(item)
    if any(it.get("imgs") for it in bank):
        _sync_images(path, bank, problems)
    return bank, problems


def count(bank):
    """统计题型分布"""
    c = {}
    for it in bank:
        c[it["kind"]] = c.get(it["kind"], 0) + 1
    return c
