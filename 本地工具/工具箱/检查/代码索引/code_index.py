# -*- coding: utf-8 -*-
r"""代码索引（C1）、调用签名校验（C2）、数据流追踪（C3）。

三件事的共同前提：**只看你自己写的文件**（工程目录下的 *.c），不碰 FWLIB / RTE / FreeRTOS。
所以先把注释剥掉 —— 骨架注释里写满了 `SYS_FRAME_BuildEnv()` 这类提示语，
不剥的话会把"注释里提到的 API"当成"你调用了"（阶段判定那边已经踩过一次）。

C1  API 使用索引（双向）
    · 正向：你在哪个文件第几行调了哪个库函数；这个函数手册里有没有条目
    · 反向：`--where SYS_USART_Init` → 你所有调用它的位置
    · 覆盖统计会明说"用了 N 个，手册只有 M 个有条目" —— 差的那部分只有头文件原型，
      查手册是查不到的（这不是 bug，是手册的边界，知道比不知道好）

C2  调用签名校验
    只报**确定错**，拿不准的一律不报：
      · 实参个数 ≠ 原型参数个数（变参原型跳过）
      · 形参是指针，实参却是个裸数字（如 `SYS_ADC_Read(1)`）
    拿不准的例子（故意不报）：把 `1` 传给 `SYS_USART_Id` 枚举形参 ——
    没有类型系统就判不了它是不是错，硬报就是误报。

C3  数据流追踪
    从"读传感器的调用"追到"组帧的实参"，看六个量有没有真的进帧；
    报了却没进帧的标「读了没用」，单位对不上的标「单位可疑」。
    追不动的地方（跨函数、走结构体）**明说追不动**，不瞎猜。

D2  条目名 → 使用位置（手册页内嵌代码索引）
    扫板1 + 板2，产出 `速查数据/data/code_usage.json`：
    每个**手册条目名**对应它在哪块板的哪个文件第几行出现。速查页/手册页读它，
    在条目里显示「你在 板1/main.c:123 用了它」（可点跳到那行）；
    没用过的条目如实写「没在你的代码里出现」。

    基准是**条目名**，不是 C1 那套 API 前缀：C1 的索引只认 `SYS_/GPIO_/LED_…` 前缀
    （"工程自己写的辅助函数"），而手册条目绝大多数是 **SPL 函数**（`RCC_…` / `USART_Init` /
    `GPIO_Init` …）—— 拿前缀去比，手册条目一个都命中不了（D2 第一版就是这么错的，实测
    索引与条目名**零交集**，所有卡片都只能写"没出现"）。所以这里直接拿**数据集里所有条目名**
    当词典去扫。

    分两组 tag，免得把"你写的"和"库里的"混为一谈：
      · `板1` / `板2`      = 你自己写的顶层 `.c`（工具生成的 `board_selftest.c` 不算）
      · `板1·库` / `板2·库` = 工程里的驱动源码 `FWLIB/src/*.c`（不是你写的，但确实在用）
    `p` 字段按"后面跟不跟 `(`"分「调用 / 出现」—— `ADC_InitTypeDef` 这种只是**类型出现**，
    说成"调用"就是瞎写。

用法：
    python 本地工具\工具箱\检查\代码索引\code_index.py --index
    python ...\code_index.py --where SYS_USART_Init
    python ...\code_index.py --signatures
    python ...\code_index.py --flow --svg 产物/数据流.svg
    python ...\code_index.py --usage                 # D2：条目名 → 使用位置（板1+板2）
    python ...\code_index.py --selftest
"""
import argparse
import glob
import json
import os
import re
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
ROOT = find_root(HERE) or os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
CFG_REL = os.path.join('本地工具', '工具箱', '速查数据', 'rules', 'board_envmonitor.json')

def cfg_file():
    """板级配置路径：TOOLBOX_BOARDCFG 优先（D3 的"加假工程"验收靠它），否则仓库里那份。

    B2 修：原来写成 `… or cfg_file()` —— 环境变量为空时**自己调自己**，直接 RecursionError
    （`--json` 一跑就崩；因为自检没测 --json，这个坑一直没被发现）。
    """
    env = os.environ.get('TOOLBOX_BOARDCFG', '').strip()
    if env:
        return env
    return os.path.join(ROOT, CFG_REL)

DS_REL = os.path.join('本地工具', '工具箱', '速查数据', 'data', 'dataset.json')
# D2：条目名 → 使用位置（板1 + 板2 的真实代码索引），由 `--usage` 生成；
# 速查页/手册页读它做「你在 板1/main.c:123 用了它」（AppPaths::usagePath()）。
USAGE_REL = os.path.join('本地工具', '工具箱', '速查数据', 'data', 'code_usage.json')

# D2：扫"你写的 .c"时，工具自己生成的这几个不算（与 C3 trace_flow 的 GENERATED 同一口径）
USAGE_GENERATED = {'board_selftest.c'}
# D2：工程里"驱动源码"那一组的子目录（不是你写的，但确实在用这些 SPL 函数）
USAGE_LIB_SUBDIR = os.path.join('FWLIB', 'src')
# D2：剥字符串字面量（`"GPIO_Init"` 这种出现在字符串里的名字不该算"用了它"）
STR_RE = re.compile(r'"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\'')
# D2：标识符（整词）
IDENT_RE = re.compile(r'(?<![A-Za-z0-9_])[A-Za-z_]\w+(?![A-Za-z0-9_])')


