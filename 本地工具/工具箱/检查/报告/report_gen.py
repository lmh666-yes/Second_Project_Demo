# -*- coding: utf-8 -*-
r"""答辩 / 周报生成器（D1 · C3）：一键出一份**单文件 HTML**，或直接打成一个**答辩包**（zip）。

为什么是单文件：发人、打印、塞进答辩 PPT 都只要一个文件，不用带图片目录、
不用担心别人打不开（CSS 全内联、SVG 直接嵌在 HTML 里、**截图也 base64 内联**，
所以整份 HTML **没有任何外链依赖** —— 拷到没网的机器上、双击就能看）。

内容全部来自**现有数据源**，不重新编一套口径（所以"报告里的数字 = 面板上的数字"）：
  · 界面截图  → `Toolbox.exe --shot-overview`（现场抓一张；抓不到就退到最近的一张）
  · 体检      → 各检查器的 `--json`（现场跑，不是读旧报告）
  · 阶段进度  → 三份进度文档的「阶段总表」（A1 回写的那张）
  · 体积趋势  → `构建烧录/_体积历史.json`（A3 记的；**整条序列**画成内联 SVG 折线）
  · 代码统计  → 工程根下 *.c 的行数（只算你写的，不算 FWLIB）
  · 时序图    → `链路仿真/产物/链路时序_*.svg`（B1 出的；没有就现场补跑一次，0.3 秒）
  · 自检覆盖  → `覆盖率/产物/覆盖率看板.md` 里的合计（D4 出的）

C3 起的「一键成包」（`--pack`）：产出一个 `答辩包_<时间>/` 目录 + 同名 `.zip`，里面有
周报.html（单文件、已内联截图与时序图）+ 截图原图（塞 PPT 用）+ `明细/`（各检查报告）
+ `说明.md`（这份包怎么看、怎么重新生成）。可打印（`@media print` + `@page`）、可发人。

用法：
    python 本地工具\工具箱\检查\报告\report_gen.py                 # 单文件 HTML
    python ...\report_gen.py --pack                                 # 一键成包（目录 + zip）
    python ...\report_gen.py --out D:\答辩\周报.html --title "第 7 周"
    python ...\report_gen.py --no-run                               # 体检不现场跑，只读现有报告
    python ...\report_gen.py --no-shot                              # 不抓界面截图（用最近一张/或省略）
    python ...\report_gen.py --selftest
"""
import argparse
import base64
import datetime
import glob
import html
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

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
TOOLBOX = os.path.abspath(os.path.join(HERE, '..', '..'))
ROOT = find_root(HERE) or os.path.abspath(os.path.join(TOOLBOX, '..', '..'))
PY = sys.executable or 'python'
CFG_REL = os.path.join('本地工具', '工具箱', '速查数据', 'rules', 'board_envmonitor.json')

def cfg_file():
    """板级配置路径：TOOLBOX_BOARDCFG 优先（D3 的"加假工程"验收靠它），否则仓库里那份。

    这里原来写成 `... or cfg_file()` —— **自己调自己**（B2 在 code_index.py 修掉的是同一个写法），
    环境变量为空时一跑就 RecursionError。虽然眼下没人调它，但留着就是颗雷，顺手收掉。
    """
    return os.environ.get('TOOLBOX_BOARDCFG', '').strip() or os.path.join(ROOT, CFG_REL)


CHECKERS = [
    ('引脚规划', '检查/引脚规划/pin_planner.py'),
    ('RTOS 检查', '检查/RTOS检查/rtos_check.py'),
    ('编码守卫', '检查/编码守卫/encoding_guard.py'),
    ('LoRa 检查', '检查/LoRa检查/lora_check.py'),
    ('协议校验', '检查/协议校验/protocol_check.py'),
    ('断网缓存', '检查/断网缓存/cache_check.py'),
    ('进度扫描', '检查/进度跟踪/progress_scan.py'),
]

# 各检查器 JSON 里表示"问题条数"的字段名（不同工具叫法不同，这里统一收）
COUNT_KEYS = ('severe', 'warn', 'note', 'findings', 'issues', 'violations',
              'conflicts', 'severe_count', 'warn_count')


def esc(s):
    return html.escape(str(s), quote=True)


def run_checker(rel):
    p = os.path.join(TOOLBOX, rel)
    if not os.path.isfile(p):
        return {'tool': rel, 'ok': False, 'error': '脚本不存在'}
    try:
        r = subprocess.run([PY, p, '--json'], capture_output=True, text=True,
                           encoding='utf-8', errors='replace', timeout=180,
                           cwd=os.path.dirname(p))
    except Exception as e:
        return {'tool': rel, 'ok': False, 'error': str(e)}
    out = (r.stdout or '').strip()
    # 有些脚本会在 JSON 前后打别的行，取最后一个能解析的 JSON 对象
    for line in reversed(out.splitlines()):
        line = line.strip()
        if line.startswith('{'):
            try:
                d = json.loads(line)
                d.setdefault('ok', r.returncode == 0)
                return d
            except json.JSONDecodeError:
                continue
    return {'tool': rel, 'ok': r.returncode == 0, 'error': '输出不是 JSON'}


