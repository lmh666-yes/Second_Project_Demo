# -*- coding: utf-8 -*-
r"""词库 / 网页 / 选项规则自检（无界面，命令行运行）

用法：python tests\check_wordbank.py
"""
import io
import json
import os
import random
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
JSON_SRC = os.path.join(ROOT, '词库.json')
HTML = os.path.join(ROOT, 'www', 'index.html')
csv_path = os.path.join(ROOT, '词库', '词库.csv')

fails = []


def check(cond, msg):
    print(("  [PASS] " if cond else "  [FAIL] ") + msg)
    if not cond:
        fails.append(msg)


bank = json.load(io.open(JSON_SRC, encoding='utf-8'))
print("词条数:", len(bank))

# 1. 词库完整性
check(len(bank) >= 700, f"词条数 ≥700（实际 {len(bank)}）")
check(all(it['en'].strip() and it['cn'].strip() for it in bank), "没有空英文/空释义")
check(len(set(it['en'].lower() for it in bank)) == len(bank), "英文单词不重复")
bad_en = [it for it in bank if re.match(r'^\d', it['en'])]
check(not bad_en, f"没有数字开头的残行（{len(bad_en)} 条）")
bad_cn = [it for it in bank if it['cn'].startswith('的') or not it['cn'].strip()]
check(not bad_cn, f"没有折断/空的释义（{len(bad_cn)} 条）")
tiny = [it for it in bank if len(it['cn']) == 1]
print(f"  [INFO] 单字释义（正常，如 列/域/秒）：{len(tiny)} 条 {[it['en'] for it in tiny]}")
long_cn = [it for it in bank if len(it['cn']) > 40]
check(len(long_cn) <= 3, f"释义普遍不过长（>40 字仅 {len(long_cn)} 条）")
abbr = [it for it in bank if it['note']]
check(len(abbr) >= 30, f"缩写/全称条目 ≥30（实际 {len(abbr)}）")

# 2. 选项生成可行性（1 正确 + 3 干扰，干扰互不相同）
random.seed(11)
unable = []
for it in random.sample(bank, 120):
    pool = set()
    for o in bank:
        if o['en'] == it['en'] or not o['cn']:
            continue
        if o['cn'] == it['cn']:
            continue
        if o['cn'].split('，')[0] == it['cn'].split('，')[0]:
            continue
        pool.add(o['cn'])
    if len(pool) < 3:
        unable.append(it['en'])
check(not unable, f"随机 120 个词都能选出 3 个不同干扰项（失败 {len(unable)}：{unable[:5]}）")


def _ncn(s):
    """与网页 normCn 等价的简化规范化：去掉标点与空白"""
    for ch in '，；;／/,、\u3000 ．。·（）()“”"\'':
        s = s.replace(ch, '')
    return s.strip()


# 2b. 干扰项无歧义：排除"同义 / 互为子串"后，每个词仍能凑出 3 个干扰项
amb = []
for it in bank:
    c = _ncn(it['cn'])
    n = 0
    for o in bank:
        if o['en'] == it['en'] or not o['cn']:
            continue
        d = _ncn(o['cn'])
        if d == c:                                    # 完全相同
            continue
        if len(d) >= 2 and len(c) >= 2 and (d in c or c in d):   # 互为子串
            continue
        if o['cn'].split('，')[0] == it['cn'].split('，')[0]:
            continue
        n += 1
    if n < 3:
        amb.append(it['en'])
check(not amb, f"全部 {len(bank)} 个词在排除同义/互为子串后仍有 ≥3 个干扰项（不足 {len(amb)} 个：{amb[:5]}）")

# 3. CSV 与 JSON 一致
rows = io.open(csv_path, encoding='utf-8-sig').read().strip().split('\n')[1:]
check(len(rows) == len(bank), f"词库.csv 行数与词库.json 一致（{len(rows)} vs {len(bank)}）")

# 4. 生成的网页
html = io.open(HTML, encoding='utf-8').read()
check('const BANK=' in html, "网页内嵌了词库")
m = re.search(r'"cn":', html)
check(m is not None, "词库 JSON 结构正常")
check(html.count('"id": "w') == len(bank), f"网页内嵌词条数 == {len(bank)}")
for kw, desc in (('resumeIndex', '进度记忆'), ('buildOptions', '选项随机生成'),
                 ('checkMeaning', '考试判定'), ('var EXAM_NUM=20', '考试 20 题'),
                 ('错词本', '错词本'), ('填', '填空提示')):
    check(kw in html, f"网页包含「{desc}」逻辑（{kw}）")
check('\\u00a7' in html or '\u00a7' in html, "网页包含省略号等价判定")
check('EXAM_SCORE=5' in html, "考试每题 5 分")

# 5. 省略号/多义项判定（与网页实现同规则，用 Python 复核一遍）
def norm(s):
    s = re.sub(r'[\s\u3000]+', '', str(s or ''))
    s = s.replace('（', '(').replace('）', ')')
    s = re.sub(r'[，,、]+', '，', s)
    s = re.sub(r'[；;]+', '；', s)
    s = re.sub(r'[。.]+$', '', s)
    s = re.sub(r'…+|\.{2,}|。{2,}', '\u00a7', s)
    return s


def same(user, ref):
    c, u = norm(ref), norm(user)
    if not u:
        return False
    if u == c:
        return True
    if '\u00a7' in c:
        u2 = re.sub(r'什么|[xXＸｘ]{2,}|某(个|些)?', '\u00a7', u)
        if u2 == c:
            return True
    return False


def meaning_ok(user, cn):
    if same(user, cn):
        return True
    return any(same(user, s) for s in re.split(r'[，,；;/]', cn) if s.strip())


cases = [('改变…的位置', '改变…的位置', True), ('改变什么的位置', '改变…的位置', True),
         ('改变...的位置', '改变…的位置', True), ('改变XX的位置', '改变…的位置', True),
         ('改变的位置', '改变…的位置', False), ('陪同', '陪同，伴随', True),
         ('论据', '论据；参数', True), (' 绝对 ', '绝对', True), ('绝对。', '绝对', True),
         ('保存', '绝对', False), ('', '绝对', False)]
for user, ref, want in cases:
    got = meaning_ok(user, ref)
    check(got == want, f"判定：{user!r} vs {ref!r} → {got}（期望 {want}）")

print()
print("结果：" + ("全部通过 ✔" if not fails else f"{len(fails)} 项失败 ✘"))
sys.exit(1 if fails else 0)
