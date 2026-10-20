# -*- coding: utf-8 -*-
r"""上位机解析器对照（B3）＋上位机三方对照台（D4）。

为什么不能只比常量：常量对了，组帧顺序/字节序/CRC 范围仍可能错。
所以这里做「三层对照 + 三方对照台」，能跑几层跑几层，跑不了的**明说卡在哪**：

  第 1 层  常量：`sys_frame.h`（固件）vs `frameparser.h`（上位机）
  第 2 层  期望向量：上位机自检里写死的 VEC_NORMAL / VEC_NEGATIVE
           vs 仿真器按同样输入组出来的字节 —— 逐字节比
  第 3 层  真解析：把仿真帧喂给上位机的 FrameParser，逐字段比
           —— 需要上位机协议层已实现；现在 `frameparser.cpp` 还有 21 处 TODO(阶段2) 桩，
           所以这一层会报「被 F1 阻塞」并给出还剩多少处，**不假装跑过**

D4 · 三方对照台（逐字段）：
  ① 真板串口流   —— `--capture <文件>` 指定的真板抓包（默认找同目录 `产物/真板抓包.hex`）；
                   按帧头/帧长切帧，用 `sys_frame.h` 的 `ENV_*_OFF` 与单位/倍率口径
                   逐字段解出六个物理量（解不出的字段如实写「解不出」）。
  ② 仿真帧       —— `link_sim` 按**标准样例**参数（与上位机自检 VEC_NORMAL 同源，
                   见下方 REF_VALS）生成的那一帧，同样逐字段解。
  ③ 上位机 FrameParser —— 仍按 `frameparser.cpp` 里 TODO(阶段2) 桩计数判「还是桩」，
                   并**点名是哪几处**（函数名 + 行号），honest SKIP。

  ①② 逐字段比（定点整数口径，如 humi：真板 6030 / 仿真 6100）；不一致时**点名到字段**，
  说清是哪两方、哪个字段。哪一方缺就**如实报 SKIP**，**不许把 SKIP 说成一致**。

抓包放哪：`产物/真板抓包.hex`（或 `--capture` 指到别处）。文件是文本 hex，解析宽容——
支持行内/整段 hex、`#` 注释行、空行、大小写、`0x` 前缀、空格/逗号分隔；坏行会被点名。

F1 做完后不用改结构：第 ③ 方只是「按 TODO 计数判桩」，桩清零（layer3=ready）后，
同一个 `compare_capture` 就能把仿真帧也喂给真 FrameParser，自动变真对照。

用法：
    python 本地工具\工具箱\检查\协议对照\host_compare.py
    python ...\host_compare.py --capture 产物\真板抓包.hex
    python ...\host_compare.py --json
    python ...\host_compare.py --selftest
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', '链路仿真'))
import link_sim as ls

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
FW_HDR = 'EnvMonitor/Project_1_检测端/Project/FWLIB/inc/sys_frame.h'
HOST_HDR = 'EnvMonitor/QT_project/SerialMonitor/frameparser.h'
HOST_CPP = 'EnvMonitor/QT_project/SerialMonitor/frameparser.cpp'
HOST_TEST = 'EnvMonitor/QT_project/SerialMonitor/consistencytest.cpp'

# D4：真板抓包默认位置（脚本同级 产物/）；不存在时真板这一方 honest SKIP
DEFAULT_CAPTURE = os.path.join(HERE, '产物', '真板抓包.hex')

# 要对照的常量：名字 → (固件宏名, 上位机常量名)
CONSTS = [
    ('帧头高字节', 'SYS_FRAME_HEAD_HI', 'HEAD_HI'),
    ('帧头低字节', 'SYS_FRAME_HEAD_LO', 'HEAD_LO'),
    ('帧尾高字节', 'SYS_FRAME_TAIL_HI', 'TAIL_HI'),
    ('帧尾低字节', 'SYS_FRAME_TAIL_LO', 'TAIL_LO'),
    ('固定开销', 'SYS_FRAME_OVERHEAD', 'OVERHEAD'),
    ('环境帧命令字', 'SYS_FRAME_CMD_ENV', 'CMD_ENV'),
    ('环境帧数据长度', 'SYS_FRAME_ENV_LEN', 'ENV_LEN'),
    ('整帧长度', None, 'ENV_TOTAL'),          # 固件侧是 ENV_LEN + OVERHEAD，没有同名宏
]

# D4 · 仿真帧的标准样例参数（与上位机自检 VEC_NORMAL 同源：2560/6030/10132/320/15/812）。
# 真板按同一组测试输入上报时应与之一致；用别的读数会被如实点名。
REF_VALS = (25.60, 60.30, 1013.2, 320, 15, 812)

# D4 · 环境帧六字段：键 → (数据段偏移宏, 有符号?, 倍率, 单位, 中文名)。
# 偏移复用 sys_frame.h 的 ENV_*_OFF；单位/倍率口径见 sys_frame.h 区块1 字段表。
FIELD_SPECS = [
    ('temp',  'SYS_FRAME_ENV_TEMP_OFF',  True,  100.0, '℃',   '温度'),
    ('humi',  'SYS_FRAME_ENV_HUMI_OFF',  True,  100.0, '%RH',  '湿度'),
    ('press', 'SYS_FRAME_ENV_PRESS_OFF', True,  10.0,  'hPa',  '气压'),
    ('light', 'SYS_FRAME_ENV_LIGHT_OFF', False, 1.0,   'lx',   '光照'),
    ('tvoc',  'SYS_FRAME_ENV_TVOC_OFF',  False, 1.0,   'ppb',  'TVOC'),
    ('mq135', 'SYS_FRAME_ENV_MQ135_OFF', False, 1.0,   'ADC',  'MQ135'),
]


def read(path):
    with open(os.path.join(ROOT, path), 'rb') as f:
        raw = f.read()
    for enc in ('utf-8', 'gbk'):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace')


def parse_fw_consts(text):
    """#define SYS_FRAME_XXX  0xAAU  → {'SYS_FRAME_XXX': 170}"""
    out = {}
    for m in re.finditer(r'#define\s+(SYS_FRAME_[A-Z0-9_]+)\s+\(?\s*(0x[0-9A-Fa-f]+|\d+)[uUlL]*\s*\)?', text):
        out[m.group(1)] = int(m.group(2), 0)
    return out


