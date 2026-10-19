# -*- coding: utf-8 -*-
r"""编码与仓库守卫（只读源码，不改任何文件）。

用途：把工程的编码口径做成可执行的检查，防止中文注释被编辑器改坏。

口径（见 注释与文档文风规范.md 第 4 节）：
  · 扫描根含 EnvMonitor 与 OTA 两个工程（OTA1 / OTA1_Boot）；
  · 各 main.c（板1 / 板2 / OTA1）为 GBK(936) 编码；
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

检查项（--strict-comments 启用）：
  5) 注释严格模式：注释里的全角标点也按"违规"计（默认只作提示，见报告 1b 节）。

另附"内存占用快照"：读 Keil 构建日志里的 Program Size 行。

另附 H8「工作区换行统计」：始终统计"哪些文件在工作区里是 CRLF、共几条"（**只统计、不算违规**）——
  换行违规仍只看 git 索引；这一栏是给"编辑器把某个文件整成了 CRLF"这类改动留个可见的计数。

跳过第三方、构建产物与参考素材（FreeRTOS / ETH / RTE / qcustomplot / build / 双版资源）；
.bat 等工程外文件本就不在扫描类型内。

报告写到脚本同目录的 编码守卫报告.md，并在控制台打印摘要。
有违规时退出码非 0，便于接进自动化。

用法：
    python 本地工具\工具箱\检查\编码守卫\encoding_guard.py
    python 本地工具\工具箱\检查\编码守卫\encoding_guard.py --eol
    python 本地工具\工具箱\检查\编码守卫\encoding_guard.py --strict-comments
"""
import json
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

# C1+C2 板级配置换装口：板名 / Keil 日志文件名 / Flash-RAM 上限优先读
# 速查数据/rules/board_envmonitor.json，换板只改那份 JSON、不用动脚本。
# 读不到（文件缺失/损坏）回退到下面的内置默认，行为与老版本一致。
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
try:
    import boardcfg
except Exception:
    boardcfg = None

# A1：扫描根不再只有 EnvMonitor —— OTA 的两个工程（OTA1 / OTA1_Boot）也在口径内，
# 免得 Keil 哪天把它们的源码另存成 UTF-8 却没人报。口径不变（见 expected_encoding）：
# main.c = GBK 无 BOM，其余 = UTF-8 无 BOM；RTE 生成物与 .bat 照旧豁免（见 EXCLUDE_DIR / SCAN_EXT）。
SCAN_ROOT_CANDIDATES = [
    ('EnvMonitor', os.path.join(ROOT, 'EnvMonitor')),
    ('OTA', os.path.join(ROOT, 'OTA')),
]

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

# B4：注释体检（单独一节，仅提示，不计入违规）
#  · 全角标点：中文注释的习惯写法，不影响编译，列出来是为了统一风格时有个清单；
#  · GBK / 乱码残留：UTF-8 字节被当 GBK 解出来的经典签名（如"锟斤拷"），这才是真损坏；
#  · 替换字符 U+FFFD：已经按"乱码"计入违规，这里再按"注释区"点一次名便于定位。
FULLWIDTH_PUNCT = set('，。！？；：、（）【】《》“”‘’…—～·〈〉「」『』')
GBK_MOJIBAKE_SIGNS = ('锟斤拷', '锟')
CODE_EXT = {'.c', '.h', '.cpp', '.hpp', '.cc', '.hh'}

# Keil 复编日志（内存快照来源）。路径相对脚本自身定位，工具箱整体挪动后仍然有效；
# 找不到时会在报告里显式提示，而不是静默留空。
# (标题, 日志文件名候选, 芯片 Flash KB, 芯片 RAM KB)
# F407ZE = 512KB Flash；F407ZG = 1MB Flash；两者 SRAM 均 192KB（112+16+64）。
KEIL_LOG_CANDIDATES = [
    ('板1 检测端 (F407ZE)', ['keil_板1.log', 'keil_板1.txt'], 512, 192),
    ('板2 通信端 (F407ZG)', ['keil_板2.log', 'keil_板2.txt'], 1024, 192),
]

