# -*- coding: utf-8 -*-
r"""阶段自动判定（A1）：按代码现状判「阶段做到哪了」，并回写进度文档的阶段总表。

用途：把「我自己记着做到第几阶段」变成「机器判」。判据来自
      `速查数据/rules/stage_checks.json`（换板/换工程只改这一份）。

只做三件事，不越界：
  1) 只判**可机器判**的阶段（关键 API 有没有被调用、文件在不在、TODO 桩还在不在）；
  2) 判不了的（联调、界面好不好看、数值对不对）标「需人工确认」，**不假装能判**；
  3) `--apply` 只改阶段总表的「状态」「证据」两列，**manual 阶段整行不碰**（那是你手填的）。

默认是 dry-run（只打印，不写文件）；加 `--apply` 才回写。

用法：
    python 本地工具\工具箱\检查\阶段判定\stage_status.py
    python ...\stage_status.py --board 板1
    python ...\stage_status.py --apply
    python ...\stage_status.py --json
    python ...\stage_status.py --selftest
"""
import argparse
import glob
import json
import os
import re
import sys
import tempfile

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass


def find_root(start):
    d = os.path.abspath(start)
    while True:
        if os.path.isdir(os.path.join(d, 'EnvMonitor')):
            return d
        p = os.path.dirname(d)
        if p == d:
            return None
        d = p


HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = find_root(HERE) or os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
RULES_REL = os.path.join('本地工具', '工具箱', '速查数据', 'rules', 'stage_checks.json')

STATUS_DONE = '☑ 完成（自动）'
STATUS_DOING = '◐ 进行中'
STATUS_TODO = '☐ 未开始'
STATUS_MANUAL = '⚠ 需人工确认'


def load_rules(root):
    p = os.path.join(root, RULES_REL)
    with open(p, encoding='utf-8') as f:
        return json.load(f), p


def read_texts(root, rels):
    """把若干相对路径读成 (合并文本, 实际读到的文件列表)。找不到的跳过（不报错）。"""
    parts, found = [], []
    for rel in rels:
        p = os.path.join(root, rel)
        if not os.path.isfile(p):
            continue
        try:
            with open(p, 'rb') as f:
                raw = f.read()
        except OSError:
            continue
        for enc in ('utf-8', 'gbk'):
            try:
                parts.append(raw.decode(enc))
                found.append(rel)
                break
            except UnicodeDecodeError:
                continue
    return '\n'.join(parts), found


def strip_comments(s):
    """去掉 C/C++ 注释，再拿来做"API 有没有被调用"的匹配。

    为什么必须去：骨架里每段注释都写着 `SYS_FRAME_BuildEnv()` / `LORA_E22_Send()` 这类
    提示语 —— 不剥注释的话，**一行代码没写也会被判成"阶段已完成"**（踩过）。
    副作用：字符串里的 `//` 会被当注释截断（本工程里极少见），可接受。
    """
    s = re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)
    s = re.sub(r'//[^\n]*', ' ', s)
    return s


def _has_api(text, name):
    """整词匹配（避免 SYS_USART_Init 命中 SYS_USART_InitRxIT 这种前缀误判）。"""
    return re.search(r'(?<![A-Za-z0-9_])' + re.escape(name) + r'(?![A-Za-z0-9_])', text) is not None


def eval_check(chk, text, raw=None):
    """执行一条判据，返回 (是否通过, 一句证据)。

    text = 去注释后的代码（判"有没有真调用"）；raw = 原文（判 TODO 桩还在不在）。
    """
    raw = text if raw is None else raw
    t = chk.get('type')
    if t == 'file_exists':
        return os.path.isfile(os.path.join(ROOT, chk['path'])), chk['path']
    if t == 'pattern':
        hit = re.search(chk['pattern'], text) is not None
        return hit, (chk.get('what') or chk['pattern'])
    if t == 'api':
        names = chk['names']
        miss = [n for n in names if not _has_api(text, n)]
        ok = not miss
        return ok, ('全有：' + ' / '.join(names)) if ok else ('缺：' + ' / '.join(miss))
    if t == 'api_any':
        names = chk['names']
        hit = [n for n in names if _has_api(text, n)]
        need = int(chk.get('min') or 1)
        ok = len(hit) >= need
        ev = '命中 %d/%d（要 ≥%d）' % (len(hit), len(names), need)
        if chk.get('what'):
            ev += '　' + chk['what']
        if ok and len(hit) < len(names):
            ev += '　已接：' + ' / '.join(hit)
        return ok, ev
    if t == 'api_count':
        name = chk['name']
        n = len(re.findall(r'(?<![A-Za-z0-9_])' + re.escape(name) + r'\s*\(', text))
        need = int(chk.get('min') or 1)
        return n >= need, '%s 调用 %d 次（要 ≥%d）' % (name, n, need)
    if t == 'no_todo':
        tag = chk.get('tag') or 'TODO'
        n = raw.count(tag)                 # TODO 在注释里，必须查原文
        return n == 0, ('无 %s' % tag) if n == 0 else ('还剩 %d 处 %s' % (n, tag))
    return False, '未知判据类型：%s' % t


