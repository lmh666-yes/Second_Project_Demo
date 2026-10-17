# -*- coding: utf-8 -*-
r"""引脚与资源规划器（幂等，只读源码，不改任何文件）。

与 `仓库巡检/pin_audit.py` 的分工：
  pin_audit 只做"体检"——列出全板的引脚占用与冲突；
  本工具做"规划"——按模块看引脚与中断资源，并在你选定一组模块后，
  直接报出这组模块之间的冲突、要占的脚、以及中断优先级是否越过内核上限。

输出：
  1) 每块板：模块与引脚表（模块基名 -> 占用的引脚）、冲突引脚、无人认领引脚、
     声明了但没人用（僵尸宏）的引脚；
  2) 每块板：中断资源表（IRQn / 抢占 / 子优先级 / 出处），标注抢占低于内核上限的项；
  3) 若给出 --use，附"方案检查"：选中模块之间的冲突与引脚清单。

报告写到脚本同目录的 引脚规划报告.md，并在控制台打印摘要。只读，不改任何文件。

用法：
    python 本地工具\工具箱\检查\引脚规划\pin_planner.py
    python 本地工具\工具箱\检查\引脚规划\pin_planner.py --use "LORA_E22,W25QXX,RS485"
    python 本地工具\工具箱\检查\引脚规划\pin_planner.py --board 2 --quiet
    python 本地工具\工具箱\检查\引脚规划\pin_planner.py --selftest
（--selftest 在临时目录造合成板核对判定，不写报告、不碰仓库。）
"""
import json
import os
import re
import sys

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass

def find_root(start):
    """向上找含 .git 或 EnvMonitor 的目录，作为仓库根。与工具放在哪一层无关。"""
    d = os.path.abspath(start)
    while True:
        if os.path.isdir(os.path.join(d, '.git')) or os.path.isdir(os.path.join(d, 'EnvMonitor')):
            return d
        p = os.path.dirname(d)
        if p == d:
            return None
        d = p


ROOT = find_root(os.path.dirname(os.path.abspath(__file__)))
if ROOT is None:
    # 兜底：从脚本位置向上逐级找含 EnvMonitor 的目录，不写死层数。
    _d = os.path.dirname(os.path.abspath(__file__))
    _hit = None
    for _ in range(6):
        if os.path.isdir(os.path.join(_d, 'EnvMonitor')):
            _hit = _d
            break
        _p = os.path.dirname(_d)
        if _p == _d:
            break
        _d = _p
    ROOT = _hit or _d

BOARDS = [
    ('板1 检测端 (F407ZE)', os.path.join('EnvMonitor', 'Project_1_检测端', 'Project')),
    ('板2 通信端 (F407ZG)', os.path.join('EnvMonitor', 'Project_2_数据通信端', 'Project')),
]

PORTS = ['A', 'B', 'C', 'D', 'E', 'F', 'G']
PINS_PER_PORT = 16
ALL_PINS = set((p, b) for p in PORTS for b in range(PINS_PER_PORT))

# 调试口（SWD/JTAG）：业务宏一旦占了这几脚，就等于自断下载/在线调试通道（B1 判严重）。
DEBUG_PINS = {'PA13', 'PA14', 'PA15', 'PB3', 'PB4'}

RE_PORT = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*?)_PORT\s+(GPIO[A-Z])\s*(?:/\*.*)?$')
RE_PIN = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*?)_PIN\s+((?:GPIO_Pin_\d+)(?:\s*\|\s*GPIO_Pin_\d+)*)\s*(?:/\*.*)?$')
RE_BIT = re.compile(r'GPIO_Pin_(\d+)')
RE_IRQ_SET = re.compile(r'SYS_NVIC_SetPriority\s*\(\s*([A-Za-z_]\w*)\s*,\s*([A-Za-z_]\w*|\d+)\s*,\s*([A-Za-z_]\w*|\d+)\s*\)')
RE_IRQ_CH = re.compile(r'NVIC_IRQChannel\s*=\s*([A-Za-z_]\w*)')
RE_IRQ_PRE = re.compile(r'NVIC_IRQChannelPreemptionPriority\s*=\s*([A-Za-z_]\w*|\d+)')
RE_IRQ_SUB = re.compile(r'NVIC_IRQChannelSubPriority\s*=\s*([A-Za-z_]\w*|\d+)')

# 模块分组用：这些短尾缀多为功能脚，分组时去掉，归到同一模块
FUNC_TAIL = {
    'TX', 'RX', 'M0', 'M1', 'AUX', 'CS', 'SCK', 'MISO', 'MOSI', 'DE', 'BL',
    'DATA', 'SCL', 'SDA', 'INT', 'IRQ', 'RST', 'RES', 'DC', 'CLK', 'DIN',
    'DOUT', 'SCL', 'SDA', 'PWM', 'EN', 'DIR', 'STEP',
}


def read_lines(path):
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ('utf-8', 'gbk', 'latin-1'):
        try:
            return raw.decode(enc).splitlines()
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace').splitlines()


def rel(path):
    """相对仓库根的展示路径；跨盘符（如临时目录在 C:、仓库在 D:）退回绝对路径，不抛异常。"""
    try:
        return os.path.relpath(path, ROOT)
    except ValueError:
        return path


def module_of(base):
    """把 `LORA_E22_TX` 这类基名归到一个模块名（去掉功能尾缀）。"""
    parts = base.split('_')
    if len(parts) >= 2 and parts[-1].upper() in FUNC_TAIL:
        return '_'.join(parts[:-1])
    return base