# 优先读外置板级配置；读到了才覆盖，读不到保持上面内置默认。
_cfg = boardcfg.load(ROOT) if boardcfg else None
if _cfg:
    _cand = []
    for _bd in (_cfg.get('boards') or []):
        if not isinstance(_bd, dict) or not _bd.get('name') or not _bd.get('keil_log'):
            continue
        _cand.append((_bd['name'], list(_bd['keil_log']),
                      int(_bd.get('flash_kb') or 0), int(_bd.get('ram_kb') or 0)))
    if _cand:
        KEIL_LOG_CANDIDATES = _cand

KEIL_LOG_HINTS = [
    os.path.join('仓库巡检', '验证日志'),
    os.path.join('本地工具', '仓库巡检', '验证日志'),
    '验证日志',
]


def locate_keil_logs():
    """在候选目录里找 Keil 复编日志，返回 (title, 绝对路径, flash_kb, ram_kb) 列表与缺失提示。"""
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
    for title, names, flash_kb, ram_kb in KEIL_LOG_CANDIDATES:
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
            found.append((title, hit, flash_kb, ram_kb))
        else:
            missing.append(title)
    return found, missing


def expected_encoding(path):
    return 'gbk' if os.path.basename(path).lower() == 'main.c' else 'utf-8'


def scan_roots():
    """A1：实际要扫的根目录（label, abs）。目录不存在就跳过，
    便于换装 / 裁剪后仍能跑而不报错。"""
    return [(n, p) for n, p in SCAN_ROOT_CANDIDATES if os.path.isdir(p)]


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


def comment_text(text):
    """抽出 C 系源码里的注释区文本（// 整行 与 /* */ 块），字符串与代码都跳过。
    B4 只在这些区域里查乱码与全角标点 —— 代码区出现全角标点属于语法问题，
    由编译器直接拦，不必在这儿判。"""
    chunks = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = i
            while i < n and text[i] != '\n':
                i += 1
            chunks.append(text[j:i])
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = i
            i += 2
            while i < n and not (text[i] == '*' and i + 1 < n and text[i + 1] == '/'):
                i += 1
            i = min(i + 2, n)
            chunks.append(text[j:i])
        elif c == '"' or c == "'":
            q = c
            i += 1
            while i < n and text[i] != q:
                if text[i] == '\\' and i + 1 < n:
                    i += 2
                else:
                    i += 1
            i += 1
        else:
            i += 1
    return '\n'.join(chunks)


def comment_notes(text, ext, strict=False):
    """B4：注释区的提示项。返回 [( kind, 说明 )]，没有就返回空。
    B5：strict=True（--strict-comments）时，全角标点按"违规"计（kind='注释违规'），
    默认仍只作提示（kind='注释'）。乱码残留/替换字符本来就是问题，两档都照报。"""
    if ext not in CODE_EXT:
        return []
    cet = comment_text(text)
    if not cet:
        return []
    notes = []
    fw = sorted({ch for ch in cet if ch in FULLWIDTH_PUNCT})
    if fw:
        if strict:
            notes.append(('注释违规',
                          '注释里用了全角标点 %s（--strict-comments：按违规计）'
                          % ''.join(fw)))
        else:
            notes.append(('注释', '注释里用了全角标点 %s（中文注释的习惯写法，不影响编译）'
                          % ''.join(fw)))
    bad = sum(cet.count(s) for s in GBK_MOJIBAKE_SIGNS)
    if bad:
        notes.append(('注释', '注释里出现 %d 处 GBK / 乱码残留签名（如"锟斤拷"），'
                      '多半是编码转换留下的，建议重打这段注释' % bad))
    rep = cet.count('\ufffd')
    if rep:
        notes.append(('注释', '注释区有 %d 个替换字符 U+FFFD（乱码残留）' % rep))
    return notes


def check_file(path, index_eol, do_eol, strict=False):
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
        # B4：注释区体检（kind = '注释'/'注释违规'，由 main 分拣出去）
        issues.extend(comment_notes(text, os.path.splitext(path)[1].lower(), strict))

    if do_eol and index_eol:
        # 跨盘符（如临时目录在 C:、仓库在 D:）时 relpath 会抛 ValueError，跳过即可 ——
        # 真实扫描的路径都在 ROOT 之下，这里只是防"拿别的盘的文件来判"。
        try:
            rel = os.path.relpath(path, ROOT).replace('\\', '/')
        except ValueError:
            rel = None
        if rel:
            ie = index_eol.get(rel)
            if ie and ie != 'i/lf':
                issues.append(('换行', 'git 索引换行为 %s（应为 LF）' % ie))

    return issues


