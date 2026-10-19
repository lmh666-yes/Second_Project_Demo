# -*- coding: utf-8 -*-
r"""虚拟链路仿真器（无硬件也能跑通链路）。

用途：硬件未到货时，造出与工程一致的字节流，喂给 Qt 上位机或协议工作台做联调。

协议口径（与工程一致）：
    AA 55 | CMD | LEN | DATA(LEN) | CRC_LO CRC_HI | 55 AA
    CRC16-MODBUS（初值 0xFFFF，多项式 0xA001），低字节在前，覆盖 CMD..DATA。

场景：
    normal   正常环境帧流（数值缓变）
    lossy    丢帧 + 帧间噪声（模拟无线不稳）
    noise    重度噪声（测上位机的重同步）
    replay   断网缓存补传：0x96 进行中 -> 若干 0x15 历史帧 -> 0x96 完成

输出：
    <out>.hex   连续十六进制，可直接粘进协议工作台的解帧页，或经虚拟串口喂给上位机
    <out>.log   场景说明（每帧的命令与解出的物理量、异常备注）
    默认写到脚本同级的 产物/ 目录；用 --out 可改到别处。

可选：--port 用 pyserial 直接写串口（需装 pyserial，配合虚拟串口对使用）。

用法：
    python 本地工具\工具箱\检查\链路仿真\link_sim.py --scenario normal --count 20
    python 本地工具\工具箱\检查\链路仿真\link_sim.py --scenario replay
    python 本地工具\工具箱\检查\链路仿真\link_sim.py --scenario lossy --out D:\tmp\case1
    python 本地工具\工具箱\检查\链路仿真\link_sim.py --selftest
"""
import os
import random
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


HEAD0, HEAD1, TAIL0, TAIL1 = 0xAA, 0x55, 0x55, 0xAA
MAX_LEN = 64


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build_frame(cmd, data):
    f = bytearray([HEAD0, HEAD1, cmd & 0xFF, len(data) & 0xFF])
    f += data
    c = crc16(bytes(f[2:]))
    f += bytes([c & 0xFF, (c >> 8) & 0xFF, TAIL0, TAIL1])
    return bytes(f)


def env_data(temp_c, humi_rh, press_hpa, light, tvoc, mq):
    def s16(v):
        return bytes([(v >> 8) & 0xFF, v & 0xFF])
    return (s16(int(round(temp_c * 100)) & 0xFFFF)
            + s16(int(round(humi_rh * 100)) & 0xFFFF)
            + s16(int(round(press_hpa * 10)) & 0xFFFF)
            + s16(light & 0xFFFF) + s16(tvoc & 0xFFFF) + s16(mq & 0xFFFF))


def ts_bytes(ts):
    return bytes([(ts >> 24) & 0xFF, (ts >> 16) & 0xFF, (ts >> 8) & 0xFF, ts & 0xFF])


def hex_spaced(b):
    return ' '.join('%02X' % x for x in b)


def parse_stream(stream):
    """最小解析：返回 [(offset, ok, cmd, data)]，用于自检与 log 判定。"""
    out = []
    n = len(stream)
    i = 0
    while i + 1 < n:
        if stream[i] != HEAD0 or stream[i + 1] != HEAD1:
            i += 1
            continue
        if i + 3 >= n:
            break
        cmd = stream[i + 2]
        ln = stream[i + 3]
        if ln > MAX_LEN:
            i += 1
            continue
        total = 8 + ln
        if i + total > n:
            break
        raw = stream[i:i + total]
        calc = crc16(raw[2:2 + 2 + ln])
        got = raw[4 + ln] | (raw[5 + ln] << 8)
        ok = (raw[total - 2] == TAIL0 and raw[total - 1] == TAIL1 and calc == got)
        out.append((i, ok, cmd, bytes(raw[4:4 + ln])))
        i += total
    return out


def decode_env(d):
    if len(d) < 12:
        return {}
    def i16(i):
        v = (d[i] << 8) | d[i + 1]
        return v - 0x10000 if v & 0x8000 else v
    return {
        'temp': i16(0) / 100.0,
        'humi': i16(2) / 100.0,
        'press': i16(4) / 10.0,
        'light': (d[6] << 8) | d[7],
        'tvoc': (d[8] << 8) | d[9],
        'mq': (d[10] << 8) | d[11],
    }


def drift(base, amp, rnd):
    return base + rnd.uniform(-amp, amp)