def scan_unused_decls(proj, bases):
    """A3：返回"头文件里声明了 _PORT/_PIN、但全工程没有任何代码引用"的基名集合。

    判据很硬：某个基名除了它自己的声明头之外，在工程任何 .c/.h 里都找不到第二次出现
    （注释与字符串先掩掉，免得"注释里提了一句"就算用了）。命中的多半是没启用的模块 ——
    这些脚回收代价最低，正好配合引脚冲突报告里的"候选脚"建议。只读，不改任何文件。
    """
    if not os.path.isdir(proj):
        return set()
    masked = {}
    for dp, _, fns in os.walk(proj):
        for fn in fns:
            if fn.lower().endswith(('.c', '.h')):
                fp = os.path.normpath(os.path.join(dp, fn))
                masked[fp] = strip_comments('\n'.join(read_lines(fp)))

    unused = set()
    for base, info in bases.items():
        decl = info['loc'].rsplit(':', 1)[0]
        decl = os.path.normpath(decl if os.path.isabs(decl) else os.path.join(ROOT, decl))
        for fp, txt in masked.items():
            if os.path.normcase(fp) == os.path.normcase(decl):
                continue
            if base in txt:
                break
        else:
            unused.add(base)
    return unused


def unused_pin_map(bases, unused_bases):
    """{脚名: [基名,...]} —— 由"无人引用"的基名占着的脚。"""
    m = {}
    for base in sorted(unused_bases):
        for pb in bases[base]['pins']:
            m.setdefault(fmt_pin(pb), []).append(base)
    return m


def scan_pins(inc_dir):
    """返回 (bases, owners, unpaired, dup_base, nfiles)
       bases: {base: {'mod':module, 'pins':set((port,bit)), 'loc':rel:line}}"""
    bases = {}
    owners = {}
    ports_seen = {}
    pins_seen = {}
    files = sorted(f for f in os.listdir(inc_dir) if f.lower().endswith('.h'))
    for fn in files:
        here = rel(os.path.join(inc_dir, fn))
        for i, line in enumerate(read_lines(os.path.join(inc_dir, fn)), 1):
            m = RE_PORT.match(line)
            if m:
                ports_seen.setdefault(m.group(1), []).append((m.group(2)[4:], here, i))
                continue
            m = RE_PIN.match(line)
            if m:
                pins_seen.setdefault(m.group(1), []).append((RE_BIT.findall(m.group(2)), here, i))

    for base, ports in ports_seen.items():
        port = ports[0][0]
        loc = '%s:%d' % (ports[0][1], ports[0][2])
        pins = set()
        for bits, _, _ in pins_seen.get(base, []):
            for b in bits:
                pins.add((port, int(b)))
        if pins:
            bases[base] = {'mod': module_of(base), 'pins': pins, 'loc': loc}
            for pb in pins:
                owners.setdefault(pb, []).append(base)
    unpaired = sorted(set(ports_seen) ^ set(pins_seen))
    dup_base = sorted(b for b, v in ports_seen.items() if len(v) > 1)
    return bases, owners, unpaired, dup_base, len(files)


def read_prio_macros(inc_dir):
    prio = {}
    for fn in os.listdir(inc_dir):
        if not fn.lower().endswith('.h'):
            continue
        for line in read_lines(os.path.join(inc_dir, fn)):
            m = re.match(r'\s*#\s*define\s+(SYS_\w*_(?:PRE|SUB)_PRIO)\s+(\d+)', line)
            if m:
                prio[m.group(1)] = int(m.group(2))
    return prio


def resolve(tok, prio):
    if tok.isdigit():
        return int(tok)
    return prio.get(tok)


def scan_irqs(src_dir, prio, max_syscall):
    """返回 [(irqn, pre, sub, 出处, 是否越界)]"""
    found = []
    if not os.path.isdir(src_dir):
        return found
    for fn in sorted(os.listdir(src_dir)):
        if not fn.lower().endswith('.c'):
            continue
        path = os.path.join(src_dir, fn)
        here = rel(path)
        lines = read_lines(path)
        for i, line in enumerate(lines, 1):
            m = RE_IRQ_SET.search(line)
            if m:
                if not m.group(1).endswith('IRQn'):
                    continue      # IRQn 由形参传入，静态无法解析
                pre = resolve(m.group(2), prio)
                sub = resolve(m.group(3), prio)
                if pre is None:
                    continue      # 优先级也不是已知宏/常数
                found.append((m.group(1), pre, sub, '%s:%d' % (here, i),
                              max_syscall is not None and pre < max_syscall))
                continue
            m = RE_IRQ_CH.search(line)
            if m and m.group(1).endswith('IRQn'):
                irqn = m.group(1)
                pre = sub = None
                for j in range(i, min(i + 15, len(lines))):
                    mp = RE_IRQ_PRE.search(lines[j])
                    if mp and pre is None:
                        pre = resolve(mp.group(1), prio)
                    ms = RE_IRQ_SUB.search(lines[j])
                    if ms and sub is None:
                        sub = resolve(ms.group(1), prio)
                if pre is None:
                    continue
                found.append((irqn, pre, sub, '%s:%d' % (here, i),
                              max_syscall is not None and pre < max_syscall))
    # 去重（同一 IRQn 多次出现保留首次）
    seen = {}
    ordered = []
    for item in found:
        if item[0] in seen:
            continue
        seen[item[0]] = True
        ordered.append(item)
    return ordered


def read_max_syscall(cfg):
    if not os.path.exists(cfg):
        return None
    for line in read_lines(cfg):
        m = re.match(r'\s*#\s*define\s+configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY\s+(\d+)', line)
        if m:
            return int(m.group(1))
    return None


RE_DMA_STREAM = re.compile(r'\bDMA([12])_Stream([0-7])\b')
RE_DMA_CHANNEL = re.compile(r'\bDMA_Channel_([0-7])\b')
RE_DMA_MACRO = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*?)_DMA_(STREAM|CHANNEL|IRQ)\s+(\S+)')
RE_DMA_CALL = re.compile(r'\b(SYS_DMA_\w+|DMA_Init|DMA_DeInit|DMA_ITConfig)\s*\(\s*([^,)]+)')
# "真占用"只认这三种写法：字面量当流参数传进去、赋给某个字段/变量、被 return 出去。
# 驱动内部的枚举表（dma_irqn() 数组、X-Macro 清单、指针范围比较）不算占用，
# 否则整个 FWLIB 会把 16 条流全报一遍，听起来像全都在抢。
RE_DMA_CLAIM = re.compile(
    r'(?:DMA_Init|DMA_DeInit|DMA_ITConfig|SYS_DMA_\w+)\s*\(\s*(DMA[12]_Stream[0-7])\b'
    r'|=\s*(DMA[12]_Stream[0-7])\b'
    r'|\breturn\s+(DMA[12]_Stream[0-7])\b')
