# -*- coding: utf-8 -*-
r"""插件体检（新检查方向）—— 守"插件页 manifest 与上游脚本产物一致"。

为什么需要：平台化之后插件页涨到 10 个、闸门涨到 30+ 项，但**没人守这些插件自身的口径**。
两个静默故障一直没人管：

  1) manifest 里的 `report` 跟上游脚本**真会写出的路径**不一致 ——
     那一页的「打开报告」按钮**永远点不亮**（面板不报错，只是点了没反应）；
  2) 上游脚本有 `--json` 却**忘了登记**进 `速查数据/rules/json_contract.json` ——
     契约表悄悄漏了一支，谁也没发现。

本页把每个插件页当患者，逐条对账（每条都能 FAIL 并**点名到"哪一页 / 哪个文件 / 哪个键"**）：

  a. manifest 字段：name/group/desc 齐全、name 不重复、report 是相对路径、上游脚本存在；
  b. **report 一致性（核心）**：manifest 的 report 必须等于上游脚本"真会写出的路径"——
     认**两条路任一**：① 脚本里的 `REPORT_REL` / `DEFAULT_OUT` 之类常量；
     ② 插件壳 args 里 `--write` / `--out` 后面跟的值。不一致就打印
     `manifest 说 X / 脚本实际写 Y`；
  c. 上游脚本源码里必须有 `--selftest` 出口（grep 到即可）；
  d. 上游脚本若含 `--json`，必须在 `json_contract.json` 的 `tools` 或 `excluded` 里登记；
  e. 真跑一遍每个 `check.py --manifest`（子进程，10s 超时），拿不到合法 JSON 就 FAIL。

用法：
    python 本地工具\工具箱\检查\插件体检\plugin_audit.py
    python ...\plugin_audit.py --write 产物\插件体检报告.md
    python ...\plugin_audit.py --json
    python ...\plugin_audit.py --selftest      # 正例（真 检查\* 全过）+ 反例（临时假插件必点名）
"""
import argparse
import glob
import json
import os
import re
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


HERE = os.path.dirname(os.path.abspath(__file__))          # 工具箱/检查/插件体检
CHECKS = os.path.dirname(HERE)                             # 工具箱/检查
TOOLBOX = os.path.dirname(CHECKS)                          # 工具箱
ROOT = find_root(HERE) or os.path.abspath(os.path.join(TOOLBOX, '..', '..'))
PY = sys.executable or 'python'
CONTRACT_PATH = os.path.join(TOOLBOX, '速查数据', 'rules', 'json_contract.json')
REPORT_REL = os.path.join('产物', '插件体检报告.md')

# 上游脚本名：插件壳里 `main(upstream='xxx.py', ...)` 的那一个。
RE_UPSTREAM = re.compile(r"upstream\s*=\s*['\"]([^'\"]+)['\"]")
# 路径路①：脚本里的 `REPORT_REL` / `DEFAULT_OUT` 之类常量（os.path.join(...) 或直接字符串两种写法都认）。
RE_CONST = re.compile(
    r"(?:REPORT_REL|DEFAULT_REPORT|DEFAULT_OUT|OUT_REL|REPORT)"
    r"\s*=\s*(?:os\.path\.join\(([^)]*)\)|['\"]([^'\"]+)['\"])")
RE_QUOTED = re.compile(r"['\"]([^'\"]+)['\"]")
# 路径路②：插件壳 args 里 `'--write', '值'` / `'--out', '值'`。
RE_ARG_OUT = re.compile(r"['\"]--(?:write|out)['\"]\s*,\s*['\"]([^'\"]+)['\"]")


def norm(p):
    """报告路径统一成"正斜杠 + 去掉开头 ./"的比较形态。"""
    s = (p or '').replace('\\', '/').strip()
    while s.startswith('./'):
        s = s[2:]
    return s


def relpath(p, base=TOOLBOX):
    try:
        return os.path.relpath(p, base).replace('\\', '/')
    except ValueError:
        return str(p).replace('\\', '/')


def read_text(path):
    try:
        with open(path, encoding='utf-8', errors='ignore') as f:
            return f.read()
    except OSError:
        return ''