def env_values(i, rnd):
    return dict(temp=drift(23.5, 0.4, rnd), humi=drift(60.0, 2.5, rnd),
                press=drift(1013.0, 1.2, rnd), light=int(drift(1200, 300, rnd)),
                tvoc=int(drift(150, 30, rnd)), mq=int(drift(2048, 200, rnd)))


def env_frame(v):
    return build_frame(0x01, env_data(v['temp'], v['humi'], v['press'],
                                      v['light'], v['tvoc'], v['mq']))


def env_line(i, v):
    return ('帧%02d CMD=0x01 温度=%.2f℃ 湿度=%.2f%%RH 气压=%.1f hPa 光照=%d lx TVOC=%d ppb MQ135=%d'
            % (i, v['temp'], v['humi'], v['press'], v['light'], v['tvoc'], v['mq']))


def frame_line(i, f):
    return '帧%02d CMD=0x%02X %s' % (i, f[2], hex_spaced(f))


def scen_normal(count, rnd):
    chunks, log = [], []
    for i in range(count):
        v = env_values(i, rnd)
        f = env_frame(v)
        chunks.append(f)
        log.append(env_line(i, v))
    return chunks, log


def scen_lossy(count, rnd):
    chunks, log = [], []
    kept = 0
    for i in range(count):
        if rnd.random() < 0.25:
            log.append('帧%02d 丢弃（模拟无线丢包）' % i)
            continue
        if rnd.random() < 0.5:
            junk = bytes(rnd.randint(0, 255) for _ in range(rnd.randint(1, 3)))
            chunks.append(junk)
            log.append('  插入噪声 %s' % hex_spaced(junk))
        v = env_values(i, rnd)
        chunks.append(env_frame(v))
        log.append(env_line(i, v))
        kept += 1
    log.insert(0, '丢包场景：共 %d 帧，实际发送 %d 帧' % (count, kept))
    return chunks, log


def scen_noise(count, rnd):
    chunks, log = [], []
    for i in range(count):
        junk = bytes(rnd.randint(0, 255) for _ in range(rnd.randint(2, 5)))
        chunks.append(junk)
        v = env_values(i, rnd)
        chunks.append(env_frame(v))
        log.append('帧%02d 前后夹噪声 %s' % (i, hex_spaced(junk)))
    log.insert(0, '噪声场景：每帧前后夹 2~5 字节随机噪声，用于测上位机重同步')
    return chunks, log


def scen_replay(rnd, cache_n=3):
    chunks, log = [], []
    base_ts = 1000000
    log.append('补传场景：先 2 帧实时，再触发补传（0x96），回传 %d 条历史（0x15），最后 0x96 完成' % cache_n)
    for i in range(2):
        v = env_values(i, rnd)
        chunks.append(env_frame(v))
        log.append(env_line(i, v))
    chunks.append(build_frame(0x96, bytes([1, 0, 0, cache_n & 0xFF])))
    log.append('CMD=0x96 补传进行中：状态=1 已补传=0 剩余=%d' % cache_n)
    for j in range(cache_n):
        ts = base_ts + j * 1000
        v = env_values(j, rnd)
        env = env_data(v['temp'], v['humi'], v['press'], v['light'], v['tvoc'], v['mq'])
        hf = build_frame(0x15, ts_bytes(ts) + env)
        chunks.append(hf)
        log.append('CMD=0x15 历史帧 原始时间戳=%d 温度=%.2f℃ 湿度=%.2f%%RH' % (ts, v['temp'], v['humi']))
    chunks.append(build_frame(0x96, bytes([2, cache_n & 0xFF, 0, 0])))
    log.append('CMD=0x96 补传完成：状态=2 已补传=%d 剩余=0' % cache_n)
    return chunks, log


SCENARIOS = {
    'normal': lambda rnd: scen_normal(SCEN_COUNT, rnd),
    'lossy': lambda rnd: scen_lossy(SCEN_COUNT, rnd),
    'noise': lambda rnd: scen_noise(SCEN_COUNT, rnd),
    'replay': lambda rnd: scen_replay(rnd),
}
SCEN_COUNT = 10


def default_out_dir():
    """默认产物目录：脚本同级的 产物/，源码目录只留脚本与说明。
    目录不存在时自动建，便于首次运行。"""
    d = os.path.join(os.path.dirname(os.path.abspath(__file__)), '产物')
    try:
        os.makedirs(d, exist_ok=True)
    except OSError:
        return os.path.dirname(os.path.abspath(__file__))
    return d