RE_DMA_CH_CLAIM = re.compile(
    r'(?:DMA_Init|DMA_DeInit|SYS_DMA_\w+)\s*\([^,]*,\s*(DMA_Channel_[0-7])'
    r'|=\s*(DMA_Channel_[0-7])'
    r'|\breturn\s+(DMA_Channel_[0-7])')


def strip_comments(s):
    """把注释与字符串内容替换成空格（保留换行），长度不变。
    扫"真占用"时必须用它：工程里有注释写着 `= DMA1_Stream0~7`，直接扫原文会把它当占用。"""
    out = list(s)
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if c == '/' and i + 1 < n and s[i + 1] == '/':
            while i < n and s[i] != '\n':
                out[i] = ' '
                i += 1
        elif c == '/' and i + 1 < n and s[i + 1] == '*':
            out[i] = out[i + 1] = ' '
            i += 2
            while i < n and not (s[i] == '*' and i + 1 < n and s[i + 1] == '/'):
                if s[i] != '\n':
                    out[i] = ' '
                i += 1
            if i < n:
                out[i] = out[i + 1] = ' '
                i += 2
        elif c == '"' or c == "'":
            q = c
            out[i] = ' '
            i += 1
            while i < n and s[i] != q:
                if s[i] == '\\' and i + 1 < n:
                    out[i] = out[i + 1] = ' '
                    i += 2
                else:
                    if s[i] != '\n':
                        out[i] = ' '
                    i += 1
            if i < n:
                out[i] = ' '
                i += 1
        else:
            i += 1
    return ''.join(out)


def scan_dma(proj):
    """扫工程里的 DMA 占用，返回 (claims, unresolved)。

    claims 每条 = {'module', 'streams', 'channels', 'loc'}；数据只有两类来源：
    FWLIB\\inc 里的 *_DMA_STREAM / *_DMA_CHANNEL 宏，以及 FWLIB\\src 里真实出现的
    DMAx_Streamn / DMA_Channel_n 字面量。流由形参或结构体字段传进去的写法
    （p->tx_stream / adc_dma_stream(adc)）静态判不出，归到 unresolved 列出来，不猜。
    """
    inc = os.path.join(proj, 'FWLIB', 'inc')
    src = os.path.join(proj, 'FWLIB', 'src')
    claims = []
    unresolved = []

    if os.path.isdir(inc):
        for fn in sorted(os.listdir(inc)):
            if not fn.endswith('.h'):
                continue
            here = rel(os.path.join(inc, fn))
            st, ch = {}, {}
            for i, line in enumerate(read_lines(os.path.join(inc, fn)), 1):
                m = RE_DMA_MACRO.match(line)
                if not m:
                    continue
                if m.group(2) == 'STREAM':
                    v = RE_DMA_STREAM.search(m.group(3))
                    if v:
                        st[m.group(1)] = ('DMA%s_Stream%s' % (v.group(1), v.group(2)),
                                          '%s:%d' % (here, i))
                elif m.group(2) == 'CHANNEL':
                    v = RE_DMA_CHANNEL.search(m.group(3))
                    if v:
                        ch[m.group(1)] = 'DMA_Channel_%s' % v.group(1)
            for mod, (stream, loc) in st.items():
                claims.append({'module': mod, 'streams': {stream},
                               'channels': {ch[mod]} if mod in ch else set(), 'loc': loc})
                if mod not in ch:
                    unresolved.append((mod, loc, '只声明了 STREAM 宏，没写对应的 CHANNEL 宏'))

    if os.path.isdir(src):
        for fn in sorted(os.listdir(src)):
            if not fn.endswith('.c'):
                continue
            here = rel(os.path.join(src, fn))
            mod = os.path.splitext(fn)[0]
            streams, channels, first = set(), set(), ''
            raw_lines = read_lines(os.path.join(src, fn))
            masked_lines = strip_comments('\n'.join(raw_lines)).splitlines()
            for i, line in enumerate(masked_lines, 1):
                for v in RE_DMA_CLAIM.finditer(line):
                    st = v.group(1) or v.group(2) or v.group(3)
                    streams.add(st)
                    first = first or '%s:%d' % (here, i)
                for v in RE_DMA_CH_CLAIM.finditer(line):
                    channels.add(v.group(1) or v.group(2) or v.group(3))
                    first = first or '%s:%d' % (here, i)
                for v in RE_DMA_CALL.finditer(line):
                    arg = v.group(2).strip()
                    if 'DMA' in arg or arg.isdigit():
                        continue        # 字面量流/通道，已被上面的扫描覆盖
                    unresolved.append((mod, '%s:%d' % (here, i),
                                       '%s(...) 的第一个参数是 `%s`，静态判不出是哪条流'
                                       % (v.group(1), arg)))
            if streams or channels:
                claims.append({'module': mod, 'streams': streams,
                               'channels': channels, 'loc': first})
    return claims, unresolved


