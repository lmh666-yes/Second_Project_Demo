# -*- coding: utf-8 -*-
r"""驾驶舱取数（C2）—— 把"项目脉搏"要的几路现场数凑成一份 JSON，给工具箱的「项目驾驶舱」页画卡片。

为什么单独一个脚本：C2 一屏要的数散在四个工具里（阶段判定 / 进度扫描 / 构建烧录的体历史 / 平台清单）。
Qt 页自己去起四次进程、各自解析一遍 JSON，等于把这些口径抄第二遍。收在这里：**一屏要的数 = 一次取数**；
而且它自带 `--selftest`，专门钉住"卡上的数字就是那四个工具现场说的数字"（写死的卡片只会跟着代码一起烂）。

四张卡（**体检结论**那张不在这里实现：它由 Qt 页接「工程体检总览」的数据，一处口径一处实现）：
    1. 阶段进度 —— `阶段判定/stage_status.py --json`
    2. 体积趋势 —— `构建烧录/build_flash.py --history --json`
    3. 自检覆盖 —— `平台清单/platform_manifest.py --no-run --json`
    4. 待办与桩 —— `进度跟踪/progress_scan.py --json`
另给**结构化**的 stage（哪块板、下一个阶段号、能不能一键跑）+ 一句「下一步点哪」，
Qt 页据此拼「跑阶段验收」按钮的命令（阶段号与板名都来自这里，不写死）。

用法：
    python 本地工具\工具箱\检查\驾驶舱\cockpit_data.py
    python ...\cockpit_data.py --json
    python ...\cockpit_data.py --selftest
"""
import argparse
import json
import os
import subprocess
import sys

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))            # 工具箱/检查/驾驶舱
CHECKS = os.path.dirname(HERE)                               # 工具箱/检查
TOOLBOX = os.path.dirname(CHECKS)                            # 工具箱
PY = sys.executable or 'python'

STAGE = os.path.join(CHECKS, '阶段判定', 'stage_status.py')
HIST = os.path.join(CHECKS, '构建烧录', 'build_flash.py')
COVER = os.path.join(CHECKS, '平台清单', 'platform_manifest.py')
SCAN = os.path.join(CHECKS, '进度跟踪', 'progress_scan.py')

STATE_WORD = {'done': '已过', 'todo': '待做', 'manual': '需人工确认'}


def _last_json(text):
    for line in reversed((text or '').strip().splitlines()):
        try:
            return json.loads(line)
        except ValueError:
            continue
    return None


def run_sibling(path, args, timeout=240):
    """跑一个兄弟脚本取它的 JSON（最后一行）。返回 (json 或 None, 说明)。"""
    if not os.path.isfile(path):
        return None, '脚本不存在：%s' % path
    try:
        p = subprocess.run([PY, path] + list(args), capture_output=True, text=True,
                           encoding='utf-8', errors='replace', timeout=timeout,
                           cwd=os.path.dirname(path))
    except subprocess.TimeoutExpired:
        return None, '超时（>%ds）' % timeout
    except OSError as e:
        return None, '跑不起来：%s' % e
    obj = _last_json(p.stdout)
    if obj is None:
        return None, '最后一行不是 JSON（rc=%s）' % p.returncode
    return obj, ''


# ---------------------------------------------------------------- 四张卡（纯函数：喂 JSON，出卡）

def stage_progress(stage_json):
    """阶段进度卡 + 结构化 stage（下一步是哪块板的哪个阶段）。

    这里要分清**两件事**，混在一起会给出一个点不动的按钮：
      · 「下一个要做的」= 第一个不是 done 的阶段 —— 它可能是 `manual`（机器判不了，
        比如"分工与阶段地图：你自己能说清就算过"）；
      · 「按钮能跑的」  = 第一个 `todo` 的阶段 —— 只有这种 `build_flash.py --stage-run N` 真能验。
    所以：卡片上两件都说清，结构化数据里的 next_board/next_stage 给的是**按钮目标**。
    """
    boards = (stage_json or {}).get('boards') or []
    parts, rows = [], []
    next_any = None      # (板, 阶段号, 状态) —— 第一个非 done
    next_todo = None     # (板, 阶段号)     —— 第一个 todo（机器判得了）
    for b in boards:
        name = b.get('board') or '?'
        stages = b.get('stages') or []
        done = sum(1 for s in stages if s.get('status') == 'done')
        parts.append('%s %d/%d' % (name, done, len(stages)))
        rows.append({'board': name, 'done': done, 'total': len(stages)})
        for s in stages:
            st = s.get('status')
            if next_any is None and st != 'done':
                next_any = (name, s.get('no'), st)
            if next_todo is None and st == 'todo':
                next_todo = (name, s.get('no'))
    tname = ' · '.join(parts) if parts else '没读到阶段判据'
    todo_txt = ('机器能判的下一个：%s 阶段 %s' % next_todo) if next_todo \
        else '机器能判的阶段都过了'
    if next_any is None:
        sub = '机器能判的阶段都过了（判不了的阶段已在表里标「需人工确认」）'
    elif next_any[2] == 'manual':
        sub = ('下一个：%s 阶段 %s（需人工确认，你自己拍）｜%s'
               % (next_any[0], next_any[1], todo_txt))
    else:
        sub = ('下一步：%s 阶段 %s（%s）'
               % (next_any[0], next_any[1], STATE_WORD.get(next_any[2], next_any[2])))
    return {'key': 'stage', 'title': '阶段进度', 'value': tname, 'sub': sub}, \
        {'boards': rows,
         'next_board': next_todo[0] if next_todo else '',
         'next_stage': next_todo[1] if next_todo else None,
         'next_state': 'todo' if next_todo else '',
         'next_any_board': next_any[0] if next_any else '',
         'next_any_stage': next_any[1] if next_any else None,
         'next_any_state': next_any[2] if next_any else '',
         'runnable': next_todo is not None}


