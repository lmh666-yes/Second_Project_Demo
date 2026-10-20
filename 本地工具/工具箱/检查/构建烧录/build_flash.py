# -*- coding: utf-8 -*-
r"""编译 → 烧录 → 板载自检（A2），附体积历史（A3）与状态快照（A5）。

用途：把"点 Keil 编译、插 ST-Link 下载、开串口看打印"这三步合成一条命令，
      并把结果落成结构化数据（供面板 / 阶段判定 / 报告复用）。

为什么需要它：手点三步的问题不是慢，而是**判据靠眼睛** ——
  · 编译过没过，要看最后一行是 0 Error 还是 63 Error；
  · 烧没烧进去，要看 Keil 的进度条；
  · 跑起来没有，要看串口里有没有那行 banner。
  三件事都能机器判，而且判完还能顺手记下体积趋势（A3）。

工具探测（按顺序，第一个能用的胜出）：
  1) 环境变量 KEIL_UV4 / STM32_PROGRAMMER_CLI（显式指定，最高优先级）
  2) 注册表 HKLM\SOFTWARE\WOW6432Node\Keil\Products\MDK 的 Path（→ 上一级/UV4/UV4.exe）
  3) 常见盘符下 */UV4/UV4.exe（深度受限扫描）
  **许可证探测**：本机装了两套 Keil，其中一套 `UV4 -b` 会报 C9555E（licence 检不出）。
  所以编译失败时若命中 C9555E，自动换下一个候选重试一次 —— 这不是容错花活，
  是实测踩到的（E:\k5 不通、E:\k5_v5 通）。

用法：
    python 本地工具\工具箱\检查\构建烧录\build_flash.py --detect
    python ...\build_flash.py --build --board 板1
    python ...\build_flash.py --all --board 板1 --com COM3
    python ...\build_flash.py --history
    python ...\build_flash.py --snapshot --board 板1
    python ...\build_flash.py --selftest        # 不需要硬件，验解析与判定
"""
import argparse
import ctypes
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass


# ========================================================================
# 仓库 / 板级配置
# ========================================================================

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
    """板级配置路径：TOOLBOX_BOARDCFG 优先（D3 的"加假工程"验收靠它），否则仓库里那份。"""
    return os.environ.get('TOOLBOX_BOARDCFG', '').strip() or cfg_file()

HISTORY_REL = os.path.join('本地工具', '工具箱', '检查', '构建烧录', '_体积历史.json')

# 板级配置缺失时的兜底（与 board_envmonitor.json 同内容；换板只改那份 JSON）
FALLBACK_BOARDS = [
    {'name': '板1 检测端 (F407ZE)', 'dir': 'EnvMonitor/Project_1_检测端/Project',
     'uvprojx': '标准模板.uvprojx', 'hex': 'Objects/标准模板.hex',
     'serial_banner': 'board1', 'serial_baud': 115200, 'flash_kb': 512, 'ram_kb': 192, 'com': ''},
    {'name': '板2 通信端 (F407ZG)', 'dir': 'EnvMonitor/Project_2_数据通信端/Project',
     'uvprojx': '000标准模板库.uvprojx', 'hex': 'Objects/000标准模板库.hex',
     'serial_banner': 'board2', 'serial_baud': 115200, 'flash_kb': 1024, 'ram_kb': 192, 'com': ''},
    {'name': 'OTA 工程 (F407ZE)', 'dir': 'OTA/OTA1',
     'uvprojx': '标准模板.uvprojx', 'hex': 'Objects/标准模板.hex',
     'serial_banner': 'ota', 'serial_baud': 115200, 'flash_kb': 512, 'ram_kb': 192, 'com': ''},
]


def load_boards(root):
    """读板级配置；读不到用兜底。返回 (boards, 来源说明)。"""
    p = os.path.join(root, CFG_REL)
    try:
        with open(p, encoding='utf-8') as f:
            d = json.load(f)
        bs = d.get('boards') or []
        if bs:
            return bs, p
    except Exception:
        pass
    return [dict(b) for b in FALLBACK_BOARDS], '(内置兜底)'


def pick_board(boards, want):
    """按名字模糊选板：'板1' 命中 '板1 检测端 (F407ZE)'；'all' 返回全部。"""
    if not want or want.lower() == 'all':
        return boards
    hits = [b for b in boards if want in b['name']]
    if not hits:
        hits = [b for b in boards if want.lower() in b['name'].lower()]
    return hits


# ========================================================================
# 工具探测
# ========================================================================