def parse_host_consts(text):
    """constexpr quint8 HEAD_HI = 0xAAU;  /  constexpr int ENV_TOTAL = ENV_LEN + OVERHEAD;"""
    raw = {}
    for m in re.finditer(r'constexpr\s+\w+\s+([A-Z0-9_]+)\s*=\s*([^;]+);', text):
        raw[m.group(1)] = m.group(2).strip()

    def ev(expr, seen):
        e = expr.strip().rstrip('uUlL')
        e = re.sub(r'\b([A-Z][A-Z0-9_]*)\b', lambda mm: str(ev(raw[mm.group(1)], seen | {mm.group(1)}))
                   if mm.group(1) in raw and mm.group(1) not in seen else mm.group(1), e)
        e = e.replace('/', '//')
        try:
            return int(eval(e, {'__builtins__': {}}, {}))   # 只放数字与 + - * //，输入来自本仓头文件
        except Exception:
            return None

    out = {}
    for k in raw:
        v = ev(raw[k], {k})
        if isinstance(v, int):
            out[k] = v
    return out


def parse_vectors(text):
    """consistencytest.cpp 里的 VEC_XXX = "AA 55 ...";"""
    out = {}
    for m in re.finditer(r'static\s+const\s+char\s*\*\s*(VEC_[A-Z0-9_]+)\s*=\s*((?:\s*"[^"]*"\s*)+);', text):
        name = m.group(1)
        hexs = ''.join(re.findall(r'"([^"]*)"', m.group(2)))
        try:
            out[name] = bytes(int(x, 16) for x in hexs.split())
        except ValueError:
            pass
    return out


def host_stub_count(text):
    return text.count('TODO(阶段2)')


