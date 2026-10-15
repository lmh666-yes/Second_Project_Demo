# -*- coding: utf-8 -*-
"""题库构建器：从 题库/ 文件夹中的第一个题库文档（.md / .docx，自带答案）生成 题库.json。
用法：python build_bank.py   （或由 启动刷题软件.bat 在缺失题库时自动调用）
更多功能：支持多题库选择/切换，请用 更新题库.bat。"""
import sys, os, glob, json
from collections import Counter
try:
    sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)
except Exception:
    pass
import bank_parser
import bank_parser_md

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)          # 上一级 = 软件根目录
BANK_DIR = os.path.join(ROOT, "题库")
OUT = os.path.join(ROOT, "题库.json")

KIND_NAMES = {"choice": "单选", "multi": "多选", "judge": "判断",
              "qa": "特殊", "subjective": "主观"}


def main():
    if not os.path.isdir(BANK_DIR):
        os.makedirs(BANK_DIR, exist_ok=True)
    files = sorted(glob.glob(os.path.join(BANK_DIR, "*.md"))
                   + glob.glob(os.path.join(BANK_DIR, "*.docx")))
    if not files:
        sys.exit("[错误] 题库文件夹中没有题库文档（*.md / *.docx）: " + BANK_DIR)
    src = files[0]
    print("来源文档:", os.path.basename(src))
    if src.lower().endswith(".md"):
        bank, problems = bank_parser_md.parse_md(src)
    else:
        bank, problems = bank_parser.parse_bank(src)
    if not bank:
        sys.exit("[错误] 未能解析出任何题目（md 需带「**答案：…**」行）。")
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(bank, f, ensure_ascii=False, indent=1)
    cnt = Counter(it["kind"] for it in bank)
    parts = [f"{KIND_NAMES.get(k, k)} {v}" for k, v in cnt.items()]
    print("已生成:", OUT)
    print("题数统计:", " + ".join(parts), "=", len(bank), "题")
    if problems:
        print("[!] 解析告警:")
        for p in problems[:20]:
            print("   ", p)


if __name__ == "__main__":
    main()
