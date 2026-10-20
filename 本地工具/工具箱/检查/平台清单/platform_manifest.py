# -*- coding: utf-8 -*-
r"""平台清单（B4）——一页列出这套工具箱的**全部家底**，取代"靠翻文档 + 靠记"。

要回答的问题：「现在平台上有哪些检查器？谁进了闸门？谁有人守？自检到底几条？」
以前这些答案散在 4 个地方：`说明.md` 的页面表、`main.cpp` 的检查器表、
各插件目录的 `--manifest`、覆盖率看板 —— 少改一处就"文档写着一套、实际另一套"。

本页把四路**现场取数**捏成一屏：

  1) 插件页（`检查/<名>/check.py` + `--manifest`）—— 工具箱启动时就是这么扫的，
     调 `检查/_manifest_all.py` 一次读完，所以"清单 = 侧栏真有什么"；
  2) 闸门（工具箱 `--selftest` 里跑哪些脚本）—— 直接**解析 `main.cpp` 的检查器表**，
     再加上 B3 起**自动发现**的插件壳；不是抄文档；
  3) 每支的**自检断言数**（真跑 `--selftest`，退出码 3 = 本机缺工具链 → 记跳过）；
  4) **变异测试覆盖**（`main.cpp` 的 muts 表：这支脚本有没有"改坏判定→自检必须变红"的
     那一条；有 `--with-mutation` 时连工具箱 `--mutation-selftest` 一起跑）。

**一个数字都不写死**：脚本删了、名字改了、断言少了，这一页立刻跟着变。

用法：
    python 本地工具\工具箱\检查\平台清单\platform_manifest.py
    python ...\platform_manifest.py --write 产物\平台清单.md
    python ...\platform_manifest.py --no-run          # 不真跑各脚本（只看结构，秒出）
    python ...\platform_manifest.py --with-mutation   # 连变异测试一起跑（慢）
    python ...\platform_manifest.py --json
    python ...\platform_manifest.py --selftest
"""
import argparse
import importlib.util
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


HERE = os.path.dirname(os.path.abspath(__file__))            # 工具箱/检查/平台清单
CHECKS = os.path.dirname(HERE)                               # 工具箱/检查
TOOLBOX = os.path.dirname(CHECKS)                            # 工具箱
ROOT = find_root(HERE) or os.path.abspath(os.path.join(TOOLBOX, '..', '..'))
PY = sys.executable or 'python'
MAIN_CPP = os.path.join(TOOLBOX, '程序', '嵌入式工具箱', 'main.cpp')
MANIFEST_ALL = os.path.join(CHECKS, '_manifest_all.py')
REPORT_REL = os.path.join('产物', '平台清单.md')

# 闸门表：main.cpp 里 `{"名称", "检查/xxx.py"}` 这种两元素行。
RE_GATE = re.compile(r'\{"([^"]+)",\s*"(检查/[^"]+\.py|程序/[^"]+\.py)"\}')
# 变异表：`{"检查/xxx.py", "改前", "改后", "改坏的是什么"}`（跨行，\s 能跨行）
RE_MUT = re.compile(r'\{"(检查/[^"]+\.py)",\s*"[^"]*",\s*"[^"]*",\s*"([^"]+)"\}')


def relpath(p):
    try:
        return os.path.relpath(p, TOOLBOX).replace('\\', '/')
    except ValueError:
        return str(p).replace('\\', '/')


# ---------------------------------------------------------------- 四路取数

def _run(cmd, timeout=180, cwd=None):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8',
                           errors='replace', timeout=timeout, cwd=cwd)
        return p.returncode, (p.stdout or '') + (p.stderr or '')
    except (OSError, subprocess.TimeoutExpired) as e:
        return -1, str(e)


def _last_json(text):
    for line in reversed((text or '').strip().splitlines()):
        try:
            return json.loads(line)
        except ValueError:
            continue
    return None


def discover_plugins():
    """插件页清单 —— 与工具箱启动时同一份口径（`_manifest_all.py` 一次读完）。"""
    rc, out = _run([PY, MANIFEST_ALL], cwd=CHECKS)
    arr = _last_json(out)
    if not isinstance(arr, list):
        return []
    rows = []
    for m in arr:
        if not isinstance(m, dict) or not m.get('name'):
            continue
        rows.append({'name': m.get('name'), 'group': m.get('group') or '检查',
                     'script': m.get('script') or '', 'report': m.get('report') or ''})
    return rows