def judge_board(root, name, cfg):
    raw, found = read_texts(root, cfg.get('files') or [])
    text = strip_comments(raw)          # 判"有没有真调用"只看代码，不看注释
    stages = []
    for no in sorted(cfg.get('stages', {}), key=lambda x: int(x)):
        sc = cfg['stages'][no]
        item = {'no': int(no), 'status': '', 'evidence': '', 'checks': []}
        if sc.get('manual'):
            item['status'] = 'manual'
            item['evidence'] = sc['manual']
            stages.append(item)
            continue
        checks = sc.get('checks') or []
        passed = 0
        for chk in checks:
            ok, ev = eval_check(chk, text, raw)
            item['checks'].append({'type': chk.get('type'), 'ok': ok, 'evidence': ev})
            if ok:
                passed += 1
        item['passed'] = passed
        item['total'] = len(checks)
        if not checks:
            item['status'] = 'manual'
            item['evidence'] = '没有配置判据'
        elif passed == len(checks):
            item['status'] = 'done'
            item['evidence'] = '；'.join(c['evidence'] for c in item['checks'])
        elif passed > 0:
            item['status'] = 'doing'
            item['evidence'] = '%d/%d：' % (passed, len(checks)) + '；'.join(
                ('✓' if c['ok'] else '✗') + c['evidence'] for c in item['checks'])
        else:
            item['status'] = 'todo'
            # 证据列是"怎么证明做完了"—— 还没开始就没有证据可填，留空；
            # 缺什么写在 checks 里（--json / 控制台能看到），不往文档里塞噪声
            item['evidence'] = ''
        stages.append(item)
    return {'board': name, 'files': found, 'stages': stages}


# ========================================================================
# 回写进度文档（只动阶段总表的「状态」「证据」两列）
# ========================================================================

def find_table_range(lines):
    """返回阶段总表的数据行下标区间 [start, end)。找不到返回 None。"""
    start = None
    for i, line in enumerate(lines):
        s = line.strip()
        if s.startswith('| 阶段') or s.startswith('| 步骤'):
            start = i + 1
            break
    if start is None:
        return None
    # 跳过分隔行
    while start < len(lines) and set(lines[start].strip()) <= set('|-: '):
        start += 1
    end = start
    while end < len(lines) and lines[end].strip().startswith('|'):
        end += 1
    return start, end


