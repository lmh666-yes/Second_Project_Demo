# -*- coding: utf-8 -*-
r"""上位机解析器对照（B3）：把"两边协议到底一不一致"变成可跑的检查。

为什么不能只比常量：常量对了，组帧顺序/字节序/CRC 范围仍可能错。
所以这里做三层对照，能跑几层跑几层，跑不了的**明说卡在哪**：

  第 1 层  常量：`sys_frame.h`（固件）vs `frameparser.h`（上位机）
  第 2 层  期望向量：上位机自检里写死的 VEC_NORMAL / VEC_NEGATIVE
           vs 仿真器按同样输入组出来的字节 —— 逐字节比
  第 3 层  真解析：把仿真帧喂给上位机的 FrameParser，逐字段比
           —— 需要上位机协议层已实现；现在 `frameparser.cpp` 还有 TODO(阶段2) 桩，
           所以这一层会报「被 F1 阻塞」并给出还剩多少处，**不假装跑过**

用法：
    python 本地工具\工具箱\检查\协议对照\host_compare.py
    python ...\host_compare.py --json
    python ...\host_compare.py --selftest
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', '链路仿真'))
import link_sim as ls

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
ROOT = find_root(HERE) or os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
FW_HDR = 'EnvMonitor/Project_1_检测端/Project/FWLIB/inc/sys_frame.h'
HOST_HDR = 'EnvMonitor/QT_project/SerialMonitor/frameparser.h'
HOST_CPP = 'EnvMonitor/QT_project/SerialMonitor/frameparser.cpp'
HOST_TEST = 'EnvMonitor/QT_project/SerialMonitor/consistencytest.cpp'

# 要对照的常量：名字 → (固件宏名, 上位机常量名)
CONSTS = [
    ('帧头高字节', 'SYS_FRAME_HEAD_HI', 'HEAD_HI'),
    ('帧头低字节', 'SYS_FRAME_HEAD_LO', 'HEAD_LO'),
    ('帧尾高字节', 'SYS_FRAME_TAIL_HI', 'TAIL_HI'),
    ('帧尾低字节', 'SYS_FRAME_TAIL_LO', 'TAIL_LO'),
    ('固定开销', 'SYS_FRAME_OVERHEAD', 'OVERHEAD'),
    ('环境帧命令字', 'SYS_FRAME_CMD_ENV', 'CMD_ENV'),
    ('环境帧数据长度', 'SYS_FRAME_ENV_LEN', 'ENV_LEN'),
    ('整帧长度', None, 'ENV_TOTAL'),          # 固件侧是 ENV_LEN + OVERHEAD，没有同名宏
]


def read(path):
    with open(os.path.join(ROOT, path), 'rb') as f:
        raw = f.read()
    for enc in ('utf-8', 'gbk'):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace')


def parse_fw_consts(text):
    """#define SYS_FRAME_XXX  0xAAU  → {'SYS_FRAME_XXX': 170}"""
    out = {}
    for m in re.finditer(r'#define\s+(SYS_FRAME_[A-Z0-9_]+)\s+\(?\s*(0x[0-9A-Fa-f]+|\d+)[uUlL]*\s*\)?', text):
        out[m.group(1)] = int(m.group(2), 0)
    return out


def parse_host_consts(text):
    """constexpr quint8 HEAD_HI = 0xAAU;  /  constexpr int ENV_TOTAL = ENV_LEN + OVERHEAD;"""
    raw = {}
    for m in re.finditer(r'constexpr\s+\w+\s+([A-Z0-9_]+)\s*=\s*([^;]+);', text):
        raw[m.group(1)] = m.group(2).strip()

    def ev(expr, seen):
        e = expr.strip().rstrip('uUlL')
        e = re.sub(r'\b([A-Z][A-Z0-9_]*)\b', lambda mm: str(ev(raw[mm.group(1)], seen | {mm.group(1)}))
                   if mm.group(1) in raw and mm.group(1) not in seen else mm.group(1), e)
        e = e.replace('/', '//')
        try:
            return int(eval(e, {'__builtins__': {}}, {}))   # 只放数字与 + - * //，输入来自本仓头文件
        except Exception:
            return None

    out = {}
    for k in raw:
        v = ev(raw[k], {k})
        if isinstance(v, int):
            out[k] = v
    return out


def parse_vectors(text):
    """consistencytest.cpp 里的 VEC_XXX = "AA 55 ...";"""
    out = {}
    for m in re.finditer(r'static\s+const\s+char\s*\*\s*(VEC_[A-Z0-9_]+)\s*=\s*((?:\s*"[^"]*"\s*)+);', text):
        name = m.group(1)
        hexs = ''.join(re.findall(r'"([^"]*)"', m.group(2)))
        try:
            out[name] = bytes(int(x, 16) for x in hexs.split())
        except ValueError:
            pass
    return out


def host_stub_count(text):
    return text.count('TODO(阶段2)')


def compare_consts(fw, host):
    rows = []
    for label, fwname, hostname in CONSTS:
        fv = fw.get(fwname) if fwname else None
        hv = host.get(hostname)
        if fwname is None:      # 整帧长度：固件侧算出来
            fv = (fw.get('SYS_FRAME_ENV_LEN') or 0) + (fw.get('SYS_FRAME_OVERHEAD') or 0)
        ok = (fv is not None and hv is not None and fv == hv)
        rows.append({'what': label, 'fw': fv, 'host': hv, 'ok': ok})
    return rows