def with_envelope(name, obj, ok=True):
    """B2 统一信封：每支脚本的 `--json` 都带 **tool / schema / ok** 三个键，后面才是各自的字段。
    契约校验器（检查/契约校验/json_contract.py）按这份信封 + 契约表逐字段比对。"""
    d = dict(obj)
    d['tool'], d['schema'], d['ok'] = name, 1, bool(ok)
    return d

# 认为是"库函数"的前缀（工程自己写的辅助函数不列进来，否则索引会变成函数清单）
API_PREFIX = ('SYS_', 'LORA_', 'LED_', 'KEY_', 'BEEP_', 'ESP8266_', 'MQTT_', 'LCD_',
              'W25QXX_', 'XPT2046_', 'AT24C02_', 'GPIO_', 'I2C_', 'SYS_')
API_RE = re.compile(r'(?<![A-Za-z0-9_])((?:%s)[A-Za-z0-9_]+)\s*\(' %
                    '|'.join(p.rstrip('_') + '_' for p in sorted(set(API_PREFIX))))

# 传感器读取类（C3 的起点）
SENSOR_RE = re.compile(r'(?<![A-Za-z0-9_])((?:SYS_(?:DHT11|BMP280|BH1750|CCS811|ADC)_[A-Za-z0-9_]+'
                       r'|LORA_E22_Recv|W25QXX_LogRead))\s*\(')

# 六个量在帧里的顺序（与 sys_frame.h 的 ENV_*_OFF 一致）
ENV_SLOTS = [('temp', 100, '℃×100'), ('humi', 100, '%RH×100'), ('press', 10, 'hPa×10'),
             ('light', 1, 'lx'), ('tvoc', 1, 'ppb'), ('mq135', 1, 'ADC')]


# ========================================================================
# 基础
# ========================================================================

def read_text(path):
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ('utf-8', 'gbk'):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace')


def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    return s


def line_of(text, pos):
    return text.count('\n', 0, pos) + 1


def split_args(s):
    """按顶层逗号切实参（括号/花括号/字符串里的逗号不算）。"""
    out, depth, cur, i, n = [], 0, [], 0, len(s)
    quote = ''
    while i < n:
        ch = s[i]
        if quote:
            cur.append(ch)
            if ch == '\\' and i + 1 < n:
                cur.append(s[i + 1])
                i += 2
                continue
            if ch == quote:
                quote = ''
        elif ch in '"\'':
            quote = ch
            cur.append(ch)
        elif ch in '([{':
            depth += 1
            cur.append(ch)
        elif ch in ')]}':
            depth -= 1
            cur.append(ch)
        elif ch == ',' and depth == 0:
            out.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(ch)
        i += 1
    if ''.join(cur).strip():
        out.append(''.join(cur).strip())
    return out


def call_args(text, start):
    """从函数名后的 '(' 起，取出配对的实参文本。start 指向 '('。"""
    depth, i, n = 0, start, len(text)
    while i < n:
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                return text[start + 1:i]
        i += 1
    return ''


def user_files(project_dir):
    """你写的文件 = 工程根目录下的 *.c（FWLIB / RTE / FreeRTOS 都在子目录里，天然排除）。"""
    return sorted(glob.glob(os.path.join(project_dir, '*.c')))


def load_boards():
    with open(cfg_file(), encoding='utf-8') as f:
        return json.load(f).get('boards') or []


def pick_board(boards, want):
    if not want or want.lower() == 'all':
        return boards
    return [b for b in boards if want in b['name']]


def manual_names():
    """手册里**有同名条目**的函数名（C1 用它判"这个 API 有没有自己的卡"）。"""
    try:
        with open(os.path.join(ROOT, DS_REL), encoding='utf-8') as f:
            d = json.load(f)
    except Exception:
        return set()
    return {c.get('n', '') for c in d.get('all', []) if c.get('n')}


def manual_mentions():
    """函数名在**哪些卡的正文里被提到**（≠ 有自己的条目）。

    为什么要区分：`SYS_USART_Init` 没有自己的条目，但它出现在「两板外设驱动一览表」
    等 5 张卡的正文里 —— 搜是搜得到的，只是没有完整签名与调用示例。
    只按"同名条目"统计会把这件事说成"手册完全没有"，那是误导。
    """
    try:
        with open(os.path.join(ROOT, DS_REL), encoding='utf-8') as f:
            d = json.load(f)
    except Exception:
        return {}
    blobs = []
    for c in d.get('all', []):
        blobs.append((c.get('n', ''), json.dumps(c, ensure_ascii=False)))
    return blobs


def count_mentions(blobs, name):
    """名字出现在多少张卡的正文里（不含它自己那张，按整词匹配）。"""
    pat = re.compile(r'(?<![A-Za-z0-9_])' + re.escape(name) + r'(?![A-Za-z0-9_])')
    n = 0
    for card_name, blob in blobs:
        if card_name == name:
            continue
        if pat.search(blob):
            n += 1
    return n


# ========================================================================
# C1：API 使用索引
# ========================================================================