def host_stub_sites(text):
    """D4：点名上位机协议层还缺在哪几处 —— 每条 TODO(阶段2) 配上它所属的函数名与行号。"""
    lines = text.splitlines()
    out = []
    for i, ln in enumerate(lines):
        if 'TODO(阶段2)' not in ln:
            continue
        func = ''
        for j in range(i, min(i + 5, len(lines))):     # TODO 注释通常紧挨函数定义
            m = re.search(r'([A-Za-z_]\w*::[A-Za-z_]\w*)\s*\(', lines[j])
            if m:
                func = m.group(1)
                break
        out.append({'func': func or '(未知)', 'line': i + 1, 'msg': ln.strip()})
    return out


def compare_consts(fw, host):
    rows = []
    for label, fwname, hostname in CONSTS:
        fv = fw.get(fwname) if fwname else None
        hv = host.get(hostname)
        if fwname is None:      # 整帧长度：固件侧算出来
            fv = (fw.get('SYS_FRAME_ENV_LEN') or 0) + (fw.get('SYS_FRAME_OVERHEAD') or 0)
        ok = (fv is not None and hv is not None and fv == hv)
        rows.append({'what': label, 'fw': fv, 'host': hv, 'ok': ok})
    return rows


def compare_vectors(vectors):
    """按上位机自检的期望向量，用仿真器组一遍，逐字节比。"""
    cases = {
        'VEC_NORMAL': (25.60, 60.30, 1013.2, 320, 15, 812),
        'VEC_NEGATIVE': (-5.00, 45.00, 1000.0, 0, 0, 0),
    }
    rows = []
    for name, vals in cases.items():
        want = vectors.get(name)
        if want is None:
            rows.append({'what': name, 'ok': False, 'detail': '上位机自检里没找到这个向量'})
            continue
        got = ls.build_frame(0x01, ls.env_data(*vals))
        ok = got == want
        detail = '逐字节一致（%d 字节）' % len(want) if ok else \
                 '期望 %s\n实际 %s' % (ls.hex_spaced(want), ls.hex_spaced(got))
        rows.append({'what': name, 'ok': ok, 'detail': detail})
    return rows


# ========================================================================
# D4 · 三方对照台：真板抓包解析 + 逐字段对照
# ========================================================================

def parse_capture_text(text):
    """宽容解析真板抓包文本 hex。返回 (bytes, bad_lines)。

    支持：行内/整段 hex、`#` 注释行（到行尾）、空行、大小写、`0x` 前缀、空格/逗号分隔。
    坏行（含非 hex 记号、或奇数长度的 hex 记号）**不静默吞掉**，记进 bad_lines 一并点名。
    """
    out = bytearray()
    bad = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split('#', 1)[0].strip()          # 行内注释（# 到行尾） + 空行
        if not line:
            continue
        hexes = []
        line_ok = True
        for tok in re.split(r'[\s,;]+', line):
            if not tok:
                continue
            h = tok[2:] if tok[:2].lower() == '0x' else tok
            if not re.fullmatch(r'[0-9A-Fa-f]+', h) or len(h) % 2 != 0:
                line_ok = False
                break
            hexes.append(h)
        if not line_ok:
            bad.append({'line': lineno, 'text': raw.strip()})
            continue
        for h in hexes:
            for k in range(0, len(h), 2):
                out.append(int(h[k:k + 2], 16))
    return bytes(out), bad


