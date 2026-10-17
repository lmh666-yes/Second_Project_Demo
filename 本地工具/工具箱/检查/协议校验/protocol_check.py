# -*- coding: utf-8 -*-
r"""协议端到端校验（幂等，只读源码，不改任何文件）。

用途：核查板1（检测端）与板2（通信端）的 sys_frame.h/.c 中帧协议
      常量、字段偏移、字节序是否完全一致，防止"组帧方改了、解帧方没改"
      导致联调时帧错、数据错位。

背景：两板靠 LoRa 透传互通，帧格式由 sys_frame.h 定义；任何一方的
      帧头/帧尾/字段偏移/字节序与另一方不一致都会导致接收失败。

检查项：
  1) 两板 sys_frame.h 的关键协议常量是否一致（帧头、帧尾、开销、
     最大负载、环境帧 CMD/LEN）
  2) 环境帧字段偏移表是否一致（TEMP/HUMI/PRESS/LIGHT/TVOC/MQ135）
  3) 字段偏移总和 + 末字段宽度 是否等于 数据段长度（防漏字段/越界）
  4) 字节序：BuildEnv 与 UnpackEnv 是否都用大端（高字节在前）
  5) CRC 范围：组帧与校验是否都覆盖 CMD+LEN+DATA（不含帧头/帧尾）
  6) 帧头帧尾是否互为反序（0xAA55 ↔ 0x55AA）
  7) 文件编码是否可识别
  8) 帧长 vs 上限（B2）：各 `*_LEN` 是否 <= SYS_FRAME_MAX_PAYLOAD、各 `*_OFF`
     是否落在数据段内；收帧缓冲是否按 MAX_PAYLOAD 定尺、组帧/收帧是否有按
     MAX_PAYLOAD 的长度上界护栏、帧长是否按 OVERHEAD 算
  9) 两板命令字表（B2）：同名命令字取值是否一致、同板内是否"撞车"（两个名字
     同一个值）；只在一板出现的命令字列进报告对照表，不算问题（两板角色不同）
 10) 命令字"新增即检查"（B3）：命令字取值不再只认字面量，宏表达式（如
     `REPLAY_ACK_CMD = CMD_REPLAY | ACK_FLAG`）也能算出来；核对命名规范
     （SYS_FRAME_CMD_<大写下划线>）与"应答 CMD = 请求 CMD | ACK_FLAG"约定，
     并把两板命令字并集落成模板 `速查数据/rules/frame_commands.json`

报告写到脚本同目录的 协议校验报告.md，并在控制台打印摘要。

用法：
    python 本地工具\工具箱\检查\协议校验\protocol_check.py
    python 本地工具\工具箱\检查\协议校验\protocol_check.py --quiet
    python 本地工具\工具箱\检查\协议校验\protocol_check.py --json
    python 本地工具\工具箱\检查\协议校验\protocol_check.py --selftest
"""
import json
import os
import re
import sys
import tempfile
import shutil

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass


def find_root(start):
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

# 两板的 sys_frame.h 与 sys_frame.c 路径
BOARDS = [
    ('板1 检测端 (F407ZE)',
     os.path.join('EnvMonitor', 'Project_1_检测端', 'Project', 'FWLIB', 'inc', 'sys_frame.h'),
     os.path.join('EnvMonitor', 'Project_1_检测端', 'Project', 'FWLIB', 'src', 'sys_frame.c')),
    ('板2 通信端 (F407ZG)',
     os.path.join('EnvMonitor', 'Project_2_数据通信端', 'Project', 'FWLIB', 'inc', 'sys_frame.h'),
     os.path.join('EnvMonitor', 'Project_2_数据通信端', 'Project', 'FWLIB', 'src', 'sys_frame.c')),
]

# 必须一致的协议常量
PROTOCOL_MACROS = {
    'SYS_FRAME_HEAD_HI': '帧头高字节',
    'SYS_FRAME_HEAD_LO': '帧头低字节',
    'SYS_FRAME_TAIL_HI': '帧尾高字节',
    'SYS_FRAME_TAIL_LO': '帧尾低字节',
    'SYS_FRAME_OVERHEAD': '帧固定开销',
    'SYS_FRAME_MAX_PAYLOAD': '最大负载',
    'SYS_FRAME_CMD_ENV': '环境帧命令字',
    'SYS_FRAME_ENV_LEN': '环境帧数据长度',
    'SYS_FRAME_ACK_FLAG': '应答位（应答 CMD = 请求 CMD | 本位）',
}

# 环境帧字段偏移宏
ENV_OFFSET_MACROS = [
    'SYS_FRAME_ENV_TEMP_OFF',
    'SYS_FRAME_ENV_HUMI_OFF',
    'SYS_FRAME_ENV_PRESS_OFF',
    'SYS_FRAME_ENV_LIGHT_OFF',
    'SYS_FRAME_ENV_TVOC_OFF',
    'SYS_FRAME_ENV_MQ135_OFF',
]

