# -*- coding: utf-8 -*-
r"""进度与桩跟踪器（只读源码，不改任何文件）。

背景：当前仓库处于"阶段骨架 + 桩函数"状态 —— 两份 main.c 是骨架、
      Qt 的部分业务类 .cpp 是桩函数、三份进度文档为空。缺一份能回答
      "还有哪些没实现"的清单，也缺让进展每天可见的反馈。

本脚本扫描源码里的待办标记与疑似桩函数，输出：

  1) 待办标记清单（TODO / FIXME / 待实现 / 未实现 / 待补充 ...），带行号与原文；
  2) 疑似桩函数清单，分为"待实现"与"注释说明为有意留空"两栏；
  3) 三份进度文档的现状（是否为空）；
  4) 总览统计。

报告写到脚本同目录的 进度报告.md，并在控制台打印摘要。只读，不改任何文件。

跳过第三方与产物目录（FreeRTOS / ETH / RTE / qcustomplot / build 等），
避免把第三方库里的 TODO 当成自己的待办。

用法：
    python 本地工具\工具箱\检查\进度跟踪\progress_scan.py
    python 本地工具\工具箱\检查\进度跟踪\progress_scan.py --root d:\GitHub_Project --quiet
    python 本地工具\工具箱\检查\进度跟踪\progress_scan.py --selftest
（--selftest 在临时目录造合成工程核对判定，不写报告、不动趋势快照。）
"""
import datetime
import json
import os
import re
import sys

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass

# 仓库根：自适应向上查找，与工具放在哪一层无关
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

SCAN_EXT = {'.c', '.h', '.cpp', '.hpp', '.cc', '.hh'}
EXCLUDE_DIR = {
    '.git', '.venv', '.vscode', '__pycache__', 'node_modules',
    'FreeRTOS', 'ETH', 'RTE', 'Objects', 'Listings',
    'build', 'build_verify', '.qtcreator', '.gradle', 'android',
    '本地工具', '_参考实现备份', '_diagram_tmp', 'qcustomplot',
    # Linux-32：Linux + STM32 自学试验场，工程由 EnvMonitor 复制而来、尚未改写，
    # 与 .gitignore 的「整体不入库」口径保持一致，不计入进度统计。
    'Linux-32',
}
EXCLUDE_FILE = re.compile(r'(^qcustomplot\.|^moc_|^ui_|^rcc_)')

# 待办标记：ASCII 一类，中文一类
MARKERS = [
    ('TODO', re.compile(r'\bTODO\b', re.I)),
    ('FIXME', re.compile(r'\bFIXME\b', re.I)),
    ('XXX', re.compile(r'\bXXX\b', re.I)),
    ('HACK', re.compile(r'\bHACK\b', re.I)),
    ('TBD', re.compile(r'\bTBD\b', re.I)),
    ('待实现', re.compile(r'待实现')),
    ('未实现', re.compile(r'未实现')),
    ('待补充', re.compile(r'待补充')),
    ('待完成', re.compile(r'待完成')),
    ('待接入', re.compile(r'待接入')),
    ('占位', re.compile(r'占位')),
    ('空桩', re.compile(r'空桩')),
]

# 紧跟函数体左括号之前、允许出现的限定符（去掉空白后比对）
TRAIL_OK = {
    '', 'const', 'noexcept', 'constnoexcept', 'override', 'final', 'mutable',
    'constoverride', 'noexceptoverride', 'finaloverride', 'constfinal',
}
# 注释里出现这些词时，空函数多半是有意留空，而不是待实现
NOOP_HINT = ['留空', '空操作', '默认态已满足', '无需', '暂无', 'no-op']

# 疑似函数定义里，紧跟 ( 之前的这些词不是函数名（控制语句）
KEYWORDS = {
    'if', 'for', 'while', 'switch', 'do', 'else', 'return', 'sizeof',
    'catch', 'case', 'default', 'typeof', 'defined', 'static_assert',
}

PROGRESS_DOCS = [
    os.path.join('EnvMonitor', 'Project_1_检测端', '板1进度文档.md'),
    os.path.join('EnvMonitor', 'Project_2_数据通信端', '板2进度文档.md'),
    os.path.join('EnvMonitor', 'QT_project', 'QT上位机进度文档.md'),
]


def read_text(path):
    """按 utf-8 / gbk 顺序解码，返回 (文本, 编码名)。解码失败回退 latin-1。"""
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ('utf-8', 'gbk', 'latin-1'):
        try:
            return raw.decode(enc), enc
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace'), 'latin-1'


def strip_comments(s):
    """把注释与字符串内容替换成空格（保留换行），长度不变。"""
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