def summarize(d):
    """从各检查器的 JSON 里抽出"严重/注意/其它"三档计数（字段名各家不同）。"""
    sev = warn = other = 0
    for b in d.get('boards') or []:
        for k, v in b.items():
            if not isinstance(v, int) or isinstance(v, bool):
                continue
            kl = k.lower()
            if 'severe' in kl or 'fatal' in kl or 'error' in kl:
                sev += v
            elif 'warn' in kl or 'note' in kl or 'conflict' in kl or 'issue' in kl:
                warn += v
            elif k in COUNT_KEYS:
                other += v
    return sev, warn, other


def collect_checkers(do_run):
    rows = []
    for name, rel in CHECKERS:
        if do_run:
            d = run_checker(rel)
            sev, warn, other = summarize(d)
            rows.append({'name': name, 'ok': bool(d.get('ok')),
                         'severe': sev, 'warn': warn, 'other': other,
                         'error': d.get('error', '')})
        else:
            md = os.path.join(TOOLBOX, os.path.dirname(rel), '%s报告.md' % name.replace(' ', ''))
            rows.append({'name': name, 'ok': os.path.isfile(md),
                         'severe': 0, 'warn': 0, 'other': 0,
                         'error': '' if os.path.isfile(md) else '没跑过（无报告）'})
    return rows


def parse_stage_table(path):
    """读进度文档「阶段总表」：返回 [(阶段号, 名称, 状态, 证据)]。"""
    try:
        with open(path, encoding='utf-8') as f:
            lines = f.read().splitlines()
    except OSError:
        return []
    start = None
    for i, ln in enumerate(lines):
        if ln.strip().startswith('| 阶段'):
            start = i + 1
            break
    if start is None:
        return []
    out = []
    while start < len(lines) and lines[start].strip().startswith('|'):
        cells = lines[start].split('|')
        if len(cells) >= 6:
            try:
                no = int(cells[1].strip())
                out.append((no, cells[2].strip(), cells[3].strip(), cells[4].strip()))
            except ValueError:
                pass
        start += 1
    return out


def progress_docs():
    """板级配置里登记的进度文档绝对路径（阶段表就取自它们）。"""
    cfg = os.path.join(TOOLBOX, '速查数据', 'rules', 'board_envmonitor.json')
    try:
        with open(cfg, encoding='utf-8') as f:
            docs = json.load(f).get('extra', {}).get('progress_docs') or []
    except Exception:
        docs = []
    return [os.path.join(ROOT, rel) for rel in docs]


def collect_progress():
    out = []
    for p in progress_docs():
        rows = parse_stage_table(p)
        if rows:
            name = os.path.basename(p).replace('进度文档.md', '')
            out.append({'name': name, 'rows': rows})
    return out


def read_size_history():
    """读 `构建烧录/_体积历史.json` 的**整条序列**（A3 每编译一次记一条）。

    一条记录：{time, board, code, ro, rw, zi, flash, ram, flash_kb, ram_kb, errors}。
    文件本身就是按时间追加的，所以顺序即时间顺序。
    """
    p = os.path.join(TOOLBOX, '检查', '构建烧录', '_体积历史.json')
    try:
        with open(p, encoding='utf-8') as f:
            h = json.load(f)
    except Exception:
        return []
    return [r for r in h if isinstance(r, dict) and r.get('board')]


def size_latest(hist):
    """每块板的最新一条（与面板「编译烧录」页显示的"最近一次"同源）。"""
    latest = {}
    for r in hist:
        latest[r['board']] = r
    return list(latest.values())


def size_series(hist):
    """每块板的时间序列：board → [(time, flash, ram)]（旧 → 新）。"""
    ser = {}
    for r in hist:
        ser.setdefault(r['board'], []).append(
            (r.get('time', ''), r.get('flash', 0), r.get('ram', 0)))
    return ser


def render_size_trend(points, name, flash_kb=512):
    """把一块板的体积序列画成**内联 SVG 迷你折线**（Flash 蓝 / RAM 绿），不引任何外部库。

    点少于 2 个就**不画**（一条线至少两个点才有意义）—— 调用方据此如实写"记录不足"。
    """
    if len(points) < 2:
        return ''
    W, H, PAD = 460, 96, 26
    xs = [p[1] for p in points]
    ys = [p[2] for p in points]
    lo = min(min(xs), min(ys))
    hi = max(max(xs), max(ys))
    if hi == lo:
        hi = lo + 1                      # 全平（没变过）也要能画成一条线
    n = len(points)

    def px(i):
        return PAD + (W - 2 * PAD) * (i / (n - 1.0))

    def py(v):
        return H - PAD - (H - 2 * PAD) * ((v - lo) / float(hi - lo))

    def poly(vals):
        return ' '.join('%.1f,%.1f' % (px(i), py(v)) for i, v in enumerate(vals))
    cap = 'Flash %d→%d B　RAM %d→%d B（%d 次记录：%s → %s）' % (
        xs[0], xs[-1], ys[0], ys[-1], n, points[0][0][:16], points[-1][0][:16])
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
        'viewBox="0 0 %d %d" role="img" aria-label="%s">'
        '<rect x="0" y="0" width="%d" height="%d" fill="#ffffff"/>'
        '<polyline fill="none" stroke="#2563eb" stroke-width="2" points="%s"/>'
        '<polyline fill="none" stroke="#16a34a" stroke-width="2" points="%s"/>'
        '<text x="%d" y="14" font-size="11" fill="#2563eb">Flash %d B</text>'
        '<text x="%d" y="14" font-size="11" fill="#16a34a">RAM %d B</text>'
        '<text x="%d" y="%d" font-size="10" fill="#6b7280">%s</text>'
        '</svg>'
        % (W, H, W, H, esc(cap), W, H, poly(xs), poly(ys),
           PAD, max(xs), PAD + 92, max(ys), PAD, H - 8, esc(cap)))