def apply_to_doc(path, stages):
    """把判定结果写回阶段总表。返回 (改动行数, 说明)。manual 阶段整行不碰。"""
    if not os.path.isfile(path):
        return 0, '文档不存在：%s' % path
    with open(path, 'rb') as f:
        raw = f.read()
    enc = 'utf-8'
    try:
        text = raw.decode('utf-8')
    except UnicodeDecodeError:
        enc = 'gbk'
        text = raw.decode('gbk')
    newline = '\r\n' if '\r\n' in text else '\n'
    lines = text.split(newline)

    rng = find_table_range(lines)
    if not rng:
        return 0, '没找到「阶段总表」（表头应是 | 阶段 | 阶段名 | …）'
    start, end = rng

    by_no = {s['no']: s for s in stages}
    changed = 0
    for i in range(start, end):
        cells = lines[i].split('|')
        if len(cells) < 6:
            continue
        try:
            no = int(cells[1].strip())
        except ValueError:
            continue
        st = by_no.get(no)
        if not st:
            continue
        if st['status'] == 'manual':
            # 判不了的阶段：你填过的一律不动；只有还停在默认「☐ 未开始」时，
            # 标一句「需人工确认」—— 否则跑完判定文档一点变化都没有，看不出跑过。
            if cells[3].strip() in ('☐ 未开始', ''):
                if cells[3].strip() != STATUS_MANUAL:
                    cells[3] = ' %s ' % STATUS_MANUAL
                    cells[4] = ' %s ' % (st.get('evidence') or '')
                    lines[i] = '|'.join(cells)
                    changed += 1
            continue
        if st['status'] == 'done':
            status = STATUS_DONE
        elif st['status'] == 'doing':
            status = '%s %d/%d' % (STATUS_DOING, st.get('passed', 0), st.get('total', 0))
        else:
            status = STATUS_TODO
        ev = (st.get('evidence') or '').replace('|', '/')
        if cells[3].strip() == status and cells[4].strip() == ev:
            continue
        cells[3] = ' %s ' % status
        cells[4] = ' %s ' % ev
        lines[i] = '|'.join(cells)
        changed += 1

    if changed:
        with open(path, 'w', encoding=enc, newline='') as f:
            f.write(newline.join(lines))
    return changed, ''


def render_text(boards):
    L = []
    for b in boards:
        L.append('=== %s ===' % b['board'])
        if not b['files']:
            L.append('  ⚠ 没读到任何源文件（判据里的路径对不上？）')
        for s in b['stages']:
            mark = {'done': '✔', 'doing': '◐', 'todo': '·', 'manual': '？'}[s['status']]
            ev = s.get('evidence') or ''
            if not ev and s.get('checks'):
                # 文档里"未开始"不留证据，但控制台要告诉你缺什么
                ev = '缺：' + '；'.join(c['evidence'] for c in s['checks'] if not c['ok'])
            L.append('  %s 阶段 %-2d %s' % (mark, s['no'], ev))
        L.append('')
    return '\n'.join(L)


# ========================================================================
# 自检（合成工程 + 合成文档，不碰真实工程）
# ========================================================================

