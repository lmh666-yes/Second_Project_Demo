# -*- coding: utf-8 -*-
r"""素材识别工具 —— 把一个文件夹（或单个文件）里的"图片 / PDF / docx / md / txt / csv"
统一识别成一份可读的 Markdown，方便快速浏览与转写题库。

用法：
    python 工具\素材识别.py <文件夹或文件> [--out 输出.md] [--no-ocr] [--index]
        --out     指定输出文件（默认写到 素材目录\_识别结果.md）
        --no-ocr  只列清单、不调用 OCR（快；图片只登记不识别）
        --index   只输出素材清单表

引擎说明：
    · 图片：Windows 自带 OCR（Windows.Media.Ocr，中文简体），无需安装任何东西；
            识别结果会有少量错字与断行，**转写题库时必须人工复核**（尤其代码、下划线、下标）
    · PDF ：优先用 pymupdf(fitz) 抽文本层；扫描版 PDF 会提示按页导出图片再走 OCR
    · docx：python-docx（段落 + 表格）
    · md/txt/csv/json：直接读取
"""
import argparse
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile

IMAGE_EXT = {'.png', '.jpg', '.jpeg', '.bmp', '.gif', '.webp', '.tif', '.tiff'}
TEXT_EXT = {'.md', '.txt', '.csv', '.json', '.log', '.ini', '.c', '.cpp', '.h', '.py'}
PDF_EXT = {'.pdf'}
DOCX_EXT = {'.docx'}
PS1 = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ocr_win.ps1')


def human(n):
    for u in ('B', 'KB', 'MB', 'GB'):
        if n < 1024 or u == 'GB':
            return f'{n:.0f} {u}' if u == 'B' else f'{n:.1f} {u}'
        n /= 1024


def cjk_clean(s):
    """OCR 结果清理：去掉 CJK 字符之间的空格、压缩多余空白"""
    s = re.sub(r'(?<=[\u4e00-\u9fff]) +(?=[\u4e00-\u9fff])', '', s)
    s = re.sub(r'(?<=[\u4e00-\u9fff]) +(?=[，。、；：？！）】”’])', '', s)
    s = re.sub(r'(?<=[（【“‘]) +(?=[\u4e00-\u9fff])', '', s)
    s = re.sub(r'[ \t]{2,}', ' ', s)
    return s.strip()


def ocr_image(path, timeout=120):
    """调用 Windows OCR；失败返回 None"""
    if not os.path.exists(PS1):
        return None
    try:
        kw = {}
        if os.name == 'nt':
            kw['creationflags'] = 0x08000000       # CREATE_NO_WINDOW
        r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                            '-File', PS1, '-Path', path],
                           capture_output=True, timeout=timeout, **kw)
        out = r.stdout.decode('utf-8', 'replace')
        if 'NO_OCR_ENGINE' in out:
            return None
        lines = [cjk_clean(l) for l in out.splitlines()]
        return '\n'.join(l for l in lines if l)
    except Exception as e:
        return f'[OCR 失败：{e}]'


def pdf_text(path):
    try:
        import fitz                                  # pymupdf
    except Exception:
        return None, '未安装 pymupdf（pip install pymupdf），无法抽取 PDF 文本'
    d = fitz.open(path)
    parts = []
    for i, page in enumerate(d, 1):
        t = page.get_text().strip()
        parts.append(f'--- 第 {i}/{len(d)} 页 ---\n' + (t if t else '[本页无文本层，可能是扫描件，需导出图片后 OCR]'))
    return '\n\n'.join(parts), None


def docx_text(path):
    try:
        import docx                                  # python-docx
    except Exception:
        return None, '未安装 python-docx（pip install python-docx），无法读取 docx'
    d = docx.Document(path)
    out = [p.text for p in d.paragraphs if p.text.strip()]
    for ti, tb in enumerate(d.tables, 1):
        out.append(f'--- 表格 {ti} ---')
        for row in tb.rows:
            out.append(' | '.join(c.text.strip() for c in row.cells))
    return '\n'.join(out), None


