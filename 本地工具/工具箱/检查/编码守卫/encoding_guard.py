# -*- coding: utf-8 -*-
r"""编码与仓库守卫（只读源码，不改任何文件）。

用途：把工程的编码口径做成可执行的检查，防止中文注释被编辑器改坏。

口径（见 注释与文档文风规范.md 第 4 节）：
  · 两份 main.c 为 GBK(936) 编码；
  · 其余源码与文档为 UTF-8 无 BOM。
  仓库索引中的换行统一为 LF。

关于换行的重要说明：若本机 git 设了 core.autocrlf=true，签出到工作区的 UTF-8
文本会被自动转成 CRLF，此时看工作区字节会误判。所以换行检查只对 git 索引里的
内容做（--eol 选项），默认不查工作区换行。可用 git config --get core.autocrlf 查看。

检查项（默认）：
  1) BOM：UTF-8 / UTF-16 BOM 一律不允许；
  2) 编码：main.c 必须能按 GBK 解码；其余必须能按 UTF-8 解码；
  3) 乱码：解码后出现替换字符 U+FFFD 即报。

检查项（--eol 启用）：
  4) 换行：git 索引里的换行必须为 LF（autocrlf 不影响该判定）。

另附"内存占用快照"：读 Keil 构建日志里的 Program Size 行。

跳过第三方、构建产物与参考素材（FreeRTOS / ETH / RTE / qcustomplot / build / 双版资源）。

报告写到脚本同目录的 编码守卫报告.md，并在控制台打印摘要。
有违规时退出码非 0，便于接进自动化。

用法：
    python 本地工具\工具箱\检查\编码守卫\encoding_guard.py
    python 本地工具\工具箱\检查\编码守卫\encoding_guard.py --eol
"""
import os
import re
import subprocess
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

PROJECT = os.path.join(ROOT, 'EnvMonitor')

# 只查口径覆盖的源码与文档类型（不含 .bat/.ini/.ps1 等工程外文件）
SCAN_EXT = {'.c', '.h', '.cpp', '.hpp', '.cc', '.hh', '.md', '.txt',
            '.pro', '.json', '.ui'}
EXCLUDE_DIR = {
    '.git', '.venv', '.vscode', '__pycache__', 'node_modules',
    'FreeRTOS', 'ETH', 'RTE', 'Objects', 'Listings',
    'build', 'build_verify', '.qtcreator', '.gradle', 'android', '_diagram_tmp',
    '双版资源',
    # Linux-32：Linux + STM32 自学试验场，工程由 EnvMonitor 复制而来、尚未改写，
    # 与 .gitignore 的「整体不入库」口径保持一致，不纳入编码检查。
    'Linux-32',
}
EXCLUDE_FILE = re.compile(r'(^qcustomplot\.|^moc_|^ui_|^rcc_|Makefile$)')

# Keil 复编日志（内存快照来源）。路径相对脚本自身定位，工具箱整体挪动后仍然有效；
# 找不到时会在报告里显式提示，而不是静默留空。
KEIL_LOG_CANDIDATES = [
    ('板1 检测端 (F407ZE)', ['keil_板1.log', 'keil_板1.txt'], 512),
    ('板2 通信端 (F407ZG)', ['keil_板2.log', 'keil_板2.txt'], 1024),
]
KEIL_LOG_HINTS = [
    os.path.join('仓库巡检', '验证日志'),
    os.path.join('本地工具', '仓库巡检', '验证日志'),
    '验证日志',
]


def locate_keil_logs():
    """在候选目录里找 Keil 复编日志，返回 (title, 绝对路径, flash_kb) 列表与缺失提示。"""
    tool_dir = os.path.dirname(os.path.abspath(__file__))
    roots = []
    d = os.path.abspath(os.path.dirname(tool_dir))
    while True:
        roots.append(d)
        p = os.path.dirname(d)
        if p == d:
            break
        d = p
    found, missing = [], []
    for title, names, flash_kb in KEIL_LOG_CANDIDATES:
        hit = None
        for base in roots:
            for hint in KEIL_LOG_HINTS:
                for name in names:
                    cand = os.path.join(base, hint, name)
                    if os.path.exists(cand):
                        hit = cand
                        break
                if hit:
                    break
            if hit:
                break
        if hit:
            found.append((title, hit, flash_kb))
        else:
            missing.append(title)
    return found, missing


def expected_encoding(path):
    return 'gbk' if os.path.basename(path).lower() == 'main.c' else 'utf-8'


def iter_files(root):
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in EXCLUDE_DIR]
        for fn in files:
            if EXCLUDE_FILE.search(fn):
                continue
            if os.path.splitext(fn)[1].lower() in SCAN_EXT:
                yield os.path.join(base, fn)