def render_dma(proj):
    """DMA 资源一节：把工程里真实写下来的数据流/通道占用列成矩阵，
    并把"同一条流被多个模块用"单独标出来。"""
    out = ['### DMA 资源', '']
    claims, unresolved = scan_dma(proj)
    if not claims and not unresolved:
        out.append('未找到可静态解析的 DMA 资源占用（工程可能还没用 DMA）。')
        out.append('')
        return '\n'.join(out)

    owner = {}
    for c in claims:
        for st in c['streams']:
            owner.setdefault(st, set()).add(c['module'])
    shared = sorted((st, sorted(mods)) for st, mods in owner.items() if len(mods) > 1)

    out.append('模块 %d 个，数据流 %d 条，被多个模块共用的流 %d 条，'
               '无法静态解析的调用 %d 处。'
               % (len(claims), len(owner), len(shared), len(unresolved)))
    out.append('')
    out.append('| 模块 | 数据流 | 通道 | 出处 |')
    out.append('|---|---|---|---|')
    for c in sorted(claims, key=lambda x: x['module']):
        out.append('| `%s` | %s | %s | `%s` |'
                   % (c['module'], ' '.join(sorted(c['streams'])) or '-',
                      ' '.join(sorted(c['channels'])) or '-', c['loc']))
    out.append('')
    out.append('同一数据流被多个模块使用（同时用会互相改掉对方的 DMA 配置，'
               '要么二选一、要么自己做占用登记）：')
    out.append('')
    if shared:
        for st, mods in shared:
            out.append('- %s ← %s' % (st, '、'.join(mods)))
    else:
        out.append('无。')
    out.append('')
    if unresolved:
        out.append('无法静态解析的 DMA 调用（流由形参/字段传入，需人工核对）：')
        out.append('')
        show = unresolved[:20]
        for mod, loc, why in show:
            out.append('- `%s` @ `%s`：%s' % (mod, loc, why))
        if len(unresolved) > len(show):
            out.append('- ……另有 %d 处同类调用，按上面的写法自查即可。'
                       % (len(unresolved) - len(show)))
        out.append('')
    out.append('注：这里只列工程自己写下的映射，不内置芯片手册的"外设-通道"对照表 —— '
               '那张表必须对着数据手册核，凭印象写出来是害人。')
    out.append('')
    return '\n'.join(out)


def fmt_pin(pb):
    return 'P%s%d' % (pb[0], pb[1])


# 不能当"改到别的脚"的候选：SWD/JTAG 调试口，改到那里等于自断下载与在线调试通道。
SKIP_AS_CAND = {'PA13', 'PA14', 'PA15', 'PB3', 'PB4'}


def nearest_free(port, bit, free_set, limit=6):
    """B2：给一个冲突脚找"就近"的空闲脚。
    先同端口按位距由近到远（相邻脚改起来最省事，布线也最短），再跨端口（按端口、位升序）。
    全程跳过 SWD/JTAG 调试口。返回 (同端口候选, 跨端口候选)。"""
    def ok(pb):
        return fmt_pin(pb) not in SKIP_AS_CAND

    same = sorted((pb for pb in free_set if pb[0] == port and pb[1] != bit and ok(pb)),
                  key=lambda pb: (abs(pb[1] - bit), pb[1]))
    cross = sorted((pb for pb in free_set if pb[0] != port and ok(pb)),
                   key=lambda pb: (pb[0], pb[1]))
    return [fmt_pin(pb) for pb in same[:limit]], [fmt_pin(pb) for pb in cross[:limit]]