def load_contract():
    try:
        with open(CONTRACT_PATH, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


# ------------------------------------------------------------------ 取数

def discover_dirs(checks_dir):
    """这棵 `检查/` 下有 `check.py` 的插件目录（与工具箱扫描口径一致）。"""
    return [d for d in sorted(glob.glob(os.path.join(checks_dir, '*')))
            if os.path.isdir(d) and os.path.isfile(os.path.join(d, 'check.py'))]


def read_manifest(script, timeout=10):
    """真跑一遍 `check.py --manifest`（子进程）。返回 (manifest 或 None, 说明)。"""
    try:
        p = subprocess.run([PY, script, '--manifest'], capture_output=True, text=True,
                           encoding='utf-8', errors='replace', timeout=timeout,
                           cwd=os.path.dirname(script))
    except subprocess.TimeoutExpired:
        return None, '超时（>%ds）' % timeout
    except OSError as e:
        return None, '跑不起来：%s' % e
    for line in reversed((p.stdout or '').strip().splitlines()):
        try:
            m = json.loads(line)
        except ValueError:
            continue
        if isinstance(m, dict):
            return m, ''
    tail = (p.stdout or '').strip().splitlines()
    return None, '最后一行不是 JSON（rc=%s，末行：%s）' % (
        p.returncode, (tail[-1][:60] if tail else '（无输出）'))


def actual_report_paths(up_src, shell_src):
    """上游脚本"真会写出的路径" —— 两条路都认，返回去重后的正斜杠列表。"""
    found = []
    # 路①：上游脚本里的路径常量（os.path.join(...) 或直接字符串）
    for g_join, g_str in RE_CONST.findall(up_src or ''):
        if g_join:
            parts = RE_QUOTED.findall(g_join)
            if parts:
                found.append(norm('/'.join(parts)))
        elif g_str:
            found.append(norm(g_str))
    # 路②：插件壳 args 里 --write / --out 后面的值
    found += [norm(m) for m in RE_ARG_OUT.findall(shell_src or '')]
    out = []
    for f in found:
        if f and f not in out:
            out.append(f)
    return out


def registered_in_contract(contract, key):
    """上游脚本的登记状态：'tools' / 'excluded' / ''。"""
    for sec in ('tools', 'excluded'):
        for e in contract.get(sec) or []:
            if norm(e.get('script')) == norm(key):
                return sec
    return ''


# ------------------------------------------------------------------ 体检

def audit_one(d, contract, timeout=10):
    """给一个插件目录出报告行 + 问题清单。"""
    shell = os.path.join(d, 'check.py')
    shell_src = read_text(shell)
    page = os.path.basename(d)
    problems = []

    def bad(msg):
        problems.append(msg)

    # (e) 真跑 --manifest
    manifest, note = read_manifest(shell, timeout)
    if manifest is None:
        bad('插件页「%s」：跑 `check.py --manifest` 拿不到合法 JSON（%s）' % (page, note))
        return {'page': page, 'name': '?', 'group': '?', 'upstream': '?', 'report': '?',
                'actual': '?', 'contract': '?', 'problems': problems}

    name = (manifest.get('name') or '').strip()
    group = (manifest.get('group') or '').strip()
    desc = (manifest.get('desc') or '').strip()
    report = manifest.get('report') or ''

    # (a) manifest 字段
    for k, v in (('name', name), ('group', group), ('desc', desc)):
        if not v:
            bad('插件页「%s」：manifest 缺 `%s`' % (page, k))
    if report and (norm(report).startswith('/') or '..' in norm(report).split('/')):
        bad('插件页「%s」：report 不是相对路径（%r）' % (page, report))

    # 上游脚本：壳里 upstream= 指谁；指不出来（自带本体的插件）就是 check.py 自己
    m = RE_UPSTREAM.search(shell_src)
    up_name = m.group(1) if m else os.path.basename(shell)
    up_abs = os.path.join(d, up_name)
    if not os.path.isfile(up_abs):
        bad('插件页「%s」：上游脚本不存在（%s）' % (page, up_name))
        return {'page': page, 'name': name or '?', 'group': group or '?',
                'upstream': up_name, 'report': report or '—', 'actual': '?',
                'contract': '?', 'problems': problems}
    up_src = read_text(up_abs)

    # (b) report 一致性（核心）
    actual = actual_report_paths(up_src, shell_src)
    if report:
        if norm(report) not in actual:
            bad('插件页「%s」：report 与脚本实际写出的路径不一致 —— '
                'manifest 说 %r / 脚本实际写 %s'
                % (page, report, '、'.join(actual) if actual else '（脚本里找不到写报告的路径）'))
    # (c) --selftest 出口
    if '--selftest' not in up_src:
        bad('插件页「%s」：上游脚本 %s 缺 `--selftest` 出口' % (page, up_name))
    # (d) --json 必须登记契约
    contract_state = '无需（无 --json）'
    if '--json' in up_src:
        key = relpath(up_abs)
        sec = registered_in_contract(contract, key)
        if sec:
            contract_state = '已登记（%s）' % sec
        else:
            contract_state = '未登记'
            bad('插件页「%s」：上游脚本 %s 有 `--json` 却未登记进 json_contract.json'
                % (page, up_name))

    return {'page': page, 'name': name or '?', 'group': group or '?', 'upstream': up_name,
            'report': report or '—',
            'actual': '、'.join(actual) if actual else '—',
            'contract': contract_state, 'problems': problems}


def collect(checks_dir=None, timeout=10):
    """跑一遍体检，返回 board（含每页一行 + 问题清单）。"""
    checks_dir = checks_dir or CHECKS
    contract = load_contract()
    rows = []
    names = {}
    for d in discover_dirs(checks_dir):
        row = audit_one(d, contract, timeout)
        rows.append(row)
        n = row['name']
        if n and n != '?':
            names.setdefault(n, []).append(row['page'])
    # (a) name 不重复
    for n, pages in names.items():
        if len(pages) > 1:
            for r in rows:
                if r['name'] == n:
                    r['problems'].append(
                        '插件页「%s」：manifest 的 name「%s」与「%s」重复' % (r['page'], n, '、'.join(pages)))
    problems = []
    for r in rows:
        problems += r['problems']
    return {'tool': '插件体检', 'schema': 1, 'ok': not problems,
            'plugins': rows, 'problems': problems}


# ------------------------------------------------------------------ 渲染

def render_md(b):
    L = ['# 插件体检报告', '',
         '> 每个插件页一行：manifest 的 `report` 跟上游脚本**真会写出的路径**对不对得上、'
         '`--json` 有没有登记契约。',
         '> 数字全部现场取（真跑 `--manifest`、读上游源码），一个都不写死。', '',
         '| 名字 | 分组 | 上游脚本 | manifest report | 脚本实际写 | 契约登记 | 结论 |',
         '|---|---|---|---|---|---|---|']
    for r in b['plugins']:
        L.append('| %s | %s | `%s` | `%s` | `%s` | %s | %s |'
                 % (r['name'], r['group'], r['upstream'], r['report'], r['actual'],
                    r['contract'], '✅ 通过' if not r['problems'] else '❌ 不通过'))
    L.append('')
    L.append('- 共 **%d** 个插件页：**%d** 通过 / **%d** 有问题。'
             % (len(b['plugins']),
                sum(1 for r in b['plugins'] if not r['problems']),
                sum(1 for r in b['plugins'] if r['problems'])))
    L.append('')
    L += ['## 问题点名', '']
    if b['problems']:
        for p in b['problems']:
            L.append('- %s' % p)
    else:
        L.append('- 无。')
    L.append('')
    return '\n'.join(L)


# ------------------------------------------------------------------ 自检

def _fake_plugin(dirpath, name, manifest_report, upstream_name, upstream_src):
    """造一个假插件目录（反例用）：check.py 壳 + 上游脚本。"""
    os.makedirs(dirpath, exist_ok=True)
    shell = os.path.join(dirpath, 'check.py')
    with open(shell, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# -*- coding: utf-8 -*-\n'
                'import json, sys\n'
                'MAN = %r\n'
                'upstream = %r\n'
                "if '--manifest' in sys.argv:\n"
                '    print(json.dumps(MAN, ensure_ascii=False))\n'
                '    sys.exit(0)\n'
                "print('假的')\n"
                % ({'name': name, 'group': '检查', 'desc': '反例插件', 'report': manifest_report},
                   upstream_name))
    with open(os.path.join(dirpath, upstream_name), 'w', encoding='utf-8', newline='\n') as f:
        f.write(upstream_src)


def selftest():
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    # 正例：真实 检查\* 全过
    rep_abs = os.path.join(HERE, REPORT_REL)
    rep_before = os.path.getmtime(rep_abs) if os.path.isfile(rep_abs) else None
    real = collect()
    ck('正例：真实插件页 ≥6 个', len(real['plugins']) >= 6, str(len(real['plugins'])))
    ck('正例：真实插件页全部对得上（0 问题）', not real['problems'],
       str(real['problems'][:3]))

    tmp = tempfile.mkdtemp(prefix='plugin_audit_selftest_')
    try:
        base = os.path.join(tmp, '检查')
        os.makedirs(base)
        # 反例一：manifest 说 产物/a.md，脚本实际写 产物/b.md → 必须点名
        _fake_plugin(os.path.join(base, '假插件A'), '假插件A', '产物/a.md', 'up.py',
                     "# -*- coding: utf-8 -*-\nimport os\n"
                     "REPORT_REL = %r\n"
                     "# --selftest 出口\n" % '产物/b.md')
        # 反例二：有 --json 但没登记契约 → 必须点名
        _fake_plugin(os.path.join(base, '假插件B'), '假插件B', '产物/c.md', 'up2.py',
                     "# -*- coding: utf-8 -*-\nimport os\n"
                     "REPORT_REL = %r\n"
                     "# 有 --json 出口（但没登记 json_contract.json）\n"
                     "# --selftest 出口\n" % '产物/c.md')
        bad = collect(checks_dir=base)

        n1 = [p for p in bad['problems'] if '假插件A' in p]
        ck('反例一：report 不一致被点名', bool(n1), str(bad['problems'][:3]))
        ck('反例一：打印出「manifest 说 X / 脚本实际写 Y」',
           any(('a.md' in p and 'b.md' in p) for p in n1), str(n1[:2]))

        n2 = [p for p in bad['problems'] if '假插件B' in p]
        ck('反例二：忘了登记契约被点名', bool(n2), str(bad['problems'][:3]))
        ck('反例二：点名说它「未登记」', any('未登记' in p for p in n2), str(n2[:2]))

        ck('反例被整体判红（ok=False）', not bad['ok'], str(bad['ok']))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # 自检不污染真实报告：跑完自检，报告文件（存在与否 / 修改时间）必须没变
    rep_after = os.path.getmtime(rep_abs) if os.path.isfile(rep_abs) else None
    ck('自检不写/不改真实报告文件', rep_after == rep_before,
       '%r → %r' % (rep_before, rep_after))

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('插件体检自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='插件体检：manifest 与上游脚本产物一致性')
    ap.add_argument('--write', default='', help='把体检报告写到这个 md')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    b = collect()
    if a.write:
        out = a.write if os.path.isabs(a.write) else os.path.join(HERE, a.write)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'w', encoding='utf-8', newline='\n') as f:
            f.write(render_md(b))
        print('报告：%s' % out)
    elif not a.json:
        out = os.path.join(HERE, REPORT_REL)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'w', encoding='utf-8', newline='\n') as f:
            f.write(render_md(b))
        print('报告：%s' % out)

    if a.json:
        print(json.dumps(b, ensure_ascii=False))
    else:
        nbad = len(b['problems'])
        print('插件体检：%d 个插件页｜%d 有问题'
              % (len(b['plugins']), nbad))
        for r in b['plugins']:
            print('  %s %-12s 上游 %-22s report=%-24s 实际写=%s'
                  % ('✅' if not r['problems'] else '❌', r['name'], r['upstream'],
                     r['report'], r['actual']))
        for p in b['problems']:
            print('  ❌ %s' % p)
    sys.exit(0 if b['ok'] else 1)


if __name__ == '__main__':
    main()
