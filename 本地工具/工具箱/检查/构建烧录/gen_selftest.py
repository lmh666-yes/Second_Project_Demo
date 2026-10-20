# -*- coding: utf-8 -*-
r"""阶段自检代码生成（A4）：给能"机器判"的阶段生成 selftest，串口回传 STAGE<N>=PASS/FAIL。

为什么需要它：A1 判的是**静态代码**（API 有没有被调用），判不了"跑起来对不对"。
这份生成器补上另一半：把每个阶段的关键行为写成一段可执行自检，烧进去跑，
串口打印 `STAGE6=PASS` —— A2 的 `--serial` 就能据此判"这一段真的成了"。

覆盖哪些阶段（只挑**不接外设也能判**的）：
  阶段 2  时钟 168MHz + 滴答真的在走
  阶段 3  队列能建、能发、能收（FreeRTOS，调度器启动前也能跑）
  阶段 6  组帧 20 字节逐字节对 + CRC 重算一致 + Verify=0 + 改一字节必须被 CRC 拦下
  阶段 7  LoRa 模块能初始化（**要接模块**，默认关，用 --stages 7 打开）
别的阶段（传感器读数、屏上显示、联调）要人看，不生成。

生成的 `board_selftest.c/.h` 放进工程目录；**要你自己在 Keil 里 Add Existing Files**，
再在 main() 初始化完串口后调一次 `BOARD_SELFTEST_Run();`。文件头会写清这两步。

编译校验（不需要烧板子）：用 armcc 单独编译生成的 .c。头文件路径从 uvprojx 的
IncludePath + 工程里实际存在的 stm32f4xx_conf.h / RTE_Components.h 自动补齐；
**还缺哪个就现场搜出来再加 -I 重试**（最多 6 轮）—— 因为 Keil 的 RTE 路径不在
IncludePath 里，硬编码目录名（`RTE/_gpio______/`）换个 target 就失效。

用法：
    python 本地工具\工具箱\检查\构建烧录\gen_selftest.py --board 板1
    python ...\gen_selftest.py --board 板1 --stages 2,3,6
    python ...\gen_selftest.py --board 板2 --stages 2,3,4 --out D:\tmp
    python ...\gen_selftest.py --selftest

`--selftest` 退出码：0=全过；1=有失败；**3=跳过**（本机没找到 Keil → armcc 编译校验
不测、其余断言照跑）。工具箱 `--selftest` 与覆盖率看板都把 3 当"跳过"而不是失败，
所以换台没装 Keil 的机器也能过这道闸门。
"""
import argparse
import datetime
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

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
CFG_REL = os.path.join('本地工具', '工具箱', '速查数据', 'rules', 'board_envmonitor.json')

def cfg_file():
    """板级配置路径：TOOLBOX_BOARDCFG 优先（D3 的"加假工程"验收靠它），否则仓库里那份。"""
    return os.environ.get('TOOLBOX_BOARDCFG', '').strip() or cfg_file()


# 阶段 → 需要的头文件 / 是否需要 FreeRTOS / 是否要外设
STAGE_META = {
    2: {'needs': ['sys_clock.h', 'sys_tick.h'], 'rtos': False, 'hw': False,
        'what': '时钟 168MHz + 滴答在走'},
    3: {'needs': ['FreeRTOS.h', 'queue.h'], 'rtos': True, 'hw': False,
        'what': '队列能建、能发、能收'},
    4: {'needs': [], 'rtos': True, 'hw': True,
        'what': '传感器读数（要接传感器）'},
    6: {'needs': ['sys_frame.h', 'sys_modbus.h'], 'rtos': False, 'hw': False,
        'what': '组帧 20 字节 + CRC + Verify 回环'},
    7: {'needs': ['lora_e22.h'], 'rtos': False, 'hw': True,
        'what': 'LoRa 模块能初始化'},
}
DEFAULT_STAGES = [2, 3, 6]