def size_trend(hist_json):
    """体积趋势卡：每块板最近一次的 Flash/RAM + 记录轮数。"""
    boards = (hist_json or {}).get('boards') or []
    if not boards:
        return {'key': 'size', 'title': '体积趋势', 'value': '还没有编译记录',
                'sub': '去「编译烧录」页点一次编译，这里就有 Flash/RAM 与趋势了'}
    first = boards[0]
    last = first.get('last') or {}
    value = '%s Flash %dB（%.1f%%）· RAM %dB（%.1f%%）' % (
        first.get('board') or '?', last.get('flash', 0), first.get('flash_pct') or 0,
        last.get('ram', 0), first.get('ram_pct') or 0)
    tail = []
    for b in boards[1:]:
        l = b.get('last') or {}
        tail.append('%s Flash %dB · RAM %dB' % (b.get('board') or '?',
                                                l.get('flash', 0), l.get('ram', 0)))
    d_f, d_r = first.get('d_flash'), first.get('d_ram')
    delta = '较上次 ' + ('持平' if (d_f == 0 and d_r == 0)
                        else 'Flash %+d / RAM %+d' % (d_f or 0, d_r or 0))
    sub = '｜'.join(tail + ['记录 %d 轮（共 %d 点）· %s'
                            % (first.get('points', 0), (hist_json or {}).get('points', 0), delta)])
    return {'key': 'size', 'title': '体积趋势', 'value': value, 'sub': sub}


def cover_line(cover_json):
    """自检覆盖卡：闸门多少项、插件壳多少、变异覆盖多少（数字来自现场解析 main.cpp）。"""
    c = cover_json or {}
    if not c.get('gated'):
        return {'key': 'cover', 'title': '自检覆盖', 'value': '没读平台清单',
                'sub': '跑一下「平台清单」页（或 检查/平台清单/platform_manifest.py）'}
    return {'key': 'cover', 'title': '自检覆盖',
            'value': '闸门 %d 项 · 插件壳 %d 个 · 变异覆盖 %d 项'
                     % (c.get('gated', 0), c.get('plugin_pages', 0),
                        c.get('mutation_covered', 0)),
            'sub': '逐项明细（入口 / 断言数 / 进闸门 / 变异）看「平台清单」页'}


def todo_line(scan_json):
    """待办与桩卡：标记（TODO 等）/ 桩函数 / 空壳，外加"桩最多的那个文件"。"""
    s = scan_json or {}
    if not s:
        return {'key': 'todo', 'title': '待办与桩', 'value': '没读到进度扫描',
                'sub': '跑一下「进度扫描」页（或 检查/进度跟踪/progress_scan.py）'}
    stubs = s.get('stub_files') or {}
    top = ''
    if stubs:
        k = max(stubs, key=lambda x: stubs[x])
        top = '｜桩最多：%s（%d 处）' % (os.path.basename(k.replace('\\', '/')), stubs[k])
    return {'key': 'todo', 'title': '待办与桩',
            'value': '标记（TODO 等）%d 处 · 桩函数 %d 处 · 空壳 %d 处'
                     % (s.get('markers', 0), s.get('stubs', 0), s.get('noop', 0)),
            'sub': '扫 %d 个文件%s' % (s.get('files', 0), top)}


# ---------------------------------------------------------------- 取数 + 拼盘