def build_index(board, manuals, blobs=None):
    proj = os.path.join(ROOT, board['dir'])
    rows = []
    for path in user_files(proj):
        raw = read_text(path)
        text = strip_comments(raw)
        for m in API_RE.finditer(text):
            name = m.group(1)
            par = text.find('(', m.end() - 1)
            args = split_args(call_args(text, par)) if par >= 0 else []
            rows.append({'api': name, 'file': os.path.basename(path),
                         'line': line_of(text, m.start()), 'argc': len(args),
                         'doc': name in manuals})
    # 汇总
    apis = {}
    for r in rows:
        apis.setdefault(r['api'], {'api': r['api'], 'doc': r['doc'], 'uses': []})
        apis[r['api']]['uses'].append('%s:%d' % (r['file'], r['line']))
    if blobs:
        for a in apis.values():
            a['mentions'] = 0 if a['doc'] else count_mentions(blobs, a['api'])
    return {'board': board['name'], 'calls': rows,
            'apis': sorted(apis.values(), key=lambda x: (-len(x['uses']), x['api'])),
            'files': [os.path.basename(p) for p in user_files(proj)]}


def render_index(idx):
    apis = idx['apis']
    documented = [a for a in apis if a['doc']]
    undoc = [a for a in apis if not a['doc']]
    mentioned = [a for a in undoc if a.get('mentions')]
    L = ['# API 使用索引 · %s' % idx['board'], '',
         '扫的文件：%s' % ('、'.join(idx['files']) or '（没有 .c 文件）'), '',
         '- 调用总数：**%d** 处，涉及 **%d** 个库函数' % (len(idx['calls']), len(apis)),
         '- 有自己条目：**%d** 个' % len(documented),
         '- 没有条目但正文提到过：**%d** 个（搜得到，但没有完整签名/示例）' % len(mentioned),
         '- 完全没提到：**%d** 个（只能翻头文件）' % (len(undoc) - len(mentioned)), '']
    if documented:
        L += ['## 有自己条目的', '', '| 函数 | 用了几次 | 在哪 |', '|---|---|---|']
        for a in documented:
            L.append('| `%s` | %d | %s |' % (a['api'], len(a['uses']), '、'.join(a['uses'][:6])))
        L.append('')
    if undoc:
        L += ['## 没有自己的条目', '',
              '| 函数 | 用了几次 | 在哪 | 正文提及 |', '|---|---|---|---|']
        for a in undoc:
            m = a.get('mentions')
            L.append('| `%s` | %d | %s | %s |'
                     % (a['api'], len(a['uses']), '、'.join(a['uses'][:6]),
                        ('%d 张卡' % m) if m else '没提到'))
        L.append('')
        L += ['> 「正文提及」= 这个函数名出现在多少张**别的**卡的正文里：搜是搜得到的，',
              '> 但没有它自己的条目，也就**没有完整签名和调用示例**。',
              '> 查签名请去 `FWLIB/inc/` 对应头文件；想让它也能当条目查，得在速查数据里补卡',
              '> （内容侧的事，工具不代写 —— 硬编一份"印象签名"只会更糟）。', '']
    return '\n'.join(L)


# ========================================================================
# D2：条目名 → 使用位置（手册页内嵌代码索引）
# ========================================================================

def usage_out_path():
    """`--usage` 的默认输出：数据根（速查数据）/data/code_usage.json。"""
    return os.path.join(ROOT, USAGE_REL)


def board_tag(board):
    """板级配置里的板 → 手册里用的短 tag（`板1` / `板2`；都不是就原样返回）。"""
    n = board.get('name', '')
    if '板1' in n:
        return '板1'
    if '板2' in n:
        return '板2'
    return n


def usage_boards(boards=None):
    """要扫的板：name 含「板1」/「板2」的两块（**跳过 OTA**），返回 [(board, tag)]。"""
    boards = load_boards() if boards is None else boards
    return [(b, board_tag(b)) for b in boards
            if '板1' in b.get('name', '') or '板2' in b.get('name', '')]


def scan_names(path, names):
    """扫一个 .c：返回 [(名字, 行号, 是调用还是仅出现)]，只留**手册条目名**。

    先剥注释再剥字符串：`/* SYS_USART_Init(...) */` 与 `"GPIO_Init"` 都不该算"用了它"。
    `p` 分「调用 / 出现」：名字后面跟 `(` 才算调用 —— `ADC_InitTypeDef di;` 只是类型出现，
    写成"调用"就是瞎说。
    """
    text = STR_RE.sub('', strip_comments(read_text(path)))
    out, seen = [], set()
    for m in IDENT_RE.finditer(text):
        nm = m.group(0)
        if nm not in names:
            continue
        ln = line_of(text, m.start())
        if (nm, ln) in seen:
            continue
        seen.add((nm, ln))
        call = bool(re.compile(r'\s*\(').match(text, m.end()))
        out.append((nm, ln, '调用' if call else '出现'))
    return out


def usage_sources(board):
    """一块板的两个来源组：[(tag, [文件...])]。

    板1/板2 = 你写的顶层 .c（工具生成的 board_selftest.c 不算）；
    板1·库/板2·库 = 工程里的驱动源码 FWLIB/src/*.c（不是你写的，但确实在用它）。
    """
    tag = board_tag(board)
    proj = os.path.join(ROOT, board['dir'])
    mine = [p for p in user_files(proj)
            if os.path.basename(p) not in USAGE_GENERATED]
    lib = sorted(glob.glob(os.path.join(proj, USAGE_LIB_SUBDIR, '*.c')))
    return [(tag, mine), (tag + '·库', lib)]


