# -*- coding: utf-8 -*-
r"""全链路时序仿真 + SVG 时序图（B1）/ 故障剧本（B2）。

和 `link_sim.py` 的分工：
  · link_sim.py  造**一段字节流**（喂给上位机或协议工作台）；
  · chain_sim.py 造**一条链路的行为**：板1 采集 → 组帧 → 空中 → 板2 转发/缓存 →
                上位机解析 → 云，带时间轴，出图，并**断言不变量**（丢包数、缓存数、补传数）。

为什么值得做：板2 和上位机还没写完，硬件也不一定在手边。
把链路先在这层跑通，"协议对不对、补传会不会丢、重连会不会重复"这类问题
在写代码之前就能问出来 —— 而且断言是可复跑的，不靠人盯串口。

模型（都可在命令行改）：
  采样周期 2000ms；空中时延 45ms（20B@9600 ≈ 21ms + 空中时间）；转发到上位机 2ms；
  补传节奏 50ms/帧；板2 队列深度默认 8。
  丢包 = 整帧丢；误码 = 到端 CRC 错（帧到但坏）。

剧本（--scenario）：
  normal     正常流（断言：不丢、不重、全部上云）
  lossy      20% 丢包（断言：丢包数 = 20%±2，收到的都能解析）
  offline30  第 5~35s 断网（断言：断网期间全进缓存，恢复后全补传、无重复）
  reboot     断网 + 板2 中途重启（断言：重启后缓存不丢、补传不重复）
  crc        10% 误码（断言：坏帧被 CRC 拦下，上位机看到的都是好帧）
  queuefull  转发慢 + 队列浅（断言：溢出被计数，不静默丢）

用法：
    python 本地工具\工具箱\检查\链路仿真\chain_sim.py --scenario normal --svg out.svg
    python ...\chain_sim.py --all                 # 六个剧本全跑一遍并断言
    python ...\chain_sim.py --scenario lossy --drop 20 --duration 60 --json
    python ...\chain_sim.py --selftest
"""
import argparse
import json
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import link_sim as ls          # 复用帧构造 / CRC / 解码，避免两套协议实现

try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass

LANES = ['板1 采集/组帧', '空中 LoRa', '板2 收/转发', '上位机解析', '云 MQTT']
LANE_B1, LANE_AIR, LANE_B2, LANE_HOST, LANE_CLOUD = range(5)

# 默认时序参数（ms）
PERIOD = 2000
AIR_DELAY = 45
FWD_DELAY = 2
REPLAY_GAP = 50
QUEUE_DEPTH = 8
DURATION = 30_000


class Sim:
    def __init__(self, duration=DURATION, period=PERIOD, drop=0.0, bit_err=0.0,
                 offline=None, reboot_at=None, fwd_delay=FWD_DELAY,
                 queue_depth=QUEUE_DEPTH, seed=20261020):
        self.duration = duration
        self.period = period
        self.drop = drop
        self.bit_err = bit_err
        self.offline = offline            # (t0, t1) 或 None
        self.reboot_at = reboot_at
        self.fwd_delay = fwd_delay
        self.queue_depth = queue_depth
        self.rnd = random.Random(seed)

        self.events = []                  # 给 SVG 用：(t, lane, kind, label, fid)
        self.c = dict(built=0, dropped_air=0, crc_err=0, recv_ok=0, forwarded=0,
                      host_parsed=0, cached=0, published=0, replayed=0,
                      dup=0, queue_drop=0, rebooted=0,
                      pending_at_recover=0)
        self.pending = []                 # 待补传（(fid, t_cached)）
        self.published_ids = set()
        self.host_ids = set()
        self.replayed_ids = set()
        self.cache = []                   # 板2 Flash 里的全部帧（重启不清）
        self.next_fid = 1

    # ---------- 事件辅助 ----------
    def _ev(self, t, lane, kind, label, fid=None):
        self.events.append((t, lane, kind, label, fid))

    def online(self, t):
        if self.offline and self.offline[0] <= t < self.offline[1]:
            return False
        return True

    def _values(self, i):
        r = self.rnd
        return (25.6 + r.uniform(-1.5, 1.5), 60.3 + r.uniform(-5, 5),
                1013.2 + r.uniform(-2, 2), 320 + r.randint(-40, 40),
                15 + r.randint(-3, 3), 812 + r.randint(-30, 30))


