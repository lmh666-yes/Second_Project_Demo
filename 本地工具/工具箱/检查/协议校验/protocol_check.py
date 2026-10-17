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

RE_DEFINE_HEX = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*)\s+(0x[0-9A-Fa-f]+|\d+)U?')
RE_DEFINE_NUM = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*)\s+(\d+)U?')

# B2：命令字 / 帧数据段长度 / 字段偏移 三类宏（用于命令字表对照与上限核算）
RE_CMD_NAME = re.compile(r'^SYS_FRAME_(?:CMD_\w+|\w+_CMD)$')
RE_LEN_NAME = re.compile(r'^SYS_FRAME_\w+_LEN$')
RE_OFF_NAME = re.compile(r'^SYS_FRAME_\w+_OFF$')
# 收帧缓冲按 MAX_PAYLOAD 定尺（如 data[SYS_FRAME_MAX_PAYLOAD]）
RE_C_SIZED = re.compile(r'\[[^\]]*SYS_FRAME_MAX_PAYLOAD[^\]]*\]')
# 长度上界护栏：if (len > SYS_FRAME_MAX_PAYLOAD) 这类比较
RE_C_GUARD = re.compile(r'if\s*\([^)]{0,120}SYS_FRAME_MAX_PAYLOAD')


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


def parse_header(text):
    """解析 sys_frame.h，返回 (关键常量, B2 宏表, 问题列表)。

    B2 宏表收三类：命令字（SYS_FRAME_CMD_* / SYS_FRAME_*_CMD）、
    帧数据段长度（SYS_FRAME_*_LEN）、字段偏移（SYS_FRAME_*_OFF）。
    取值写成宏表达式（如 ACK_CMD = CMD_REPLAY | ACK_FLAG）的，静态算不出数值，
    直接跳过不猜。"""
    vals = {}
    extra = {}
    issues = []
    for i, line in enumerate(text.splitlines(), 1):
        m = RE_DEFINE_HEX.match(line)
        if not m:
            m = RE_DEFINE_NUM.match(line)
        if not m:
            continue
        k, v = m.group(1), m.group(2)
        try:
            num = int(v, 0)
        except ValueError:
            issues.append(('宏解析失败', '%s = %s（第 %d 行）' % (k, v, i)))
            continue
        if k in PROTOCOL_MACROS or k in ENV_OFFSET_MACROS:
            vals[k] = num
        elif RE_CMD_NAME.match(k) or RE_LEN_NAME.match(k) or RE_OFF_NAME.match(k):
            extra[k] = num
    return vals, extra, issues


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

        ok, findings, _ = run(d, quiet=True)
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
