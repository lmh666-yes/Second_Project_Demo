# -*- coding: utf-8 -*-
r"""从 `计算机专业英语词汇终结版.pdf` 提取词库 → 词库/词库.csv + 词库.json

用法：python tools/extract_pdf.py [PDF路径]
说明：
 · 一行 = 「英文单词/短语 + 中文释义」（中文从第一个汉字开始）
 · 纯中文行 / 「数字开头」行 = 上一条释义的换行续行，自动拼回上一条
 · 「其他缩写」段：`缩写 全称 中文` → word=缩写，note=全称
 · 括号缩写（如 Domain name system（DNS））→ note = DNS
 · 英文里的 " / " 是原文档分栏残留 → 只取斜杠前部分
 · 同一单词重复出现 → 释义合并（用「；」连接）
"""
import csv
import io
import json
import os
import random
import re
import sys

import pymupdf

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PDF = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(ROOT),
                                                         '计算机专业英语词汇终结版.pdf')
CJK = re.compile(r'[\u4e00-\u9fff]')


def clean_en(en):
    en = re.sub(r'[ \t\u3000]+', ' ', en).strip()
    en = re.sub(r'[\s\]】」"”]+$', '', en.strip())        # 去掉尾部杂符
    en = re.split(r'\s*/\s*', en)[0].strip()             # 分栏残留
    en = re.sub(r'(\b\d{4})\1+', r'\1', en)              # 19941994 → 1994
    en = re.sub(r'\bof(\d{4})', r'of \1', en)            # of1986 → of 1986
    en = re.sub(r'^([A-Z]{2,6})([a-z])', r'\1 \2', en)   # MICRmagnetic → MICR magnetic
    en = re.sub(r'([a-z])([A-Z])', r'\1 \2', en) if en.isupper() is False else en
    return re.sub(r'\s+', ' ', en).strip(' -–—')


def clean_cn(cn):
    return cn.replace(' ', '').strip()


def parse(pdf_path):
    doc = pymupdf.open(pdf_path)
    entries, skipped, notes = [], [], []
    in_abbr = False
    for pno, page in enumerate(doc, 1):
        for raw in page.get_text('text').split('\n'):
            s = re.sub(r'[ \t\u3000]+', ' ', raw).strip()
            if not s or '.....' in s or s in ('计算机专业英语词汇', '目录'):
                continue
            if re.fullmatch(r'[A-Z]', s):
                continue
            if s == '其他缩写':
                in_abbr = True
                continue
            if not CJK.search(s):
                notes.append(f"p{pno} 英文残行（无释义，忽略）: {s}")
                continue
            cut = CJK.search(s).start()
            head, body = s[:cut], clean_cn(s[cut:])
            if not re.search(r'[A-Za-z]', head):          # 纯中文/数字开头 → 续行
                if entries:
                    entries[-1]['cn'] = clean_cn(entries[-1]['cn'] + body)
                else:
                    skipped.append(f"p{pno} 无归属续行: {s}")
                continue
            m = re.match(r'^([A-Z]{2,6})\s+([A-Za-z][A-Za-z0-9\-\s\.]*?)\s*$', head)
            if in_abbr and m:
                entries.append({'en': m.group(1), 'note': m.group(2).strip(), 'cn': body})
                continue
            en, note = clean_en(head), ''
            m = re.match(r'^(.*?)[（(]\s*(.*?)\s*[）)]\s*$', en)
            if m and m.group(1).strip() and re.fullmatch(r'[A-Za-z0-9\-\./ ]{2,48}', m.group(2)):
                en, note = m.group(1).strip(), m.group(2).strip()
            else:
                # 括号后残留的英文单词（实际属于释义，如 IP（Internet Protocol）Internet 协议…）
                m2 = re.match(r'^(.*[）)])\s*([A-Za-z][A-Za-z0-9\-]*)$', en)
                if m2 and m2.group(2).lower() in m2.group(1).lower():
                    body = m2.group(2) + body
                    en = m2.group(1)
                    m3 = re.match(r'^(.*?)[（(]\s*(.*?)\s*[）)]$', en)
                    if m3 and m3.group(1).strip():
                        en, note = m3.group(1).strip(), m3.group(2).strip()
            if not en:
                skipped.append(f"p{pno} 无英文: {s}")
                continue
            entries.append({'en': en, 'note': note, 'cn': body})

    merged, order = {}, []
    for it in entries:
        it['cn'] = it['cn'].strip('，,。;；')
        if len(it['en']) > 60 or it['cn'].startswith('的') or len(it['cn']) < 1:
            skipped.append(f"丢弃: {it['en']} | {it['cn']}")
            continue
        k = it['en'].lower()
        if k not in merged:
            merged[k] = {'en': it['en'], 'note': it['note'], 'cn': [it['cn']]}
            order.append(k)
        else:
            if it['cn'] and it['cn'] not in merged[k]['cn']:
                merged[k]['cn'].append(it['cn'])
            if it['note'] and not merged[k]['note']:
                merged[k]['note'] = it['note']
    return [{'en': merged[k]['en'], 'note': merged[k]['note'],
             'cn': '；'.join(merged[k]['cn'])} for k in order], skipped, notes


def main():
    bank, skipped, notes = parse(PDF)
    os.makedirs(os.path.join(ROOT, '词库'), exist_ok=True)
    with io.open(os.path.join(ROOT, '词库', '词库.csv'), 'w',
                 encoding='utf-8-sig', newline='') as f:
        w = csv.writer(f)
        w.writerow(['英文', '全称/缩写', '中文释义'])
        for it in bank:
            w.writerow([it['en'], it['note'], it['cn']])
    json.dump(bank, io.open(os.path.join(ROOT, '词库.json'), 'w', encoding='utf-8'),
              ensure_ascii=False, indent=1)
    print(f"来源: {PDF}")
    print(f"词条: {len(bank)} | 跳过: {len(skipped)} | 提示: {len(notes)}")
    for x in skipped:
        print("   ", x)
    for x in notes:
        print("   ", x)
    random.seed(7)
    print("抽样:", " / ".join(f"{it['en']}={it['cn']}" for it in random.sample(bank, 5)))


if __name__ == '__main__':
    main()