def _reg_keil_paths():
    r"""注册表里登记的 Keil 安装（MDK 的 Path 指向 <安装>\ARM）。"""
    out = []
    try:
        import winreg
    except Exception:
        return out
    keys = [
        (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Keil\Products\MDK'),
        (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Keil\Products\MDK'),
        (winreg.HKEY_CURRENT_USER, r'Software\Keil\Products\MDK'),
    ]
    for hive, sub in keys:
        try:
            with winreg.OpenKey(hive, sub) as k:
                path, _ = winreg.QueryValueEx(k, 'Path')
            if path:
                out.append(path)
        except Exception:
            continue
    return out


def keil_candidates():
    """按可信度排序的 UV4.exe 候选（去重、只留存在的）。"""
    cands = []
    env = os.environ.get('KEIL_UV4', '').strip()
    if env:
        cands.append(env)
    for arm in _reg_keil_paths():                  # <install>\ARM → <install>\UV4\UV4.exe
        cands.append(os.path.join(os.path.dirname(arm.rstrip('\\/')), 'UV4', 'UV4.exe'))
    # 常见盘符扫一层：<盘>\*/UV4/UV4.exe 与 <盘>\UV4/UV4.exe
    for drv in 'CDEFG':
        base = drv + ':\\'
        if not os.path.isdir(base):
            continue
        try:
            for name in os.listdir(base):
                if name.lower().startswith('keil'):
                    cands.append(os.path.join(base, name, 'UV4', 'UV4.exe'))
                else:
                    sub = os.path.join(base, name, 'UV4', 'UV4.exe')
                    if os.path.isfile(sub):
                        cands.append(sub)
        except OSError:
            continue
        cands.append(os.path.join(base, 'Keil_v5', 'UV4', 'UV4.exe'))
    seen, out = set(), []
    for c in cands:
        n = os.path.normcase(os.path.abspath(c))
        if n in seen:
            continue
        seen.add(n)
        if os.path.isfile(c):
            out.append(os.path.abspath(c))
    return out


def programmer_candidates():
    out = []
    env = os.environ.get('STM32_PROGRAMMER_CLI', '').strip()
    if env:
        out.append(env)
    for drv in 'CDEFG':
        for sub in ('stm32pro\\bin', 'STMicroelectronics\\STM32Cube\\STM32CubeProgrammer\\bin',
                    'Program Files\\STMicroelectronics\\STM32Cube\\STM32CubeProgrammer\\bin',
                    'Program Files (x86)\\STMicroelectronics\\STM32Cube\\STM32CubeProgrammer\\bin'):
            out.append(os.path.join(drv + ':\\', sub, 'STM32_Programmer_CLI.exe'))
    seen, res = set(), []
    for c in out:
        n = os.path.normcase(os.path.abspath(c))
        if n in seen:
            continue
        seen.add(n)
        if os.path.isfile(c):
            res.append(os.path.abspath(c))
    return res


def list_serial_ports():
    """注册表里的 COM 口（不依赖 pyserial）。返回 [(COM3, '描述'), ...]。"""
    out = []
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                            r'HARDWARE\DEVICEMAP\SERIALCOMM') as k:
            i = 0
            while True:
                try:
                    name, val, _ = winreg.EnumValue(k, i)
                except OSError:
                    break
                out.append((val, name))
                i += 1
    except Exception:
        pass
    return sorted(set(out))


# ========================================================================
# 编译输出解析
# ========================================================================

RE_SIZE = re.compile(
    r'Program Size:\s*Code=(\d+)\s+RO-data=(\d+)\s+RW-data=(\d+)\s+ZI-data=(\d+)')
RE_SUMMARY = re.compile(r'(\d+)\s+Error\(s\),\s*(\d+)\s+Warning\(s\)')
RE_ERROR_LINE = re.compile(r'^(.*?\.(?:c|h|s|S))\((\d+)\):\s*(error|warning):\s*(.*)$')
RE_LICENSE = re.compile(r'C9555E|Failed to check out a license')


def parse_program_size(text):
    """从编译日志里取体积。返回 dict 或 None。"""
    m = RE_SIZE.search(text)
    if not m:
        return None
    code, ro, rw, zi = (int(x) for x in m.groups())
    return {'code': code, 'ro': ro, 'rw': rw, 'zi': zi,
            'flash': code + ro + rw,      # 烧进 Flash 的：Code + RO + RW(初值)
            'ram': rw + zi}               # 运行占 RAM 的：RW + ZI


def parse_summary(text):
    """取最后一条 'N Error(s), M Warning(s)'。返回 (errors, warnings) 或 None。"""
    ms = RE_SUMMARY.findall(text)
    if not ms:
        return None
    e, w = ms[-1]
    return int(e), int(w)


def extract_diagnostics(text, limit=8):
    """把 'xxx.c(12): error: ...' 抽出来（编译错误/警告行）。"""
    out = []
    for line in text.splitlines():
        m = RE_ERROR_LINE.match(line.strip())
        if m:
            out.append({'file': m.group(1), 'line': int(m.group(2)),
                        'kind': m.group(3), 'msg': m.group(4).strip()})
            if len(out) >= limit:
                break
    return out


def classify_build(rc, text):
    """把 UV4 的退出码 + 日志翻成一句人话。rc: 0=OK 1=警告 2=错误 3=致命
    11=打不开工程 12=器件 13=写 15=读 20=许可证。"""
    if RE_LICENSE.search(text):
        return 'license'
    if rc in (0, 1):
        return 'ok'
    if rc == 2:
        return 'errors'
    if rc == 3:
        return 'fatal'
    if rc == 11:
        return 'no_project'
    if rc == 20:
        return 'license'
    return 'unknown'


def read_text_any(path):
    """日志可能是 GBK 也可能是 UTF-8 —— 两种都试，取能解出中文的那个。"""
    try:
        with open(path, 'rb') as f:
            raw = f.read()
    except OSError:
        return ''
    for enc in ('utf-8', 'gbk'):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace')


# ========================================================================
# 编译
# ========================================================================

def build_one(root, board, uv4, quiet=False, rebuild=False):
    """编译一块板。返回结果 dict（不抛异常）。

    rebuild=False 走增量（`-b`，快）；但增量且无改动时 UV4 **不重新链接**，
    日志里就没有 `Program Size:` 行 —— 那会让"体积"这一项空掉（踩过）。
    所以调用方可以在缺体积时用 rebuild=True 再跑一次（`-r` 全量重链，约 7 秒）。
    """
    proj_dir = os.path.join(root, board['dir'])
    proj = os.path.join(proj_dir, board.get('uvprojx') or '')
    res = {'board': board['name'], 'ok': False, 'rc': None, 'kind': '',
           'size': None, 'errors': 0, 'warnings': 0, 'diags': [],
           'uv4': uv4, 'project': proj, 'log': '', 'seconds': 0.0,
           'rebuild': bool(rebuild)}
    if not os.path.isfile(proj):
        res['kind'] = 'no_project'
        res['log'] = '找不到工程文件：%s' % proj
        return res

    fd, logpath = tempfile.mkstemp(prefix='uv4_', suffix='.log')
    os.close(fd)
    t0 = time.time()
    flag = '-r' if rebuild else '-b'
    try:
        p = subprocess.run([uv4, flag, proj, '-j0', '-o', logpath],
                           cwd=proj_dir, capture_output=True, text=True,
                           encoding='utf-8', errors='replace', timeout=600)
        res['rc'] = p.returncode
    except subprocess.TimeoutExpired:
        res['kind'] = 'timeout'
        res['log'] = '编译超时（>600s）'
        return res
    except OSError as e:
        res['kind'] = 'no_uv4'
        res['log'] = '启动 UV4 失败：%s' % e
        return res
    res['seconds'] = round(time.time() - t0, 2)

    text = read_text_any(logpath)
    try:
        os.remove(logpath)
    except OSError:
        pass
    res['log'] = text
    res['kind'] = classify_build(res['rc'], text)
    sm = parse_summary(text)
    if sm:
        res['errors'], res['warnings'] = sm
    res['size'] = parse_program_size(text)
    res['diags'] = extract_diagnostics(text)
    # 体积只是"顺带记"的信息，不该决定成败 —— 判据是退出码 + 错误数
    res['ok'] = (res['kind'] == 'ok' and res['errors'] == 0)
    return res


def build_with_fallback(root, board, quiet=False):
    """依次试 UV4 候选：许可证失败就换下一个（本机 E:\\k5 不通、E:\\k5_v5 通）。
    编译通过但没拿到体积时，再全量重链一次补上体积。"""
    cands = keil_candidates()
    if not cands:
        return {'board': board['name'], 'ok': False, 'kind': 'no_uv4',
                'log': '没找到 UV4.exe（可用环境变量 KEIL_UV4 指定）', 'tried': []}
    tried = []
    for uv4 in cands:
        r = build_one(root, board, uv4)
        tried.append({'uv4': uv4, 'rc': r.get('rc'), 'kind': r.get('kind')})
        if r.get('kind') != 'license':
            if r.get('ok') and not r.get('size'):
                if not quiet:
                    print('  · 增量编译没重链接，补一次全量以取体积…')
                r2 = build_one(root, board, uv4, quiet, rebuild=True)
                r2['tried'] = tried
                return r2
            r['tried'] = tried
            return r
        if not quiet:
            print('  · %s 许可证不通，换下一个 UV4' % uv4)
    last = dict(r)
    last['tried'] = tried
    last['log'] = '所有 UV4 候选都因许可证失败（C9555E）。\n' + last.get('log', '')
    return last


# ========================================================================
# 烧录
# ========================================================================

def flash_one(root, board, cli, com=''):
    """烧录 hex。返回结果 dict。"""
    hexf = os.path.join(root, board['dir'], board.get('hex') or '')
    res = {'board': board['name'], 'ok': False, 'hex': hexf, 'cli': cli,
           'cmd': '', 'log': '', 'kind': ''}
    if not os.path.isfile(hexf):
        res['kind'] = 'no_hex'
        res['log'] = 'hex 不存在（先编译）：%s' % hexf
        return res
    cmd = [cli, '-c', 'port=SWD', '-w', hexf, '-v', '-rst']
    res['cmd'] = ' '.join('"%s"' % c if ' ' in c else c for c in cmd)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True,
                           encoding='utf-8', errors='replace', timeout=180)
    except subprocess.TimeoutExpired:
        res['kind'] = 'timeout'
        res['log'] = '烧录超时（>180s）'
        return res
    except OSError as e:
        res['kind'] = 'no_cli'
        res['log'] = '启动 STM32_Programmer_CLI 失败：%s' % e
        return res
    out = (p.stdout or '') + (p.stderr or '')
    res['log'] = out
    low = out.lower()
    res['ok'] = (p.returncode == 0) and ('file download complete' in low
                                         or 'download verified' in low
                                         or 'success' in low)
    if not res['ok']:
        if 'no stlink' in low or 'no device' in low or 'error' in low:
            res['kind'] = 'no_target'
        else:
            res['kind'] = 'failed'
    return res