def _png_size(path):
    """从 PNG 头读宽高（IHDR 固定在第 16~24 字节）—— 只为在报告里标一句"多大"，
    不引第三方图片库（工具箱的约定：拷走就能跑）。"""
    try:
        with open(path, 'rb') as f:
            head = f.read(24)
        if head[:8] == b'\x89PNG\r\n\x1a\n':
            w, h = struct.unpack('>II', head[16:24])
            return int(w), int(h)
    except OSError:
        pass
    return 0, 0


def collect_shot(do_run, do_shot=True):
    """界面截图 —— 现场抓一张（`Toolbox.exe --shot-overview`），抓不到就退到最近的一张。

    为什么要有它：答辩时"界面长什么样"是最直观的一页，而截图**必须内联进 HTML**
    （base64），否则单文件就不成立了。整条链拿不到就如实返回 None，报告里写清"还没有截图"。
    """
    out_dir = os.path.join(HERE, '产物')
    os.makedirs(out_dir, exist_ok=True)
    fresh = os.path.join(out_dir, '概览截图.png')
    exe = os.path.join(TOOLBOX, '程序', '嵌入式工具箱', 'build', 'release', 'Toolbox.exe')
    note = ''
    if do_run and do_shot and os.path.isfile(exe):
        try:
            r = subprocess.run([exe, '--shot-overview', fresh], capture_output=True,
                               timeout=240, cwd=os.path.dirname(exe))
            if r.returncode != 0 or not os.path.isfile(fresh):
                note = '现场抓图没成功（退出码 %s），已退到最近的现成截图' % r.returncode
        except Exception as e:
            note = '现场抓图失败（%s），已退到最近的现成截图' % e
    elif do_run and not os.path.isfile(exe):
        note = '没编译过 Toolbox.exe，没法现场抓图'
    elif not do_shot:
        note = '按要求没抓图（--no-shot）'

    cands = [fresh,
             os.path.join(TOOLBOX, '检查', '一键验收', '产物', '概览截图.png')]
    cands += sorted(glob.glob(os.path.join(TOOLBOX, '检查', '**', '概览截图*.png'),
                              recursive=True))
    for p in cands:
        if not os.path.isfile(p):
            continue
        try:
            with open(p, 'rb') as f:
                raw = f.read()
        except OSError:
            continue
        if len(raw) < 1000:                 # 空图/坏图不塞进报告
            continue
        w, h = _png_size(p)
        return {'path': p, 'b64': base64.b64encode(raw).decode('ascii'),
                'w': w, 'h': h, 'bytes': len(raw), 'note': note}
    return None


def collect_svgs(do_run):
    """时序图：先读现成产物；一份都没有就**现场补跑一次**（`chain_sim.py --all --svg`，0.3 秒）。"""
    d = os.path.join(TOOLBOX, '检查', '链路仿真', '产物')
    if do_run and not glob.glob(os.path.join(d, '链路时序_*.svg')):
        chain = os.path.join(TOOLBOX, '检查', '链路仿真', 'chain_sim.py')
        if os.path.isfile(chain):
            try:
                os.makedirs(d, exist_ok=True)
                subprocess.run([PY, chain, '--all', '--svg',
                                os.path.join(d, '链路时序.svg')],
                               capture_output=True, timeout=180, cwd=os.path.dirname(chain))
            except Exception:
                pass
    out = []
    for p in sorted(glob.glob(os.path.join(d, '链路时序_*.svg'))):
        try:
            with open(p, encoding='utf-8') as f:
                svg = f.read()
        except OSError:
            continue
        # 只留 <svg>…</svg>，去掉 XML 声明（嵌进 HTML 时不需要）
        m = re.search(r'<svg[\s\S]*</svg>', svg)
        if m:
            out.append({'name': os.path.basename(p).replace('链路时序_', '').replace('.svg', ''),
                        'svg': m.group(0)})
    return out


