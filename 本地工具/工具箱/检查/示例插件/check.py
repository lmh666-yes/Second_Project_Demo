# -*- coding: utf-8 -*-
r"""示例检查器插件（D2）—— 也是"怎么写插件"的模板。

**加一个检查器只要三步，不用改一行 C++**：
  1) 在 `检查/<你的名字>/` 下放一个 `check.py`
  2) 实现 `--manifest`（告诉工具箱你叫什么、说明是什么、报告写哪）
  3) 默认运行（不带参数）产出报告，退出码 0 = 通过

工具箱启动时会扫 `检查/*/check.py`，读 `--manifest`，自动把它加成侧栏里的一页
（分组默认「检查」），并挂上「打开报告」按钮。没 manifest 的目录会被静默跳过，
所以插件写坏了最多是"这一页不出现"，不会拖垮整个工具箱。

manifest 字段：
    name    必填，侧栏与页标题
    group   选填，默认「检查」
    desc    选填，页顶说明
    report  选填，相对插件目录的报告路径（用于「打开报告」按钮）

本示例做的事很朴素：数一数仓库里 `.c` / `.h` 的行数、TODO 数、桩函数数，
写一份 `产物/示例报告.md`。**它是模板，不是正经检查器** —— 你照着改即可。

用法：
    python check.py                # 跑一遍，写报告
    python check.py --json         # 机器可读
    python check.py --manifest     # 给工具箱看的元信息
    python check.py --selftest
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

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLBOX = os.path.abspath(os.path.join(HERE, '..', '..'))
ROOT = os.path.abspath(os.path.join(TOOLBOX, '..', '..'))
REPORT_REL = os.path.join('产物', '示例报告.md')

MANIFEST = {
    'name': '示例插件',
    'group': '检查',
    'desc': '示例检查器插件（D2 模板）：统计源码行数 / TODO / 桩函数。'
            '照着 check.py 改就能加自己的检查器，不用改 C++。',
    'report': REPORT_REL,
}

SCAN_DIRS = ['EnvMonitor', 'OTA']


def scan():
    files = []
    for d in SCAN_DIRS:
        base = os.path.join(ROOT, d)
        if not os.path.isdir(base):
            continue
        for pat in ('**/*.c', '**/*.h'):
            files.extend(glob.glob(os.path.join(base, pat), recursive=True))
    # FWLIB / RTE / FreeRTOS / 生成物不算"你写的"
    def keep(p):
        low = p.replace('\\', '/').lower()
        if any(x in low for x in ('/fwlib/', '/rte/', '/freertos/', '/objects/', '/listings/')):
            return False
        return not os.path.basename(p).startswith('board_selftest')
    files = [p for p in files if keep(p)]

    lines = todo = stub = 0
    for p in files:
        try:
            with open(p, 'rb') as f:
                raw = f.read()
        except OSError:
            continue
        for enc in ('utf-8', 'gbk'):
            try:
                t = raw.decode(enc)
                break
            except UnicodeDecodeError:
                t = raw.decode('latin-1', 'replace')
        lines += t.count('\n')
        todo += t.count('TODO')
        stub += t.count('桩函数')
    return {'files': len(files), 'lines': lines, 'todo': todo, 'stub': stub}


def render(r):
    return '\n'.join([
        '# 示例插件报告', '',
        '> 这是 D2 的插件模板跑出来的报告 —— 换个检查逻辑就是你的检查器。', '',
        '| 指标 | 数值 |', '|---|---|',
        '| 自写源文件 | %d |' % r['files'],
        '| 行数 | %d |' % r['lines'],
        '| TODO 标记 | %d |' % r['todo'],
        '| 「桩函数」字样 | %d |' % r['stub'], '',
        '扫描范围：`EnvMonitor/`、`OTA/` 下的 .c/.h，排除 FWLIB / RTE / FreeRTOS / 构建产物。', '',
    ])


def selftest():
    ok = True
    r = scan()
    if r['files'] <= 0:
        print('  [FAIL] 扫到源文件')
        ok = False
    else:
        print('  [PASS] 扫到 %d 个源文件 / %d 行' % (r['files'], r['lines']))
    md = render(r)
    if '| 行数 |' not in md:
        print('  [FAIL] 报告含"行数"行')
        ok = False
    else:
        print('  [PASS] 报告结构完整')
    if MANIFEST.get('name') and MANIFEST.get('report'):
        print('  [PASS] manifest 有 name 与 report')
    else:
        print('  [FAIL] manifest 缺字段')
        ok = False
    print('示例插件自检：%s' % ('通过' if ok else '失败'))
    return ok


def main():
    ap = argparse.ArgumentParser(description='示例检查器插件（D2）')
    ap.add_argument('--manifest', action='store_true', help='输出元信息（工具箱读它）')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.manifest:
        print(json.dumps(MANIFEST, ensure_ascii=False))
        return
    if a.selftest:
        sys.exit(0 if selftest() else 1)

    r = scan()
    out = os.path.join(HERE, REPORT_REL)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(render(r))
    if a.json:
        print(json.dumps({'tool': 'example_plugin', 'schema': 1, 'ok': True,
                          'report': REPORT_REL, **r}, ensure_ascii=False))
    else:
        print('示例插件：%d 个源文件，%d 行，TODO %d 处，桩函数 %d 处'
              % (r['files'], r['lines'], r['todo'], r['stub']))
        print('报告：%s' % out)


if __name__ == '__main__':
    main()
