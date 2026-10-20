# -*- coding: utf-8 -*-
r"""一键验收（C1）—— 一条命令跑完"这轮改动能不能收工"，分「快 / 全」两档。

**病根**：验证链是一长串命令（构建 → `--selftest` → 冒烟 → 截图 → 5 个探针 → UI 量测 →
性能量测 → 变异自测 → 速查重建链 → 三端同步 → Keil 双板复编），全靠人记。
漏跑一环不会报错，只会**悄悄少验一层**；跑完了也说不清"到底是哪一步红"。

**做法**：把它们收成一张**步骤表**，一条命令按档跑完，逐步行 + 出结论：
  - **快档**（默认）= 构建 + 工具箱自检 + 冒烟/截图 + 5 探针 + UI 量测 + 性能量测 + 变异自测；
  - **全档**（`--full`）= 快档 + 速查重建链（`rebuild.bat` 8 环）+ 三端同步检查 + Keil 双板复编。

每一步**独立判定**，所以"故意改坏一处"只会红那一步、其余仍绿，结论里**点名**是哪一步；
本机缺外部工具链（没装 Keil）的步骤记**跳过**（与 `--selftest` 的退出码 3 同一套口径），不算红。
结果归档到 `检查/一键验收/产物/`（最新一份 + 带时间戳的历史一份）。

用法：
    python 本地工具\工具箱\检查\一键验收\verify_all.py                 # 快档
    python ...\verify_all.py --full                                  # 全档
    python ...\verify_all.py --only build,selftest                    # 只跑这几步（调参用）
    python ...\verify_all.py --skip mutation                          # 跳过这几步
    python ...\verify_all.py --no-build                               # 跳过构建（工具箱正开着时用）
    python ...\verify_all.py --json
    python ...\verify_all.py --selftest                               # 验本脚本自己（离线、秒出）

工具箱根目录另有 `一键验收.bat`（双击 = 快档；`一键验收.bat full` = 全档）。
"""
import argparse
import datetime
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


HERE = os.path.dirname(os.path.abspath(__file__))               # 工具箱/检查/一键验收
CHECKS = os.path.dirname(HERE)                                  # 工具箱/检查
TOOLBOX = os.path.dirname(CHECKS)                               # 工具箱
PY = sys.executable or 'python'
EXE = os.path.join(TOOLBOX, '程序', '嵌入式工具箱', 'build', 'release', 'Toolbox.exe')
BUILD_PY = os.path.join(TOOLBOX, '构建.py')
LOOKUP_TOOLS = os.path.join(TOOLBOX, '速查数据', 'tools')
REBUILD_BAT = os.path.join(LOOKUP_TOOLS, 'rebuild.bat')
CHECK_SYNC = os.path.join(LOOKUP_TOOLS, 'check_sync.py')
BUILD_FLASH = os.path.join(CHECKS, '构建烧录', 'build_flash.py')
PRODUCT = os.path.join(HERE, '产物')
SHOT = os.path.join(PRODUCT, '概览截图.png')
REPORT_REL = os.path.join('产物', '一键验收.md')

RE_SELFTEST_FAILS = re.compile(r'失败项\s*(\d+)')
RE_SYNC = re.compile(r'(\d+)\s*(?:项)?通过\s*/\s*(\d+)\s*(?:项)?失败')
RE_COLD = re.compile(r'冷启动[^:：]*[:：]\s*(\d+)\s*ms')
RE_MUT = re.compile(r'共\s*(\d+)\s*处[，,]\s*未变红\s*(\d+)\s*处')
RE_HINT = re.compile(r'FAIL|error:|Error|错误|失败|不通过|Traceback')
# "强"的行优先摘：真正写"谁 FAIL 了"的那些（`FAIL xxx` / `error:` / Traceback）。
RE_HINT_STRONG = re.compile(r'^\s*\[?FAIL\]?|error:|Traceback|不通过')
# "绿"的噪音行不要摘 —— `失败项 0` / `未变红 0 处` / `0 个目标失败` 都含"失败"字样，
# 摘进来会误导（第一次跑就摘到"失败项 0"，看着像失败）。
RE_HINT_SKIP = re.compile(r'失败项\s*(0|\d+\s*/\s*\d+)?\s*$|未变红\s*0|0\s*个目标失败')
RE_SELFTEST_FAILS = re.compile(r'失败项\s*(\d+)')