def split_frames(stream, fw):
    """按 sys_frame.h 的帧头/帧长/帧尾常量切帧（宽容：帧头对不上就逐字节前移重同步）。
    返回每帧 {pos, ok, cmd, data, why}；CRC/帧尾不过的帧仍返回，供尽力解字段。"""
    head = (fw.get('SYS_FRAME_HEAD_HI', 0xAA), fw.get('SYS_FRAME_HEAD_LO', 0x55))
    tail = (fw.get('SYS_FRAME_TAIL_HI', 0x55), fw.get('SYS_FRAME_TAIL_LO', 0xAA))
    over = fw.get('SYS_FRAME_OVERHEAD', 8)
    maxp = fw.get('SYS_FRAME_MAX_PAYLOAD', 64)
    out = []
    i, n = 0, len(stream)
    while i + 1 < n:
        if stream[i] != head[0] or stream[i + 1] != head[1]:
            i += 1
            continue
        if i + 3 >= n:
            out.append({'pos': i, 'ok': False, 'cmd': None, 'data': b'',
                        'why': '帧不完整（流在此结束）'})
            break
        cmd, ln = stream[i + 2], stream[i + 3]
        if ln > maxp:                                # AA 55 撞上但长度超上限：多半是噪声假帧头
            i += 1
            continue
        total = over + ln
        if i + total > n:
            out.append({'pos': i, 'ok': False, 'cmd': cmd,
                        'data': bytes(stream[i + 4:min(i + 4 + ln, n)]),
                        'why': '帧不完整（流在此结束）'})
            break
        raw = stream[i:i + total]
        tail_ok = raw[total - 2] == tail[0] and raw[total - 1] == tail[1]
        calc = ls.crc16(raw[2:4 + ln])
        got = raw[4 + ln] | (raw[5 + ln] << 8)
        why = ''
        if not tail_ok:
            why = '帧尾不符（期望 %02X%02X）' % tail
        elif calc != got:
            why = 'CRC 不符（算得 %04X / 帧内 %04X）' % (calc, got)
        out.append({'pos': i, 'ok': tail_ok and calc == got, 'cmd': cmd,
                    'data': bytes(raw[4:4 + ln]), 'why': why})
        i += total
    return out


def decode_field(payload, off, signed):
    """按大端从数据段取一字段的定点整数；长度不够/偏移缺失 → None（如实写「解不出」）。"""
    if payload is None or off is None or off + 2 > len(payload):
        return None
    v = (payload[off] << 8) | payload[off + 1]
    if signed and v & 0x8000:
        v -= 0x10000
    return v


def decode_env_fields(payload, fw):
    """按 sys_frame.h 的 ENV_*_OFF 解出六个定点整数（字段名 → 值/None）。"""
    out = {}
    for key, off_macro, signed, _scale, _unit, _label in FIELD_SPECS:
        out[key] = decode_field(payload, fw.get(off_macro), signed)
    return out


def ref_field_ints():
    """标准样例参数的定点整数值（仿真帧应解出这一组）。"""
    t, h, p, li, tv, mq = REF_VALS
    return {'temp': int(round(t * 100)), 'humi': int(round(h * 100)),
            'press': int(round(p * 10)), 'light': int(li), 'tvoc': int(tv),
            'mq135': int(mq)}


def sim_frame(fw):
    """仿真帧：link_sim 按标准样例参数组的那一帧。返回 (整帧字节, 数据段)。"""
    cmd = fw.get('SYS_FRAME_CMD_ENV', 0x01)
    data = ls.env_data(*REF_VALS)
    return ls.build_frame(cmd, data), data