def usage_payload(boards=None, manuals=None):
    """扫板1 + 板2，产出"手册条目名 → 使用位置"。

    规范形状（写进 code_usage.json 的就是前四个键）：
        {"generatedAt","boards","usage":{name:[{tag,f,l,p}]},"counts":{apis,calls}}
    另外带 board / calls 两个键 —— `--usage --json` 要用它们满足 B2 契约表
    （契约表给本脚本登记的就是 board / calls，缺了契约闸门会红）。

    词典取 `manuals`（默认 = 数据集里所有条目名）。**必须按条目名扫**：见文件头 D2 那段
    说明 —— 用 C1 的 API 前缀去比，手册条目命中数为 0（SPL 函数不在那套前缀里）。
    """
    names = manual_names() if manuals is None else set(manuals)
    usage, calls, blist = {}, [], []
    for b, _tag in usage_boards(boards):
        blist.append(b.get('name', ''))
        for tag, files in usage_sources(b):
            for p in files:
                for nm, ln, what in scan_names(p, names):
                    calls.append({'api': nm, 'file': os.path.basename(p),
                                  'line': ln, 'tag': tag})
                    usage.setdefault(nm, []).append(
                        {'tag': tag, 'f': os.path.basename(p), 'l': ln, 'p': what})
    return {
        'generatedAt': time.strftime('%Y-%m-%dT%H:%M:%S'),
        'boards': blist,
        'usage': usage,
        'counts': {'apis': len(usage), 'calls': len(calls)},
        'board': '板1+板2',
        'calls': calls,
    }