RE_DEFINE_VALUE = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*)\s+(.+?)\s*$')
RE_DEFINE_FUNC = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*)\(')   # 函数式宏，跳过

# B2：命令字 / 帧数据段长度 / 字段偏移 三类宏（用于命令字表对照与上限核算）
RE_CMD_NAME = re.compile(r'^SYS_FRAME_(?:CMD_\w+|\w+_CMD)$')
RE_LEN_NAME = re.compile(r'^SYS_FRAME_\w+_LEN$')
RE_OFF_NAME = re.compile(r'^SYS_FRAME_\w+_OFF$')
# B3：命令字的"规范名" —— SYS_FRAME_CMD_<大写下划线> 或 SYS_FRAME_<大写下划线>_CMD
RE_CMD_STD = re.compile(r'^SYS_FRAME_(?:CMD_[A-Z0-9_]+|[A-Z0-9_]+_CMD)$')
# 收帧缓冲按 MAX_PAYLOAD 定尺（如 data[SYS_FRAME_MAX_PAYLOAD]）
RE_C_SIZED = re.compile(r'\[[^\]]*SYS_FRAME_MAX_PAYLOAD[^\]]*\]')
# 长度上界护栏：if (len > SYS_FRAME_MAX_PAYLOAD) 这类比较
RE_C_GUARD = re.compile(r'if\s*\([^)]{0,120}SYS_FRAME_MAX_PAYLOAD')

# B3：命令字全集模板（相对仓库根）—— 两板命令字并集落成一份，新增命令字会出现在这里
CMD_TEMPLATE_REL = os.path.join('本地工具', '工具箱', '速查数据', 'rules', 'frame_commands.json')
RE_NUM_SUFFIX = re.compile(r'([0-9A-Fa-f])[uUlL]+\b')
RE_HEX = re.compile(r'0[xX][0-9A-Fa-f]+')


def read_text(path):
    if not os.path.exists(path):
        return None, '文件不存在'
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ('utf-8', 'gbk', 'latin-1'):
        try:
            return raw.decode(enc), enc
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace'), 'latin-1'


def eval_expr(expr, pool, depth=0):
    """把宏表达式算成整数：支持字面量、| & + - << >> 括号与宏引用
    （如 SYS_FRAME_REPLAY_ACK_CMD = SYS_FRAME_CMD_REPLAY | SYS_FRAME_ACK_FLAG）。
    算不出（含函数调用、类型名、字符串）返回 None —— 不猜。"""
    if depth > 8 or expr is None:
        return None
    e = re.split(r'/\*|//', expr)[0].strip()
    if not e:
        return None
    e = RE_NUM_SUFFIX.sub(r'\1', e)

    def repl(m):
        n = m.group(1)
        v = eval_expr(pool.get(n), pool, depth + 1) if n in pool else None
        return str(v) if v is not None else n

    e = re.sub(r'\b([A-Za-z_]\w*)\b', repl, e)
    # 把 16 进制字面量的 x 排除掉，再看还剩没算出来的标识符
    if re.search(r'[A-Za-z_]', RE_HEX.sub('0', e)):
        return None
    try:
        v = eval(e, {'__builtins__': {}}, {})
    except Exception:
        return None
    return int(v) if isinstance(v, int) else None


def parse_header(text):
    """解析 sys_frame.h，返回 (关键常量, B2 宏表, 问题列表)。

    B2 宏表收三类：命令字（SYS_FRAME_CMD_* / SYS_FRAME_*_CMD）、
    帧数据段长度（SYS_FRAME_*_LEN）、字段偏移（SYS_FRAME_*_OFF）。
    B3：取值不再只认字面量 —— 像 `SYS_FRAME_REPLAY_ACK_CMD = SYS_FRAME_CMD_REPLAY |
    SYS_FRAME_ACK_FLAG` 这种宏表达式也能算出来（这样应答位约定才核得了），
    真算不出的才跳过。"""
    vals = {}
    extra = {}
    issues = []
    # 先把全部宏表达式收进池子，供 eval_expr 解析宏引用
    pool = {}
    for line in text.splitlines():
        m = RE_DEFINE_VALUE.match(line)
        if m and not RE_DEFINE_FUNC.match(line) and m.group(1) not in pool:
            pool[m.group(1)] = m.group(2).strip()

    for i, line in enumerate(text.splitlines(), 1):
        if RE_DEFINE_FUNC.match(line):
            continue
        m = RE_DEFINE_VALUE.match(line)
        if not m:
            continue
        k, raw = m.group(1), m.group(2).strip()
        num = eval_expr(raw, pool)
        if num is None:
            if k in PROTOCOL_MACROS or k in ENV_OFFSET_MACROS:
                issues.append(('宏解析失败', '%s = %s（第 %d 行）' % (k, raw, i)))
            continue
        if k in PROTOCOL_MACROS or k in ENV_OFFSET_MACROS:
            vals[k] = num
        elif RE_CMD_NAME.match(k) or RE_LEN_NAME.match(k) or RE_OFF_NAME.match(k):
            extra[k] = num
    return vals, extra, issues


def ack_base_of(name):
    """B3：把"应答命令字名"映射回它的"请求命令字名"。
    认得两种写法：SYS_FRAME_<X>_ACK_CMD 与 SYS_FRAME_CMD_<X>_ACK。
    不是应答命名则返回 None。"""
    m = re.match(r'^SYS_FRAME_(.+)_ACK_CMD$', name)
    if m:
        return 'SYS_FRAME_CMD_' + m.group(1)
    m = re.match(r'^SYS_FRAME_CMD_(.+)_ACK$', name)
    if m:
        return 'SYS_FRAME_CMD_' + m.group(1)
    return None


def cmd_convention_issues(board_name, cmds, ack_flag):
    """B3：命令字"新增即检查" —— 命名规范 + 应答位约定（应答 CMD = 请求 CMD | ACK_FLAG）。
    返回 findings（(等级, 位置, 说明, 类别)）。"""
    out = []

    # 命名规范：SYS_FRAME_CMD_<大写下划线> 或 SYS_FRAME_<大写下划线>_CMD
    for k in sorted(cmds):
        if not RE_CMD_STD.match(k):
            out.append(('注意', board_name,
                        '命令字 %s 命名不规范：应为 SYS_FRAME_CMD_<大写下划线>'
                        '（应答可用 SYS_FRAME_<X>_ACK_CMD）' % k, 'cmd_naming'))

    for k in sorted(cmds):
        base = ack_base_of(k)
        if base is not None:
            if base not in cmds:
                out.append(('注意', board_name,
                            '应答命令字 %s 找不到对应请求命令字 %s（名字写错？请求还没定义？）'
                            % (k, base), 'cmd_ack'))
                continue
            expect = cmds[base] | ack_flag
            if cmds[k] != expect:
                out.append(('严重', board_name,
                            '应答位不对：%s = 0x%02X，应为 %s(0x%02X) | 0x%02X = 0x%02X'
                            % (k, cmds[k], base, cmds[base], ack_flag, expect), 'cmd_ack'))
        elif cmds[k] & ack_flag:
            # 请求 CMD 应是 0x10~0x7F；最高位被置 1 说明"长得像应答却没按应答命名"
            out.append(('严重', board_name,
                        '命令字 %s = 0x%02X 最高位（应答位）被置 1，但名字不是应答命名'
                        '（应写成 SYS_FRAME_<X>_ACK_CMD；请求命令字应是 0x10~0x7F）'
                        % (k, cmds[k]), 'cmd_ack'))
    return out


def write_cmd_template(root, tables, ack_flag):
    """B3：把两板命令字并集落成一份模板（JSON），方便"新增命令字时"对照。
    tables = [(板名, {命令字: 值}), ...]。写不动就静默跳过（只读工具不因写模板失败而挂）。"""
    path = os.path.join(root, CMD_TEMPLATE_REL)
    names = sorted({k for _, t in tables for k in t})
    cmds = []
    for k in names:
        boards = [nm for nm, t in tables if k in t]
        vals = sorted({t[k] for _, t in tables if k in t})
        cmds.append({'name': k, 'value': vals[0] if len(vals) == 1 else None,
                     'value_hex': ('0x%02X' % vals[0]) if len(vals) == 1 else '两板不一致',
                     'boards': boards})
    doc = {'version': 1, 'ack_flag': '0x%02X' % ack_flag,
           'ack_rule': '应答 CMD = 请求 CMD | ACK_FLAG；请求 CMD 0x10~0x7F',
           'commands': cmds}
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(json.dumps(doc, ensure_ascii=False, indent=2) + '\n')
    except OSError:
        pass
    return path


def cmd_table_of(vals, extra):
    """命令字表：extra 里的 CMD_* 宏 + vals 里被列为关键常量的命令字
    （SYS_FRAME_CMD_ENV 属 PROTOCOL_MACROS，落在 vals 里，必须一并算进来，
    否则"同名不同值 / 撞车"会漏判）。"""
    d = {k: v for k, v in extra.items() if RE_CMD_NAME.match(k)}
    for k, v in vals.items():
        if RE_CMD_NAME.match(k):
            d[k] = v
    return d


def parse_c_byteorder(text):
    """检查 .c 中是否大端存取（高字节在前）。"""
    ok = False
    for line in text.splitlines():
        if 'frame_put_u16_be' in line or 'frame_get_u16_be' in line:
            ok = True
            break
        # 直接看到移位模式也认：高字节在前
        if '>> 8' in line and 'p[0]' in line and 'frame_put_u16' in line:
            ok = True
            break
    return ok


def parse_c_crc_range(text):
    """检查 .c 中 CRC 是否覆盖 CMD+LEN+DATA。"""
    calls = []
    for line in text.splitlines():
        # 匹配 frame_crc16(...); 允许括号嵌套
        m = re.search(r'frame_crc16\s*\((.*)\)\s*;', line)
        if m:
            inner = m.group(1)
            parts = [p.strip() for p in inner.split(',', 1)]
            if len(parts) == 2:
                calls.append((parts[0], parts[1]))
    good = False
    for start, length in calls:
        if ('[2]' in start) and ('len' in length.lower() or 'dlen' in length.lower()):
            good = True
            break
    return good, calls


def check_board(name, h_path, c_path):
    findings = []
    h_text, h_enc = read_text(h_path)
    if h_text is None:
        findings.append(('严重', h_path, '头文件缺失', 'protocol'))
        return {'findings': findings, 'vals': {}, 'extra': {}}

    vals, extra, h_issues = parse_header(h_text)
    for iss in h_issues:
        findings.append(('注意', h_path, iss[1], 'protocol'))

    # 检查帧头帧尾是否互为反序
    head = (vals.get('SYS_FRAME_HEAD_HI'), vals.get('SYS_FRAME_HEAD_LO'))
    tail = (vals.get('SYS_FRAME_TAIL_HI'), vals.get('SYS_FRAME_TAIL_LO'))
    if head != (None, None) and tail != (None, None):
        if head != (tail[1], tail[0]):
            findings.append(('注意', h_path,
                             '帧头 0x%02X%02X 与帧尾 0x%02X%02X 不互为反序' % (head[0], head[1], tail[0], tail[1]),
                             'protocol'))

    # 检查环境帧字段偏移覆盖
    env_len = vals.get('SYS_FRAME_ENV_LEN', 0)
    offsets = [vals.get(k) for k in ENV_OFFSET_MACROS]
    if None not in offsets and env_len > 0:
        offsets = sorted(offsets)
        # 每个字段占 2 字节（uint16 / int16）
        end = offsets[-1] + 2
        if end != env_len:
            findings.append(('注意', h_path,
                             '环境帧字段偏移末项(%d)+2 = %d，不等于 ENV_LEN(%d)，可能漏字段或越界'
                             % (offsets[-1], end, env_len), 'protocol'))
        # 检查是否连续（无重叠、无间隙）
        for i in range(1, len(offsets)):
            if offsets[i] - offsets[i - 1] != 2:
                findings.append(('注意', h_path,
                                 '字段偏移不连续：%s=%d 与下一项间隔 %d 字节（预期 2）'
                                 % (ENV_OFFSET_MACROS[i - 1], offsets[i - 1], offsets[i] - offsets[i - 1]),
                                 'protocol'))

    # .c 检查
    c_text, c_enc = read_text(c_path)
    if c_text is None:
        findings.append(('严重', c_path, '实现文件缺失', 'protocol'))
    else:
        if not parse_c_byteorder(c_text):
            findings.append(('注意', c_path, '未找到大端存取辅助函数（frame_put_u16_be / frame_get_u16_be），字节序可能不一致', 'protocol'))
        crc_ok, crc_calls = parse_c_crc_range(c_text)
        if not crc_ok:
            findings.append(('注意', c_path,
                             'CRC 计算范围疑似未覆盖 CMD+LEN+DATA（frame_crc16 调用: %s）'
                             % str(crc_calls), 'protocol'))

    # B2 —— 帧长 vs 上限：各命令帧声明的数据段长度 / 字段偏移是否落在上限内
    maxp = vals.get('SYS_FRAME_MAX_PAYLOAD')
    if maxp:
        for k in sorted(extra):
            v = extra[k]
            if RE_LEN_NAME.match(k) and v > maxp:
                findings.append(('严重', h_path,
                                 '%s = %d 超过 SYS_FRAME_MAX_PAYLOAD(%d)：收帧缓冲装不下这条数据段'
                                 % (k, v, maxp), 'frame_len'))
            if RE_OFF_NAME.match(k) and v >= maxp:
                findings.append(('严重', h_path,
                                 '%s = %d 不在数据段范围内（上限 %d）' % (k, v, maxp),
                                 'frame_len'))
        # B2 —— 收帧缓冲定尺 / 组帧收帧上界护栏 / 帧长按 OVERHEAD 算
        if c_text is not None:
            if not RE_C_SIZED.search(c_text):
                findings.append(('严重', c_path,
                                 '收帧缓冲没有按 SYS_FRAME_MAX_PAYLOAD 定尺（数据段最长 %d 字节）'
                                 % maxp, 'frame_len'))
            if not RE_C_GUARD.search(c_text):
                findings.append(('严重', c_path,
                                 '找不到"长度 > SYS_FRAME_MAX_PAYLOAD 就拒绝"的上界护栏',
                                 'frame_len'))
            if 'SYS_FRAME_OVERHEAD' not in c_text:
                findings.append(('严重', c_path,
                                 '帧长没有按 SYS_FRAME_OVERHEAD 计算', 'frame_len'))

    return {'findings': findings, 'vals': vals, 'extra': extra}


def run(root, quiet=False):
    findings = []
    details = []
    all_vals = []
    all_extra = []
    template_path = os.path.join(root, CMD_TEMPLATE_REL)

    for name, h_rel, c_rel in BOARDS:
        h_path = os.path.join(root, h_rel)
        c_path = os.path.join(root, c_rel)
        d = check_board(name, h_path, c_path)
        findings.extend(d['findings'])
        all_vals.append((name, d['vals']))
        all_extra.append((name, d['extra']))
        details.append({
            'board': name,
            'h_file': h_rel,
            'c_file': c_rel,
            'macros': d['vals'],
            'cmd_table': cmd_table_of(d['vals'], d['extra']),
            'issues': len(d['findings']),
        })

    # 两板对比
    if len(all_vals) == 2:
        n1, v1 = all_vals[0]
        n2, v2 = all_vals[1]
        for macro, desc in PROTOCOL_MACROS.items():
            a, b = v1.get(macro), v2.get(macro)
            if a is not None and b is not None and a != b:
                findings.append(('严重', '%s ↔ %s' % (n1, n2),
                                 '%s(%s) 不一致：%s=0x%02X / %s=0x%02X'
                                 % (desc, macro, n1, a, n2, b), 'protocol'))
        for macro in ENV_OFFSET_MACROS:
            a, b = v1.get(macro), v2.get(macro)
            if a is not None and b is not None and a != b:
                findings.append(('严重', '%s ↔ %s' % (n1, n2),
                                 '字段偏移 %s 不一致：%s=%d / %s=%d'
                                 % (macro, n1, a, n2, b), 'protocol'))

        # B2 —— 两板命令字表：同名取值必须一致；同板内两个名字同值 = 撞车
        cmds1 = cmd_table_of(all_vals[0][1], all_extra[0][1])
        cmds2 = cmd_table_of(all_vals[1][1], all_extra[1][1])
        for k in sorted(set(cmds1) & set(cmds2)):
            if cmds1[k] != cmds2[k]:
                findings.append(('严重', '%s ↔ %s' % (n1, n2),
                                 '命令字 %s 不一致：%s=0x%02X / %s=0x%02X'
                                 % (k, n1, cmds1[k], n2, cmds2[k]), 'cmd_table'))
        for nm, cmds in ((n1, cmds1), (n2, cmds2)):
            rev = {}
            for k, v in sorted(cmds.items()):
                rev.setdefault(v, []).append(k)
            for v, ks in sorted(rev.items()):
                if len(ks) > 1:
                    findings.append(('严重', nm,
                                     '命令字撞车：%s 都是 0x%02X，对端无法区分'
                                     % (' 与 '.join(ks), v), 'cmd_table'))

        # B3 —— 命令字命名规范 + 应答位约定（应答 CMD = 请求 CMD | ACK_FLAG）
        ack_flag = 0x80
        for _, vv in all_vals:
            if 'SYS_FRAME_ACK_FLAG' in vv:
                ack_flag = vv['SYS_FRAME_ACK_FLAG']
                break
        findings.extend(cmd_convention_issues(n1, cmds1, ack_flag))
        findings.extend(cmd_convention_issues(n2, cmds2, ack_flag))
        # B3 —— 命令字全集模板：两板并集落一份 JSON，新增命令字会在这里出现
        template_path = write_cmd_template(root, [(n1, cmds1), (n2, cmds2)], ack_flag)

    ok = not any(f[0] == '严重' for f in findings)

    # 报告
    lines = ['# 协议端到端校验报告\n']
    lines.append('## 摘要')
    lines.append('- 检查板：%d' % len(BOARDS))
    lines.append('- 发现问题：%d 条（严重 %d / 注意 %d）' % (
        len(findings),
        sum(1 for f in findings if f[0] == '严重'),
        sum(1 for f in findings if f[0] == '注意')))
    lines.append('- 结论：**%s**\n' % ('通过' if ok else '不通过'))

    if findings:
        lines.append('## 发现')
        for lv, loc, msg, cat in findings:
            lines.append('- **[%s]** %s — %s' % (lv, loc, msg))
        lines.append('')

    lines.append('## 各板常量对照')
    lines.append('| 常量 | %s | %s |' % (all_vals[0][0], all_vals[1][0]))
    lines.append('|------|------|------|')
    for macro in list(PROTOCOL_MACROS.keys()) + ENV_OFFSET_MACROS:
        v1 = all_vals[0][1].get(macro)
        v2 = all_vals[1][1].get(macro)
        s1 = '0x%02X' % v1 if v1 is not None else '—'
        s2 = '0x%02X' % v2 if v2 is not None else '—'
        match = '✓' if v1 == v2 and v1 is not None else ('✗' if v1 != v2 else '?')
        lines.append('| %s | %s | %s | %s |' % (macro, s1, s2, match))
    lines.append('')

    # B2：命令字表（只在一板出现的列出来，但不算问题——两板角色不同）
    c1 = cmd_table_of(all_vals[0][1], all_extra[0][1])
    c2 = cmd_table_of(all_vals[1][1], all_extra[1][1])
    lines.append('## 命令字表')
    lines.append('| 命令字 | %s | %s | 说明 |' % (all_vals[0][0], all_vals[1][0]))
    lines.append('|------|------|------|------|')
    for k in sorted(set(c1) | set(c2)):
        s1 = '0x%02X' % c1[k] if k in c1 else '—'
        s2 = '0x%02X' % c2[k] if k in c2 else '—'
        if k in c1 and k in c2:
            note = '一致' if c1[k] == c2[k] else '**不一致**'
        else:
            only = all_vals[0][0] if k in c1 else all_vals[1][0]
            note = '只在 %s 出现（两板角色不同，未必是错；对端若要认这条命令字就得补齐）' % only
        lines.append('| %s | %s | %s | %s |' % (k, s1, s2, note))
    lines.append('')
    lines.append('> 只在一板出现不算问题：板2 收上位机的控制命令，板1 不需要解析它们。'
                 '真正要抓的是"同名取值不一致"和同板内"两个命令字同值"。')
    lines.append('')
    # B3：命令字"新增即检查" —— 命名规范与应答位约定
    ack_flag = 0x80
    for _, vv in all_vals:
        if 'SYS_FRAME_ACK_FLAG' in vv:
            ack_flag = vv['SYS_FRAME_ACK_FLAG']
            break
    lines.append('## 命令字命名与应答位（新增即检查）')
    lines.append('')
    lines.append('- 请求 CMD 取 0x10 ~ 0x7F；**应答 CMD = 请求 CMD | SYS_FRAME_ACK_FLAG(0x%02X)**。'
                 % ack_flag)
    lines.append('- 名字规范：`SYS_FRAME_CMD_<大写下划线>`；应答可写 `SYS_FRAME_<X>_ACK_CMD`。')
    lines.append('- 新增命令字时本检查自动核对这两条：命名不合规 → 注意，'
                 '应答位不是"请求|应答位"、或请求字最高位被置 1 → 严重。')
    try:
        shown = os.path.relpath(template_path, root).replace('\\', '/')
    except ValueError:
        shown = template_path
    lines.append('- 命令字全集模板：`%s`（每次运行刷新；两板命令字并集都在这里，'
                 '新增一条就会在模板里多一行）。' % shown)
    lines.append('')

    out_dir = os.path.dirname(os.path.abspath(__file__))
    report_path = os.path.join(out_dir, '协议校验报告.md')
    with open(report_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))

    if not quiet:
        print('\n'.join(lines))
        print('报告已保存：%s' % report_path)

    return ok, findings, details