# ========================================================================
# 串口（ctypes 直调 Win32，不依赖 pyserial）
# ========================================================================

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3


class _DCB(ctypes.Structure):
    _fields_ = [
        ('DCBlength', ctypes.c_uint32), ('BaudRate', ctypes.c_uint32),
        ('fBinary', ctypes.c_uint32, 1), ('fParity', ctypes.c_uint32, 1),
        ('fOutxCtsFlow', ctypes.c_uint32, 1), ('fOutxDsrFlow', ctypes.c_uint32, 1),
        ('fDtrControl', ctypes.c_uint32, 2), ('fDsrSensitivity', ctypes.c_uint32, 1),
        ('fTXContinueOnXoff', ctypes.c_uint32, 1), ('fOutX', ctypes.c_uint32, 1),
        ('fInX', ctypes.c_uint32, 1), ('fErrorChar', ctypes.c_uint32, 1),
        ('fNull', ctypes.c_uint32, 1), ('fRtsControl', ctypes.c_uint32, 2),
        ('fAbortOnError', ctypes.c_uint32, 1), ('fDummy2', ctypes.c_uint32, 17),
        ('wReserved', ctypes.c_uint16), ('XonLim', ctypes.c_uint16),
        ('XoffLim', ctypes.c_uint16), ('ByteSize', ctypes.c_uint8),
        ('Parity', ctypes.c_uint8), ('StopBits', ctypes.c_uint8),
        ('XonChar', ctypes.c_char), ('XoffChar', ctypes.c_char),
        ('ErrorChar', ctypes.c_char), ('EofChar', ctypes.c_char),
        ('EvtChar', ctypes.c_char), ('wReserved1', ctypes.c_uint16),
    ]