# ========================================================================
# 工程信息
# ========================================================================

def _boards_from_cfg(root):
    import json
    with open(os.path.join(root, CFG_REL), encoding='utf-8') as f:
        return json.load(f).get('boards') or []


def pick_board(boards, want):
    if not want or want.lower() == 'all':
        return boards
    return [b for b in boards if want in b['name']]


def read_uvprojx(path):
    """取 IncludePath（第一个非空）与 Define。返回 (includes, defines)。"""
    try:
        with open(path, encoding='utf-8', errors='ignore') as f:
            s = f.read()
    except OSError:
        return [], []
    incs = []
    for m in re.finditer(r'<IncludePath>([^<]*)</IncludePath>', s):
        v = m.group(1).strip()
        if v:
            incs.append(v)
            break
    defs = []
    for m in re.finditer(r'<Define>([^<]*)</Define>', s):
        v = m.group(1).strip()
        if v:
            defs = [x.strip() for x in v.split(',') if x.strip()]
            break
    return ([x.strip() for x in incs[0].split(';') if x.strip()] if incs else []), defs


# ========================================================================
# 代码生成
# ========================================================================

HDR_TMPL = '''#ifndef BOARD_SELFTEST_H
#define BOARD_SELFTEST_H

/* 阶段自检（A4）—— 由 工具箱/检查/构建烧录/gen_selftest.py 生成，别手改。
   用法：
     1) 把 board_selftest.c / .h 加进 Keil 工程（右键分组 → Add Existing Files）
     2) main() 里串口初始化之后调一次 BOARD_SELFTEST_Run();
     3) 串口 115200 会打 STAGE<N>=PASS / FAIL，A2 的 --serial 按这个判
   下面几个开关按需要置 0/1：接不了的阶段置 0，免得一直 FAIL。 */

#define BOARD_SELFTEST_STAGE2 {s2}
#define BOARD_SELFTEST_STAGE3 {s3}
#define BOARD_SELFTEST_STAGE6 {s6}
#define BOARD_SELFTEST_STAGE7 {s7}

void BOARD_SELFTEST_Run(void);

#endif /* BOARD_SELFTEST_H */
'''


def gen_header(stages):
    return HDR_TMPL.format(s2=int(2 in stages), s3=int(3 in stages),
                           s6=int(6 in stages), s7=int(7 in stages))


