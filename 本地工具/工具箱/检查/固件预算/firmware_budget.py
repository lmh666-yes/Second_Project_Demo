# -*- coding: utf-8 -*-
r"""固件预算检查（幂等，只读或只产出新文件，绝不改动既有源码/固件）。

用途：把 OTA 方案的"分区上限"这件事，从"靠脑子记"变成"一键核"。
      三种输入，一种默认自检：

      1) --map <x.map>   解析 Keil 链接 map，取出 Code / RO / RW / ZI 与 Flash 合计，
                         对照分区上限（App 48KB / Boot 16KB）判越界、判"够但不宽裕"。
      2) --bin <x.bin>   算整镜像 CRC16-MODBUS、按 ota_part.h 的固件头布局生成 16 字节头，
                         拼成可直接下发的包（头 + 镜像）；--verify 反过来校验已有包。
      3) --hex <x.hex>   解析 Intel HEX 的地址范围，判它是否落在预期分区里（防"刷过界"）。
      4) 不给参数        只做分区表自检（Boot/App/下载区/参数区不重叠、下载区 ≥ App、
                         镜像上限 ≤ App 运行区）——适合挂进「全量检查」无输入地跑。

  分区常量不在本文件里写死：运行时会去读 OTA/OTA1/ota/ota_part.h（唯一事实来源），
  读不到才退回下面的默认值。所以改了分区只会改一处，这里自动跟着走。

报告写到脚本同目录的 固件预算报告.md，并在控制台打印摘要。

用法：
    python 本地工具\工具箱\检查\固件预算\firmware_budget.py
    python 本地工具\工具箱\检查\固件预算\firmware_budget.py --map OTA\OTA1\Objects\app.map --part app
    python 本地工具\工具箱\检查\固件预算\firmware_budget.py --bin OTA\OTA1\Objects\app.bin --ver 2 --out pkg.bin
    python 本地工具\工具箱\检查\固件预算\firmware_budget.py --verify pkg.bin
    python 本地工具\工具箱\检查\固件预算\firmware_budget.py --hex OTA\OTA1\Objects\app.hex --part app
    python 本地工具\工具箱\检查\固件预算\firmware_budget.py --selftest
"""
import json
import os
import re
import struct
import sys

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass


# ---------------- 常量 ----------------
# 默认值 = OTA/OTA1/ota/ota_part.h 现值（板1 STM32F407ZE）。
# 运行时若能读到那个头，就以头为准覆盖这里的值。
DEFAULT_PARTS = {
    'boot_addr': 0x08000000, 'boot_size': 0x00004000,
    'app_addr':  0x08004000, 'app_size':  0x0000C000,
    'dl_addr':   0x08010000, 'dl_size':   0x00010000,
    'param_a':   0x08070000, 'param_b':   0x08060000,
    'max_image': 0x0000C000,
    'hdr_size':  16,
}

MAGIC = b'OTA1'          # ota_part.h: 'O''T''A''1'（逐字节比较，不绑端序）
HDR_SIZE = 16            # 头长，4 字节对齐


def find_root(start):
    """向上找含 .git 或 EnvMonitor 的那层，作为仓库根。"""
    d = os.path.abspath(start)
    while True:
        if os.path.isdir(os.path.join(d, '.git')) or os.path.isdir(os.path.join(d, 'EnvMonitor')):
            return d
        p = os.path.dirname(d)
        if p == d:
            return None
        d = p


def crc16(data):
    """CRC16-MODBUS（初值 0xFFFF，多项式 0xA001 反向）。与工程 ota_crc16 / sys_frame 一致。
    自检向量：crc16(b'123456789') == 0x4B37。"""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def _parse_defines(text):
    vals = {}
    for m in re.finditer(r'^\s*#\s*define\s+(OTA_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+)', text, re.M):
        try:
            vals[m.group(1)] = int(m.group(2), 0)
        except ValueError:
            pass
    return vals