class _TIMEOUTS(ctypes.Structure):
    _fields_ = [('ReadIntervalTimeout', ctypes.c_uint32),
                ('ReadTotalTimeoutMultiplier', ctypes.c_uint32),
                ('ReadTotalTimeoutConstant', ctypes.c_uint32),
                ('WriteTotalTimeoutMultiplier', ctypes.c_uint32),
                ('WriteTotalTimeoutConstant', ctypes.c_uint32)]


def read_serial(port, baud=115200, seconds=6.0):
    """开串口读 N 秒，返回 (文本, 错误说明)。不依赖 pyserial。"""
    if os.name != 'nt':
        return '', '仅支持 Windows'
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.CreateFileW.restype = ctypes.c_void_p
    handle = k32.CreateFileW(ctypes.c_wchar_p('\\\\.\\' + port),
                             GENERIC_READ | GENERIC_WRITE, 0, None,
                             OPEN_EXISTING, 0, None)
    if handle in (None, ctypes.c_void_p(-1).value):
        return '', '打不开 %s（被占用或不存在）' % port
    try:
        dcb = _DCB()
        dcb.DCBlength = ctypes.sizeof(_DCB)
        if not k32.GetCommState(ctypes.c_void_p(handle), ctypes.byref(dcb)):
            return '', 'GetCommState 失败'
        dcb.BaudRate = int(baud)
        dcb.ByteSize = 8
        dcb.Parity = 0          # NOPARITY
        dcb.StopBits = 0        # ONESTOPBIT
        dcb.fBinary = 1
        dcb.fParity = 0
        if not k32.SetCommState(ctypes.c_void_p(handle), ctypes.byref(dcb)):
            return '', 'SetCommState 失败（波特率 %s 不支持？）' % baud
        to = _TIMEOUTS(50, 0, 200, 0, 0)
        k32.SetCommTimeouts(ctypes.c_void_p(handle), ctypes.byref(to))
        k32.PurgeComm(ctypes.c_void_p(handle), 0x0004 | 0x0008)   # RX/TX 清空
        buf = ctypes.create_string_buffer(1024)
        n = ctypes.c_uint32(0)
        got = bytearray()
        deadline = time.time() + seconds
        while time.time() < deadline:
            if k32.ReadFile(ctypes.c_void_p(handle), buf, 1024,
                            ctypes.byref(n), None):
                if n.value:
                    got += buf.raw[:n.value]
            time.sleep(0.03)
        return decode_bytes(bytes(got)), ''
    finally:
        k32.CloseHandle(ctypes.c_void_p(handle))