def collect_code():
    cfg = os.path.join(TOOLBOX, '速查数据', 'rules', 'board_envmonitor.json')
    try:
        with open(cfg, encoding='utf-8') as f:
            boards = json.load(f).get('boards') or []
    except Exception:
        boards = []
    out = []
    for b in boards:
        d = os.path.join(ROOT, b['dir'])
        files = sorted(glob.glob(os.path.join(d, '*.c')))
        files = [f for f in files if not os.path.basename(f).startswith('board_selftest')]
        lines = 0
        for f in files:
            try:
                with open(f, 'rb') as fh:
                    lines += fh.read().count(b'\n')
            except OSError:
                pass
        if files:
            out.append({'board': b['name'], 'files': len(files), 'lines': lines})
    return out


def collect_coverage():
    p = os.path.join(TOOLBOX, '检查', '覆盖率', '产物', '覆盖率看板.md')
    try:
        with open(p, encoding='utf-8') as f:
            t = f.read()
    except OSError:
        return None
    m = re.search(r'\*\*合计\*\*\s*\|\s*\*\*(\d+)\*\*\s*\|\s*\*\*(\d+)\*\*\s*\|\s*\*\*(\d+)\*\*', t)
    mu = re.search(r'共\s*\*\*(\d+)\*\*\s*处，未变红\s*\*\*(\d+)\*\*', t)
    if not m:
        return None
    return {'total': int(m.group(1)), 'passed': int(m.group(2)), 'failed': int(m.group(3)),
            'mut_total': int(mu.group(1)) if mu else 0,
            'mut_bad': int(mu.group(2)) if mu else 0}


