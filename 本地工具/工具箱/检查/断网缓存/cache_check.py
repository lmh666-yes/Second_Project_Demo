# -*- coding: utf-8 -*-
r"""断网缓存核算（只读源码 + 参数推演，不改任何文件）。

回答两个问题：
  1) 板2 的 W25QXX 滚动日志（w25qxx_log.h 划出来的环形区）断网时能扛多久？
  2) 补传（读一条 + 发一条）会不会比写入还慢 —— 那样缓存会被"边补边覆盖"，
     还没补上的数据直接丢了（w25qxx_log.h 里 W25QXX_LogLost 非 0 就是这种现象）。

事实来源（都是工程真值，不内置印象数据）：
  · 缓存几何 → 板2 `FWLIB\inc\w25qxx_log.h`（SLOT_SIZE / HDR_SIZE / SECTORS / 基址）
  · 分区上界 → 板2 `FWLIB\inc\w25qxx.h`（ADDR_LOG / ADDR_FONT / SECTOR_SIZE）
  · 上传周期与补传节奏 → 命令行参数（--period-ms / --replay-ms），
    默认值只是示例，请按工程实际填。

用法：
    python 本地工具\工具箱\检查\断网缓存\cache_check.py
    python 本地工具\工具箱\检查\断网缓存\cache_check.py --period-ms 1000 --replay-ms 50
    python 本地工具\工具箱\检查\断网缓存\cache_check.py --record-bytes 24
    python 本地工具\工具箱\检查\断网缓存\cache_check.py --json
    python 本地工具\工具箱\检查\断网缓存\cache_check.py --selftest
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
    for _ in range(6):
        if os.path.isdir(os.path.join(_d, 'EnvMonitor')):
            ROOT = _d
            break
        _p = os.path.dirname(_d)
        if _p == _d:
            break
        _d = _p

# C1+C2 板级配置换装口：板名 / 工程相对路径优先读 速查数据/rules/board_envmonitor.json，
# 换板只改那份 JSON、不用动脚本。读不到（文件缺失/损坏）回退到下面的内置默认，行为不变。
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
try:
    import boardcfg
except Exception:
    boardcfg = None

INC2 = os.path.join('EnvMonitor', 'Project_2_数据通信端', 'Project', 'FWLIB', 'inc')
LOG_H_REL = os.path.join(INC2, 'w25qxx_log.h')
FLASH_H_REL = os.path.join(INC2, 'w25qxx.h')

RE_DEFINE = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*)\s+(.+?)\s*(?:/\*.*)?$')


def read_text(path):
    if not os.path.exists(path):
        return None
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ('utf-8', 'gbk', 'latin-1'):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace')


def collect_defines(text):
    out = {}
    for line in text.splitlines():
        m = RE_DEFINE.match(line)
        if m:
            out.setdefault(m.group(1), m.group(2).strip())
    return out


# ---------------- B2：从工程里读"上传周期 / 补传节奏" ----------------
# 这两个是核算结论的关键假设。以前只能手填（默认 1000/50 只是示例），其实工程里
# 写得出出处：采集/上报任务 `vTaskDelay(pdMS_TO_TICKS(2000))`（板1 main.c）、
# 补传任务 `vTaskDelay(pdMS_TO_TICKS(REPLAY_PERIOD_MS))`（板2 main.c，宏 = 50）。
# 能读到就拿来用并在报告里标出出处；读不到就退回命令行值，并标明"默认值/未在工程里找到"。
MAIN1_REL = os.path.join('EnvMonitor', 'Project_1_检测端', 'Project', 'main.c')
MAIN2_REL = os.path.join('EnvMonitor', 'Project_2_数据通信端', 'Project', 'main.c')

# C1+C2 换装口：板2 工程路径 / Flash 日志头 / Flash 分区头 / 两块板 main.c 优先从配置读；
# 读不到保持上面内置默认。板2 = 配置里带 flash_log_header 的那块（只有通信端有）。
_CFG = boardcfg.load(ROOT) if boardcfg else None
if _CFG:
    _bl = [x for x in (_CFG.get('boards') or []) if isinstance(x, dict)]
    _b2 = next((x for x in _bl
                if x.get('dir') and x.get('flash_log_header') and x.get('flash_header')), None)
    if _b2:
        INC2 = os.path.join(_b2['dir'], 'FWLIB', 'inc')
        LOG_H_REL = os.path.join(_b2['dir'], _b2['flash_log_header'])
        FLASH_H_REL = os.path.join(_b2['dir'], _b2['flash_header'])
        MAIN2_REL = os.path.join(_b2['dir'], 'main.c')
    _b1 = next((x for x in _bl if x.get('dir') and x is not _b2), None)
    if _b1:
        MAIN1_REL = os.path.join(_b1['dir'], 'main.c')

RE_TICKS = re.compile(r'pdMS_TO_TICKS\s*\(\s*(\d+)\s*\)')
RE_FUNC_NAME = re.compile(r'^\s*(?:static\s+)?void\s+(\w+)\s*\(')
RE_REPLAY_MACRO = re.compile(r'#\s*define\s+(\w*REPLAY\w*)\s+(\d+)\s*$', re.M)


def task_delay(text, hint, rel):
    """在 text 里找"函数名含 hint 的那个函数"体内第一个 pdMS_TO_TICKS(N) → (N, 出处)。"""
    lines = text.splitlines()
    for i, ln in enumerate(lines):
        fm = RE_FUNC_NAME.match(ln)
        if not fm or hint.lower() not in fm.group(1).lower():
            continue
        for j in range(i, min(i + 400, len(lines))):
            m = RE_TICKS.search(lines[j])
            if m:
                return int(m.group(1)), '%s:%d（任务 %s）' % (rel, j + 1, fm.group(1))
    return None, ''


def guess_periods(root):
    """返回 (上传周期ms, 出处, 补传ms, 出处)；读不到就是 None / ''。"""
    period, psrc = None, ''
    t1 = read_text(os.path.join(root, MAIN1_REL))
    if t1:
        for hint in ('Sensor', 'Report', 'Upload', 'Env', 'Sample', 'Comm'):
            period, psrc = task_delay(t1, hint, MAIN1_REL)
            if period:
                break
    replay, rsrc = None, ''
    t2 = read_text(os.path.join(root, MAIN2_REL))
    if t2:
        m = RE_REPLAY_MACRO.search(t2)
        if m:
            replay, rsrc = int(m.group(2)), '%s（宏 %s）' % (MAIN2_REL, m.group(1))
        else:
            replay, rsrc = task_delay(t2, 'Replay', MAIN2_REL)
    if replay is None:                     # 宏也可能写在板2 的某个头文件里
        inc2 = os.path.join(root, INC2)
        if os.path.isdir(inc2):
            for fn in sorted(os.listdir(inc2)):
                if not fn.endswith('.h'):
                    continue
                t = read_text(os.path.join(inc2, fn))
                m = RE_REPLAY_MACRO.search(t or '')
                if m:
                    replay = int(m.group(2))
                    rsrc = '%s%s（宏 %s）' % (INC2, fn, m.group(1))
                    break
    return period, psrc, replay, rsrc


def num_of(name, pool, depth=0):
    """把宏名算成整数：支持字面量、纯算术与宏引用（如 SLOT - HDR）。
    算不出（含函数调用、类型、字符串）返回 None，不猜。"""
    if depth > 8:
        return None
    expr = pool.get(name)
    if expr is None:
        return None
    expr = expr.split('/*')[0].strip()
    # 去掉整数字面量的 U/L 后缀（16U → 16、0x001000UL → 0x001000）
    expr = re.sub(r'([0-9A-Fa-f])[uUlL]+\b', r'\1', expr)

    def repl(m):
        n = m.group(1)
        v = num_of(n, pool, depth + 1)
        return str(v) if v is not None else n

    expr = re.sub(r'\b([A-Za-z_]\w*)\b', repl, expr)
    # 把 16 进制字面量里的 x 排除掉，再判"还剩没算出来的标识符"
    if re.search(r'[A-Za-z_]', re.sub(r'0[xX][0-9A-Fa-f]+', '0', expr)):
        return None
    try:
        v = eval(expr, {'__builtins__': {}}, {})
    except Exception:
        return None
    return int(v) if isinstance(v, int) else None


def fmt_bytes(n):
    if n >= 1024 * 1024:
        return '%.1f MB' % (n / 1024.0 / 1024.0)
    if n >= 1024:
        return '%.1f KB' % (n / 1024.0)
    return '%d B' % n


def fmt_dur(sec):
    if sec >= 3600:
        return '%.1f 小时' % (sec / 3600.0)
    if sec >= 60:
        return '%.1f 分钟' % (sec / 60.0)
    return '%.1f 秒' % sec


def fmt_period(ms):
    """把上传周期毫秒数写成人话（1 秒 / 10 秒 / 30 秒；非整秒就写 ms）。"""
    if ms % 1000 == 0:
        return '%d 秒' % (ms // 1000)
    return '%d ms' % ms


def period_rows(stats, replay_ms):
    """B1：换个上传周期再算一遍 —— 同一块缓存，周期越长越能扛，但补传越追不上。
    返回 [(周期ms, 写入速率, 断网可扛秒, 补传结论)]。"""
    cap = stats.get('capacity_slots') or 0
    rows = []
    for ms in (1000, 5000, 10000, 30000):
        hold = cap * ms / 1000.0
        if replay_ms > ms:
            verdict = '补传慢于写入：积压补不完，边补边覆盖（这一档别选）'
        else:
            net = 1000.0 / replay_ms - 1000.0 / ms if (ms > 0 and replay_ms > 0) else 0
            catch = (cap / net) if net > 0 else None
            verdict = ('补传快于写入，满缓存约 %s 补完' % fmt_dur(catch)) if catch \
                else '补传快于写入'
        rows.append((ms, 1000.0 / ms if ms > 0 else 0, hold, verdict,
                     '注意' if replay_ms > ms else 'OK'))
    return rows


def analyze(log_text, flash_text, period_ms, replay_ms, record_bytes):
    """返回 (findings, stats)。findings 每项 (等级, 位置, 现象, 键)。"""
    findings = []
    stats = {}
    lp = collect_defines(log_text)
    fp = collect_defines(flash_text)
    # 两份头文件合成一个宏池：日志头里的 W25QXX_LOG_BASE 是引用 w25qxx.h 的 ADDR_LOG，
    # 单看一份算不出来。
    pool = dict(fp)
    pool.update(lp)

    slot = num_of('W25QXX_LOG_SLOT_SIZE', pool)
    hdr = num_of('W25QXX_LOG_HDR_SIZE', pool)
    sectors = num_of('W25QXX_LOG_SECTORS', pool)
    base = num_of('W25QXX_LOG_BASE', pool)
    sector = num_of('W25QXX_SECTOR_SIZE', pool)
    addr_font = num_of('W25QXX_ADDR_FONT', pool)
    addr_log = num_of('W25QXX_ADDR_LOG', pool)

    stats.update({'slot_size': slot, 'hdr_size': hdr, 'sectors': sectors,
                  'base': base, 'sector_size': sector, 'addr_font': addr_font,
                  'period_ms': period_ms, 'replay_ms': replay_ms,
                  'record_bytes': record_bytes})

    if None in (slot, hdr, sectors, sector):
        findings.append(('严重', LOG_H_REL,
                         '缓存几何宏算不出来（SLOT_SIZE/HDR_SIZE/SECTORS/SECTOR_SIZE），'
                         '无法核算；先确认宏名没被改', 'geom'))
        return findings, stats

    payload = slot - hdr
    stats['payload'] = payload

    # F1 记录是否跨扇区：SLOT 必须能整除扇区大小，且为 4 的倍数
    if slot % 4 != 0:
        findings.append(('严重', LOG_H_REL,
                         'SLOT_SIZE = %d 不是 4 的倍数，记录可能跨字对齐边界' % slot, 'geom'))
    if sector % slot != 0:
        findings.append(('严重', LOG_H_REL,
                         'SLOT_SIZE = %d 不能整除扇区 %d：记录会跨扇区，'
                         '擦除一个扇区会连带撕掉邻扇区的记录' % (slot, sector), 'geom'))
    if hdr >= slot:
        findings.append(('严重', LOG_H_REL,
                         'HDR_SIZE = %d 不小于 SLOT_SIZE = %d：没有数据空间' % (hdr, slot), 'geom'))

    # F2 每条记录装得下线上记录吗
    if record_bytes > payload:
        findings.append(('严重', LOG_H_REL,
                         '一条记录载荷 %d 字节，但线上记录需要 %d 字节：帧塞不进一条记录，'
                         '要么调大 SLOT_SIZE，要么拆条' % (payload, record_bytes), 'record'))
    elif record_bytes == payload:
        findings.append(('注意', LOG_H_REL,
                         '一条记录载荷 %d 字节正好等于线上记录长度，没有余量：'
                         '线上帧一旦变长（例如加个字段）就塞不下，要同步调 SLOT_SIZE'
                         % payload, 'record'))

    # F3 日志区是否落在分区内
    if addr_font is not None:
        end = base + sectors * sector
        stats['log_end'] = end
        if base != addr_log:
            findings.append(('注意', LOG_H_REL,
                             'W25QXX_LOG_BASE(0x%X) 与分区表里的 ADDR_LOG(0x%X) 不一致'
                             % (base, addr_log), 'layout'))
        if end > addr_font:
            findings.append(('严重', LOG_H_REL,
                             '日志区 0x%X ~ 0x%X 越过了下一个分区上界 ADDR_FONT(0x%X)：'
                             '会覆盖字库/图片分区' % (base, end, addr_font), 'layout'))

    # F4 容量与"能扛多久"
    per_sector = sector // slot if slot else 0
    cap_slots = sectors * per_sector
    cap_bytes = sectors * sector
    stats.update({'per_sector': per_sector, 'capacity_slots': cap_slots,
                  'capacity_bytes': cap_bytes})
    hold_sec = cap_slots * period_ms / 1000.0 if period_ms > 0 else 0
    stats['hold_seconds'] = hold_sec
    stats['hold_text'] = fmt_dur(hold_sec)

    # F5 补传会不会比写入慢（边补边覆盖）
    stats['fill_per_sec'] = 1000.0 / period_ms if period_ms > 0 else 0
    stats['drain_per_sec'] = 1000.0 / replay_ms if replay_ms > 0 else 0
    if replay_ms > period_ms:
        findings.append(('严重', '推演',
                         '补传一条 %d ms 比写入一条 %d ms 还慢：断网期间积压的 %d 条'
                         '永远补不完，环形区会边补边覆盖，未补数据直接丢'
                         '（对应 W25QXX_LogLost 非 0）'
                         % (replay_ms, period_ms, cap_slots), 'replay'))
    else:
        net = 1000.0 / replay_ms - 1000.0 / period_ms
        stats['catchup_sec'] = cap_slots / net if net > 0 else None
        if stats['catchup_sec'] is not None:
            findings.append(('注意', '推演',
                             '补传快于写入，断网 %s 满缓存后约需 %s 才能补完（净速率 %.2f 条/秒）；'
                             '补传期间若又断网，积压会重新叠加' % (
                                 fmt_dur(hold_sec), fmt_dur(stats['catchup_sec']), net), 'catchup'))

    # F6 扇区粒度的连带丢失（教学提示，总是给）
    if per_sector:
        findings.append(('注意', LOG_H_REL,
                         'Flash 最小擦除单位是整个扇区（%d 条/扇区）：绕回某扇区首槽时整扇擦除，'
                         '同扇区里还没读出的记录会被连带丢掉（这是 W25QXX_LogLost 的来源）'
                         % per_sector, 'sector'))
    return findings, stats


# B2：这几条"注意"原来只写风险，没写"怎么改、改完怎么验"。按项目"教方法"的口径补上 ——
# 每条给一个可执行的动作 + 一个能观测的验收判据（按 finding 的 key 挂）。
# 只加报告文字，不动判定逻辑、也不动 JSON 字段（面板读的仍是 findings 那 4 个字段）。
FIX_NOTE = {
    'record':
        '- **改法**：给一条记录留余量。板2 `FWLIB\\inc\\w25qxx_log.h` 的 `W25QXX_LOG_SLOT_SIZE` '
        '往大改 —— 要选**能整除扇区 4096 且是 4 的倍数**的值（例如 64 → 载荷 52 字节；'
        '36 / 40 / 48 都除不尽 4096，别选），代价是容量从 2048 条降到 1024 条；'
        '或者反过来把线上帧里可省的字段裁掉，让帧长度小于载荷。\n'
        '  - **验收**：重跑本工具，看「单条数据载荷」> 「一条线上记录的字节数」；'
        '再重跑协议校验，确认两板的帧长与常量跟新的 SLOT 对得上。',
    'catchup':
        '- **改法**：现在补传比写入快、但余量不大 —— 想更稳就 ① 板2 补传任务把 `REPLAY_PERIOD_MS` '
        '调小（或一轮补多条），或 ② 板1 上报任务把 `vTaskDelay(pdMS_TO_TICKS(...))` 调大。\n'
        '  - **验收**：重跑本工具，看「净速率」变大、「补完满缓存」变短；'
        '再做一次"断网攒满 → 恢复"的实测，串口里 `W25QXX_LogLost()` 应保持 0。',
    'sector':
        '- **改法**：这是 Flash 的硬件限制（最小擦除单位是整扇区），不是"关掉"的问题 —— '
        '工程里已经按"擦之前先数出会被连带擦掉的条数、扣掉并计入 `s_lost`"处理'
        '（见 `FWLIB\\src\\w25qxx_log.c` 擦除那一支）。所以要做的是把它**用起来**：'
        '① 让上报 / OLED / TFT 在 `W25QXX_LogLost()` 非 0 时给出明确提示（"断网太久，丢了 N 条"），'
        '别让上层以为数据是完整的；② 想少丢就调小 `W25QXX_LOG_SLOT_SIZE`'
        '（每扇区条数变多、单次连带丢的占比变小），或放宽上传周期 / 加快补传，别让缓存被撑满。\n'
        '  - **验收**：连续写满 `每扇区条数 + 1` 条（正好绕回扇区首槽）复现一次，'
        '确认 `W25QXX_LogLost()` 的增量**正好等于**实际丢掉的条数（不多不少）。',
}


def render(findings, stats):
    severe = sum(1 for f in findings if f[0] == '严重')
    warn = sum(1 for f in findings if f[0] == '注意')
    out = ['# 断网缓存核算报告', '',
           '来源：`本地工具/工具箱/检查/断网缓存/cache_check.py`（只读核算，可重复运行）', '']

    # D4：把两个"待你确认的假设"摆到最前面 —— 免得拿默认值当实测值，结论看着像实测。
    out.append('## 待确认的假设（先看这里，结论都建立在这两条上）')
    out.append('')
    out.append('| 假设 | 本次取值 | 来源 |')
    out.append('|---|---|---|')
    out.append('| 上传周期 | %s ms | %s |'
               % (stats.get('period_ms'), stats.get('period_src') or '默认值'))
    out.append('| 补传每条耗时 | %s ms | %s |'
               % (stats.get('replay_ms'), stats.get('replay_src') or '默认值'))
    out.append('| 一条线上记录的字节数 | %s B | **命令行参数**（--record-bytes） |'
               % stats.get('record_bytes'))
    out.append('')
    out.append('> 上传周期与补传节奏**优先从工程里读**（采集/上报任务的 '
               '`vTaskDelay(pdMS_TO_TICKS(...))`、补传任务的 `REPLAY_PERIOD_MS`），'
               '读不到才用命令行/默认值。上面第三列就是本次的取值来源 —— 显示"默认值"说明'
               '工程里没找到，请按实际的采集/上报与补传节奏填，否则「断网可扛」「追不追得上」'
               '的结论都不成立。')
    out.append('')

    out.append('## 摘要')
    out.append('')
    out.append('| 项 | 值 |')
    out.append('|---|---|')
    out.append('| 结论 | **%s** |' % ('通过' if severe == 0 else '不通过'))
    out.append('| 严重 / 注意 | %d / %d |' % (severe, warn))
    out.append('| 缓存容量 | %s（%d 条 × %s/条） |' % (
        fmt_bytes(stats.get('capacity_bytes') or 0), stats.get('capacity_slots') or 0,
        fmt_bytes(stats.get('slot_size') or 0)))
    out.append('| 上传周期 | %s ms |' % stats.get('period_ms'))
    out.append('| 补传每条 | %s ms |' % stats.get('replay_ms'))
    out.append('| 断网可扛 | %s（约 %d 条） |' % (
        stats.get('hold_text') or '—', stats.get('capacity_slots') or 0))
    out.append('')

    out.append('## 缓存几何（取自 w25qxx_log.h / w25qxx.h）')
    out.append('')
    out.append('| 项 | 值 |')
    out.append('|---|---|')
    out.append('| 每条槽位 SLOT_SIZE | %s B |' % stats.get('slot_size'))
    out.append('| 槽内头部 HDR_SIZE | %s B（序号4 + 时间4 + 长度2 + CRC2） |' % stats.get('hdr_size'))
    out.append('| 单条数据载荷 | %s B |' % stats.get('payload'))
    out.append('| 日志扇区数 | %s × %s B = %s |' % (
        stats.get('sectors'), stats.get('sector_size'), fmt_bytes(stats.get('capacity_bytes') or 0)))
    out.append('| 每扇区条数 | %s |' % stats.get('per_sector'))
    out.append('')

    out.append('## 推演')
    out.append('')
    out.append('- 写入速率：%.3f 条/秒（上传周期 %s ms）' % (
        stats.get('fill_per_sec') or 0, stats.get('period_ms')))
    out.append('- 补传速率：%.3f 条/秒（每条 %s ms）' % (
        stats.get('drain_per_sec') or 0, stats.get('replay_ms')))
    out.append('- 断网可扛：%s（满缓存 %d 条）' % (
        stats.get('hold_text'), stats.get('capacity_slots') or 0))
    if stats.get('catchup_sec'):
        out.append('- 补完满缓存：约 %s' % fmt_dur(stats['catchup_sec']))
    out.append('')

    # B1：周期对照表 —— 一次把几档上传周期各自能扛多久、补传结论列出来，方便挑周期。
    cap_slots = stats.get('capacity_slots') or 0
    if cap_slots:
        out.append('## 上传周期对照表（挑周期用）')
        out.append('')
        out.append('同一块缓存（%d 条）、补传每条固定 %s ms，换上传周期试算：' % (
            cap_slots, stats.get('replay_ms')))
        out.append('')
        out.append('| 上传周期 | 写入速率 | 断网可扛 | 补传结论 |')
        out.append('|---|---|---|---|')
        for ms, rate, hold, verdict, _lv in period_rows(stats, stats.get('replay_ms') or 0):
            mark = '（当前）' if ms == stats.get('period_ms') else ''
            out.append('| %s%s | %.3f 条/秒 | %s | %s |' % (
                fmt_period(ms), mark, rate, fmt_dur(hold), verdict))
        out.append('')
        out.append('> 周期越长越能扛，但写入越慢、补传越容易追上；周期越短能扛的时长越短。'
                   '挑周期时两头都看一眼，别只看"断网可扛"。')
        out.append('')

    if findings:
        out.append('## 发现')
        out.append('')
        for lv, loc, msg, key in findings:
            out.append('- **[%s]** %s — %s' % (lv, loc, msg))
            # B2：命中"注意"时紧跟一条"怎么改 + 改完怎么验"，别只丢个风险就走人
            fix = FIX_NOTE.get(key)
            if fix:
                out.append(fix)
        out.append('')

    # B1：命中后具体先做什么（只加报告文字，不动判定逻辑与 JSON 字段）
    out.append('## 下一步怎么做')
    out.append('')
    out.append('- 先看最上面「待确认的假设」：**来源若不是"工程里读到"，结论就只是按默认值试算** —— '
               '先把采集/上报周期与补传节奏填对（或让工具自己读到），再看下面的数。')
    out.append('- 判到"补传慢于写入"（严重）：要么加快补传（调小 `--replay-ms` 对应的实际节奏），'
               '要么放宽上传周期；两者都改不动时，靠 `W25QXX_LogLost` 报警来兜。')
    out.append('- 判到"记录塞不下 / 跨扇区 / 越过分区上界"：动的是板2 的 `w25qxx_log.h`'
               '（`SLOT_SIZE` / `SECTORS`）与分区表，改完**重跑本工具 + 重跑协议校验**'
               '（线上帧长度可能跟着变）。')
    out.append('- 「上传周期对照表」用来挑周期：别只看"能扛多久"，同看它的补传结论那一列，'
               '尽量挑"补传追得上"的档。')
    out.append('- 扇区粒度的连带丢失是硬件限制：要么留余量，要么在 `W25QXX_LogLost` 非 0 时报警'
               '（这条不是可改的 bug）。')
    out.append('')

    out.append('## 说明')
    out.append('')
    out.append('1) 上传周期与补传节奏是**参数**（--period-ms / --replay-ms），默认值只是示例；'
               '报告最上方「待确认的假设」一节把本次取值摆出来了，请按工程实际填，结论才有意义。')
    out.append('2) 缓存几何全部读自 `w25qxx_log.h` 与 `w25qxx.h`，不内置任何印象数据。')
    out.append('3) "断网可扛"按"每个上传周期写 1 条"估算；若一次上报写多条，把 --period-ms 按等效周期填。')
    out.append('4) 扇区粒度导致的连带丢失是硬件限制：要么留余量、要么在 W25QXX_LogLost 非 0 时报警。')
    out.append('5) 「上传周期对照表」固定补传每条（--replay-ms），只换上传周期试算，'
               '方便对比挑周期；表里的"（当前）"就是本次实际用的那档。')
    out.append('')
    return '\n'.join(out)


def to_json(findings, stats):
    return {
        'tool': 'cache_check',
        'schema': 1,
        'ok': not any(f[0] == '严重' for f in findings),
        'capacity_slots': stats.get('capacity_slots'),
        'capacity_bytes': stats.get('capacity_bytes'),
        'slot_size': stats.get('slot_size'),
        'payload': stats.get('payload'),
        'period_ms': stats.get('period_ms'),
        'replay_ms': stats.get('replay_ms'),
        'hold_seconds': stats.get('hold_seconds'),
        'hold_text': stats.get('hold_text'),
        'severe': sum(1 for f in findings if f[0] == '严重'),
        'warn': sum(1 for f in findings if f[0] == '注意'),
        'notes': len(findings),
        'findings': [{'level': lv, 'loc': loc, 'msg': msg, 'key': k}
                     for lv, loc, msg, k in findings],
    }


LOG_SAMPLE = (
    '#define W25QXX_LOG_BASE         W25QXX_ADDR_LOG\n'
    '#define W25QXX_LOG_SECTORS      16U\n'
    '#define W25QXX_LOG_SLOT_SIZE    32U\n'
    '#define W25QXX_LOG_HDR_SIZE     12U\n'
)
FLASH_SAMPLE = (
    '#define W25QXX_SECTOR_SIZE  4096UL\n'
    '#define W25QXX_ADDR_LOG     0x001000UL\n'
    '#define W25QXX_ADDR_FONT    0x020000UL\n'
)


def selftest():
    """在内存里造几组宏文本核对判定（不碰仓库、不写报告）。"""
    checks = []

    def ck(name, ok, extra=''):
        checks.append((name, bool(ok), extra))

    def run(log_text, flash_text, period_ms=1000, replay_ms=50, record_bytes=20):
        return analyze(log_text, flash_text, period_ms, replay_ms, record_bytes)

    f, s = run(LOG_SAMPLE, FLASH_SAMPLE)
    ck('正常几何：算出容量 64KB / 2048 条 / 载荷 20B',
       s['capacity_bytes'] == 65536 and s['capacity_slots'] == 2048 and s['payload'] == 20,
       str(s.get('capacity_slots')))
    ck('正常几何：断网可扛 = 2048 条 × 1s ≈ 34.1 分钟',
       abs(s['hold_seconds'] - 2048.0) < 0.01, str(s.get('hold_seconds')))
    ck('反例：正常的扇区/条数/分区边界不报"严重"',
       not any(lv == '严重' for lv, _, _, _ in f),
       '、'.join(m for lv, _, m, _ in f if lv == '严重') or '无')

    # 错例：SLOT_SIZE 30（不能整除 4096）
    f2, _ = run(LOG_SAMPLE.replace('W25QXX_LOG_SLOT_SIZE    32U',
                                   'W25QXX_LOG_SLOT_SIZE    30U'), FLASH_SAMPLE)
    ck('错例：SLOT_SIZE 不能整除扇区 → 严重',
       any(k == 'geom' and lv == '严重' and '跨扇区' in m for lv, _, m, k in f2))

    # 错例：16 扇区改成 256 扇区（越过 ADDR_FONT）
    f3, s3 = run(LOG_SAMPLE.replace('W25QXX_LOG_SECTORS      16U',
                                    'W25QXX_LOG_SECTORS      256U'), FLASH_SAMPLE)
    ck('错例：日志区越过分区上界 → 严重',
       any(k == 'layout' and lv == '严重' for lv, _, m, k in f3))

    # 错例：线上记录 24 字节 > 载荷 20 → 严重
    f4, _ = run(LOG_SAMPLE, FLASH_SAMPLE, record_bytes=24)
    ck('错例：线上记录比一条载荷长 → 严重',
       any(k == 'record' and lv == '严重' for lv, _, m, k in f4))
    # 反例：记录正好等于载荷 → 只提示（不严重）
    f4b, _ = run(LOG_SAMPLE, FLASH_SAMPLE, record_bytes=20)
    ck('反例：记录正好等于载荷 → 只提示"没有余量"，不判严重',
       any(k == 'record' and lv == '注意' for lv, _, m, k in f4b)
       and not any(k == 'record' and lv == '严重' for lv, _, m, k in f4b))

    # 错例：补传 2000ms 比写入 1000ms 慢 → 严重
    f5, _ = run(LOG_SAMPLE, FLASH_SAMPLE, period_ms=1000, replay_ms=2000)
    ck('错例：补传慢于写入 → 严重（边补边覆盖）',
       any(k == 'replay' and lv == '严重' for lv, _, m, k in f5))
    # 反例：补传 50ms 快于写入 1000ms → 不报 replay 严重
    ck('反例：补传快于写入 → 不报 replay 严重（改为给追平时间）',
       not any(k == 'replay' for lv, _, m, k in f)
       and any(k == 'catchup' for lv, _, m, k in f))

    # B2：几条"注意"必须带"怎么改 + 改完怎么验"（错例：只写风险、不给动作 → 这几条就红）
    for _k in ('record', 'catchup', 'sector'):
        _fix = FIX_NOTE.get(_k, '')
        ck('B2 “%s”那条带改法与验收' % _k, '改法' in _fix and '验收' in _fix, _fix[:30])
    _rep_b2 = render(f, s)
    ck('B2 报告的「发现」一节里把改法与验收印出来了（各 ≥3 处）',
       _rep_b2.count('**改法**') >= 3 and _rep_b2.count('**验收**') >= 3,
       '改法 %d 处 / 验收 %d 处' % (_rep_b2.count('**改法**'), _rep_b2.count('**验收**')))
    ck('B2 反例：FIX_NOTE 只给这三条挂改法（别的 key 不会凭空印出"改法"字样）',
       set(FIX_NOTE) == {'record', 'catchup', 'sector'}, '、'.join(sorted(FIX_NOTE)))

    # B2 从工程读参数：错例（采集任务 vTaskDelay(pdMS_TO_TICKS(2000)) → 读到 2000）
    #                 + 反例（工程里没有这些文件 → 返回 None，不瞎猜）
    import shutil
    import tempfile
    d3 = tempfile.mkdtemp(prefix='cache_b2_')
    d4 = tempfile.mkdtemp(prefix='cache_b2b_')
    try:
        m1 = os.path.join(d3, MAIN1_REL)
        m2 = os.path.join(d3, MAIN2_REL)
        os.makedirs(os.path.dirname(m1))
        os.makedirs(os.path.dirname(m2))
        with open(m1, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write('void vTaskSensor(void *p)\n{\n    vTaskDelay(pdMS_TO_TICKS(2000));\n}\n')
        with open(m2, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write('#define REPLAY_PERIOD_MS 50\n'
                     'void vTaskReplay(void *p)\n{\n    vTaskDelay(pdMS_TO_TICKS(REPLAY_PERIOD_MS));\n}\n')
        g_p, g_ps, g_r, g_rs = guess_periods(d3)
        ck('错例：从工程读到采集周期 2000ms（出处带任务名与行号）',
           g_p == 2000 and 'vTaskSensor' in g_ps, str((g_p, g_ps)))
        ck('错例：从工程读到补传节奏 50ms（来自 REPLAY_PERIOD_MS 宏）',
           g_r == 50 and 'REPLAY' in g_rs, str((g_r, g_rs)))
        ck('反例：工程里没有这些文件 → 读不到就返回 None（不瞎猜）',
           guess_periods(d4) == (None, '', None, ''), str(guess_periods(d4)))
    finally:
        shutil.rmtree(d3, ignore_errors=True)
        shutil.rmtree(d4, ignore_errors=True)

    # 缺宏：算不出来要报严重，而不是静默通过
    f6, _ = run('#define W25QXX_LOG_SECTORS 16U\n', FLASH_SAMPLE)
    ck('错例：几何宏缺失 → 报严重而不是静默通过',
       any(k == 'geom' and lv == '严重' for lv, _, m, k in f6))

    # B1 上传周期对照表：错例（补传 2000ms 时 1 秒档必须点出"边补边覆盖"）
    f7, s7 = run(LOG_SAMPLE, FLASH_SAMPLE, period_ms=1000, replay_ms=2000)
    rows_bad = period_rows(s7, 2000)
    ck('错例：周期对照表里"补传慢于写入"的档要点明（1 秒档 → 边补边覆盖）',
       any(ms == 1000 and '边补边覆盖' in v for ms, _, _, v, _ in rows_bad),
       str([(ms, v) for ms, _, _, v, _ in rows_bad]))
    # 反例：补传 50ms 快于各档 → 表里不许出现"边补边覆盖"
    ck('反例：补传快于写入时，周期对照表不许出现"边补边覆盖"',
       not any('边补边覆盖' in v for _, _, _, v, _ in period_rows(s, 50)))
    ck('周期对照表：周期越长、断网可扛越久（单调）',
       [r[2] for r in period_rows(s, 50)] == sorted(r[2] for r in period_rows(s, 50)))
    ck('周期对照表：报告里确有这一节',
       '## 上传周期对照表（挑周期用）' in render(f, s))

    # D4 待确认假设：错例（报告最上方必须列出假设一节）/ 反例（不许把它塞到摘要后面）
    rep = render(f, s)
    ck('D4：报告最上方有「待确认的假设」一节，且排在摘要之前',
       '## 待确认的假设' in rep
       and rep.index('## 待确认的假设') < rep.index('## 摘要'))
    ck('D4：假设一节把本次的上传周期/补传耗时取值写出来',
       ('| 上传周期 | %s ms |' % s.get('period_ms')) in rep
       and ('| 补传每条耗时 | %s ms |' % s.get('replay_ms')) in rep)

    j = to_json(f, s)
    ck('JSON 结构正确（工具名/ok/容量/可扛时长）',
       j['tool'] == 'cache_check' and j['ok'] is True
       and j['capacity_slots'] == 2048 and j['hold_seconds'] > 0)
    # B6 JSON 契约：面板读 severe/warn/hold_text/容量，字段名/类型一改就红
    ck('JSON 契约：面板读取的字段名/类型都在',
       set(j) == {'tool', 'schema', 'ok', 'capacity_slots', 'capacity_bytes',
                  'slot_size', 'payload', 'period_ms', 'replay_ms', 'hold_seconds',
                  'hold_text', 'severe', 'warn', 'notes', 'findings'}
       and isinstance(j['severe'], int) and isinstance(j['warn'], int)
       and isinstance(j['hold_text'], str)
       and set(j['findings'][0]) == {'level', 'loc', 'msg', 'key'},
       '、'.join(sorted(j)))

    # C1+C2 板级配置读取器（换装口）：正例读到 2 块板 / 反例配置被改名后返回 None
    if boardcfg is None:
        ck('板级配置读取器可用（boardcfg 导入成功）', False, 'boardcfg 导入失败')
    else:
        ck('板级配置读取器可用（boardcfg 导入成功）', True)
        rb = boardcfg.boards(ROOT)
        ck('正例：仓库真配置读出 3 块板（板1 / 板2 / OTA 工程）、名字与 board_envmonitor.json 一致',
           rb is not None and len(rb) == 3
           and {n for n, _ in rb} == {'板1 检测端 (F407ZE)', '板2 通信端 (F407ZG)',
                                      'OTA 工程 (F407ZE)'},
           str(rb))
        ck('正例：板2 的 Flash 头文件路径取自配置（落在 Project_2 下）',
           os.path.normpath(LOG_H_REL).replace('\\', '/')
           == 'EnvMonitor/Project_2_数据通信端/Project/FWLIB/inc/w25qxx_log.h',
           LOG_H_REL)
        dcfg = tempfile.mkdtemp(prefix='boardcfg_selftest_')
        try:
            rp = os.path.join(dcfg, '本地工具', '工具箱', '速查数据', 'rules')
            os.makedirs(rp)
            cfgp = os.path.join(rp, 'board_envmonitor.json')
            with open(cfgp, 'w', encoding='utf-8', newline='\n') as fh:
                json.dump({'version': 1, 'boards': [
                    {'name': '板1 假配置', 'dir': 'x/y'},
                    {'name': '板2 假配置', 'dir': 'z/w'}]}, fh, ensure_ascii=False)
            ck('正例：临时假配置读到 2 块板（名字取自假配置）',
               boardcfg.boards(dcfg) == [('板1 假配置', 'x/y'), ('板2 假配置', 'z/w')],
               str(boardcfg.boards(dcfg)))
            os.rename(cfgp, cfgp + '.bak')
            ck('反例：配置被临时改名后返回 None（脚本回退内置默认，不崩）',
               boardcfg.load(dcfg, fallback=False) is None
               and boardcfg.boards(dcfg, fallback=False) is None)
        finally:
            shutil.rmtree(dcfg, ignore_errors=True)

    bad = [c for c in checks if not c[1]]
    for name, ok, extra in checks:
        print('  [%s] %s%s' % ('PASS' if ok else 'FAIL', name,
                               ('  （实际：%s）' % extra) if (extra and not ok) else ''))
    print('断网缓存自检：%d 通过 / %d 失败' % (len(checks) - len(bad), len(bad)))
    return 1 if bad else 0


def main(argv):
    if '--selftest' in argv:
        return selftest()

    quiet = False
    json_mode = False
    period_ms, replay_ms, record_bytes = 1000, 50, 20
    bad = []
    given_period, given_replay = False, False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == '--quiet':
            quiet = True
            i += 1
        elif a == '--json':
            json_mode = True
            i += 1
        elif a in ('--period-ms', '--replay-ms', '--record-bytes'):
            if i + 1 >= len(argv):
                bad.append(a + ' 缺少值')
                i += 1
                continue
            try:
                v = int(argv[i + 1])
            except ValueError:
                bad.append('%s 的值不是整数：%s' % (a, argv[i + 1]))
                i += 2
                continue
            if a == '--period-ms':
                period_ms, given_period = v, True
            elif a == '--replay-ms':
                replay_ms, given_replay = v, True
            else:
                record_bytes = v
            i += 2
        else:
            bad.append(a)
            i += 1

    if bad:
        print('未知参数：%s' % ' '.join(bad))
        print('可用参数：--period-ms <毫秒> | --replay-ms <毫秒> | --record-bytes <字节> '
              '| --quiet | --json | --selftest')
        return 2

    log_text = read_text(os.path.join(ROOT, LOG_H_REL))
    flash_text = read_text(os.path.join(ROOT, FLASH_H_REL))
    if log_text is None:
        print('找不到 %s（板2 的 w25qxx_log.h），无法核算。' % LOG_H_REL)
        return 2
    if flash_text is None:
        flash_text = ''

    findings, stats = analyze(log_text, flash_text, period_ms, replay_ms, record_bytes)
    # B2：命令行没给的两个参数，试着从工程里读出来（读得到就用，并记出处给报告用）
    g_p, g_ps, g_r, g_rs = guess_periods(ROOT)
    if not given_period and g_p:
        period_ms, period_src = g_p, '工程里读到：%s' % g_ps
        findings, stats = analyze(log_text, flash_text, period_ms, replay_ms, record_bytes)
    else:
        period_src = '命令行参数' if given_period else '默认值（未在工程里找到）'
    if not given_replay and g_r:
        replay_ms, replay_src = g_r, '工程里读到：%s' % g_rs
        findings, stats = analyze(log_text, flash_text, period_ms, replay_ms, record_bytes)
    else:
        replay_src = '命令行参数' if given_replay else '默认值（未在工程里找到）'
    stats['period_src'] = period_src
    stats['replay_src'] = replay_src
    ok = not any(f[0] == '严重' for f in findings)

    if json_mode:
        print(json.dumps(to_json(findings, stats), ensure_ascii=False))
        return 0 if ok else 1

    report = render(findings, stats)
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '断网缓存报告.md')
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(report)

    if not quiet:
        print('\n'.join(report.splitlines()[:16]))
    print('断网缓存核算完成')
    print('  缓存容量: %s（%d 条）' % (fmt_bytes(stats.get('capacity_bytes') or 0),
                                       stats.get('capacity_slots') or 0))
    print('  断网可扛: %s' % stats.get('hold_text'))
    print('  严重/注意: %d / %d' % (
        sum(1 for x in findings if x[0] == '严重'),
        sum(1 for x in findings if x[0] == '注意')))
    print('  报告    : %s' % out_path)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