def workspace_crlf(path):
    """H8：只统计、不判违规 —— 这份**工作区**文件里 CRLF（\\r\\n）的条数。

    为什么单列：换行检查只按 git 索引判（本机 core.autocrlf=true 时，签出到工作区的文本
    本来就会被转成 CRLF，拿工作区字节判会误报）。但"编辑器把某个文件整成了 CRLF"这类改动
    也得看得见 —— 所以给一个**计数**：数出来、写进报告，但不计入违规。"""
    try:
        with open(path, 'rb') as f:
            return f.read().count(b'\r\n')
    except OSError:
        return 0


def git_autocrlf():
    """本机 core.autocrlf 的取值（true / input / false / 空）—— 「工作区 CRLF 统计」要按它解读：
    true 时工作区本来就会被签出成 CRLF，那 200+ 个文件全是 CRLF 属正常，不是"有人改了换行"。"""
    try:
        r = subprocess.run(['git', '-C', ROOT, 'config', '--get', 'core.autocrlf'],
                           capture_output=True, text=True)
        return (r.stdout or '').strip()
    except Exception:
        return ''


def keil_snapshot():
    rows = []
    logs, missing = locate_keil_logs()
    for title, path, flash_kb, ram_kb in logs:
        with open(path, 'rb') as f:
            text = f.read().decode('gbk', 'replace')
        m = re.search(r'Program Size:\s*Code=(\d+)\s+RO-data=(\d+)\s+RW-data=(\d+)\s+ZI-data=(\d+)', text)
        if m:
            code, ro, rw, zi = (int(x) for x in m.groups())
            rows.append((title, code, ro, rw, zi, flash_kb, ram_kb, code + ro, rw + zi))
        else:
            missing.append('%s（日志存在但未匹配到 Program Size，可能复编未成功）' % title)
    return rows, missing