def collect():
    """真跑四路取数、拼成给 Qt 页用的 JSON。任一路取不到就如实写"没读到"，不编数字。"""
    stage_json, e1 = run_sibling(STAGE, ['--json'])
    hist_json, e2 = run_sibling(HIST, ['--history', '--json'])
    cover_json, e3 = run_sibling(COVER, ['--no-run', '--json'])
    scan_json, e4 = run_sibling(SCAN, ['--json'])

    stage_card, stage_meta = stage_progress(stage_json)
    cards = [stage_card,
             None,                       # 体检结论：由 Qt 页插（接总览页数据）
             size_trend(hist_json),
             cover_line(cover_json),
             todo_line(scan_json)]

    notes = [x for x in (e1, e2, e3, e4) if x]
    if stage_meta.get('runnable'):
        nxt = '跑 %s 阶段 %s 的验收（点「跑阶段验收」）' % (
            stage_meta['next_board'], stage_meta['next_stage'])
    else:
        nxt = '机器能判的阶段都过了 —— 往上堆阶段 4/5/9，或点「跑一键验收」收个尾'
    return {'tool': '驾驶舱', 'schema': 1, 'ok': not notes, 'cards': cards,
            'stage': stage_meta, 'next_action': nxt, 'notes': notes}


def render_text(b):
    L = ['项目脉搏（现场取数，一个数字都不写死）', '']
    for c in b['cards']:
        if not c:
            continue
        L.append('  %-10s %s' % (c['title'], c['value']))
        if c.get('sub'):
            L.append('  %-10s %s' % ('', c['sub']))
    L.append('')
    L.append('  下一步点哪：%s' % b['next_action'])
    return '\n'.join(L)


# ---------------------------------------------------------------- 自检

