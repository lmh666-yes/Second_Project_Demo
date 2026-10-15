# -*- coding: utf-8 -*-
r"""引脚与资源规划器（幂等，只读源码，不改任何文件）。

与 `仓库巡检/pin_audit.py` 的分工：
  pin_audit 只做"体检"——列出全板的引脚占用与冲突；
  本工具做"规划"——按模块看引脚与中断资源，并在你选定一组模块后，
  直接报出这组模块之间的冲突、要占的脚、以及中断优先级是否越过内核上限。

输出：
  1) 每块板：模块与引脚表（模块基名 -> 占用的引脚）、冲突引脚、无人认领引脚；
  2) 每块板：中断资源表（IRQn / 抢占 / 子优先级 / 出处），标注抢占低于内核上限的项；
  3) 若给出 --use，附"方案检查"：选中模块之间的冲突与引脚清单。

报告写到脚本同目录的 引脚规划报告.md，并在控制台打印摘要。只读，不改任何文件。

用法：
    python 本地工具\工具箱\检查\引脚规划\pin_planner.py
    python 本地工具\工具箱\检查\引脚规划\pin_planner.py --use "LORA_E22,W25QXX,RS485"
    python 本地工具\工具箱\检查\引脚规划\pin_planner.py --board 2 --quiet
"""
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


def module_of(base):
    """把 `LORA_E22_TX` 这类基名归到一个模块名（去掉功能尾缀）。"""
    parts = base.split('_')
    if len(parts) >= 2 and parts[-1].upper() in FUNC_TAIL:
        return '_'.join(parts[:-1])
    return base


def scan_pins(inc_dir):
    """返回 (bases, owners, unpaired, dup_base, nfiles)
       bases: {base: {'mod':module, 'pins':set((port,bit)), 'loc':rel:line}}"""
    bases = {}
    owners = {}
    ports_seen = {}
    pins_seen = {}
    files = sorted(f for f in os.listdir(inc_dir) if f.lower().endswith('.h'))
    for fn in files:
        rel = os.path.relpath(os.path.join(inc_dir, fn), ROOT)
        for i, line in enumerate(read_lines(os.path.join(inc_dir, fn)), 1):
            m = RE_PORT.match(line)
            if m:
                ports_seen.setdefault(m.group(1), []).append((m.group(2)[4:], rel, i))
                continue
            m = RE_PIN.match(line)
            if m:
                pins_seen.setdefault(m.group(1), []).append((RE_BIT.findall(m.group(2)), rel, i))

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
        rel = os.path.relpath(path, ROOT)
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
                found.append((m.group(1), pre, sub, '%s:%d' % (rel, i),
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
                found.append((irqn, pre, sub, '%s:%d' % (rel, i),
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


def fmt_pin(pb):
    return 'P%s%d' % (pb[0], pb[1])


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
    else:
        out.append('无。')
    out.append('')

    out.append('### 无人认领引脚')
    out.append('')
    out.append('  ' + '  '.join(fmt_pin(pb) for pb in free) if free else '无。')
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
    return '\n'.join(out)


def main(argv):
    quiet = False
    only = None
    use_list = []
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '引脚规划报告.md')
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == '--quiet':
            quiet = True
            i += 1
        elif a == '--board' and i + 1 < len(argv):
            only = argv[i + 1]
            i += 2
        elif a == '--use' and i + 1 < len(argv):
            use_list = [x.strip() for x in re.split(r'[,\s]+', argv[i + 1]) if x.strip()]
            i += 2
        elif a == '--out' and i + 1 < len(argv):
            out_path = os.path.abspath(argv[i + 1])
            i += 2
        else:
            i += 1

    out = ['# 引脚规划报告', '',
           '来源：`本地工具/工具箱/检查/引脚规划/pin_planner.py`（只读扫描，可重复运行）', '']
    out.append('## 总览')
    out.append('')
    out.append('| 板 | 头文件 | 占用脚 | 冲突 | 空闲 |')
    out.append('|---|---|---|---|---|')
    results = []
    for title, proj in BOARDS:
        if only == '1' and '板1' not in title:
            continue
        if only == '2' and '板2' not in title:
            continue
        inc = os.path.join(ROOT, proj, 'FWLIB', 'inc')
        if not os.path.isdir(inc):
            out.append('| %s | - | - | - | 目录不存在 |' % title)
            continue
        r = scan_pins(inc)
        results.append((title, inc, r))
        conflicts = sum(1 for v in r[1].values() if len(v) > 1)
        free = len(ALL_PINS - set(r[1]))
        out.append('| %s | %d | %d | %d | %d |' % (title, r[4], len(r[1]), conflicts, free))
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
