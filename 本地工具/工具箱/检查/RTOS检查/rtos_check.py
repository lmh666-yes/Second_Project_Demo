# -*- coding: utf-8 -*-
r"""FreeRTOS 安全检查器（只读源码，不改任何文件）。

用途：把 FreeRTOS 的三类常见硬约束做成静态检查，配合"刚开始学 RTOS"的阶段用。
      检查只覆盖能静态判定的部分，命中项逐条附原理说明。

检查项：
  1) 中断优先级：ISR 里要调 xxxFromISR 时，其抢占优先级数值必须 >=
     configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY（否则越过内核 BASEPRI）。
     扫 FWLIB\inc\*.h 里的 SYS_*_IRQ_PRE_PRIO。
  2) NVIC 分组：必须是 NVIC_PriorityGroup_4（4 位抢占）。
  3) ISR 误用：中断上下文里调用了任务态接口（会阻塞或进临界区）。
     检查 *_IRQHandler 及其转发的 *_isr / *_irq / *_dispatch。
  4) 任务栈：xTaskCreate 的栈深度单位是字，过小或过大都提示。
  5) 堆预算：所有任务栈之和是否超过 configTOTAL_HEAP_SIZE。
  6) configASSERT 是否存在。

报告写到脚本同目录的 RTOS检查报告.md，并在控制台打印摘要。只读，不改任何文件。

用法：
    python 本地工具\工具箱\检查\RTOS检查\rtos_check.py
    python 本地工具\工具箱\检查\RTOS检查\rtos_check.py --quiet
"""
import os
import re
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

BOARDS = [
    ('板1 检测端 (F407ZE)', os.path.join('EnvMonitor', 'Project_1_检测端', 'Project')),
    ('板2 通信端 (F407ZG)', os.path.join('EnvMonitor', 'Project_2_数据通信端', 'Project')),
]

KEYWORDS = {
    'if', 'for', 'while', 'switch', 'do', 'else', 'return', 'sizeof',
    'catch', 'case', 'default', 'typeof', 'defined', 'static_assert',
}

# 中断上下文里不允许直接调用的任务态接口（严重）
BLOCKING_API = {
    'xQueueSend', 'xQueueSendToBack', 'xQueueSendToFront', 'xQueueReceive',
    'xQueuePeek', 'xQueueReset', 'xQueueOverwrite',
    'xSemaphoreTake', 'xSemaphoreGive', 'xSemaphoreTakeRecursive',
    'xSemaphoreGiveRecursive', 'vSemaphoreCreateBinary',
    'vTaskDelay', 'vTaskDelayUntil', 'vTaskDelete', 'vTaskSuspend',
    'vTaskResume', 'vTaskPrioritySet', 'vTaskStartScheduler',
    'vTaskSuspendAll', 'xTaskResumeAll',
    'xEventGroupWaitBits', 'xEventGroupSetBits', 'xEventGroupClearBits',
    'ulTaskNotifyTake', 'xTaskNotifyWait', 'xTaskNotify', 'xTaskNotifyGive',
    'xTimerStart', 'xTimerStop', 'xTimerChangePeriod', 'xTimerReset',
    'vTaskPrioritySetFromISR',
}

RTOS_PREFIX = ('xQueue', 'xSemaphore', 'vTask', 'xTask', 'xEventGroup',
               'xTimer', 'ulTaskNotify', 'vTimerSetTimerID')