def write_usage(payload, path):
    """写文件：只落规范里的四个键（board / calls 是给 --json 契约用的，不进文件）。"""
    obj = {k: payload[k] for k in ('generatedAt', 'boards', 'usage', 'counts')}
    d = os.path.dirname(os.path.abspath(path))
    if d:
        os.makedirs(d, exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(obj, f, ensure_ascii=False, separators=(',', ':'))
    return len(obj.get('usage') or {})


# ========================================================================
# C2：调用签名校验
# ========================================================================

def parse_prototypes(headers):
    """从 FWLIB/inc/*.h 抽原型：返回 {name: {'ret':..., 'params':[{type,name}], 'variadic':bool}}"""
    out = {}
    for h in headers:
        text = strip_comments(read_text(h))
        # 抓 "type name(args);"（允许跨行）
        for m in re.finditer(r'([A-Za-z_][\w \t\*]*?)\b([A-Za-z_]\w*)\s*\(([^;{}]*?)\)\s*;',
                             text, re.S):
            ret, name, params = m.group(1).strip(), m.group(2), m.group(3).strip()
            if not ret or ret.split()[0] in ('if', 'while', 'for', 'switch', 'return', 'sizeof'):
                continue
            if name in ('if', 'while', 'for', 'switch'):
                continue
            variadic = '...' in params
            plist = []
            if params and params != 'void' and not variadic:
                for p in split_args(params):
                    p = p.strip()
                    if not p:
                        continue
                    # 最后一个标识符当参数名（数组/函数指针简单处理）
                    mm = re.search(r'([A-Za-z_]\w*)\s*(\[[^\]]*\])?$', p)
                    pname = mm.group(1) if mm else ''
                    ptype = p[:mm.start(1)].strip() if mm else p
                    plist.append({'type': ptype, 'name': pname})
            elif variadic:
                plist = [{'type': p, 'name': ''} for p in split_args(params) if p.strip() and p.strip() != '...']
            out.setdefault(name, {'ret': ret, 'params': plist, 'variadic': variadic,
                                  'header': os.path.basename(h)})
    return out


def check_signatures(board, protos):
    proj = os.path.join(ROOT, board['dir'])
    issues = []
    checked = 0
    for path in user_files(proj):
        text = strip_comments(read_text(path))
        for m in API_RE.finditer(text):
            name = m.group(1)
            proto = protos.get(name)
            if not proto:
                continue
            par = text.find('(', m.end() - 1)
            args = split_args(call_args(text, par))
            checked += 1
            line = line_of(text, m.start())
            fn = os.path.basename(path)
            if proto['variadic']:
                continue
            if len(args) != len(proto['params']):
                issues.append({'file': fn, 'line': line, 'api': name, 'kind': 'argc',
                               'msg': '实参 %d 个，原型要 %d 个（%s）'
                                      % (len(args), len(proto['params']),
                                         ', '.join(p['type'] + (' ' + p['name'] if p['name'] else '')
                                                   for p in proto['params']))})
                continue
            for i, (a, p) in enumerate(zip(args, proto['params'])):
                if '*' in p['type'] and re.fullmatch(r'-?\d+[uUlL]*', a.strip()):
                    issues.append({'file': fn, 'line': line, 'api': name, 'kind': 'ptr',
                                   'msg': '第 %d 个实参 `%s` 是裸数字，形参 `%s %s` 要指针'
                                          % (i + 1, a, p['type'], p['name'])})
    return {'board': board['name'], 'checked': checked, 'issues': issues}


# ========================================================================
# C3：数据流追踪
# ========================================================================

# 生成物不算"你写的数据流"：board_selftest.c 里组帧用的是写死的常量，
# 把它当数据流会把真实的管线盖掉（第一次跑就踩了：6 个槽全追到测试文件）。
GENERATED = {'board_selftest.c'}


def trace_flow(board):
    proj = os.path.join(ROOT, board['dir'])
    reads = []          # {'api','vars','file','line'}
    frame_calls = []    # {'args','file','line'}
    for path in user_files(proj):
        fn = os.path.basename(path)
        if fn in GENERATED:
            continue
        text = strip_comments(read_text(path))
        for m in SENSOR_RE.finditer(text):
            line = line_of(text, m.start())
            # 变量可能来自两种写法：
            #   ① 返回值赋值：  t = SYS_ADC_Read(...)
            #   ② 指针出参：    SYS_DHT11_Read(&t, &h)   ← 工程里的传感器接口大多是这种
            head = text[max(0, m.start() - 120):m.start()]
            mm = re.search(r'([A-Za-z_]\w*)\s*=\s*$', head)
            par = text.find('(', m.end() - 1)
            argvars = [v for v in re.findall(r'&\s*([A-Za-z_]\w*)', call_args(text, par))]
            if not argvars:
                argvars = [v for v in re.findall(r'(?<![A-Za-z0-9_.])([A-Za-z_]\w*)(?![A-Za-z0-9_(])',
                                                 call_args(text, par))]
            reads.append({'api': m.group(1),
                          'vars': ([mm.group(1)] if mm else []) + argvars,
                          'file': fn, 'line': line})
        for m in re.finditer(r'(?<![A-Za-z0-9_])(SYS_FRAME_BuildEnv)\s*\(', text):
            par = text.find('(', m.end() - 1)
            frame_calls.append({'api': m.group(1), 'args': split_args(call_args(text, par)),
                                'file': fn, 'line': line_of(text, m.start())})

    slots = []
    if frame_calls:
        args = frame_calls[-1]['args']
        # BuildEnv(out, cap, temp, humi, press, light, tvoc, mq) → 后 6 个是数据
        data = args[2:] if len(args) >= 8 else []
        for i, (key, scale, unit) in enumerate(ENV_SLOTS):
            expr = data[i] if i < len(data) else ''
            src = ''
            for r in reads:
                for v in r['vars']:
                    if re.search(r'(?<![A-Za-z0-9_])' + re.escape(v) + r'(?![A-Za-z0-9_])', expr):
                        src = r['api']
                        break
                if src:
                    break
            scale_ok = True
            note = ''
            if src and scale > 1:
                if not re.search(r'\*\s*%d\b|x%d\b|_%d\b' % (scale, scale, scale), expr):
                    scale_ok = False
                    note = '没看到 ×%d（%s）' % (scale, unit)
            slots.append({'slot': key, 'unit': unit, 'expr': expr, 'from': src,
                          'ok': bool(expr), 'traced': bool(src),
                          'scale_ok': scale_ok, 'note': note})
    used = set()
    if frame_calls:
        used = set(re.findall(r'[A-Za-z_]\w*', ' '.join(frame_calls[-1]['args'])))
    unused = [r for r in reads if r['vars'] and not (set(r['vars']) & used)]
    return {'board': board['name'], 'reads': reads, 'frame': frame_calls[-1] if frame_calls else None,
            'slots': slots, 'unused': unused}


def render_flow_svg(flow):
    W, H = 1100, 470
    L = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" '
         'font-family="Segoe UI, Microsoft YaHei, sans-serif">' % (W, H, W, H),
         '<rect width="%d" height="%d" fill="#ffffff"/>' % (W, H),
         '<text x="24" y="34" font-size="18" font-weight="700" fill="#111827">数据流 · %s</text>'
         % _esc(flow['board'])]
    if not flow['frame']:
        L.append('<text x="24" y="80" font-size="13" fill="#b45309">'
                 '没找到 SYS_FRAME_BuildEnv 调用 —— 组帧那段还没写（阶段 6 未开始），追不到帧字段。</text>')
        L.append('<text x="24" y="106" font-size="12" fill="#6b7280">'
                 '已读到的传感器调用：%d 处（见下方列表）</text>' % len(flow['reads']))
        y = 140
        for r in flow['reads'][:10]:
            L.append('<text x="32" y="%d" font-size="12" fill="#374151">%s:%d  %s() → %s</text>'
                     % (y, _esc(r['file']), r['line'], _esc(r['api']),
                          _esc('、'.join(r['vars']) or '（没解析出变量）')))
            y += 20
        L.append('</svg>')
        return '\n'.join(L)

    # 三列：传感器 → 变量/换算 → 帧字段
    col1, col2, col3 = 40, 420, 800
    L.append('<text x="%d" y="70" font-size="12" fill="#6b7280">传感器调用</text>' % col1)
    L.append('<text x="%d" y="70" font-size="12" fill="#6b7280">中间表达式</text>' % col2)
    L.append('<text x="%d" y="70" font-size="12" fill="#6b7280">帧字段（%s:%d）</text>'
             % (col3, _esc(flow['frame']['file']), flow['frame']['line']))
    y = 100
    for i, s in enumerate(flow['slots']):
        if not s['ok']:
            col = '#dc2626'                      # 槽是空的
        elif s['traced'] and s['scale_ok']:
            col = '#16a34a'                      # 追到了来源、单位也对
        else:
            col = '#f59e0b'                      # 常量或单位可疑
        L.append('<rect x="%d" y="%d" width="300" height="42" rx="6" fill="#f9fafb" stroke="#e5e7eb"/>' % (col1, y - 18))
        L.append('<text x="%d" y="%d" font-size="12" fill="#374151">%s</text>'
                 % (col1 + 10, y - 1, _esc(s['from'] or '（没追到来源）')))
        L.append('<rect x="%d" y="%d" width="320" height="42" rx="6" fill="#f9fafb" stroke="#e5e7eb"/>' % (col2, y - 18))
        L.append('<text x="%d" y="%d" font-size="11" fill="#374151">%s</text>'
                 % (col2 + 10, y - 1, _esc(s['expr'][:46] or '（空）')))
        L.append('<rect x="%d" y="%d" width="250" height="42" rx="6" fill="%s" opacity="0.12"/>' % (col3, y - 18, col))
        L.append('<rect x="%d" y="%d" width="250" height="42" rx="6" fill="none" stroke="%s"/>' % (col3, y - 18, col))
        L.append('<text x="%d" y="%d" font-size="12" fill="%s" font-weight="600">%s（%s）</text>'
                 % (col3 + 10, y - 1, col, _esc(s['slot']), _esc(s['unit'])))
        if s['note']:
            L.append('<text x="%d" y="%d" font-size="10" fill="#b45309">%s</text>'
                     % (col3 + 10, y + 13, _esc(s['note'])))
        L.append('<path d="M %d %d L %d %d" stroke="%s" stroke-width="1.4" fill="none"/>'
                 % (col1 + 300, y, col2, y, col))
        L.append('<path d="M %d %d L %d %d" stroke="%s" stroke-width="1.4" fill="none"/>'
                 % (col2 + 320, y, col3, y, col))
        y += 58
    L.append('</svg>')
    return '\n'.join(L)