def render_board(title, inc_dir, use_list, result):
    out = []
    out.append('## %s' % title)
    out.append('')
    bases, owners, unpaired, dup_base, nfiles = result
    conflicts = sorted(((k, v) for k, v in owners.items() if len(v) > 1),
                       key=lambda kv: (kv[0][0], kv[0][1]))
    free = sorted(ALL_PINS - set(owners), key=lambda pb: (pb[0], pb[1]))
    out.append('头文件 %d 个，占用脚 %d 个，冲突 %d 个，无人认领 %d 个。'
               % (nfiles, len(owners), len(conflicts), len(free)))
    out.append('')

    # 方案检查
    if use_list:
        sel = {b for b in bases for p in use_list if b.startswith(p)}
        sel_pins = {}
        for b in sel:
            for pb in bases[b]['pins']:
                sel_pins.setdefault(pb, []).append(b)
        clash = sorted(((k, v) for k, v in sel_pins.items() if len(v) > 1),
                       key=lambda kv: (kv[0][0], kv[0][1]))
        out.append('### 方案检查（--use %s）' % ','.join(use_list))
        out.append('')
        out.append('选中基名 %d 个，占用脚 %d 个。' % (len(sel), len(sel_pins)))
        out.append('')
        if sel:
            out.append('选中模块：%s' % '、'.join(sorted(sel)))
            out.append('')
        if clash:
            out.append('选中模块之间的冲突：')
            out.append('')
            for (port, bit), lst in clash:
                out.append('- %s ← %s' % (fmt_pin((port, bit)), ', '.join(sorted(lst))))
        else:
            out.append('选中模块之间无冲突。')
        out.append('')

    out.append('### 模块与引脚')
    out.append('')
    out.append('| 基名 | 模块 | 引脚 | 出处 |')
    out.append('|---|---|---|---|')
    for base in sorted(bases):
        pins = ' '.join(fmt_pin(pb) for pb in sorted(bases[base]['pins']))
        out.append('| `%s` | %s | %s | `%s` |' % (base, bases[base]['mod'], pins, bases[base]['loc']))
    out.append('')

    out.append('### 冲突引脚')
    out.append('')
    if conflicts:
        for (port, bit), lst in conflicts:
            out.append('- %s ← %s' % (fmt_pin((port, bit)), ', '.join(sorted(set(lst)))))
        out.append('')
        # 修复建议：候选脚只取"库里没人认领"的脚，并明确要求核对原理图。
        # B2：改成按冲突脚就近给 —— 同端口（位距近的先）优先，跨端口次之，而不是
        # 全表按端口字母顺序取前 8 个（那 8 个往往跟冲突脚八竿子打不着）。
        free_set = set(free)
        cand = [fmt_pin(pb) for pb in free if fmt_pin(pb) not in SKIP_AS_CAND][:8]
        out.append('> **下一步怎么做**：上面每个冲突脚都得有一个模块让出来，否则后 init 的会悄悄覆盖前一个。'
                   '可选方向：① 把其中一个模块改到别的脚（库内当前无人认领的 %d 个脚，'
                   '各冲突脚的就近候选见下表，但**必须对照原理图确认该脚在板上真的没接东西**'
                   '——库里没人用不等于硬件空着）；'
                   '② 两个模块本来就不会同时用（比如互斥的外设），在代码里加互斥判断；'
                   '③ 确认是"声明了但没实际初始化"的僵尸宏，删掉即可。'
                   % len(free))
        out.append('')
        out.append('**各冲突脚的就近候选**（同端口优先、位距近的先；已排除 SWD/JTAG 调试口；'
                   '整板无人认领脚里前 8 个：%s）：' % ('、'.join(cand) if cand else '（无）'))
        out.append('')
        for (port, bit), lst in conflicts:
            same, cross = nearest_free(port, bit, free_set)
            out.append('- %s → 同端口：%s；跨端口：%s' % (
                fmt_pin((port, bit)),
                '、'.join(same) if same else '（本端口无空闲）',
                '、'.join(cross) if cross else '（无）'))
        out.append('')
    else:
        out.append('无。')
    out.append('')

    # 调试口被占用（B1）：SWD/JTAG 的脚被业务宏占了，按严重报出。
    dbg = sorted(((fmt_pin(pb), sorted(set(owners[pb]))) for pb in owners
                  if fmt_pin(pb) in DEBUG_PINS), key=lambda kv: kv[0])
    out.append('### 调试口被占用（严重）')
    out.append('')
    if dbg:
        for name, mods in dbg:
            out.append('- %s ← %s' % (name, '、'.join(mods)))
        out.append('')
        out.append('> SWD/JTAG 的脚被业务宏占了，等于自断下载与在线调试通道。'
                   '**PA13（SWDIO）/ PA14（SWCLK）任何时候都不能动**；'
                   'PA15（JTDI）/ PB3（JTDO）/ PB4（NJTRST）只有在代码里关掉 JTAG 复用'
                   '（只保留 SW/D）之后才可用，而且关了就没法用 JTAG 调试。'
                   '处理方向：① 把业务宏改到别的脚（用上面的无人认领/声明未用脚当候选）；'
                   '② 确实要用 PB3/PB4 时，明确在代码里关掉 JTAG，并写清取舍。')
    else:
        out.append('无。')
    out.append('')

    out.append('### 无人认领引脚')
    out.append('')
    out.append('  ' + '  '.join(fmt_pin(pb) for pb in free) if free else '无。')
    out.append('')

    # 声明了但没人用（A3）：头文件里有 _PORT/_PIN，工程里却没有一处引用。
    umap = unused_pin_map(bases, scan_unused_decls(
        os.path.dirname(os.path.dirname(inc_dir)), bases))
    out.append('### 声明了但没人用（僵尸宏）')
    out.append('')
    if umap:
        for pin in sorted(umap):
            out.append('- %s ← %s（头文件里声明了 _PORT/_PIN，全工程没有代码引用）'
                       % (pin, '、'.join(umap[pin])))
        out.append('')
        out.append('> **下一步怎么做**：这些脚循环里没人用、硬件上也可能空着，'
                   '是引脚冲突时最省事的候选。动手前仍要对照原理图确认，'
                   '并确认不是"别的宏绕开了它"（比如模块直接用了别的脚名）。')
    else:
        out.append('无。')
    out.append('')

    if unpaired or dup_base:
        out.append('### 需人工确认')
        out.append('')
        for b in unpaired:
            out.append('- 宏不成对：`%s`' % b)
        for b in dup_base:
            out.append('- 同板内重定义：`%s`' % b)
        out.append('')

    # 中断资源
    prio = read_prio_macros(inc_dir)
    max_syscall = read_max_syscall(os.path.join(os.path.dirname(inc_dir), '..', 'FreeRTOS', 'port', 'FreeRTOSConfig.h'))
    irqs = scan_irqs(os.path.join(os.path.dirname(inc_dir), 'src'), prio, max_syscall)
    out.append('### 中断资源')
    out.append('')
    out.append('内核上限 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = %s。'
               '抢占低于此值的 ISR 若调用 FromISR，会越过内核 BASEPRI。'
               % (max_syscall if max_syscall is not None else '?'))
    out.append('')
    if irqs:
        out.append('| IRQn | 抢占 | 子 | 出处 | 备注 |')
        out.append('|---|---|---|---|---|')
        for irqn, pre, sub, loc, bad in irqs:
            note = '抢占 < 上限' if bad else ''
            out.append('| `%s` | %s | %s | `%s` | %s |'
                       % (irqn, pre if pre is not None else '?',
                          sub if sub is not None else '?', loc, note))
    else:
        out.append('未找到可静态解析的 NVIC 配置调用（IRQn 由形参传入的写法不计入）。')
    out.append('')

    out.append(render_dma(os.path.dirname(os.path.dirname(inc_dir))))
    return '\n'.join(out)


def to_json(results):
    """结构化输出，供工具箱的可视化体检面板读取。
    occupied / conflicts / free 的键都是 'PA2' 这种脚名，值为占用者模块基名列表。
    decl_unused 是"声明了 _PORT/_PIN 但全工程没引用"的脚名列表（A3 热力图的第四态）。"""
    boards = []
    for title, inc, r in results:
        bases, owners, unpaired, dup_base, nfiles = r
        occupied = {}
        conflicts = {}
        for (port, bit), mods in sorted(owners.items(), key=lambda kv: (kv[0][0], kv[0][1])):
            name = fmt_pin((port, bit))
            uniq = sorted(set(mods))
            occupied[name] = uniq
            if len(uniq) > 1:
                conflicts[name] = uniq
        free = sorted(ALL_PINS - set(owners), key=lambda pb: (pb[0], pb[1]))
        # 每个脚的具体出处（宏基名 + 文件:行），供面板点击查看
        detail = {}
        for base, info in bases.items():
            for pb in info['pins']:
                detail.setdefault(fmt_pin(pb), []).append(
                    {'base': base, 'loc': info['loc']})
        decl_unused = sorted(unused_pin_map(bases, scan_unused_decls(
            os.path.dirname(os.path.dirname(inc)), bases)))
        debug_used = sorted(fmt_pin(pb) for pb in owners if fmt_pin(pb) in DEBUG_PINS)
        boards.append({
            'title': title,
            'files': nfiles,
            'occupied': occupied,
            'conflicts': conflicts,
            'free': [fmt_pin(pb) for pb in free],
            'decl_unused': decl_unused,
            'debug_used': debug_used,
            'pin_detail': detail,
        })
    ok = all(not b['conflicts'] and not b['debug_used'] for b in boards)
    return {'tool': 'pin_planner', 'schema': 1, 'ok': ok, 'boards': boards}