def parse_main_cpp():
    """解析 main.cpp：闸门表条目 + 变异表条目。返回 (gate, muts)；读不到返回 ([], [])。

    为什么不问工具箱要：闸门表是 C++ 里的字面量，`--selftest` 的**人读输出**没带脚本路径。
    直接读源文件反而是最不容易过期的口径 —— 表改了，这一页跟着变。
    """
    try:
        with open(MAIN_CPP, encoding='utf-8', errors='ignore') as f:
            txt = f.read()
    except OSError:
        return [], []
    gate = []
    for name, rel in RE_GATE.findall(txt):
        rel = rel.replace('/', os.sep)
        gate.append({'name': name, 'script': rel,
                     'exists': os.path.isfile(os.path.join(TOOLBOX, rel))})
    muts = {}
    for rel, what in RE_MUT.findall(txt):
        muts.setdefault(rel, []).append(what)      # 保持正斜杠，与 relpath() 的输出一致
    return gate, muts


def _coverage_board():
    """借覆盖率看板的"跑 → 解析断言数"那套正则（一处实现，别抄第二份）。

    它是个脚本不是包，用 importlib 从路径装；装不上就退回本文件自带的简化解析。
    """
    p = os.path.join(CHECKS, '覆盖率', 'coverage_board.py')
    if not os.path.isfile(p):
        return None
    try:
        spec = importlib.util.spec_from_file_location('coverage_board', p)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod
    except Exception:
        return None


RE_PASS_FAIL = re.compile(r'(\d+)\s*(?:项)?通过\s*/\s*(\d+)\s*(?:项)?失败')
RE_FAIL_ONLY = re.compile(r'失败项\s*(\d+)')
RE_PASS_LINE = re.compile(r'^\s*\[?PASS\]?\s', re.M)
RE_FAIL_LINE = re.compile(r'^\s*\[?FAIL\]?\s', re.M)


def parse_result(text):
    """没有 coverage_board 时的退路（认三种写法，与它一致）。"""
    m = RE_PASS_FAIL.findall(text)
    if m:
        a, b = m[-1]
        return int(a), int(b)
    m = RE_FAIL_ONLY.findall(text)
    if m:
        return len(RE_PASS_LINE.findall(text)), int(m[-1])
    return len(RE_PASS_LINE.findall(text)), len(RE_FAIL_LINE.findall(text))


def run_selftest(script_abs, cb=None, timeout=240):
    """真跑一支脚本的 --selftest，返回断言数与状态（退出码 3 = 跳过，不算红）。"""
    r = {'passed': 0, 'failed': 0, 'seconds': 0.0, 'ok': False, 'skipped': False, 'note': ''}
    if not os.path.isfile(script_abs):
        r['note'] = '脚本不存在'
        return r
    if cb is not None:
        return cb.run_one(script_abs, timeout=timeout)
    t0 = time.time()
    rc, out = _run([PY, script_abs, '--selftest'], timeout=timeout,
                   cwd=os.path.dirname(script_abs))
    r['seconds'] = round(time.time() - t0, 1)
    if rc < 0:
        r['note'] = '跑不起来（%s）' % out.strip()[:60]
        return r
    passed, failed = parse_result(out)
    r.update({'passed': passed, 'failed': failed,
              'ok': rc == 0 and failed == 0, 'skipped': rc == 3,
              'note': '跳过（本机缺外部工具链）' if rc == 3
                      else ('' if rc == 0 else '退出码 %d' % rc)})
    return r


