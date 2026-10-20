# -*- coding: utf-8 -*-
r"""JSON 契约校验（B2）：把各脚本的 `--json` 收成**一份契约**，谁改字段名谁红。

为什么需要：各检查器的 `--json` 是「体检面板 / 报告 / 阶段判定 / 覆盖率看板」的输入口。
以前"契约"只散在各脚本自己的 `--selftest` 里（各测各的）—— **没人守跨脚本的统一口径**：
字段名一改，面板读不到，面板显示的是空/绿，而不是报错。

做法：契约写在 `速查数据/rules/json_contract.json`（一份，与阈值/板表同一个目录同一套用法），
本脚本按它：
  ① 用契约里写好的参数跑每支脚本；
  ② 取输出的最后一行 JSON；
  ③ 比**信封**（tool 名 / schema 是 int≥1 / ok 是 bool）+ 必需键在不在。
谁不对就 FAIL，并**点名"哪支脚本、哪个键"**。退出码 0 = 全过，1 = 有违约。

用法：
    python 本地工具\工具箱\检查\契约校验\json_contract.py
    python ...\json_contract.py --json      # 机器可读
    python ...\json_contract.py --selftest
"""
import argparse
import json
import os
import subprocess
import sys

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
TOOLBOX = os.path.abspath(os.path.join(HERE, '..', '..'))     # 工具箱
ROOT = find_root(HERE) or os.path.abspath(os.path.join(TOOLBOX, '..', '..'))
RULES_REL = os.path.join('本地工具', '工具箱', '速查数据', 'rules', 'json_contract.json')

REQUIRED_ENVELOPE = ('tool', 'schema', 'ok')


def load_contract(root):
    p = os.path.join(root, RULES_REL)
    with open(p, encoding='utf-8') as f:
        return json.load(f), p


def check_one(entry, obj):
    """按契约条目校验一份 JSON，返回问题清单（空 = 通过）。

    单独抽出来是为了能离线自检：正例（真输出）必须 0 问题，错例（删键 / 类型改掉）
    必须**点名到底是哪个键** —— 不然"校验器永远报绿"就是摆设。
    """
    bad = []
    if not isinstance(obj, dict):
        return ['输出的最后一行不是 JSON 对象']
    for k in REQUIRED_ENVELOPE:
        if k not in obj:
            bad.append('缺信封键 %s' % k)
    if 'tool' in obj and obj.get('tool') != entry.get('tool'):
        bad.append('tool 名不对：期望 %r 实际 %r' % (entry.get('tool'), obj.get('tool')))
    if 'schema' in obj and not isinstance(obj.get('schema'), int):
        bad.append('schema 必须是 int（实际 %r）' % (obj.get('schema'),))
    if 'schema' in obj and isinstance(obj.get('schema'), int) and obj['schema'] < 1:
        bad.append('schema 必须 ≥ 1（实际 %r）' % (obj.get('schema'),))
    if 'ok' in obj and not isinstance(obj.get('ok'), bool):
        bad.append('ok 必须是 bool（实际 %r）' % (obj.get('ok'),))
    for k in entry.get('keys') or []:
        if k not in obj:
            bad.append('缺字段 %s' % k)
    return bad


def run_tool(root, entry, timeout=180):
    """跑一支脚本，返回 (最后一行 JSON 或 None, 说明)。

    契约里的 `script` 是**相对工具箱**的路径（`检查/<名字>/xxx.py`）——
    这样契约表读起来短，也不必每行都写 `本地工具/工具箱/` 前缀。
    """
    rel = entry.get('script') or ''
    path = os.path.join(TOOLBOX, rel)
    if not os.path.isfile(path):
        return None, '脚本不存在：%s' % path
    try:
        p = subprocess.run([sys.executable or 'python', path] + list(entry.get('args') or []),
                           capture_output=True, text=True, encoding='utf-8',
                           errors='replace', timeout=timeout, cwd=os.path.dirname(path))
    except subprocess.TimeoutExpired:
        return None, '超时（>%ds）' % timeout
    except OSError as e:
        return None, '跑不起来：%s' % e
    for line in reversed((p.stdout or '').strip().splitlines()):
        try:
            return json.loads(line), ''
        except ValueError:
            continue
    tail = (p.stdout or '').strip().splitlines()
    return None, '最后一行不是 JSON（rc=%s，末行：%s）' % (
        p.returncode, (tail[-1][:80] if tail else '（无输出）'))