def gen_source(board, stages, stamp):
    L = []
    L.append('/* ================================================================')
    L.append(' *  board_selftest.c — 阶段自检（A4）')
    L.append(' *  板子：%s' % board['name'])
    L.append(' *  生成：%s（由 gen_selftest.py 生成，手改会被覆盖）' % stamp)
    L.append(' *')
    L.append(' *  接入两步：① 本文件与 board_selftest.h 加进 Keil 工程')
    L.append(' *            （右键分组 → Add Existing Files）；')
    L.append(' *            ② main() 里串口初始化之后调一次 BOARD_SELFTEST_Run();')
    L.append(' *  串口 115200 会打印 STAGE<N>=PASS / FAIL。')
    L.append(' * ================================================================ */')
    L.append('#include "board_selftest.h"')
    L.append('')
    L.append('#include "stm32f4xx.h"')
    L.append('#include "sys_usart.h"')
    if 2 in stages:
        L.append('#include "sys_clock.h"')
        L.append('#include "sys_tick.h"')
    if 3 in stages:
        L.append('#include "FreeRTOS.h"')
        L.append('#include "queue.h"')
    if 6 in stages:
        L.append('#include "sys_frame.h"')
        # CRC 例程两板名字不同：板1 = sys_modbus.h / SYS_MODBUS_Crc16，
        # 板2 = modbus.h / MODBUS_CRC16（sys_frame.c 里注释写明了）。按板级配置取。
        L.append('#include "%s"' % ((board.get('crc_api') or {}).get('header')
                                     or 'sys_modbus.h'))
    if 7 in stages:
        L.append('#include "lora_e22.h"')
    L.append('')
    L.append('/* 自检输出用的串口：与 main.c 的调试串口保持一致 */')
    L.append('#define ST_DBG   SYS_USART_1')
    L.append('')
    if 2 in stages:
        L.append('/* ---------- 阶段 2：时钟 168MHz + 滴答在走 ---------- */')
        L.append('static uint8_t st_stage2(void)')
        L.append('{')
        L.append('    uint32_t t0;')
        L.append('')
        L.append('    if (SYS_CLK_GetFreq() != 168000000UL) {')
        L.append('        return 0U;                  /* 时钟没跑到 168MHz */')
        L.append('    }')
        L.append('    t0 = SYS_TICK_GetTick();')
        L.append('    SYS_TICK_Delay_ms(10U);')
        L.append('    if ((uint32_t)(SYS_TICK_GetTick() - t0) < 8U) {')
        L.append('        return 0U;                  /* 滴答没走（或走得远慢于 1ms） */')
        L.append('    }')
        L.append('    return 1U;')
        L.append('}')
        L.append('')
    if 3 in stages:
        L.append('/* ---------- 阶段 3：队列能建、能发、能收（调度器启动前也能跑） ---------- */')
        L.append('static uint8_t st_stage3(void)')
        L.append('{')
        L.append('    QueueHandle_t q;')
        L.append('    uint32_t v = 0x12345678UL;')
        L.append('    uint32_t got = 0UL;')
        L.append('')
        L.append('    q = xQueueCreate(2U, sizeof(uint32_t));')
        L.append('    if (q == NULL) {')
        L.append('        return 0U;                  /* 堆不够或参数非法 */')
        L.append('    }')
        L.append('    if (xQueueSend(q, &v, 0U) != pdPASS) {')
        L.append('        vQueueDelete(q);')
        L.append('        return 0U;')
        L.append('    }')
        L.append('    if (xQueueReceive(q, &got, 0U) != pdPASS) {')
        L.append('        vQueueDelete(q);')
        L.append('        return 0U;')
        L.append('    }')
        L.append('    vQueueDelete(q);')
        L.append('    return (uint8_t)(got == v);')
        L.append('}')
        L.append('')
    if 6 in stages:
        L.append('/* ---------- 阶段 6：组帧 20 字节 + CRC + Verify 回环 ---------- */')
        L.append('static uint8_t st_stage6(void)')
        L.append('{')
        L.append('    uint8_t  f[32];')
        L.append('    uint16_t n;')
        L.append('    uint16_t crc;')
        L.append('')
        L.append('    /* 25.60C / 60.30%RH / 1013.2hPa / 320lx / 15ppb / 812 */')
        L.append('    n = SYS_FRAME_BuildEnv(f, (uint16_t)sizeof(f),')
        L.append('                            2560, 6030, 10132, 320U, 15U, 812U);')
        L.append('    if (n != 20U) {')
        L.append('        return 0U;                  /* 整帧应是 12 + 8 = 20 字节 */')
        L.append('    }')
        L.append('    /* 帧头 / CMD / LEN */')
        L.append('    if (f[0] != 0xAAU || f[1] != 0x55U) return 0U;')
        L.append('    if (f[2] != 0x01U || f[3] != 0x0CU) return 0U;')
        L.append('    /* 数据段大端：0A00 178E 2794 0140 000F 032C */')
        L.append('    if (f[4]  != 0x0AU || f[5]  != 0x00U) return 0U;')
        L.append('    if (f[6]  != 0x17U || f[7]  != 0x8EU) return 0U;')
        L.append('    if (f[8]  != 0x27U || f[9]  != 0x94U) return 0U;')
        L.append('    if (f[10] != 0x01U || f[11] != 0x40U) return 0U;')
        L.append('    if (f[12] != 0x00U || f[13] != 0x0FU) return 0U;')
        L.append('    if (f[14] != 0x03U || f[15] != 0x2CU) return 0U;')
        L.append('    /* 帧尾 */')
        L.append('    if (f[18] != 0x55U || f[19] != 0xAAU) return 0U;')
        L.append('    /* CRC 覆盖 CMD..DATA 共 14 字节，低字节在前 */')
        L.append('    crc = %s(&f[2], 14U);' % ((board.get('crc_api') or {}).get('func')
                                                or 'SYS_MODBUS_Crc16'))
        L.append('    if (f[16] != (uint8_t)(crc & 0xFFU)) return 0U;')
        L.append('    if (f[17] != (uint8_t)(crc >> 8))   return 0U;')
        L.append('    /* 整帧校验必须过 */')
        L.append('    if (SYS_FRAME_Verify(f, n) != SYS_FRAME_OK) return 0U;')
        L.append('    /* 负例：改一个字节，必须被 CRC 拦下（证明校验真的在起作用） */')
        L.append('    f[4] ^= 0x01U;')
        L.append('    if (SYS_FRAME_Verify(f, n) != SYS_FRAME_ERR_CRC) return 0U;')
        L.append('    return 1U;')
        L.append('}')
        L.append('')
    if 7 in stages:
        L.append('/* ---------- 阶段 7：LoRa 模块能初始化（要接模块） ---------- */')
        L.append('static uint8_t st_stage7(void)')
        L.append('{')
        L.append('    if (LORA_E22_Init() != LORA_E22_OK) {')
        L.append('        return 0U;                  /* AUX 常忙 / 没接模块 / M0M1 错 */')
        L.append('    }')
        L.append('    return 1U;')
        L.append('}')
        L.append('')
    L.append('/* 打印一条 STAGE<N>=PASS/FAIL */')
    L.append('static void st_report(uint8_t no, uint8_t ok)')
    L.append('{')
    L.append('    SYS_USART_Printf(ST_DBG, "STAGE%u=%s\\r\\n",')
    L.append('                     (unsigned)no, ok ? "PASS" : "FAIL");')
    L.append('}')
    L.append('')
    L.append('void BOARD_SELFTEST_Run(void)')
    L.append('{')
    L.append('    SYS_USART_SendLine(ST_DBG, "");')
    L.append('    SYS_USART_SendLine(ST_DBG, "=== board selftest ===");')
    if 2 in stages:
        L.append('#if BOARD_SELFTEST_STAGE2')
        L.append('    st_report(2U, st_stage2());')
        L.append('#endif')
    if 3 in stages:
        L.append('#if BOARD_SELFTEST_STAGE3')
        L.append('    st_report(3U, st_stage3());')
        L.append('#endif')
    if 6 in stages:
        L.append('#if BOARD_SELFTEST_STAGE6')
        L.append('    st_report(6U, st_stage6());')
        L.append('#endif')
    if 7 in stages:
        L.append('#if BOARD_SELFTEST_STAGE7')
        L.append('    st_report(7U, st_stage7());')
        L.append('#endif')
    L.append('    SYS_USART_SendLine(ST_DBG, "=== selftest done ===");')
    L.append('}')
    L.append('')
    return '\n'.join(L)


