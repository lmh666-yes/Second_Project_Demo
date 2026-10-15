# -*- coding: utf-8 -*-
"""把词库（词库.json / 词库/词库.csv）内嵌进手机版单文件应用。

用法：python build_web.py
读取：词库.json（缺失时自动从 词库/词库.csv 生成）+ www/template.html
输出：www/index.html + 根目录「英语单词刷词.html」+ android/app/src/main/assets/index.html
"""
import csv
import io
import json
import os
import sys

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

HERE = os.path.dirname(os.path.abspath(__file__))
CSV = os.path.join(HERE, "词库", "词库.csv")
JSON_SRC = os.path.join(HERE, "词库.json")
TEMPLATE = os.path.join(HERE, "www", "template.html")
OUT = os.path.join(HERE, "www", "index.html")
FRIENDLY = os.path.join(HERE, "英语单词刷词.html")
APK_OUT = os.path.join(HERE, "android", "app", "src", "main", "assets", "index.html")


def from_csv():
    """词库.csv（英文,全称/缩写,中文释义）→ 词条列表"""
    bank, seen = [], set()
    with io.open(CSV, encoding="utf-8-sig", newline="") as f:
        for i, row in enumerate(csv.reader(f)):
            if i == 0 or len(row) < 3:
                continue
            en, note, cn = row[0].strip(), row[1].strip(), row[2].strip()
            if not en or not cn:
                continue
            key = en.lower()
            if key in seen:
                continue
            seen.add(key)
            bank.append({"en": en, "note": note, "cn": cn})
    return bank


def main():
    if os.path.exists(JSON_SRC):
        bank = json.load(io.open(JSON_SRC, encoding="utf-8"))
        src = "词库.json"
    elif os.path.exists(CSV):
        bank = from_csv()
        json.dump(bank, io.open(JSON_SRC, "w", encoding="utf-8"),
                  ensure_ascii=False, indent=1)
        src = "词库/词库.csv（已同步生成 词库.json）"
    else:
        sys.exit("[错误] 未找到词库：请先运行 tools/extract_pdf.py 生成 词库/词库.csv")
    if not os.path.exists(TEMPLATE):
        sys.exit("[错误] 未找到模板: " + TEMPLATE)

    html = io.open(TEMPLATE, encoding="utf-8").read()
    if "/*__BANK__*/" not in html:
        sys.exit("[错误] 模板缺少词库占位符 /*__BANK__*/")
    data = []
    for i, it in enumerate(bank, 1):
        data.append({"id": "w%04d" % i, "en": it["en"],
                     "note": it.get("note", ""), "cn": it["cn"]})
    js = "const BANK=" + json.dumps(data, ensure_ascii=False) + ";\n"
    js += ('const BANK_INFO={"count":%d,"source":"计算机专业英语词汇终结版.pdf"};'
           % len(data))
    html = html.replace("/*__BANK__*/", js)
    html = html.replace("/*__CONFIG__*/", "var EXAM_NUM=20, EXAM_SCORE=5;")

    for path in (OUT, FRIENDLY, APK_OUT):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        io.open(path, "w", encoding="utf-8", newline="\n").write(html)
    dup = len(set(it["en"].lower() for it in data))
    print("词库来源:", src)
    print("已生成:")
    print("  ", OUT)
    print("  ", FRIENDLY, "（根目录友好名，发这个到手机更好找）")
    print("  ", APK_OUT)
    print("词条:", len(data), "（去重后英文单词数", dup, "）| 文件大小",
          os.path.getsize(OUT) // 1024, "KB")


if __name__ == "__main__":
    main()