def selftest():
    """自检：在临时目录造一块合成板的 FWLIB，故意埋下几类错例 ——
    两个模块抢同一个引脚、一对只有 _PORT 没有 _PIN 的宏、抢占低于内核上限的 ISR
    （其中 ISR 用 SYS_NVIC_SetPriority 与 NVIC_InitStructure 两种写法各一个，覆盖两条解析路径）、
    一个声明了却全工程没人引用的"僵尸宏"（同时留一个被引用的做反例）。
    直接调 scan_pins / scan_unused_decls / read_max_syscall / scan_irqs / render_board /
    to_json 核对确实命中，不写报告、不碰仓库。
    """
    import shutil
    import tempfile

    checks = []

    def ck(name, ok, extra=''):
        checks.append((name, bool(ok), extra))

    tmp = tempfile.mkdtemp(prefix='pin_selftest_')
    try:
        inc = os.path.join(tmp, 'FWLIB', 'inc')
        src = os.path.join(tmp, 'FWLIB', 'src')
        cfgdir = os.path.join(tmp, 'FreeRTOS', 'port')
        os.makedirs(inc)
        os.makedirs(src)
        os.makedirs(cfgdir)

        def wr(path, text):
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)

        # 引脚宏：LORA_E22 与 W25QXX 故意抢同一个 PB5；TFT 独占 PC7 且被引用（反例：
        # 不该被判成"声明未用"）；ADC 只有 _PORT 不成对；ADC_LIGHT 独占 PA3 且无人引用（错例）；
        # KEY1 占 PA13（SWDIO）且被引用 —— B1 调试口错例（占了调试口必须报严重）。
        wr(os.path.join(inc, 'pin_cfg.h'),
           '#define LORA_E22_PORT   GPIOB\n'
           '#define LORA_E22_PIN    GPIO_Pin_5\n'
           '#define W25QXX_PORT     GPIOB\n'
           '#define W25QXX_PIN      GPIO_Pin_5\n'
           '#define TFT_PORT        GPIOC\n'
           '#define TFT_PIN         GPIO_Pin_7\n'
           '#define ADC_LIGHT_PORT  GPIOA\n'
           '#define ADC_LIGHT_PIN   GPIO_Pin_3\n'
           '#define KEY1_PORT       GPIOA\n'
           '#define KEY1_PIN        GPIO_Pin_13\n'
           '#define ADC_PORT        GPIOA\n')
        wr(os.path.join(inc, 'prio_cfg.h'),
           '#define SYS_USART1_PRE_PRIO  4\n'
           '#define SYS_SPI1_PRE_PRIO    5\n')
        wr(os.path.join(cfgdir, 'FreeRTOSConfig.h'),
           '#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5\n')
        wr(os.path.join(src, 'irq.c'),
           'void Bsp_IrqInit(void)\n{\n'
           '    SYS_NVIC_SetPriority(USART1_IRQn, SYS_USART1_PRE_PRIO, 0);\n'
           '    SYS_NVIC_SetPriority(SPI1_IRQn, SYS_SPI1_PRE_PRIO, 1);\n'
           '    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;\n'
           '    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;\n'
           '    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;\n}\n')
        # DMA：宏式声明一条完整的（FOO）、一条缺 CHANNEL 的（BAR）；
        # 另外让 a.c 与 b.c 都去用 DMA2_Stream0（应判为"被多个模块共用"），
        # c.c 用形参传流（应归入"无法静态解析"）。
        wr(os.path.join(inc, 'dma_cfg.h'),
           '#define FOO_DMA_STREAM   DMA1_Stream5\n'
           '#define FOO_DMA_CHANNEL  DMA_Channel_7\n'
           '#define FOO_DMA_IRQ      DMA1_Stream5_IRQn\n'
           '#define BAR_DMA_STREAM   DMA2_Stream3\n')
        wr(os.path.join(src, 'a.c'),
           'void Bsp_A_Init(void) { DMA_DeInit(DMA2_Stream0); GPIO_Init(TFT_PORT, &cfg); '
           'GPIO_Init(KEY1_PORT, &cfg); }\n')
        wr(os.path.join(src, 'b.c'),
           'void Bsp_B_Init(void) { DMA_DeInit(DMA2_Stream0); p->dma_ch = DMA_Channel_2; }\n')
        wr(os.path.join(src, 'c.c'),
           'void Bsp_C_Start(void)\n{\n'
           '    SYS_DMA_MemToPeriph(p->tx_stream, 4, buf, 8);\n}\n')

        bases, owners, unpaired, dup_base, nfiles = scan_pins(inc)
        result = (bases, owners, unpaired, dup_base, nfiles)
        conflicts = sorted(((k, v) for k, v in owners.items() if len(v) > 1),
                           key=lambda kv: (kv[0][0], kv[0][1]))

        ck('引脚冲突命中 1 个且在 PB5',
           len(conflicts) == 1 and conflicts[0][0] == ('B', 5),
           '、'.join(fmt_pin(k) for k, _ in conflicts) or '无')
        ck('冲突的两个模块都点出来',
           len(conflicts) == 1 and sorted(conflicts[0][1]) == ['LORA_E22', 'W25QXX'],
           '、'.join(conflicts[0][1]) if conflicts else '无')
        ck('不成对的宏被列出（ADC）', unpaired == ['ADC'], '、'.join(unpaired) or '无')
        ck('模块与占用脚计数正确（5 个模块 / 4 个脚）',
           len(bases) == 5 and len(owners) == 4 and nfiles == 3,
           '%d / %d / %d' % (len(bases), len(owners), nfiles))

        max_syscall = read_max_syscall(os.path.join(cfgdir, 'FreeRTOSConfig.h'))
        ck('内核上限宏解析正确', max_syscall == 5, str(max_syscall))

        prio = read_prio_macros(inc)
        irqs = scan_irqs(src, prio, max_syscall)
        bad = sorted(i[0] for i in irqs if i[4])
        ck('抢占越界项抓到 2 个（两种写法各一）',
           bad == ['TIM2_IRQn', 'USART1_IRQn'], '、'.join(bad) or '无')
        ck('不越界的 SPI1 未被误报', not any(i[0] == 'SPI1_IRQn' and i[4] for i in irqs))
        ck('抢占值都按宏/常数解析出来',
           {i[0]: i[1] for i in irqs} == {'USART1_IRQn': 4, 'SPI1_IRQn': 5, 'TIM2_IRQn': 3},
           str({i[0]: i[1] for i in irqs}))

        txt = render_board('板X', inc, ['LORA_E22', 'W25QXX'], result)
        ck('方案检查报出选中模块之间的冲突',
           '选中模块之间的冲突' in txt and 'PB5' in txt and 'LORA_E22' in txt)
        ck('报告渲染不炸且含中断资源表', '### 中断资源' in txt and 'USART1_IRQn' in txt)

        # B1 调试口被占：错例（PA13 ← KEY1 必须报严重并写进报告）/
        # 反例（业务脚 PB5 / PC7 / PA3 不该被算成调试口）
        ck('调试口被占被判严重（PA13 ← KEY1）且写进报告',
           '### 调试口被占用（严重）' in txt and '- PA13 ← KEY1' in txt)

        # A3 声明了但没人用：错例（三个没人引用的基名必须被抓到）/
        # 反例（被 a.c 引用的 TFT 必须不报，不能把"用过"的也算成僵尸）
        unused = scan_unused_decls(tmp, bases)
        ck('僵尸宏判定：没引用的 LORA_E22/W25QXX/ADC_LIGHT 命中，被引用的 TFT 不命中',
           unused == {'LORA_E22', 'W25QXX', 'ADC_LIGHT'}, '、'.join(sorted(unused)) or '无')
        ck('报告有「声明了但没人用」一节且列出 ADC_LIGHT',
           '### 声明了但没人用' in txt and 'ADC_LIGHT' in txt)

        # B2 冲突候选脚就近排序：错例（同端口按位距由近到远：PB10 应给 PB12 先于 PB2）/
        # 反例（调试口 PA13/PA14/PA15 不许当候选，业务脚 PA6 可以）
        fs = {('B', 2), ('B', 12), ('B', 10), ('A', 13), ('A', 14), ('A', 15),
              ('A', 6), ('C', 1)}
        same, cross = nearest_free('B', 10, fs)
        ck('错例：冲突脚 PB10 的同端口候选按位距就近（PB12 先于 PB2），且不含自己',
           same == ['PB12', 'PB2'], '、'.join(same))
        ck('反例：调试口 PA13/PA14/PA15 不当候选，业务脚 PA6 仍在跨端口候选里',
           not any(p in ('PA13', 'PA14', 'PA15') for p in cross) and 'PA6' in cross,
           '、'.join(cross))
        ck('报告里每个冲突脚都给出「就近候选」行（同端口/跨端口）',
           '各冲突脚的就近候选' in txt and '→ 同端口：' in txt)

        claims, unresolved = scan_dma(tmp)
        by_mod = {c['module']: c for c in claims}
        ck('DMA 宏式声明被解析出流与通道',
           by_mod.get('FOO', {}).get('streams') == {'DMA1_Stream5'}
           and by_mod.get('FOO', {}).get('channels') == {'DMA_Channel_7'},
           str(by_mod.get('FOO')))
        ck('DMA 字面量占用被解析（a.c / b.c 都在用 DMA2_Stream0）',
           by_mod.get('a', {}).get('streams') == {'DMA2_Stream0'}
           and by_mod.get('b', {}).get('streams') == {'DMA2_Stream0'},
           '、'.join(sorted(by_mod)))

        # 「共用」判定必须断言 render_dma 的真实输出，不能在这儿自己复算一遍：
        # 以前这里另写了一份 owner/shared，等于测了份影子实现 ——
        # 把 render_dma 里的判定改坏（>1 改成 >99）自检照样全绿。
        dma_txt = render_dma(tmp)
        ck('同一数据流被多个模块共用被抓到（走报告输出）',
           '被多个模块共用的流 1 条' in dma_txt and 'DMA2_Stream0 ←' in dma_txt,
           [l for l in dma_txt.splitlines() if '共用的流' in l][:1])
        ck('独占的那条流不被误判为共用（走报告输出）',
           'DMA1_Stream5 ←' not in dma_txt)
        ck('无法静态解析的调用写进了报告',
           '无法静态解析的 DMA 调用' in dma_txt)
        ck('缺 CHANNEL 宏的声明被列为待核（BAR）',
           any(m == 'BAR' for m, _, _ in unresolved))
        ck('形参传流的调用被列为无法静态解析',
           any('p->tx_stream' in w for _, _, w in unresolved))

        j = to_json([('板X', inc, result)])
        ck('JSON 结构正确（工具名/板数/ok）',
           j['tool'] == 'pin_planner' and len(j['boards']) == 1 and j['ok'] is False)
        ck('JSON 冲突与占用都在',
           j['boards'][0]['conflicts'].get('PB5') == ['LORA_E22', 'W25QXX']
           and 'PC7' in j['boards'][0]['occupied'],
           str(j['boards'][0]['conflicts']))
        ck('JSON 带引脚出处（pin_detail 含 base 与 loc）',
           len(j['boards'][0]['pin_detail'].get('PB5', [])) == 2
           and all('base' in d and 'loc' in d
                   for d in j['boards'][0]['pin_detail']['PB5']),
           str(j['boards'][0]['pin_detail'].get('PB5')))
        ck('JSON 带声明未用引脚（decl_unused 含 PA3/PB5，不含被引用的 PC7）',
           'PA3' in j['boards'][0]['decl_unused']
           and 'PB5' in j['boards'][0]['decl_unused']
           and 'PC7' not in j['boards'][0]['decl_unused'],
           str(j['boards'][0]['decl_unused']))
        ck('JSON 带调试口占用（debug_used == [PA13]）且整板 ok 为 False',
           j['boards'][0]['debug_used'] == ['PA13'] and j['ok'] is False,
           str(j['boards'][0]['debug_used']))
        ck('反例：业务脚 PB5 / PC7 / PA3 不算调试口误报',
           not ({'PB5', 'PC7', 'PA3'} & set(j['boards'][0]['debug_used'])),
           str(j['boards'][0]['debug_used']))
        # B6 JSON 契约：面板读 occupied/conflicts/free/decl_unused/debug_used/pin_detail，
        # 字段一漂移（今天修过的那类问题）这里先红
        ck('JSON 契约：面板读取的字段名/类型都在',
           set(j) == {'tool', 'schema', 'ok', 'boards'}
           and set(j['boards'][0]) == {'title', 'files', 'occupied', 'conflicts', 'free',
                                       'decl_unused', 'debug_used', 'pin_detail'}
           and isinstance(j['boards'][0]['conflicts'], dict)
           and isinstance(j['boards'][0]['free'], list)
           and isinstance(j['boards'][0]['decl_unused'], list)
           and isinstance(j['boards'][0]['debug_used'], list)
           and isinstance(j['boards'][0]['pin_detail'], dict),
           '、'.join(sorted(j['boards'][0])))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    bad_items = [c for c in checks if not c[1]]
    for name, ok, extra in checks:
        print('  [%s] %s%s' % ('PASS' if ok else 'FAIL', name,
                               ('  （实际：%s）' % extra) if (extra and not ok) else ''))
    print('引脚规划自检：%d 通过 / %d 失败' % (len(checks) - len(bad_items), len(bad_items)))
    return 1 if bad_items else 0