def load_parts(root):
    """以 ota_part.h 为唯一事实来源；读不到退回默认（保证没有仓库也能自检）。"""
    parts = dict(DEFAULT_PARTS)
    if root:
        path = os.path.join(root, 'OTA', 'OTA1', 'ota', 'ota_part.h')
        if os.path.exists(path):
            try:
                with open(path, 'r', encoding='utf-8', errors='replace') as f:
                    d = _parse_defines(f.read())
                mapping = {
                    'boot_addr': 'OTA_BOOT_ADDR', 'boot_size': 'OTA_BOOT_SIZE',
                    'app_addr':  'OTA_APP_ADDR',  'app_size':  'OTA_APP_SIZE',
                    'dl_addr':   'OTA_DL_ADDR',   'dl_size':   'OTA_DL_SIZE',
                    'param_a':   'OTA_PARAM_A_ADDR', 'param_b': 'OTA_PARAM_B_ADDR',
                    'max_image': 'OTA_MAX_IMAGE', 'hdr_size': 'OTA_HDR_SIZE',
                }
                for k, name in mapping.items():
                    if name in d:
                        parts[k] = d[name]
                parts['_source'] = os.path.relpath(path, root)
            except Exception:
                pass
    return parts


def resolve_input(path, root):
    """把用户给的输入路径落到真实文件上。三种写法都认：
       绝对路径 / 相对当前工作目录 / 相对仓库根（如 OTA/OTA1/Objects/app.map）。
       找不到就原样返回，交给调用方报"找不到文件"。"""
    for cand in (path,
                 os.path.abspath(path),
                 os.path.join(root, path) if root else None):
        if cand and os.path.isfile(cand):
            return cand
    return path


def part_range(p, which):
    """返回 (起始地址, 大小, 名称)。which ∈ app/boot/dl。"""
    if which == 'boot':
        return p['boot_addr'], p['boot_size'], 'Boot 区'
    if which == 'dl':
        return p['dl_addr'], p['dl_size'], '下载区'
    return p['app_addr'], p['app_size'], 'App 运行区'


def size_limit(p, which):
    if which == 'boot':
        return p['boot_size']
    if which == 'dl':
        return p['dl_size']
    return p['max_image']


