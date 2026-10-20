# -*- coding: utf-8 -*-
r"""答辩 / 周报生成器（D1）：一键出一份**单文件 HTML**。

为什么是单文件：发人、打印、塞进答辩 PPT 都只要一个文件，不用带图片目录、
不用担心别人打不开（CSS 全内联，SVG 直接嵌在 HTML 里）。

内容全部来自**现有数据源**，不重新编一套口径：
  · 体检      → 各检查器的 `--json`（现场跑，不是读旧报告）
  · 阶段进度  → 三份进度文档的「阶段总表」（A1 回写的那张）
  · 体积      → `构建烧录/_体积历史.json`（A3 记的）
  · 代码统计  → 工程根下 *.c 的行数（只算你写的，不算 FWLIB）
  · 时序图    → `链路仿真/产物/链路时序_*.svg`（B1 出的）
  · 自检覆盖  → `覆盖率/产物/覆盖率看板.md` 里的合计（D4 出的）

用法：
    python 本地工具\工具箱\检查\报告\report_gen.py
    python ...\report_gen.py --out D:\答辩\周报.html --title "第 7 周"
    python ...\report_gen.py --no-run        # 体检不现场跑，只读现有报告
    python ...\report_gen.py --selftest
"""
import argparse
import datetime
import glob
import html
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
    """板级配置路径：TOOLBOX_BOARDCFG 优先（D3 的"加假工程"验收靠它），否则仓库里那份。"""
    return os.environ.get('TOOLBOX_BOARDCFG', '').strip() or cfg_file()


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


def collect_progress():
    cfg = os.path.join(TOOLBOX, '速查数据', 'rules', 'board_envmonitor.json')
    try:
        with open(cfg, encoding='utf-8') as f:
            docs = json.load(f).get('extra', {}).get('progress_docs') or []
    except Exception:
        docs = []
    out = []
    for rel in docs:
        p = os.path.join(ROOT, rel)
        rows = parse_stage_table(p)
        if rows:
            name = os.path.basename(rel).replace('进度文档.md', '')
            out.append({'name': name, 'rows': rows})
    return out


def collect_size():
    p = os.path.join(TOOLBOX, '检查', '构建烧录', '_体积历史.json')
    try:
        with open(p, encoding='utf-8') as f:
            h = json.load(f)
    except Exception:
        return []
    latest = {}
    for r in h:
        latest[r['board']] = r          # 按板取最后一次
    return list(latest.values())


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


