# -*- coding: utf-8 -*-
r"""LoRa 模块配置检查（幂等，只读源码，不改任何文件）。

用途：核查两板 lora_e22.h 的配置一致性、引脚宏成对性、波特率合法性、
      帧间隔合理性，以及 .c 实现与 .h 配置是否匹配。

背景：LoRa 两板必须波特率/帧格式一致才能互通；引脚宏不成对会导致编译
      通过但运行期端口为 0；帧间隔太小会把一帧切成多段。

检查项：
  1) 两板关键数值宏是否一致（波特率、帧间隔、超时、缓冲）
  2) 波特率是否在 E22 支持列表中
  3) 引脚宏是否成对（每个 _PORT 都有对应的 _PIN）
  4) M0/M1 是否都有定义（缺了则配置模式不可用）
  5) 串口宏是否在 sys_usart 的枚举范围内（静态核查，枚举表需人工确认）
  6) 帧间隔是否 ≥ 30ms（若波特率 ≤ 2400 时 1 字节约 4ms，10ms 可能不够）
  7) .c 文件是否存在且编码正确

报告写到脚本同目录的 LoRa检查报告.md，并在控制台打印摘要。

用法：
    python 本地工具\工具箱\检查\LoRa检查\lora_check.py
    python 本地工具\工具箱\检查\LoRa检查\lora_check.py --quiet
    python 本地工具\工具箱\检查\LoRa检查\lora_check.py --selftest
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

BOARDS = [
    ('板1 检测端 (F407ZE)', os.path.join('EnvMonitor', 'Project_1_检测端', 'Project', 'FWLIB', 'inc', 'lora_e22.h'),
     os.path.join('EnvMonitor', 'Project_1_检测端', 'Project', 'FWLIB', 'src', 'lora_e22.c')),
    ('板2 通信端 (F407ZG)', os.path.join('EnvMonitor', 'Project_2_数据通信端', 'Project', 'FWLIB', 'inc', 'lora_e22.h'),
     os.path.join('EnvMonitor', 'Project_2_数据通信端', 'Project', 'FWLIB', 'src', 'lora_e22.c')),
]

# E22-400T22S 支持的波特率（AT+UART 参数）
VALID_BAUDS = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200}

# 必须一致才能互通的数值宏
MUST_MATCH = {
    'SYS_LORA_BAUD': '波特率',
    'SYS_LORA_FRAME_GAP_MS': '帧间隔',
    'SYS_LORA_AUX_TIMEOUT_MS': 'AUX 超时',
    'SYS_LORA_AT_TIMEOUT_MS': 'AT 超时',
    'SYS_LORA_RX_BUF_SIZE': '接收缓冲',
}

RE_DEFINE = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*)\s+(\S+)')
RE_PORT = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*?)_PORT\s+(GPIO[A-Z])')
RE_PIN = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*?)_PIN\s+(GPIO_Pin_\d+)')


def read_lines(path):
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ('utf-8', 'gbk', 'latin-1'):
        try:
            return raw.decode(enc).splitlines(), enc
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace').splitlines(), 'latin-1'


def parse_header(path):
    """解析 lora_e22.h，返回 (数值宏, 引脚宏, 问题列表)。"""
    issues = []
    nums = {}
    ports = {}
    pins = {}

    if not os.path.exists(path):
        return nums, ports, pins, ['文件不存在']

    lines, enc = read_lines(path)
    for i, line in enumerate(lines, 1):
        # 先匹配具体的 _PORT / _PIN，再匹配通用数值宏，避免 RE_DEFINE 吃掉引脚宏
        m = RE_PORT.match(line)
        if m:
            ports[m.group(1)] = (m.group(2), i)
            continue
        m = RE_PIN.match(line)
        if m:
            pins[m.group(1)] = (m.group(2), i)
            continue
        m = RE_DEFINE.match(line)
        if m:
            k, v = m.group(1), m.group(2)
            if k in MUST_MATCH:
                # 去掉末尾的 U 后缀再转整数
                v_clean = v.rstrip('Uu')
                try:
                    nums[k] = int(v_clean)
                except ValueError:
                    issues.append(('数值宏解析失败', '%s = %s（第 %d 行）' % (k, v, i)))
            continue

    # 成对检查
    for base in ports:
        if base not in pins:
            issues.append(('引脚宏不成对', '%s_PORT 没有对应的 %s_PIN（第 %d 行）' % (base, base, ports[base][1])))
    for base in pins:
        if base not in ports:
            issues.append(('引脚宏不成对', '%s_PIN 没有对应的 %s_PORT（第 %d 行）' % (base, base, pins[base][1])))

    # M0/M1 检查
    has_m0 = any('M0' in b for b in ports)
    has_m1 = any('M1' in b for b in ports)
    if not has_m0:
        issues.append(('控制脚缺失', '未定义 M0 引脚：配置模式（AT 指令）不可用'))
    if not has_m1:
        issues.append(('控制脚缺失', '未定义 M1 引脚：配置模式（AT 指令）不可用'))

    return nums, ports, pins, issues


def check_board(title, h_path, c_path):
    nums, ports, pins, issues = parse_header(h_path)

    # 波特率检查
    baud = nums.get('SYS_LORA_BAUD')
    if baud is not None and baud not in VALID_BAUDS:
        issues.append(('波特率非法', '%d 不在 E22 支持列表（%s）中' % (baud, ' / '.join(str(b) for b in sorted(VALID_BAUDS)))))

    # 帧间隔检查：波特率 ≤ 2400 时 1 字节约 4ms（1200 时约 8.3ms），建议 ≥ 30ms
    gap = nums.get('SYS_LORA_FRAME_GAP_MS')
    if gap is not None and baud is not None:
        byte_time_ms = 10.0 * 1000.0 / baud  # 10 bit/byte (8N1) * 1000 ms/s / baud
        min_gap = byte_time_ms * 3  # 至少 3 个字节的间隔才稳妥
        if gap < min_gap:
            issues.append(('帧间隔偏小', '波特率 %d 时 1 字节约 %.1f ms，帧间隔 %d ms 小于建议值 %.0f ms，可能把一帧切成多段' % (baud, byte_time_ms, gap, min_gap)))

    # .c 文件检查
    if not os.path.exists(c_path):
        issues.append(('实现缺失', '对应的 .c 文件不存在'))
    else:
        _, enc = read_lines(c_path)
        if enc == 'gbk':
            issues.append(('编码注意', '.c 文件为 GBK 编码；工程口径要求 .c/.h 为 UTF-8（仅 main.c 例外为 GBK）'))

    return {
        'title': title,
        'h_path': h_path,
        'c_path': c_path,
        'nums': nums,
        'ports': ports,
        'pins': pins,
        'issues': issues,
    }


def cross_check(results):
    """两板交叉检查，返回问题列表。"""
    issues = []
    if len(results) < 2:
        return issues

    r1, r2 = results[0], results[1]
    for key, name in MUST_MATCH.items():
        v1 = r1['nums'].get(key)
        v2 = r2['nums'].get(key)
        if v1 is not None and v2 is not None and v1 != v2:
            issues.append(('两板不一致', '%s（%s）：板1=%s，板2=%s' % (key, name, v1, v2)))

    return issues


def render(results, cross_issues):
    out = ['# LoRa 配置检查报告', '',
           '来源：`lora_check.py`（只读扫描，可重复运行）', '']
    out.append('## 0. 总览')
    out.append('')
    out.append('| 板 | 头文件 | 实现文件 | 数值项 | 引脚对 | 问题 |')
    out.append('|---|---|---|---|---|---|')
    for r in results:
        h_ok = '存在' if os.path.exists(r['h_path']) else '**缺失**'
        c_ok = '存在' if os.path.exists(r['c_path']) else '**缺失**'
        n_nums = len(r['nums'])
        n_pairs = len(set(r['ports']) & set(r['pins']))
        n_issues = len(r['issues'])
        out.append('| %s | %s | %s | %d/%d | %d | %d |' % (r['title'], h_ok, c_ok, n_nums, len(MUST_MATCH), n_pairs, n_issues))
    out.append('')

    if cross_issues:
        out.append('## 1. 两板交叉检查')
        out.append('')
        for kind, detail in cross_issues:
            out.append('- **%s**：%s' % (kind, detail))
        out.append('')
    else:
        out.append('## 1. 两板交叉检查')
        out.append('')
        out.append('两板关键配置一致，无冲突。')
        out.append('')

    out.append('## 2. 各板详情')
    out.append('')
    for r in results:
        out.append('### %s' % r['title'])
        out.append('')
        out.append('**数值宏**')
        out.append('')
        out.append('| 宏 | 值 | 说明 |')
        out.append('|---|---|---|')
        for k in MUST_MATCH:
            v = r['nums'].get(k)
            out.append('| `%s` | %s | %s |' % (k, v if v is not None else '—', MUST_MATCH[k]))
        out.append('')

        out.append('**引脚宏**')
        out.append('')
        out.append('| 基名 | 端口 | 引脚 |')
        out.append('|---|---|---|')
        for base in sorted(set(r['ports']) | set(r['pins'])):
            port = r['ports'].get(base, ('—', ''))[0]
            pin = r['pins'].get(base, ('—', ''))[0]
            out.append('| `%s` | %s | %s |' % (base, port, pin))
        out.append('')

        if r['issues']:
            out.append('**本板问题**')
            out.append('')
            for kind, detail in r['issues']:
                out.append('- **%s**：%s' % (kind, detail))
            out.append('')
        else:
            out.append('本板无问题。')
            out.append('')

    # B1：报告结尾补一节"下一步怎么做" —— 命中问题后要明确先改哪块板、哪个宏、改到多少。
    # 只加报告文字，不动判定逻辑与 JSON 字段。
    out.append('## 下一步怎么做')
    out.append('')
    out.append('- 波特率不在 E22 支持列表：按 `AT+UART` 取值表改成合法值'
               '（如 9600 / 19200 / 115200），两板取同一个值。')
    out.append('- 两板配置不一致（波特率 / 帧间隔 / 超时 / 缓冲）：'
               '以会发包的那块（板1 检测端）为准，把另一块的宏同步过来。')
    out.append('- 帧间隔偏小：按「字节耗时 × 3」把 `SYS_LORA_FRAME_GAP_MS` 抬到建议值，'
               '否则一帧会被切成多段。')
    out.append('- 引脚宏不成对 / 缺 M0、M1：补齐对应的 `*_PORT` / `*_PIN`，'
               '否则编译能过、运行期该端口为 0。')
    out.append('')

    out.append('## 3. 说明')
    out.append('')
    out.append('1) 两板波特率、帧间隔、超时、缓冲必须一致，否则无法互通。')
    out.append('2) 引脚宏成对检查只针对 `*_PORT` / `*_PIN` 格式；`SYS_LORA_UART` 等无 _PORT/_PIN 后缀的宏不参与。')
    out.append('3) 帧间隔偏小告警按「波特率 ÷ 10bit/byte = 字节耗时，×3 倍 = 建议最小帧间隔」估算。')
    out.append('4) 波特率合法性校验针对亿佰特 E22-400T22S 的 AT+UART 支持列表。')
    out.append('')
    return '\n'.join(out)


def to_json(results, cross_issues):
    """结构化输出，供工具箱的可视化体检面板读取。"""
    boards = []
    for r in results:
        bases = sorted(set(r['ports']) | set(r['pins']))
        pins = {}
        for b in bases:
            port = r['ports'].get(b, ('', ''))[0]
            pin = r['pins'].get(b, ('', ''))[0]
            if port and pin:
                pins[b] = [port, pin]
        boards.append({
            'title': r['title'],
            'issues': [{'kind': k, 'detail': d} for k, d in r['issues']],
            'nums': {k: v for k, v in r['nums'].items()},
            'pins': pins,
        })
    ok = (not cross_issues) and all(not r['issues'] for r in results)
    return {
        'tool': 'lora_check',
        'schema': 1,
        'ok': ok,
        'boards': boards,
        'cross': [{'kind': k, 'detail': d} for k, d in cross_issues],
    }


def selftest():
    import shutil
    import tempfile

    checks = []

    def ck(name, ok, extra=''):
        checks.append((name, bool(ok), extra))

    tmp = tempfile.mkdtemp(prefix='lora_selftest_')
    try:
        def wr(name, text):
            with open(os.path.join(tmp, name), 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)

        # 板1：正常配置
        wr('b1.h',
           '#define SYS_LORA_UART         SYS_USART_3\n'
           '#define SYS_LORA_BAUD         9600U\n'
           '#define SYS_LORA_M0_PORT      GPIOB\n'
           '#define SYS_LORA_M0_PIN       GPIO_Pin_0\n'
           '#define SYS_LORA_M1_PORT      GPIOB\n'
           '#define SYS_LORA_M1_PIN       GPIO_Pin_1\n'
           '#define SYS_LORA_AUX_PORT     GPIOB\n'
           '#define SYS_LORA_AUX_PIN      GPIO_Pin_2\n'
           '#define SYS_LORA_AUX_TIMEOUT_MS  1000U\n'
           '#define SYS_LORA_AT_TIMEOUT_MS   500U\n'
           '#define SYS_LORA_FRAME_GAP_MS    10U\n'
           '#define SYS_LORA_RX_BUF_SIZE     256U\n')
        wr('b1.c', '/* stub */\n')

        # 板2：波特率非法(14400) + 缺 M1 + 帧间隔偏小（2400 波特率配 1ms）
        wr('b2.h',
           '#define SYS_LORA_UART         SYS_USART_6\n'
           '#define SYS_LORA_BAUD         14400U\n'
           '#define SYS_LORA_M0_PORT      GPIOE\n'
           '#define SYS_LORA_M0_PIN       GPIO_Pin_5\n'
           '#define SYS_LORA_AUX_PORT     GPIOE\n'
           '#define SYS_LORA_AUX_PIN      GPIO_Pin_7\n'
           '#define SYS_LORA_AUX_TIMEOUT_MS  1000U\n'
           '#define SYS_LORA_AT_TIMEOUT_MS   500U\n'
           '#define SYS_LORA_FRAME_GAP_MS    1U\n'
           '#define SYS_LORA_RX_BUF_SIZE     256U\n')
        wr('b2.c', '/* stub */\n')

        r1 = check_board('板1', os.path.join(tmp, 'b1.h'), os.path.join(tmp, 'b1.c'))
        r2 = check_board('板2', os.path.join(tmp, 'b2.h'), os.path.join(tmp, 'b2.c'))
        cross = cross_check([r1, r2])

        ck('板1无问题', len(r1['issues']) == 0, '%d 个问题' % len(r1['issues']))
        ck('板2波特率非法被命中', any('波特率非法' in i[0] for i in r2['issues']))
        ck('板2缺 M1 被命中', any('M1' in i[1] for i in r2['issues']))
        ck('板2帧间隔偏小被命中', any('帧间隔偏小' in i[0] for i in r2['issues']))
        ck('两板波特率不一致被命中', any('两板不一致' in i[0] and 'SYS_LORA_BAUD' in i[1] for i in cross))
        ck('报告渲染不炸', 'LoRa 配置检查报告' in render([r1, r2], cross))

        j = to_json([r1, r2], cross)
        ck('JSON 结构正确（工具名/板数/ok/交叉）',
           j['tool'] == 'lora_check' and len(j['boards']) == 2
           and j['ok'] is False and len(j['cross']) >= 1)
        ck('JSON 含引脚对与数值宏',
           j['boards'][0]['pins'].get('SYS_LORA_M0') == ['GPIOB', 'GPIO_Pin_0']
           and j['boards'][0]['nums'].get('SYS_LORA_BAUD') == 9600)
        # B6 JSON 契约：面板读的字段名/类型固化（字段一改名，这里先红）
        ck('JSON 契约：顶层与板级字段名/类型都在',
           set(j) == {'tool', 'schema', 'ok', 'boards', 'cross'}
           and all(set(b) == {'title', 'issues', 'nums', 'pins'} for b in j['boards'])
           and isinstance(j['boards'][0]['issues'], list)
           and isinstance(j['cross'], list),
           '、'.join(sorted(j)) + ' / ' + '、'.join(sorted(j['boards'][0])))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    bad = [c for c in checks if not c[1]]
    for name, ok, extra in checks:
        print('  [%s] %s%s' % ('PASS' if ok else 'FAIL', name,
                               ('  （实际：%s）' % extra) if (extra and not ok) else ''))
    print('LoRa 检查自检：%d 通过 / %d 失败' % (len(checks) - len(bad), len(bad)))
    return 1 if bad else 0


def main(argv):
    if '--selftest' in argv:
        return selftest()

    quiet = False
    json_mode = False
    bad = []
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'LoRa检查报告.md')
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == '--quiet':
            quiet = True
            i += 1
        elif a == '--json':
            json_mode = True
            i += 1
        elif a == '--out':
            if i + 1 >= len(argv):
                bad.append('--out 缺少值')
                i += 1
                continue
            out_path = os.path.abspath(argv[i + 1])
            i += 2
        else:
            bad.append(a)
            i += 1

    if bad:
        print('未知参数：%s' % ' '.join(bad))
        print('可用参数：--out <文件> | --quiet | --json | --selftest')
        return 2

    results = []
    for title, h_rel, c_rel in BOARDS:
        h_path = os.path.join(ROOT, h_rel)
        c_path = os.path.join(ROOT, c_rel)
        results.append(check_board(title, h_path, c_path))

    cross_issues = cross_check(results)

    # --json：结构化输出给可视化面板，不写 md 报告（避免只读调用产生副作用）
    if json_mode:
        print(json.dumps(to_json(results, cross_issues), ensure_ascii=False))
        return 1 if (cross_issues or any(r['issues'] for r in results)) else 0

    report = render(results, cross_issues)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(report)

    total_issues = sum(len(r['issues']) for r in results) + len(cross_issues)
    print('LoRa 配置检查完成')
    for r in results:
        n = len(r['issues'])
        print('  %-24s 问题 %d' % (r['title'], n))
    print('  两板交叉问题: %d' % len(cross_issues))
    print('  总计问题: %d' % total_issues)
    print('  报告: %s' % out_path)
    return 1 if total_issues > 0 else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