# ---------------- 分区表自检 ----------------
def check_parts(p):
    issues = []
    if p['boot_addr'] + p['boot_size'] > p['app_addr']:
        issues.append('Boot 区（0x%08X+%d）越过了 App 起始地址 0x%08X'
                      % (p['boot_addr'], p['boot_size'], p['app_addr']))
    if p['app_addr'] + p['app_size'] > p['dl_addr']:
        issues.append('App 区（0x%08X+%d）越过了下载区起始地址 0x%08X'
                      % (p['app_addr'], p['app_size'], p['dl_addr']))
    if p['dl_addr'] + p['dl_size'] > min(p['param_a'], p['param_b']):
        issues.append('下载区越过了参数区（参数区最低 0x%08X）' % min(p['param_a'], p['param_b']))
    if p['dl_size'] < p['app_size']:
        issues.append('下载区（%dKB）小于 App 运行区（%dKB）：新固件装不下'
                      % (p['dl_size'] // 1024, p['app_size'] // 1024))
    if p['max_image'] > p['app_size']:
        issues.append('镜像上限（%dKB）大于 App 运行区（%dKB）'
                      % (p['max_image'] // 1024, p['app_size'] // 1024))
    return issues


# ---------------- Keil map ----------------
RE_PROG = re.compile(r'Program Size:\s*Code=(\d+)\s+RO-data=(\d+)\s+RW-data=(\d+)\s+ZI-data=(\d+)')
RE_RO   = re.compile(r'Total RO\s+Size \(Code \+ RO Data\)\s+(\d+)')
RE_ROM  = re.compile(r'Total ROM Size \(Code \+ RO Data \+ RW Data\)\s+(\d+)')
RE_RW   = re.compile(r'Total RW\s+Size \(RW Data \+ ZI Data\)\s+(\d+)')


def parse_map(text):
    """返回 dict。rom = Code + RO + RW（真正落到 Flash 的字节数）。"""
    res = {}
    m = RE_PROG.search(text)
    if m:
        res['code'], res['ro'], res['rw'], res['zi'] = (int(x) for x in m.groups())
    m2 = RE_RO.search(text)
    if m2:
        res['ro_total'] = int(m2.group(1))
    m3 = RE_ROM.search(text)
    if m3:
        res['rom'] = int(m3.group(1))
    m4 = RE_RW.search(text)
    if m4:
        res['rw_total'] = int(m4.group(1))
    if 'rom' not in res and all(k in res for k in ('code', 'ro', 'rw')):
        res['rom'] = res['code'] + res['ro'] + res['rw']
    return res


def check_map(path, p, which):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        res = parse_map(f.read())
    addr, size, label = part_range(p, which)
    limit = size_limit(p, which)
    issues, notes = [], []
    used = res.get('rom')
    if used is None:
        issues.append('map 里找不到体积信息（Program Size / Total ROM Size 都没有）')
    else:
        pct = used * 100.0 / limit
        res['pct'] = pct
        res['limit'] = limit
        if used > limit:
            issues.append('体积越界：%d 字节 > %s 上限 %d 字节（%dKB），超 %d 字节'
                          % (used, label, limit, limit // 1024, used - limit))
        elif used > limit * 0.9:
            notes.append('接近上限：%d / %d 字节（%.0f%%），留给后续功能的空间不多了'
                         % (used, limit, pct))
    res['label'] = label
    res['addr'] = addr
    return res, issues, notes


# ---------------- 固件包（头 + 镜像） ----------------
def build_header(size, img_crc, ver):
    h = bytearray(HDR_SIZE)
    h[0:4] = MAGIC
    h[4:8] = struct.pack('<I', size)          # 镜像字节数，小端 uint32
    h[8:10] = struct.pack('<H', img_crc & 0xFFFF)   # 整镜像 CRC16，小端
    h[10:12] = struct.pack('<H', ver & 0xFFFF)      # 版本号，小端
    h[12:14] = struct.pack('<H', crc16(bytes(h[0:12])))  # 头前 12 字节的 CRC16，小端
    h[14:16] = b'\xff\xff'                    # 保留
    return bytes(h)


def parse_header(hdr):
    if len(hdr) < HDR_SIZE:
        return None
    if bytes(hdr[0:4]) != MAGIC:
        return None
    return {
        'size': struct.unpack('<I', bytes(hdr[4:8]))[0],
        'crc':  struct.unpack('<H', bytes(hdr[8:10]))[0],
        'ver':  struct.unpack('<H', bytes(hdr[10:12]))[0],
        'hcrc': struct.unpack('<H', bytes(hdr[12:14]))[0],
        'hcrc_ok': crc16(bytes(hdr[0:12])) == struct.unpack('<H', bytes(hdr[12:14]))[0],
    }


def make_package(bin_path, ver, limit):
    with open(bin_path, 'rb') as f:
        data = f.read()
    size = len(data)
    img_crc = crc16(data)
    hdr = build_header(size, img_crc, ver)
    issues = []
    if size == 0:
        issues.append('固件是空的（0 字节）')
    if size > limit:
        issues.append('固件 %d 字节超过上限 %d 字节（板子会回 TOOBIG）' % (size, limit))
    return hdr + data, {'size': size, 'crc': img_crc, 'ver': ver, 'limit': limit}, issues


def verify_package(path, limit):
    with open(path, 'rb') as f:
        raw = f.read()
    issues = []
    if len(raw) < HDR_SIZE:
        return None, ['文件不足 %d 字节，连头都不完整' % HDR_SIZE]
    info = parse_header(raw[:HDR_SIZE])
    if info is None:
        return None, ['头 4 字节不是 "OTA1"：没有待安装固件 / 头被作废（写 0）/ 头不完整']
    if not info['hcrc_ok']:
        issues.append('头 CRC 不符：头前 12 字节被改过，或头只写了一半（掉电中断）')
    img = raw[HDR_SIZE:HDR_SIZE + info['size']]
    if len(img) < info['size']:
        issues.append('包内镜像长度不足：头声明 %d 字节，实际只有 %d 字节（文件被截断）'
                      % (info['size'], len(img)))
    else:
        got = crc16(img)
        if got != info['crc']:
            issues.append('整镜像 CRC 不符：头里 0x%04X，实际算得 0x%04X（镜像被改过）'
                          % (info['crc'], got))
    if info['size'] > limit:
        issues.append('镜像 %d 字节超过上限 %d 字节' % (info['size'], limit))
    info['limit'] = limit
    return info, issues


# ---------------- Intel HEX ----------------
def parse_hex(path):
    """返回 (min_addr, max_addr, data_records)。支持 type 00/02/04。"""
    min_a = None
    max_a = None
    n = 0
    base = 0
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line.startswith(':'):
                continue
            try:
                b = bytes.fromhex(line[1:])
            except ValueError:
                continue
            if len(b) < 5:
                continue
            cnt, addr, typ = b[0], (b[1] << 8) | b[2], b[3]
            data = b[4:4 + cnt]
            if typ == 0 and cnt > 0:
                s = base + addr
                e = s + cnt - 1
                min_a = s if min_a is None else min(min_a, s)
                max_a = e if max_a is None else max(max_a, e)
                n += 1
            elif typ == 4 and len(data) >= 2:
                base = ((data[0] << 8) | data[1]) << 16
            elif typ == 2 and len(data) >= 2:
                base = ((data[0] << 8) | data[1]) << 4
    return min_a, max_a, n


def check_hex(path, p, which):
    min_a, max_a, n = parse_hex(path)
    addr, size, label = part_range(p, which)
    end = addr + size
    issues = []
    if min_a is None:
        issues.append('hex 里没有数据记录（全是空/只有 EOF 行）')
    else:
        if min_a < addr:
            issues.append('起始 0x%08X 低于 %s 起点 0x%08X：刷下去会盖到前面（如 Boot）'
                          % (min_a, label, addr))
        if max_a >= end:
            issues.append('结束 0x%08X 越过 %s 末尾 0x%08X：会盖到下一个分区'
                          % (max_a, label, end))
    return {'min': min_a, 'max': max_a, 'n': n, 'label': label,
            'addr': addr, 'end': end}, issues


# ---------------- 报告 ----------------
def render(mode, p, payload):
    out = ['# 固件预算报告', '',
           '来源：`firmware_budget.py`（只读扫描 / 只产出新文件，可重复运行）', '']
    src = p.get('_source')
    out.append('## 0. 分区表')
    out.append('')
    out.append('| 分区 | 起始 | 大小 | 上限 |')
    out.append('|---|---|---|---|')
    out.append('| Boot | 0x%08X | %dKB | — |' % (p['boot_addr'], p['boot_size'] // 1024))
    out.append('| App 运行区 | 0x%08X | %dKB | %dKB |' % (p['app_addr'], p['app_size'] // 1024, p['max_image'] // 1024))
    out.append('| 下载区 | 0x%08X | %dKB | — |' % (p['dl_addr'], p['dl_size'] // 1024))
    out.append('')
    out.append('巡航区：参数区最低 0x%08X；分区常量来源 %s。'
               % (min(p['param_a'], p['param_b']),
                  ('`%s`' % src) if src else '**脚本内置默认值**（没读到 ota_part.h）'))
    out.append('')

    issues = payload.get('issues', [])
    notes = payload.get('notes', [])

    if mode == 'parts':
        out.append('## 1. 分区表自检')
        out.append('')
        out.append('四段互不重叠、下载区 ≥ App、镜像上限 ≤ App 运行区 —— **通过**。'
                   if not issues else '发现下面这些问题：')
        out.append('')
    elif mode == 'map':
        r = payload['res']
        out.append('## 1. Keil map 体积')
        out.append('')
        out.append('| 项 | 字节 |')
        out.append('|---|---|')
        for k, name in (('code', 'Code'), ('ro', 'RO-data'), ('rw', 'RW-data'), ('zi', 'ZI-data')):
            if k in r:
                out.append('| %s | %d |' % (name, r[k]))
        if 'rom' in r:
            out.append('| **Flash 合计（Code+RO+RW）** | **%d** |' % r['rom'])
        if 'rw_total' in r:
            out.append('| RAM 合计（RW+ZI） | %d |' % r['rw_total'])
        out.append('')
        if 'rom' in r:
            out.append('%s：上限 %d 字节，占用 %d 字节（%.1f%%）。'
                       % (r['label'], r['limit'], r['rom'], r.get('pct', 0)))
            out.append('')
    elif mode == 'bin':
        r = payload['res']
        out.append('## 1. 固件包')
        out.append('')
        out.append('- 输入固件：%d 字节，CRC16=0x%04X，版本=%d' % (r['size'], r['crc'], r['ver']))
        out.append('- 上限：%d 字节' % r['limit'])
        if payload.get('pkg'):
            out.append('- 输出包：`%s`（%d 字节 = 16 头 + %d 镜像）' % (payload['pkg'], r['size'] + HDR_SIZE, r['size']))
        out.append('')
    elif mode == 'verify':
        r = payload.get('res')
        out.append('## 1. 固件包校验')
        out.append('')
        if r:
            out.append('- 头：magic=OTA1，size=%d，CRC16=0x%04X，版本=%d，头CRC %s'
                       % (r['size'], r['crc'], r['ver'], 'ok' if r['hcrc_ok'] else '**不符**'))
            out.append('- 上限：%d 字节' % r['limit'])
        else:
            out.append('- 头解析失败（见问题）。')
        out.append('')
    elif mode == 'hex':
        r = payload['res']
        out.append('## 1. Intel HEX 地址范围')
        out.append('')
        if r['min'] is None:
            out.append('- 没有数据记录。')
        else:
            out.append('- 地址范围 0x%08X ~ 0x%08X（%d 条数据记录）' % (r['min'], r['max'], r['n']))
            out.append('- 期望落点：%s 0x%08X ~ 0x%08X' % (r['label'], r['addr'], r['end']))
        out.append('')

    out.append('## 2. 结论')
    out.append('')
    if issues:
        for i in issues:
            out.append('- **问题**：%s' % i)
    else:
        out.append('- 没有越界/损坏问题。')
    for i in notes:
        out.append('- **注意**：%s' % i)
    out.append('')
    out.append('## 3. 说明')
    out.append('')
    out.append('1) Flash 合计 = Code + RO-data + RW-data（RW 初值要随程序存进 Flash，掉电不丢）。')
    out.append('2) 固件头布局以 `OTA/OTA1/ota/ota_part.h` 为唯一事实来源；本脚本运行时读它，读不到才用内置默认值。')
    out.append('3) 头里 size/CRC/版本 一律小端；头前 12 字节的 CRC16 放在 [12..13]，[14..15] 保留 0xFFFF。')
    out.append('4) 判越界是硬问题（返回码 1）；"接近上限"只是注意，不影响返回码。')
    out.append('')
    return '\n'.join(out)


# ---------------- 自检 ----------------
def selftest():
    import shutil
    import tempfile

    checks = []

    def ck(name, ok, extra=''):
        checks.append((name, bool(ok), extra))

    # 1) CRC 算法向量（与 ota_crc16 / ota_send.py 同一个标准向量）
    ck('CRC16 标准向量 123456789 -> 0x4B37', crc16(b'123456789') == 0x4B37,
       '0x%04X' % crc16(b'123456789'))

    p = dict(DEFAULT_PARTS)

    tmp = tempfile.mkdtemp(prefix='fwb_selftest_')
    try:
        def wr(name, data, binary=True):
            path = os.path.join(tmp, name)
            if binary:
                with open(path, 'wb') as f:
                    f.write(data)
            else:
                with open(path, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(data)
            return path

        # ---- 分区表自检：默认值应当通过（反例：不误报） ----
        ck('分区表默认值无问题（反例）', len(check_parts(p)) == 0)

        # ---- 分区表自检：错例（下载区比 App 小）必须被命中 ----
        bad_p = dict(p)
        bad_p['dl_size'] = 0x8000   # 32KB < App 48KB
        ck('分区表错例：下载区<App 被命中', any('下载区' in i for i in check_parts(bad_p)))

        # ---- map：反例（在 48KB 以内不报越界） ----
        ok_map = wr('ok.map',
                    'Program Size: Code=24000 RO-data=1800 RW-data=64 ZI-data=1500\n'
                    '    Total RO  Size (Code + RO Data)                25800 (  25.20kB)\n'
                    '    Total RW  Size (RW Data + ZI Data)              1564 (   1.53kB)\n'
                    '    Total ROM Size (Code + RO Data + RW Data)      25864 (  25.26kB)\n',
                    binary=False)
        res, issues, notes = check_map(ok_map, p, 'app')
        ck('map 反例：25KB 不报越界', len(issues) == 0, '; '.join(issues))
        ck('map 解析出 Flash 合计 = 25864', res.get('rom') == 25864, str(res.get('rom')))

        # ---- map：错例（超 48KB 必须报越界） ----
        big_map = wr('big.map',
                     'Program Size: Code=45000 RO-data=5000 RW-data=100 ZI-data=2000\n'
                     '    Total ROM Size (Code + RO Data + RW Data)      50100 (  48.93kB)\n',
                     binary=False)
        res2, issues2, _ = check_map(big_map, p, 'app')
        ck('map 错例：50100 > 49152 被命中', any('越界' in i for i in issues2), '; '.join(issues2))

        # ---- bin：正常小固件 -> 头正确 + 反查校验通过（反例） ----
        fw = bytes(range(256)) * 4          # 1024 字节
        binp = wr('app.bin', fw)
        pkg, info, issues3 = make_package(binp, 2, p['max_image'])
        ck('bin 反例：1KB 固件不报问题', len(issues3) == 0, '; '.join(issues3))
        ck('生成的头 magic = OTA1', pkg[:4] == MAGIC)
        hd = parse_header(pkg[:HDR_SIZE])
        ck('头内 size/CRC 与镜像一致', hd and hd['size'] == len(fw) and hd['crc'] == crc16(fw))
        ck('头 CRC 自洽（hcrc_ok）', bool(hd and hd['hcrc_ok']))
        pkgp = wr('app.pkg', pkg)
        vinfo, vissues = verify_package(pkgp, p['max_image'])
        ck('校验反例：合法包无问题', len(vissues) == 0 and vinfo is not None, '; '.join(vissues))

        # ---- bin：错例（镜像被改一个字节 -> CRC 不符） ----
        tampered = bytearray(pkg)
        tampered[HDR_SIZE + 5] ^= 0xFF
        tp = wr('bad.pkg', bytes(tampered))
        _, tvissues = verify_package(tp, p['max_image'])
        ck('校验错例：改一个字节被命中 CRC', any('CRC 不符' in i for i in tvissues), '; '.join(tvissues))

        # ---- bin：错例（头被作废：magic 写 0） ----
        voided = bytearray(pkg)
        voided[0:4] = b'\x00\x00\x00\x00'
        vp = wr('void.pkg', bytes(voided))
        vinfo2, voidissues = verify_package(vp, p['max_image'])
        ck('校验错例：作废头被命中', vinfo2 is None and any('OTA1' in i for i in voidissues))

        # ---- bin：错例（超上限 -> TOOBIG） ----
        big_bin = wr('big.bin', b'\x00' * (p['max_image'] + 1))
        _, _, big_issues = make_package(big_bin, 1, p['max_image'])
        ck('bin 错例：超 48KB 被命中', any('超过上限' in i for i in big_issues), '; '.join(big_issues))

        # ---- hex：反例（落在 App 区不报） ----
        def hexrec(addr, typ, data):
            body = bytes([len(data), (addr >> 8) & 0xFF, addr & 0xFF, typ]) + data
            s = (-sum(body)) & 0xFF
            return ':' + (body + bytes([s])).hex().upper()
        # 先放一条扩展线性地址记录把基址抬到 0x0800_0000，再在 0x08004000 放 16 字节数据
        ok_hex = wr('ok.hex',
                    hexrec(0x0000, 4, b'\x08\x00') + '\n' +
                    hexrec(0x4000, 0, b'\xAA' * 16) + '\n' +
                    ':00000001FF\n', binary=False)
        r, hissues = check_hex(ok_hex, p, 'app')
        ck('hex 反例：落在 App 区不报越界', len(hissues) == 0, '; '.join(hissues))
        ck('hex 解析出起始 0x08004000', r['min'] == 0x08004000, hex(r['min'] or 0))

        # ---- hex：错例（起始低于 App 起点 -> 会盖 Boot） ----
        low_hex = wr('low.hex', hexrec(0x0000, 0, b'\x11' * 16) + '\n' + ':00000001FF\n', binary=False)
        _, lissues = check_hex(low_hex, p, 'app')
        ck('hex 错例：低于 App 起点被命中', any('低于' in i for i in lissues), '; '.join(lissues))

        # ---- hex：错例（越过 App 末尾） ----
        # 把并行地址抬到 0x0801（=0x08010000），即 App 末尾之外
        hi_hex = wr('hi.hex',
                    hexrec(0x0000, 4, b'\x08\x01') + '\n' +
                    hexrec(0x0000, 0, b'\x22' * 16) + '\n' + ':00000001FF\n', binary=False)
        _, hissues2 = check_hex(hi_hex, p, 'app')
        ck('hex 错例：越过 App 末尾被命中', any('越过' in i for i in hissues2), '; '.join(hissues2))

        # ---- 报告渲染不炸 ----
        ck('报告渲染不炸', '固件预算报告' in render('parts', p, {'issues': [], 'notes': []}))

        # ---- 输入路径解析：相对仓库根也能找到（工具箱页里就按这种写法给路径） ----
        os.makedirs(os.path.join(tmp, 'ota'), exist_ok=True)
        wr(os.path.join('ota', 'x.bin'), b'\x01\x02')
        ck('路径解析：相对仓库根可找到',
           os.path.normpath(resolve_input('ota/x.bin', tmp))
           == os.path.normpath(os.path.join(tmp, 'ota', 'x.bin')))
        ck('路径解析：找不到就原样返回（供上层报错）',
           resolve_input('ota/none.bin', tmp) == 'ota/none.bin')

        j = to_json('parts', load_parts(tmp), {'issues': [], 'notes': []})
        ck('JSON 结构正确（工具名/mode/parts）',
           j['tool'] == 'firmware_budget' and j['mode'] == 'parts'
           and j['ok'] is True and 'app_addr' in j['parts'])
        # B6 JSON 契约：面板读 issues 列表，名字/类型一改就红
        ck('JSON 契约：面板读取的字段名/类型都在',
           set(j) == {'tool', 'schema', 'ok', 'mode', 'parts', 'issues', 'notes', 'res'}
           and isinstance(j['issues'], list) and isinstance(j['notes'], list)
           and isinstance(j['parts'], dict),
           '、'.join(sorted(j)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    bad = [c for c in checks if not c[1]]
    for name, ok, extra in checks:
        print('  [%s] %s%s' % ('PASS' if ok else 'FAIL', name,
                               ('  （实际：%s）' % extra) if (extra and not ok) else ''))
    print('固件预算自检：%d 通过 / %d 失败' % (len(checks) - len(bad), len(bad)))
    return 1 if bad else 0


def to_json(mode, parts, payload):
    """结构化输出，供工具箱的可视化体检面板读取。"""
    return {
        'tool': 'firmware_budget',
        'schema': 1,
        'ok': not payload['issues'],
        'mode': mode,
        'parts': {k: v for k, v in parts.items()
                  if isinstance(v, (int, float, bool, str)) or v is None},
        'issues': payload['issues'],
        'notes': payload['notes'],
        'res': payload.get('res'),
    }


# ---------------- 入口 ----------------
USAGE = ('可用参数：--map <x.map> | --bin <x.bin> [--ver N] [--out <包文件>] | '
         '--verify <包文件> | --hex <x.hex> | --part app|boot|dl | '
         '--report <文件> | --quiet | --json | --selftest')


def main(argv):
    if '--selftest' in argv:
        return selftest()

    opts = {'map': None, 'bin': None, 'hex': None, 'verify': None, 'part': 'app',
            'ver': 1, 'out': None, 'report': None, 'quiet': False, 'json': False}
    bad = []
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in ('--map', '--bin', '--hex', '--verify', '--ver', '--out', '--report', '--part'):
            if i + 1 >= len(argv):
                bad.append('%s 缺少值' % a)
                i += 1
                continue
            v = argv[i + 1]
            i += 2
            if a == '--part':
                if v not in ('app', 'boot', 'dl'):
                    bad.append('--part 只支持 app|boot|dl')
                else:
                    opts['part'] = v
            elif a == '--ver':
                try:
                    opts['ver'] = int(v) & 0xFFFF
                except ValueError:
                    bad.append('--ver 需要整数')
            elif a == '--report':
                opts['report'] = os.path.abspath(v)
            else:
                opts[a[2:]] = v
        elif a == '--quiet':
            opts['quiet'] = True
            i += 1
        elif a == '--json':
            opts['json'] = True
            i += 1
        else:
            bad.append(a)
            i += 1

    if bad:
        print('未知/非法参数：%s' % ' '.join(bad))
        print(USAGE)
        return 2

    root = find_root(os.path.dirname(os.path.abspath(__file__)))
    p = load_parts(root)

    # 输入路径解析 + 存在性检查：给"绝对路径 / 相对当前目录 / 相对仓库根"三种写法都认，
    # 找不到就报清楚，别让它一路走到 open() 抛栈。
    for key in ('map', 'bin', 'hex', 'verify'):
        if opts[key]:
            opts[key] = resolve_input(opts[key], root)
            if not os.path.isfile(opts[key]):
                print('找不到输入文件：%s' % opts[key])
                print('提示：可给绝对路径，或相对仓库根的路径（如 OTA/OTA1/Objects/app.map）')
                return 2

    mode = 'parts'
    payload = {'issues': [], 'notes': []}

    if opts['verify']:
        mode = 'verify'
        info, issues = verify_package(opts['verify'], size_limit(p, opts['part']))
        payload['res'] = info
        payload['issues'] = issues
    elif opts['map']:
        mode = 'map'
        res, issues, notes = check_map(opts['map'], p, opts['part'])
        payload.update(res=res, issues=issues, notes=notes)
    elif opts['hex']:
        mode = 'hex'
        res, issues = check_hex(opts['hex'], p, opts['part'])
        payload.update(res=res, issues=issues)
    elif opts['bin']:
        mode = 'bin'
        pkg, info, issues = make_package(opts['bin'], opts['ver'], size_limit(p, opts['part']))
        out_path = opts['out'] or (opts['bin'] + '.otapkg')
        if not issues:
            with open(out_path, 'wb') as f:
                f.write(pkg)
            payload['pkg'] = out_path
        payload.update(res=info, issues=issues)
    else:
        payload['issues'] = check_parts(p)

    # --json：结构化输出给可视化面板，不写 md 报告
    if opts['json']:
        print(json.dumps(to_json(mode, p, payload), ensure_ascii=False))
        return 1 if payload['issues'] else 0

    report = render(mode, p, payload)
    report_path = opts['report'] or os.path.join(os.path.dirname(os.path.abspath(__file__)), '固件预算报告.md')
    with open(report_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(report)

    if not opts['quiet']:
        print('固件预算检查完成（模式：%s）' % mode)
        if mode == 'bin' and payload.get('pkg'):
            print('  输出包: %s' % payload['pkg'])
        if mode == 'map' and payload.get('res', {}).get('rom') is not None:
            r = payload['res']
            print('  Flash 占用: %d / %d 字节（%.1f%%）' % (r['rom'], r['limit'], r.get('pct', 0)))
        for i in payload['issues']:
            print('  问题: %s' % i)
        for i in payload['notes']:
            print('  注意: %s' % i)
        print('  报告: %s' % report_path)

    return 1 if payload['issues'] else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