def _esc(s):
    return str(s).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


# ========================================================================
# 自检
# ========================================================================

FIXTURE_H = r'''
#ifndef FOO_H
#define FOO_H
/* 注释里的原型不该被当真：void NotAReal(int x); */
uint8_t SYS_USART_Init(int id, uint32_t baud);
uint8_t SYS_ADC_Read(uint16_t *out, uint8_t ch);
void    SYS_Log(const char *fmt, ...);
uint8_t SYS_DHT11_Read(float *t, float *h);
#endif
'''

FIXTURE_C = r'''
#include "foo.h"
int main(void)
{
    /* 注释里写着 SYS_FRAME_BuildEnv(buf, 32, ...) —— 不该被索引 */
    SYS_USART_Init(1, 115200);          /* 对：2 个实参 */
    SYS_ADC_Read(&v, 1);                /* 对 */
    SYS_DHT11_Read(&t, &h);             /* 对 */
    SYS_Log("x=%d", 1);                 /* 变参：跳过 */
    return 0;
}
'''

FIXTURE_BAD = r'''
#include "foo.h"
int main(void)
{
    SYS_USART_Init(1);                  /* 错：少一个实参 */
    SYS_ADC_Read(1, 2);                 /* 错：第一个实参是裸数字，形参要指针 */
    return 0;
}
'''