def decode_bytes(raw):
    for enc in ('utf-8', 'gbk'):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace')


def serial_probe(port, baud, banner, seconds=6.0):
    """读串口并按 banner 判 PASS/FAIL。banner 为空时只要收到非空就算过。"""
    text, err = read_serial(port, baud, seconds)
    res = {'port': port, 'baud': baud, 'banner': banner, 'text': text,
           'error': err, 'ok': False}
    if err:
        return res
    if not text.strip():
        res['error'] = '%s 秒内没收到任何字节（板子没复位？波特率不对？）' % seconds
        return res
    res['ok'] = (banner in text) if banner else True
    if not res['ok']:
        res['error'] = '收到 %d 字节但没有「%s」' % (len(text), banner)
    return res


# ========================================================================
# 体积历史（A3）
# ========================================================================

def history_path(root):
    return os.path.join(root, HISTORY_REL)


def load_history(root):
    try:
        with open(history_path(root), encoding='utf-8') as f:
            d = json.load(f)
        return d if isinstance(d, list) else []
    except Exception:
        return []


def save_history(root, hist, keep=200):
    p = history_path(root)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    hist = hist[-keep:]
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(hist, f, ensure_ascii=False, indent=1)
        f.write('\n')


def record_history(root, board, build_res):
    """把一次编译记进历史（同一板同一时刻只记一条）。返回本次记录。"""
    if not build_res.get('size'):
        return None
    s = build_res['size']
    rec = {'time': datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
           'board': board['name'],
           'code': s['code'], 'ro': s['ro'], 'rw': s['rw'], 'zi': s['zi'],
           'flash': s['flash'], 'ram': s['ram'],
           'flash_kb': board.get('flash_kb'), 'ram_kb': board.get('ram_kb'),
           'errors': build_res.get('errors', 0)}
    hist = load_history(root)
    hist.append(rec)
    save_history(root, hist)
    return rec


def _board_trend(hist):
    """同一块板的历史 → 趋势摘要。hist 必须已按板过滤。"""
    if not hist:
        return None
    last = hist[-1]
    prev = hist[-2] if len(hist) > 1 else None

    def pct(v, kb):
        return round(100.0 * v / (kb * 1024), 1) if kb else None

    out = {'board': last['board'], 'points': len(hist), 'last': last,
           'flash_pct': pct(last['flash'], last.get('flash_kb')),
           'ram_pct': pct(last['ram'], last.get('ram_kb'))}
    if prev:
        out['d_flash'] = last['flash'] - prev['flash']
        out['d_ram'] = last['ram'] - prev['ram']
    warns = []
    if out.get('flash_pct') and out['flash_pct'] > 90:
        warns.append('Flash 占用 %.1f%%（>90%%）' % out['flash_pct'])
    if out.get('ram_pct') and out['ram_pct'] > 90:
        warns.append('RAM 占用 %.1f%%（>90%%）' % out['ram_pct'])
    out['warnings'] = warns
    return out


def history_report(root, board_name=None):
    """算趋势。指定板号 → 只算这块板；不指定 → 按板分别算（**绝不跨板比体积**，
    板1 512KB / 板2 1024KB，混在一起算出来的"较上次 +2216B"是假的）。"""
    hist = load_history(root)
    if not hist:
        return {'ok': True, 'points': 0, 'boards': [],
                'text': '还没有编译记录（先跑一次 --build）'}
    if board_name:
        rows = [h for h in hist if board_name in h.get('board', '')]
        t = _board_trend(rows)
        if not t:
            return {'ok': True, 'points': 0, 'boards': [],
                    'text': '这块板还没有编译记录'}
        t['ok'] = True
        return t
    names = []
    for h in hist:
        if h.get('board') not in names:
            names.append(h.get('board'))
    boards = []
    for n in names:
        t = _board_trend([h for h in hist if h.get('board') == n])
        if t:
            boards.append(t)
    return {'ok': True, 'points': len(hist), 'boards': boards}


# ========================================================================
# 快照（A5）
# ========================================================================