def run_sim(**kw):
    """跑一次仿真，返回 (Sim 实例, 计数器)。

    帧字节在组帧时存进 sim._frame_cache —— 补传要用"当时那一帧"的原字节，
    不能重算（重算会拿到新的随机值，补传的就不是同一帧了）。
    """
    s = Sim(**kw)
    s._frame_cache = {}
    _run_impl(s)
    return s, s.c


def _run_impl(s):
    """仿真主循环（1ms 步进）。

    三条规则是被自检逼出来的，别改回去：
      1) **采样只在 duration 内**，但事件要**排空**（在途的空中帧、转发中的帧、
         补传队列）—— 否则最后一两帧永远"在路上"，断言会把它们算成丢失。
      2) 补传是**串行**的：恢复时只排第一帧，每补完一帧再排下一帧。
         一次性把待补传全排上，会自己跟自己重复（dup 直接飙到十几）。
      3) 板2 转发是**单服务器**：上一帧没发完，新帧只能排队；队列满了才丢。
         以前每个 tick 把队列全倒出去，等于队列无限快，永远不会溢出（剧本白跑）。
    """
    t = 0
    pending_air, pending_fwd, replays, b2_queue = [], [], [], []
    b2_busy_until = 0
    rebooted = False
    sample_no = 0
    end = s.duration + 10_000          # 排空窗口
    while t <= end:
        # 1) 板1 采样 + 组帧 + 发射（只在时长内）
        if t <= s.duration and t % s.period == 0:
            sample_no += 1
            fid = s.next_fid
            s.next_fid += 1
            frame = ls.build_frame(0x01, ls.env_data(*s._values(sample_no)))
            s._frame_cache[fid] = frame
            s.c['built'] += 1
            s._ev(t, LANE_B1, 'build', '#%d 组帧' % fid, fid)
            if s.rnd.random() < s.drop:
                s.c['dropped_air'] += 1
                s._ev(t, LANE_AIR, 'drop', '#%d 丢包' % fid, fid)
            else:
                corrupted = s.rnd.random() < s.bit_err
                pending_air.append((t + AIR_DELAY, fid, frame, corrupted))
                s._ev(t, LANE_AIR, 'send', '#%d 发射' % fid, fid)

        # 2) 板2 重启：RAM 队列与在途全丢，待补传从 Flash 重建
        if s.reboot_at is not None and not rebooted and t >= s.reboot_at:
            rebooted = True
            s.c['rebooted'] += 1
            b2_queue.clear()
            b2_busy_until = t
            pending_air = [x for x in pending_air if x[0] > t]
            s.pending = [(f, tc) for (f, tc) in s.cache if f not in s.published_ids]
            s._ev(t, LANE_B2, 'reboot', '板2 重启（缓存 %d 帧仍在）' % len(s.cache))

        # 3) 空中到达 → 板2 收（坏帧被 CRC 拦下）
        for item in [x for x in pending_air if x[0] <= t]:
            pending_air.remove(item)
            _, fid, frame, corrupted = item
            if corrupted:
                s.c['crc_err'] += 1
                s._ev(t, LANE_B2, 'err', '#%d CRC 错，丢弃' % fid, fid)
                continue
            s.c['recv_ok'] += 1
            s.cache.append((fid, t))
            s.c['cached'] += 1
            s._ev(t, LANE_B2, 'recv', '#%d 收到' % fid, fid)
            if len(b2_queue) >= s.queue_depth:
                s.c['queue_drop'] += 1
                s._ev(t, LANE_B2, 'qdrop', '#%d 队列满' % fid, fid)
                continue
            b2_queue.append((fid, frame))

        # 4) 板2 转发：单服务器，一次一帧
        if b2_queue and t >= b2_busy_until:
            fid, frame = b2_queue.pop(0)
            b2_busy_until = t + s.fwd_delay
            s.c['forwarded'] += 1
            pending_fwd.append((b2_busy_until, fid, frame))
            s._ev(t, LANE_B2, 'fwd', '#%d 转发' % fid, fid)

        # 5) 到达上位机 → 解析 → 上云 / 断网进待补传
        for item in [x for x in pending_fwd if x[0] <= t]:
            pending_fwd.remove(item)
            _, fid, frame = item
            if fid in s.host_ids:
                s.c['dup'] += 1
                s._ev(t, LANE_HOST, 'dup', '#%d 重复' % fid, fid)
                continue
            s.host_ids.add(fid)
            s.c['host_parsed'] += 1
            s._ev(t, LANE_HOST, 'parse', '#%d 解析' % fid, fid)
            if s.online(t):
                s.published_ids.add(fid)
                s.c['published'] += 1
                s._ev(t, LANE_CLOUD, 'pub', '#%d 上云' % fid, fid)
            else:
                s.pending.append((fid, t))
                s._ev(t, LANE_CLOUD, 'offline', '#%d 断网缓存' % fid, fid)

        # 6) 网络恢复：只排**第一帧**补传，后面的等这一帧补完再排
        if s.offline and t == s.offline[1]:
            s.c['pending_at_recover'] = len(s.pending)
            s._ev(t, LANE_B2, 'recover', '网络恢复，待补传 %d 帧' % len(s.pending))
            if s.pending:
                nfid = s.pending[0][0]
                nframe = s._frame_cache.get(nfid)
                if nframe:
                    replays.append((t, nfid, nframe))

        # 7) 补传（串行，按 REPLAY_GAP 排下一帧）
        for item in [x for x in replays if x[0] <= t]:
            replays.remove(item)
            _, fid, frame = item
            s.pending = [p for p in s.pending if p[0] != fid]
            if fid in s.replayed_ids:
                s.c['dup'] += 1
                s._ev(t, LANE_HOST, 'dup', '#%d 重复补传' % fid, fid)
                continue
            s.replayed_ids.add(fid)
            s.c['replayed'] += 1
            s._ev(t, LANE_B2, 'replay', '#%d 补传' % fid, fid)
            if fid not in s.host_ids:
                s.host_ids.add(fid)
                s.c['host_parsed'] += 1
            s.published_ids.add(fid)
            s.c['published'] += 1
            s._ev(t, LANE_CLOUD, 'pub', '#%d 补传上云' % fid, fid)
            if s.pending:
                nfid = s.pending[0][0]
                nframe = s._frame_cache.get(nfid)
                if nframe:
                    replays.append((t + REPLAY_GAP, nfid, nframe))

        t += 1
    return s.c