def compare_vectors(vectors):
    """按上位机自检的期望向量，用仿真器组一遍，逐字节比。"""
    cases = {
        'VEC_NORMAL': (25.60, 60.30, 1013.2, 320, 15, 812),
        'VEC_NEGATIVE': (-5.00, 45.00, 1000.0, 0, 0, 0),
    }
    rows = []
    for name, vals in cases.items():
        want = vectors.get(name)
        if want is None:
            rows.append({'what': name, 'ok': False, 'detail': '上位机自检里没找到这个向量'})
            continue
        got = ls.build_frame(0x01, ls.env_data(*vals))
        ok = got == want
        detail = '逐字节一致（%d 字节）' % len(want) if ok else \
                 '期望 %s\n实际 %s' % (ls.hex_spaced(want), ls.hex_spaced(got))
        rows.append({'what': name, 'ok': ok, 'detail': detail})
    return rows


def run_all():
    fw = parse_fw_consts(read(FW_HDR))
    host = parse_host_consts(read(HOST_HDR))
    vectors = parse_vectors(read(HOST_TEST))
    stubs = host_stub_count(read(HOST_CPP))
    return {
        'consts': compare_consts(fw, host),
        'vectors': compare_vectors(vectors),
        'host_stubs': stubs,
        'layer3': ('blocked' if stubs else 'ready'),
    }


def selftest():
    ok_all = True
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    fw_txt = '#define SYS_FRAME_HEAD_HI 0xAAU\n#define SYS_FRAME_ENV_LEN 0x0CU\n'
    fw = parse_fw_consts(fw_txt)
    ck('解析固件 #define（含 0x..U 后缀）', fw == {'SYS_FRAME_HEAD_HI': 0xAA,
                                                  'SYS_FRAME_ENV_LEN': 12}, fw)

    host_txt = ('constexpr quint8 HEAD_HI = 0xAAU;\n'
                'constexpr quint8 ENV_LEN = 0x0CU;\n'
                'constexpr int  OVERHEAD = 8;\n'
                'constexpr int  ENV_TOTAL = ENV_LEN + OVERHEAD;\n')
    host = parse_host_consts(host_txt)
    ck('解析上位机 constexpr（含表达式 ENV_LEN + OVERHEAD）',
       host.get('ENV_TOTAL') == 20 and host.get('HEAD_HI') == 0xAA, host)

    vec_txt = 'static const char *VEC_X =\n    "AA 55 01 0C 0A 00 17 8E 27 94 01 40 00 0F 03 2C 98 FA 55 AA";\n'
    vs = parse_vectors(vec_txt)
    ck('解析上位机期望向量', len(vs.get('VEC_X', b'')) == 20, len(vs.get('VEC_X', b'')))

    real = run_all()
    bad = [r for r in real['consts'] if not r['ok']]
    ck('真实：8 项协议常量全部一致', not bad, bad)
    bad2 = [r for r in real['vectors'] if not r['ok']]
    ck('真实：仿真组帧与上位机期望向量逐字节一致', not bad2,
       [r['what'] for r in bad2])
    ck('真实：能读出上位机还剩多少处 TODO(阶段2)',
       isinstance(real['host_stubs'], int), real['host_stubs'])
    ck('第 3 层状态明确（blocked / ready）', real['layer3'] in ('blocked', 'ready'))
    if real['host_stubs']:
        ck('上位机协议层还是桩 → 第 3 层必须报 blocked（不假装跑过）',
           real['layer3'] == 'blocked', real['layer3'])

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('协议对照自检：%d 通过 / %d 失败'
          % (sum(1 for _, c, _ in checks if c), sum(1 for _, c, _ in checks if not c)))
    return ok_all


def main():
    ap = argparse.ArgumentParser(description='上位机解析器对照（B3）')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)

    r = run_all()
    ok = all(x['ok'] for x in r['consts']) and all(x['ok'] for x in r['vectors'])

    if a.json:
        print(json.dumps({'tool': '协议对照', 'schema': 1, 'ok': ok, 'layer3': r['layer3'],
                          'host_stubs': r['host_stubs'],
                          'consts': r['consts'], 'vectors': r['vectors']},
                         ensure_ascii=False))
        sys.exit(0 if ok else 1)

    print('第 1 层 · 协议常量（固件 sys_frame.h ↔ 上位机 frameparser.h）')
    for x in r['consts']:
        print('  [%s] %-12s 固件=%-4s 上位机=%-4s' % ('PASS' if x['ok'] else 'FAIL',
                                                      x['what'], x['fw'], x['host']))
    print('\n第 2 层 · 期望向量（上位机自检里写死的字节 ↔ 仿真器组帧）')
    for x in r['vectors']:
        print('  [%s] %s　%s' % ('PASS' if x['ok'] else 'FAIL', x['what'],
                                 x['detail'] if not x['ok'] else '逐字节一致'))
    print('\n第 3 层 · 真解析（仿真帧 → 上位机 FrameParser → 逐字段比）')
    if r['layer3'] == 'blocked':
        print('  [SKIP] 被 F1 阻塞：frameparser.cpp 里还有 %d 处 TODO(阶段2)（协议层是桩，返回空）。' % r['host_stubs'])
        print('         补完协议层后重跑本命令，第 3 层会自动变成真对照。')
    else:
        print('  [READY] 上位机协议层已实现，可以接真对照（下一步：编一个小 CLI 喂帧）。')

    print('\n结论：%s' % ('常量与组帧一致' if ok else '有分歧，先修上面 FAIL 的项'))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