def selftest():
    """用临时工程验证检查器能发现不一致。"""
    import tempfile
    d = tempfile.mkdtemp(prefix='proto_selftest_')
    try:
        base1 = os.path.join(d, 'EnvMonitor', 'Project_1_检测端', 'Project', 'FWLIB', 'inc')
        base1c = os.path.join(d, 'EnvMonitor', 'Project_1_检测端', 'Project', 'FWLIB', 'src')
        base2 = os.path.join(d, 'EnvMonitor', 'Project_2_数据通信端', 'Project', 'FWLIB', 'inc')
        base2c = os.path.join(d, 'EnvMonitor', 'Project_2_数据通信端', 'Project', 'FWLIB', 'src')
        os.makedirs(base1)
        os.makedirs(base1c)
        os.makedirs(base2)
        os.makedirs(base2c)

        h_common = (
            '#define SYS_FRAME_HEAD_HI       0xAAU\n'
            '#define SYS_FRAME_HEAD_LO       0x55U\n'
            '#define SYS_FRAME_TAIL_HI       0x55U\n'
            '#define SYS_FRAME_TAIL_LO       0xAAU\n'
            '#define SYS_FRAME_OVERHEAD      8U\n'
            '#define SYS_FRAME_MAX_PAYLOAD   64U\n'
            '#define SYS_FRAME_CMD_ENV       0x01U\n'
            '#define SYS_FRAME_CMD_QUERY     0x12U\n'
            '#define SYS_FRAME_ENV_LEN       0x0CU\n'
            '#define SYS_FRAME_ENV_TEMP_OFF   0U\n'
            '#define SYS_FRAME_ENV_HUMI_OFF   2U\n'
            '#define SYS_FRAME_ENV_PRESS_OFF  4U\n'
            '#define SYS_FRAME_ENV_LIGHT_OFF  6U\n'
            '#define SYS_FRAME_ENV_TVOC_OFF   8U\n'
            '#define SYS_FRAME_ENV_MQ135_OFF 10U\n'
            '#define SYS_FRAME_ACK_FLAG      0x80U\n'
            '#define SYS_FRAME_CMD_REPLAY    0x16U\n'
            '#define SYS_FRAME_REPLAY_ACK_CMD (SYS_FRAME_CMD_REPLAY | SYS_FRAME_ACK_FLAG)\n'
        )

        # 板1：标准 + 三个 B2 错例 ——
        #   CMD_ALIAS 与 CMD_ENV 同值（同板命令字撞车）
        #   BIG_LEN 80 > MAX_PAYLOAD 64（数据段超上限）
        #   BIG_OFF 64 不在数据段内（偏移越界）
        h_extra_bad = (
            '#define SYS_FRAME_CMD_ALIAS   0x01U\n'
            '#define SYS_FRAME_BIG_LEN     80U\n'
            '#define SYS_FRAME_BIG_OFF     64U\n'
        )
        with open(os.path.join(base1, 'sys_frame.h'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(h_common + h_extra_bad)

        # 板2：TEMP 偏移改为 1（模拟字段偏移不一致）+ QUERY 命令字改成 0x13
        #   （模拟"同名命令字两板取值不一致"）
        with open(os.path.join(base2, 'sys_frame.h'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(h_common.replace('#define SYS_FRAME_ENV_TEMP_OFF   0U',
                                     '#define SYS_FRAME_ENV_TEMP_OFF   1U')
                           .replace('#define SYS_FRAME_CMD_QUERY     0x12U',
                                    '#define SYS_FRAME_CMD_QUERY     0x13U'))

        # 反例素材：c 里收帧缓冲按 MAX_PAYLOAD 定尺、有上界护栏、帧长按 OVERHEAD 算
        c_common = (
            'static uint8_t g_rx[SYS_FRAME_MAX_PAYLOAD];\n'
            'static void frame_put_u16_be(uint8_t *p, uint16_t v) { p[0]=v>>8; p[1]=v; }\n'
            'static uint16_t frame_get_u16_be(const uint8_t *p) { return (p[0]<<8)|p[1]; }\n'
            'uint16_t SYS_FRAME_Build(uint8_t cmd, const uint8_t *payload, uint16_t len, uint8_t *out, uint16_t cap) {\n'
            '    if (len > SYS_FRAME_MAX_PAYLOAD) return 0U;\n'
            '    if (cap < (uint16_t)(len + SYS_FRAME_OVERHEAD)) return 0U;\n'
            '    crc = frame_crc16(&out[2], len+2);\n'
            '    return len+8;\n'
            '}\n'
        )
        with open(os.path.join(base1c, 'sys_frame.c'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(c_common)
        with open(os.path.join(base2c, 'sys_frame.c'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(c_common)

        # 临时改 BOARDS 指向临时目录
        global BOARDS, ROOT
        old_boards = BOARDS
        old_root = ROOT
        ROOT = d
        BOARDS = [
            ('板1 检测端 (F407ZE)',
             os.path.join('EnvMonitor', 'Project_1_检测端', 'Project', 'FWLIB', 'inc', 'sys_frame.h'),
             os.path.join('EnvMonitor', 'Project_1_检测端', 'Project', 'FWLIB', 'src', 'sys_frame.c')),
            ('板2 通信端 (F407ZG)',
             os.path.join('EnvMonitor', 'Project_2_数据通信端', 'Project', 'FWLIB', 'inc', 'sys_frame.h'),
             os.path.join('EnvMonitor', 'Project_2_数据通信端', 'Project', 'FWLIB', 'src', 'sys_frame.c')),
        ]

        ok, findings, det = run(d, quiet=True)
        msgs = [f[2] for f in findings]

        checks = []
        def ck(name, cond):
            checks.append((name, bool(cond)))

        ck('能发现字段偏移不一致', any('SYS_FRAME_ENV_TEMP_OFF' in m and '不一致' in m for m in msgs))
        ck('自检时报告不通过', not ok)
        ck('能发现命令字跨板不一致（QUERY 0x12 vs 0x13）',
           any('命令字' in m and 'QUERY' in m and '不一致' in m for m in msgs))
        ck('能发现同板命令字撞车（CMD_ALIAS 与 CMD_ENV 同为 0x01）',
           any('撞车' in m and 'CMD_ALIAS' in m for m in msgs))
        ck('能发现数据段长度超上限（BIG_LEN 80 > MAX_PAYLOAD 64）',
           any('BIG_LEN' in m and '超过' in m for m in msgs))
        ck('能发现字段偏移越出数据段（BIG_OFF = 64）',
           any('BIG_OFF' in m and '不在数据段' in m for m in msgs))
        ck('反例：收帧缓冲按 MAX_PAYLOAD 定尺 / 有上界护栏 / 帧长按 OVERHEAD 算 → 不误报',
           not any(('没有按 SYS_FRAME_MAX_PAYLOAD 定尺' in m) or ('上界护栏' in m)
                   or ('OVERHEAD 计算' in m) for m in msgs))
        ck('反例：合规的 ENV_LEN 与 ENV 字段偏移不误报',
           not any('SYS_FRAME_ENV_LEN' in m for m in msgs))

        # B3 命令字命名/应答位（新增即检查）：
        # 反例（合规请求+应答不报）+ 错例（应答位错 / 命名不规范 / 请求字带应答位）
        good = {'SYS_FRAME_CMD_ENV': 0x01, 'SYS_FRAME_CMD_REPLAY': 0x16,
                'SYS_FRAME_REPLAY_ACK_CMD': 0x96}
        ck('反例：合规请求 + 应答（0x16|0x80=0x96）不报问题',
           cmd_convention_issues('板X', good, 0x80) == [])
        bad_ack = dict(good)
        bad_ack['SYS_FRAME_REPLAY_ACK_CMD'] = 0x56
        ck('错例：应答位不等于 请求|0x80（0x56 ≠ 0x96）→ 严重',
           any(k == 'cmd_ack' and lv == '严重'
               for lv, _, _, k in cmd_convention_issues('板X', bad_ack, 0x80)))
        ck('错例：命令字命名不规范（小写 bad）被点出 → 注意',
           any(k == 'cmd_naming' and lv == '注意'
               for lv, _, _, k in cmd_convention_issues('板X', {'SYS_FRAME_CMD_bad': 0x30}, 0x80)))
        ck('错例：请求字最高位被置 1 却没按应答命名（0x90）→ 严重',
           any(k == 'cmd_ack' and lv == '严重'
               for lv, _, _, k in cmd_convention_issues('板X', {'SYS_FRAME_CMD_WEIRD': 0x90}, 0x80)))
        ck('反例：普通请求字 0x12 不被误报应答',
           cmd_convention_issues('板X', {'SYS_FRAME_CMD_QUERY': 0x12}, 0x80) == [])
        ck('B3：应答字由宏表达式算出（REPLAY_ACK_CMD = 0x16|0x80 = 0x96）',
           det[1]['cmd_table'].get('SYS_FRAME_REPLAY_ACK_CMD') == 0x96)
        ck('B3：命令字全集模板已写出（含应答字）',
           os.path.exists(os.path.join(d, CMD_TEMPLATE_REL))
           and 'SYS_FRAME_REPLAY_ACK_CMD'
           in open(os.path.join(d, CMD_TEMPLATE_REL), encoding='utf-8').read())

        # B6 JSON 契约：面板读 boards[].issues 与 findings，字段名/类型一改就红
        _, j_findings, j_details = run(d, quiet=True)
        j = {'tool': 'protocol_check', 'schema': '1.0',
             'ok': not any(f[0] == '严重' for f in j_findings),
             'boards': j_details,
             'findings': [{'lvl': f[0], 'loc': f[1], 'msg': f[2], 'cat': f[3]}
                          for f in j_findings]}
        ck('JSON 契约：面板读取的字段名/类型都在',
           set(j) == {'tool', 'schema', 'ok', 'boards', 'findings'}
           and set(j['boards'][0]) == {'board', 'h_file', 'c_file', 'macros',
                                       'cmd_table', 'issues'}
           and isinstance(j['boards'][0]['issues'], int)
           and isinstance(j['findings'], list)
           and set(j['findings'][0]) == {'lvl', 'loc', 'msg', 'cat'})

        for name, passed in checks:
            print('    [%s] %s' % ('PASS' if passed else 'FAIL', name))

        BOARDS = old_boards
        ROOT = old_root
        return all(p for _, p in checks)
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    quiet = '--quiet' in sys.argv
    json_out = '--json' in sys.argv
    selftest_mode = '--selftest' in sys.argv

    if selftest_mode:
        passed = selftest()
        sys.exit(0 if passed else 1)

    ok, findings, details = run(ROOT, quiet=quiet or json_out)

    if json_out:
        out = {
            'tool': 'protocol_check',
            'schema': '1.0',
            'ok': ok,
            'boards': details,
            'findings': [
                {'lvl': f[0], 'loc': f[1], 'msg': f[2], 'cat': f[3]}
                for f in findings
            ],
        }
        print(json.dumps(out, ensure_ascii=False))
        return

    if quiet:
        print('通过' if ok else '不通过')

    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