# ========================================================================
# 编译校验（armcc 单独编译，缺头文件现场补 -I）
# ========================================================================

def find_armcc(keil_root):
    for c in (os.path.join(keil_root, 'ARM', 'ARMCC', 'Bin', 'armcc.exe'),
              os.path.join(keil_root, 'ARM', 'Bin', 'armcc.exe')):
        if os.path.isfile(c):
            return c
    return ''


def keil_roots():
    """从注册表 + 常见路径找 Keil 安装根（含 ARM\\ARMCC\\Bin\\armcc.exe 的那层）。"""
    out = []
    env = os.environ.get('KEIL_ROOT', '').strip()
    if env:
        out.append(env)
    try:
        import winreg
        for hive, sub in ((winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Keil\Products\MDK'),
                          (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Keil\Products\MDK')):
            try:
                with winreg.OpenKey(hive, sub) as k:
                    p, _ = winreg.QueryValueEx(k, 'Path')     # <install>\ARM
                out.append(os.path.dirname(p.rstrip('\\/')))
            except Exception:
                pass
    except Exception:
        pass
    for drv in 'CDEFG':
        for name in ('Keil_v5', 'k5', 'k5_v5'):
            out.append(os.path.join(drv + ':\\', name))
    seen, res = set(), []
    for p in out:
        n = os.path.normcase(os.path.abspath(p))
        if n in seen:
            continue
        seen.add(n)
        if find_armcc(p):
            res.append(os.path.abspath(p))
    return res


def _search_header(name, dirs):
    for d in dirs:
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    return ''


def check_compile(src, project_dir, uvprojx, keil_root, quiet=True):
    """用 armcc 单独编译 src。返回 (ok, 日志, 用到的 -I 列表)。"""
    armcc = find_armcc(keil_root)
    if not armcc:
        return False, '没找到 armcc.exe（可用环境变量 KEIL_ROOT 指定 Keil 安装根）', []
    incs, defs = read_uvprojx(uvprojx)
    args_inc = []
    for rel in incs:
        p = os.path.normpath(os.path.join(project_dir, rel))
        if os.path.isdir(p):
            args_inc.append(p)
    # 工程里实际存在的 SPL 配置头 / RTE 头所在目录（Keil 的 RTE 路径不在 IncludePath 里）
    for name in ('stm32f4xx_conf.h', 'RTE_Components.h'):
        for root, _, files in os.walk(project_dir):
            if name in files:
                args_inc.append(root)
                break
    # Keil pack 里的 CMSIS（stm32f4xx.h / core_cm4.h）
    for pat in (os.path.join(keil_root, 'ARM', 'PACK', '*', '*', 'Device', 'Include'),
                os.path.join(keil_root, 'ARM', 'PACK', '*', '*', 'CMSIS', 'Include'),
                os.path.join(keil_root, 'ARM', 'CMSIS', 'Include')):
        for d in glob.glob(pat):
            if os.path.isdir(d):
                args_inc.append(d)
    # 去重保序
    seen, inc2 = set(), []
    for d in args_inc:
        n = os.path.normcase(os.path.abspath(d))
        if n not in seen:
            seen.add(n)
            inc2.append(d)

    base = [armcc, '--c99', '-c', '--cpu', 'Cortex-M4.fp', '-O1', '--apcs=interwork']
    for d in (defs or ['USE_STDPERIPH_DRIVER', 'STM32F40_41xxx']):
        base += ['-D', d]

    log = ''
    obj = os.path.join(tempfile.gettempdir(), '_gs_check.o')
    for _ in range(6):
        cmd = list(base)
        for d in inc2:
            cmd += ['-I', d]
        cmd += [src, '-o', obj]        # 不能用 NUL：armcc 会把 NUL 当输入文件报 L6007U
        try:
            p = subprocess.run(cmd, capture_output=True, text=True,
                               encoding='utf-8', errors='replace', timeout=180)
        except (OSError, subprocess.TimeoutExpired) as e:
            return False, str(e), inc2
        log = (p.stdout or '') + (p.stderr or '')
        if p.returncode == 0:
            try:
                os.remove(obj)
            except OSError:
                pass
            return True, log, inc2
        m = re.search(r'cannot open source input file "([^"]+)"', log)
        if not m:
            return False, log, inc2
        miss = m.group(1)
        hit = _search_header(miss, [project_dir] + glob.glob(
            os.path.join(keil_root, 'ARM', 'PACK', '*', '*', '**'), recursive=True))
        if not hit:
            return False, log, inc2
        d = os.path.dirname(hit)
        if os.path.normcase(d) in {os.path.normcase(x) for x in inc2}:
            return False, log, inc2
        inc2.append(d)          # 现场补一条 -I 重试
    return False, log, inc2


# ========================================================================
# 入口
# ========================================================================

def generate_for(board, stages, out_dir, root):
    stamp = datetime.datetime.now().strftime('%Y-%m-%d %H:%M')
    os.makedirs(out_dir, exist_ok=True)
    h = os.path.join(out_dir, 'board_selftest.h')
    c = os.path.join(out_dir, 'board_selftest.c')
    with open(h, 'w', encoding='utf-8', newline='\n') as f:
        f.write(gen_header(stages))
    with open(c, 'w', encoding='utf-8', newline='\n') as f:
        f.write(gen_source(board, stages, stamp))
    return h, c


def selftest():
    ok_all = True
    skipped = False       # 本机缺 Keil → armcc 编译校验跳过；退出码 3 告诉调用方"跳过"而非"通过"
    checks = []

    def ck(name, cond, extra=''):
        nonlocal ok_all
        checks.append((name, bool(cond), str(extra)))
        if not cond:
            ok_all = False

    boards = _boards_from_cfg(ROOT)
    b1 = pick_board(boards, '板1')
    ck('读到板级配置（板1）', bool(b1), str([b['name'] for b in boards]))
    if not b1:
        print('  [FAIL] 没有板1，后面没法测')
        return False
    b1 = b1[0]

    tmp = tempfile.mkdtemp(prefix='gs_')
    try:
        h, c = generate_for(b1, DEFAULT_STAGES, tmp, ROOT)
        hdr = open(h, encoding='utf-8').read()
        src = open(c, encoding='utf-8').read()
        ck('生成 .h：三个阶段开关都在', all('BOARD_SELFTEST_STAGE%d 1' % n in hdr
                                            for n in (2, 3, 6)), hdr)
        ck('生成 .h：阶段 7 默认关（要接模块）', 'BOARD_SELFTEST_STAGE7 0' in hdr)
        ck('生成 .c：有 Run 入口', 'void BOARD_SELFTEST_Run(void)' in src)
        ck('生成 .c：阶段 2/3/6 自检都在',
           all(('st_stage%d' % n) in src for n in (2, 3, 6)))
        ck('阶段 6 自检含 CRC 负例（改一字节必须被拦）',
           'SYS_FRAME_ERR_CRC' in src and 'f[4] ^= 0x01U' in src)
        ck('生成 .c 不含未选阶段的函数', 'st_stage7' not in src)
        ck('文件头写明接入两步', 'Add Existing Files' in src and 'BOARD_SELFTEST_Run' in src)

        # 只选 6 时，不该再拖 FreeRTOS 头
        h2, c2 = generate_for(b1, [6], os.path.join(tmp, 'only6'), ROOT)
        s2 = open(c2, encoding='utf-8').read()
        ck('只选阶段 6：不包含 FreeRTOS 头', '#include "FreeRTOS.h"' not in s2)
        ck('只选阶段 6：.h 里其它开关为 0',
           'BOARD_SELFTEST_STAGE2 0' in open(h2, encoding='utf-8').read())

        # 板2 的 CRC 例程与板1 不同名（modbus.h / MODBUS_CRC16）—— 按板级配置生成
        b2 = pick_board(boards, '板2')
        if b2:
            h3, c3 = generate_for(b2[0], [6], os.path.join(tmp, 'b2'), ROOT)
            s3 = open(c3, encoding='utf-8').read()
            ck('板2 阶段 6 用 MODBUS_CRC16 / modbus.h',
               'MODBUS_CRC16' in s3 and '#include "modbus.h"' in s3, s3[:200])
        else:
            ck('板级配置里有板2', False)

        # 真编译（有 Keil 才测；没有就明说跳过，不假装通过）
        roots = keil_roots()
        if not roots:
            skipped = True
            print('  [SKIP] armcc 编译校验：本机没找到 Keil（这几条断言未测，不计入通过数）')
        else:
            proj = os.path.join(ROOT, b1['dir'])
            uv = os.path.join(proj, b1['uvprojx'])
            ok, log, incs = check_compile(c, proj, uv, roots[0])
            ck('板1 生成的代码能编译（armcc）', ok,
               log.strip().splitlines()[-3:] if not ok else '')
            ck('编译用了 ≥3 条 include 路径', len(incs) >= 3, str(len(incs)))
            if not ok:
                for line in log.strip().splitlines()[-5:]:
                    print('    ' + line)
            if b2:
                proj2 = os.path.join(ROOT, b2[0]['dir'])
                uv2 = os.path.join(proj2, b2[0]['uvprojx'])
                ok2, log2, _ = check_compile(c3, proj2, uv2, roots[0])
                ck('板2 生成的代码能编译（armcc）', ok2,
                   log2.strip().splitlines()[-3:] if not ok2 else '')
                if not ok2:
                    for line in log2.strip().splitlines()[-5:]:
                        print('    ' + line)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    for name, cond, extra in checks:
        print('  [%s] %s%s' % ('PASS' if cond else 'FAIL', name,
                               ('　' + extra) if (extra and not cond) else ''))
    print('阶段自检生成器自检：%d 通过 / %d 失败'
          % (sum(1 for _, c3, _ in checks if c3), sum(1 for _, c3, _ in checks if not c3)))
    if skipped:
        print('SKIP  本机没找到 Keil：armcc 编译校验跳过（其余断言已跑，退出码 3）')
    return ok_all, skipped


def main():
    ap = argparse.ArgumentParser(description='阶段自检代码生成（A4）')
    ap.add_argument('--board', default='板1')
    ap.add_argument('--stages', default=','.join(str(x) for x in DEFAULT_STAGES))
    ap.add_argument('--out', default='', help='输出目录（默认写进工程目录）')
    ap.add_argument('--check', action='store_true', help='生成后用 armcc 编译校验')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        ok, sk = selftest()
        # 全过但有跳过 → 退出码 3（约定见文件头）；工具箱与覆盖率看板都认这个"跳过"
        sys.exit(3 if (ok and sk) else (0 if ok else 1))

    boards = pick_board(_boards_from_cfg(ROOT), a.board)
    if not boards:
        print('没有匹配的板：%s' % a.board)
        sys.exit(1)
    stages = [int(x) for x in a.stages.split(',') if x.strip()]
    bad = [s for s in stages if s not in STAGE_META]
    if bad:
        print('不支持的阶段：%s（可选：%s）' % (bad, sorted(STAGE_META)))
        sys.exit(1)

    roots = keil_roots()
    for b in boards:
        out = a.out or os.path.join(ROOT, b['dir'])
        h, c = generate_for(b, stages, out, ROOT)
        print('生成 %s' % b['name'])
        print('  %s' % os.path.relpath(h, ROOT))
        print('  %s' % os.path.relpath(c, ROOT))
        for s in stages:
            meta = STAGE_META[s]
            tag = '（要接外设）' if meta['hw'] else ''
            print('  阶段 %d：%s%s' % (s, meta['what'], tag))
        print('  下一步：① 在 Keil 里把这两个文件加进工程；② main() 里串口初始化后调 BOARD_SELFTEST_Run();')
        if a.check:
            if not roots:
                print('  编译校验：跳过（没找到 Keil，可设 KEIL_ROOT）')
            else:
                ok, log, incs = check_compile(c, os.path.join(ROOT, b['dir']),
                                              os.path.join(ROOT, b['dir'], b['uvprojx']),
                                              roots[0])
                print('  编译校验：%s（%d 条 include）' % ('通过' if ok else '失败', len(incs)))
                if not ok:
                    for line in log.strip().splitlines()[-6:]:
                        print('    ' + line)


if __name__ == '__main__':
    main()
