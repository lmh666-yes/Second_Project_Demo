# -*- coding: utf-8 -*-
r"""流程卡 → 可编译最小 demo 工程（D1）。

病根：手册里那 39 张「流程卡」都带一整段示例代码（`dataset.json` 里 `_t == 'flow'` 的
`e` 字段），但**没人验过这些代码还能不能编译**。手抄到工程里才发现"函数名少个字母 /
SPL 里根本没这个宏 / 依赖的头没带过来"，那时候已经烧了半天板子了。

做法：每张卡一键生成"只含这张卡"的最小工程 —— **头文件 + 时钟 + 调用**：
    · 头文件：卡自己要的 + 现场自动补的（下面"自动补头"一节）
    · 时钟：`SYS_CLK_ToHighSpeed()` + `SYS_TICK_Init()`（工程里现成的时钟/滴答）
    · 调用：调一次卡里的入口函数（`*_init` / `*_boot` / 卡自带的 `main`）
生成物是 `产物/流程卡demo/<序号>_<卡名>/demo.c` + `README.md`，再**用 armcc 真编译一遍**校验。

自动补头（这是本工具的核心动作，不是花活）：
    为了让"缺头文件"只在**真的缺**时才报，编译——解析报错——补一条——再编译，最多 5 轮：
      · `cannot open source input file "X.h"` → 全仓库按**文件名**找 X.h，找到就补 `-I`
        （两板同名头文件内容不同：先按"这张卡的主用板"排序，主用板在前）
      · `identifier "X" is undefined` / `function "X" declared implicitly` → 在
        两板 FWLIB/inc + FreeRTOS/inc + F4 的 DFP（SPL/CMSIS）里找声明它的头，补 `#include`
      · FreeRTOS 家族的硬性包含顺序（FreeRTOS.h 必须在 queue.h 之前）单独兜一下
    **补不上才报缺**，并把"补了哪些头"如实写进报告 —— 这是"能成的卡尽量都能成"，
    不是"把编译错误藏起来"。

如实报缺（报什么就是什么，不改卡的内容）：
    · 缺头文件：找遍全仓库都没有 → 点名 `X.h`
    · 缺函数/符号：SPL 与工程头里都没有这个标识符 → 点名（还会说它是不是**别的卡**里定义的）
    · 正文自身有错：首条 error 原文照抄（例：一对引号没闭上）
    · 无代码段：整张卡都是讲解文字、没有代码 → 明说"不生成 demo"（卡⑪就是这种）

用法：
    python 本地工具\工具箱\检查\流程卡demo\make_demo.py
    python ...\make_demo.py --card 22          # 只生成某一张（序号从 1 起，与手册编号一致）
    python ...\make_demo.py --out 产物\流程卡demo
    python ...\make_demo.py --no-compile       # 只生成，不跑 armcc
    python ...\make_demo.py --json
    python ...\make_demo.py --selftest

`--selftest` 退出码：0=全过；1=有失败；**3=跳过**（本机没 Keil → 真编译那几条不测，
其余断言照跑）。与 gen_selftest 同一套约定，工具箱闸门把 3 当"跳过"而非失败。

`--json`：跑全套会真编 39 次 armcc（分钟级），所以按约定登记在 `json_contract.json` 的
`excluded` 里（不进闸门），信封三键 `tool/schema/ok` 照补，由本脚本 `--selftest` 自守。
"""
import argparse
import datetime
import glob
import json
import os
import re
import shutil
import sys
import tempfile

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass

# 复用 A4「阶段自检生成」那套 armcc 机制：find_armcc / keil_roots / read_uvprojx /
# check_compile（它支持 extra_incs，正好用来把"主用板"的 include 排到前面）。
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '构建烧录'))
import gen_selftest as gs                                       # noqa: E402


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
DS_REL = os.path.join('本地工具', '工具箱', '速查数据', 'data', 'dataset.json')
CFG_REL = os.path.join('本地工具', '工具箱', '速查数据', 'rules', 'board_envmonitor.json')
DEFAULT_OUT = os.path.join('产物', '流程卡demo')
REPORT_REL = os.path.join('产物', '流程卡demo报告.md')

# 标准 C 头（armcc 自带；卡里常用却没写 include 的，统一补上，免得报成"缺函数"）
STD_INCS = ['stdint.h', 'string.h', 'stdio.h']
# 最小工程的骨架头：SPL + 工程的时钟/滴答（"头文件 + 时钟 + 调用"里的头与时钟）
BASE_INCS = ['stm32f4xx.h', 'sys_clock.h', 'sys_tick.h']
# FreeRTOS 家族：包含顺序是硬性的（FreeRTOS.h 必须在前）
RTOS_FAMILY = ('queue.h', 'semphr.h', 'task.h', 'timers.h', 'event_groups.h',
               'stream_buffer.h', 'projdefs.h', 'portmacro.h', 'list.h')

