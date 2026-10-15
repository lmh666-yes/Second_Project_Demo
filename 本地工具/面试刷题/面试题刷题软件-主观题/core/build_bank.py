# -*- coding: utf-8 -*-
"""题库构建器：从 题库/ 文件夹中的第一个 md 题库文档（自带答案）生成 题库.json。
用法：python build_bank.py   （或由 启动刷题软件.bat 在缺失题库时自动调用）"""
import sys, os, glob, json
from collections import Counter
try:
    sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)
except Exception:
    pass
import bank_parser_md

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)          # 上一级 = 软件根目录
BANK_DIR = os.path.join(ROOT, "题库")
OUT = os.path.join(ROOT, "题库.json")


def main():
    if not os.path.isdir(BANK_DIR):
        os.makedirs(BANK_DIR, exist_ok=True)
    files = sorted(glob.glob(os.path.join(BANK_DIR, "*.md")))
    if not files:
        sys.exit("[错误] 题库文件夹中没有 md 题库文档: " + BANK_DIR)
    src = files[0]
    print("来源文档:", os.path.basename(src))
    bank, problems = bank_parser_md.parse_md(src)
    if not bank:
        sys.exit("[错误] 未能解析出任何题目（需为 02 风格 md：带「**参考答案：**/得分点」）。")
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(bank, f, ensure_ascii=False, indent=1)
    cnt = Counter(it["kind"] for it in bank)
    print("已生成:", OUT)
    print("题数统计:", dict(cnt), "=", len(bank), "题")
    if problems:
        print("[!] 解析告警:")
        for p in problems[:20]:
            print("   ", p)


if __name__ == "__main__":
    main()
