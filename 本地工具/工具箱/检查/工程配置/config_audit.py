# -*- coding: utf-8 -*-
r"""多工程配置化审计（D3）。

要回答的问题：**"再加一个工程，真的只改一份 JSON 就够吗？"**
不靠读代码猜，靠两件事：

  1) `--check`  把现有配置逐字段验一遍（缺字段 / 路径不存在 / 重名 / 清单对不上）
  2) `--inject` 往配置里**临时塞一个假工程**，用 `TOOLBOX_BOARDCFG` 指过去，
     把七个检查器各跑一遍 —— 都得能跑、都认得这块假板；
     跑完撤掉环境变量，确认又回到原来那几块板（不留副作用）。

为什么用环境变量而不是改真配置：改真配置测完忘了还原，就会把假板留在仓库里
（这种事故以前踩过）。`TOOLBOX_BOARDCFG` 只影响当前进程，天然干净。

用法：
    python 本地工具\工具箱\检查\工程配置\config_audit.py --check
    python ...\config_audit.py --inject
    python ...\config_audit.py --json
    python ...\config_audit.py --selftest
"""
import argparse
import json
import os
import shutil
import subprocess
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
TOOLBOX = os.path.abspath(os.path.join(HERE, '..', '..'))
ROOT = find_root(HERE) or os.path.abspath(os.path.join(TOOLBOX, '..', '..'))
CFG_REL = os.path.join('本地工具', '工具箱', '速查数据', 'rules', 'board_envmonitor.json')
PY = sys.executable or 'python'

REQUIRED = ('name', 'dir', 'uvprojx', 'hex', 'serial_banner', 'chip', 'flash_kb', 'ram_kb')
CHECKERS = [
    ('引脚规划', '检查/引脚规划/pin_planner.py'),
    ('RTOS 检查', '检查/RTOS检查/rtos_check.py'),
    ('编码守卫', '检查/编码守卫/encoding_guard.py'),
    ('LoRa 检查', '检查/LoRa检查/lora_check.py'),
    ('协议校验', '检查/协议校验/protocol_check.py'),
    ('断网缓存', '检查/断网缓存/cache_check.py'),
    ('进度扫描', '检查/进度跟踪/progress_scan.py'),
]


def cfg_path():
    return os.environ.get('TOOLBOX_BOARDCFG', '').strip() or os.path.join(ROOT, CFG_REL)


def load_cfg(path=None):
    with open(path or cfg_path(), encoding='utf-8') as f:
        return json.load(f)


def audit(cfg):
    """逐项体检配置。返回 [(检查项, 是否通过, 说明)]。"""
    out = []
    boards = cfg.get('boards') or []
    out.append(('配置有 version / project / boards',
                bool(cfg.get('version') and cfg.get('project') and boards),
                'version=%s project=%s boards=%d'
                % (cfg.get('version'), cfg.get('project'), len(boards))))

    names = [b.get('name', '') for b in boards]
    out.append(('板名不重复', len(names) == len(set(names)),
                '重复：%s' % [n for n in set(names) if names.count(n) > 1]))

    for b in boards:
        miss = [k for k in REQUIRED if b.get(k) in (None, '')]
        out.append(('%s：字段齐全' % b.get('name', '?'), not miss, '缺：%s' % miss))
        d = os.path.join(ROOT, b.get('dir') or '')
        out.append(('%s：工程目录存在' % b.get('name', '?'), os.path.isdir(d), b.get('dir')))
        uv = os.path.join(d, b.get('uvprojx') or '')
        out.append(('%s：.uvprojx 存在' % b.get('name', '?'), os.path.isfile(uv), uv))

    docs = (cfg.get('extra') or {}).get('progress_docs') or []
    out.append(('extra.progress_docs 有 3 份（与工程引导一致）', len(docs) == 3,
                '%d 份' % len(docs)))
    missing = [p for p in docs if not os.path.isfile(os.path.join(ROOT, p))]
    out.append(('progress_docs 里的文件都在', not missing, missing))

    # 阶段判据与配置的对应关系：判据侧用**短名**（板1/板2/Qt），配置侧用**长名**
    # （板1 检测端 (F407ZE)），目前靠前缀对应。新增固件工程时**两边都要加**，
    # 这条检查就是防止只加了一边（判据侧漏了 → 那块板永远判不了阶段）。
    sp = os.path.join(TOOLBOX, '速查数据', 'rules', 'stage_checks.json')
    try:
        with open(sp, encoding='utf-8') as f:
            sc = json.load(f).get('boards') or {}
        ck_keys = list(sc)
        # Qt 是上位机，不在固件板配置里 —— 正常，不算缺失
        firmware = [n for n in names if n not in ('Qt',)]
        matched, unmatched = [], []
        for n in firmware:
            hit = [k for k in ck_keys if n.startswith(k)]
            (matched if hit else unmatched).append(n)
        # OTA 没有阶段卡，判据侧本来就不该有它 —— 从"缺失"里排除
        unmatched = [n for n in unmatched if not n.startswith('OTA')]
        out.append(('每块固件板都能在 stage_checks 找到判据（按短名前缀）',
                    not unmatched,
                    '没判据的：%s；已对应：%s' % (unmatched, matched)))
    except Exception as e:
        out.append(('stage_checks.json 可读', False, str(e)))
    return out