def selftest():
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), extra if isinstance(extra, str) else str(extra)))
        if not cond:
            ok_all = False

    global ROOT
    tmp = tempfile.mkdtemp(prefix='stage_')
    old_root = ROOT
    try:
        ROOT = tmp
        os.makedirs(os.path.join(tmp, 'proj'), exist_ok=True)
        src = os.path.join(tmp, 'proj', 'main.c')
        with open(src, 'w', encoding='utf-8', newline='\n') as f:
            f.write('int main(void){\n'
                    '  SYS_CLK_ToHighSpeed(); SYS_TICK_Init(); SYS_USART_Init(1, 2); LED_Init();\n'
                    '  xTaskCreate(a,"a",1,0,1,0);\n'
                    '  xTaskCreate(b,"b",1,0,1,0);\n'
                    '  SYS_DHT11_Read(&t, &h);\n'
                    '  SYS_FRAME_BuildEnv(buf, 32, 1,2,3,4,5,6);\n'
                    '}\n')
        cfg = {'files': ['proj/main.c'], 'stages': {
            '1': {'checks': [{'type': 'pattern', 'pattern': r'int\s+main\s*\('}]},
            '2': {'checks': [{'type': 'api', 'names': ['SYS_CLK_ToHighSpeed', 'LED_Init']}]},
            '3': {'checks': [{'type': 'api_count', 'name': 'xTaskCreate', 'min': 5}]},
            '4': {'checks': [{'type': 'api_any', 'names': ['SYS_DHT11_Read', 'SYS_BMP280_Read',
                                                           'SYS_FRAME_BuildEnv'], 'min': 2}]},
            '5': {'manual': '要实测'},
            '6': {'checks': [{'type': 'api', 'names': ['SYS_FRAME_BuildEnv']}]},
            '7': {'checks': [{'type': 'api', 'names': ['LORA_E22_Send']}]},
        }}
        b = judge_board(tmp, '板X', cfg)
        st = {s['no']: s for s in b['stages']}
        ck('pattern 命中 → done', st[1]['status'] == 'done', st[1])
        ck('api 全有 → done', st[2]['status'] == 'done', st[2])
        ck('api_count 不够（2<5）→ todo', st[3]['status'] == 'todo', st[3])
        ck('api_any 2/2 命中 → done', st[4]['status'] == 'done', st[4])
        ck('manual 阶段标 manual', st[5]['status'] == 'manual', st[5])
        ck('全缺 → todo', st[7]['status'] == 'todo', st[7])
        # 前缀误判：SYS_USART_Init 不该被 SYS_USART_InitRxIT 命中
        ck('整词匹配（前缀不算命中）',
           not _has_api('SYS_USART_InitRxIT(x);', 'SYS_USART_Init'))
        ck('整词匹配（真名算命中）', _has_api('SYS_USART_Init(1,2);', 'SYS_USART_Init'))

        # 注释里的 API 不算"用了"：骨架注释写着 SYS_FRAME_BuildEnv() 但一行没写，
        # 不剥注释就会把它判成"阶段已完成"（这是本工具最容易犯的错，必须守住）
        c2 = os.path.join(tmp, 'proj', 'comment_only.c')
        with open(c2, 'w', encoding='utf-8', newline='\n') as f:
            f.write('/* 阶段 6：用 SYS_FRAME_BuildEnv() 组帧\n'
                    ' * 再用 LORA_E22_Send() 发出去 */\n'
                    'int main(void){ /* 空的 */ return 0; }\n')
        cb = judge_board(tmp, '板Y', {'files': ['proj/comment_only.c'], 'stages': {
            '1': {'checks': [{'type': 'api', 'names': ['SYS_FRAME_BuildEnv']}]},
            '2': {'checks': [{'type': 'api', 'names': ['LORA_E22_Send']}]},
        }})
        cst = {s['no']: s for s in cb['stages']}
        ck('注释里的 API 不算实现（阶段 1 仍 todo）', cst[1]['status'] == 'todo', cst[1])
        ck('注释里的 API 不算实现（阶段 2 仍 todo）', cst[2]['status'] == 'todo', cst[2])
        ck('去注释后仍能看见真代码', 'int main' in strip_comments(open(c2, encoding='utf-8').read()))

        # 回写：只改状态/证据两列；manual 行不动；其他内容原样
        doc = os.path.join(tmp, 'doc.md')
        with open(doc, 'w', encoding='utf-8', newline='\n') as f:
            f.write('# 进度文档\n\n## 1. 阶段总表\n\n'
                    '| 阶段 | 阶段名 | 状态 | 证据（怎么证明做完了） | 日期 |\n'
                    '|---|---|---|---|---|\n'
                    '| 1 | 骨架 | ☐ 未开始 |  |  |\n'
                    '| 3 | 任务 | ☐ 未开始 |  |  |\n'
                    '| 5 | 实测 | ☑ 完成（我手填的） | 我看过了 | 10-01 |\n'
                    '| 7 | 发送 | ☐ 未开始 |  |  |\n'
                    '\n## 2. 各阶段明细\n\n正文不许动\n')
        n, err = apply_to_doc(doc, b['stages'])
        with open(doc, encoding='utf-8') as f:
            after = f.read()
        ck('回写只改动"真的变了"的行（完成 1 行；未开始本来就是那样子）',
           n == 1, '%d %s' % (n, err))
        ck('done 写成「☑ 完成（自动）」', '| 1 | 骨架 | ☑ 完成（自动） |' in after, after)
        ck('todo 保持「☐ 未开始」', '| 3 | 任务 | ☐ 未开始 |' in after)
        ck('manual 行原样保留（你手填的 ☑ 不被覆盖）',
           '| 5 | 实测 | ☑ 完成（我手填的） | 我看过了 | 10-01 |' in after)
        # 还没动过的 manual 行：标「需人工确认」，好让"判定跑过了"看得见
        doc3 = os.path.join(tmp, 'doc3.md')
        with open(doc3, 'w', encoding='utf-8', newline='\n') as f:
            f.write('| 阶段 | 阶段名 | 状态 | 证据 | 日期 |\n|---|---|---|---|---|\n'
                    '| 5 | 实测 | ☐ 未开始 |  |  |\n')
        apply_to_doc(doc3, b['stages'])
        with open(doc3, encoding='utf-8') as f:
            after3 = f.read()
        ck('未动过的 manual 行标成「需人工确认」', STATUS_MANUAL in after3, after3)
        ck('表外正文没被改', '正文不许动' in after and after.startswith('# 进度文档'))
        n2, _ = apply_to_doc(doc, b['stages'])
        ck('回写幂等（第二次改动 0 行）', n2 == 0, str(n2))

        # 表头写成「步骤」也要认（骨架文档用「步骤」）
        doc2 = os.path.join(tmp, 'doc2.md')
        with open(doc2, 'w', encoding='utf-8', newline='\n') as f:
            f.write('| 步骤 | 步骤名 | 状态 | 证据 | 日期 |\n|---|---|---|---|---|\n'
                    '| 1 | 时钟 | ☐ 未开始 |  |  |\n')
        n3, _ = apply_to_doc(doc2, b['stages'])
        ck('表头是「步骤」也能回写', n3 == 1, str(n3))
    finally:
        ROOT = old_root
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)

    # 真实配置：每块板的阶段号必须从 0 连续，且与「项目引导」的阶段卡数量一致
    # （Qt 只有 0~7，板1/板2 是 0~9 —— 所以不能一刀切按 0~9 要求）
    try:
        rules, _ = load_rules(ROOT)
        ds_path = os.path.join(ROOT, '本地工具', '工具箱', '速查数据', 'data', 'dataset.json')
        cards = {}
        if os.path.isfile(ds_path):
            with open(ds_path, encoding='utf-8') as f:
                ds = json.load(f)
            for c in ds.get('all', []):
                b = c.get('_b')
                if b:
                    cards[b] = cards.get(b, 0) + 1
        bad = []
        for name, cfg in rules.get('boards', {}).items():
            nos = sorted(int(k) for k in cfg.get('stages', {}))
            if nos != list(range(len(nos))):
                bad.append('%s 阶段号不连续：%s' % (name, nos))
            for no in nos:
                st = cfg['stages'][str(no)]
                if not st.get('checks') and not st.get('manual'):
                    bad.append('%s·阶段%d 空判据' % (name, no))
            if cards.get(name) and cards[name] != len(nos):
                bad.append('%s 判据 %d 个 ≠ 阶段卡 %d 张' % (name, len(nos), cards[name]))
        ck('真实配置：阶段号连续、无空判据、与阶段卡数量一致', not bad, str(bad[:5]))
        # 文档路径都在（否则 --apply 无处可写）
        nodoc = [n for n, c in rules['boards'].items()
                 if not os.path.isfile(os.path.join(ROOT, c.get('doc') or ''))]
        ck('真实配置：doc 路径都存在', not nodoc, str(nodoc))
    except Exception as e:
        ck('真实配置可读', False, repr(e))

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('阶段判定自检：%d 通过 / %d 失败' % (sum(1 for _, c, _ in checks if c),
                                              sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='阶段自动判定 + 回写进度文档')
    ap.add_argument('--board', default='all', help='板1 / 板2 / Qt / all')
    ap.add_argument('--apply', action='store_true', help='把结果写回进度文档（默认只打印）')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    rules, rules_path = load_rules(ROOT)
    boards = []
    for name, cfg in rules.get('boards', {}).items():
        if a.board != 'all' and a.board not in name:
            continue
        boards.append((name, cfg, judge_board(ROOT, name, cfg)))
    if not boards:
        print('没有匹配的板：%s' % a.board)
        sys.exit(1)

    applied = []
    for name, cfg, res in boards:
        if a.apply:
            doc = os.path.join(ROOT, cfg.get('doc') or '')
            n, err = apply_to_doc(doc, res['stages'])
            applied.append({'board': name, 'doc': cfg.get('doc'),
                            'changed': n, 'error': err})

    if a.json:
        print(json.dumps({'tool': '阶段判定', 'schema': 1, 'ok': True, 'rules': rules_path,
                          'boards': [r for _, _, r in boards],
                          'applied': applied}, ensure_ascii=False))
        return

    print('判据：%s' % rules_path)
    print(render_text([r for _, _, r in boards]))
    if a.apply:
        for x in applied:
            if x['error']:
                print('回写 %s：%s' % (x['board'], x['error']))
            else:
                print('回写 %s：改动 %d 行（%s）' % (x['board'], x['changed'], x['doc']))
        print('\n提示：回写后回「项目引导」页选该工程，「下一步」横幅会自动跟到新阶段。')
    else:
        print('（这是 dry-run，没有写文件；加 --apply 才回写）')


if __name__ == '__main__':
    main()