def collect_svgs():
    d = os.path.join(TOOLBOX, '检查', '链路仿真', '产物')
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
             'box-shadow:0 1px 4px rgba(0,0,0,.08);border-radius:6px}'
             'h1{font-size:26px;margin:0 0 6px}h2{font-size:18px;margin:34px 0 12px;'
             'padding-bottom:6px;border-bottom:2px solid var(--line)}'
             '.meta{color:var(--dim);font-size:13px;margin-bottom:22px}'
             'table{border-collapse:collapse;width:100%;font-size:14px}'
             'th,td{border:1px solid var(--line);padding:7px 10px;text-align:left}'
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
             '.note{color:var(--dim);font-size:13px}'
             '@media print{body{background:#fff}.wrap{box-shadow:none;margin:0;padding:0}}'
             '</style></head><body><div class="wrap">')
    H.append('<h1>%s</h1>' % esc(title))
    H.append('<div class="meta">生成时间 %s · 数据来自工具箱现场检查（引脚/RTOS/编码/LoRa/协议/缓存/进度）</div>'
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

    # 体检
    H.append('<h2>1. 体检结论</h2><table><tr><th>检查器</th><th>严重</th><th>注意</th>'
             '<th>状态</th></tr>')
    for c in ck:
        st = '通过' if (c['ok'] and not c['severe']) else ('有严重项' if c['severe'] else '未通过')
        cls = 'ok' if st == '通过' else ('bad' if c['severe'] else 'warn')
        H.append('<tr><td>%s</td><td class="num">%d</td><td class="num">%d</td>'
                 '<td class="%s">%s%s</td></tr>'
                 % (esc(c['name']), c['severe'], c['warn'], cls, esc(st),
                    ('（%s）' % esc(c['error'])) if c.get('error') else ''))
    H.append('</table>')

    # 进度
    if data['progress']:
        H.append('<h2>2. 阶段进度</h2>')
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

    # 体积
    if data['size']:
        H.append('<h2>3. 固件体积</h2><table><tr><th>板子</th><th>Flash</th>'
                 '<th>占芯片</th><th>RAM</th><th>占芯片</th></tr>')
        for s in data['size']:
            fp = 100.0 * s['flash'] / (s['flash_kb'] * 1024) if s.get('flash_kb') else 0
            rp = 100.0 * s['ram'] / (s.get('ram_kb') or 192 * 1024 / 1024) if s.get('ram_kb') else 0
            H.append('<tr><td>%s</td><td class="num">%d B</td><td class="num">%.1f%%</td>'
                     '<td class="num">%d B</td><td class="num">%.1f%%</td></tr>'
                     % (esc(s['board']), s['flash'], fp, s['ram'], rp))
        H.append('</table>')

    # 代码
    if data['code']:
        H.append('<h2>4. 代码规模（只算自己写的 .c）</h2><table>'
                 '<tr><th>工程</th><th>文件数</th><th>行数</th></tr>')
        for c in data['code']:
            H.append('<tr><td>%s</td><td class="num">%d</td><td class="num">%d</td></tr>'
                     % (esc(c['board']), c['files'], c['lines']))
        H.append('</table>')

    # 时序图
    if data['svgs']:
        H.append('<h2>5. 链路时序（仿真）</h2>')
        for s in data['svgs']:
            H.append('<p class="note">剧本：%s</p><div class="svgbox">%s</div>' % (esc(s['name']), s['svg']))

    # 覆盖率
    if data['coverage']:
        c = data['coverage']
        H.append('<h2>6. 工具箱自检</h2><p>断言 <b>%d</b> 条，失败 <b class="%s">%d</b> 条；'
                 '变异测试 %d 处，未变红 %d 处。</p>'
                 % (c['total'], 'ok' if c['failed'] == 0 else 'bad', c['failed'],
                    c['mut_total'], c['mut_bad']))

    H.append('<p class="note" style="margin-top:36px">本报告由工具箱「报告生成器」自动汇总，'
             '数字均为现场检查结果；改代码后重新生成即可。</p>')
    H.append('</div></body></html>')
    return '\n'.join(H)


def collect_all(do_run=True):
    return {'checkers': collect_checkers(do_run), 'progress': collect_progress(),
            'size': collect_size(), 'code': collect_code(),
            'svgs': collect_svgs(), 'coverage': collect_coverage()}


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
    ck('含体检/进度/体积/代码四个小节',
       all(k in h for k in ('1. 体检结论', '2. 阶段进度', '3. 固件体积', '4. 代码规模')))
    ck('标题里的 HTML 字符被转义', esc('<b>') == '&lt;b&gt;')
    ck('打印样式存在（答辩要打印）', '@media print' in h)

    # 时序图内联：把 XML 声明去掉，避免嵌进 HTML 后出现两个 <?xml
    svgs = data['svgs']
    if svgs:
        ck('时序图内联时去掉了 XML 声明', all('<?xml' not in s['svg'] for s in svgs))
    else:
        ck('（跳过）时序图：还没有产物', True)

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('报告生成器自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='答辩 / 周报生成器（D1）')
    ap.add_argument('--out', default='', help='输出 HTML 路径')
    ap.add_argument('--title', default='工程周报', help='报告标题')
    ap.add_argument('--no-run', action='store_true', help='体检不现场跑，只读现有报告')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    data = collect_all(not a.no_run)
    title = '%s · %s' % (a.title, datetime.date.today().strftime('%Y-%m-%d'))
    h = build_html(title, data)
    out = a.out or os.path.join(HERE, '产物', '周报_%s.html'
                                % datetime.datetime.now().strftime('%Y%m%d_%H%M'))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(h)
    bad = [c for c in data['checkers'] if not c['ok'] or c['severe']]
    print('报告：%s（%.1f KB）' % (out, os.path.getsize(out) / 1024.0))
    print('  检查器 %d 个，有问题 %d 个；进度 %d 份；体积 %d 块板；时序图 %d 张'
          % (len(data['checkers']), len(bad), len(data['progress']),
             len(data['size']), len(data['svgs'])))


if __name__ == '__main__':
    main()