def header_before(masked, s):
    """从函数体左括号 s 往前找函数名与名字起始位置。

    处理两种容易误判的写法：
      - C++ 构造函数初始化列表 `: Base(x), m_a(y) {`，一路往前跳到真正的函数名；
      - `#define X() do { } while (0)` 这类宏，用调用行是否以 # 开头排除。
    找不到返回 None。
    """
    n = len(masked)
    pos = s
    for _ in range(16):
        j = pos - 1
        while j >= 0 and masked[j] in ' \t\r\n':
            j -= 1
        # 往前找参数表右括号，允许中间夹 const / noexcept / 尾置类型等限定符
        rp, k, steps = None, j, 0
        while k >= 0 and steps < 40:
            c = masked[k]
            if c == ')':
                rp = k
                break
            if c.isalnum() or c == '_' or c in ' \t\r\n':
                k -= 1
                steps += 1
                continue
            break
        if rp is None:
            return None
        # 右括号与体左括号之间只允许限定符（排除 `do {` 这类误判）
        bns = re.sub(r'\s+', '', masked[rp + 1:pos])
        if bns not in TRAIL_OK and not bns.startswith('->'):
            return None
        # 配平到左括号
        depth, p, lp = 0, rp, None
        while p >= 0:
            if masked[p] == ')':
                depth += 1
            elif masked[p] == '(':
                depth -= 1
                if depth == 0:
                    lp = p
                    break
            p -= 1
        if lp is None:
            return None
        # 左括号前的标识符
        t = lp - 1
        while t >= 0 and masked[t] in ' \t\r\n':
            t -= 1
        if t < 0 or not (masked[t].isalnum() or masked[t] == '_'):
            return None
        nend = t
        while t >= 0 and (masked[t].isalnum() or masked[t] == '_'):
            t -= 1
        name = masked[t + 1:nend + 1]
        if name in KEYWORDS:
            return None
        # 名字前一个非空字符：决定是不是真正的函数名
        u = t
        while u >= 0 and masked[u] in ' \t\r\n':
            u -= 1
        if u < 0:
            return None
        prev = masked[u]
        if prev == ',':
            pos = u          # 初始化列表元素，继续往前
            continue
        if prev == ':':
            if u - 1 >= 0 and masked[u - 1] == ':':
                return (name, t + 1)    # Foo::Foo 这种限定名，就是它
            pos = u          # 初始化列表的冒号，继续往前
            continue
        if prev.isalnum() or prev == '_' or prev in '*&>~':
            return (name, t + 1)
        return None
    return None


def find_functions(masked):
    """返回 [(函数名, 名字起始下标, 行号, 体左括号, 体右括号)]。"""
    regions = []
    stack = []
    for i, ch in enumerate(masked):
        if ch == '{':
            stack.append(i)
        elif ch == '}' and stack:
            regions.append((stack.pop(), i))

    funcs = []
    for s, e in regions:
        # 宏定义行不当作函数
        line_start = masked.rfind('\n', 0, s) + 1
        if masked[line_start:s].lstrip().startswith('#'):
            continue
        h = header_before(masked, s)
        if h is None:
            continue
        name, name_at = h
        # 名字所在行的首字符是 #，判为宏定义，跳过
        ns = masked.rfind('\n', 0, name_at) + 1
        if masked[ns:name_at].lstrip().startswith('#'):
            continue
        line = masked.count('\n', 0, name_at) + 1
        funcs.append((name, name_at, line, s, e))
    return funcs


def stub_reason(masked_body):
    """判断函数体是不是桩：返回 '空函数体' / '只有一句 return' / None。"""
    code = re.sub(r'\s+', '', masked_body)
    if code == '' or code == ';':
        return '空函数体'
    m = re.fullmatch(r'return([^;]*);', code)
    if m:
        expr = m.group(1)
        if expr in ('', '0', '1', '-1', 'NULL', 'nullptr', 'false', 'true',
                    '{}', '{0}', '{0.0}', '0.0'):
            return '只有一句 return'
    return None


def iter_sources(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIR]
        for fn in filenames:
            if EXCLUDE_FILE.match(fn):
                continue
            if os.path.splitext(fn)[1].lower() in SCAN_EXT:
                yield os.path.join(dirpath, fn)


def rel(path):
    try:
        return os.path.relpath(path, ROOT)
    except ValueError:
        return path