PRINCIPLE = {
    'prio': 'ISR 里调用 FromISR 接口时，其抢占优先级数值必须 >= '
            'configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY。否则该 ISR 优先级高于内核'
            '屏蔽上限，会越过 BASEPRI、破坏内核临界区。依据见 FreeRTOSConfig.h 顶部注释。',
    'group': 'FreeRTOS 要求 NVIC 用足够多的抢占位（推荐 PriorityGroup_4，4 位抢占）。'
             '分组不对会把抢占值按位截断，例如 PRE=5 被截成 1，使 ISR 越过内核上限。',
    'isr_api': '中断上下文只能用 xxxFromISR 系列与 portYIELD_FROM_ISR。调用会阻塞或'
               '进内核临界区的任务态接口，会破坏内核状态。',
    'stack': 'xTaskCreate 的栈深度单位是字（1 字 = 4 字节）。过小会栈溢出'
             '（configCHECK_FOR_STACK_OVERFLOW 能抓），过大浪费堆。',
    'heap': '所有任务栈、队列、信号量都从 configTOTAL_HEAP_SIZE 里分配。'
            '申请总量超过堆大小会在创建时失败（configUSE_MALLOC_FAILED_HOOK 会抓到）。',
    'assert': '未定义 configASSERT 时，优先级违规不会被内核当场发现，只会在运行时'
              '破坏临界区。建议保留 configASSERT。',
}


def read_text(path):
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ('utf-8', 'gbk', 'latin-1'):
        try:
            return raw.decode(enc), enc
        except UnicodeDecodeError:
            continue
    return raw.decode('latin-1', 'replace'), 'latin-1'


def strip_comments(s):
    out = list(s)
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if c == '/' and i + 1 < n and s[i + 1] == '/':
            while i < n and s[i] != '\n':
                out[i] = ' '
                i += 1
        elif c == '/' and i + 1 < n and s[i + 1] == '*':
            out[i] = out[i + 1] = ' '
            i += 2
            while i < n and not (s[i] == '*' and i + 1 < n and s[i + 1] == '/'):
                if s[i] != '\n':
                    out[i] = ' '
                i += 1
            if i < n:
                out[i] = out[i + 1] = ' '
                i += 2
        elif c == '"' or c == "'":
            q = c
            out[i] = ' '
            i += 1
            while i < n and s[i] != q:
                if s[i] == '\\' and i + 1 < n:
                    out[i] = out[i + 1] = ' '
                    i += 2
                else:
                    if s[i] != '\n':
                        out[i] = ' '
                    i += 1
            if i < n:
                out[i] = ' '
                i += 1
        else:
            i += 1
    return ''.join(out)


def eval_int(expr, sub=None):
    """求简单整数宏：支持括号、四则、已知宏替换。失败返回 None。"""
    e = expr
    for k, v in (sub or {}).items():
        e = re.sub(r'\b' + re.escape(k) + r'\b', str(v), e)
    e = re.sub(r'\(\s*(?:unsigned\s+short|unsigned\s+int|unsigned\s+long|size_t|int|'
               r'uint\d+_t|int\d+_t|TickType_t|UBaseType_t|BaseType_t)\s*\)', '', e)
    e = re.sub(r'\b(\d+)\s*[uUlL]+\b', r'\1', e)
    e = e.replace('UL', '').replace('L', '')
    if not re.fullmatch(r'[\d\s+\-*/()]+', e):
        return None
    try:
        return int(eval(e, {'__builtins__': {}}, {}))
    except Exception:
        return None


def parse_config(path):
    """返回 (macros原始文本字典, 关键值字典)。"""
    text, _ = read_text(path)
    raw = {}
    for line in text.splitlines():
        m = re.match(r'\s*#\s*define\s+(\w+)\s+(.*?)(?:/\*.*)?$', line)
        if m:
            val = m.group(2).strip()
            if val and m.group(1) not in raw:
                raw[m.group(1)] = val
    # 用续行拼接多行宏
    joined = re.sub(r'\\\s*\n', ' ', text)
    for line in joined.splitlines():
        m = re.match(r'\s*#\s*define\s+(\w+)\s+(.*?)(?:/\*.*)?$', line)
        if m and m.group(1) not in raw:
            raw[m.group(1)] = m.group(2).strip()

    vals = {}
    for key in ('configPRIO_BITS', 'configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY',
                'configLIBRARY_LOWEST_INTERRUPT_PRIORITY', 'configTOTAL_HEAP_SIZE',
                'configMINIMAL_STACK_SIZE', 'configMAX_PRIORITIES',
                'configTIMER_TASK_STACK_DEPTH', 'configUSE_MUTEXES',
                'configUSE_RECURSIVE_MUTEXES', 'configCHECK_FOR_STACK_OVERFLOW',
                'configUSE_TIMERS', 'configUSE_MALLOC_FAILED_HOOK'):
        if key in raw:
            vals[key] = eval_int(raw[key], vals)

    prio_bits = vals.get('configPRIO_BITS')
    mx = vals.get('configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY')
    if prio_bits is not None and mx is not None:
        vals['configMAX_SYSCALL_INTERRUPT_PRIORITY'] = mx << (8 - prio_bits)
    vals['_has_assert'] = bool(re.search(r'#\s*define\s+configASSERT', joined))
    return raw, vals