def write_outputs(base, stream, log):
    hexpath = base + '.hex'
    logpath = base + '.log'
    with open(hexpath, 'w', encoding='utf-8', newline='\n') as f:
        i = 0
        while i < len(stream):
            f.write(hex_spaced(stream[i:i + 32]) + '\n')
            i += 32
    frames = parse_stream(stream)
    with open(logpath, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# 链路仿真日志\n\n')
        f.write('总字节 %d，解出帧 %d（其中通过 %d）\n\n'
                % (len(stream), len(frames), sum(1 for x in frames if x[1])))
        # H10：产物日志也带一条"怎么验收" —— 产物不是"发现问题"型报告，给的是判定口径。
        f.write('验收：解出帧数应等于场景该发的帧数，且"通过"数 = 解出数；'
                '对不上时按下面日志逐条核对（噪声/丢包场景允许解出数少于场景帧数）。\n\n')
        for line in log:
            f.write(line + '\n')
    return hexpath, logpath, frames


def send_port(port, baud, stream):
    try:
        import serial  # type: ignore
    except Exception:
        print('未安装 pyserial，无法写串口。可 pip install pyserial 后重试。')
        return False
    with serial.Serial(port, baud, timeout=1) as sp:
        sp.write(stream)
        sp.flush()
    print('已写入 %s @ %d，共 %d 字节' % (port, baud, len(stream)))
    return True


def selftest():
    ok = True
    out = []

    c = crc16(b'123456789')
    out.append(('CRC16-MODBUS 标准向量 123456789 = 0x4B37', c == 0x4B37))
    f1 = build_frame(0x16, bytes([0x01]))
    out.append(('组帧 0x16 01 = AA 55 16 01 01 50 54 55 AA',
                hex_spaced(f1) == 'AA 55 16 01 01 50 54 55 AA'))
    f2 = build_frame(0x16, bytes([0x00]))
    out.append(('组帧 0x16 00 = AA 55 16 01 00 91 94 55 AA',
                hex_spaced(f2) == 'AA 55 16 01 00 91 94 55 AA'))

    rnd = random.Random(1234)
    chunks, _ = scen_normal(5, rnd)
    stream = b''.join(chunks)
    frames = parse_stream(stream)
    out.append(('normal 场景 5 帧全部可解析通过',
                len(frames) == 5 and all(x[1] for x in frames)))

    rnd = random.Random(7)
    chunks, _ = scen_replay(rnd)
    stream = b''.join(chunks)
    frames = parse_stream(stream)
    hist = [x for x in frames if x[2] == 0x15]
    acks = [x for x in frames if x[2] == 0x96]
    out.append(('replay 场景含 3 条 0x15 历史帧与 2 条 0x96 应答',
                len(hist) == 3 and len(acks) == 2 and all(x[1] for x in frames)))

    rnd = random.Random(99)
    chunks, _ = scen_lossy(10, rnd)
    frames = parse_stream(b''.join(chunks))
    out.append(('lossy 场景含噪声仍能解出若干帧', len(frames) >= 5))

    # H6：补上原先"没被断言过"的几处 —— 场景覆盖（noise / lossy / replay 细节）、产物写出、
    # CRC 判定与重同步这两条判据本身。原先只有 6 条断言，且集中在 normal 与两条组帧用例上。
    _bad = bytearray(f1)
    _bad[4] ^= 0xFF                       # 改坏一个净荷字节 → CRC 对不上
    out.append(('H6 CRC 真的在起作用：改坏一个净荷字节 → 该帧判"不通过"',
                not parse_stream(bytes(_bad))[0][1]))

    _v = dict(temp=25.5, humi=60.0, press=1013.0, light=1000, tvoc=150, mq=2048)
    _dv = decode_env(parse_stream(env_frame(_v))[0][3])
    out.append(('H6 净荷往返：env_frame → parse_stream → decode_env 还原物理量',
                abs(_dv['temp'] - 25.5) < 0.01 and abs(_dv['press'] - 1013.0) < 0.01
                and _dv['light'] == 1000 and _dv['mq'] == 2048,
                str(_dv)))

    _pf = parse_stream(bytes([0x00, 0x01, 0xFE]) + f1)
    out.append(('H6 重同步：流前夹无用字节，仍只解出那 1 帧真帧（不把垃圾当帧）',
                len([x for x in _pf if x[1]]) == 1 and _pf[-1][2] == 0x16,
                '解出 %d 条' % len(_pf)))

    rnd = random.Random(2026)
    _nz, _nzlog = scen_noise(6, rnd)
    _nf = parse_stream(b''.join(_nz))
    out.append(('H6 noise 场景：噪声里重同步，6 帧真帧全部通过（且都是 CMD=0x01）',
                len([x for x in _nf if x[1]]) == 6
                and all(x[2] == 0x01 for x in _nf if x[1]),
                '真帧 %d' % len([x for x in _nf if x[1]])))

    rnd = random.Random(4321)
    _ls, _l2 = scen_lossy(10, rnd)
    _sent = sum(1 for l in _l2 if 'CMD=0x01' in l)
    out.append(('H6 lossy 场景：确实丢了包（发送 < 10），发送出去的帧全部通过',
                _sent < 10 and len([x for x in parse_stream(b''.join(_ls)) if x[1]]) == _sent,
                '发送 %d' % _sent))

    import shutil
    import tempfile
    _td = tempfile.mkdtemp(prefix='linksim_selftest_')
    try:
        _hx, _lg, _fr = write_outputs(os.path.join(_td, 'demo'), b''.join(_nz), _l2)
        _txt = open(_lg, encoding='utf-8').read()
        out.append(('H6 产物写得出：.hex / .log 都在，log 头部统计与 parse_stream 复算一致',
                    os.path.exists(_hx) and os.path.exists(_lg)
                    and ('总字节 %d，' % len(b''.join(_nz))) in _txt
                    and ('解出帧 %d' % len(_fr)) in _txt,
                    _txt.splitlines()[2] if len(_txt.splitlines()) > 2 else ''))
        # H10：产物日志里带一条明确的"验收判据"（产物不是"发现问题"型报告）
        out.append(('H10 产物日志带「验收」判据（解出帧数 = 该发帧数、通过数 = 解出数）',
                    '验收：' in _txt and '解出帧数应等于场景该发的帧数' in _txt))
    finally:
        shutil.rmtree(_td, ignore_errors=True)

    rnd = random.Random(5)
    _rp, _ = scen_replay(rnd, cache_n=3)
    _rf = parse_stream(b''.join(_rp))
    out.append(('H6 replay 场景：0x96 触发/完成各 1 条、3 条 0x15 历史帧且都带 4 字节时间戳',
                [x[2] for x in _rf if x[1]].count(0x96) == 2
                and len([x for x in _rf if x[1] and x[2] == 0x15]) == 3
                and all(len(x[3]) >= 4 for x in _rf if x[1] and x[2] == 0x15)))

    for item in out:
        what, good = item[0], item[1]
        extra = str(item[2]) if len(item) > 2 and item[2] else ''
        print('  %s  %s%s' % ('PASS' if good else 'FAIL', what,
                              ('  ' + extra) if extra else ''))
        ok = ok and good
    print('链路仿真自检：%s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


def main(argv):
    global SCEN_COUNT
    if '--selftest' in argv:
        return selftest()

    scenario = 'normal'
    count = 10
    base = None
    seed = 1234
    port = None
    baud = 115200
    bad = []
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in ('--scenario', '--count', '--out', '--seed', '--port', '--baud'):
            if i + 1 >= len(argv):
                bad.append('%s 缺少值' % a)
                i += 1
                continue
            if a == '--scenario':
                scenario = argv[i + 1]
            elif a == '--count':
                count = int(argv[i + 1])
            elif a == '--out':
                base = os.path.abspath(argv[i + 1])
            elif a == '--seed':
                seed = int(argv[i + 1])
            elif a == '--port':
                port = argv[i + 1]
            else:
                baud = int(argv[i + 1])
            i += 2
        else:
            bad.append(a)
            i += 1

    # 参数写错要报错退出，不能静默忽略（否则会按默认场景生成一堆产物）
    if bad:
        print('未知参数：%s' % ' '.join(bad))
        print('可用参数：--scenario <%s> | --count <N> | --seed <N> | --out <前缀> | '
              '--port <COM口> | --baud <N> | --selftest'
              % '|'.join(SCENARIOS))
        return 2

    if scenario not in SCENARIOS:
        print('未知场景：%s（可选 %s）' % (scenario, ' / '.join(SCENARIOS)))
        return 2

    SCEN_COUNT = count
    rnd = random.Random(seed)
    chunks, log = SCENARIOS[scenario](rnd)
    stream = b''.join(chunks)

    if base is None:
        base = os.path.join(default_out_dir(), 'link_%s' % scenario)
    hexpath, logpath, frames = write_outputs(base, stream, log)

    print('链路仿真完成')
    print('  场景    : %s（seed=%d count=%d）' % (scenario, seed, count))
    print('  总字节  : %d' % len(stream))
    print('  解出帧  : %d（通过 %d）' % (len(frames), sum(1 for x in frames if x[1])))
    print('  hex     : %s' % hexpath)
    print('  log     : %s' % logpath)

    if port:
        send_port(port, baud, stream)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