# ---------------------------------------------------------------- 步骤表

def _default_cwd(argv):
    for a in argv:
        if isinstance(a, str) and (a.endswith('.py') or a.endswith('.exe')) and os.path.exists(a):
            return os.path.dirname(a)
    return TOOLBOX


def _s(key, name, argv, tier='fast', timeout=600, ev='rc', cwd=None):
    return {'key': key, 'name': name, 'argv': argv, 'tier': tier,
            'timeout': timeout, 'ev': ev, 'cwd': cwd or _default_cwd(argv)}


def build_steps():
    """步骤表 —— 全脚本**唯一**一份"验收链有哪些步"的口径（档、超时、怎么判、摘什么）。"""
    return [
        # ---- 快档 ----
        _s('build', '构建（工具箱 + Git上传）', [PY, BUILD_PY], timeout=1200,
           ev='build', cwd=TOOLBOX),
        _s('selftest', '工具箱自检 --selftest', [EXE, '--selftest'], timeout=900, ev='selftest'),
        _s('gui-smoke', '界面冒烟 --gui-smoke', [EXE, '--gui-smoke'], timeout=300),
        _s('shot', '概览截图 --shot-overview', [EXE, '--shot-overview', SHOT], timeout=300,
           ev='shot'),
        _s('probe-palette', '探针 · 全局搜索 --palette-probe', [EXE, '--palette-probe'], timeout=300),
        _s('probe-welcome', '探针 · 首次向导 --welcome-probe', [EXE, '--welcome-probe'], timeout=300),
        _s('probe-clean', '探针 · 清理口径 --clean-probe', [EXE, '--clean-probe'], timeout=300),
        _s('probe-finding', '探针 · 命中项 --finding-probe', [EXE, '--finding-probe'], timeout=300),
        _s('probe-thresholds', '探针 · 阈值口径 --thresholds-probe', [EXE, '--thresholds-probe'],
           timeout=300),
        _s('ui-audit', 'UI 规范量测 --ui-audit', [EXE, '--ui-audit'], timeout=300),
        _s('perf', '性能量测 --perf-probe', [EXE, '--perf-probe'], timeout=300, ev='perf'),
        _s('mutation', '变异自测 --mutation-selftest', [EXE, '--mutation-selftest'], timeout=900,
           ev='mutation'),
        # ---- 全档额外 ----
        _s('lookup-rebuild', '速查重建链 rebuild.bat（8 环）', ['cmd.exe', '/c', REBUILD_BAT],
           tier='full', timeout=1200, ev='build', cwd=LOOKUP_TOOLS),
        _s('lookup-sync', '速查三端同步检查 check_sync.py', [PY, CHECK_SYNC],
           tier='full', timeout=600, ev='sync', cwd=LOOKUP_TOOLS),
        _s('keil-b1', 'Keil 复编 · 板1', [PY, BUILD_FLASH, '--build', '--board', '板1'],
           tier='full', timeout=900, ev='keil'),
        _s('keil-b2', 'Keil 复编 · 板2', [PY, BUILD_FLASH, '--build', '--board', '板2'],
           tier='full', timeout=900, ev='keil'),
    ]


def filter_steps(steps, only='', skip=''):
    """按 --only / --skip 过一遍（纯函数，方便自检）。给定 `only` 时只留它列的。"""
    def names(s):
        return [x.strip() for x in (s or '').replace('，', ',').split(',') if x.strip()]
    keep, drop = names(only), names(skip)
    out = []
    for st in steps:
        if keep and st['key'] not in keep:
            continue
        if st['key'] in drop:
            continue
        out.append(st)
    return out


# ---------------------------------------------------------------- 判据（每步一个）

def parse_fails(text):
    m = RE_SELFTEST_FAILS.findall(text or '')
    return int(m[-1]) if m else -1


def parse_sync(text):
    m = RE_SYNC.findall(text or '')
    return (int(m[-1][0]), int(m[-1][1])) if m else (-1, -1)