def check_all(root, contract, quiet=False):
    """跑契约表里每支脚本并校验，返回 (结果列表, 是否全过)。"""
    rows = []
    for e in contract.get('tools') or []:
        obj, note = run_tool(root, e)
        probs = check_one(e, obj) if obj is not None else [note or '没拿到 JSON']
        rows.append({'script': e.get('script'), 'tool': e.get('tool'),
                     'ok': not probs, 'problems': probs})
        if not quiet:
            print('  [%s] %-46s %s' % ('PASS' if not probs else 'FAIL',
                                       e.get('script') or '?',
                                       '' if not probs else '；'.join(probs[:3])))
    return rows, all(r['ok'] for r in rows)


# ========================================================================
# 自检（不需要硬件；契约表自洽 + 正例/错例都验）
# ========================================================================

def selftest():
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    try:
        contract, path = load_contract(ROOT)
    except Exception as e:
        ck('契约文件能读（%s）' % RULES_REL, False, repr(e))
        for name, cond, extra in checks:
            print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                                   ('　' + extra) if (extra and not cond) else ''))
        print('JSON 契约自检：%d 通过 / %d 失败' % (0, len(checks)))
        return False

    ck('契约文件能读', True, path)
    tools = contract.get('tools') or []
    ck('契约表非空（≥10 支）', len(tools) >= 10, str(len(tools)))
    ids = [t.get('script') for t in tools]
    ck('每条都有 script / tool / args / keys',
       all(t.get('script') and t.get('tool') and isinstance(t.get('args'), list)
           and isinstance(t.get('keys'), list) and t.get('keys') for t in tools),
       str([t.get('script') for t in tools if not t.get('keys')][:3]))
    ck('脚本路径不重复', len(ids) == len(set(ids)), str(len(ids)))
    miss = [t['script'] for t in tools if not os.path.isfile(os.path.join(TOOLBOX, t['script']))]
    ck('契约里的脚本都真实存在', not miss, str(miss[:3]))
    ex = [e.get('script') for e in contract.get('excluded') or []]
    ck('excluded 与 tools 不重叠', not (set(ex) & set(ids)), str(sorted(set(ex) & set(ids))[:3]))

    # 真跑一支最快的（进度扫描），验"跑 → 解析 → 比对"整条链
    fast = [t for t in tools if t['script'].endswith('progress_scan.py')]
    if fast:
        obj, note = run_tool(ROOT, fast[0])
        ck('真跑一支并解析出 JSON', obj is not None, note)
        if obj is not None:
            ck('正例：真输出 0 问题', check_one(fast[0], obj) == [], str(check_one(fast[0], obj)))
            # 错例一：删掉一个必需键 → 必须点名那个键
            k = fast[0]['keys'][0]
            bad1 = dict(obj)
            bad1.pop(k, None)
            probs1 = check_one(fast[0], bad1)
            ck('错例：删掉字段 %s 要被抓到并点名' % k, any(k in p for p in probs1), str(probs1[:2]))
            # 错例二：把信封 ok 改成字符串 → 必须报"类型不对"
            bad2 = dict(obj)
            bad2['ok'] = 'yes'
            probs2 = check_one(fast[0], bad2)
            ck('错例：ok 不是 bool 要被抓到', any('bool' in p for p in probs2), str(probs2[:2]))
            # 错例三：把 tool 名改掉 → 必须点名
            bad3 = dict(obj)
            bad3['tool'] = 'wrong_name'
            probs3 = check_one(fast[0], bad3)
            ck('错例：tool 名改了要被抓到', any('tool' in p for p in probs3), str(probs3[:2]))
    else:
        ck('契约里有进度扫描（用最快的那支验整条链）', False)

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('JSON 契约自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='JSON 契约校验（统一信封 + 逐脚本字段）')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    contract, path = load_contract(ROOT)
    print('契约：%s（version=%s，%d 支脚本）'
          % (path, contract.get('version'), len(contract.get('tools') or [])))
    rows, ok = check_all(ROOT, contract, quiet=a.json)
    if a.json:
        print(json.dumps({'tool': '契约校验', 'schema': 1, 'ok': ok, 'rows': rows},
                         ensure_ascii=False))
    else:
        nbad = [r for r in rows if not r['ok']]
        print('契约校验：%d 支通过 / %d 支违约%s'
              % (len(rows) - len(nbad), len(nbad),
                 '' if not nbad else '　→ 先改脚本或改契约（%s）' % RULES_REL))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