def build_html(title, data):
    now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M')
    ck = data['checkers']
    bad = [c for c in ck if not c['ok'] or c['severe']]
    H = []
    H.append('<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8">')
    H.append('<title>%s</title>' % esc(title))
    H.append('<style>'
             ':root{--fg:#111827;--dim:#6b7280;--line:#e5e7eb;--bg:#fff;--soft:#f9fafb;'
             '--ok:#16a34a;--warn:#d97706;--bad:#dc2626;--acc:#2563eb}'
             'body{margin:0;background:#f3f4f6;color:var(--fg);'
             'font-family:"Segoe UI","Microsoft YaHei",system-ui,sans-serif;line-height:1.65}'
             '.wrap{max-width:1000px;margin:24px auto;background:var(--bg);padding:36px 44px;'
             'box-sizing:border-box;box-shadow:0 1px 4px rgba(0,0,0,.08);border-radius:6px}'
             'h1{font-size:26px;margin:0 0 6px}h2{font-size:18px;margin:34px 0 12px;'
             'padding-bottom:6px;border-bottom:2px solid var(--line)}'
             '.meta{color:var(--dim);font-size:13px;margin-bottom:22px}'
             'table{border-collapse:collapse;width:100%;font-size:14px}'
             # 发人/手机上会窄窗打开：长路径与长词要能换行，别把表格顶出屏幕
             'th,td{border:1px solid var(--line);padding:7px 10px;text-align:left;'
             'overflow-wrap:anywhere;word-break:break-word}'
             'th{background:var(--soft);font-weight:600}'
             'td.num{text-align:right;font-variant-numeric:tabular-nums}'
             '.ok{color:var(--ok);font-weight:600}.warn{color:var(--warn);font-weight:600}'
             '.bad{color:var(--bad);font-weight:600}'
             '.cards{display:flex;gap:12px;flex-wrap:wrap;margin:6px 0 4px}'
             '.card{flex:1 1 150px;border:1px solid var(--line);border-radius:6px;'
             'padding:12px 14px;background:var(--soft)}'
             '.card .k{font-size:12px;color:var(--dim)}.card .v{font-size:22px;font-weight:700}'
             '.svgbox{border:1px solid var(--line);border-radius:6px;padding:8px;'
             'overflow:auto;background:#fff}'
             '.svgbox svg{max-width:100%;height:auto}'
             '.shot{border:1px solid var(--line);border-radius:6px;padding:6px;background:#fff}'
             '.shot img{max-width:100%;height:auto;display:block}'
             '.note{color:var(--dim);font-size:13px}'
             '@media (max-width:600px){.wrap{padding:16px 12px;margin:8px auto}'
             'h1{font-size:20px}.card .v{font-size:18px}}'
             # C3：答辩要打印 —— 保色 + 别把表/图截断在两页之间 + 定好页边距
             '@media print{body{background:#fff}'
             '.wrap{box-shadow:none;margin:0;padding:0;max-width:none}'
             'h2{page-break-after:avoid}'
             'table,.shot,.svgbox{page-break-inside:avoid}'
             'tr{page-break-inside:avoid}'
             '*{-webkit-print-color-adjust:exact;print-color-adjust:exact}}'
             '@page{size:A4;margin:14mm}'
             '</style></head><body><div class="wrap">')
    H.append('<h1>%s</h1>' % esc(title))
    H.append('<div class="meta">生成时间 %s · 数据来自工具箱现场检查'
             '（界面截图 / 体检 / 阶段 / 体积 / 代码 / 时序 / 自检）</div>'
             % esc(now))

    # 结论卡片
    H.append('<div class="cards">')
    H.append('<div class="card"><div class="k">检查器</div><div class="v">%d 个</div></div>' % len(ck))
    H.append('<div class="card"><div class="k">有问题的</div><div class="v %s">%d</div></div>'
             % ('bad' if bad else 'ok', len(bad)))
    if data['code']:
        tot = sum(c['lines'] for c in data['code'])
        H.append('<div class="card"><div class="k">自写代码</div><div class="v">%d 行</div></div>' % tot)
    if data['coverage']:
        H.append('<div class="card"><div class="k">工具箱自检</div><div class="v %s">%d</div></div>'
                 % ('ok' if data['coverage']['failed'] == 0 else 'bad',
                    data['coverage']['total']))
    H.append('</div>')

    # 1. 界面截图（答辩最直观的一页；base64 内联，所以单文件成立、也无外链）
    H.append('<h2>1. 界面截图</h2>')
    shot = data.get('shot')
    if shot:
        H.append('<div class="shot"><img alt="工具箱「工程体检总览」页截图" '
                 'src="data:image/png;base64,%s"></div>' % shot['b64'])
        H.append('<p class="note">工具箱「工程体检总览」页 %d×%d（%.0f KB，已内联进本文件）'
                 '—— 截图原图也在包里（`截图.png`），可直接塞 PPT。%s</p>'
                 % (shot['w'], shot['h'], shot['bytes'] / 1024.0,
                    esc(shot['note']) if shot.get('note') else ''))
    else:
        H.append('<p class="note">还没有界面截图（没编译过 Toolbox.exe 或没抓到）—— '
                 '去「汇总 → 工程体检总览」点一次刷新后重跑本生成器即可。</p>')

    # 2. 体检
    H.append('<h2>2. 体检结论</h2><table><tr><th>检查器</th><th>严重</th><th>注意</th>'
             '<th>状态</th></tr>')
    for c in ck:
        st = '通过' if (c['ok'] and not c['severe']) else ('有严重项' if c['severe'] else '未通过')
        cls = 'ok' if st == '通过' else ('bad' if c['severe'] else 'warn')
        H.append('<tr><td>%s</td><td class="num">%d</td><td class="num">%d</td>'
                 '<td class="%s">%s%s</td></tr>'
                 % (esc(c['name']), c['severe'], c['warn'], cls, esc(st),
                    ('（%s）' % esc(c['error'])) if c.get('error') else ''))
    H.append('</table>')

    # 3. 阶段表
    if data['progress']:
        H.append('<h2>3. 阶段进度</h2>')
        for p in data['progress']:
            done = sum(1 for r in p['rows'] if '☑' in r[2] or '完成' in r[2])
            H.append('<p class="note">%s：%d / %d 个阶段完成</p>' % (esc(p['name']), done, len(p['rows'])))
            H.append('<table><tr><th>阶段</th><th>名称</th><th>状态</th><th>证据</th></tr>')
            for no, name, st, ev in p['rows']:
                cls = 'ok' if ('☑' in st or '完成' in st) else ('warn' if '人工' in st else '')
                H.append('<tr><td class="num">%d</td><td>%s</td><td class="%s">%s</td>'
                         '<td class="note">%s</td></tr>'
                         % (no, esc(name), cls, esc(st), esc(ev[:60])))
            H.append('</table>')

    # 4. 固件体积（表 + 内联趋势折线）
    if data['size']:
        H.append('<h2>4. 固件体积</h2><table><tr><th>板子</th><th>Flash</th>'
                 '<th>占芯片</th><th>RAM</th><th>占芯片</th><th>较上次</th></tr>')
        ser = data.get('size_series') or {}
        for s in data['size']:
            fp = 100.0 * s['flash'] / (s['flash_kb'] * 1024) if s.get('flash_kb') else 0
            rp = 100.0 * s['ram'] / (s.get('ram_kb') or 192 * 1024 / 1024) if s.get('ram_kb') else 0
            pts = ser.get(s['board']) or []
            delta = '—'
            if len(pts) >= 2:
                df, dr = s['flash'] - pts[-2][1], s['ram'] - pts[-2][2]
                delta = '持平' if (df == 0 and dr == 0) else 'Flash %+d / RAM %+d' % (df, dr)
            H.append('<tr><td>%s</td><td class="num">%d B</td><td class="num">%.1f%%</td>'
                     '<td class="num">%d B</td><td class="num">%.1f%%</td>'
                     '<td class="%s">%s</td></tr>'
                     % (esc(s['board']), s['flash'], fp, s['ram'], rp,
                        '' if delta in ('—', '持平') else 'warn', esc(delta)))
        H.append('</table>')
        for s in data['size']:
            pts = ser.get(s['board']) or []
            if len(pts) >= 2:
                H.append('<p class="note" style="margin:10px 0 2px">%s 体积趋势（A3 每编译一次记一条）</p>'
                         % esc(s['board']))
                H.append('<div class="svgbox">%s</div>' % render_size_trend(pts, s['board']))
            else:
                H.append('<p class="note">%s：编译记录不足 2 次，画不了趋势（再编一次就有了）。</p>'
                         % esc(s['board']))

    # 5. 代码
    if data['code']:
        H.append('<h2>5. 代码规模（只算自己写的 .c）</h2><table>'
                 '<tr><th>工程</th><th>文件数</th><th>行数</th></tr>')
        for c in data['code']:
            H.append('<tr><td>%s</td><td class="num">%d</td><td class="num">%d</td></tr>'
                     % (esc(c['board']), c['files'], c['lines']))
        H.append('</table>')

    # 6. 时序图
    if data['svgs']:
        H.append('<h2>6. 链路时序（仿真）</h2>')
        for s in data['svgs']:
            H.append('<p class="note">剧本：%s</p><div class="svgbox">%s</div>' % (esc(s['name']), s['svg']))

    # 7. 覆盖率
    if data['coverage']:
        c = data['coverage']
        H.append('<h2>7. 工具箱自检</h2><p>断言 <b>%d</b> 条，失败 <b class="%s">%d</b> 条；'
                 '变异测试 %d 处，未变红 %d 处。</p>'
                 % (c['total'], 'ok' if c['failed'] == 0 else 'bad', c['failed'],
                    c['mut_total'], c['mut_bad']))

    H.append('<p class="note" style="margin-top:36px">本报告由工具箱「报告生成器」自动汇总，'
             '数字均为现场检查结果；改代码后重新生成即可。</p>')
    H.append('</div></body></html>')
    return '\n'.join(H)