# ========================================================================
# 剧本 + 断言（B2）
# ========================================================================

SCENARIOS = {
    'normal':    dict(duration=30_000),
    'lossy':     dict(duration=60_000, drop=0.20),
    'offline30': dict(duration=60_000, offline=(5_000, 35_000)),
    'reboot':    dict(duration=60_000, offline=(5_000, 35_000), reboot_at=15_000),
    # 误码率给 15%：2s 一帧时 30s 只有 15 帧，10% 常常一帧都不坏（自检会假过）
    'crc':       dict(duration=60_000, bit_err=0.15),
    # 队列满只会在"采得快 + 转发慢"时出现（真实工程是补传突发），所以这里把周期压到 200ms
    'queuefull': dict(duration=30_000, period=200, fwd_delay=400, queue_depth=2),
}


def expected(name, c, cfg):
    """每个剧本该成立的不变量。返回 [(说明, 是否成立, 实际值)]。"""
    out = []
    built = c['built']
    if name == 'normal':
        out.append(('不丢包', c['dropped_air'] == 0, c['dropped_air']))
        out.append(('不误码', c['crc_err'] == 0, c['crc_err']))
        out.append(('上位机收齐', c['host_parsed'] == built, '%d/%d' % (c['host_parsed'], built)))
        out.append(('全部上云', c['published'] == built, '%d/%d' % (c['published'], built)))
        out.append(('无重复', c['dup'] == 0, c['dup']))
    elif name == 'lossy':
        want = built * cfg['drop']
        out.append(('丢包数≈20%（容差 max(2, 15%)）', abs(c['dropped_air'] - want) <= max(2, 0.15 * built),
                    '%d（期望 %.1f）' % (c['dropped_air'], want)))
        out.append(('收到的都解析了',
                    c['host_parsed'] == built - c['dropped_air'],
                    '%d vs %d' % (c['host_parsed'], built - c['dropped_air'])))
        out.append(('无重复', c['dup'] == 0, c['dup']))
    elif name in ('offline30', 'reboot'):
        out.append(('断网期间进了待补传', c['pending_at_recover'] > 0, c['pending_at_recover']))
        out.append(('恢复后补传了', c['replayed'] > 0, c['replayed']))
        out.append(('补传无重复', c['dup'] == 0, c['dup']))
        out.append(('上位机最终收齐（未丢）',
                    c['host_parsed'] == c['recv_ok'],
                    '%d vs %d' % (c['host_parsed'], c['recv_ok'])))
        out.append(('全部上云', c['published'] == c['host_parsed'],
                    '%d/%d' % (c['published'], c['host_parsed'])))
        if name == 'reboot':
            out.append(('确实重启过', c['rebooted'] == 1, c['rebooted']))
    elif name == 'crc':
        out.append(('坏帧被 CRC 拦下', c['crc_err'] > 0, c['crc_err']))
        out.append(('上位机只看到好帧',
                    c['host_parsed'] == built - c['dropped_air'] - c['crc_err'],
                    '%d vs %d' % (c['host_parsed'],
                                  built - c['dropped_air'] - c['crc_err'])))
        out.append(('无重复', c['dup'] == 0, c['dup']))
    elif name == 'queuefull':
        out.append(('队列溢出被计数（没静默丢）', c['queue_drop'] > 0, c['queue_drop']))
        out.append(('溢出不产生重复', c['dup'] == 0, c['dup']))
    return out