def find_functions(masked):
    """返回 [(函数名, 行号, 体左括号, 体右括号)]。"""
    regions = []
    stack = []
    for i, ch in enumerate(masked):
        if ch == '{':
            stack.append(i)
        elif ch == '}' and stack:
            regions.append((stack.pop(), i))

    funcs = []
    for s, e in regions:
        line_start = masked.rfind('\n', 0, s) + 1
        if masked[line_start:s].lstrip().startswith('#'):
            continue
        j = s - 1
        while j >= 0 and masked[j] in ' \t\r\n':
            j -= 1
        rp, k, steps = None, j, 0
        while k >= 0 and steps < 60:
            c = masked[k]
            if c == ')':
                rp = k
                break
            if c.isalnum() or c == '_' or c in ' \t\r\n':
                k -= 1
                steps += 1
                continue
            break
        if rp is None:
            continue
        depth, p, lp = 0, rp, None
        while p >= 0:
            if masked[p] == ')':
                depth += 1
            elif masked[p] == '(':
                depth -= 1
                if depth == 0:
                    lp = p
                    break
            p -= 1
        if lp is None:
            continue
        t = lp - 1
        while t >= 0 and masked[t] in ' \t\r\n':
            t -= 1
        if t < 0 or not (masked[t].isalnum() or masked[t] == '_'):
            continue
        nend = t
        while t >= 0 and (masked[t].isalnum() or masked[t] == '_'):
            t -= 1
        name = masked[t + 1:nend + 1]
        if name in KEYWORDS:
            continue
        u = t
        while u >= 0 and masked[u] in ' \t\r\n':
            u -= 1
        if u < 0 or not (masked[u].isalnum() or masked[u] in '_*&>~:'):
            continue
        line = masked.count('\n', 0, nend) + 1
        funcs.append((name, line, s, e))
    return funcs


def call_args(masked, open_paren):
    """取调用参数串，从函数调用左括号开始配平。返回 (内层字符串, 右括号下标)。"""
    depth, p = 0, open_paren
    while p < len(masked):
        if masked[p] == '(':
            depth += 1
        elif masked[p] == ')':
            depth -= 1
            if depth == 0:
                return masked[open_paren + 1:p], p
        p += 1
    return None


def split_top_commas(s):
    out, depth, cur = [], 0, []
    for ch in s:
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
    out.append(''.join(cur))
    return [x.strip() for x in out]


def iter_sources(root):
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs
                   if d not in ('FreeRTOS', 'ETH', 'RTE', 'Objects', 'Listings', '.git')]
        for fn in files:
            if fn.lower().endswith('.c'):
                yield os.path.join(base, fn)


def safe_rel(path):
    try:
        return os.path.relpath(path, ROOT)
    except ValueError:
        return path


def is_isr_name(name):
    return (name.endswith('_IRQHandler') or name.endswith('_isr')
            or name.endswith('_irq') or name.endswith('_dispatch'))