def collect(with_mutation=False, no_run=False):
    """把四路数捏成一张清单。返回 board dict。"""
    cb = _coverage_board() if not no_run else None
    plugins = discover_plugins()
    gate, muts = parse_main_cpp()

    items = []
    seen = set()
    for p in plugins:
        abs_p = p['script']
        items.append({'kind': '插件页', 'name': p['name'], 'group': p['group'],
                      'entry': relpath(abs_p),
                      'gate': '自动发现（加目录即进，不改 C++）',
                      'script_abs': abs_p})
        seen.add(os.path.normcase(os.path.abspath(abs_p)) if abs_p else '')
    for g in gate:
        abs_p = os.path.join(TOOLBOX, g['script'])
        key = os.path.normcase(os.path.abspath(abs_p))
        if key in seen:
            continue                    # 插件壳已在上面列过，别列两次
        seen.add(key)
        items.append({'kind': '闸门脚本', 'name': g['name'], 'group': '—',
                      'entry': relpath(abs_p), 'gate': 'main.cpp 显式表',
                      'script_abs': abs_p})

    for it in items:
        rel = relpath(it['script_abs'])            # 正斜杠，与 muts 的键一致
        it['mutation'] = muts.get(rel, [])
        it['in_gate'] = True            # 上面两个来源就是闸门本身
        if no_run:
            it['run'] = {'passed': 0, 'failed': 0, 'seconds': 0.0, 'ok': True,
                         'skipped': False, 'note': '未跑（--no-run）'}
        else:
            it['run'] = run_selftest(it['script_abs'], cb)
            it['in_gate'] = it['run']['ok'] or it['run']['skipped']

    tot_p = sum(i['run']['passed'] for i in items)
    tot_f = sum(i['run']['failed'] for i in items)
    n_skip = sum(1 for i in items if i['run']['skipped'])
    n_mut = sum(1 for i in items if i['mutation'])
    n_plug = sum(1 for i in items if i['kind'] == '插件页')
    ok = all(i['run']['ok'] for i in items) if not no_run else True

    mu = {'available': False, 'note': '未跑（加 --with-mutation）'}
    if with_mutation:
        exe = os.path.join(TOOLBOX, '程序', '嵌入式工具箱', 'build', 'release', 'Toolbox.exe')
        if not os.path.isfile(exe):
            mu = {'available': False, 'note': '没编译过 Toolbox.exe'}
        else:
            rc, out = _run([exe, '--mutation-selftest'], timeout=600)
            m = re.search(r'共\s*(\d+)\s*处[，,]\s*未变红\s*(\d+)\s*处', out)
            mu = ({'available': True, 'total': int(m.group(1)), 'not_red': int(m.group(2)),
                   'ok': int(m.group(2)) == 0} if m
                  else {'available': False, 'note': '输出里没找到"共 N 处"'})

    return {'tool': '平台清单', 'schema': 1, 'ok': ok,
            'items': items, 'plugins': len(plugins), 'gated': len(items),
            'plugin_pages': n_plug, 'total_passed': tot_p, 'total_failed': tot_f,
            'total_skipped': n_skip, 'mutation_covered': n_mut,
            'no_run': no_run, 'mutation': mu}