def git_index_eol():
    """返回 {相对路径: 'i/lf'|'i/crlf'|'i/mixed'}，取 git 索引里的换行。"""
    try:
        out = subprocess.run(
            ['git', '-c', 'core.quotePath=false', '-C', ROOT, 'ls-files', '--eol'],
            capture_output=True, text=True, encoding='utf-8', errors='replace')
    except Exception:
        return {}, False
    if out.returncode != 0:
        return {}, False
    mp = {}
    for line in out.stdout.splitlines():
        parts = line.split('\t')
        if len(parts) < 2:
            continue
        head = parts[0].split()
        path = parts[-1].strip()
        if head:
            mp[path.replace('\\', '/')] = head[0]
    return mp, True


def check_file(path, index_eol, do_eol):
    issues = []
    with open(path, 'rb') as f:
        data = f.read()

    if data.startswith(b'\xef\xbb\xbf'):
        issues.append(('BOM', '含 UTF-8 BOM（应为无 BOM）'))
    elif data.startswith(b'\xff\xfe') or data.startswith(b'\xfe\xff'):
        issues.append(('BOM', '含 UTF-16 BOM'))

    enc = expected_encoding(path)
    body = data[3:] if data.startswith(b'\xef\xbb\xbf') else data
    try:
        text = body.decode(enc)
    except UnicodeDecodeError as e:
        other = 'utf-8' if enc == 'gbk' else 'gbk'
        hint = ''
        try:
            body.decode(other)
            hint = '（按 %s 可解码，疑似被另存为 %s）' % (other, other)
        except UnicodeDecodeError:
            hint = ''
        issues.append(('编码', '按 %s 解码失败：%s%s' % (enc, str(e)[:56], hint)))
    else:
        bad = text.count('\ufffd')
        if bad:
            issues.append(('乱码', '含 %d 个替换字符 U+FFFD' % bad))
        # GBK 很宽容，UTF-8 中文也能被"当成"GBK 解出来；再用一条启发式判定：
        # 若同一段字节又能按 UTF-8 严格解码且含中文，则更像被另存成了 UTF-8。
        if enc == 'gbk':
            try:
                as_utf8 = body.decode('utf-8')
            except UnicodeDecodeError:
                as_utf8 = None
            if as_utf8 is not None and re.search(r'[\u4e00-\u9fff]', as_utf8):
                issues.append(('编码', '可按 GBK 解码，但内容更像 UTF-8（含中文），疑似被另存为 UTF-8'))

    if do_eol and index_eol:
        rel = os.path.relpath(path, ROOT).replace('\\', '/')
        ie = index_eol.get(rel)
        if ie and ie != 'i/lf':
            issues.append(('换行', 'git 索引换行为 %s（应为 LF）' % ie))

    return issues


def keil_snapshot():
    rows = []
    logs, missing = locate_keil_logs()
    for title, path, flash_kb in logs:
        with open(path, 'rb') as f:
            text = f.read().decode('gbk', 'replace')
        m = re.search(r'Program Size:\s*Code=(\d+)\s+RO-data=(\d+)\s+RW-data=(\d+)\s+ZI-data=(\d+)', text)
        if m:
            code, ro, rw, zi = (int(x) for x in m.groups())
            rows.append((title, code, ro, rw, zi, flash_kb, code + ro, rw + zi))
        else:
            missing.append('%s（日志存在但未匹配到 Program Size，可能复编未成功）' % title)
    return rows, missing


def render(scanned, bad_files, enc_stat, do_eol):
    out = ['# 编码守卫报告', '',
           '来源：`encoding_guard.py`（只读扫描，可重复运行）', '']
    out.append('## 0. 总览')
    out.append('')
    out.append('| 项 | 值 |')
    out.append('|---|---|')
    out.append('| 仓库根 | `%s` |' % ROOT)
    out.append('| 扫描文件 | %d |' % scanned)
    out.append('| 违规文件 | %d |' % len(bad_files))
    out.append('| 换行检查 | %s |' % ('已启用（按 git 索引判定）' if do_eol else '未启用（加 --eol 启用）'))
    out.append('')
    out.append('口径：main.c 为 GBK 无 BOM；其余为 UTF-8 无 BOM；仓库索引换行为 LF。')
    out.append('')

    out.append('## 1. 违规清单')
    out.append('')
    if not bad_files:
        out.append('无。')
        out.append('')
    else:
        out.append('| 文件 | 类型 | 详情 |')
        out.append('|---|---|---|')
        for rel, issues in bad_files:
            for kind, detail in issues:
                out.append('| `%s` | %s | %s |' % (rel, kind, detail.replace('|', '\\|')))
        out.append('')

    out.append('## 2. 内存占用快照（取自 Keil 构建日志）')
    out.append('')
    rows, missing = keil_snapshot()
    if not rows:
        out.append('未找到 Keil 构建日志，或日志中没有 Program Size 行。')
        out.append('')
    else:
        out.append('| 板 | Code | RO-data | RW-data | ZI-data | Flash(Code+RO) | RAM(RW+ZI) | 芯片 Flash |')
        out.append('|---|---|---|---|---|---|---|---|')
        for title, code, ro, rw, zi, fkb, flash, ram in rows:
            out.append('| %s | %d | %d | %d | %d | %d 字节（%.1f%% / %d KB） | %d 字节 | %d KB |'
                       % (title, code, ro, rw, zi, flash, 100.0 * flash / (fkb * 1024), fkb, ram, fkb))
        out.append('')
        out.append('注：日志为上一次 Keil 复编的结果，改代码后需重编才会更新。')
        out.append('')
    if missing:
        out.append('以下复编日志未取到，内存快照会缺项（修好后重跑本工具即可补齐）：')
        out.append('')
        for item in missing:
            out.append('- %s' % item)
        out.append('')
        out.append('查找位置：脚本目录及其各上级目录下的 `仓库巡检/验证日志/`。')
        out.append('若日志在别处，把该目录放到工具箱内或上述任一路径下即可被自动找到。')
        out.append('')

    out.append('## 3. 编码分布')
    out.append('')
    out.append('| 应有编码 | 文件数 |')
    out.append('|---|---|')
    for k, v in sorted(enc_stat.items()):
        out.append('| %s | %d |' % (k, v))
    out.append('')

    out.append('## 4. 说明')
    out.append('')
    out.append('1) 若 main.c 被判为"按 gbk 解码失败、按 utf-8 可解码"，多为编辑器把它另存成了 UTF-8，需改回 GBK。')
    out.append('2) 换行检查只在启用 --eol 时进行，且只看 git 索引，避免 core.autocrlf 造成的误判。')
    out.append('3) 本报告每次运行覆盖；有违规时进程退出码非 0。')
    out.append('')
    return '\n'.join(out)