def scan(root):
    markers = []            # (相对路径, 行号, 标记名, 原文)
    stubs = []              # (相对路径, 行号, 函数名, 原因, 含标记)
    enc_count = {}
    file_count = 0

    for path in iter_sources(root):
        text, enc = read_text(path)
        enc_count[enc] = enc_count.get(enc, 0) + 1
        file_count += 1
        r = rel(path)

        marker_lines = set()
        for ln, line in enumerate(text.splitlines(), 1):
            for name, rx in MARKERS:
                if rx.search(line):
                    markers.append((r, ln, name, line.strip()))
                    marker_lines.add(ln)
                    break

        masked = strip_comments(text)
        for name, name_at, line, s, e in find_functions(masked):
            reason = stub_reason(masked[s + 1:e])
            if not reason:
                continue
            has_marker = any(ln in marker_lines for ln in range(max(1, line - 6), line + 1))
            # 函数上方几行的注释里若说明"有意留空"，归到另一栏。
            # 注意 rfind 在函数名位于文件头 400 字节内时会返回 -1，此时要退到 0，
            # 否则 ctx 会取成 text[-1:name_at]（空串），文件开头的函数就永远看不到这条注释。
            ctx_start = masked.rfind('\n', 0, max(0, name_at - 400))
            if ctx_start < 0:
                ctx_start = 0
            ctx = text[ctx_start:name_at]
            noop = any(k in ctx for k in NOOP_HINT)
            stubs.append((r, line, name, reason, has_marker, noop))

    return markers, stubs, enc_count, file_count


def snapshot_path():
    """历史快照落在工具自身目录下，不污染仓库。"""
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), '_progress_history.json')


def load_prev():
    """读上一次的快照。没有或读不动都返回 None，不影响本次扫描。"""
    p = snapshot_path()
    if not os.path.exists(p):
        return None
    try:
        with open(p, 'r', encoding='utf-8') as f:
            return json.load(f)
    except Exception:
        return None


def save_snapshot(markers, stubs):
    """记录本次计数与时间，供下次对比。最多保留最近 30 次。"""
    todo = [s for s in stubs if not s[5]]
    cur = {
        'time': datetime.datetime.now().strftime('%Y-%m-%d %H:%M'),
        'markers': len(markers),
        'stubs': len(todo),
        'noop': len(stubs) - len(todo),
    }
    hist = load_prev()
    if not isinstance(hist, list):
        hist = []
    hist.append(cur)
    hist = hist[-30:]
    try:
        with open(snapshot_path(), 'w', encoding='utf-8', newline='\n') as f:
            json.dump(hist, f, ensure_ascii=False, indent=2)
    except Exception:
        pass
    return cur, (hist[-2] if len(hist) >= 2 else None)


def delta_text(now, prev):
    """把两个数比较成一栏文本，带方向。减少是好，所以用「↓」。"""
    if prev is None:
        return '—'
    d = now - prev
    if d == 0:
        return '持平'
    return ('↓ 减少 %d' % abs(d)) if d < 0 else ('↑ 增加 %d' % d)