def _hint_lines(out, n=4):
    """失败时摘几条"看得懂"的行 —— **先摘真正报了 FAIL 的那几行**，再退到含"失败/错误"的行。

    只报一眼能看出问题的那几条，别把整篇日志倒出来；绿灯的噪音行（`失败项 0`）要滤掉。
    """
    strong, weak = [], []
    for ln in (out or '').splitlines():
        s = ln.strip()
        if not s or RE_HINT_SKIP.search(s):
            continue
        if RE_HINT_STRONG.search(s):
            strong.append(s)
        elif RE_HINT.search(s):
            weak.append(s)
        if len(strong) >= n:
            break
    return strong[:n] if strong else weak[:n]


def evaluate(ev, rc, out):
    """返回 (ok, skipped, note, hint)。rc < 0 = 没跑起来/超时（调用方已给 note）。"""
    if ev == 'selftest':
        n = parse_fails(out)
        if rc == 3:
            return True, True, '脚本自检声明跳过（本机缺外部工具链）', []
        ok = rc == 0 and n == 0
        note = '失败项 %d' % n if n >= 0 else ''
        return ok, False, note, ([] if ok else ['（各脚本自检的 FAIL 行）'] + _hint_lines(out))
    if ev == 'sync':
        p, f = parse_sync(out)
        ok = rc == 0 and f == 0
        return ok, False, ('%d 通过 / %d 失败' % (p, f)) if p >= 0 else '', \
            ([] if ok else _hint_lines(out))
    if ev == 'perf':
        m = RE_COLD.search(out or '')
        return rc == 0, False, ('冷启动 %s ms' % m.group(1)) if m else '', \
            ([] if rc == 0 else _hint_lines(out))
    if ev == 'mutation':
        m = RE_MUT.search(out or '')
        return rc == 0, False, ('共 %s 处，未变红 %s 处' % m.groups()) if m else '', \
            ([] if rc == 0 else _hint_lines(out))
    if ev == 'shot':
        return rc == 0, False, '', ([] if rc == 0 else _hint_lines(out))
    if ev == 'keil':
        return rc == 0, False, '', ([] if rc == 0 else _hint_lines(out))
    if ev == 'build':
        return rc == 0, False, '', ([] if rc == 0 else _hint_lines(out))
    return rc == 0, False, '', ([] if rc == 0 else _hint_lines(out))


# ---------------------------------------------------------------- 跑

def _toolchain_ok(ctx):
    """Keil 在不在 —— 全档的复编两步靠它决定"真跑"还是"跳过"（缓存在 ctx 里，只探一次）。"""
    if 'keil' in ctx:
        return ctx['keil']
    ok = False
    if os.path.isfile(BUILD_FLASH):
        try:
            p = subprocess.run([PY, BUILD_FLASH, '--detect', '--json'], capture_output=True,
                               text=True, encoding='utf-8', errors='replace', timeout=120,
                               cwd=os.path.dirname(BUILD_FLASH))
            for line in reversed((p.stdout or '').strip().splitlines()):
                try:
                    ok = bool(json.loads(line).get('uv4'))
                    break
                except ValueError:
                    continue
        except (OSError, subprocess.TimeoutExpired):
            ok = False
    ctx['keil'] = ok
    return ok


def _target(argv):
    """这一步真正在"跑"的那个文件 —— 取第一个像路径的参数（.py/.exe/.bat）。

    **先看 argv[1:]，再回落到 argv[0]**：argv[0] 往往是"发射器"（`python` / `cmd.exe`），
    直接认它会把 `cmd /c xxx.bat` 认成 `cmd.exe`（那当然找不到）—— 这个坑当场踩过一次。
    `python -c "…"` 这种没有文件的，回落到解释器本身（存在即可，不必非要目标文件）。
    """
    for a in list(argv[1:]) + list(argv[:1]):
        if isinstance(a, str) and a.endswith(('.py', '.exe', '.bat')):
            return a
    return ''