def main(argv):
    if '--selftest' in argv:
        return selftest()

    quiet = False
    json_mode = False
    only = None
    use_list = []
    bad = []
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '引脚规划报告.md')
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == '--quiet':
            quiet = True
            i += 1
        elif a == '--json':
            json_mode = True
            i += 1
        elif a in ('--board', '--use', '--out'):
            if i + 1 >= len(argv):
                bad.append('%s 缺少值' % a)
                i += 1
                continue
            if a == '--board':
                only = argv[i + 1]
            elif a == '--use':
                use_list = [x.strip() for x in re.split(r'[,\s]+', argv[i + 1]) if x.strip()]
            else:
                out_path = os.path.abspath(argv[i + 1])
            i += 2
        else:
            bad.append(a)
            i += 1

    if bad:
        print('未知参数：%s' % ' '.join(bad))
        print('可用参数：--board <1|2> | --use <模块,模块> | --out <文件> | --quiet | --json | --selftest')
        return 2

    # 先收集结果，再决定出 md 报告还是结构化 JSON
    results = []
    missing = []
    for title, proj in BOARDS:
        if only == '1' and '板1' not in title:
            continue
        if only == '2' and '板2' not in title:
            continue
        inc = os.path.join(ROOT, proj, 'FWLIB', 'inc')
        if not os.path.isdir(inc):
            missing.append(title)
            continue
        results.append((title, inc, scan_pins(inc)))

    if json_mode:
        print(json.dumps(to_json(results), ensure_ascii=False))
        return 0

    out = ['# 引脚规划报告', '',
           '来源：`本地工具/工具箱/检查/引脚规划/pin_planner.py`（只读扫描，可重复运行）', '']
    out.append('## 总览')
    out.append('')
    out.append('| 板 | 头文件 | 占用脚 | 冲突 | 空闲 |')
    out.append('|---|---|---|---|---|')
    for title, inc, r in results:
        conflicts = sum(1 for v in r[1].values() if len(v) > 1)
        free = len(ALL_PINS - set(r[1]))
        out.append('| %s | %d | %d | %d | %d |' % (title, r[4], len(r[1]), conflicts, free))
    for title in missing:
        out.append('| %s | - | - | - | 目录不存在 |' % title)
    out.append('')
    for title, inc, r in results:
        out.append(render_board(title, inc, use_list, r))
        out.append('')

    out.append('## 说明')
    out.append('')
    out.append('1) 引脚来自 FWLIB\\inc\\*.h 里成对的 `*_PORT` / `*_PIN` 宏，与 pin_audit.py 同口径。')
    out.append('2) "模块"是把 `LORA_E22_TX` 这类基名去掉功能尾缀后的分组，仅供参考。')
    out.append('3) 无人认领引脚只指"库里没有任何模块声明"，不等于硬件上一定空着。')
    out.append('4) 中断资源来自 FWLIB\\src 里的 NVIC 配置调用与 SYS_NVIC_SetPriority。')
    out.append('5) 本报告每次运行覆盖。')
    out.append('')
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out))

    print('引脚规划完成')
    for title, inc, r in results:
        conflicts = sum(1 for v in r[1].values() if len(v) > 1)
        print('  %-24s 占用 %3d  冲突 %2d  空闲 %3d' % (title, len(r[1]), conflicts,
                                                       len(ALL_PINS - set(r[1]))))
    print('  报告: %s' % out_path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
