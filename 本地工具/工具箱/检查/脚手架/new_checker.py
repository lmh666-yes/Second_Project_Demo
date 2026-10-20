# -*- coding: utf-8 -*-
r"""新检查器脚手架（B3）—— 一条命令长出"能被工具箱发现、能过闸门、能出报告"的检查器。

背景：B1 让"加检查器不用改一行 C++"成立（放一个目录 + `check.py --manifest` 就进侧栏），
但真动手时还是要在 4 件事上对齐，缺一件就"能看见但过不了闸门"：
  ① 目录 / 文件命名与 `--manifest` 字段；
  ② 三条出口齐全：`--manifest`（给工具箱）/ `--json`（给面板）/ `--selftest`（给闸门）+ 报告落盘；
  ③ 有 `--json` 就必须登记进 `速查数据/rules/json_contract.json`，否则 B2 的契约闸门会红；
  ④ 自检里要有**一条正例 + 一条反例** —— 只有正例的自检，变异测试只会告诉
     "这条自检根本没测到那条判定"（H7 踩过）。

本脚本把这 4 件一次做完（③ 默认自动做），生成的壳里带 TODO 指向要改的地方。

用法：
    python 本地工具\工具箱\检查\脚手架\new_checker.py 我的检查器
    python ...\new_checker.py 我的检查器 --group 汇总 --desc "一句话说明"
    python ...\new_checker.py 我的检查器 --no-register    # 不自动登记契约（默认登记）
    python ...\new_checker.py --list                      # 看现有的插件与闸门脚本
    python ...\new_checker.py --selftest                  # 验脚手架自己（生成→发现→跑通→清理）

生成后按提示跑两条命令即可确认"真被发现了"：
    python 本地工具\工具箱\检查\_manifest_all.py     # 应能读到新插件的 name
    build\release\Toolbox.exe --selftest             # 闸门里应有新插件名（自动发现）
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

HERE = os.path.dirname(os.path.abspath(__file__))              # 工具箱/检查/脚手架
CHECKS = os.path.dirname(HERE)                                 # 工具箱/检查
TOOLBOX = os.path.dirname(CHECKS)                              # 工具箱
PY = sys.executable or 'python'

CONTRACT_REL = os.path.join('速查数据', 'rules', 'json_contract.json')
CONTRACT = os.path.join(TOOLBOX, CONTRACT_REL)

BAD_CHARS = set('\\/:*?"<>|')
# 生成物里的 4 个占位：名字 / 分组 / 说明 / 报告相对路径
TEMPLATE = r'''# -*- coding: utf-8 -*-
r"""@@NAME@@ 检查器（由 `检查/脚手架/new_checker.py` 生成）。

加这个检查器**没改一行 C++**：工具箱启动时扫 `检查/<名字>/check.py`、读 `--manifest`，
自动把它加成侧栏一页；`--selftest` 也会被闸门自动收进去（B3）。

三条出口（与其它检查器一致）：
    python check.py --manifest    给工具箱看的元信息
    python check.py --json        机器可读结果（**改字段名要先改 json_contract.json**）
    python check.py --selftest    自检（含一条正例、一条反例）
    python check.py               默认跑一遍，报告写到 `@@REPORT@@`

TODO（你要改的就这一处）：`scan()` 现在只是**示例逻辑**（数仓库里 .c / .h 的行数），
换成你真正要检查的东西。改写时守三条：
  1) 数字**现场算**，不要把结论写死（写死的看板会跟着代码一起烂）；
  2) `--selftest` 至少留**一条正例 + 一条反例** —— 反例是"判据改坏了必须报出来"；
  3) 有 `--json` 就在 `速查数据/rules/json_contract.json` 里登记（生成时已自动登记一份，
     你改了字段名就把那边的 `keys` 一起改，否则契约闸门会点名）。