def run_step(st, ctx, echo=True):
    t0 = time.time()
    argv = list(st['argv'])
    rec = {'key': st['key'], 'name': st['name'], 'tier': st['tier'],
           'cmd': ' '.join(os.path.basename(a) if a.endswith(('.py', '.exe', '.bat')) else a
                           for a in argv if a != 'cmd.exe'),
           'rc': None, 'ok': False, 'skipped': False, 'note': '', 'hint': [], 'seconds': 0.0}

    target = _target(argv)
    if st['ev'] == 'keil' and not _toolchain_ok(ctx):
        rec.update({'skipped': True, 'ok': True,
                    'note': '本机没找到 UV4.exe（Keil）——按跳过记，不算红'})
    elif target and not os.path.isfile(target):
        rec.update({'note': '找不到：%s' % target, 'hint': ['找不到要跑的目标']})
    else:
        try:
            p = subprocess.run(argv, capture_output=True, text=True, encoding='utf-8',
                               errors='replace', timeout=st['timeout'], cwd=st['cwd'])
            rc, out = p.returncode, (p.stdout or '') + (p.stderr or '')
            ok, skipped, note, hint = evaluate(st['ev'], rc, out)
            rec.update({'rc': rc, 'ok': ok, 'skipped': skipped, 'note': note, 'hint': hint})
        except subprocess.TimeoutExpired:
            rec.update({'note': '超时（>%ds）' % st['timeout'], 'hint': ['这一步超时了']})
        except OSError as e:
            rec.update({'note': '跑不起来：%s' % e, 'hint': ['这一步启动失败']})
    rec['seconds'] = round(time.time() - t0, 1)
    if echo:
        print('  %-44s %s%s' % (st['name'],
                                ('⏭ 跳过' if rec['skipped'] else ('✅' if rec['ok'] else '❌')),
                                ('　' + rec['note']) if rec['note'] else ''))
        sys.stdout.flush()
    return rec


def conclusion(b):
    """结论一句话 —— **点名**哪一步红、并说清其余仍绿（这就是 C1 验收要的那句）。"""
    reds = [r['name'] for r in b['steps'] if not r['ok'] and not r['skipped']]
    n_skip = sum(1 for r in b['steps'] if r['skipped'])
    tail = ('（跳过 %d 步）' % n_skip) if n_skip else ''
    if reds:
        return '有 %d 步红：%s；其余 %d 步仍绿%s' % (
            len(reds), '、'.join(reds), len(b['steps']) - len(reds) - n_skip, tail)
    return '全绿：%d 步全过%s' % (len(b['steps']) - n_skip, tail)


def run_all(steps, ctx=None, echo=True):
    ctx = ctx if ctx is not None else {}
    if echo:
        print('一键验收（%s 档，共 %d 步）'
              % ('全' if any(s['tier'] == 'full' for s in steps) else '快', len(steps)))
    t0 = time.time()
    recs = []
    for i, st in enumerate(steps, 1):
        if echo:
            print('[%d/%d] %s' % (i, len(steps), st['name']))
        recs.append(run_step(st, ctx, echo=echo))
    b = {'tool': '一键验收', 'schema': 1,
         'tier': 'full' if any(s['tier'] == 'full' for s in steps) else 'fast',
         'time': datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
         'seconds': round(time.time() - t0, 1), 'steps': recs}
    b['ok'] = all(r['ok'] for r in recs)
    b['conclusion'] = conclusion(b)
    return b


# ---------------------------------------------------------------- 报告 / 归档

def render_md(b):
    L = ['# 一键验收报告（%s 档）' % ('全' if b['tier'] == 'full' else '快'), '',
         '> 一条命令跑完验收链，逐步行；每步**独立判定**，所以改坏一处只红那一处。', '',
         '- 时间：%s' % b['time'],
         '- 用时：%.1fs' % b['seconds'],
         '- 结论：**%s**' % b['conclusion'], '',
         '| # | 步骤 | 档 | 结果 | 耗时 | 摘要 |', '|---|---|---|---|---|---|']
    for i, r in enumerate(b['steps'], 1):
        mark = '⏭ 跳过' if r['skipped'] else ('✅ 通过' if r['ok'] else '❌ 不通过')
        L.append('| %d | %s | %s | %s | %.1fs | %s |'
                 % (i, r['name'], '快' if r['tier'] == 'fast' else '全', mark,
                    r['seconds'], r['note'] or '—'))
    L.append('')
    reds = [r for r in b['steps'] if not r['ok'] and not r['skipped']]
    if reds:
        L += ['## 红在哪（只列红的那几步）', '']
        for r in reds:
            L.append('### %s' % r['name'])
            L.append('')
            L.append('- 命令：`%s`' % r['cmd'])
            if r['note']:
                L.append('- 说明：%s' % r['note'])
            for h in r['hint']:
                L.append('- `%s`' % h)
            L.append('')
    else:
        L += ['## 红在哪', '', '- 没有红的步骤。', '']
    return '\n'.join(L)