def scan(target):
    if os.path.isfile(target):
        return [target]
    files = []
    for root, _dirs, names in os.walk(target):
        for n in names:
            files.append(os.path.join(root, n))
    return sorted(files, key=lambda p: os.path.normpath(p).lower())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('target')
    ap.add_argument('--out')
    ap.add_argument('--no-ocr', action='store_true')
    ap.add_argument('--index', action='store_true')
    a = ap.parse_args()

    target = os.path.abspath(a.target)
    files = scan(target)
    base = target if os.path.isdir(target) else os.path.dirname(target)
    out_path = a.out or os.path.join(base, '_识别结果.md')

    groups = {'图片': [], 'PDF': [], 'Word': [], '文本': [], '其它': []}
    for p in files:
        ext = os.path.splitext(p)[1].lower()
        if ext in IMAGE_EXT:
            groups['图片'].append(p)
        elif ext in PDF_EXT:
            groups['PDF'].append(p)
        elif ext in DOCX_EXT:
            groups['Word'].append(p)
        elif ext in TEXT_EXT:
            groups['文本'].append(p)
        else:
            groups['其它'].append(p)

    lines = [f'# 素材识别结果', '',
             f'- 目录：`{target}`',
             f'- 文件：图片 {len(groups["图片"])}、PDF {len(groups["PDF"])}、'
             f'Word {len(groups["Word"])}、文本 {len(groups["文本"])}、其它 {len(groups["其它"])}',
             '']

    # 清单表
    lines += ['## 素材清单', '', '| 序号 | 文件 | 类型 | 大小 |', '|---|---|---|---|']
    for i, p in enumerate(files, 1):
        ext = os.path.splitext(p)[1].lower()
        kind = ('图片' if ext in IMAGE_EXT else 'PDF' if ext in PDF_EXT else
                'Word' if ext in DOCX_EXT else '文本' if ext in TEXT_EXT else '其它')
        lines.append(f'| {i} | `{os.path.basename(p)}` | {kind} | {human(os.path.getsize(p))} |')
    lines.append('')

    if a.index:
        io.open(out_path, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
        print('\n'.join(lines))
        print(f'\n（--index）已写入 {out_path}')
        return

    # 内容
    lines += ['## 内容', '']
    for kind in ('文本', 'Word', 'PDF', '图片'):
        for p in groups[kind]:
            name = os.path.relpath(p, base)
            lines += [f'### 【{kind}】{name}', '']
            text = None
            if kind == '文本':
                for enc in ('utf-8', 'utf-8-sig', 'gbk'):
                    try:
                        text = io.open(p, encoding=enc).read()
                        break
                    except Exception:
                        continue
                text = text if text is not None else '[无法解码]'
                lines += ['```text', text.rstrip(), '```', '']
            elif kind == 'Word':
                text, err = docx_text(p)
                if err:
                    lines += [f'> {err}', '']
                else:
                    lines += ['```text', text.rstrip(), '```', '']
            elif kind == 'PDF':
                text, err = pdf_text(p)
                if err:
                    lines += [f'> {err}', '']
                else:
                    lines += ['```text', text.rstrip(), '```', '']
            else:                                   # 图片
                if a.no_ocr:
                    lines += ['> 已跳过 OCR（--no-ocr）', '']
                    continue
                text = ocr_image(p)
                if text is None:
                    lines += ['> 本机无可用 OCR 引擎，请人工看图转写', '']
                else:
                    lines += ['```text', text.rstrip(), '```', '']
    for p in groups['其它']:
        lines += [f'### 【其它】{os.path.relpath(p, base)}', '',
                  f'> 未处理的类型（{os.path.splitext(p)[1] or "无扩展名"}），{human(os.path.getsize(p))}', '']

    io.open(out_path, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print(f'识别完成：{out_path}')
    print(f'  图片 {len(groups["图片"])}（OCR）｜ PDF {len(groups["PDF"])}｜ '
          f'Word {len(groups["Word"])}｜ 文本 {len(groups["文本"])}｜ 其它 {len(groups["其它"])}')
    print('  提示：OCR 结果有错字与断行，转写题库时务必对照原图复核。')


if __name__ == '__main__':
    main()