def run_checker(rel, env_extra):
    p = os.path.join(TOOLBOX, rel)
    env = dict(os.environ)
    env.update(env_extra)
    try:
        r = subprocess.run([PY, p, '--json'], capture_output=True, text=True,
                           encoding='utf-8', errors='replace', timeout=180,
                           cwd=os.path.dirname(p), env=env)
    except Exception as e:
        return {'tool': rel, 'ok': False, 'error': str(e), 'out': ''}
    out = (r.stdout or '') + (r.stderr or '')
    return {'tool': rel, 'ok': r.returncode == 0, 'out': out, 'error': ''}


def inject_test(cfg):
    """临时加一块假工程 → 检查器都得能跑、都认得它 → 撤掉后回到原样。"""
    real_n = len(cfg.get('boards') or [])
    base = (cfg.get('boards') or [{}])[0]
    fake = dict(base)
    fake['name'] = '假工程 (F407ZE)'
    fake['note'] = '配置化验收用的临时板，不在真配置里'
    cfg2 = json.loads(json.dumps(cfg))
    cfg2['boards'] = list(cfg.get('boards') or []) + [fake]

    tmp = tempfile.mkdtemp(prefix='cfg_')
    p = os.path.join(tmp, 'board_fake.json')
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(cfg2, f, ensure_ascii=False, indent=2)

    rows = []
    try:
        # 1) boardcfg 认不认这块假板（进程内：要先把环境变量设上，_paths 是调用时读的）
        sys.path.insert(0, os.path.join(TOOLBOX, '检查'))
        import importlib
        import boardcfg
        os.environ['TOOLBOX_BOARDCFG'] = p
        importlib.reload(boardcfg)
        got = boardcfg.boards(ROOT) or []
        names = [n for n, _ in got]
        rows.append({'what': 'boardcfg 读到 %d 块板（原 %d + 假 1）' % (len(got), real_n),
                     'ok': len(got) == real_n + 1 and fake['name'] in names,
                     'detail': names})
        os.environ.pop('TOOLBOX_BOARDCFG', None)
        importlib.reload(boardcfg)

        # 2) 七个检查器都能跑；**列板的**还得认得假板
        #    断网缓存 / 进度扫描的 JSON 本来就不列板（一个是容量模型、一个是全仓扫描），
        #    对它们只要求"能跑且 ok"，硬要求出现板名就是误判（第一版就这么错了）。
        real_names = [b.get('name', '') for b in (cfg.get('boards') or [])]
        for label, rel in CHECKERS:
            r = run_checker(rel, {'TOOLBOX_BOARDCFG': p})
            lists_boards = any(n and n in r['out'] for n in real_names)
            seen = fake['name'] in r['out']
            ok = r['ok'] and (seen if lists_boards else True)
            why = ''
            if not r['ok']:
                why = r.get('error') or '退出码非 0'
            elif lists_boards and not seen:
                why = '输出里列了真板却没有假板'
            rows.append({'what': '%s 能跑%s' % (label, '且认得假板' if lists_boards else '（不列板，只验能跑）'),
                         'ok': ok, 'detail': why})

        # 3) 撤掉环境变量 → 回到原样
        os.environ.pop('TOOLBOX_BOARDCFG', None)
        importlib.reload(boardcfg)
        back = boardcfg.boards(ROOT) or []
        rows.append({'what': '撤掉环境变量后回到 %d 块板（不留假板）' % real_n,
                     'ok': len(back) == real_n, 'detail': [n for n, _ in back]})
    finally:
        os.environ.pop('TOOLBOX_BOARDCFG', None)
        shutil.rmtree(tmp, ignore_errors=True)
    return rows