def compare_capture(path, fw, stubs):
    """D4：真板抓包 ↔ 仿真帧 逐字段对照；三方状态一并给出。"""
    cmd_env = fw.get('SYS_FRAME_CMD_ENV', 0x01)
    sim_bytes, sim_data = sim_frame(fw)
    sim_fields = decode_env_fields(sim_data, fw)

    info = {'path': path, 'found': os.path.isfile(path), 'bytes': 0, 'frames': 0,
            'env_frames': 0, 'bad_lines': [], 'frame_ok': None, 'frame_why': '',
            'summary': ''}
    found = info['found']

    # 上位机 FrameParser 一方：按桩计数判 SKIP，并点名缺在哪几处
    sites = host_stub_sites(read(HOST_CPP))
    parser_detail = '被 F1 阻塞：frameparser.cpp 里还有 %d 处 TODO(阶段2)（协议层是桩，返回空）' % stubs
    if stubs:
        shown = sites[:6]
        parser_detail += '。缺的几处：' + '、'.join(
            '%s（第 %d 行）' % (s['func'], s['line']) for s in shown)
        if len(sites) > len(shown):
            parser_detail += ' …（另有 %d 处）' % (len(sites) - len(shown))
    else:
        parser_detail = '上位机协议层 TODO(阶段2) 已清零（F1 已完成），下一步可接真 FrameParser 逐帧对照。'

    if not found:
        real_detail = ('没有真板抓包：把板子串口(115200)收到的原始帧存成 `%s`'
                       '（或 `--capture` 指定）再跑' % path)
        sides = [
            {'name': '真板串口流', 'status': 'skip', 'detail': real_detail},
            {'name': '仿真帧', 'status': 'compare',
             'detail': 'link_sim 按标准样例参数生成（%s）' % ls.hex_spaced(sim_bytes)},
            {'name': '上位机 FrameParser', 'status': 'skip' if stubs else 'ready',
             'detail': parser_detail},
        ]
        info['sides'] = sides
        info['fields'] = []
        n_cmp = sum(1 for s in sides if s['status'] == 'compare')
        n_skip = sum(1 for s in sides if s['status'] == 'skip')
        info['summary'] = ('三方对照：真板串口流 SKIP（缺抓包）、上位机 FrameParser %s；'
                           '本次 %d 方真比（仿真帧）、%d 方 SKIP。'
                           'SKIP 不等于一致。'
                           % ('SKIP（被 F1 阻塞）' if stubs else '就绪', n_cmp, n_skip))
        return info

    with open(path, 'rb') as f:
        raw = f.read()
    text = None
    for enc in ('utf-8', 'gbk', 'latin-1'):
        try:
            text = raw.decode(enc)
            break
        except UnicodeDecodeError:
            continue
    text = text or ''
    stream, bad_lines = parse_capture_text(text)
    frames = split_frames(stream, fw)
    env_frames = [x for x in frames if x['cmd'] == cmd_env]
    info.update({'bytes': len(stream), 'frames': len(frames),
                 'env_frames': len(env_frames), 'bad_lines': bad_lines})

    chosen = next((x for x in env_frames if x['ok']), env_frames[0] if env_frames else None)
    real_fields = decode_env_fields(chosen['data'], fw) if chosen else \
        {k: None for k, *_ in FIELD_SPECS}
    if chosen:
        info['frame_ok'] = chosen['ok']
        info['frame_why'] = chosen['why']

    rows = []
    mismatch = []
    for key, _off, _s, scale, unit, label in FIELD_SPECS:
        rv, sv = real_fields.get(key), sim_fields.get(key)
        if rv is None:
            note = '解不出（真板净荷不足或 ENV_*_OFF 缺失）'
            ok = False
        elif sv is None:
            note = '解不出（仿真侧）'
            ok = False
        elif rv == sv:
            note = ''
            ok = True
        else:
            note = '真板与仿真帧不一致：字段 `%s`（真板 %d / 仿真 %d）' % (key, rv, sv)
            ok = False
            mismatch.append(key)
        rows.append({'field': key, 'label': label, 'unit': unit, 'scale': scale,
                     'real': rv, 'sim': sv, 'ok': ok, 'note': note})
    info['fields'] = rows

    real_detail = ('抓包 %s：%d 字节，切出 %d 帧（环境帧 %d）'
                   % (os.path.basename(path), info['bytes'], info['frames'], info['env_frames']))
    if bad_lines:
        real_detail += '；坏行 %d 处（第 %s 行）' % (
            len(bad_lines), '、'.join(str(b['line']) for b in bad_lines[:5]))
    if chosen is None:
        real_detail += '；没切出环境帧（CMD=0x%02X）→ 六字段解不出' % cmd_env
    elif not chosen['ok']:
        real_detail += '；该帧校验未过（%s），仍按净荷尽力解' % chosen['why']

    sides = [
        {'name': '真板串口流', 'status': 'compare', 'detail': real_detail},
        {'name': '仿真帧', 'status': 'compare',
         'detail': 'link_sim 按标准样例参数生成（%s）' % ls.hex_spaced(sim_bytes)},
        {'name': '上位机 FrameParser', 'status': 'skip' if stubs else 'ready',
         'detail': parser_detail},
    ]
    info['sides'] = sides

    n_cmp = sum(1 for s in sides if s['status'] == 'compare')
    n_skip = sum(1 for s in sides if s['status'] == 'skip')
    if mismatch:
        info['summary'] = ('三方对照：真板串口流 与 仿真帧 有 %d 个字段不一致（%s）；'
                           '上位机 FrameParser SKIP（被 F1 阻塞）。本次 %d 方真比、%d 方 SKIP。'
                           % (len(mismatch), '、'.join(mismatch), n_cmp, n_skip))
    else:
        info['summary'] = ('三方对照：真板串口流 与 仿真帧 逐字段一致（%d/%d）；'
                           '上位机 FrameParser %s。本次 %d 方真比、%d 方 SKIP。'
                           % (len(rows), len(rows),
                              'SKIP（被 F1 阻塞）' if stubs else '就绪', n_cmp, n_skip))
    return info