INC_RE = re.compile(r'^\s*#include\s+[<"]([^>"]+)[>"]\s*$', re.M)
# 顶层函数定义（够用的启发式：返回类型 + 名字 + 形参表 + `{`）
DEF_RE = re.compile(r'^\s*(?:static\s+)?(?:void|int|unsigned|uint\w*_t|int\w*_t|float|double|char|bool|long)'
                    r'\s+(?:[\w\*]+\s+)?(\w+)\s*\(([^)]*)\)\s*\{', re.M)
MAIN_RE = re.compile(r'(?<![A-Za-z0-9_])main\s*\(')
COMMENT_RE = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
# 入口函数偏好：卡里的"起手函数"通常叫 init / boot / setup…
ENTRY_PREF = ('init', 'boot', 'setup', 'start', 'begin', 'demo')

MISS_RE = re.compile(r'cannot open source input file "([^"]+)"')
UNDEF_RE = re.compile(r'identifier "([^"]+)" is undefined')
IMPL_RE = re.compile(r'#223-D: function "([^"]+)" declared implicitly')
ERR_RE = re.compile(r'line (\d+): Error:\s*#(\d+)[-D]*: (.*)')
RTOS_ORDER_RE = re.compile(r'#error directive:\s*"include FreeRTOS\.h"')
ILLEGAL = re.compile(r'[\\/:*?"<>|\r\n\t]+')


def with_envelope(name, obj, ok=True):
    d = dict(obj)
    d['tool'], d['schema'], d['ok'] = name, 1, bool(ok)
    return d


def read_text(path):
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ('utf-8', 'gbk'):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace')


# ========================================================================
# 数据 / 板级配置
# ========================================================================

def load_cards(root):
    """手册里 kind=flow 的那批卡（顺序即手册编号顺序）。"""
    with open(os.path.join(root, DS_REL), encoding='utf-8') as f:
        d = json.load(f)
    return [c for c in d.get('all', []) if c.get('_t') == 'flow']


def load_boards(root):
    try:
        with open(os.path.join(root, CFG_REL), encoding='utf-8') as f:
            bs = json.load(f).get('boards') or []
    except Exception:
        bs = []
    return [b for b in bs if b.get('dir') and b.get('uvprojx')]


def fwlib_inc(board, root):
    return os.path.join(root, board['dir'], 'FWLIB', 'inc')


def rtos_inc(board, root):
    return os.path.join(root, board['dir'], 'FreeRTOS', 'inc')


def card_includes(card):
    """卡正文里自己写的 #include（正文里这些行会在生成时去掉、统一挪到文件头）。"""
    return [m.group(1) for m in INC_RE.finditer(card.get('e') or '')]


def has_code(card):
    """卡里有没有真代码（把注释剥掉还剩下东西才算）。"""
    return bool(COMMENT_RE.sub('', card.get('e') or '').strip())


def pick_board(card, boards, root):
    """选"主用板"：这张卡引用的头文件，哪块板的 FWLIB/inc 命中得多就用哪块。"""
    hs = set(card_includes(card))
    best, best_n = boards[0], -1
    for b in boards:
        n = sum(1 for h in hs if os.path.isfile(os.path.join(fwlib_inc(b, root), h)))
        if n > best_n:
            best, best_n = b, n
    return best, best_n


def cand_dirs(board, boards, root, kr):
    """补头/找声明时的搜索目录（主用板在最前；两板同名头以主用板为准）。

    PACK 里可能同时装着 STM32F1 的 DFP（`misc.h` 等同名头会先命中 F1 版 → 触发 F1 的
    `#error "select first the target STM32F10x"`），所以只对 **PACK 下** 的目录筛一遍：
    含 stm32f4xx.h 或 stm32f4xx_*.h 才算 F4。工程自己的 FWLIB/inc 不做这个筛
    （它装的是项目头，本来就没有 stm32f4xx_*.h）。
    """
    out = []
    for b in [board] + [x for x in boards if x is not board]:
        out += [fwlib_inc(b, root), rtos_inc(b, root)]
    d = []
    for pat in (os.path.join(kr, 'ARM', 'PACK', '*', '*', '*', 'CMSIS', 'Include'),
                os.path.join(kr, 'ARM', 'PACK', '*', '*', '*', 'Device', 'Include'),
                os.path.join(kr, 'ARM', 'PACK', '*', '*', '*', 'Device', 'StdPeriph_Driver', 'inc'),
                os.path.join(kr, 'ARM', 'CMSIS', 'Include')):
        d += glob.glob(pat)
    pack = os.sep + 'PACK' + os.sep
    for x in d:
        if not os.path.isdir(x):
            continue
        if pack in x and not (os.path.isfile(os.path.join(x, 'stm32f4xx.h'))
                              or glob.glob(os.path.join(x, 'stm32f4xx_*.h'))):
            continue
        out.append(x)
    seen, res = set(), []
    for x in out:
        if not os.path.isdir(x):
            continue
        n = os.path.normcase(os.path.abspath(x))
        if n not in seen:
            seen.add(n)
            res.append(x)
    return res