def selftest():
    """钉住"卡上的数字 = 那四个工具现场说的数字"，外加错例/反例：
       · 正例：真跑四路，卡上的数字与源 JSON 对得上
       · 错例：源 JSON 里板1 全 done → 下一阶段必须变"都过了"，不能瞎指一个阶段
       · 反例：空 JSON 进各卡构造 → 如实写"没读到"，不能编 0 或抛异常
    """
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    # ---- 正例：真跑四路，逐一对账 ----
    stage_json, e1 = run_sibling(STAGE, ['--json'])
    hist_json, e2 = run_sibling(HIST, ['--history', '--json'])
    cover_json, e3 = run_sibling(COVER, ['--no-run', '--json'])
    scan_json, e4 = run_sibling(SCAN, ['--json'])
    ck('阶段判定取到 JSON（tool 名对）',
       isinstance(stage_json, dict) and stage_json.get('tool') == '阶段判定', e1)
    ck('体积历史取到 JSON（有 boards）',
       isinstance(hist_json, dict) and isinstance(hist_json.get('boards'), list), e2)
    ck('平台清单取到 JSON（tool 名对）',
       isinstance(cover_json, dict) and cover_json.get('tool') == '平台清单', e3)
    ck('进度扫描取到 JSON（tool 名对）',
       isinstance(scan_json, dict) and scan_json.get('tool') == 'progress_scan', e4)

    card, meta = stage_progress(stage_json)
    if isinstance(stage_json, dict) and stage_json.get('boards'):
        b0 = stage_json['boards'][0]
        done = sum(1 for s in (b0.get('stages') or []) if s.get('status') == 'done')
        ck('阶段卡的 x/y 与源 JSON 的 done/total 一致',
           ('%s %d/%d' % (b0.get('board'), done, len(b0.get('stages') or []))) in card['value'],
           card['value'])
        # 按钮目标 = 现场第一个 `todo` 的阶段（只有 todo 才机器判得了、才跑得动 --stage-run）
        want_todo = None
        for b in stage_json['boards']:
            for s in (b.get('stages') or []):
                if s.get('status') == 'todo':
                    want_todo = (b.get('board'), s.get('no'))
                    break
            if want_todo:
                break
        ck('「跑阶段验收」的目标 = 现场第一个 todo 的阶段',
           (want_todo is None and meta['next_stage'] is None and not meta['runnable'])
           or (want_todo and meta['next_board'] == want_todo[0]
               and meta['next_stage'] == want_todo[1] and meta['runnable']),
           str((meta['next_board'], meta['next_stage'], meta['runnable'], 'want', want_todo)))
        # 人看的"下一个" = 第一个非 done（可能是 manual，那就得说清它判不了）
        want_any = None
        for b in stage_json['boards']:
            for s in (b.get('stages') or []):
                if s.get('status') != 'done':
                    want_any = (b.get('board'), s.get('no'), s.get('status'))
                    break
            if want_any:
                break
        ck('卡上"下一个要做的"= 现场第一个非 done 的阶段',
           (want_any is None and not meta['next_any_board'])
           or (want_any and meta['next_any_board'] == want_any[0]
               and meta['next_any_stage'] == want_any[1] and meta['next_any_state'] == want_any[2]),
           str((meta['next_any_board'], meta['next_any_stage'], meta['next_any_state'],
                'want', want_any)))
    else:
        ck('阶段判定里至少有一块板', False, e1)

    if isinstance(hist_json, dict) and hist_json.get('boards'):
        b0 = hist_json['boards'][0]
        ck('体积卡里的 Flash 字节 = 源 JSON 的 last.flash',
           str((b0.get('last') or {}).get('flash')) in size_trend(hist_json)['value'],
           size_trend(hist_json)['value'])
    if isinstance(cover_json, dict) and cover_json.get('gated'):
        ck('自检覆盖卡的闸门项数 = 源 JSON 的 gated',
           str(cover_json.get('gated')) in cover_line(cover_json)['value'])
    if isinstance(scan_json, dict) and scan_json.get('markers') is not None:
        ck('待办卡里的标记数 = 源 JSON 的 markers',
           str(scan_json.get('markers')) in todo_line(scan_json)['value'])

    # ---- 错例：改坏源数据必须被反映出来（不能瞎指一个阶段 / 不能给出点不动的按钮） ----
    fake_all_done = {'boards': [{'board': '板1', 'stages': [
        {'no': 0, 'status': 'done'}, {'no': 1, 'status': 'done'}]}]}
    c2, m2 = stage_progress(fake_all_done)
    ck('错例：板1 全 done → 必须说"都过了"而不是瞎指一个阶段',
       m2['next_stage'] is None and m2['runnable'] is False and '都过了' in c2['sub'],
       str((m2['next_stage'], m2['runnable'], c2['sub'])))
    ck('错例：全 done 时不给"跑阶段验收"（runnable=False）', m2['runnable'] is False)

    fake_manual_only = {'boards': [{'board': '板1', 'stages': [
        {'no': 0, 'status': 'manual'}, {'no': 1, 'status': 'done'}]}]}
    c3, m3 = stage_progress(fake_manual_only)
    ck('错例：只剩 manual 时段 → 按钮不给（manual 机器判不了）',
       m3['runnable'] is False and m3['next_stage'] is None, str(m3))
    ck('错例：只剩 manual 时段 → 卡上仍要说清"下一个要做的"是它、且标"需人工确认"',
       m3['next_any_stage'] == 0 and '需人工确认' in c3['sub'], c3['sub'])

    fake_todo = {'boards': [{'board': '板2', 'stages': [
        {'no': 0, 'status': 'done'}, {'no': 3, 'status': 'todo'}]}]}
    c4, m4 = stage_progress(fake_todo)
    ck('正例（假数据）：有 todo → 按钮目标就是它（板2 阶段 3）',
       m4['runnable'] and m4['next_board'] == '板2' and m4['next_stage'] == 3,
       str((m4['next_board'], m4['next_stage'])))

    # ---- 反例：空数据不许编数字 ----
    ck('反例：空阶段 JSON → 如实写"没读到"，不抛异常',
       '没读到' in stage_progress(None)[0]['value'])
    ck('反例：空体积 JSON → 如实写"还没有编译记录"',
       '还没有编译记录' in size_trend(None)['value'])
    ck('反例：空覆盖 JSON → 如实写"没读平台清单"',
       '没读平台清单' in cover_line(None)['value'])
    ck('反例：空扫描 JSON → 如实写"没读到进度扫描"',
       '没读到进度扫描' in todo_line(None)['value'])

    # ---- 拼盘：四张卡都在、每张有值，且留出体检结论那一位 ----
    b = collect()
    ck('拼盘有四张现场卡 + 为体检结论留的一位',
       len(b['cards']) == 5 and b['cards'][1] is None, str(len(b['cards'])))
    ck('每张现场卡 value / sub 都非空',
       all(c.get('value') and c.get('sub') for c in b['cards'] if c), str(b['cards']))
    ck('「下一步点哪」非空', bool(b['next_action']), b['next_action'])
    ck('stage 结构化数据齐（板/阶段号/runnable）',
       isinstance(b['stage'], dict) and 'runnable' in b['stage'])
    ck('渲染文本含每张卡的标题与结论', all(
        c['title'] in render_text(b) for c in b['cards'] if c)
       and '下一步点哪' in render_text(b))

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('驾驶舱取数自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='驾驶舱取数（C2）：一屏要的数一次取回')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    b = collect()
    if a.json:
        print(json.dumps(b, ensure_ascii=False))
    else:
        print(render_text(b))
    sys.exit(0 if b['ok'] else 1)


if __name__ == '__main__':
    main()