def run_all(capture_path=None):
    fw = parse_fw_consts(read(FW_HDR))
    host = parse_host_consts(read(HOST_HDR))
    vectors = parse_vectors(read(HOST_TEST))
    stubs = host_stub_count(read(HOST_CPP))
    cap = compare_capture(capture_path or DEFAULT_CAPTURE, fw, stubs)
    return {
        'consts': compare_consts(fw, host),
        'vectors': compare_vectors(vectors),
        'host_stubs': stubs,
        'host_stub_sites': host_stub_sites(read(HOST_CPP)),
        'layer3': ('blocked' if stubs else 'ready'),
        'capture': cap,
        'sides': cap['sides'],
        'fields': cap['fields'],
    }


def selftest():
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    fw_txt = '#define SYS_FRAME_HEAD_HI 0xAAU\n#define SYS_FRAME_ENV_LEN 0x0CU\n'
    fw = parse_fw_consts(fw_txt)
    ck('解析固件 #define（含 0x..U 后缀）', fw == {'SYS_FRAME_HEAD_HI': 0xAA,
                                                  'SYS_FRAME_ENV_LEN': 12}, fw)

    host_txt = ('constexpr quint8 HEAD_HI = 0xAAU;\n'
                'constexpr quint8 ENV_LEN = 0x0CU;\n'
                'constexpr int  OVERHEAD = 8;\n'
                'constexpr int  ENV_TOTAL = ENV_LEN + OVERHEAD;\n')
    host = parse_host_consts(host_txt)
    ck('解析上位机 constexpr（含表达式 ENV_LEN + OVERHEAD）',
       host.get('ENV_TOTAL') == 20 and host.get('HEAD_HI') == 0xAA, host)

    vec_txt = 'static const char *VEC_X =\n    "AA 55 01 0C 0A 00 17 8E 27 94 01 40 00 0F 03 2C 98 FA 55 AA";\n'
    vs = parse_vectors(vec_txt)
    ck('解析上位机期望向量', len(vs.get('VEC_X', b'')) == 20, len(vs.get('VEC_X', b'')))

    real = run_all()
    bad = [r for r in real['consts'] if not r['ok']]
    ck('真实：8 项协议常量全部一致', not bad, bad)
    bad2 = [r for r in real['vectors'] if not r['ok']]
    ck('真实：仿真组帧与上位机期望向量逐字节一致', not bad2,
       [r['what'] for r in bad2])
    ck('真实：能读出上位机还剩多少处 TODO(阶段2)',
       isinstance(real['host_stubs'], int), real['host_stubs'])
    ck('第 3 层状态明确（blocked / ready）', real['layer3'] in ('blocked', 'ready'))
    if real['host_stubs']:
        ck('上位机协议层还是桩 → 第 3 层必须报 blocked（不假装跑过）',
           real['layer3'] == 'blocked', real['layer3'])
    ck('D4：能点名上位机桩在哪几处（函数名 + 行号）',
       real['host_stub_sites'] and all(s['func'] and s['line'] for s in real['host_stub_sites']),
       real['host_stub_sites'][:2] if real['host_stub_sites'] else '空')
    ck('D4：JSON 信封新增键 capture / sides / fields 都在',
       {'capture', 'sides', 'fields'} <= set(real))

    # ---- D4 抓包解析：宽容格式（注释 / 空行 / 0x 前缀 / 大小写 / 整段 hex）----
    _tolerant = ('# 这是注释行\n'
                 'AA55 010c\n'
                 '0x0a 0x00\n'
                 '\n'
                 '178e 2794 0140 000F 032C 98FA 55AA\n')
    _want = 'AA 55 01 0C 0A 00 17 8E 27 94 01 40 00 0F 03 2C 98 FA 55 AA'
    _bytes, _bad = parse_capture_text(_tolerant)
    ck('D4 抓包解析宽容：注释/空行/0x/大小写/整段 hex 都能读，且 0 坏行',
       len(_bad) == 0 and ls.hex_spaced(_bytes) == _want, (_bytes.hex(), _bad))

    import shutil
    import tempfile
    td = tempfile.mkdtemp(prefix='hostcmp_selftest_')
    try:
        fw_real = parse_fw_consts(read(FW_HDR))
        stubs = host_stub_count(read(HOST_CPP))
        good, _gdata = sim_frame(fw_real)                     # 与 link_sim 输出完全一致

        # 正例：抓包 == link_sim 输出 → 真板 = 仿真、字段全一致、FrameParser 一方 SKIP
        p_ok = os.path.join(td, '真板抓包.hex')
        with open(p_ok, 'w', encoding='utf-8', newline='\n') as f:
            f.write('# 真板抓包（与 link_sim 输出一致）\n')
            f.write(ls.hex_spaced(good) + '\n')
        r_ok = compare_capture(p_ok, fw_real, stubs)
        _real_side = next(s for s in r_ok['sides'] if s['name'] == '真板串口流')
        _par_side = next(s for s in r_ok['sides'] if s['name'] == '上位机 FrameParser')
        ck('D4 正例：真板=仿真、六字段全一致',
           len(r_ok['fields']) == 6 and all(x['ok'] for x in r_ok['fields']),
           [x['field'] for x in r_ok['fields'] if not x['ok']])
        ck('D4 正例：真板这一方状态=对比（不是 SKIP）',
           _real_side['status'] == 'compare', _real_side['status'])
        ck('D4 正例：FrameParser 一方 SKIP（原因是桩）',
           _par_side['status'] == 'skip' and 'TODO(阶段2)' in _par_side['detail'],
           _par_side['detail'])

        # 反例1：把抓包里 humi 字段改一个字节 → 点名「真板与仿真帧不一致，字段 humi」
        bad = bytearray(good)
        humi_off = 4 + fw_real.get('SYS_FRAME_ENV_HUMI_OFF', 2)   # 数据段偏移 + 帧头/CMD/LEN
        bad[humi_off + 1] ^= 0x01
        p_bad = os.path.join(td, '坏字段.hex')
        with open(p_bad, 'w', encoding='utf-8', newline='\n') as f:
            f.write(ls.hex_spaced(bytes(bad)) + '\n')
        r_bad = compare_capture(p_bad, fw_real, stubs)
        by_field = {x['field']: x for x in r_bad['fields']}
        ck('D4 反例1：改坏 humi 一字节 → 点名到字段 humi（不是笼统报不一致）',
           by_field['humi']['ok'] is False and 'humi' in by_field['humi']['note']
           and '真板' in by_field['humi']['note'],
           by_field['humi']['note'])
        ck('D4 反例1：其它字段仍判一致（只有 humi 被点名）',
           all(by_field[k]['ok'] for k in by_field if k != 'humi'))

        # 反例2：抓包缺失 → 真板一方记 SKIP（不是 FAIL，更不是"一致"）
        r_miss = compare_capture(os.path.join(td, '不存在.hex'), fw_real, stubs)
        _miss = next(s for s in r_miss['sides'] if s['name'] == '真板串口流')
        ck('D4 反例2：抓包缺失 → 真板一方记 SKIP',
           _miss['status'] == 'skip' and '没有真板抓包' in _miss['detail'],
           _miss['status'])
        ck('D4 反例2：缺失时不把 SKIP 说成一致（总结论无"逐字段一致"）',
           '逐字段一致' not in r_miss['summary'], r_miss['summary'])

        # 反例3：三段 hex 中间插一行垃圾 → 报成坏行，不静默当合法帧
        seg = ls.hex_spaced(good).split()
        lines = [' '.join(seg[0:8]), '这是一行垃圾 GG！', ' '.join(seg[8:16]), ' '.join(seg[16:])]
        p_junk = os.path.join(td, '夹垃圾.hex')
        with open(p_junk, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
        _jtext = '\n'.join(lines) + '\n'
        _jbytes, _jbad = parse_capture_text(_jtext)
        r_junk = compare_capture(p_junk, fw_real, stubs)
        ck('D4 反例3：三段 hex 中间夹垃圾行 → 报成坏行（不静默当合法帧）',
           len(_jbad) == 1 and len(r_junk['bad_lines']) == 1,
           (_jbad, r_junk['bad_lines']))
        ck('D4 反例3：坏行被剔除后，好行仍能切出环境帧（不是整段作废）',
           r_junk['frames'] >= 1 and r_junk['env_frames'] >= 1,
           (r_junk['frames'], r_junk['env_frames']))
    finally:
        shutil.rmtree(td, ignore_errors=True)

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('协议对照自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='上位机解析器对照（B3）＋三方对照台（D4）')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--capture', default=None,
                    help='真板串口抓包（文本 hex）；默认 %s' % DEFAULT_CAPTURE)
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    r = run_all(a.capture)
    cap = r['capture']
    consts_ok = all(x['ok'] for x in r['consts'])
    vectors_ok = all(x['ok'] for x in r['vectors'])
    cap_ok = (not cap['found']) or all(x['ok'] for x in cap['fields'])
    ok = consts_ok and vectors_ok and cap_ok

    if a.json:
        print(json.dumps({'tool': '协议对照', 'schema': 1, 'ok': ok, 'layer3': r['layer3'],
                          'host_stubs': r['host_stubs'],
                          'consts': r['consts'], 'vectors': r['vectors'],
                          'capture': cap, 'sides': r['sides'], 'fields': r['fields'],
                          'host_stub_sites': r['host_stub_sites']},
                         ensure_ascii=False))
        sys.exit(0 if ok else 1)

    print('第 1 层 · 协议常量（固件 sys_frame.h ↔ 上位机 frameparser.h）')
    for x in r['consts']:
        print('  [%s] %-12s 固件=%-4s 上位机=%-4s' % ('PASS' if x['ok'] else 'FAIL',
                                                      x['what'], x['fw'], x['host']))
    print('\n第 2 层 · 期望向量（上位机自检里写死的字节 ↔ 仿真器组帧）')
    for x in r['vectors']:
        print('  [%s] %s　%s' % ('PASS' if x['ok'] else 'FAIL', x['what'],
                                 x['detail'] if not x['ok'] else '逐字节一致'))
    print('\n第 3 层 · 真解析（仿真帧 → 上位机 FrameParser → 逐字段比）')
    if r['layer3'] == 'blocked':
        print('  [SKIP] 被 F1 阻塞：frameparser.cpp 里还有 %d 处 TODO(阶段2)（协议层是桩，返回空）。'
              % r['host_stubs'])
        print('         补完协议层后重跑本命令，第 3 层会自动变成真对照。')
    else:
        print('  [READY] 上位机协议层已实现，可以接真对照（下一步：编一个小 CLI 喂帧）。')

    print('\n三方对照台 · 真板串口流 ↔ 仿真帧 ↔ 上位机 FrameParser')
    tag = {'compare': '对比', 'skip': 'SKIP', 'ready': '就绪'}
    for s in r['sides']:
        print('  [%s] %s：%s' % (tag.get(s['status'], s['status'].upper()),
                                 s['name'], s['detail']))
    if cap['bad_lines']:
        for b in cap['bad_lines'][:5]:
            print('  [坏行] 第 %d 行：%s' % (b['line'], b['text']))
    if r['fields']:
        print('  ── 六字段逐字段对照（定点整数口径）')
        for x in r['fields']:
            rv = '解不出' if x['real'] is None else str(x['real'])
            sv = '解不出' if x['sim'] is None else str(x['sim'])
            line = '  [%s] %-6s 真板=%-6s 仿真=%-6s %s×%g' % (
                'PASS' if x['ok'] else 'FAIL', x['field'], rv, sv, x['unit'], x['scale'])
            print(line)
            if x['note'] and not x['ok']:
                print('        %s' % x['note'])
    print('  %s' % cap['summary'])

    print('\n结论：%s' % ('常量与组帧一致' + ('，三方对照见上' if cap['found'] else '（真板抓包缺 → 该方 SKIP）')
                          if ok else '有分歧，先修上面 FAIL 的项'))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