def archive(b):
    md = render_md(b)
    os.makedirs(PRODUCT, exist_ok=True)
    latest = os.path.join(PRODUCT, '一键验收.md')
    with open(latest, 'w', encoding='utf-8', newline='\n') as f:
        f.write(md)
    hist_dir = os.path.join(PRODUCT, '历史')
    os.makedirs(hist_dir, exist_ok=True)
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    hist = os.path.join(hist_dir, '一键验收_%s%s.md'
                        % ('全档_' if b['tier'] == 'full' else '', stamp))
    with open(hist, 'w', encoding='utf-8', newline='\n') as f:
        f.write(md)
    return latest, hist


# ---------------------------------------------------------------- 自检

# 合成步骤的载荷用**纯 ASCII**：`python -c` 的实参里带中文在 Windows 上会被按本机代码页解码，
# 测试夹具不该被这种环境细节绊住（真步骤都是跑文件，不受影响）。
OK_STEP = "print('PASS local step one')"
BAD_STEP = "import sys;print('PASS local broken');print('FAIL local broken step: judgment was broken');sys.exit(1)"


def selftest():
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    steps = build_steps()
    keys = [s['key'] for s in steps]
    ck('步骤表 key 唯一', len(keys) == len(set(keys)), str(len(keys)))
    ck('每步都有 name / argv / 超时',
       all(s['name'] and isinstance(s['argv'], list) and s['argv'] and s['timeout'] > 0
           for s in steps))
    ck('档只有 fast / full', all(s['tier'] in ('fast', 'full') for s in steps))
    fast = [s for s in steps if s['tier'] == 'fast']
    full = [s for s in steps if s['tier'] == 'full']
    ck('快档 ≥ 10 步', len(fast) >= 10, str(len(fast)))
    ck('全档额外 ≥ 4 步', len(full) >= 4, str(len(full)))

    ck('快档要的 构建.py 在', os.path.isfile(BUILD_PY))
    ck('快档要的 Toolbox.exe 在（先跑构建再验收）', os.path.isfile(EXE), EXE)
    ck('全档要的 rebuild.bat / check_sync.py 在',
       os.path.isfile(REBUILD_BAT) and os.path.isfile(CHECK_SYNC))
    ck('全档要的 build_flash.py 在（Keil 复编）', os.path.isfile(BUILD_FLASH))

    ck('--only 只留指定步骤', [s['key'] for s in filter_steps(steps, 'build', '')] == ['build'])
    ck('--skip 去掉指定步骤',
       'mutation' not in [s['key'] for s in filter_steps(steps, '', 'mutation')])
    ck('--only 支持逗号多值',
       [s['key'] for s in filter_steps(steps, 'build, selftest', '')] == ['build', 'selftest'])

    ck('自检失败项解析', parse_fails('[PASS] a\n[FAIL] b\n失败项 1') == 1)
    ck('无失败项解析', parse_fails('...失败项 0') == 0)
    ck('同步检查解析「25 通过 / 0 失败」', parse_sync('同步检查：25 项通过 / 0 项失败 / 0 跳过')
       == (25, 0))
    ck('性能量测摘冷启动毫秒',
       evaluate('perf', 0, '冷启动(构造全部页面): 572 ms\nF1 判定: 通过')[2] == '冷启动 572 ms')
    ck('变异自测摘「共 N 处 / 未变红 M 处」',
       evaluate('mutation', 0, '变异自测：共 4 处，未变红 0 处 → 通过')[2] == '共 4 处，未变红 0 处')

    ck('失败行摘取：优先摘 FAIL 行、滤掉"失败项 0"这类绿灯噪音',
       _hint_lines('自检：失败项 0\nPASS a\nFAIL 示例插件\n失败项 0') == ['FAIL 示例插件'],
       str(_hint_lines('自检：失败项 0\nPASS a\nFAIL 示例插件\n失败项 0')))

    ck('目标识别：`cmd /c xxx.bat` 认的是 .bat，不是 cmd.exe',
       os.path.basename(_target(['cmd.exe', '/c', REBUILD_BAT])) == os.path.basename(REBUILD_BAT),
       _target(['cmd.exe', '/c', REBUILD_BAT]))
    ck('目标识别：GUI 步取 exe（argv[1] 是开关 --xxx）', _target([EXE, '--selftest']) == EXE)
    ck('目标识别：python 步取脚本', _target([PY, BUILD_PY]) == BUILD_PY)
    ck('目标识别：`-c` 那种没有文件的回落到解释器', _target([PY, '-c', 'x']) == PY)

    # 合成步骤：验"独立判定"这条 —— 全绿、以及**故意改坏一处只红那一处**
    good = [_s('s1', '合成·一', [PY, '-c', OK_STEP]), _s('s2', '合成·二', [PY, '-c', OK_STEP])]
    bg = run_all(good, echo=False)
    ck('合成·全绿：结论是"全绿"', bg['ok'] and '全绿' in bg['conclusion'], bg['conclusion'])
    ck('合成·全绿：渲染出表头与结论', '| # | 步骤 |' in render_md(bg) and '全绿' in render_md(bg))

    broken = [_s('s1', '合成·一', [PY, '-c', OK_STEP]),
              _s('s2', '合成·故意改坏的一步', [PY, '-c', BAD_STEP]),
              _s('s3', '合成·三', [PY, '-c', OK_STEP])]
    bb = run_all(broken, echo=False)
    reds = [r['name'] for r in bb['steps'] if not r['ok']]
    ck('合成·改坏一处：只有那一步红', reds == ['合成·故意改坏的一步'], str(reds))
    ck('合成·改坏一处：其余两步仍绿',
       sum(1 for r in bb['steps'] if r['ok']) == 2, str([r['ok'] for r in bb['steps']]))
    ck('合成·结论点名红的那一步', '合成·故意改坏的一步' in bb['conclusion'], bb['conclusion'])
    ck('合成·结论写明其余仍绿', '仍绿' in bb['conclusion'], bb['conclusion'])
    ck('合成·红的那一步摘出了 FAIL 原话',
       any('FAIL local broken step' in h for h in bb['steps'][1]['hint']),
       str(bb['steps'][1]['hint']))
    md = render_md(bb)
    ck('归档 md 有"红在哪"小节且只列红的那一步',
       '## 红在哪' in md and md.count('### ') == 1)
    ck('归档 md 摘出了失败的原话', 'FAIL local broken step' in md)

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('一键验收自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='一键验收（C1）：快 / 全两档跑完验收链')
    ap.add_argument('--full', action='store_true', help='全档（含速查重建链 + 三端同步 + Keil 双板复编）')
    ap.add_argument('--only', default='', help='只跑这些步骤（逗号分隔的 key）')
    ap.add_argument('--skip', default='', help='跳过这些步骤（逗号分隔的 key）')
    ap.add_argument('--no-build', action='store_true',
                    help='跳过构建（工具箱正开着时，exe 被占用会改不动它）')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    steps = filter_steps(build_steps(), a.only, a.skip)
    if not a.only and not a.full:
        steps = [s for s in steps if s['tier'] == 'fast']
    if a.no_build:
        steps = [s for s in steps if s['key'] != 'build']
    if not steps:
        print('没有要跑的步骤（--only / --skip 过滤后为空）')
        sys.exit(2)

    b = run_all(steps)
    latest, hist = archive(b)
    print('')
    print('结论：%s' % b['conclusion'])
    print('报告：%s（历史归档：%s）' % (latest, hist))
    if a.json:
        print(json.dumps(b, ensure_ascii=False))
    else:
        for r in b['steps']:
            for h in r['hint'][:3]:
                print('  ✗ %s ｜ %s' % (r['name'], h))
    sys.exit(0 if b['ok'] else 1)


if __name__ == '__main__':
    main()