def render(root, markers, stubs, enc_count, file_count, cur=None, prev=None):
    out = []
    out.append('# 进度报告')
    out.append('')
    out.append('来源：`本地工具/工具箱/检查/进度跟踪/progress_scan.py`（只读扫描，可重复运行）')
    out.append('')
    out.append('| 项 | 值 |')
    out.append('|---|---|')
    out.append('| 扫描根 | `%s` |' % root)
    out.append('| 源码文件数 | %d |' % file_count)
    out.append('| 编码分布 | %s |' % '，'.join('%s x%d' % (k, v) for k, v in sorted(enc_count.items())))
    out.append('| 待办标记 | %d 处 |' % len(markers))
    out.append('| 疑似桩函数 | %d 个 |' % len(stubs))
    out.append('')

    # 与上次对比：把数字变成趋势，"有没有在推进"一眼可见
    if cur is not None:
        out.append('## 0. 较上次')
        out.append('')
        if prev is None:
            out.append('本次为首次记录，暂无对比基线。下次运行即可看到变化。')
            out.append('')
        else:
            out.append('| 指标 | 上次（%s） | 本次（%s） | 变化 |' % (prev.get('time', '-'), cur.get('time', '-')))
            out.append('|---|---|---|---|')
            out.append('| 待办标记 | %s | %s | %s |'
                       % (prev.get('markers', '-'), cur.get('markers', '-'),
                          delta_text(cur.get('markers', 0), prev.get('markers'))))
            out.append('| 待实现桩 | %s | %s | %s |'
                       % (prev.get('stubs', '-'), cur.get('stubs', '-'),
                          delta_text(cur.get('stubs', 0), prev.get('stubs'))))
            out.append('| 有意留空 | %s | %s | %s |'
                       % (prev.get('noop', '-'), cur.get('noop', '-'),
                          delta_text(cur.get('noop', 0), prev.get('noop'))))
            out.append('')
            out.append('注：待办与桩减少代表在推进；数字不变不代表没干活——也可能这轮写的是新功能、只增不减。')
            out.append('')

    out.append('## 1. 待办标记（按文件）')
    out.append('')
    if not markers:
        out.append('无。')
        out.append('')
    else:
        by_file = {}
        for item in markers:
            by_file.setdefault(item[0], []).append(item)
        for f in sorted(by_file):
            out.append('### %s' % f)
            out.append('')
            out.append('| 行 | 标记 | 原文 |')
            out.append('|---|---|---|')
            for _, ln, name, txt in by_file[f]:
                txt = txt.replace('|', '\\|')
                if len(txt) > 120:
                    txt = txt[:117] + '...'
                out.append('| %d | %s | `%s` |' % (ln, name, txt))
            out.append('')

    todo_stubs = [s for s in stubs if not s[5]]
    noop_stubs = [s for s in stubs if s[5]]

    out.append('## 2. 疑似桩函数（待实现）')
    out.append('')
    out.append('判定口径：空函数体，或函数体只有一句 return；且注释未说明"有意留空"。')
    out.append('')
    if not todo_stubs:
        out.append('无。')
        out.append('')
    else:
        by_file = {}
        for item in todo_stubs:
            by_file.setdefault(item[0], []).append(item)
        for f in sorted(by_file):
            out.append('### %s' % f)
            out.append('')
            out.append('| 行 | 函数 | 原因 | 含待办标记 |')
            out.append('|---|---|---|---|')
            for _, ln, name, reason, has_marker, _ in by_file[f]:
                out.append('| %d | `%s` | %s | %s |' % (ln, name, reason, '是' if has_marker else '否'))
            out.append('')

    out.append('## 3. 疑似有意留空（非待实现，核对后忽略）')
    out.append('')
    if not noop_stubs:
        out.append('无。')
        out.append('')
    else:
        by_file = {}
        for item in noop_stubs:
            by_file.setdefault(item[0], []).append(item)
        for f in sorted(by_file):
            out.append('### %s' % f)
            out.append('')
            out.append('| 行 | 函数 | 原因 |')
            out.append('|---|---|---|')
            for _, ln, name, reason, _, _ in by_file[f]:
                out.append('| %d | `%s` | %s |' % (ln, name, reason))
            out.append('')

    out.append('## 4. 进度文档现状')
    out.append('')
    out.append('| 文档 | 大小 | 现状 |')
    out.append('|---|---|---|')
    for d in PROGRESS_DOCS:
        p = os.path.join(ROOT, d)
        if not os.path.exists(p):
            out.append('| `%s` | - | 不存在 |' % d)
            continue
        size = os.path.getsize(p)
        state = '空（待填写）' if size == 0 else '有内容'
        out.append('| `%s` | %d 字节 | %s |' % (d, size, state))
    out.append('')

    out.append('## 5. 说明')
    out.append('')
    out.append('1) 待办标记取源码注释与代码里的关键字，第三方目录已跳过。')
    out.append('2) 桩函数是启发式判定，个别宏或声明式写法可能误报，按清单核对。')
    out.append('3) "含待办标记"指函数上下 6 行内有 TODO 等标记，这类基本是要动手实现的。')
    out.append('4) 本报告每次运行覆盖，最新的一份即当前状态。')
    out.append('')
    return '\n'.join(out)