def selftest():
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    cfg = load_cfg()
    ck('读到真配置', bool(cfg.get('boards')), len(cfg.get('boards') or []))
    a = audit(cfg)
    bad = [x for x in a if not x[1]]
    ck('真配置逐项体检全过（%d 项）' % len(a), not bad, bad[:3])

    # 反例：故意造坏配置，审计必须抓出来
    broken = json.loads(json.dumps(cfg))
    broken['boards'][0].pop('uvprojx', None)
    broken['boards'].append(dict(broken['boards'][0]))          # 重名
    ab = audit(broken)
    ck('反例：缺字段被抓到', any('字段齐全' in x[0] and not x[1] for x in ab))
    ck('反例：重名被抓到', any('板名不重复' in x[0] and not x[1] for x in ab))

    # 注入：用临时目录造一份带假板的配置，只验 boardcfg 这一层（检查器那层放 --inject 手动跑）
    tmp = tempfile.mkdtemp(prefix='cfg_st_')
    try:
        cfg2 = json.loads(json.dumps(cfg))
        fake = dict(cfg2['boards'][0])
        fake['name'] = '假工程 (F407ZE)'
        cfg2['boards'].append(fake)
        p = os.path.join(tmp, 'c.json')
        with open(p, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(cfg2, f, ensure_ascii=False)
        sys.path.insert(0, os.path.join(TOOLBOX, '检查'))
        import importlib
        import boardcfg
        os.environ['TOOLBOX_BOARDCFG'] = p
        importlib.reload(boardcfg)
        got = boardcfg.boards(ROOT) or []
        ck('TOOLBOX_BOARDCFG 生效（多出一块假板）',
           len(got) == len(cfg['boards']) + 1 and '假工程 (F407ZE)' in [n for n, _ in got],
           [n for n, _ in got])
        os.environ.pop('TOOLBOX_BOARDCFG', None)
        importlib.reload(boardcfg)
        back = boardcfg.boards(ROOT) or []
        ck('撤掉环境变量回到原样', len(back) == len(cfg['boards']), len(back))
    finally:
        os.environ.pop('TOOLBOX_BOARDCFG', None)
        shutil.rmtree(tmp, ignore_errors=True)

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('工程配置审计自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='多工程配置化审计（D3）')
    ap.add_argument('--check', action='store_true', help='逐项体检配置')
    ap.add_argument('--inject', action='store_true', help='临时加假工程，跑七个检查器')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    cfg = load_cfg()
    rows = audit(cfg)
    inj = inject_test(cfg) if a.inject else []
    ok = all(r[1] for r in rows) and all(r['ok'] for r in inj)

    if a.json:
        print(json.dumps({'tool': '工程配置', 'schema': 1, 'ok': ok,
                          'audit': [{'what': w, 'ok': o, 'detail': str(d)}
                                              for w, o, d in rows],
                          'inject': inj}, ensure_ascii=False))
        sys.exit(0 if ok else 1)

    print('配置：%s（%d 块板）' % (cfg_path(), len(cfg.get('boards') or [])))
    for w, o, d in rows:
        print('  [%s] %s%s' % ('PASS' if o else 'FAIL', w,
                               ('　' + str(d)) if (d and not o) else ''))
    if inj:
        print('\n加一块假工程后：')
        for r in inj:
            print('  [%s] %s%s' % ('PASS' if r['ok'] else 'FAIL', r['what'],
                                   ('　' + str(r['detail'])) if (r['detail'] and not r['ok']) else ''))
    print('\n结论：%s' % ('配置可换装' if ok else '有项没通过，看上面 FAIL'))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