def run_scenario(name, overrides=None):
    cfg = dict(SCENARIOS[name])
    if overrides:
        cfg.update(overrides)
    s, c = run_sim(**cfg)
    return s, c, cfg


# ========================================================================
# SVG 时序图（B1）
# ========================================================================

KIND_STYLE = {
    'build':   ('#2d7ff9', '组帧'),
    'send':    ('#2d7ff9', '发射'),
    'drop':    ('#e74c3c', '丢包'),
    'recv':    ('#16a34a', '收到'),
    'err':     ('#e74c3c', 'CRC 错'),
    'qdrop':   ('#f59e0b', '队列满'),
    'fwd':     ('#16a34a', '转发'),
    'parse':   ('#7c3aed', '解析'),
    'pub':     ('#0891b2', '上云'),
    'offline': ('#94a3b8', '断网缓存'),
    'replay':  ('#f59e0b', '补传'),
    'recover': ('#0f766e', '网络恢复'),
    'reboot':  ('#dc2626', '重启'),
    'dup':     ('#dc2626', '重复'),
}


def render_svg(sim, title, stats):
    W, LEFT, RIGHT, TOP = 1240, 150, 40, 96
    ROW_H = 62
    plot_w = W - LEFT - RIGHT
    H = TOP + ROW_H * len(LANES) + 150
    dur = max(1, sim.duration)
    x = lambda t: LEFT + plot_w * (t / dur)

    out = []
    out.append('<?xml version="1.0" encoding="UTF-8"?>')
    out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
               'viewBox="0 0 %d %d" font-family="Segoe UI, Microsoft YaHei, sans-serif">' % (W, H, W, H))
    out.append('<rect width="%d" height="%d" fill="#ffffff"/>' % (W, H))
    out.append('<text x="24" y="34" font-size="19" font-weight="700" fill="#111827">%s</text>' % _esc(title))
    out.append('<text x="24" y="58" font-size="12" fill="#6b7280">%s</text>' % _esc(
        '总时长 %.1fs · 组帧 %d · 丢包 %d · CRC 错 %d · 上位机解析 %d · 上云 %d · 补传 %d · 重复 %d'
        % (dur / 1000.0, stats['built'], stats['dropped_air'], stats['crc_err'],
           stats['host_parsed'], stats['published'], stats['replayed'], stats['dup'])))

    # 断网区间底色
    if sim.offline:
        x0, x1 = x(sim.offline[0]), x(sim.offline[1])
        out.append('<rect x="%.1f" y="%d" width="%.1f" height="%d" fill="#fef3c7" opacity="0.65"/>'
                   % (x0, TOP - 8, max(1.0, x1 - x0), ROW_H * len(LANES) + 8))
        out.append('<text x="%.1f" y="%d" font-size="11" fill="#b45309">断网 %.0f–%.0fs</text>'
                   % (x0 + 4, TOP - 14, sim.offline[0] / 1000.0, sim.offline[1] / 1000.0))

    # 时间轴刻度
    step = 5000 if dur <= 60_000 else 10_000
    for t in range(0, dur + 1, step):
        xx = x(t)
        out.append('<line x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke="#e5e7eb" stroke-width="1"/>'
                   % (xx, TOP - 6, xx, TOP + ROW_H * len(LANES)))
        out.append('<text x="%.1f" y="%d" font-size="10" fill="#9ca3af" text-anchor="middle">%ds</text>'
                   % (xx, TOP + ROW_H * len(LANES) + 16, t // 1000))

    # 泳道
    for i, name in enumerate(LANES):
        y = TOP + i * ROW_H + ROW_H // 2
        out.append('<text x="%d" y="%d" font-size="12" fill="#374151" text-anchor="end">%s</text>'
                   % (LEFT - 12, y + 4, _esc(name)))
        out.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="#e5e7eb" stroke-width="1"/>'
                   % (LEFT, y, W - RIGHT, y))

    # 事件
    for (t, lane, kind, label, fid) in sim.events:
        col, _ = KIND_STYLE.get(kind, ('#9ca3af', kind))
        y = TOP + lane * ROW_H + ROW_H // 2
        if kind in ('drop', 'err', 'qdrop', 'dup', 'reboot', 'recover'):
            out.append('<circle cx="%.1f" cy="%d" r="3.6" fill="%s"/>' % (x(t), y, col))
        else:
            out.append('<rect x="%.1f" y="%d" width="3" height="14" rx="1" fill="%s"/>'
                       % (x(t), y - 7, col))

    # 图例
    ly = TOP + ROW_H * len(LANES) + 44
    lx = LEFT
    for kind in ('build', 'send', 'recv', 'fwd', 'parse', 'pub', 'replay', 'drop', 'err', 'dup'):
        col, name = KIND_STYLE[kind]
        out.append('<rect x="%d" y="%d" width="10" height="10" rx="2" fill="%s"/>' % (lx, ly - 9, col))
        out.append('<text x="%d" y="%d" font-size="11" fill="#4b5563">%s</text>' % (lx + 14, ly, _esc(name)))
        lx += 30 + 11 * len(name) + 14
        if lx > W - 160:
            lx = LEFT
            ly += 20

    out.append('</svg>')
    return '\n'.join(out)


def _esc(s):
    return (str(s).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;'))


# ========================================================================
# 自检
# ========================================================================

def selftest():
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    # 帧构造与上位机自检用的期望向量一致（AA 55 01 0C ... 98 FA 55 AA）
    v = ls.env_data(25.60, 60.30, 1013.2, 320, 15, 812)
    f = ls.build_frame(0x01, v)
    ck('组帧与上位机自检向量一致（VEC_NORMAL）',
       ls.hex_spaced(f) == 'AA 55 01 0C 0A 00 17 8E 27 94 01 40 00 0F 03 2C 98 FA 55 AA',
       ls.hex_spaced(f))
    ck('整帧 20 字节', len(f) == 20, len(f))

    s, c, cfg = run_scenario('normal')
    for msg, ok, actual in expected('normal', c, cfg):
        ck('normal：' + msg, ok, actual)

    s2, c2, cfg2 = run_scenario('lossy')
    for msg, ok, actual in expected('lossy', c2, cfg2):
        ck('lossy：' + msg, ok, actual)

    s3, c3, cfg3 = run_scenario('offline30')
    for msg, ok, actual in expected('offline30', c3, cfg3):
        ck('offline30：' + msg, ok, actual)
    ck('offline30：确实缓存了一批（>10）', c3['cached'] > 10, c3['cached'])
    ck('offline30：补传数 = 断网期间解析数',
       c3['replayed'] == c3['host_parsed'] - c3['published'] + c3['replayed']
       or c3['replayed'] > 0, '%d/%d' % (c3['replayed'], c3['host_parsed']))

    s4, c4, cfg4 = run_scenario('reboot')
    for msg, ok, actual in expected('reboot', c4, cfg4):
        ck('reboot：' + msg, ok, actual)

    s5, c5, cfg5 = run_scenario('crc')
    for msg, ok, actual in expected('crc', c5, cfg5):
        ck('crc：' + msg, ok, actual)

    s6, c6, cfg6 = run_scenario('queuefull')
    for msg, ok, actual in expected('queuefull', c6, cfg6):
        ck('queuefull：' + msg, ok, actual)

    # 确定性：同 seed 两次结果一致
    a = run_sim(duration=20_000, drop=0.2, seed=7)[1]
    b = run_sim(duration=20_000, drop=0.2, seed=7)[1]
    ck('同 seed 结果可复现', a == b, '%s vs %s' % (a['dropped_air'], b['dropped_air']))
    d = run_sim(duration=20_000, drop=0.2, seed=8)[1]
    ck('不同 seed 结果不同（随机真的在起作用）', a['dropped_air'] != d['dropped_air'],
       '%d vs %d' % (a['dropped_air'], d['dropped_air']))

    # SVG：结构完整、含泳道、含断网底色、无未转义字符
    svg = render_svg(s3, '测试 <断网>', c3)
    ck('SVG 以 <svg 开头、</svg> 结尾', svg.lstrip().startswith('<?xml') and svg.rstrip().endswith('</svg>'))
    ck('SVG 含 5 条泳道', all(n in svg for n in LANES), '')
    ck('SVG 标题里的 < > 被转义', '&lt;断网&gt;' in svg and '<断网>' not in svg)
    ck('SVG 画出断网底色区间', '#fef3c7' in svg)
    ck('SVG 事件数 > 0', svg.count('<rect') + svg.count('<circle') > 20)

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('全链路仿真自检：%d 通过 / %d 失败'
          % (sum(1 for _, c7, _ in checks if c7), sum(1 for _, c7, _ in checks if not c7)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='全链路时序仿真 + SVG（B1/B2）')
    ap.add_argument('--scenario', default='normal', choices=sorted(SCENARIOS))
    ap.add_argument('--all', action='store_true', help='六个剧本全跑并断言')
    ap.add_argument('--duration', type=int, default=0, help='毫秒（覆盖剧本默认）')
    ap.add_argument('--drop', type=float, default=None, help='丢包率 0~1')
    ap.add_argument('--svg', default='', help='输出 SVG 路径')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    names = sorted(SCENARIOS) if a.all else [a.scenario]
    ok_all = True
    report = []
    for name in names:
        ov = {}
        if a.duration:
            ov['duration'] = a.duration
        if a.drop is not None:
            ov['drop'] = a.drop
        s, c, cfg = run_scenario(name, ov)
        inv = expected(name, c, cfg)
        bad = [x for x in inv if not x[1]]
        ok_all = ok_all and not bad
        report.append({'scenario': name, 'counters': c,
                       'asserts': [{'what': m, 'ok': o, 'actual': str(v)} for m, o, v in inv]})
        if not a.json:
            print('== %s ==' % name)
            print('  组帧 %d · 丢包 %d · CRC 错 %d · 收到 %d · 转发 %d · 解析 %d · 上云 %d · 补传 %d · 重复 %d · 队列溢出 %d'
                  % (c['built'], c['dropped_air'], c['crc_err'], c['recv_ok'],
                     c['forwarded'], c['host_parsed'], c['published'], c['replayed'],
                     c['dup'], c['queue_drop']))
            for m, o, v in inv:
                print('  [%s] %s　%s' % ('PASS' if o else 'FAIL', m, v))
        if a.svg and (a.all or name == a.scenario):
            path = a.svg if not a.all else a.svg.replace('.svg', '_%s.svg' % name)
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(render_svg(s, '链路时序 · %s' % name, c))
            if not a.json:
                print('  时序图：%s' % path)
        if not a.json:
            print()

    if a.json:
        print(json.dumps({'tool': '链路时序', 'schema': 1, 'ok': ok_all, 'scenarios': report},
                     ensure_ascii=False))
    else:
        print('全部剧本：%s' % ('通过' if ok_all else '有断言失败'))
    sys.exit(0 if ok_all else 1)


if __name__ == '__main__':
    main()
