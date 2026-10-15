# -*- coding: utf-8 -*-
r"""两板 FWLIB 引脚占用核查（幂等，只读源码，不改任何文件）。

背景：`代码审查报告.md` §9 记的那类问题 —— "两个模块把同一个引脚写进各自的
      `#define`"，编译器一个字都不会说，两边都编得过、两边都会 GPIO_Init 那个脚，
      后 init 的悄悄把前一个改掉。根因是**库里没有全局引脚分配表**。

本脚本把两板 `FWLIB\inc\*.h` 里所有 `#define XXXX_PORT  GPIOn` +
`#define XXXX_PIN  GPIO_Pin_m` 配成对，按 (端口, 位号) 归并，输出：

  ① 每个引脚的占用者清单（模块基名 + `文件:行号`）
  ② 冲突清单（同一脚 >= 2 个占用者）—— 供人工判断"是不是活冲突"
  ③ 两板各自"无人认领"的引脚清单 —— 选空闲脚时先看这里
  ④ 宏名不成对（只有 _PORT 没有 _PIN 或反之）的可疑项

输出写到脚本同目录的 `_pin_audit.txt`，并在控制台打印摘要。

用法：
    python 本地工具\仓库巡检\pin_audit.py
    python 本地工具\仓库巡检\pin_audit.py --quiet     只写文件不打印全表
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BOARDS = [
    ('板1 检测端 (F407ZE)', os.path.join(ROOT, 'EnvMonitor', 'Project_1_检测端', 'Project', 'FWLIB', 'inc')),
    ('板2 通信端 (F407ZG)', os.path.join(ROOT, 'EnvMonitor', 'Project_2_数据通信端', 'Project', 'FWLIB', 'inc')),
]

PORTS = ['A', 'B', 'C', 'D', 'E', 'F', 'G']          # F407Zx 是 LQFP144，只有 PA~PG
PINS_PER_PORT = 16

RE_PORT = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*?)_PORT\s+(GPIO[A-Z])\s*(?:/\*.*)?$')
RE_PIN = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*?)_PIN\s+((?:GPIO_Pin_\d+)(?:\s*\|\s*GPIO_Pin_\d+)*)\s*(?:/\*.*)?$')
RE_BIT = re.compile(r'GPIO_Pin_(\d+)')

# 明确不是"某模块占一个脚"的宏：整组端口时钟、整端口初始化用
SKIP_BASES = set()


def read_lines(path):
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ('utf-8', 'gbk', 'latin-1'):
        try:
            return raw.decode(enc).splitlines(), enc
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace').splitlines(), 'latin-1'


def scan_board(inc_dir):
    """返回 (owners, unpaired, files_scanned)
    owners: {(port, bit): [(base, relpath, lineno), ...]}"""
    owners = {}
    ports_seen = {}
    pins_seen = {}
    files = sorted(f for f in os.listdir(inc_dir) if f.lower().endswith('.h'))
    for fn in files:
        full = os.path.join(inc_dir, fn)
        lines, _ = read_lines(full)
        rel = os.path.relpath(full, ROOT)
        for i, line in enumerate(lines, 1):
            m = RE_PORT.match(line)
            if m:
                ports_seen.setdefault(m.group(1), []).append((m.group(2)[4:], rel, i))
                continue
            m = RE_PIN.match(line)
            if m:
                bits = RE_BIT.findall(m.group(2))
                pins_seen.setdefault(m.group(1), []).append((bits, rel, i))
    # 配对
    for base, ports in ports_seen.items():
        port_list = [p[0] for p in ports]
        for bits, prel, pline in pins_seen.get(base, []):
            for port in port_list:
                for b in bits:
                    owners.setdefault((port, int(b)), []).append((base, ports[0][1] + ':%d' % ports[0][2], prel + ':%d' % pline))
    unpaired = sorted(set(ports_seen) ^ set(pins_seen))
    # 一个 base 多次定义（同板内重定义）也要看
    dup_base = sorted(b for b, v in ports_seen.items() if len(v) > 1)
    return owners, unpaired, dup_base, len(files)


def fmt_pin(port, bit):
    return 'P%s%d' % (port, bit)


def main():
    quiet = '--quiet' in sys.argv
    out = []
    summary = []
    all_pins = set((p, b) for p in PORTS for b in range(PINS_PER_PORT))

    for title, inc_dir in BOARDS:
        if not os.path.isdir(inc_dir):
            out.append('## %s\n\n!! 目录不存在：%s\n' % (title, inc_dir))
            continue
        owners, unpaired, dup_base, nfiles = scan_board(inc_dir)
        conflicts = sorted(((k, v) for k, v in owners.items() if len(v) > 1),
                           key=lambda kv: (kv[0][0], kv[0][1]))
        free = sorted(all_pins - set(owners), key=lambda pb: (pb[0], pb[1]))

        out.append('=' * 78)
        out.append('## %s' % title)
        out.append('   源：%s' % os.path.relpath(inc_dir, ROOT))
        out.append('   头文件 %d 个，被占用引脚 %d 个，其中冲突 %d 个，无人认领 %d 个'
                   % (nfiles, len(owners), len(conflicts), len(free)))
        out.append('=' * 78)
        out.append('')
        out.append('### 冲突引脚（同一脚被 >= 2 个模块声明）')
        out.append('')
        if conflicts:
            for (port, bit), lst in conflicts:
                out.append('- **%s**  ← %d 个占用者' % (fmt_pin(port, bit), len(lst)))
                for base, portat, pinat in lst:
                    out.append('    - `%s`   %s   %s' % (base, portat, pinat))
        else:
            out.append('（无）')
        out.append('')
        out.append('### 全部占用引脚（单占用也列出，便于人工对表）')
        out.append('')
        for (port, bit) in sorted(owners, key=lambda pb: (pb[0], pb[1])):
            lst = owners[(port, bit)]
            tag = '  [冲突]' if len(lst) > 1 else ''
            out.append('- %-4s %s%s' % (fmt_pin(port, bit), ', '.join(sorted(set(x[0] for x in lst))), tag))
        out.append('')
        out.append('### 无人认领引脚（库里没有任何模块声明；选空闲脚先看这里）')
        out.append('')
        line = []
        for port, bit in free:
            line.append(fmt_pin(port, bit))
        out.append('  ' + '  '.join(line))
        out.append('')
        if unpaired:
            out.append('### 宏不成对（只有 _PORT 或只有 _PIN，需人工确认）')
            out.append('')
            for b in unpaired:
                out.append('- `%s`' % b)
            out.append('')
        if dup_base:
            out.append('### 同板内重定义（同一基名的 _PORT 出现多次）')
            out.append('')
            for b in dup_base:
                out.append('- `%s`' % b)
            out.append('')
        summary.append((title, nfiles, len(owners), len(conflicts), len(free)))
        if not quiet:
            print('')
            print('## %s' % title)
            print('   冲突 %d 个：' % len(conflicts))
            for (port, bit), lst in conflicts:
                print('     %-5s %s' % (fmt_pin(port, bit), ', '.join(sorted(set(x[0] for x in lst)))))
            print('   无人认领 %d 个：%s' % (len(free), ' '.join(fmt_pin(p, b) for p, b in free)))

    dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), '_pin_audit.txt')
    io.open(dst, 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
    print('')
    print('已写入 %s' % dst)
    for t, nf, no, nc, nfree in summary:
        print('  %-24s 头文件 %2d  占用 %3d  冲突 %2d  空闲 %3d' % (t, nf, no, nc, nfree))
    return 0


if __name__ == '__main__':
    sys.exit(main())