def selftest():
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    import tempfile
    tmp = tempfile.mkdtemp(prefix='ci_')
    try:
        # B2 修：板级配置路径原来在"环境变量为空"时**自己调自己**（RecursionError），
        # 导致 `--json` 一跑就崩。这里钉住"能解析出真实路径、且不递归"。
        cf = cfg_file()
        ck('板级配置路径能解析且文件存在（不再自己调自己 → RecursionError）',
           bool(cf) and os.path.isfile(cf), cf)
        # 正例与错例分开放，免得互相污染（错例目录里只有坏文件）
        for d in ('good', 'bad'):
            os.makedirs(os.path.join(tmp, d, 'inc'))
            with open(os.path.join(tmp, d, 'inc', 'foo.h'), 'w',
                      encoding='utf-8', newline='\n') as f:
                f.write(FIXTURE_H)
        with open(os.path.join(tmp, 'good', 'main.c'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(FIXTURE_C)
        with open(os.path.join(tmp, 'bad', 'bad.c'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(FIXTURE_BAD)

        protos = parse_prototypes([os.path.join(tmp, 'good', 'inc', 'foo.h')])
        ck('解析原型：参数个数对', len(protos.get('SYS_USART_Init', {}).get('params', [])) == 2,
           protos.get('SYS_USART_Init'))
        ck('解析原型：变参标记', protos.get('SYS_Log', {}).get('variadic') is True)
        ck('解析原型：注释里的原型不算', 'NotAReal' not in protos)

        global ROOT
        old = ROOT
        ROOT = tmp
        good = {'name': '假板', 'dir': 'good'}
        bad = {'name': '假板', 'dir': 'bad'}

        # C1：注释里的调用不该被索引
        idx = build_index(good, {'SYS_USART_Init'})
        names = {r['api'] for r in idx['calls']}
        ck('C1：索引到真调用', {'SYS_USART_Init', 'SYS_ADC_Read', 'SYS_DHT11_Read'} <= names, names)
        ck('C1：注释里的 SYS_FRAME_BuildEnv 不被索引', 'SYS_FRAME_BuildEnv' not in names, names)
        ck('C1：手册覆盖判定（只有 SYS_USART_Init 有条目）',
           any(a['api'] == 'SYS_USART_Init' and a['doc'] for a in idx['apis']))
        ck('C1：报告里明说手册没条目', '没有自己的条目' in render_index(idx))

        # D2：--usage（板1 + 板2 → 条目名 → 使用位置）正例 + 反例
        #     自检一律用临时目录 / 临时 --out，绝不覆盖真实的 code_usage.json。
        #     词典必须显式传：tmp 里没有真 dataset.json，manual_names() 会返回空集
        #     （也正是"D2 第一版拿前缀去比、命中 0"的现场——所以这里钉住按条目名扫）。
        os.makedirs(os.path.join(tmp, 'note'))
        with open(os.path.join(tmp, 'note', 'note.c'), 'w',
                  encoding='utf-8', newline='\n') as f:
            # 只在注释里出现、正文没有任何真调用 —— 不该被收进使用位置
            f.write('/* 只在注释里出现：SYS_USART_Init(1, 115200); */\n'
                    'const char *s = "SYS_USART_Init";\nint nothing;\n')
        # 库那一组：good/FWLIB/src/lib.c 里出现了一个 SPL 条目名 → 应记成 板1·库
        os.makedirs(os.path.join(tmp, 'good', 'FWLIB', 'src'))
        with open(os.path.join(tmp, 'good', 'FWLIB', 'src', 'lib.c'), 'w',
                  encoding='utf-8', newline='\n') as f:
            f.write('void f(void) { GPIO_Init(0, 0); }\n')
        payload = usage_payload([{'name': '板1 检测端', 'dir': 'good'},
                                 {'name': '板2 通信端', 'dir': 'note'}],
                                manuals={'SYS_USART_Init', 'GPIO_Init'})
        exp_line = next((i + 1 for i, l in enumerate(FIXTURE_C.split('\n'))
                         if 'SYS_USART_Init' in l), -1)
        hits = payload['usage'].get('SYS_USART_Init', [])
        ck('D2：--usage 正例——按**条目名**收下 板1/main.c 与真实调用行号',
           any(h['tag'] == '板1' and h['f'] == 'main.c' and h['l'] == exp_line
               and h['p'] == '调用' for h in hits),
           hits)
        ck('D2：--usage 反例（注释里的同名 + 字符串里的同名都不收录）',
           not any(h['tag'] == '板2' for h in hits), hits)
        ck('D2：--usage 分两组 tag —— 驱动源码记 板1·库（不是你写的）',
           any(h['tag'] == '板1·库' and h['f'] == 'lib.c'
               for h in payload['usage'].get('GPIO_Init', [])),
           payload['usage'].get('GPIO_Init'))
        ck('D2：--usage 只留条目名（非条目名的标识符不收）',
           'nothing' not in payload['usage'] and 'f' not in payload['usage'],
           sorted(payload['usage'])[:6])
        ck('D2：--usage 信封保留 board / calls（B2 契约表登记的键）',
           payload['board'] == '板1+板2'
           and any(c['api'] == 'SYS_USART_Init' for c in payload['calls']),
           payload['board'])
        uout = os.path.join(tmp, 'code_usage.json')
        write_usage(payload, uout)
        with open(uout, encoding='utf-8') as f:
            saved = json.load(f)
        ck('D2：--usage 输出形状（generatedAt / boards / usage / counts）',
           bool(saved.get('generatedAt')) and bool(saved.get('boards'))
           and isinstance(saved.get('usage'), dict)
           and saved.get('counts', {}).get('calls', 0) >= 1,
           sorted(saved.keys()))

        # C2：正例 0 误报；错例两种都抓到
        g = check_signatures(good, protos)
        ck('C2：正例 0 误报', not g['issues'], g['issues'])
        b = check_signatures(bad, protos)
        kinds = sorted(i['kind'] for i in b['issues'])
        ck('C2：错例抓到「实参个数不符」+「指针传裸数字」', kinds == ['argc', 'ptr'], kinds)
        ck('C2：变参调用不误报', not any(i['api'] == 'SYS_Log' for i in g['issues']))

        # C3：没有 BuildEnv 时明说追不到
        flow = trace_flow(good)
        ck('C3：没组帧调用时 frame 为空', flow['frame'] is None)
        ck('C3：仍列出读到的传感器调用', len(flow['reads']) >= 1, len(flow['reads']))
        svg = render_flow_svg(flow)
        ck('C3：SVG 里明说"追不到帧字段"', '追不到帧字段' in svg and svg.rstrip().endswith('</svg>'))

        # C3：有组帧调用时能对上 6 个槽
        with open(os.path.join(tmp, 'good', 'frame.c'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('int main(void){ float t,h; uint8_t b[32];\n'
                    ' SYS_DHT11_Read(&t,&h);\n'
                    ' SYS_FRAME_BuildEnv(b,32,(int)(t*100),(int)(h*100),10132,320,15,812);\n'
                    ' return 0; }\n')
        flow2 = trace_flow(good)
        ck('C3：找到组帧调用', flow2['frame'] is not None)
        ck('C3：六个槽都解析出来', len(flow2['slots']) == 6, len(flow2['slots']))
        ck('C3：temp 槽追到 SYS_DHT11_Read', flow2['slots'][0]['from'] == 'SYS_DHT11_Read',
           flow2['slots'][0])
        ck('C3：light 槽没追到来源（常量 320）', flow2['slots'][3]['from'] == '',
           flow2['slots'][3])
        ck('C3：press 槽是常量 → 标记没追到来源', flow2['slots'][2]['from'] == '')
        ROOT = old
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('代码索引自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='代码索引 / 签名校验 / 数据流')
    ap.add_argument('--board', default='板1')
    ap.add_argument('--index', action='store_true', help='C1：API 使用索引')
    ap.add_argument('--where', default='', help='C1：反查某个 API 用在哪')
    ap.add_argument('--signatures', action='store_true', help='C2：调用签名校验')
    ap.add_argument('--flow', action='store_true', help='C3：数据流追踪')
    ap.add_argument('--svg', default='', help='C3：数据流图输出路径')
    ap.add_argument('--usage', action='store_true', help='D2：条目名 → 使用位置（板1 + 板2）')
    ap.add_argument('--out', default='', help='D2：--usage 的输出路径（默认 速查数据/data/code_usage.json）')
    ap.add_argument('--write', default='', help='把报告写到这个 md 文件')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    if a.usage:
        payload = usage_payload()
        outp = a.out.strip() or usage_out_path()
        write_usage(payload, outp)
        if a.json:
            print(json.dumps(with_envelope('代码索引', payload),
                             ensure_ascii=False))
        else:
            print('使用位置：%d 个条目、%d 处调用（板：%s）'
                  % (payload['counts']['apis'], payload['counts']['calls'],
                     '、'.join(payload['boards']) or '无'))
            print('写入：%s' % outp)
        # 只有 --usage 单跑就到此为止；带了 --index（插件壳那页）继续往下出报告，
        # 一次跑同时刷新报告与使用位置。
        if not a.index:
            sys.exit(0)

    boards = pick_board(load_boards(), a.board)
    if not boards:
        print('没有匹配的板：%s' % a.board)
        sys.exit(1)
    board = boards[0]
    manuals = manual_names()
    blobs = manual_mentions()

    if a.where:
        idx = build_index(board, manuals, blobs)
        hit = [r for r in idx['calls'] if r['api'] == a.where]
        if not hit:
            print('%s：没在 %s 里找到调用' % (a.where, board['name']))
            sys.exit(1)
        print('%s 在 %s 里的调用（%d 处）：' % (a.where, board['name'], len(hit)))
        for r in hit:
            print('  %s:%d（%d 个实参）' % (r['file'], r['line'], r['argc']))
        print('手册有条目：%s' % ('是' if hit[0]['doc'] else '否（只有头文件原型）'))
        return

    if a.signatures:
        protos = parse_prototypes(glob.glob(os.path.join(ROOT, board['dir'], 'FWLIB', 'inc', '*.h')))
        res = check_signatures(board, protos)
        if a.json:
            print(json.dumps(with_envelope('代码索引', res, ok=not res['issues']),
                             ensure_ascii=False))
        else:
            print('%s：查了 %d 处调用，发现 %d 个确定错' % (board['name'], res['checked'], len(res['issues'])))
            for i in res['issues']:
                print('  [%s] %s:%d  %s —— %s' % (i['kind'], i['file'], i['line'], i['api'], i['msg']))
            if not res['issues']:
                print('  （只报确定错：实参个数不符 / 指针形参传了裸数字。拿不准的不报。）')
        sys.exit(0 if not res['issues'] else 1)

    if a.flow:
        flow = trace_flow(board)
        svg = render_flow_svg(flow)
        if a.svg:
            os.makedirs(os.path.dirname(os.path.abspath(a.svg)), exist_ok=True)
            with open(a.svg, 'w', encoding='utf-8', newline='\n') as f:
                f.write(svg)
            print('数据流图：%s' % a.svg)
        if a.json:
            print(json.dumps(with_envelope('代码索引', flow), ensure_ascii=False))
        else:
            if not flow['frame']:
                print('%s：没找到 SYS_FRAME_BuildEnv 调用 —— 组帧那段还没写，追不到帧字段。' % board['name'])
                print('已读到的传感器调用 %d 处：' % len(flow['reads']))
                for r in flow['reads'][:12]:
                    print('  %s:%d  %s() → %s' % (r['file'], r['line'], r['api'],
                                                '、'.join(r['vars']) or '（没解析出变量）'))
            else:
                print('%s 数据流（组帧在 %s:%d）：' % (board['name'], flow['frame']['file'],
                                                     flow['frame']['line']))
                for s in flow['slots']:
                    if not s['ok']:
                        flag = '缺  '
                    elif s['traced'] and s['scale_ok']:
                        flag = '追踪到'
                    elif not s['traced']:
                        flag = '常量  '
                    else:
                        flag = '单位?'
                    print('  [%s] %-7s %-10s ← %-22s %s' % (flag, s['slot'], s['unit'],
                                                           s['from'] or '（常量/没追到）',
                                                           s['note']))
                if flow['unused']:
                    print('读了但没进帧：')
                    for r in flow['unused']:
                        print('  %s:%d  %s() → %s' % (r['file'], r['line'], r['api'], '、'.join(r['vars'])))
        return

    # 默认 --index
    idx = build_index(board, manuals, blobs)
    md = render_index(idx)
    if a.write:
        os.makedirs(os.path.dirname(os.path.abspath(a.write)), exist_ok=True)
        with open(a.write, 'w', encoding='utf-8', newline='\n') as f:
            f.write(md)
        print('报告：%s' % a.write)
    if a.json:
        print(json.dumps(with_envelope('代码索引', idx), ensure_ascii=False))
    else:
        apis = idx['apis']
        doc = [x for x in apis if x['doc']]
        men = [x for x in apis if not x['doc'] and x.get('mentions')]
        print('%s：%d 个文件，%d 处调用，%d 个库函数'
              '（有条目 %d / 仅正文提及 %d / 完全没提 %d）'
              % (board['name'], len(idx['files']), len(idx['calls']), len(apis),
                 len(doc), len(men), len(apis) - len(doc) - len(men)))
        for x in apis[:15]:
            print('  %s %-26s %d 处  %s' % ('✓' if x['doc'] else '·', x['api'],
                                            len(x['uses']), '、'.join(x['uses'][:3])))
        if len(apis) > 15:
            print('  …（还有 %d 个，用 --write 出完整报告）' % (len(apis) - 15))


if __name__ == '__main__':
    main()