def selftest():
    """自检：在临时目录造一个合成工程，故意埋下"待办标记 / 空桩 / 有意留空 / 产物目录里的待办"
    四类样本，直接调 scan() 与 render() 核对判定确实命中。

    刻意**不调 main()** ：main 里的 save_snapshot 会把临时目录的计数写进趋势历史，
    污染真实基线（自检末尾有一条断言专门守这个）。不写真实报告、不碰仓库里任何文件。
    """
    import shutil
    import tempfile

    checks = []

    def ck(name, ok, extra=''):
        checks.append((name, bool(ok), extra))

    before = load_prev()
    tmp = tempfile.mkdtemp(prefix='progress_selftest_')
    try:
        def wr(name, text):
            with open(os.path.join(tmp, name), 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)

        # 样本1：待办标记 + 空函数体（应判为待实现桩）；旁边放一个真有实现的函数做对照
        wr('a.c', '/* TODO: 采集任务还没写 */\n'
                  'void App_Sensor(void)\n{\n}\n\n'
                  'int App_Calc(int x)\n{\n    return x * 2;\n}\n')
        # 样本2：函数体只有一句 return（应判为待实现桩）
        wr('b.cpp', 'int App_Read(void)\n{\n    return 0;\n}\n')
        # 样本3：注释说明有意留空（应归到"非待实现"栏）
        wr('c.c', '/* 默认态已满足，本回调有意留空 */\nvoid App_Cb(void)\n{\n}\n')
        # 样本4：落在被排除目录里，它的待办不得计入
        os.makedirs(os.path.join(tmp, 'build'))
        wr(os.path.join('build', 'gen.c'), '// TODO: 产物目录里的待办\nvoid Lib_Foo(void)\n{\n}\n')

        markers, stubs, enc_count, file_count = scan(tmp)
        todo = [s for s in stubs if not s[5]]
        noop = [s for s in stubs if s[5]]
        todo_names = sorted(s[2] for s in todo)
        noop_names = sorted(s[2] for s in noop)
        reason = {s[2]: s[3] for s in stubs}
        all_names = [s[2] for s in stubs]

        ck('待办标记命中 1 处（产物目录里的不计）', len(markers) == 1, '%d 处' % len(markers))
        ck('源码文件计 3 个（build/ 已排除）', file_count == 3, '%d 个' % file_count)
        ck('待实现桩 2 个', todo_names == ['App_Read', 'App_Sensor'], '、'.join(todo_names) or '无')
        ck('有意留空 1 个且未混进待实现', noop_names == ['App_Cb'], '、'.join(noop_names) or '无')
        ck('真有实现的函数未被误判', 'App_Calc' not in all_names)
        ck('空函数体 / 单句 return 的原因判定正确',
           reason.get('App_Sensor') == '空函数体' and reason.get('App_Read') == '只有一句 return',
           '%s / %s' % (reason.get('App_Sensor'), reason.get('App_Read')))
        ck('待办标记与上方的桩关联上', any(s[2] == 'App_Sensor' and s[4] for s in todo))

        report = render(tmp, markers, stubs, enc_count, file_count)
        ck('报告渲染不炸且含两栏清单',
           '## 2. 疑似桩函数（待实现）' in report and 'App_Sensor' in report
           and '## 3. 疑似有意留空' in report and 'App_Cb' in report)
        ck('自检未污染趋势快照', load_prev() == before)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    bad = [c for c in checks if not c[1]]
    for name, ok, extra in checks:
        print('  [%s] %s%s' % ('PASS' if ok else 'FAIL', name,
                               ('  （实际：%s）' % extra) if (extra and not ok) else ''))
    print('进度跟踪自检：%d 通过 / %d 失败' % (len(checks) - len(bad), len(bad)))
    return 1 if bad else 0


def main(argv):
    if '--selftest' in argv:
        return selftest()

    root = ROOT
    quiet = False
    bad = []
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '进度报告.md')
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in ('--root', '--out'):
            if i + 1 >= len(argv):
                bad.append('%s 缺少值' % a)
                i += 1
                continue
            if a == '--root':
                root = os.path.abspath(argv[i + 1])
            else:
                out_path = os.path.abspath(argv[i + 1])
            i += 2
        elif a == '--quiet':
            quiet = True
            i += 1
        else:
            bad.append(a)
            i += 1

    if bad:
        print('未知参数：%s' % ' '.join(bad))
        print('可用参数：--root <目录> | --out <文件> | --quiet | --selftest')
        return 2

    if not os.path.isdir(root):
        print('扫描根不存在：%s' % root)
        return 2

    markers, stubs, enc_count, file_count = scan(root)
    prev = load_prev()
    if isinstance(prev, list):
        prev = prev[-1] if prev else None
    cur, _ = save_snapshot(markers, stubs)
    report = render(root, markers, stubs, enc_count, file_count, cur, prev)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(report)

    todo = [s for s in stubs if not s[5]]
    print('进度扫描完成')
    print('  扫描根    : %s' % root)
    print('  源码文件  : %d' % file_count)
    print('  待办标记  : %d 处' % len(markers))
    print('  待实现桩  : %d 个' % len(todo))
    print('  有意留空  : %d 个' % (len(stubs) - len(todo)))
    if prev:
        print('  较上次    : 待办 %s，桩 %s'
              % (delta_text(len(markers), prev.get('markers')),
                 delta_text(len(todo), prev.get('stubs'))))
    else:
        print('  较上次    : 首次记录，已存基线')
    print('  报告      : %s' % out_path)
    if not quiet:
        print('')
        print('待实现桩函数按文件：')
        counts = {}
        for r, _, _, _, _, noop in stubs:
            if not noop:
                counts[r] = counts.get(r, 0) + 1
        for f in sorted(counts, key=lambda k: -counts[k]):
            print('  %3d  %s' % (counts[f], f))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
