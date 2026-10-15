# -*- coding: utf-8 -*-
"""新题库全面校验：对 题库.json 与 docx 原文做 5 遍逐题映射校验。
覆盖：题数/题型 / 字段完整性 / 乱码 / 答案合法性 / 与原文逐行映射 / 代码完整 /
题序 / 图片标注 / 解析确定性。全部通过才算合格。"""
import sys, io, os, re, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
import bank_parser

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
DOCX = os.path.join(ROOT, "题库", "嵌入式软件开发（中级）题库.docx")
JSON = os.path.join(ROOT, "题库.json")

def norm(s):
    return re.sub(r"\s+", "", s)

def check_once(round_no):
    errs = []
    # 1. 解析 docx
    paras = bank_parser.load_paras(DOCX)
    bank, problems = bank_parser.parse_bank(DOCX)

    # 2. 题数/题型
    if len(bank) != 130:
        errs.append(f"题数 {len(bank)} != 130")
    cnt = bank_parser.count(bank)
    if cnt != {"choice": 80, "judge": 50}:
        errs.append(f"题型 {cnt}")

    # 3. 从题库.json 读取(独立于解析)
    bank_json = json.load(open(JSON, encoding="utf-8"))
    if len(bank_json) != len(bank):
        errs.append("题库.json 与解析结果题数不一致")
    if len(bank_json) != 130:
        errs.append(f"题库.json 题数 {len(bank_json)} != 130")
    cnt2 = bank_parser.count(bank_json)
    if cnt2 != {"choice": 80, "judge": 50}:
        errs.append(f"题库.json 题型 {cnt2}")

    # 4. 原文段落集合(去题号/选项前缀)
    stem_pool = set()
    opt_pool = set()
    for p in paras:
        stem_pool.add(norm(re.sub(r"^(\d+[、.．]|\*\d+[、.．])\s*", "", p)))
        opt_pool.add(norm(re.sub(r"^([A-Da-d][.、．])\s*", "", p)))

    # 5. 逐题校验
    for it in bank_json:
        qid = it.get("id", "")
        # 字段
        for f in ("id", "kind", "stem", "answer"):
            if not it.get(f):
                errs.append(f"{qid} 缺字段 {f}")
        if it["kind"] not in ("choice", "judge"):
            errs.append(f"{qid} kind={it['kind']}")
        # 乱码
        blob = json.dumps(it, ensure_ascii=False)
        if "\ufffd" in blob:
            errs.append(f"{qid} 含替换字符乱码")
        if re.search(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", blob):
            errs.append(f"{qid} 含非法控制字符")
        # stem 逐行在原文
        stem = it["stem"]
        for line in stem.split("\n"):
            l = line.strip()
            if not l or "【本题含图片" in l:
                continue
            ln = norm(re.sub(r"^(\d+[、.．]|\*\d+[、.．])\s*", "", l))
            if ln not in stem_pool and ln not in opt_pool:
                errs.append(f"{qid} 题干行不在原文: {l[:40]!r}")
        # 选项在原文(去前缀)
        for o in it.get("options", []):
            on = norm(o.get("text", ""))
            if on and on not in opt_pool:
                errs.append(f"{qid} 选项不在原文: {o.get('text','')[:40]!r}")
        # 选择题答案合法性 + 选项key唯一
        if it["kind"] == "choice":
            keys = [o["key"] for o in it.get("options", [])]
            if len(keys) != len(set(keys)):
                errs.append(f"{qid} 选项key重复")
            if str(it["answer"]).strip().upper() not in keys:
                errs.append(f"{qid} 答案 {it['answer']} 不在选项 {keys}")
            if len(keys) != 4:
                errs.append(f"{qid} 选项数 {len(keys)}")
        else:
            a = str(it["answer"]).strip().upper()
            if a not in ("对", "错", "√", "×", "T", "F", "TRUE", "FALSE"):
                errs.append(f"{qid} 判断题答案非法 {it['answer']}")

    # 6. 题序: id 顺序与原文大题顺序一致
    choice_ids = [it["id"] for it in bank_json if it["kind"] == "choice"]
    judge_ids = [it["id"] for it in bank_json if it["kind"] == "judge"]
    if choice_ids != [f"choice-{i}" for i in range(1, 81)]:
        errs.append("单选题序异常")
    if judge_ids != [f"judge-{i}" for i in range(1, 51)]:
        errs.append("判断题序异常")

    # 7. 代码完整性: 判断40 含完整代码
    j40 = next((it for it in bank_json if it["id"] == "judge-40"), None)
    if j40 and "printf" not in j40["stem"]:
        errs.append("判断40 代码缺失")
    # 图片归档存在性（图片属第三大题 LED 编程题，不在题库范围，仅归档供查看）
    imgf = os.path.join(ROOT, "题库", "题目图片", "LED闪烁效果图.jpg")
    if not os.path.exists(imgf):
        errs.append("LED 图片未归档")

    # 8. 确定性: 重复解析结果一致
    bank2, _ = bank_parser.parse_bank(DOCX)
    if json.dumps(bank, ensure_ascii=False) != json.dumps(bank2, ensure_ascii=False):
        errs.append("解析不确定(两次结果不一致)")

    print(f"  第 {round_no} 遍: {'通过 ✓' if not errs else '存在异常 ✗'}")
    for e in errs[:15]:
        print(f"    - {e}")
    return len(errs) == 0

def main():
    print("=== 新题库全面校验(5 遍) ===")
    all_ok = True
    for r in range(1, 6):
        okk = check_once(r)
        all_ok = all_ok and okk
    print(f"\n===== 5 遍校验: {'全部通过 ✓✓✓✓✓' if all_ok else '存在失败 ✗'} =====")
    sys.exit(0 if all_ok else 1)

if __name__ == "__main__":
    main()