def render(scanned, bad_files, enc_stat, do_eol, comment_list=None, strict=False,
           scan_labels=None, ws_crlf_files=0, ws_crlf_total=0):
    out = ['# 编码守卫报告', '',
           '来源：`encoding_guard.py`（只读扫描，可重复运行）', '']
    out.append('## 0. 总览')
    out.append('')
    out.append('| 项 | 值 |')
    out.append('|---|---|')
    out.append('| 仓库根 | `%s` |' % ROOT)
    out.append('| 扫描根 | %s |' % ('、'.join(scan_labels) if scan_labels else '（未指定）'))
    out.append('| 扫描文件 | %d |' % scanned)
    out.append('| 违规文件 | %d |' % len(bad_files))
    out.append('| 换行检查 | %s |' % ('已启用（按 git 索引判定）' if do_eol else '未启用（加 --eol 启用）'))
    out.append('| 工作区换行统计 | %d 个文件含 CRLF（共 %d 条；**仅统计，不算违规**）'
               '　本机 `core.autocrlf=%s` |'
               % (ws_crlf_files, ws_crlf_total, git_autocrlf() or '未设'))
    out.append('| 注释严格模式 | %s |' % ('已启用（--strict-comments：全角标点按违规计）'
                                        if strict else '未启用（加 --strict-comments 启用）'))
    out.append('')
    out.append('口径：main.c 为 GBK 无 BOM；其余为 UTF-8 无 BOM；仓库索引换行为 LF。'
               '扫描根含 EnvMonitor 与 OTA（OTA1 / OTA1_Boot），RTE 生成物与 .bat 豁免。')
    out.append('')
    if ws_crlf_files:
        _ac = git_autocrlf()
        _why = ('本机 `core.autocrlf=%s`，工作区里文本本来就会被签出成 CRLF，**属正常**；'
                % _ac) if _ac == 'true' else ''
        out.append('注（工作区 CRLF）：%s工作区里这些文件是 CRLF 本身**不是违规** —— 真违规看的是'
                   'git **索引**里的换行（上面「换行检查」一行）。这一栏只把"哪些文件在工作区是 '
                   'CRLF"数出来，方便一眼看出"是不是编辑器把某个文件整成了 CRLF"。' % _why)
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

    out.append('## 1b. 注释体检（全角标点 / 乱码残留；仅提示，不计入违规）')
    out.append('')
    cl = comment_list or []
    if not cl:
        out.append('无。')
        out.append('')
    else:
        shown = cl[:25]
        out.append('| 文件 | 提示 |')
        out.append('|---|---|')
        for rel, notes in shown:
            for _, detail in notes:
                out.append('| `%s` | %s |' % (rel, detail.replace('|', '\\|')))
        out.append('')
        if len(cl) > len(shown):
            out.append('另有 %d 个文件同类（注释里用全角标点属中文注释习惯，'
                       '不影响编译，不逐条列出）。' % (len(cl) - len(shown)))
            out.append('')
        out.append('注：这一节只做提示。全角标点在注释里是习惯问题；真正按"违规"处理的'
                   '只有编码/BOM/换行/替换字符那些（见上一节）。'
                   '想让全角标点也按违规计，加 `--strict-comments`（那一档会写进第 1 节）。')
        out.append('')

    out.append('## 2. 内存占用快照（取自 Keil 构建日志）')
    out.append('')
    rows, missing = keil_snapshot()
    if not rows:
        out.append('未找到 Keil 构建日志，或日志中没有 Program Size 行。')
        out.append('')
    else:
        out.append('| 板 | Code | RO-data | RW-data | ZI-data | Flash(Code+RO) | RAM(RW+ZI) |')
        out.append('|---|---|---|---|---|---|---|')
        for title, code, ro, rw, zi, fkb, rkb, flash, ram in rows:
            out.append('| %s | %d | %d | %d | %d | %d 字节（%.1f%% / %d KB） | %d 字节（%.1f%% / %d KB） |'
                       % (title, code, ro, rw, zi, flash, 100.0 * flash / (fkb * 1024), fkb,
                          ram, 100.0 * ram / (rkb * 1024), rkb))
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

    # B1：报告结尾补一节"下一步怎么做" —— 命中问题后要分清哪些是必须修的真违规、
    # 哪些只是习惯提示，别把提示当违规去改。只加报告文字，不动判定逻辑与 JSON 字段。
    out.append('## 下一步怎么做')
    out.append('')
    out.append('- 第 1 节「违规清单」里的 BOM / 编码 / 换行 / 替换字符才是真违规，必须逐条修：'
               'BOM 用编辑器「另存为无 BOM」，main.c 存回 GBK，其余源码与文档存成 UTF-8。')
    out.append('- 第 1b 节的全角标点属中文注释习惯，不影响编译；'
               '想按违规计就加 `--strict-comments` 重跑，那一档会写进第 1 节。')
    out.append('- 换行只在加 `--eol` 时查、且按 git 索引判定；'
               '若报 CRLF，用 `git add --renormalize` 重归一后再提交。')
    out.append('- 「工作区换行统计」那一栏只是**计数**：本机 autocrlf 把工作区转成 CRLF 属正常，'
               '不算违规；它涨上去时值得看一眼"是不是编辑器把哪个文件整成了 CRLF"。')
    out.append('- 第 2 节内存快照缺项，只说明 Keil 复编日志没取到，'
               '把 `keil_板N.log` 放到上述查找路径下重跑即可，不影响编码结论。')
    out.append('')

    out.append('## 4. 说明')
    out.append('')
    out.append('1) 若 main.c 被判为"按 gbk 解码失败、按 utf-8 可解码"，多为编辑器把它另存成了 UTF-8，需改回 GBK。')
    out.append('2) 换行检查只在启用 --eol 时进行，且只看 git 索引，避免 core.autocrlf 造成的误判。')
    out.append('3) 本报告每次运行覆盖；有违规时进程退出码非 0。')
    out.append('')
    return '\n'.join(out)