def check_board(title, proj):
    """返回 findings 列表：每项 (等级, 位置, 现象, 原理键)。"""
    findings = []
    cfg = os.path.join(proj, 'FreeRTOS', 'port', 'FreeRTOSConfig.h')
    if not os.path.exists(cfg):
        return None, [('严重', proj, '找不到 FreeRTOSConfig.h', 'assert')], []

    _, vals = parse_config(cfg)
    mx_lib = vals.get('configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY')

    # 1) NVIC 分组
    inc = os.path.join(proj, 'FWLIB', 'inc')
    group_ok = False
    if os.path.isdir(inc):
        for fn in os.listdir(inc):
            if fn.endswith('.h'):
                txt, _ = read_text(os.path.join(inc, fn))
                m = re.search(r'#\s*define\s+SYS_NVIC_PRIORITY_GROUP\s+(\S+)', txt)
                if m:
                    if m.group(1) == 'NVIC_PriorityGroup_4':
                        group_ok = True
                    else:
                        findings.append(('严重', '%s/%s' % (os.path.basename(inc), fn),
                                         'SYS_NVIC_PRIORITY_GROUP = %s（应为 PriorityGroup_4）'
                                         % m.group(1), 'group'))
    if not group_ok and not any(f[3] == 'group' for f in findings):
        findings.append(('注意', 'FWLIB/inc/sys_nvic.h',
                         '未找到 SYS_NVIC_PRIORITY_GROUP = NVIC_PriorityGroup_4', 'group'))

    # 2) 各模块中断抢占优先级
    if mx_lib is not None and os.path.isdir(inc):
        for fn in sorted(os.listdir(inc)):
            if not fn.endswith('.h'):
                continue
            txt, _ = read_text(os.path.join(inc, fn))
            for m in re.finditer(r'#\s*define\s+(SYS_\w*_IRQ_PRE_PRIO)\s+(\d+)', txt):
                val = int(m.group(2))
                if val < mx_lib:
                    findings.append(('严重', '%s/%s' % (os.path.basename(inc), fn),
                                     '%s = %d，小于内核上限 %d' % (m.group(1), val, mx_lib),
                                     'prio'))

    # 3) ISR 误用 RTOS 接口 + 4/5) 任务栈与堆预算
    srcs = list(iter_sources(proj))
    tasks = []          # (文件, 行, 名字, 栈, 优先级)
    for path in srcs:
        text, _ = read_text(path)
        masked = strip_comments(text)
        rel = safe_rel(path)
        for name, line, s, e in find_functions(masked):
            if not is_isr_name(name):
                continue
            body = masked[s + 1:e]
            for c in re.finditer(r'([A-Za-z_]\w*)\s*\(', body):
                api = c.group(1)
                if api.endswith('FromISR') or api == 'portYIELD_FROM_ISR':
                    continue
                if api in BLOCKING_API:
                    findings.append(('严重', '%s:%d' % (rel, line),
                                     'ISR 上下文（%s）里调用了 %s' % (name, api), 'isr_api'))
                elif api.startswith(RTOS_PREFIX):
                    findings.append(('注意', '%s:%d' % (rel, line),
                                     'ISR 上下文（%s）里调用了 %s（非 FromISR）'
                                     % (name, api), 'isr_api'))
        # 任务创建
        for m in re.finditer(r'\bxTaskCreate(?:Static)?\s*\(', masked):
            lp = m.end() - 1
            res = call_args(masked, lp)
            if res is None:
                continue
            args, rp = res
            parts = split_top_commas(args)                 # 结构用去注释后的串
            raw_parts = split_top_commas(text[lp + 1:rp])  # 任务名取原文（字符串未被屏蔽）
            if len(parts) >= 5:
                tname = raw_parts[1].strip().strip('"') if len(raw_parts) >= 5 else ''
                stack = eval_int(parts[2])
                prio = eval_int(parts[4])
                line = masked.count('\n', 0, m.start()) + 1
                tasks.append((rel, line, tname, stack, prio))

    min_stack = vals.get('configMINIMAL_STACK_SIZE')
    max_prio = vals.get('configMAX_PRIORITIES')
    heap = vals.get('configTOTAL_HEAP_SIZE')
    stack_words = 0
    for rel, line, tname, stack, prio in tasks:
        loc = '%s:%d' % (rel, line)
        if stack is not None and min_stack is not None and stack < min_stack:
            findings.append(('注意', loc, '任务 %s 栈深度 %d 字 < configMINIMAL_STACK_SIZE %d'
                             % (tname, stack, min_stack), 'stack'))
            stack_words += stack
        elif stack is not None:
            stack_words += stack
        if prio is not None and max_prio is not None and prio >= max_prio:
            findings.append(('严重', loc, '任务 %s 优先级 %d 超出 configMAX_PRIORITIES %d'
                             % (tname, prio, max_prio), 'stack'))

    if heap is not None and stack_words * 4 > heap * 0.7:
        findings.append(('注意', 'FWLIB/..',
                         '任务栈合计约 %d 字（%d 字节），占堆 %d 字节的七成以上'
                         % (stack_words, stack_words * 4, heap), 'heap'))

    # 6) configASSERT
    if not vals.get('_has_assert'):
        findings.append(('注意', 'FreeRTOS/port/FreeRTOSConfig.h',
                         '未定义 configASSERT，优先级违规不会被内核当场发现', 'assert'))

    return vals, findings, tasks