_HDR_TEXT = {}


def _header_text(path):
    if path not in _HDR_TEXT:
        try:
            _HDR_TEXT[path] = COMMENT_RE.sub('', read_text(path))
        except OSError:
            _HDR_TEXT[path] = ''
    return _HDR_TEXT[path]


def find_header_file(name, dirs):
    """按**文件名**找头文件（对应 cannot open source input file）。返回 (名字, 所在目录)。"""
    for d in dirs:
        if os.path.isfile(os.path.join(d, name)):
            return name, d
    for d in dirs:
        hits = glob.glob(os.path.join(d, '**', name), recursive=True)
        if hits:
            return name, os.path.dirname(hits[0])
    return '', ''


def find_header_declaring(sym, dirs):
    """找声明/定义了这个标识符的头文件。返回 (头文件名, 所在目录)。"""
    pat = re.compile(r'(?<![A-Za-z0-9_])' + re.escape(sym) + r'(?![A-Za-z0-9_])')
    for d in dirs:
        for h in sorted(glob.glob(os.path.join(d, '*.h'))):
            if pat.search(_header_text(h)):
                return os.path.basename(h), d
    return '', ''


def card_defines(cards):
    """{标识符: [卡序号...]}：哪些名字是**在哪张卡的示例里**定义的（跨卡依赖的提示用）。"""
    out = {}
    for i, c in enumerate(cards):
        body = COMMENT_RE.sub('', c.get('e') or '')
        for m in DEF_RE.finditer(body):
            out.setdefault(m.group(1), []).append(i + 1)
    return out


# ========================================================================
# 生成最小工程
# ========================================================================

def pick_entry(card):
    """从卡正文里挑一个"零参入口"来调用（`*_init`/`*_boot` 优先）。"""
    body = COMMENT_RE.sub('', card.get('e') or '')
    defs = [(m.group(1), m.group(2).strip()) for m in DEF_RE.finditer(body)]
    for name, prm in defs:
        if prm in ('', 'void') and any(p in name.lower() for p in ENTRY_PREF):
            return name
    for name, prm in defs:
        if prm in ('', 'void'):
            return name
    return ''


def build_demo(card, includes, strip_main=True):
    """拼出最小工程的 .c 正文。返回 (文本, 说明 dict)。

    三段 = 头文件 + 时钟 + 调用（外加卡的正文原样搬进来，只做两处机械处理：
    正文里的 `#include` 行挪到文件头；卡自带的 `main()` 改名 `card_main()`，
    免得跟我们生成的 main 撞名 —— 撞名是编译错误，不是卡的问题）。
    """
    e = card.get('e') or ''
    body = INC_RE.sub('', e)
    had_main = bool(MAIN_RE.search(body))
    if had_main and strip_main:
        body = MAIN_RE.sub('card_main(', body)
    entry = pick_entry(card)
    call = 'card_main();' if had_main else ((entry + '();') if entry else '')

    L = ['/* ============================================================',
         ' *  最小 demo 工程 —— 「%s」' % (card.get('n') or ''),
         ' *  由 工具箱/检查/流程卡demo/make_demo.py 生成（D1），别手改：',
         ' *    头文件 → 卡自己要的 + 现场补的；时钟 → SYS_CLK/SYS_TICK；调用 → 卡里的入口。',
         ' *  正文来自速查数据集（dataset.json 的 flow 卡示例），原样搬运，未做改写。',
         ' * ============================================================ */']
    seen = set()
    for h in STD_INCS:
        if h not in seen:
            seen.add(h)
            L.append('#include <%s>' % h)
    for h in BASE_INCS + list(includes):
        if h not in seen:
            seen.add(h)
            L.append('#include "%s"' % h)
    L += ['', '/* ==================== 卡的正文（原样） ==================== */', '', body, '',
          '/* ==================== 调用（头文件 + 时钟 + 调用） ==================== */',
          'int main(void)',
          '{',
          '    (void)SYS_CLK_ToHighSpeed();   /* 时钟：168MHz（上电 SystemInit 已配好，这里显式确认一次） */',
          '    SYS_TICK_Init();               /* 1ms 滴答：延时/超时都靠它 */']
    if call:
        L.append('    %-30s /* 卡里的入口 */' % call)
    else:
        L.append('    /* 这张卡没有零参入口，只验证正文可编译 */')
    L += ['    for (;;) { }',
          '}',
          '']
    return '\n'.join(L), {'had_main': had_main, 'entry': entry, 'call': call}