def to_json(scanned, bad_files, enc_stat, do_eol, comment_list=None, strict=False,
            scan_labels=None, ws_crlf_files=0, ws_crlf_total=0):
    """结构化输出，供工具箱的可视化体检面板读取。"""
    rows, missing = keil_snapshot()
    memory = []
    for title, code, ro, rw, zi, fkb, rkb, flash, ram in rows:
        memory.append({'title': title, 'code': code, 'ro': ro, 'rw': rw, 'zi': zi,
                       'flash_kb': fkb, 'ram_kb': rkb, 'flash_used': flash, 'ram_used': ram})
    return {
        'tool': 'encoding_guard',
        'schema': 1,
        'ok': len(bad_files) == 0,
        'scanned': scanned,
        'scan_roots': list(scan_labels or []),
        'violations': len(bad_files),
        'violations_list': [
            {'file': rel, 'issues': [{'kind': k, 'detail': d} for k, d in iss]}
            for rel, iss in bad_files],
        'comment_notes': [
            {'file': rel, 'notes': [{'kind': k, 'detail': d} for k, d in ns]}
            for rel, ns in (comment_list or [])],
        'encoding_stat': enc_stat,
        'memory': memory,
        'memory_missing': missing,
        'eol_checked': bool(do_eol),
        'strict_comments': bool(strict),
        'ws_crlf_files': int(ws_crlf_files),      # H8：工作区含 CRLF 的文件数（仅统计）
        'ws_crlf_total': int(ws_crlf_total),      # H8：工作区 CRLF 总条数（仅统计）
    }


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

    # A1：扫描根放开到 OTA —— 错例（OTA 的 main.c 被存成 UTF-8 也报）/ 反例（仓库真那份是 GBK）
    p_ota_bad = os.path.join(d, 'OTA1', 'main.c')
    os.makedirs(os.path.dirname(p_ota_bad), exist_ok=True)
    with open(p_ota_bad, 'wb') as f:
        f.write('/* 温度 */\nint x;\n'.encode('utf-8'))
    results.append(('A1 错例：OTA 的 main.c 存成 UTF-8 应报编码（扫描根放开后同样守得住）',
                    any(k == '编码' for k, _ in check_file(p_ota_bad, {}, False))))
    _ota_real = os.path.join(ROOT, 'OTA', 'OTA1', 'main.c')
    results.append(('A1 反例：仓库里 OTA/OTA1/main.c 合口径（编码/BOM/乱码都不报）',
                    os.path.isfile(_ota_real)
                    and not any(k in ('BOM', '编码', '乱码')
                                for k, _ in check_file(_ota_real, {}, False))))
    results.append(('A1：扫描根已含 OTA（不再只扫 EnvMonitor）',
                    any(os.path.basename(p) == 'OTA' for _, p in scan_roots())))

    # 4) B4：注释区体检 —— 全角标点 / GBK 乱码残留签名（只提示，不算违规）
    p4 = os.path.join(d, 'comm.c')
    with open(p4, 'wb') as f:
        f.write('/* 温度，湿度。 */\nint x;\n'.encode('utf-8'))
    results.append(('B4 错例：注释里的全角标点被列出（提示）',
                    any('全角标点' in d2 for _, d2 in comment_notes('/* 温度，湿度。 */\nint x;', '.c'))))
    results.append(('B4 反例：注释里没有全角标点 / 乱码时不提示',
                    comment_notes('/* temp, humi. */\nint x;', '.c') == []))
    results.append(('B4 错例：GBK 乱码残留签名（锟斤拷）被列出',
                    any('乱码残留' in d2 for _, d2 in comment_notes('/* \u951f\u65a4\u62f7 */\n', '.c'))))
    results.append(('B4 反例：非 C 系文件（md/txt）不做注释体检',
                    comment_notes('温度，湿度。', '.md') == []
                    and comment_notes('温度，湿度。', '.txt') == []))
    results.append(('B4 注释提示在 check_file 里以 kind=注释 返回（供 main 分拣，不计违规）',
                    any(k == '注释' for k, _ in check_file(p4, {}, False))))

    # 5) B5：注释严格模式 —— 全角标点按违规计（kind=注释违规）；默认仍只提示
    results.append(('B5 错例：--strict-comments 下注释全角标点按违规计（kind=注释违规）',
                    any(k == '注释违规'
                        for k, _ in comment_notes('/* 温度，湿度。 */', '.c', strict=True))))
    results.append(('B5 反例：默认（非严格）时同一段注释仍只是提示（kind=注释，不算违规）',
                    {k for k, _ in comment_notes('/* 温度，湿度。 */', '.c')} == {'注释'}))
    results.append(('B5 反例：注释里没有全角标点时，严格模式也不报',
                    not any(k == '注释违规'
                            for k, _ in comment_notes('/* temp, humi. */', '.c', strict=True))))
    results.append(('B5 反例：严格模式只对 C 系文件生效（md 不做注释体检）',
                    comment_notes('温度，湿度。', '.md', strict=True) == []))
    with open(os.path.join(d, 'strict.c'), 'wb') as f:
        f.write('/* 温度，湿度。 */\nint x;\n'.encode('utf-8'))
    results.append(('B5：check_file 在严格模式下把注释违规一并返回',
                    any(k == '注释违规'
                        for k, _ in check_file(os.path.join(d, 'strict.c'), {}, False, True))))
    results.append(('B5 反例：check_file 默认模式不把注释算违规（只给 kind=注释）',
                    not any(k == '注释违规'
                            for k, _ in check_file(os.path.join(d, 'strict.c'), {}, False, False))))

    # H8：工作区 CRLF —— "只统计不判违规"。错例：CRLF 要数得出来；反例：不算违规；
    #      对照：git 索引里是 CRLF 时**才**算违规（判据在索引，不在工作区）。
    p_crlf = os.path.join(d, 'crlf.c')
    with open(p_crlf, 'wb') as f:
        f.write(b'int a;\r\nint b;\r\n')
    p_lf = os.path.join(d, 'lf.c')
    with open(p_lf, 'wb') as f:
        f.write(b'int a;\nint b;\n')
    results.append(('H8 错例：工作区 CRLF 被数出来（2 条）', workspace_crlf(p_crlf) == 2))
    results.append(('H8 反例：LF 文件数出来为 0', workspace_crlf(p_lf) == 0))
    results.append(('H8 反例：工作区 CRLF 不算违规（check_file 不给"换行"项）',
                    not any(k == '换行' for k, _ in check_file(p_crlf, {}, True))))

    j = to_json(3, [(p1, [('编码', 'x')])], {'utf-8': 2}, False)
    results.append(('JSON 输出结构正确',
                    j['tool'] == 'encoding_guard' and j['scanned'] == 3
                    and j['violations'] == 1 and j['ok'] is False))
    j2 = to_json(1, [(p1, [('编码', 'x')])], {'utf-8': 1}, False,
                 [(p2, comment_notes('/* 温度，湿度。 */', '.c'))])
    results.append(('JSON 带 comment_notes 字段（B4，且与违规分开）',
                    'comment_notes' in j2 and len(j2['comment_notes']) == 1))
    # B6 JSON 契约：面板读 violations 与 memory 里那串字段，名字一改就红
    results.append(('JSON 契约：面板读取的字段名/类型都在',
                    set(j) == {'tool', 'schema', 'ok', 'scanned', 'scan_roots',
                               'violations', 'violations_list', 'comment_notes',
                               'encoding_stat', 'memory', 'memory_missing',
                               'eol_checked', 'strict_comments',
                               'ws_crlf_files', 'ws_crlf_total'}
                    and isinstance(j['violations'], int)
                    and isinstance(j['memory'], list)))
    results.append(('JSON 契约：strict_comments 反映严格模式开关',
                    j['strict_comments'] is False
                    and to_json(1, [], {}, False, None, True)['strict_comments'] is True))

    jm = to_json(1, [], {'utf-8': 1}, False)
    jm['memory'] = [{'title': 't', 'code': 1, 'ro': 2, 'rw': 3, 'zi': 4,
                     'flash_kb': 512, 'ram_kb': 192, 'flash_used': 5, 'ram_used': 6}]
    results.append(('JSON 契约：memory 项字段（面板占用条要用 flash_used/ram_used）',
                    set(jm['memory'][0]) == {'title', 'code', 'ro', 'rw', 'zi',
                                             'flash_kb', 'ram_kb', 'flash_used', 'ram_used'}))

    # C1+C2 板级配置读取器（换装口）：正例读到 2 块板 / 反例配置被改名后返回 None
    if boardcfg is None:
        results.append(('板级配置读取器可用（boardcfg 导入成功）', False))
    else:
        results.append(('板级配置读取器可用（boardcfg 导入成功）', True))
        rb = boardcfg.boards(ROOT)
        results.append(('正例：仓库真配置读出 3 块板（板1 / 板2 / OTA 工程）、名字与 board_envmonitor.json 一致',
                        rb is not None and len(rb) == 3
                        and {n for n, _ in rb} == {'板1 检测端 (F407ZE)', '板2 通信端 (F407ZG)',
                                                   'OTA 工程 (F407ZE)'}))
        dcfg = tempfile.mkdtemp(prefix='boardcfg_selftest_')
        try:
            rp = os.path.join(dcfg, '本地工具', '工具箱', '速查数据', 'rules')
            os.makedirs(rp)
            cfgp = os.path.join(rp, 'board_envmonitor.json')
            with open(cfgp, 'w', encoding='utf-8', newline='\n') as fh:
                json.dump({'version': 1, 'boards': [
                    {'name': '板1 假配置', 'dir': 'x/y'},
                    {'name': '板2 假配置', 'dir': 'z/w'}]}, fh, ensure_ascii=False)
            results.append(('正例：临时假配置读到 2 块板（名字取自假配置）',
                            boardcfg.boards(dcfg) == [('板1 假配置', 'x/y'), ('板2 假配置', 'z/w')]))
            os.rename(cfgp, cfgp + '.bak')
            results.append(('反例：配置被临时改名后返回 None（脚本回退内置默认，不崩）',
                            boardcfg.load(dcfg, fallback=False) is None
                            and boardcfg.boards(dcfg, fallback=False) is None))
        finally:
            shutil.rmtree(dcfg, ignore_errors=True)

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
    json_mode = False
    strict = False
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
        elif a == '--json':
            json_mode = True
            i += 1
        elif a == '--strict-comments':
            strict = True
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
        print('可用参数：--out <文件> | --quiet | --eol | --strict-comments | --json | --selftest')
        return 2

    index_eol, ok = git_index_eol() if do_eol else ({}, False)
    if do_eol and not ok and not json_mode:
        print('提示：无法读取 git 索引，换行检查跳过（可能不在 git 仓库或未装 git）。')

    scanned = 0
    bad_files = []
    comment_list = []
    enc_stat = {}
    ws_crlf_files = 0     # H8：工作区含 CRLF 的文件数（只统计，不判违规）
    ws_crlf_total = 0     # H8：工作区 CRLF 总条数
    roots = scan_roots()
    for _label, root in roots:
        for path in iter_files(root):
            scanned += 1
            enc = expected_encoding(path)
            enc_stat[enc] = enc_stat.get(enc, 0) + 1
            n = workspace_crlf(path)
            if n:
                ws_crlf_files += 1
                ws_crlf_total += n
            issues = check_file(path, index_eol, do_eol, strict)
            # B4/B5：把注释区的提示项拣出来单独成节 —— 全角标点是习惯问题，不该算"违规文件"；
            # 但开了 --strict-comments 时，全角标点以 kind='注释违规' 返回，归到违规里。
            notes = [x for x in issues if x[0] == '注释']
            strict_hits = [x for x in issues if x[0] == '注释违规']
            issues = [x for x in issues if x[0] not in ('注释', '注释违规')]
            rel = os.path.relpath(path, ROOT)
            if issues or strict_hits:
                bad_files.append((rel, issues + strict_hits))
            if notes:
                comment_list.append((rel, notes))

    labels = [n for n, _ in roots]
    if json_mode:
        print(json.dumps(to_json(scanned, bad_files, enc_stat, do_eol, comment_list,
                                 strict, labels, ws_crlf_files, ws_crlf_total),
                         ensure_ascii=False))
        return 1 if bad_files else 0

    report = render(scanned, bad_files, enc_stat, do_eol, comment_list, strict, labels,
                    ws_crlf_files, ws_crlf_total)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(report)

    print('编码守卫完成')
    print('  仓库根  : %s' % ROOT)
    print('  扫描文件: %d' % scanned)
    print('  违规文件: %d' % len(bad_files))
    print('  注释体检: %d 个文件有提示（仅提示，不计违规，见报告 1b 节）' % len(comment_list))
    print('  换行检查: %s' % ('开' if do_eol else '关（加 --eol）'))
    print('  工作区CRLF: %d 个文件 / %d 条（仅统计，不算违规；core.autocrlf=%s）'
          % (ws_crlf_files, ws_crlf_total, git_autocrlf() or '未设'))
    print('  严格注释: %s' % ('开（全角标点按违规计）' if strict else '关（加 --strict-comments）'))
    print('  报告    : %s' % out_path)
    if not quiet and bad_files:
        for rel, issues in bad_files:
            for kind, detail in issues:
                print('  [%s] %s  %s' % (kind, rel, detail))
    return 1 if bad_files else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