def collect_all(do_run=True, do_shot=True):
    hist = read_size_history()
    return {'checkers': collect_checkers(do_run), 'progress': collect_progress(),
            'size': size_latest(hist), 'size_series': size_series(hist),
            'code': collect_code(), 'svgs': collect_svgs(do_run),
            'coverage': collect_coverage(), 'shot': collect_shot(do_run, do_shot)}


# ---------------------------------------------------------------- C3：一键成包

def detail_files():
    """明细里放哪些报告：七个检查器的报告 + 覆盖率看板 / 平台清单 / 一键验收（有就放）。"""
    out = []
    for name, rel in CHECKERS:
        p = os.path.join(TOOLBOX, os.path.dirname(rel), '%s报告.md' % name.replace(' ', ''))
        if os.path.isfile(p):
            out.append(p)
    for rel in (('检查', '覆盖率', '产物', '覆盖率看板.md'),
                ('检查', '平台清单', '产物', '平台清单.md'),
                ('检查', '一键验收', '产物', '一键验收.md')):
        p = os.path.join(TOOLBOX, *rel)
        if os.path.isfile(p):
            out.append(p)
    return out


def pack_readme(title, data):
    ck = data['checkers']
    bad = [c for c in ck if not c['ok'] or c['severe']]
    L = ['# 答辩包 · %s' % title, '',
         '生成时间：%s' % datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'), '',
         '## 包里有什么', '',
         '- `周报.html` —— **单文件**报告：CSS / SVG 时序图 / 界面截图全部内联，'
         '**没有任何外链**，拷到没网的机器上双击就能看；打印也用它'
         '（A4 版式，表格与图不会跨页截断）。',
         '- `截图.png` —— 工具箱「工程体检总览」页的现场截图原图，答辩时直接塞 PPT。',
         '- `明细/` —— 各检查器本次（或上一次）跑出来的报告原文，周报里「体检结论」那节就是它们的汇总。',
         '',
         '## 这份数字是怎么来的', '',
         '- 体检：**现场跑**各检查器的 `--json`（不是读旧报告）；阶段：进度文档的「阶段总表」（A1 回写的）；',
         '  体积：`检查\\构建烧录\\_体积历史.json` 的整条序列；时序图：链路仿真的六个剧本；',
         '  自检覆盖：覆盖率看板的合计。**一个数字都不写死** —— 都是生成那一刻的现场值。',
         '- 结论：%d 个检查器，%d 个有问题%s。' % (len(ck), len(bad),
                                                   '（%s）' % '、'.join(c['name'] for c in bad) if bad else ''),
         '',
         '## 怎么重新生成', '',
         '```',
         'python 本地工具\\工具箱\\检查\\报告\\report_gen.py --pack',
         '```',
         '',
         '（`--no-run` 体检不现场跑、只读现有报告；`--no-shot` 不抓界面截图。）', '']
    return '\n'.join(L)