# ========================================================================
# 编译 + 自动补头 + 如实报缺
# ========================================================================

def classify(log):
    """把 armcc 日志分成四类（离线可测，不碰文件系统）。"""
    return {'missing_headers': sorted(set(MISS_RE.findall(log))),
            'missing_funcs': sorted(set(IMPL_RE.findall(log))),
            'undefined': sorted(set(UNDEF_RE.findall(log))),
            'errors': [m.strip() for _l, _n, m in ERR_RE.findall(log)]}


def analyze_card(card, boards, root, kr, workdir, do_compile=True):
    """生成 + 编译校验一张卡。返回结果 dict（与报告/--json 共用的形状）。"""
    board, hit_n = pick_board(card, boards, root)
    res = {'id': 0, 'name': card.get('n') or '', 'board': board.get('name'),
           'entry': '', 'status': 'uncompiled',
           'includes': [], 'auto_includes': [], 'missing_headers': [],
           'missing_funcs': [], 'undefined': [], 'errors': [], 'notes': []}
    if not has_code(card):
        res['status'] = 'nocode'
        res['notes'].append('整张卡没有代码段（纯讲解），不生成 demo')
        return res
    if not kr or not do_compile:
        res['notes'].append('本机没找到 Keil' if not kr else '未编译（--no-compile）')
        res['entry'] = pick_entry(card)
        res['includes'] = card_includes(card)
        res['status'] = 'uncompiled'
        return res

    proj = os.path.join(root, board['dir'])
    uv = os.path.join(proj, board['uvprojx'])
    dirs = cand_dirs(board, boards, root, kr)
    includes = list(card_includes(card))          # 卡自己的 + 现场补的（顺序即最终 include 顺序）
    extra = list(dirs)
    log, ok, added = '', False, []
    for _round in range(5):
        text, meta = build_demo(card, includes)
        path = os.path.join(workdir, '_demo.c')
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        ok, log, _incs = gs.check_compile(path, proj, uv, kr, extra_incs=extra)
        if ok and not IMPL_RE.findall(log):
            break
        prog = False
        for miss in MISS_RE.findall(log):         # 缺文件：按文件名找，补 -I
            h, d = find_header_file(miss, dirs)
            if h:
                if d not in extra:
                    extra.append(d)
                    prog = True
                if h not in includes:
                    includes.append(h)
                    added.append(h)
                    prog = True
            else:
                res['missing_headers'].append(miss)
        for sym in set(UNDEF_RE.findall(log)) | set(IMPL_RE.findall(log)):   # 缺声明：补 #include
            h, _d = find_header_declaring(sym, dirs)
            if h and h not in includes:
                includes.append(h)
                added.append(h)
                prog = True
        if any(h in RTOS_FAMILY for h in includes) or RTOS_ORDER_RE.search(log):
            for x in ('task.h', 'FreeRTOS.h'):
                if x in includes:
                    includes.remove(x)
                includes.insert(0, x)
            prog = True
        if not prog:
            break

    cls = classify(log)
    res['entry'] = 'card_main（卡自带 main，生成时改名）' if meta['had_main'] else meta['entry']
    res['had_main'] = meta['had_main']
    res['includes'] = list(includes)
    res['auto_includes'] = sorted(set(added))
    res['missing_headers'] = sorted(set(cls['missing_headers']))
    # 缺函数/符号：SPL 与工程头里都找不到声明的那些（编译通过也会留在这里，是链接期问题）
    unresolved = []
    for sym in sorted(set(cls['missing_funcs']) | set(cls['undefined'])):
        h, _d = find_header_declaring(sym, dirs)
        if not h:
            unresolved.append(sym)
    res['missing_funcs'] = sorted(set(cls['missing_funcs']) & set(unresolved))
    res['undefined'] = sorted(set(cls['undefined']) & set(unresolved))
    res['errors'] = cls['errors'][:3]
    res['status'] = 'pass' if ok else 'fail'
    if ok and (res['missing_funcs'] or res['undefined']):
        res['notes'].append('编译通过，但这些名字在工程头里没有声明 —— 链接时会缺（见缺函数）')
    if not ok and not res['missing_headers'] and not res['undefined'] and res['errors']:
        res['notes'].append('不是缺头/缺符号，是正文自身有问题（首条错误见下表）')
    return res


