# -*- coding: utf-8 -*-
r"""工具箱自身覆盖率看板（D4）。

要回答的问题：**"自检条数看着不少，但关键判定到底测没测？"**
所以这里不只看"跑过没跑过"，而是把三样摆在一屏：

  1) 每个脚本的断言数与通过/失败（真跑一遍 `--selftest`，不是读文档里的数字）
  2) 有没有 `--selftest` 这条出口（没有的脚本 = 自检空白区）
  3) 变异测试覆盖了哪几条判定（`Toolbox.exe --mutation-selftest`，可选）

数字**全部来自实际运行**，一个都不写死 —— 写死的看板只会跟着代码一起烂。

用法：
    python 本地工具\工具箱\检查\覆盖率\coverage_board.py
    python ...\coverage_board.py --with-mutation      # 连变异测试一起跑（慢 ~30s）
    python ...\coverage_board.py --json
    python ...\coverage_board.py --selftest
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time

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
TOOLBOX = os.path.abspath(os.path.join(HERE, '..', '..'))
ROOT = find_root(HERE) or os.path.abspath(os.path.join(TOOLBOX, '..', '..'))
PY = sys.executable or 'python'

# 要统计的范围（相对工具箱根）
SCOPES = ['检查/*/*.py', '程序/工程模板/*.py', '速查数据/tools/*.py']

RE_PASS_FAIL = re.compile(r'(\d+)\s*(?:项)?通过\s*/\s*(\d+)\s*(?:项)?失败')
RE_FAIL_ONLY = re.compile(r'失败项\s*(\d+)')
# 各脚本输出格式不统一：有 `[PASS] x`、也有 `  PASS  x`（无方括号）。
# 只认一种就会把后者的断言数算成 0 —— 看板自己先被这个坑坑过。
RE_PASS_LINE = re.compile(r'^\s*\[?PASS\]?\s', re.M)
RE_FAIL_LINE = re.compile(r'^\s*\[?FAIL\]?\s', re.M)


def discover():
    out = []
    for pat in SCOPES:
        for p in sorted(glob.glob(os.path.join(TOOLBOX, pat))):
            try:
                with open(p, encoding='utf-8', errors='ignore') as f:
                    txt = f.read()
            except OSError:
                continue
            if '--selftest' in txt:
                out.append(p)
    return out


def parse_result(text):
    """从自检输出里取 (通过, 失败)。认三种写法，取最后一处。"""
    m = RE_PASS_FAIL.findall(text)
    if m:
        a, b = m[-1]
        return int(a), int(b)
    m = RE_FAIL_ONLY.findall(text)
    if m:
        fails = int(m[-1])
        return len(RE_PASS_LINE.findall(text)), fails
    return len(RE_PASS_LINE.findall(text)), len(RE_FAIL_LINE.findall(text))


def run_one(path, timeout=240):
    rel = os.path.relpath(path, TOOLBOX)
    t0 = time.time()
    try:
        p = subprocess.run([PY, path, '--selftest'], capture_output=True, text=True,
                           encoding='utf-8', errors='replace', timeout=timeout,
                           cwd=os.path.dirname(path))
        out = (p.stdout or '') + (p.stderr or '')
        rc = p.returncode
    except subprocess.TimeoutExpired:
        return {'script': rel, 'ok': False, 'skipped': False, 'passed': 0, 'failed': 0,
                'seconds': round(time.time() - t0, 1), 'note': '超时（>%ds）' % timeout}
    except OSError as e:
        return {'script': rel, 'ok': False, 'skipped': False, 'passed': 0, 'failed': 0,
                'seconds': 0.0, 'note': '启动失败：%s' % e}
    passed, failed = parse_result(out)
    # 退出码 3 = 脚本自己声明"跳过"（本机缺外部工具链，如没装 Keil 时的 armcc 编译校验）。
    # 这不是失败，看板里单列，别把它算红。
    if rc == 3:
        return {'script': rel, 'ok': True, 'skipped': True, 'passed': passed,
                'failed': failed, 'seconds': round(time.time() - t0, 1),
                'note': '跳过（本机缺外部工具链）'}
    return {'script': rel, 'ok': (rc == 0 and failed == 0), 'skipped': False, 'passed': passed,
            'failed': failed, 'seconds': round(time.time() - t0, 1),
            'note': '' if rc == 0 else '退出码 %d' % rc}


def mutation_info():
    """跑 Toolbox --mutation-selftest，取"共 N 处，未变红 M 处"。"""
    exe = os.path.join(TOOLBOX, '程序', '嵌入式工具箱', 'build', 'release', 'Toolbox.exe')
    if not os.path.isfile(exe):
        return {'available': False, 'note': '没编译过 Toolbox.exe'}
    try:
        p = subprocess.run([exe, '--mutation-selftest'], capture_output=True, text=True,
                           encoding='utf-8', errors='replace', timeout=600)
        out = (p.stdout or '') + (p.stderr or '')
    except Exception as e:
        return {'available': False, 'note': '跑不起来：%s' % e}
    m = re.search(r'共\s*(\d+)\s*处[，,]\s*未变红\s*(\d+)\s*处', out)
    items = re.findall(r'\[(PASS|FAIL)\]\s*改坏[：:]\s*(.+)', out)
    if not m:
        return {'available': False, 'note': '输出里没找到"共 N 处"'}
    return {'available': True, 'total': int(m.group(1)), 'not_red': int(m.group(2)),
            'items': [{'ok': k == 'PASS', 'what': w.strip()} for k, w in items]}


def build_board(with_mutation=False):
    rows = []
    for p in discover():
        rows.append(run_one(p))
    total_p = sum(r['passed'] for r in rows)
    total_f = sum(r['failed'] for r in rows)
    total_s = sum(1 for r in rows if r.get('skipped'))
    return {'scripts': rows, 'total_passed': total_p, 'total_failed': total_f,
            'total_skipped': total_s,
            'all_ok': all(r['ok'] for r in rows),
            'mutation': mutation_info() if with_mutation else {'available': False,
                                                               'note': '未跑（加 --with-mutation）'}}


def render_md(b):
    L = ['# 工具箱自身覆盖率看板', '',
         '> 数字全部来自**实际跑一遍** `--selftest`，没有一个是写死的。', '',
         '| 脚本 | 断言 | 通过 | 失败 | 耗时 | 状态 |', '|---|---|---|---|---|---|']
    for r in b['scripts']:
        L.append('| `%s` | %d | %d | %d | %.1fs | %s |'
                 % (r['script'], r['passed'] + r['failed'], r['passed'], r['failed'],
                    r['seconds'],
                    '⏭ 跳过' if r.get('skipped')
                    else ('✅' if r['ok'] else '❌ ' + (r['note'] or ''))))
    L.append('| **合计** | **%d** | **%d** | **%d** | | %s |'
             % (b['total_passed'] + b['total_failed'], b['total_passed'],
                b['total_failed'],
                ('⏭ 跳过 %d 项 · ' % b.get('total_skipped', 0) if b.get('total_skipped')
                 else '') + ('✅' if b['all_ok'] else '❌')))
    L.append('')
    mu = b.get('mutation') or {}
    if mu.get('available'):
        L += ['## 变异测试（把判定改坏，自检必须变红）', '',
              '- 共 **%d** 处，未变红 **%d** 处 → %s'
              % (mu['total'], mu['not_red'], '通过' if mu['not_red'] == 0 else '有问题'), '']
        for it in mu.get('items', []):
            L.append('- %s %s' % ('✅' if it['ok'] else '❌', it['what']))
        L.append('')
    else:
        L += ['## 变异测试', '', '- %s' % (mu.get('note') or '未跑'), '']
    # 空白区：有检查目录但没有自检的脚本
    no_self = []
    for pat in SCOPES:
        for p in sorted(glob.glob(os.path.join(TOOLBOX, pat))):
            rel = os.path.relpath(p, TOOLBOX)
            if any(r['script'] == rel for r in b['scripts']):
                continue
            if os.path.basename(p).startswith('_'):
                continue
            no_self.append(rel)
    if no_self:
        L += ['## 自检空白区（没有 --selftest 出口）', '']
        for x in no_self:
            L.append('- `%s`' % x)
        L.append('')
    return '\n'.join(L)


def selftest():
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    ck('解析「N 通过 / M 失败」', parse_result('…自检：12 通过 / 1 失败') == (12, 1))
    ck('解析「失败项 0」并数 [PASS]',
       parse_result('[PASS] a\n[PASS] b\n失败项 0') == (2, 0))
    ck('无方括号的 `  PASS  x` 也算（编码守卫/链路仿真那种格式）',
       parse_result('  PASS  a\n  PASS  b\n失败项 0') == (2, 0))
    ck('`自检：PASS` 这种没有数字的也要数出断言',
       parse_result('  PASS  a\n  PASS  b\n链路仿真自检：PASS') == (2, 0))
    ck('解析「N 项通过 / M 项失败」',
       parse_result('同步检查：21 项通过 / 0 项失败 / 0 项跳过') == (21, 0))
    ck('解析带冒号的中文前缀',
       parse_result('构建烧录自检：26 通过 / 0 失败') == (26, 0))
    ck('解析失败项 >0', parse_result('[PASS] a\n[FAIL] b\n失败项 1') == (1, 1))

    found = discover()
    ck('发现带 --selftest 的脚本（≥10）', len(found) >= 10, len(found))
    ck('发现的都是真实文件', all(os.path.isfile(p) for p in found))

    # 真跑一个最快的脚本，验证"跑→解析"整条链（挑进度扫描，几秒内出结果）
    fast = [p for p in found if p.endswith('progress_scan.py')]
    if fast:
        r = run_one(fast[0], timeout=180)
        ck('真跑一个脚本并解析出断言数', r['passed'] > 0, r)
        ck('真跑结果与退出码一致', (r['failed'] == 0) == (r['note'] == '' or '退出码 0' in r['note']), r)

    # 看板渲染：表头与合计行都在
    fake = {'scripts': [{'script': 'x.py', 'ok': True, 'passed': 3, 'failed': 0,
                         'seconds': 1.0, 'note': ''}],
            'total_passed': 3, 'total_failed': 0, 'all_ok': True,
            'mutation': {'available': False, 'note': '未跑'}}
    md = render_md(fake)
    ck('看板有表头与合计行', '| 脚本 |' in md and '**合计**' in md)
    ck('看板标明"数字来自实际运行"', '实际跑一遍' in md)

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('覆盖率看板自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='工具箱自身覆盖率看板（D4）')
    ap.add_argument('--with-mutation', action='store_true')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--write', default='', help='把看板写到这个 md')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    b = build_board(a.with_mutation)
    md = render_md(b)
    if a.write:
        os.makedirs(os.path.dirname(os.path.abspath(a.write)), exist_ok=True)
        with open(a.write, 'w', encoding='utf-8', newline='\n') as f:
            f.write(md)
        print('看板：%s' % a.write)
    if a.json:
        print(json.dumps(b, ensure_ascii=False))
    else:
        for r in b['scripts']:
            print('  %s %-46s %3d 断言 %6.1fs %s'
                  % ('OK ' if r['ok'] else 'FAIL', r['script'],
                     r['passed'] + r['failed'], r['seconds'], r['note']))
        print('合计：%d 断言，%d 失败 → %s'
              % (b['total_passed'] + b['total_failed'], b['total_failed'],
                 '全部通过' if b['all_ok'] else '有失败'))
        mu = b.get('mutation') or {}
        if mu.get('available'):
            print('变异测试：共 %d 处，未变红 %d 处' % (mu['total'], mu['not_red']))
        else:
            print('变异测试：%s' % (mu.get('note') or '未跑'))
    sys.exit(0 if b['all_ok'] else 1)


if __name__ == '__main__':
    main()
