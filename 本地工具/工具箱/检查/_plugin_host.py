# -*- coding: utf-8 -*-
r"""插件壳的公共实现（B1）—— 让"已经有脚本、但没界面"的命令行检查器，几行就挂进工具箱侧栏。

背景：插件约定是 `检查/<名字>/check.py --manifest`（D2 定的），
但每个插件都要重复写一遍 argparse / 子进程 / manifest 样板。所以把样板收在这里：
每个插件的 `check.py` 只剩"我是谁 + 我调哪个上游脚本 + 默认带什么参数"。

本文件放在 `检查/` 根目录（**不在**任何插件目录里）—— 扫描插件的代码只认
`检查/<目录>/check.py`，所以它不会被当成一个插件页。

用法（插件壳里）：
    from _plugin_host import main
    main(upstream='code_index.py', args=['--index', '--board', '板1'],
         name='代码索引', desc='...', report='产物/API索引_板1.md')

它支持三条出口，与其它检查器一致：
    --manifest   给工具箱看的元信息（JSON）
    （默认）     用固定参数调上游脚本，原样转发输出与退出码
    --selftest   守住 manifest 字段与"上游脚本在不在"
"""
import argparse
import json
import os
import subprocess
import sys


def _host_dir():
    """当前插件壳所在目录（= 上游脚本所在目录）。

    优先用 `sys.argv[0]`（插件壳是 `python check.py …` 这样被调起的）——
    工具箱批量读 manifest 时是用 runpy 跑的，那时 `__main__.__file__` 指向驱动脚本
    而不是本文件（踩过），只看它会把目录认错。
    """
    if sys.argv and sys.argv[0] and os.path.isfile(sys.argv[0]):
        return os.path.dirname(os.path.abspath(sys.argv[0]))
    return os.path.dirname(os.path.abspath(sys.modules['__main__'].__file__))


def build_manifest(name, group, desc, report):
    m = {'name': name, 'group': group}
    if desc:
        m['desc'] = desc
    if report:
        m['report'] = report
    return m


def selftest(up, manifest, args):
    """插件壳自检：不需要硬件，也**不跑**上游（那是工具箱闸门里另跑一遍的事）。"""
    checks = []

    def ck(what, ok, extra=''):
        checks.append((what, bool(ok), str(extra)))

    ck('manifest 有 name / group / desc',
       manifest.get('name') and manifest.get('group') and manifest.get('desc'))
    ck('manifest 能被 JSON 序列化（工具箱就是这么读的）',
       json.loads(json.dumps(manifest)).get('name') == manifest.get('name'), manifest.get('name'))
    ck('上游脚本真实存在', os.path.isfile(up), up)
    ck('默认参数都是字符串', all(isinstance(x, str) for x in args), str(list(args)[:4]))
    rp = manifest.get('report') or ''
    ck('report 是相对路径（不以 / 开头）', (not rp) or (not rp.startswith('/')), rp)

    for what, ok, extra in checks:
        print('  [%s] %s%s' % ('PASS' if ok else 'FAIL', what,
                               ('　' + extra) if (extra and not ok) else ''))
    print('插件壳自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return 0 if all(c for _, c, _ in checks) else 1


def main(upstream, args=(), name='', group='检查', desc='', report=''):
    here = _host_dir()
    up = os.path.join(here, upstream)
    manifest = build_manifest(name, group, desc, report)

    ap = argparse.ArgumentParser(description=desc or name)
    ap.add_argument('--manifest', action='store_true', help='打印元信息（给工具箱）')
    ap.add_argument('--selftest', action='store_true', help='只验插件壳本身')
    ns, extra = ap.parse_known_args()

    if ns.manifest:
        print(json.dumps(manifest, ensure_ascii=False))
        return 0
    if ns.selftest:
        return selftest(up, manifest, list(args))

    if not os.path.isfile(up):
        print('找不到上游脚本：%s' % up)
        return 1
    cmd = [sys.executable or 'python', up] + list(args) + list(extra)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True,
                           encoding='utf-8', errors='replace', cwd=here)
    except OSError as e:
        print('跑不起来：%s' % e)
        return 1
    sys.stdout.write((p.stdout or '') + (p.stderr or ''))
    return p.returncode
