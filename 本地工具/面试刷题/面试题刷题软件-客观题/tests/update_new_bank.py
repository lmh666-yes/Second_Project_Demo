# -*- coding: utf-8 -*-
"""用新题库 docx 生成 题库.json，并归档题目图片。
图片无法内嵌进文本版题库，故：导出图片到 题库/题目图片/，并在对应题目标注。"""
import sys, io, os, json, shutil
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core"))
import docx
import bank_parser

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
NEW_DOCX = os.path.join(ROOT, "题库", "嵌入式软件开发（中级）题库.docx")
OUT = os.path.join(ROOT, "题库.json")
IMG_DIR = os.path.join(ROOT, "题库", "题目图片")
IMG_NAME = "LED闪烁效果图.jpg"

def export_images(docx_path, out_dir, name):
    """导出 docx 中的图片；返回导出文件列表"""
    os.makedirs(out_dir, exist_ok=True)
    doc = docx.Document(docx_path)
    out_files = []
    for i, rel in enumerate(doc.part.rels.values()):
        if "image" in rel.reltype:
            ext = rel.target_part.content_type.split("/")[-1]
            fn = os.path.join(out_dir, name if ext == "jpeg" else f"img_{i}.{ext}")
            with open(fn, "wb") as f:
                f.write(rel.target_part.blob)
            out_files.append(fn)
    return out_files

def main():
    bank, problems = bank_parser.parse_bank(NEW_DOCX)
    if not bank:
        print("[错误] 解析失败:", problems[:10])
        sys.exit(1)

    # 归档图片（该图片属于第三大题“简答与编程”的 LED 编程题，不在题库刷题范围内；
    # 仅归档保存供查看，不改变题库内容以保证一字不差）
    imgs = export_images(NEW_DOCX, IMG_DIR, IMG_NAME)
    print("归档图片:", imgs)
    if imgs:
        print("[提示] 图片属于第三大题（简答与编程）的 LED 编程题，不在题库范围内，"
              "仅归档到 题库/题目图片/ 供查看")

    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(bank, f, ensure_ascii=False, indent=1)

    print("已生成:", OUT)
    print("题数:", len(bank), bank_parser.count(bank))
    if problems:
        print("告警:")
        for p in problems:
            print("  !", p)
    print("[成功] 题库已更新")

if __name__ == "__main__":
    main()