"""
import argparse
import glob
import json
import os
import sys

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))             # 工具箱/检查/@@NAME@@
TOOLBOX = os.path.abspath(os.path.join(HERE, '..', '..'))     # 工具箱
ROOT = os.path.abspath(os.path.join(TOOLBOX, '..', '..'))     # 仓库根
REPORT_REL = os.path.join('产物', '@@NAME@@报告.md')

MANIFEST = {
    'name': @@NAME_LIT@@,
    'group': @@GROUP_LIT@@,
    'desc': @@DESC_LIT@@,
    'report': REPORT_REL,
}

# 示例扫描范围：仓库里"你写的"两个工程目录（截图/构建产物不算）
SCAN_DIRS = ['EnvMonitor', 'OTA']


def scan():
    """TODO：换成你真正的检查逻辑。返回的 dict 会被 render / --json 直接用。"""
    files = []
    for d in SCAN_DIRS:
        base = os.path.join(ROOT, d)
        if not os.path.isdir(base):
            continue
        for pat in ('**/*.c', '**/*.h'):
            files.extend(glob.glob(os.path.join(base, pat), recursive=True))

    def keep(p):
        low = p.replace('\\', '/').lower()
        if any(x in low for x in ('/fwlib/', '/rte/', '/freertos/', '/objects/', '/listings/')):
            return False
        return not os.path.basename(p).startswith('board_selftest')

    files = [p for p in files if keep(p)]
    lines = todo = 0
    for p in files:
        try:
            with open(p, 'rb') as f:
                raw = f.read()
        except OSError:
            continue
        t = raw.decode('utf-8', 'replace')
        lines += t.count('\n')
        todo += t.count('TODO')
    return {'files': len(files), 'lines': lines, 'todo': todo}


def is_ok(r):
    """判据抽成一个**纯函数**：这样自检能喂错例进去，断言"判据真的会拒"。"""
    return r.get('files', 0) > 0


def render(r):
    return '\n'.join([
        '# @@NAME@@报告', '',
        '> 这是 `检查/脚手架/new_checker.py` 生成的骨架跑出来的报告 —— 把 `scan()` 换掉就是你的检查器。', '',
        '| 指标 | 数值 |', '|---|---|',
        '| 自写源文件 | %d |' % r['files'],
        '| 行数 | %d |' % r['lines'],
        '| TODO 标记 | %d |' % r['todo'], '',
        '扫描范围：`%s` 下的 .c / .h（排除 FWLIB / RTE / FreeRTOS / 构建产物）。'
        % '、'.join(SCAN_DIRS), '',
    ])


def selftest():
    ok = True
    checks = []

    def ck(what, cond, extra=''):
        checks.append((what, bool(cond), str(extra)))

    r = scan()
    ck('正例：真扫到源文件（%d 个）' % r['files'], is_ok(r), r)
    ck('反例：空结果必须判不过（判据真在拦）', not is_ok({'files': 0}), '')
    md = render(r)
    ck('报告结构完成（有指标表）', '| 行数 |' in md, '')
    ck('manifest 有 name / group / report',
       MANIFEST.get('name') and MANIFEST.get('group') and MANIFEST.get('report'), '')

    for what, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', what,
                               ('　' + extra) if (extra and not cond) else ''))
    print('@@NAME@@自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return all(c for _, c, _ in checks)


def main():
    ap = argparse.ArgumentParser(description='@@NAME@@ 检查器（脚手架生成，B3）')
    ap.add_argument('--manifest', action='store_true', help='输出元信息（工具箱读它）')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.manifest:
        print(json.dumps(MANIFEST, ensure_ascii=False))
        return 0
    if a.selftest:
        return 0 if selftest() else 1

    r = scan()
    out = os.path.join(HERE, REPORT_REL)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(render(r))
    if a.json:
        print(json.dumps({'tool': MANIFEST['name'], 'schema': 1, 'ok': is_ok(r),
                          'report': REPORT_REL, **r}, ensure_ascii=False))
    else:
        print('@@NAME@@：%d 个源文件，%d 行，TODO %d 处 → %s'
              % (r['files'], r['lines'], r['todo'], '通过' if is_ok(r) else '不通过'))
        print('报告：%s' % out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
'''

DEFAULT_DESC = '脚手架生成的检查器骨架（B3）：默认只数 .c/.h 行数；请把 scan() 换成真正的检查逻辑。'


def valid_name(name):
    if not name or len(name) > 20:
        return False, '名字要在 1~20 字之间'
    if name.startswith('_'):
        return False, '别用下划线开头（那是工具箱内部文件/临时目录的命名）'
    if any(c in BAD_CHARS for c in name):
        return False, '名字里不能有 \\ / : * ? " < > |'
    if name.endswith('.'):
        return False, '名字别以点结尾'
    return True, ''


def render_template(name, group, desc):
    out = TEMPLATE
    out = out.replace('@@NAME@@', name)
    out = out.replace('@@NAME_LIT@@', json.dumps(name, ensure_ascii=False))
    out = out.replace('@@GROUP_LIT@@', json.dumps(group, ensure_ascii=False))
    out = out.replace('@@DESC_LIT@@', json.dumps(desc, ensure_ascii=False))
    out = out.replace('@@REPORT@@', '产物/%s报告.md' % name)
    return out


def register_contract(contract_path, script_rel, tool, keys):
    """把新检查器登记进契约表。返回 (是否插入, 说明)。

    用**文本插入**而不是整份重写：契约文件是手写的、排版有意义（每支一行 + 折行），
    整份 json.dump 回去会造出一大片与内容无关的 diff。
    """
    try:
        with open(contract_path, encoding='utf-8') as f:
            txt = f.read()
    except OSError as e:
        return False, '读不到契约文件：%s' % e
    if '"script": "%s"' % script_rel in txt:
        return False, '契约里已经有这条了（不重复插）'
    anchor = '\n  ],\n  "excluded"'
    i = txt.find(anchor)
    if i < 0:
        return False, '找不到 tools 收尾锚点（契约文件被改过？）'
    ent = ['    {"script": "%s", "tool": "%s", "args": ["--json"],' % (script_rel, tool),
           '     "keys": [%s]}' % ', '.join('"%s"' % k for k in keys)]
    # 上一行没逗号（它是数组最后一项）→ 补上逗号；新条目**不能**带尾逗号（JSON 不许）。
    txt = txt[:i] + ',\n' + '\n'.join(ent) + txt[i:]
    try:
        json.loads(txt)                       # 先验能解析再落盘，写坏契约比不写更糟
    except ValueError as e:
        return False, '插入后不是合法 JSON：%s' % e
    with open(contract_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(txt)
    return True, '已登记进契约表'


def generate(name, group='检查', desc='', force=False, register=True, contract_path=None):
    """生成 检查/<name>/check.py。返回 (ok, 消息行列表)。"""
    ok, why = valid_name(name)
    if not ok:
        return False, ['名字不合法：%s' % why]
    d = os.path.join(CHECKS, name)
    lines = []
    if os.path.exists(d) and not force:
        return False, ['目录已存在：%s（要覆盖就加 --force）' % d]
    if os.path.exists(d) and not os.path.isdir(d):
        return False, ['同名文件挡路：%s' % d]
    os.makedirs(d, exist_ok=True)
    script = os.path.join(d, 'check.py')
    with open(script, 'w', encoding='utf-8', newline='\n') as f:
        f.write(render_template(name, group, desc or DEFAULT_DESC))
    lines.append('生成：%s' % script)

    if register:
        got, msg = register_contract(contract_path or CONTRACT,
                                     '检查/%s/check.py' % name, name,
                                     ['files', 'lines', 'todo', 'report'])
        lines.append('契约：%s（%s）' % (msg, CONTRACT_REL))
    else:
        lines.append('契约：未登记（--no-register）—— 有 --json 就记得去登记 %s' % CONTRACT_REL)
    return True, lines


def list_plugins():
    """现有插件（检查/<名>/check.py）与闸门脚本（main.cpp 检查器表里的 rel）。"""
    print('插件（工具箱启动时会扫出来加成侧栏页）：')
    for fi in sorted(os.scandir(CHECKS), key=lambda e: e.name):
        if fi.is_dir() and os.path.isfile(os.path.join(fi.path, 'check.py')):
            print('  %s' % fi.name)
    main_cpp = os.path.join(TOOLBOX, '程序', '嵌入式工具箱', 'main.cpp')
    try:
        with open(main_cpp, encoding='utf-8', errors='ignore') as f:
            txt = f.read()
    except OSError:
        print('\n（读不到 main.cpp，闸门脚本列表跳过）')
        return
    import re
    rels = sorted(set(re.findall(r'\{"[^"]+",\s*"(检查/[^"]+\.py)"\}', txt)))
    print('\n闸门脚本（main.cpp 检查器表里显式列的）：')
    for r in rels:
        print('  %s' % r)


def _run(script, *args):
    p = subprocess.run([PY, script] + list(args), capture_output=True, text=True,
                       encoding='utf-8', errors='replace', timeout=180,
                       cwd=os.path.dirname(script))
    return p.returncode, (p.stdout or '') + (p.stderr or '')


def _last_json(text):
    for line in reversed((text or '').strip().splitlines()):
        try:
            return json.loads(line)
        except ValueError:
            continue
    return None


def _load_json_file(path):
    try:
        with open(path, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def selftest():
    """脚手架自检：真生成一份到 检查/ 下（临时目录名以 `_` 开头那种不行 —— 会挡住正常的
    插件目录；所以用 `脚手架自检_<pid>`），走完"发现→跑通→登记→清理"全链，再拆掉。"""
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    back = os.path.basename(CHECKS)
    ck('反例：带斜杠的名字必须被拒', not valid_name('a/b')[0], '')
    ck('反例：下划线开头的名字必须被拒', not valid_name('_tmp')[0], '')
    ck('正例：正常中文名字通过', valid_name('我的检查器')[0])

    name = '脚手架自检_%d' % os.getpid()
    d = os.path.join(CHECKS, name)
    tmp_contract = os.path.join(tempfile.gettempdir(), 'json_contract_selftest_%d.json' % os.getpid())
    tmp_contract2 = tmp_contract + '.manual'
    try:
        try:
            shutil.copyfile(CONTRACT, tmp_contract)
            shutil.copyfile(CONTRACT, tmp_contract2)
        except OSError as e:
            ck('能拷一份契约做登记试验', False, repr(e))
        gok, lines = generate(name, group='检查', desc='脚手架自检临时件', register=True,
                              contract_path=tmp_contract)
        ck('正例：生成成功', gok, '；'.join(lines))
        script = os.path.join(d, 'check.py')
        ck('生成出 check.py', os.path.isfile(script))
        # 生成时顺手登记契约 —— 用拷贝验，不动真契约
        c1 = _load_json_file(tmp_contract)
        ck('生成时自动登记进契约且仍是合法 JSON',
           isinstance(c1, dict) and any(t.get('script') == '检查/%s/check.py' % name
                                       for t in c1.get('tools') or []))
        ck('反例：同一条再登记一次不重复插',
           not register_contract(tmp_contract, '检查/%s/check.py' % name, name, [])[0])

        if os.path.isfile(script):
            rc, out = _run(script, '--manifest')
            m = _last_json(out)
            ck('--manifest 能解析且名字对',
               rc == 0 and isinstance(m, dict) and m.get('name') == name, out.strip()[:80])
            ck('--manifest 带 group 与 report',
               isinstance(m, dict) and m.get('group') and m.get('report'), str(m))

            rc, out = _run(script, '--selftest')
            ck('生成的 --selftest 全绿（含正例 + 反例）', rc == 0, out.strip()[-120:])

            rc, out = _run(script, '--json')
            j = _last_json(out)
            ck('--json 信封齐全（tool/schema/ok）',
               isinstance(j, dict) and j.get('tool') == name and j.get('schema') == 1
               and isinstance(j.get('ok'), bool), str(j)[:100])

            rc, out = _run(script)
            ck('默认跑一次出报告', rc == 0
               and os.path.isfile(os.path.join(d, '产物', '%s报告.md' % name)), out.strip()[-80:])

        # 发现链：工具箱就是跑这个脚本一次读完所有插件 manifest
        rc, out = _run(os.path.join(CHECKS, '_manifest_all.py'))
        arr = _last_json(out)
        ck('能被工具箱发现（_manifest_all.py 读到了它）',
           isinstance(arr, list) and any(isinstance(x, dict) and x.get('name') == name
                                         for x in arr),
           '共 %d 个插件' % (len(arr) if isinstance(arr, list) else -1))

        # 手工登记的路子（对一份**干净**的契约副本）：插得进去、插完仍合法、能读回来
        got, msg = register_contract(tmp_contract2, '检查/%s/check.py' % name, name,
                                     ['files', 'lines', 'todo', 'report'])
        ck('正例：手工登记把条目插进契约', got, msg)
        c2 = _load_json_file(tmp_contract2)
        ck('手工登记后契约仍是合法 JSON', isinstance(c2, dict))
        ck('契约里能找到新条目',
           any(t.get('script') == '检查/%s/check.py' % name for t in c2.get('tools') or []))
    finally:
        shutil.rmtree(d, ignore_errors=True)
        for p in (tmp_contract, tmp_contract2):
            try:
                os.remove(p)
            except OSError:
                pass
        ck('清理掉临时插件目录（%s 没了）' % back, not os.path.exists(d))

    for nm, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', nm,
                               ('　' + extra) if (extra and not cond) else ''))
    print('脚手架自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='新检查器脚手架（B3）：生成→自动登记→可被发现')
    ap.add_argument('name', nargs='?', help='检查器名字（同时是目录名与侧栏页名）')
    ap.add_argument('--group', default='检查', help='侧栏分组（默认「检查」）')
    ap.add_argument('--desc', default='', help='一句话说明（manifest.desc）')
    ap.add_argument('--force', action='store_true', help='覆盖已存在的目录')
    ap.add_argument('--no-register', action='store_true', help='不自动登记 JSON 契约')
    ap.add_argument('--list', action='store_true', help='列出现有插件与闸门脚本')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)
    if a.list:
        list_plugins()
        return 0
    if not a.name:
        ap.print_help()
        return 2

    ok, lines = generate(a.name, a.group, a.desc, a.force, not a.no_register)
    for ln in lines:
        print(ln)
    if not ok:
        return 1
    print('')
    print('接下来（两条就能确认"真被发现了"）：')
    print('  1) python 检查\\_manifest_all.py          → 输出里应能找到 "%s"' % a.name)
    print('  2) build\\release\\Toolbox.exe --selftest   → 闸门里应出现 "%s"' % a.name)
    print('然后改 检查\\%s\\check.py 里的 scan()：换成真正的检查逻辑。' % a.name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