def make_snapshot(root, results, extra_files=()):
    """把本次结果打包成 状态_YYYYmmdd_HHMMSS/。返回目录。"""
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    d = os.path.join(root, '本地工具', '工具箱', '检查', '构建烧录', '状态_%s' % stamp)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, '结果.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(results, f, ensure_ascii=False, indent=1)
        f.write('\n')
    with open(os.path.join(d, '结果.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(render_md(results))
    for src in extra_files:
        try:
            if os.path.isfile(src):
                shutil.copy2(src, d)
        except OSError:
            pass
    return d


def render_md(results):
    L = ['# 板子状态快照', '', '生成时间：%s' % results.get('time', ''), '']
    for b in results.get('boards', []):
        L.append('## %s' % b['board'])
        bd = b.get('build') or {}
        if bd:
            L.append('- 编译：%s（%s 个错误 / %s 个警告，%.2fs）'
                     % ('通过' if bd.get('ok') else '失败',
                        bd.get('errors'), bd.get('warnings'), bd.get('seconds') or 0))
            s = bd.get('size')
            if s:
                L.append('- 体积：Flash %d B（Code %d + RO %d + RW %d）／RAM %d B（RW %d + ZI %d）'
                         % (s['flash'], s['code'], s['ro'], s['rw'], s['ram'], s['rw'], s['zi']))
            for d in (bd.get('diags') or [])[:5]:
                L.append('  - `%s(%d): %s: %s`' % (d['file'], d['line'], d['kind'], d['msg']))
        fl = b.get('flash') or {}
        if fl:
            L.append('- 烧录：%s' % ('通过' if fl.get('ok') else '失败（%s）' % fl.get('kind')))
        se = b.get('serial') or {}
        if se:
            L.append('- 串口自检：%s' % ('通过（收到 %s）' % se.get('banner')
                                       if se.get('ok') else '失败（%s）' % se.get('error')))
            if se.get('text'):
                L.append('  ```')
                for line in se['text'].splitlines()[:12]:
                    L.append('  ' + line)
                L.append('  ```')
        L.append('')
    return '\n'.join(L)


# ========================================================================
# 主流程
# ========================================================================

def run_detect(root):
    boards, src = load_boards(root)
    uv4 = keil_candidates()
    cli = programmer_candidates()
    ports = list_serial_ports()
    return {'ok': bool(uv4), 'boards': [b['name'] for b in boards],
            'config': src, 'uv4': uv4, 'programmer': cli, 'ports': ports}


def do_build(root, boards, quiet=False):
    out = []
    for b in boards:
        if not quiet:
            print('编译 %s ...' % b['name'])
        r = build_with_fallback(root, b, quiet)
        if r.get('size'):
            record_history(root, b, r)
        out.append({'board': b['name'], 'build': r})
        if not quiet:
            s = r.get('size') or {}
            print('  → %s（%s 错 / %s 警）%s' % (
                '通过' if r.get('ok') else '失败',
                r.get('errors'), r.get('warnings'),
                '  Flash=%dB RAM=%dB' % (s['flash'], s['ram']) if s else ''))
            for d in (r.get('diags') or [])[:5]:
                print('    %s(%d): %s: %s' % (d['file'], d['line'], d['kind'], d['msg']))
            if not r.get('ok') and not r.get('diags'):
                tail = (r.get('log') or '').strip().splitlines()[-6:]
                for line in tail:
                    print('    ' + line)
    return out


def do_flash(root, boards, quiet=False):
    cli = programmer_candidates()
    if not cli:
        return [{'board': b['name'], 'flash': {
            'ok': False, 'kind': 'no_cli',
            'log': '没找到 STM32_Programmer_CLI.exe（可用环境变量 STM32_PROGRAMMER_CLI 指定）'}}
            for b in boards]
    out = []
    for b in boards:
        if not quiet:
            print('烧录 %s ...' % b['name'])
        r = flash_one(root, b, cli[0])
        out.append({'board': b['name'], 'flash': r})
        if not quiet:
            print('  → %s' % ('通过' if r['ok'] else '失败：%s' % (r['kind'] or r['log'][:120])))
    return out


def do_serial(root, boards, com, quiet=False, seconds=6.0):
    out = []
    for b in boards:
        port = com or b.get('com') or ''
        if not port:
            out.append({'board': b['name'], 'serial': {
                'ok': False, 'error': '没指定串口（--com COM3，或在板级配置里填 com）'}})
            if not quiet:
                print('串口 %s：跳过（没指定 --com）' % b['name'])
            continue
        if not quiet:
            print('串口自检 %s @ %s ...' % (b['name'], port))
        r = serial_probe(port, b.get('serial_baud') or 115200,
                         b.get('serial_banner') or '', seconds)
        out.append({'board': b['name'], 'serial': r})
        if not quiet:
            print('  → %s' % ('通过' if r['ok'] else '失败：%s' % r['error']))
    return out


def merge(parts):
    """把 [{'board':X,'build':...}, {'board':X,'flash':...}] 合成按板一行。"""
    order, acc = [], {}
    for p in parts:
        n = p['board']
        if n not in acc:
            acc[n] = {'board': n}
            order.append(n)
        acc[n].update({k: v for k, v in p.items() if k != 'board'})
    return [acc[n] for n in order]


# ========================================================================
# 自检（不需要硬件）
# ========================================================================

def selftest():
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), extra))
        if not cond:
            ok_all = False

    log_ok = '''compiling main.c...
linking...
Program Size: Code=11140 RO-data=1164 RW-data=188 ZI-data=4028
FromELF: creating hex file...
".\\Objects\\标准模板.axf" - 0 Error(s), 0 Warning(s).
Build Time Elapsed:  00:00:07
'''
    s = parse_program_size(log_ok)
    ck('解析 Program Size（Code/RO/RW/ZI）',
       s == {'code': 11140, 'ro': 1164, 'rw': 188, 'zi': 4028,
             'flash': 12492, 'ram': 4216}, str(s))
    ck('Flash = Code+RO+RW、RAM = RW+ZI',
       s and s['flash'] == 11140 + 1164 + 188 and s['ram'] == 188 + 4028)
    ck('解析 0 Error / 0 Warning', parse_summary(log_ok) == (0, 0),
       str(parse_summary(log_ok)))
    ck('编译通过判定', classify_build(0, log_ok) == 'ok')

    log_err = '''..\\..\\FWLIB\\src\\sys_usart.c(120): error:  #20: identifier "FOO" is undefined
..\\main.c(33): warning:  #177-D: variable "x" was declared but never referenced
".\\Objects\\标准模板.axf" - 1 Error(s), 1 Warning(s).
'''
    d = extract_diagnostics(log_err)
    ck('抓到 error 行（文件+行号+消息）',
       d and d[0]['file'].endswith('sys_usart.c') and d[0]['line'] == 120
       and 'identifier' in d[0]['msg'], str(d[:1]))
    ck('error 与 warning 都抓', len(d) == 2 and {x['kind'] for x in d} == {'error', 'warning'})
    ck('解析 1 Error / 1 Warning', parse_summary(log_err) == (1, 1))
    ck('编译失败判定（rc=2）', classify_build(2, log_err) == 'errors')

    log_lic = '''Error: C9555E: Failed to check out a license.
".\\Objects\\x.axf" - 63 Error(s), 0 Warning(s).
'''
    ck('许可证失败单独分类（C9555E）', classify_build(2, log_lic) == 'license')
    ck('许可证日志能被认出来（换 UV4 的依据）', bool(RE_LICENSE.search(log_lic)))

    # 增量编译且无改动：UV4 不重链接 → 没有 Program Size 行。这不能算失败（踩过）。
    log_inc = '''".\\Objects\\标准模板.axf" - 0 Error(s), 0 Warning(s).
Build Time Elapsed:  00:00:01
'''
    ck('增量编译无体积时：体积为 None 但编译判通过',
       parse_program_size(log_inc) is None and classify_build(0, log_inc) == 'ok'
       and parse_summary(log_inc) == (0, 0))

    # 体积趋势：两次记录 → 增量算得对
    tmp = tempfile.mkdtemp(prefix='bf_hist_')
    try:
        global HISTORY_REL
        old_rel, HISTORY_REL = HISTORY_REL, os.path.join(tmp, 'h.json')
        b = {'name': '板X', 'flash_kb': 512, 'ram_kb': 192}
        record_history(tmp, b, {'size': {'code': 1000, 'ro': 100, 'rw': 10, 'zi': 200,
                                         'flash': 1110, 'ram': 210}, 'errors': 0})
        record_history(tmp, b, {'size': {'code': 3000, 'ro': 100, 'rw': 10, 'zi': 200,
                                         'flash': 3110, 'ram': 210}, 'errors': 0})
        rep = history_report(tmp, '板X')
        ck('体积历史：两点', rep['points'] == 2, str(rep.get('points')))
        ck('体积历史：增量 = 最后一次 − 上一次', rep.get('d_flash') == 2000, str(rep.get('d_flash')))
        ck('体积历史：占用率算对（3110/512KB）',
           abs((rep.get('flash_pct') or 0) - 0.59) < 0.05, str(rep.get('flash_pct')))
        # 不指定板号时**按板分开**算：板1 512KB、板2 1024KB 混在一起算增量是假的
        record_history(tmp, {'name': '板Y', 'flash_kb': 1024, 'ram_kb': 192},
                       {'size': {'code': 20000, 'ro': 0, 'rw': 0, 'zi': 0,
                                 'flash': 20000, 'ram': 0}, 'errors': 0})
        allrep = history_report(tmp, '')
        ck('体积历史：不指定板号时按板分组（2 块）', len(allrep['boards']) == 2,
           str([b['board'] for b in allrep.get('boards', [])]))
        ck('体积历史：跨板不算增量（板Y 只有一条，不给 d_flash）',
           'd_flash' not in [b for b in allrep['boards'] if b['board'] == '板Y'][0],
           str(allrep['boards']))
        HISTORY_REL = old_rel
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # 串口解码：GBK / UTF-8 都能出中文，纯 ASCII banner 能找到
    ck('串口解码 GBK', decode_bytes('[板1] 启动'.encode('gbk')) == '[板1] 启动')
    ck('串口解码 UTF-8', decode_bytes('[板1] 启动'.encode('utf-8')) == '[板1] 启动')
    ck('banner 判定（收到才算过）', 'board1' in decode_bytes(b'x\r\n[board1] ready\r\n'))

    # 板级配置：字段齐全（缺 uvprojx/hex/banner 就没法编译烧录）
    boards, src = load_boards(ROOT)
    ck('板级配置读到 %d 块板' % len(boards), len(boards) >= 2, src)
    need = ('dir', 'uvprojx', 'hex', 'serial_banner')
    miss = [(b['name'], k) for b in boards for k in need if not b.get(k)]
    ck('每块板都有 dir/uvprojx/hex/serial_banner', not miss, str(miss))
    # 工程文件真在（配置写错的话这里就红）
    bad = [b['name'] for b in boards
           if not os.path.isfile(os.path.join(ROOT, b['dir'], b['uvprojx']))]
    ck('配置里的 .uvprojx 都真实存在', not bad, str(bad))
    # 选择器：'板1' 只命中一块；'all' 返回全部
    ck('选板：模糊匹配「板1」', len(pick_board(boards, '板1')) == 1)
    ck('选板：all 返回全部', len(pick_board(boards, 'all')) == len(boards))

    # 工具探测：候选去重、路径都存在
    c = keil_candidates()
    ck('UV4 候选都真实存在', all(os.path.isfile(x) for x in c), str(c[:3]))
    ck('UV4 候选无重复', len(c) == len({os.path.normcase(x) for x in c}))

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('构建烧录自检：%d 通过 / %d 失败' % (sum(1 for _, c2, _ in checks if c2),
                                              sum(1 for _, c2, _ in checks if not c2)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='编译 → 烧录 → 板载自检')
    ap.add_argument('--detect', action='store_true', help='探测工具链与串口')
    ap.add_argument('--build', action='store_true', help='编译')
    ap.add_argument('--flash', action='store_true', help='烧录')
    ap.add_argument('--serial', action='store_true', help='串口自检')
    ap.add_argument('--all', action='store_true', help='编译+烧录+串口')
    ap.add_argument('--history', action='store_true', help='体积趋势')
    ap.add_argument('--snapshot', action='store_true', help='把结果打包成状态快照')
    ap.add_argument('--board', default='all', help='板1 / 板2 / OTA / all')
    ap.add_argument('--com', default='', help='串口，如 COM3')
    ap.add_argument('--seconds', type=float, default=6.0, help='串口收多久')
    ap.add_argument('--json', action='store_true', help='输出 JSON')
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    root = ROOT
    boards_all, cfg_src = load_boards(root)

    if a.detect:
        d = run_detect(root)
        if a.json:
            print(json.dumps(d, ensure_ascii=False))
        else:
            print('仓库根：%s' % root)
            print('板级配置：%s' % d['config'])
            print('板子：%s' % '、'.join(d['boards']))
            print('UV4 候选：')
            for x in d['uv4']:
                print('  ' + x)
            if not d['uv4']:
                print('  （没有；可用环境变量 KEIL_UV4 指定）')
            print('烧录器：')
            for x in d['programmer'] or ['（没有；可用环境变量 STM32_PROGRAMMER_CLI 指定）']:
                print('  ' + x)
            print('串口：%s' % ('、'.join('%s(%s)' % (p, n) for p, n in d['ports']) or '（无）'))
        return

    boards = pick_board(boards_all, a.board)
    if not boards:
        print('没有匹配的板：%s（可选：%s）' % (a.board, '、'.join(b['name'] for b in boards_all)))
        sys.exit(1)

    if a.history:
        rep = history_report(root, '' if a.board == 'all' else a.board)
        if a.json:
            print(json.dumps(rep, ensure_ascii=False))
        else:
            rows = rep.get('boards') or ([rep] if rep.get('last') else [])
            if not rows:
                print(rep.get('text') or '还没有编译记录')
            for t in rows:
                l = t['last']
                print('%s　记录 %d 次' % (l['board'], t['points']))
                print('  最近：%s' % l['time'])
                print('  Flash %d B（%.1f%% of %sKB）　RAM %d B（%.1f%% of %sKB）'
                      % (l['flash'], t['flash_pct'] or 0, l['flash_kb'],
                         l['ram'], t['ram_pct'] or 0, l['ram_kb']))
                if 'd_flash' in t:
                    print('  较上次：Flash %+d B　RAM %+d B' % (t['d_flash'], t['d_ram']))
                else:
                    print('  较上次：这是第一份记录')
                for w in t.get('warnings') or []:
                    print('  ⚠ ' + w)
        return

    do_b = a.build or a.all or a.snapshot
    do_f = a.flash or a.all
    do_s = a.serial or a.all

    parts = []
    if do_b:
        parts += do_build(root, boards, a.quiet)
    if do_f:
        parts += do_flash(root, boards, a.quiet)
    if do_s:
        parts += do_serial(root, boards, a.com, a.quiet, a.seconds)

    results = {'time': datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
               'config': cfg_src, 'boards': merge(parts)}
    ok = all((b.get('build') or {}).get('ok', True) and
             (b.get('flash') or {}).get('ok', True) and
             (b.get('serial') or {}).get('ok', True)
             for b in results['boards'])

    if a.snapshot:
        d = make_snapshot(root, results)
        if not a.json:
            print('快照：%s' % d)
        results['snapshot'] = d

    if a.json:
        print(json.dumps({'ok': ok, 'boards': results['boards']}, ensure_ascii=False))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