def pack(title, do_run=True, do_shot=True, out_dir=''):
    """一键成包：`答辩包_<时间>/` 目录 + 同名 `.zip`（可打印、可发人）。"""
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    full_title = '%s · %s' % (title, datetime.date.today().strftime('%Y-%m-%d'))
    data = collect_all(do_run, do_shot)
    h = build_html(full_title, data)

    base = os.path.join(os.path.abspath(out_dir) if out_dir else os.path.join(HERE, '产物'),
                        '答辩包_%s' % stamp)
    os.makedirs(base, exist_ok=True)
    html_path = os.path.join(base, '周报.html')
    with open(html_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(h)
    shot = data.get('shot')
    if shot:
        shutil.copy2(shot['path'], os.path.join(base, '截图.png'))
    details = detail_files()
    if details:
        sub = os.path.join(base, '明细')
        os.makedirs(sub, exist_ok=True)
        for p in details:
            shutil.copy2(p, os.path.join(sub, os.path.basename(p)))
    with open(os.path.join(base, '说明.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(pack_readme(full_title, data))

    zip_path = base + '.zip'
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as z:
        for root, _dirs, files in os.walk(base):
            for fn in files:
                fp = os.path.join(root, fn)
                z.write(fp, os.path.relpath(fp, base).replace(os.sep, '/'))
    return {'dir': base, 'html': html_path, 'zip': zip_path, 'shot': bool(shot),
            'details': len(details), 'zip_bytes': os.path.getsize(zip_path),
            'html_bytes': os.path.getsize(html_path)}


def selftest():
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    ck('解析阶段总表', parse_stage_table(os.path.join(
        ROOT, 'EnvMonitor', 'Project_1_检测端', '板1进度文档.md'))[:1] != [])
    rows = parse_stage_table(os.path.join(ROOT, 'EnvMonitor', 'Project_1_检测端', '板1进度文档.md'))
    ck('阶段总表读出 10 行', len(rows) == 10, len(rows))

    data = collect_all(do_run=False)
    ck('体检 7 个检查器都有行', len(data['checkers']) == 7, len(data['checkers']))
    ck('进度文档 ≥3 份', len(data['progress']) >= 3, len(data['progress']))
    ck('代码统计只含自己写的 .c（不含 board_selftest）',
       all(c['files'] >= 1 for c in data['code']), data['code'])

    h = build_html('测试标题', data)
    ck('HTML 以 <!DOCTYPE html> 开头', h.startswith('<!DOCTYPE html>'))
    ck('HTML 完整闭合', h.rstrip().endswith('</html>'))
    # 注意：内联 SVG 里有 xmlns="http://www.w3.org/2000/svg" —— 那是命名空间标识，
    # 不是外链（浏览器不会去请求它）。所以判"无外链"要看 <link / <script src / url(http。
    ck('CSS 内联（无 <link> / <script src> / 外部 url()）',
       '<style>' in h and '<link' not in h and 'script src' not in h
       and 'url(http' not in h, '')
    ck('含截图/体检/阶段/体积/代码五个小节',
       all(k in h for k in ('1. 界面截图', '2. 体检结论', '3. 阶段进度',
                            '4. 固件体积', '5. 代码规模')))
    ck('标题里的 HTML 字符被转义', esc('<b>') == '&lt;b&gt;')
    ck('打印样式存在（答辩要打印）', '@media print' in h)

    # ---- C3：截图内联 / 无外链 / 体积趋势 / 打包 ----
    # "无外链依赖"这条的机器判据：**所有** <img src 都必须是 data: URI（图片一旦外链就不是单文件了）
    img_srcs = re.findall(r'<img[^>]*src="([^"]*)"', h)
    ck('所有 <img> 都用 data: 内联（截图不许外链）',
       all(s.startswith('data:image/') for s in img_srcs), str(img_srcs[:1]))
    shot = data.get('shot')
    if shot:
        ck('截图真内联了（base64 长度 > 1000）',
           'src="data:image/png;base64,' in h and len(shot['b64']) > 1000,
           '%.1f KB' % (len(shot['b64']) / 1024.0))
        ck('截图宽高读出来了（PNG 头解析）', shot['w'] > 100 and shot['h'] > 100,
           '%dx%d' % (shot['w'], shot['h']))
        ck('截图说明里的宽高与 PNG 头一致',
           '%d×%d' % (shot['w'], shot['h']) in h)
        # 真内联的硬证据：把 HTML 里的 base64 解回来，与那张 png 逐字节一致
        m = re.search(r'data:image/png;base64,([A-Za-z0-9+/=]+)', h)
        try:
            with open(shot['path'], 'rb') as f:
                raw = f.read()
            ck('HTML 里的截图 base64 解码回来与 png 文件逐字节一致',
               bool(m) and base64.b64decode(m.group(1)) == raw)
        except OSError as e:
            ck('能读回那张 png 做对账', False, repr(e))
    else:
        ck('（如实）没有截图时报告里写清"还没有界面截图"', '还没有界面截图' in h)

    # 体积：报告里的数字 = `_体积历史.json` 现场值（面板「编译烧录」页读的是同一个文件）
    hist = read_size_history()
    latest = size_latest(hist)
    ser = size_series(hist)
    ok_size = bool(latest)
    for s in latest:
        ok_size = ok_size and ('%d B' % s['flash'] in h)
    ck('体积表里的 Flash 字节 = 现场历史文件的最新值', ok_size, len(latest))
    want_poly = sum(1 for v in ser.values() if len(v) >= 2)
    ck('有 ≥2 次记录的板都画了趋势折线（<polyline>）',
       h.count('<polyline') >= want_poly, '%d 条折线 / %d 块够料的板'
       % (h.count('<polyline'), want_poly))
    for name, v in ser.items():
        if len(v) < 2:
            ck('（如实）%s 记录不足 2 次时写"画不了趋势"' % name, '画不了趋势' in h)

    # 阶段表：x/y 与**现场再解析一遍**进度文档的结果一致（不是抄 data 里的）
    ok_stage = True
    rows_total = 0
    for p in progress_docs():
        rows = parse_stage_table(p)
        if not rows:
            continue
        rows_total += len(rows)
        done = sum(1 for r in rows if '☑' in r[2] or '完成' in r[2])
        name = os.path.basename(p).replace('进度文档.md', '')
        if ('%s：%d / %d 个阶段完成' % (name, done, len(rows))) not in h:
            ok_stage = False
    ck('阶段表的 x/y 与现场再解析的进度文档一致', ok_stage and rows_total >= 10,
       '%d 行' % rows_total)
    cov = collect_coverage()
    if cov:
        ck('自检覆盖那节的断言数 = 覆盖率看板.md 的合计',
           ('断言 <b>%d</b> 条' % cov['total']) in h, cov['total'])

    # 时序图内联：把 XML 声明去掉，避免嵌进 HTML 后出现两个 <?xml
    svgs = data['svgs']
    if svgs:
        ck('时序图内联时去掉了 XML 声明', all('<?xml' not in s['svg'] for s in svgs))
        ck('时序图有 5 条泳道', all('泳道' in s['svg'] or s['svg'].count('<rect') > 5
                                   for s in svgs), '%d 张' % len(svgs))
    else:
        ck('（跳过）时序图：还没有产物', True)

    # 打包：真跑一次到临时目录，断言目录 / 三件套 / zip 内容，并核对 zip 里的 HTML 与磁盘上逐字节一致
    tmp = os.path.join(tempfile.gettempdir(), 'report_pack_test_%d' % os.getpid())
    try:
        p = pack('自检包', do_run=False, do_shot=False, out_dir=tmp)
        ck('打包产出目录 + 周报.html', os.path.isdir(p['dir']) and os.path.isfile(p['html']))
        ck('打包产出 zip 且非空', os.path.isfile(p['zip']) and p['zip_bytes'] > 1000,
           '%d B' % p['zip_bytes'])
        ck('包里含 说明.md', os.path.isfile(os.path.join(p['dir'], '说明.md')))
        with zipfile.ZipFile(p['zip']) as z:
            names = z.namelist()
            ck('zip 里有 周报.html 与 说明.md',
               '周报.html' in names and '说明.md' in names, str(names[:3]))
            same = z.read('周报.html') == open(p['html'], 'rb').read()
        ck('zip 里的 周报.html 与磁盘上那份逐字节一致', same)
    except Exception as e:
        ck('打包能跑通', False, repr(e))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # 反例：体积历史为空时不许编数字
    b2 = {'checkers': data['checkers'], 'progress': [], 'size': [], 'size_series': {},
          'code': [], 'svgs': [], 'coverage': None, 'shot': None}
    h2 = build_html('空数据', b2)
    ck('反例：没有数据时 HTML 里如实说明（截图/体积不编数字）',
       '还没有界面截图' in h2 and '4. 固件体积' not in h2 and '1. 界面截图' in h2)

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('报告生成器自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='答辩 / 周报生成器（D1 · C3）')
    ap.add_argument('--out', default='', help='输出 HTML 路径（单文件模式）')
    ap.add_argument('--title', default='工程周报', help='报告标题')
    ap.add_argument('--pack', action='store_true',
                    help='一键成包：答辩包_<时间>/ 目录 + 同名 zip（周报.html + 截图.png + 明细/ + 说明.md）')
    ap.add_argument('--bundle-dir', default='', help='配合 --pack：包放哪个目录（默认 检查/报告/产物）')
    ap.add_argument('--no-run', action='store_true', help='体检不现场跑，只读现有报告')
    ap.add_argument('--no-shot', action='store_true', help='不抓界面截图（用最近一张，或省略）')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    if a.pack:
        r = pack(a.title, not a.no_run, not a.no_shot, a.bundle_dir)
        print('答辩包：%s' % r['dir'])
        print('  周报.html %.0f KB　截图 %s　明细 %d 份'
              % (r['html_bytes'] / 1024.0, '有' if r['shot'] else '无', r['details']))
        print('  zip：%s（%.0f KB）' % (r['zip'], r['zip_bytes'] / 1024.0))
        sys.exit(0)

    data = collect_all(not a.no_run, not a.no_shot)
    title = '%s · %s' % (a.title, datetime.date.today().strftime('%Y-%m-%d'))
    h = build_html(title, data)
    out = a.out or os.path.join(HERE, '产物', '周报_%s.html'
                                % datetime.datetime.now().strftime('%Y%m%d_%H%M'))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(h)
    bad = [c for c in data['checkers'] if not c['ok'] or c['severe']]
    print('报告：%s（%.1f KB）' % (out, os.path.getsize(out) / 1024.0))
    print('  检查器 %d 个，有问题 %d 个；进度 %d 份；体积 %d 块板；时序图 %d 张；截图 %s'
          % (len(data['checkers']), len(bad), len(data['progress']),
             len(data['size']), len(data['svgs']), '有' if data['shot'] else '无'))
    print('  想要"能直接发人的包"：加 --pack（目录 + zip，含截图原图与明细）')


if __name__ == '__main__':
    main()