def render_md(b):
    L = ['# 平台清单', '',
         '> 一页看全：有哪些插件页 / 闸门脚本、自检几条、谁有人守。',
         '> 数字**全部现场取**（读 manifest、解析 main.cpp、真跑 `--selftest`），一个都不写死。', '',
         '| 形态 | 名称 | 分组 | 入口 | 自检断言 | 进闸门 | 变异覆盖 |', '|---|---|---|---|---|---|---|']
    for it in b['items']:
        r = it['run']
        assert_txt = '未跑' if r.get('note') == '未跑（--no-run）' \
            else '%d 通过 / %d 失败' % (r['passed'], r['failed'])
        if r['skipped']:
            assert_txt = '%d 断言 · ⏭ 跳过' % (r['passed'] + r['failed'])
        gate_txt = ('✅ ' + it['gate']) if it['in_gate'] else ('❌ ' + (r['note'] or '不通过'))
        mut_txt = '—'
        if it['mutation']:
            mut_txt = '✅ %d 条' % len(it['mutation'])
        L.append('| %s | %s | %s | `%s` | %s | %s | %s |'
                 % (it['kind'], it['name'], it['group'], it['entry'], assert_txt,
                    gate_txt, mut_txt))
    L.append('')
    L.append('- 共 **%d** 项（插件页 **%d**）：自检断言 **%d** 通过 / **%d** 失败；'
             '跳过 %d 项；变异覆盖 **%d** 项。'
             % (b['gated'], b['plugin_pages'], b['total_passed'], b['total_failed'],
                b['total_skipped'], b['mutation_covered']))
    L.append('')
    mu = b.get('mutation') or {}
    L += ['## 变异测试（把判定改坏 → 自检必须变红）', '']
    if mu.get('available'):
        L.append('- 共 **%d** 处，未变红 **%d** 处 → %s'
                 % (mu['total'], mu['not_red'], '通过' if mu.get('ok') else '有问题'))
    else:
        L.append('- %s' % (mu.get('note') or '未跑'))
    L.append('')
    L += ['## 谁有人守（有变异覆盖的脚本）', '']
    for it in b['items']:
        if not it['mutation']:
            continue
        L.append('- `%s`：%s' % (it['entry'], '；'.join(it['mutation'])))
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

    gate, muts = parse_main_cpp()
    ck('能解析 main.cpp 得到闸门表（≥15 项）', len(gate) >= 15, str(len(gate)))
    ck('闸门表里的脚本都真实存在',
       all(g['exists'] for g in gate),
       str([g['script'] for g in gate if not g['exists']][:3]))
    ck('解析出变异表（≥3 条改坏点）', sum(len(v) for v in muts.values()) >= 3,
       str(sum(len(v) for v in muts.values())))
    ck('每条变异都有"改坏的是哪条判定"的说明',
       all(w.strip() for v in muts.values() for w in v))

    plugins = discover_plugins()
    ck('插件发现链能跑（≥6 个插件页）', len(plugins) >= 6, str(len(plugins)))
    ck('插件清单带 name / group / script',
       all(p['name'] and p['group'] and p['script'] for p in plugins), '')

    cb = _coverage_board()
    ck('能借到覆盖率看板的解析器（一处实现，不抄第二份）', cb is not None)
    if cb is not None:
        ck('借来的解析器口径一致', cb.parse_result('自检：12 通过 / 1 失败') == (12, 1))
        ck('本文件的退路解析器口径一致', parse_result('自检：12 通过 / 1 失败') == (12, 1))

    # 真跑一支最快的，验"跑 → 解析 → 记断言数"整条链
    fast = [os.path.join(TOOLBOX, g['script']) for g in gate
            if g['script'].endswith('progress_scan.py')]
    if fast:
        r = run_selftest(fast[0], cb)
        ck('真跑一支并解析出断言数', r['passed'] > 0, str(r))
    else:
        ck('闸门表里有进度扫描（用最快的那支验整条链）', False)

    # 反例：不存在的脚本必须被判红（不能悄悄记成"通过"）
    rbad = run_selftest(os.path.join(TOOLBOX, '检查', '不存在的目录', 'no_such.py'), cb)
    ck('反例：不存在的脚本判红并说明', (not rbad['ok']) and rbad['note'], str(rbad))

    b = collect(no_run=True)
    md = render_md(b)
    ck('清单有表头与合计行', '| 形态 |' in md and '共 **' in md)
    ck('清单标明"数字现场取、不写死"', '一个都不写死' in md)
    ck('插件页在清单里按"自动发现"记进闸门',
       any(i['kind'] == '插件页' and '自动发现' in i['gate'] for i in b['items']))
    # 变异表与条目**能对上**（正斜杠/反斜杠一错就全变成"—"，这条就是钉这个的）
    ck('变异覆盖能对到脚本条目（≥3 项）', b['mutation_covered'] >= 3,
       '实际对上 %d 项' % b['mutation_covered'])

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('平台清单自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='平台清单（B4）：全部插件/脚本一页看全')
    ap.add_argument('--write', default='', help='把清单写到这个 md')
    ap.add_argument('--no-run', action='store_true', help='不真跑各脚本（只看结构）')
    ap.add_argument('--with-mutation', action='store_true', help='连变异测试一起跑（慢）')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    b = collect(a.with_mutation, a.no_run)
    md = render_md(b)
    if a.write:
        out = os.path.abspath(a.write)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'w', encoding='utf-8', newline='\n') as f:
            f.write(md)
        print('清单：%s' % out)
    if a.json:
        print(json.dumps(b, ensure_ascii=False))
    else:
        print('平台清单：%d 项（插件页 %d）｜自检断言 %d 通过 / %d 失败｜跳过 %d｜变异覆盖 %d'
              % (b['gated'], b['plugin_pages'], b['total_passed'], b['total_failed'],
                 b['total_skipped'], b['mutation_covered']))
        for it in b['items']:
            r = it['run']
            print('  %s %-14s %-28s %4d 断言  %s'
                  % (it['kind'], it['name'], it['entry'],
                     r['passed'] + r['failed'],
                     ('⏭ 跳过' if r['skipped'] else ('✅' if r['ok'] else '❌ ' + (r['note'] or '')))))
        mu = b.get('mutation') or {}
        print('变异测试：%s' % ('共 %d 处，未变红 %d 处' % (mu['total'], mu['not_red'])
                                if mu.get('available') else (mu.get('note') or '未跑')))
    sys.exit(0 if b['ok'] else 1)


if __name__ == '__main__':
    main()
