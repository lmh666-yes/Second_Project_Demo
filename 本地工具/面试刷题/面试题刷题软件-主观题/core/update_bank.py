# -*- coding: utf-8 -*-
"""更新题库工具：扫描 题库/ 文件夹中的 md 题库文档（自带答案），
选择要使用的题库并解析，把结果覆盖生成 题库.json —— 实现多种题库切换。

用法：双击 更新题库.bat，或命令行 python update_bank.py"""
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


def _input(prompt=""):
    """读取输入；非交互环境（管道/重定向）下返回空串不崩溃"""
    try:
        return input(prompt)
    except EOFError:
        return ""


def main():
    if not os.path.isdir(BANK_DIR):
        os.makedirs(BANK_DIR, exist_ok=True)
    files = sorted(glob.glob(os.path.join(BANK_DIR, "*.md")))
    if not files:
        print("[错误] 题库文件夹中没有找到 md 题库文档（*.md）")
        print("       请把题库文档（自带答案）放入：", BANK_DIR)
        _input("\n按回车退出...")
        return

    # 选择题库
    if len(files) == 1:
        chosen = files[0]
        print("找到 1 个题库文档：", os.path.basename(chosen))
    else:
        print(f"找到 {len(files)} 个题库文档，请选择要使用的题库：")
        for i, f in enumerate(files, 1):
            print(f"  [{i}] {os.path.basename(f)}")
        while True:
            s = _input(f"请输入编号（1-{len(files)}）：").strip()
            try:
                n = int(s)
                if 1 <= n <= len(files):
                    chosen = files[n - 1]
                    break
            except ValueError:
                pass
            if not s:
                chosen = files[0]
                print("  未输入，默认使用第 1 个：", os.path.basename(chosen))
                break
            print("  输入无效，请重新输入")

    # 解析
    print("\n正在解析：", os.path.basename(chosen))
    bank, problems = bank_parser_md.parse_md(chosen)
    if not bank:
        print("\n[错误] 未能从文档解析出任何题目：")
        for p in problems[:10]:
            print("   ", p)
        print("       请确认题库文档中每题带有「**参考答案：**」与「**得分点：**」行。")
        _input("\n按回车退出...")
        return

    if problems:
        print("\n[!] 解析告警：")
        for p in problems[:20]:
            print("   ", p)

    # 覆盖生成 题库.json
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(bank, f, ensure_ascii=False, indent=1)

    cnt = Counter(it["kind"] for it in bank)
    print("\n[成功] 题库已更新！")
    print(f"  当前题库：{os.path.basename(chosen)}")
    print(f"  题数统计：{dict(cnt)} = {len(bank)} 题")
    print("  重新打开刷题软件即可使用新题库。")
    _input("\n按回车退出...")


if __name__ == "__main__":
    main()