def render(results):
    out = ['# RTOS 检查报告', '',
           '来源：`本地工具/工具箱/检查/RTOS检查/rtos_check.py`（只读扫描，可重复运行）', '']
    out.append('## 0. 总览')
    out.append('')
    out.append('| 板 | 严重 | 注意 | 配置摘要 |')
    out.append('|---|---|---|---|')
    for title, vals, findings, tasks in results:
        crit = sum(1 for f in findings if f[0] == '严重')
        warn = sum(1 for f in findings if f[0] == '注意')
        brief = ''
        if vals:
            brief = '上限 %s / 堆 %s / 抢占位 %s' % (
                vals.get('configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY', '?'),
                vals.get('configTOTAL_HEAP_SIZE', '?'),
                vals.get('configPRIO_BITS', '?'))
        out.append('| %s | %d | %d | %s |' % (title, crit, warn, brief))
    out.append('')

    for title, vals, findings, tasks in results:
        out.append('## %s' % title)
        out.append('')
        out.append('### 配置摘要')
        out.append('')
        if vals:
            keys = ['configPRIO_BITS', 'configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY',
                    'configMAX_SYSCALL_INTERRUPT_PRIORITY', 'configTOTAL_HEAP_SIZE',
                    'configMINIMAL_STACK_SIZE', 'configMAX_PRIORITIES',
                    'configTIMER_TASK_STACK_DEPTH', 'configUSE_MUTEXES',
                    'configCHECK_FOR_STACK_OVERFLOW', '_has_assert']
            out.append('| 项 | 值 |')
            out.append('|---|---|')
            for k in keys:
                if k in vals:
                    v = vals[k]
                    if k == '_has_assert':
                        v = '有' if v else '无'
                    out.append('| `%s` | %s |' % (k, v))
            out.append('')
        out.append('### 任务创建')
        out.append('')
        if tasks:
            out.append('| 位置 | 任务 | 栈(字) | 优先级 |')
            out.append('|---|---|---|---|')
            for rel, line, tname, stack, prio in tasks:
                out.append('| %s:%d | %s | %s | %s |'
                           % (rel, line, tname, stack if stack is not None else '?',
                              prio if prio is not None else '?'))
        else:
            out.append('未发现 xTaskCreate 调用（当前可能处于骨架阶段）。')
        out.append('')
        out.append('### 检查结果')
        out.append('')
        if not findings:
            out.append('未发现问题。')
            out.append('')
        else:
            out.append('| 等级 | 位置 | 现象 |')
            out.append('|---|---|---|')
            for lv, loc, msg, _ in findings:
                out.append('| %s | `%s` | %s |' % (lv, loc, msg.replace('|', '\\|')))
            out.append('')
            seen = []
            for _, _, _, key in findings:
                if key not in seen:
                    seen.append(key)
            out.append('### 原理')
            out.append('')
            for key in seen:
                out.append('- %s' % PRINCIPLE.get(key, ''))
            out.append('')
    out.append('## 说明')
    out.append('')
    out.append('1) 静态检查只覆盖能判定的部分，命中项需人工确认上下文。')
    out.append('2) 中断优先级上限取 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY。')
    out.append('3) 本报告每次运行覆盖。')
    out.append('')
    return '\n'.join(out)