def selftest():
    """用几个故意写坏的文件验证检查确实命中。"""
    import shutil
    import tempfile

    d = tempfile.mkdtemp(prefix='enc_selftest_')
    results = []

    # 1) main.c 存成 UTF-8（应为 GBK）
    p1 = os.path.join(d, 'main.c')
    with open(p1, 'wb') as f:
        f.write('/* 温度 */\nint x;\n'.encode('utf-8'))
    # 2) 文档存成 GBK（应为 UTF-8）
    p2 = os.path.join(d, 'doc.md')
    with open(p2, 'wb') as f:
        f.write('温度\n'.encode('gbk'))
    # 3) UTF-8 BOM
    p3 = os.path.join(d, 'bom.md')
    with open(p3, 'wb') as f:
        f.write(b'\xef\xbb\xbf' + '温度\n'.encode('utf-8'))

    results.append(('main.c 存成 UTF-8 应报编码',
                    any(k == '编码' for k, _ in check_file(p1, {}, False))))
    results.append(('文档存成 GBK 应报编码',
                    any(k == '编码' for k, _ in check_file(p2, {}, False))))
    results.append(('UTF-8 BOM 应报 BOM',
                    any(k == 'BOM' for k, _ in check_file(p3, {}, False))))

    ok = all(r[1] for r in results)
    print('编码守卫自检：%s' % ('PASS' if ok else 'FAIL'))
    for what, good in results:
        print('  %s  %s' % ('PASS' if good else 'FAIL', what))
    shutil.rmtree(d, ignore_errors=True)
    return 0 if ok else 1


def main(argv):
    if '--selftest' in argv:
        return selftest()
    quiet = False
    do_eol = False
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '编码守卫报告.md')
    bad = []
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == '--quiet':
            quiet = True
            i += 1
        elif a == '--eol':
            do_eol = True
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

    # 参数写错要报错退出，不能静默忽略（否则会跑成一次全量真实扫描并覆盖报告）
    if bad:
        print('未知参数：%s' % ' '.join(bad))
        print('可用参数：--out <文件> | --quiet | --eol | --selftest')
        return 2

    index_eol, ok = git_index_eol() if do_eol else ({}, False)
    if do_eol and not ok:
        print('提示：无法读取 git 索引，换行检查跳过（可能不在 git 仓库或未装 git）。')

    scanned = 0
    bad_files = []
    enc_stat = {}
    for path in iter_files(PROJECT):
        scanned += 1
        enc = expected_encoding(path)
        enc_stat[enc] = enc_stat.get(enc, 0) + 1
        issues = check_file(path, index_eol, do_eol)
        if issues:
            bad_files.append((os.path.relpath(path, ROOT), issues))

    report = render(scanned, bad_files, enc_stat, do_eol)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(report)

    print('编码守卫完成')
    print('  仓库根  : %s' % ROOT)
    print('  扫描文件: %d' % scanned)
    print('  违规文件: %d' % len(bad_files))
    print('  换行检查: %s' % ('开' if do_eol else '关（加 --eol）'))
    print('  报告    : %s' % out_path)
    if not quiet and bad_files:
        for rel, issues in bad_files:
            for kind, detail in issues:
                print('  [%s] %s  %s' % (kind, rel, detail))
    return 1 if bad_files else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