def annotate_other_cards(res, defmap):
    """给"缺函数"补一句：这个名字其实在哪张卡的示例里定义过（跨卡依赖要说清）。"""
    where = {}
    for k in ('missing_funcs', 'undefined'):
        for sym in res.get(k) or []:
            if sym in defmap:
                where[sym] = defmap[sym]
    res['defined_in_cards'] = where
    return res


# ========================================================================
# 落盘 + 报告
# ========================================================================

def safe_name(name):
    return ILLEGAL.sub('_', (name or '')).strip('_')[:40] or 'card'


def write_demo(out_dir, idx, card, res):
    d = os.path.join(out_dir, '%02d_%s' % (idx + 1, safe_name(card.get('n'))))
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, 'demo.c')
    if has_code(card):
        text, _meta = build_demo(card, res.get('includes') or card_includes(card))
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    rd = ['# 流程卡 demo · %02d %s' % (idx + 1, card.get('n') or ''), '',
          '- 主用板：%s（这张卡引用的头文件多数在这块板）' % (res.get('board') or ''),
          '- 入口：%s' % (res.get('entry') or ('卡自带 main → 改名 card_main 后调用' if res.get('had_main') else '（无零参入口）')),
          '- 结果：%s' % res.get('status'), '']
    if has_code(card):
        rd += ['## 这个最小工程由三部分组成', '',
               '1. **头文件**：卡自己要的 + 现场补的',
               '2. **时钟**：`SYS_CLK_ToHighSpeed()` + `SYS_TICK_Init()`',
               '3. **调用**：调一次卡里的入口函数（见上）', '',
               '```', 'demo.c 已生成在同目录；把它加进任意 F407 工程即可（或在 Keil 里',
               'Add Existing Files，或命令行 armcc -c demo.c 单独编）。', '```', '']
    else:
        rd += ['这张卡是纯讲解（没有代码段），没有生成 demo.c —— 不假装生成一个空壳。', '']
    if res.get('auto_includes'):
        rd += ['## 现场补的头（为了尽量能编过）', ''] + ['- `%s`' % h for h in res['auto_includes']] + ['']
    if res.get('missing_headers'):
        rd += ['## 缺头文件（全仓库都没有）', ''] + ['- `%s`' % h for h in res['missing_headers']] + ['']
    if res.get('missing_funcs') or res.get('undefined'):
        rd += ['## 缺函数 / 缺符号（SPL 与工程头里都没有声明）', '']
        for sym in sorted(set(res.get('missing_funcs', []) + res.get('undefined', []))):
            n = (res.get('defined_in_cards') or {}).get(sym)
            extra = '（它在卡 %s 的示例里定义过 —— 这张卡没带上）' % '、'.join('%02d' % x for x in n) if n else ''
            rd.append('- `%s`%s' % (sym, extra))
        rd.append('')
    if res.get('errors'):
        rd += ['## 正文自身的错误（首几条）', ''] + ['- `%s`' % x for x in res['errors']] + ['']
    with open(os.path.join(d, 'README.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(rd))
    return d


def render_report(results, out_dir):
    n_pass = sum(1 for r in results if r['status'] == 'pass')
    n_fail = sum(1 for r in results if r['status'] == 'fail')
    n_nocode = sum(1 for r in results if r['status'] == 'nocode')
    n_unc = sum(1 for r in results if r['status'] == 'uncompiled')
    L = ['# 流程卡 → 可编译最小 demo 工程 · 报告', '',
         '生成时间：%s' % datetime.datetime.now().strftime('%Y-%m-%d %H:%M'),
         '生成位置：`%s`' % out_dir, '',
         '## 结论', '',
         '- 总共 **%d** 张流程卡' % len(results),
         '- **能成（armcc 编译通过）：%d 张**' % n_pass,
         '- 不能成：**%d 张**（下面逐张点名缺什么）' % n_fail,
         '- 无代码段（纯讲解，不生成）：%d 张' % n_nocode]
    if n_unc:
        L.append('- 未编译：%d 张（本机没 Keil 或 --no-compile）' % n_unc)
    L += ['', '每张卡都一键生成了"只含这张卡"的最小工程（头文件 + 时钟 + 调用），',
          '缺头文件/缺函数的卡**只报缺什么，不改卡的内容**。', '',
          '## 逐张', '',
          '| # | 卡 | 主用板 | 入口 | 状态 | 缺什么 |', '|---|---|---|---|---|---|']
    for r in results:
        miss = []
        if r['missing_headers']:
            miss.append('缺头 ' + '、'.join(r['missing_headers']))
        funcs = sorted(set(r.get('missing_funcs', []) + r.get('undefined', [])))
        if funcs:
            miss.append('缺函数/符号 ' + '、'.join(funcs))
        if r['errors'] and r['status'] == 'fail' and not miss:
            miss.append(r['errors'][0][:60])
        st = {'pass': '通过', 'fail': '**不能成**', 'nocode': '无代码段',
              'uncompiled': '未编译'}[r['status']]
        L.append('| %02d | %s | %s | `%s` | %s | %s |'
                 % (r['id'], r['name'][:30], (r['board'] or '')[:4],
                    r['entry'] or '（无零参入口）', st, '；'.join(miss) or '—'))
    L += ['', '## 不能成的卡，缺什么（原文）', '']
    any_fail = False
    for r in results:
        if r['status'] != 'fail':
            continue
        any_fail = True
        L.append('### %02d %s' % (r['id'], r['name']))
        L.append('')
        if r['missing_headers']:
            L.append('- **缺头文件**：%s' % '、'.join('`%s`' % h for h in r['missing_headers']))
        for sym in sorted(set(r.get('missing_funcs', []) + r.get('undefined', []))):
            n = (r.get('defined_in_cards') or {}).get(sym)
            tail = '（在卡 %s 的示例里定义过，这张卡没带上）' % '、'.join('%02d' % x for x in n) if n \
                else '（SPL 与工程头里都没有这个标识符）'
            L.append('- **缺函数/符号**：`%s` %s' % (sym, tail))
        for e in r['errors']:
            L.append('- 编译错误原文：`%s`' % e)
        for nt in r.get('notes', []):
            L.append('- 说明：%s' % nt)
        L.append('')
    if not any_fail:
        L += ['（没有不能成的卡）', '']
    warn = [r for r in results
            if r['status'] == 'pass' and (r.get('missing_funcs') or r.get('undefined'))]
    if warn:
        L += ['## 编译通过、但链接会缺的（提示，不算不能成）', '']
        for r in warn:
            syms = sorted(set(r['missing_funcs'] + r['undefined']))
            L.append('- %02d %s：%s' % (r['id'], r['name'][:24], '、'.join('`%s`' % s for s in syms)))
        L.append('')
    return '\n'.join(L)


# ========================================================================
# 自检
# ========================================================================

GOOD_CARD = {'n': '假卡·能成', '_t': 'flow',
             'e': '/* 假卡 */\n#include "stm32f4xx.h"\n'
                  'static void fake_boot(void)\n{\n'
                  '    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);\n'
                  '}\n'}
# 反例一：调一个工程里根本没有的函数（armcc 只给 #223-D 隐式声明告警，编译"过"）
NEG_FUNC_CARD = {'n': '假卡·缺函数', '_t': 'flow',
                 'e': 'void fake_init(void)\n{\n    this_func_does_not_exist_zzz(1);\n}\n'}
# 反例二：用一个 SPL/工程里都没有的标识符（编译必红）
NEG_SYM_CARD = {'n': '假卡·缺符号', '_t': 'flow',
                'e': 'void fake_init(void)\n{\n    uint32_t x = THIS_SYMBOL_IS_NOWHERE_ZZZ;\n    (void)x;\n}\n'}
# 反例三：include 一个全仓库都没有的头
NEG_HDR_CARD = {'n': '假卡·缺头', '_t': 'flow',
                'e': '#include "no_such_header_zzz.h"\nvoid fake_init(void)\n{\n}\n'}
MAIN_CARD = {'n': '假卡·自带main', '_t': 'flow',
             'e': 'int main(void)\n{\n    return 0;\n}\n'}
COMMENT_ONLY = {'n': '假卡·纯讲解', '_t': 'flow', 'e': '/* 只有讲解：\n * 先开时钟\n * 再配引脚\n */\n'}


def selftest():
    ok_all = True
    skipped = False
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    cards = load_cards(ROOT)
    ck('读到手册里的 flow 卡（≥30 张）', len(cards) >= 30, '实际 %d 张' % len(cards))
    boards = load_boards(ROOT)
    ck('读到板级配置（≥2 块）', len(boards) >= 2, str([b['name'] for b in boards]))

    # 生成：三段结构（头文件 / 时钟 / 调用）都在
    text, meta = build_demo(GOOD_CARD, [])
    ck('生成物含 SPL 头 stm32f4xx.h', '#include "stm32f4xx.h"' in text)
    ck('生成物含时钟/滴答两条调用',
       'SYS_CLK_ToHighSpeed()' in text and 'SYS_TICK_Init()' in text)
    ck('生成物有 main 并调了一次卡的入口', 'int main(void)' in text and 'fake_boot();' in text)
    ck('挑入口：*_boot 优先于其它函数', meta['entry'] == 'fake_boot', meta['entry'])
    ck('正文里的 #include 被挪到文件头（正文不再重复）',
       text.count('#include "stm32f4xx.h"') == 1, text.count('#include "stm32f4xx.h"'))

    # 卡自带 main：改名 card_main，绝不与生成的 main 撞名
    t2, m2 = build_demo(MAIN_CARD, [])
    ck('卡自带 main → 改名 card_main', m2['had_main'] and 'card_main(' in t2 and 'card_main();' in t2)
    ck('生成物里只有一个 main 定义', len(re.findall(r'(?<![A-Za-z0-9_])main\s*\(', t2)) == 1)

    # 纯讲解卡：不生成，且明说
    ck('纯讲解卡判为无代码段', not has_code(COMMENT_ONLY) and has_code(GOOD_CARD))

    # 分类器：正例 + 三条错例（离线，不碰 armcc）
    good_log = '"D:\\x\\_demo.c", line 5: Warning:  #1-D: last line of file ends without a newline\n0 errors'
    c = classify(good_log)
    ck('正例：没有 error/缺件时四类皆空',
       not (c['missing_headers'] or c['missing_funcs'] or c['undefined'] or c['errors']), str(c))
    c1 = classify('"a.c", line 1: Error:  #5: cannot open source input file "no_such_header_zzz.h"')
    ck('错例：缺头文件点名 no_such_header_zzz.h',
       c1['missing_headers'] == ['no_such_header_zzz.h'], str(c1['missing_headers']))
    c2 = classify('"a.c", line 9: Warning:  #223-D: function "foo_bar_zzz" declared implicitly')
    ck('错例：隐式声明点名 foo_bar_zzz', c2['missing_funcs'] == ['foo_bar_zzz'], str(c2['missing_funcs']))
    c3 = classify('"a.c", line 9: Error:  #20: identifier "XYZ_ZZZ" is undefined')
    ck('错例：未定义标识符点名 XYZ_ZZZ', c3['undefined'] == ['XYZ_ZZZ'], str(c3['undefined']))
    ck('错例：正文错误（首条）被抽出来',
       bool(classify('"a.c", line 3: Error:  #8: missing closing quote')['errors']))

    # 跨卡提示：卡里定义过的名字要能认出来
    dm = card_defines(cards)
    ck('跨卡索引能认出卡里定义过的函数（如 uart_send_buf）', 'uart_send_buf' in dm, str(dm.get('uart_send_buf')))

    # 真编译：有 Keil 才测（没有就明说跳过，退出码 3）
    kr = (gs.keil_roots() or [''])[0]
    if not kr or len(boards) < 2:
        skipped = True
        print('  [SKIP] armcc 编译校验：本机没找到 Keil（这几条断言未测，不计入通过数）')
    else:
        tmp = tempfile.mkdtemp(prefix='d1_')
        try:
            r1 = analyze_card(GOOD_CARD, boards, ROOT, kr, tmp)
            ck('真编译：正例卡编译通过', r1['status'] == 'pass', r1['errors'])
            r2 = analyze_card(NEG_SYM_CARD, boards, ROOT, kr, tmp)
            ck('真编译：错例（未定义标识符）编译必红', r2['status'] == 'fail', str(r2))
            ck('真编译：错例点名到了那个标识符',
               'THIS_SYMBOL_IS_NOWHERE_ZZZ' in r2['undefined'], str(r2['undefined']))
            r3 = analyze_card(NEG_FUNC_CARD, boards, ROOT, kr, tmp)
            ck('真编译：缺函数（隐式声明）在编译通过时也要报出来',
               'this_func_does_not_exist_zzz' in r3['missing_funcs'], str(r3['missing_funcs']))
            r4 = analyze_card(NEG_HDR_CARD, boards, ROOT, kr, tmp)
            ck('真编译：缺头文件点名 no_such_header_zzz.h',
               r4['missing_headers'] == ['no_such_header_zzz.h'], str(r4['missing_headers']))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('流程卡 demo 自检：%d 通过 / %d 失败' % (sum(1 for _, c, _ in checks if c),
                                                  sum(1 for _, c, _ in checks if not c)))
    if not ok_all:
        return 1
    return 3 if skipped else 0


# ========================================================================
# 入口
# ========================================================================

def run(only=None, out_dir=None, do_compile=True, write=None, as_json=False):
    cards = load_cards(ROOT)
    boards = load_boards(ROOT)
    if not boards:
        print('读不到板级配置（%s）—— 没法选主用板，先检查那份 JSON' % CFG_REL)
        raise SystemExit(1)
    kr = (gs.keil_roots() or [''])[0]
    out_dir = out_dir or os.path.join(HERE, DEFAULT_OUT)
    picked = [(i, c) for i, c in enumerate(cards) if (not only or (i + 1) in only)]
    defmap = card_defines(cards)

    tmp = tempfile.mkdtemp(prefix='d1_run_')
    results = []
    try:
        for i, c in picked:
            r = analyze_card(c, boards, ROOT, kr, tmp, do_compile=do_compile)
            r['id'] = i + 1
            annotate_other_cards(r, defmap)
            results.append(r)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    for (i, c), r in zip(picked, results):
        write_demo(out_dir, i, c, r)
        if not as_json:
            mark = {'pass': '通过', 'fail': '不能成', 'nocode': '无代码段',
                    'uncompiled': '未编译'}[r['status']]
            miss = r['missing_headers'] + r['missing_funcs'] + r['undefined']
            print('  [%-4s] %02d %s%s' % (mark, r['id'], r['name'][:34],
                                          ('　缺：' + '、'.join(miss)) if miss else ''))

    md = render_report(results, out_dir)
    mdpath = write or os.path.join(HERE, REPORT_REL)
    os.makedirs(os.path.dirname(os.path.abspath(mdpath)), exist_ok=True)
    with open(mdpath, 'w', encoding='utf-8', newline='\n') as f:
        f.write(md)

    payload = with_envelope('流程卡demo',
                            {'total': len(results),
                             'pass': sum(1 for r in results if r['status'] == 'pass'),
                             'fail': sum(1 for r in results if r['status'] == 'fail'),
                             'nocode': sum(1 for r in results if r['status'] == 'nocode'),
                             'uncompiled': sum(1 for r in results if r['status'] == 'uncompiled'),
                             'out': out_dir, 'report': mdpath,
                             'cards': results},
                            ok=not any(r['status'] == 'fail' for r in results))
    if as_json:
        print(json.dumps(payload, ensure_ascii=False))
    else:
        print('流程卡 demo：%d 张，能成 %d / 不能成 %d / 无代码段 %d%s'
              % (payload['total'], payload['pass'], payload['fail'], payload['nocode'],
                 (' / 未编译 %d' % payload['uncompiled']) if payload['uncompiled'] else ''))
        print('报告：%s' % mdpath)
        print('demo 目录：%s' % out_dir)
        if payload['fail'] and not kr:
            print('（本机没找到 Keil，未做编译校验 —— 装上 Keil 或设 KEIL_ROOT 再跑一次）')
    return payload


def main():
    ap = argparse.ArgumentParser(description='流程卡 → 可编译最小 demo 工程（D1）')
    ap.add_argument('--card', default='', help='只生成某几张卡（序号，逗号分隔，从 1 起）')
    ap.add_argument('--out', default='', help='demo 输出目录（默认 产物/流程卡demo）')
    ap.add_argument('--write', default='', help='报告 md 路径（默认 产物/流程卡demo报告.md）')
    ap.add_argument('--no-compile', action='store_true', help='只生成，不跑 armcc')
    ap.add_argument('--strict', action='store_true',
                    help='有任何一张卡不能成 → 退出码 1（默认不管有几张不能成都算"跑完了"）')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(selftest())

    only = [int(x) for x in re.split(r'[,\s]+', a.card) if x.strip().isdigit()] if a.card else None
    p = run(only=only, out_dir=a.out or None, do_compile=not a.no_compile,
            write=a.write or None, as_json=a.json)
    if a.json:
        sys.exit(0)
    if p['uncompiled'] and p['uncompiled'] == p['total']:
        sys.exit(3)                     # 一张都没真编译（没 Keil / --no-compile）→ 跳过
    sys.exit(1 if (a.strict and p['fail']) else 0)


if __name__ == '__main__':
    main()