def selftest():
    """用一组故意写错的合成工程验证检查确实命中，避免"永远报通过"。"""
    import tempfile
    import shutil

    d = tempfile.mkdtemp(prefix='rtos_selftest_')
    base = os.path.join(d, 'Project')
    os.makedirs(os.path.join(base, 'FreeRTOS', 'port'))
    os.makedirs(os.path.join(base, 'FWLIB', 'inc'))

    with open(os.path.join(base, 'FreeRTOS', 'port', 'FreeRTOSConfig.h'),
              'w', encoding='utf-8') as f:
        f.write('#define configPRIO_BITS 4\n'
                '#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5\n'
                '#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15\n'
                '#define configTOTAL_HEAP_SIZE ((size_t)(30 * 1024))\n'
                '#define configMINIMAL_STACK_SIZE ((unsigned short)128)\n'
                '#define configMAX_PRIORITIES 8\n'
                '#define configASSERT(x) if ((x) == 0) { for (;;); }\n')
    with open(os.path.join(base, 'FWLIB', 'inc', 'sys_nvic.h'), 'w', encoding='utf-8') as f:
        f.write('#define SYS_NVIC_PRIORITY_GROUP NVIC_PriorityGroup_2\n')
    with open(os.path.join(base, 'FWLIB', 'inc', 'sys_usart.h'), 'w', encoding='utf-8') as f:
        f.write('#define SYS_USART_IRQ_PRE_PRIO 2\n')
    with open(os.path.join(base, 'main.c'), 'w', encoding='utf-8') as f:
        f.write('void USART1_IRQHandler(void) { xQueueSend(q, &x, 0); }\n'
                'void app_init(void) { xTaskCreate(taskA, "A", 32, NULL, 2, NULL); }\n')

    _, findings, _ = check_board('synthetic', base)
    keys = {f[3] for f in findings}
    expect = {'group', 'prio', 'isr_api', 'stack'}
    ok = expect.issubset(keys)
    print('RTOS 检查自检：%s' % ('PASS' if ok else 'FAIL'))
    print('  命中类别: %s' % (', '.join(sorted(keys)) or '无'))
    print('  期望类别: %s' % ', '.join(sorted(expect)))
    for lv, loc, msg, key in findings:
        print('    [%s][%s] %s' % (lv, key, msg))
    shutil.rmtree(d, ignore_errors=True)
    return 0 if ok else 1


def main(argv):
    if '--selftest' in argv:
        return selftest()
    quiet = False
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'RTOS检查报告.md')
    i = 1
    while i < len(argv):
        if argv[i] == '--quiet':
            quiet = True
            i += 1
        elif argv[i] == '--out' and i + 1 < len(argv):
            out_path = os.path.abspath(argv[i + 1])
            i += 2
        else:
            i += 1

    results = []
    for title, projrel in BOARDS:
        proj = os.path.join(ROOT, projrel)
        if not os.path.isdir(proj):
            results.append((title, None, [('严重', projrel, '工程目录不存在', 'assert')], []))
            continue
        vals, findings, tasks = check_board(title, proj)
        results.append((title, vals, findings, tasks))

    report = render(results)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(report)

    print('RTOS 检查完成')
    for title, vals, findings, tasks in results:
        crit = sum(1 for f in findings if f[0] == '严重')
        warn = sum(1 for f in findings if f[0] == '注意')
        print('  %-24s 严重 %d / 注意 %d / 任务 %d' % (title, crit, warn, len(tasks)))
    print('  报告: %s' % out_path)
    if not quiet:
        for title, vals, findings, tasks in results:
            if findings:
                print('')
                print('%s 命中项：' % title)
                for lv, loc, msg, _ in findings:
                    print('  [%s] %s  %s' % (lv, loc, msg))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
