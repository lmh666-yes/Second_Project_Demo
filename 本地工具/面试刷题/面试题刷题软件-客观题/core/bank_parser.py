# -*- coding: utf-8 -*-
"""通用题库解析器：把任意「含答案」的 Word 题库文档解析为 题库.json 数据。

只识别两类大题：
  · 单选题（A-D 选项 + 答案：X）
  · 判断题（每组若干题 + 答案串 √×…）
其余大题（简答/填空等）自动跳过。

兼容格式：
  · 题干可含多行代码（完整保留）
  · 选项带或不带 A、B、C、D 前缀
  · 选项在一行内用制表符/多空格分隔
  · 判断题 N 题一组共享一个答案串，末题可含多行代码
  · 已知文档笔误修正表 FIXES（按题干关键字匹配，仅对该题生效）
"""
import os
import re

# 已知原文档笔误修正（不影响其他题库）：
# 嵌入式题库第 24 题「interface」选项重复，答案 D 指向 ifconfig
FIXES = {
    "查看服务器网口配置的命令是什么": {
        "options": ["ipconfig", "show", "interface", "ifconfig"],
        "answer": "D",
    },
}


def load_paras(docx_path):
    """读取 docx 的所有非空段落文本"""
    import docx
    doc = docx.Document(docx_path)
    return [p.text.strip() for p in doc.paragraphs if p.text.strip()]


def find_sections(paras):
    """按“一、/二、/三、…”大题标题切分，返回 [{'title':..., 'lines':[...]}]"""
    sections = []
    cur = None
    for t in paras:
        m = re.match(r"^[一二三四五六七八九十]+、\s*(.*)$", t)
        if m:
            if cur:
                sections.append(cur)
            cur = {"title": m.group(1).strip(), "lines": []}
        elif cur is not None:
            cur["lines"].append(t)
    if cur:
        sections.append(cur)
    return sections


def _split_by_answer(lines):
    """把连续行按“答案：…”切块，返回 [(题目行, 答案行), ...]"""
    blocks, cur = [], []
    for t in lines:
        if t.startswith("答案"):
            if cur:
                blocks.append((cur, t))
            cur = []
        else:
            cur.append(t)
    return blocks


def parse_choice_block(lines, ans_line):
    """解析一道单选题，返回 (stem, opts, answer, problem)"""
    problem = ""
    m = re.search(r"答案[:：]\s*([A-Da-d])", ans_line)
    answer = m.group(1).upper() if m else "?"
    # 带 A-D 前缀的选项
    pre_idx = [i for i, ln in enumerate(lines) if re.match(r"^[A-Da-d][\.、．]", ln)]
    if len(pre_idx) >= 4:
        stem_lines = lines[:pre_idx[0]]
        opts = []
        for ln in lines[pre_idx[0]:]:
            m2 = re.match(r"^([A-Da-d])[\.、．]\s*(.*)$", ln)
            if m2:
                opts.append({"key": m2.group(1).upper(), "text": m2.group(2).strip()})
    else:
        # 无前缀（或部分带前缀）：最后 4 行是选项，其余（含代码）是题干
        if len(lines) > 4:
            stem_lines, opt_lines = lines[:-4], lines[-4:]
        else:
            stem_lines, opt_lines = lines[:1], lines[1:]
        # 清理选项前缀：原文档可能存在“A.-3 / B.9 / -12 / 6”这类部分带前缀的混合格式
        cleaned_opts = []
        for ln in opt_lines:
            m3 = re.match(r"^[A-Da-d][\.、．]\s*(.*)$", ln.strip())
            cleaned_opts.append(m3.group(1).strip() if m3 else ln.strip())
        opts = [{"key": "ABCD"[i] if i < 4 else "?", "text": t}
                for i, t in enumerate(cleaned_opts)]
        # 一行内多个选项（制表符 / 2+ 空格分隔）；若带 A-D 前缀则一并去除
        if len(opts) == 1 and re.search(r"\t|\s{2,}", opts[0]["text"]):
            parts = re.split(r"\t|\s{2,}", opts[0]["text"].strip())
            cleaned = []
            for p in parts:
                m3 = re.match(r"^[A-Da-d][\.、．]\s*(.*)$", p.strip())
                cleaned.append(m3.group(1).strip() if m3 else p.strip())
            opts = [{"key": "ABCD"[i], "text": t} for i, t in enumerate(cleaned)]
    stem = "\n".join(stem_lines)
    # 已知笔误修正
    for kw, fix in FIXES.items():
        if kw in stem:
            opts = [{"key": "ABCD"[i], "text": t} for i, t in enumerate(fix["options"])]
            answer = fix["answer"]
            stem = stem.split("\n")[0]
            problem = "已按笔误修正"
            break
    if len(opts) != 4:
        problem = f"选项数={len(opts)}"
    return stem, opts, answer, problem


def parse_judge_block(lines, ans_line):
    """判断题 N 题一组共享答案串；若行数多于答案数，末题带代码（多行合并）"""
    items = []
    m = re.search(r"答案[:：]\s*([√×对错TF]+)", ans_line)
    if not m:
        return [(ln, "?", "答案格式异常") for ln in lines]
    s = m.group(1)
    N, M = len(s), len(lines)
    norm = lambda c: "√" if c in ("√", "对", "T") else "×"
    if M == N:
        for j in range(N):
            items.append((lines[j], norm(s[j]), ""))
    elif M > N:
        for j in range(N - 1):
            items.append((lines[j], norm(s[j]), ""))
        items.append(("\n".join(lines[N - 1:]), norm(s[N - 1]), "末题含多行代码已合并"))
    else:
        for j in range(M):
            items.append((lines[j], norm(s[j]) if j < N else "?", "答案数多于题数"))
    return items


def parse_bank(docx_path):
    """解析任意题库 Word 文档 → (题库列表, 解析告警列表)。仅生成 choice + judge。
    文档无法打开/损坏时返回空列表与错误告警（不抛异常）。"""
    if not os.path.exists(docx_path):
        return [], ["文件不存在: " + docx_path]
    try:
        paras = load_paras(docx_path)
    except Exception as e:
        return [], [f"无法打开文档（可能不是有效的 Word 文件）：{e}"]
    sections = find_sections(paras)
    bank, problems = [], []
    for sec in sections:
        title = sec["title"]
        if "单选" in title:
            for idx, (lines, ans) in enumerate(_split_by_answer(sec["lines"]), 1):
                stem, opts, answer, problem = parse_choice_block(lines, ans)
                if problem:
                    problems.append(f"单选#{idx} {problem} | {stem[:40]}")
                bank.append({"id": f"choice-{idx}", "kind": "choice", "kind_name": "单选题",
                             "num": idx, "stem": stem, "options": opts,
                             "answer": answer, "explain": ""})
        elif "判断" in title:
            jnum = 0
            for lines, ans in _split_by_answer(sec["lines"]):
                for stem, answer, problem in parse_judge_block(lines, ans):
                    jnum += 1
                    if problem:
                        problems.append(f"判断#{jnum} {problem} | {stem[:40]}")
                    bank.append({"id": f"judge-{jnum}", "kind": "judge", "kind_name": "判断题",
                                 "num": jnum, "stem": stem, "options": [],
                                 "answer": answer, "explain": ""})
    return bank, problems


def count(bank):
    """统计题型分布"""
    c = {"choice": 0, "judge": 0}
    for it in bank:
        c[it["kind"]] = c.get(it["kind"], 0) + 1
    return c
