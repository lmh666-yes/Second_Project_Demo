# -*- coding: utf-8 -*-
r"""FreeRTOS 安全检查器（只读源码，不改任何文件）。

用途：把 FreeRTOS 的三类常见硬约束做成静态检查，配合"刚开始学 RTOS"的阶段用。
      检查只覆盖能静态判定的部分，命中项逐条附原理说明。

检查项：
  1) 中断优先级（按实际调用判定）：调了 xxxFromISR 的 ISR，其抢占优先级数值必须 >=
     configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY，否则越过内核 BASEPRI。
     做法是把 *_IRQHandler 对回 IRQn，取 FWLIB\src 里真正配下去的那个值；
     没调 FromISR 的中断不受这条约束。FWLIB\inc 里的 SYS_*_IRQ_PRE_PRIO 声明低于上限
     只给"注意"级提示（判不出它会不会被用于 FromISR，不该直接判严重）。
  2) NVIC 分组：必须是 NVIC_PriorityGroup_4（4 位抢占）。
  3) ISR 误用：中断上下文里调用了任务态接口（会阻塞或进临界区）。
     从 *_IRQHandler / *_isr / *_irq / *_dispatch 出发按调用关系往下追：
     途中遇到包装宏先展开一层（如 `#define SEND(q,x) xQueueSend((q),(x),0)`），
     遇到别的函数再往下最多三层。因此 "ISR -> App_ExtiNotify() -> xQueueSend"
     这类隔着转发、隔着宏的误用也能报出来，并写明是经谁转发过来的。
     ISR 里进内核临界区（taskENTER_CRITICAL / portENTER_CRITICAL 等）也算这一类，判严重。
     反例同样守住：任务态函数里调同名接口不报，ISR 里调 xxxFromISR 不报。
  4) 中断里写、任务里读的共享变量缺 volatile：只看文件作用域声明的变量
     （局部变量不可能被 ISR 与任务共享），命中给"注意"级。
  5) 任务栈：xTaskCreate 的栈深度单位是字，过小或过大都提示。
  6) 堆预算：所有任务栈之和是否超过 configTOTAL_HEAP_SIZE。
  7) configASSERT 是否存在。
  8) 看门狗 vs Flash 擦写：工程里既做长耗时擦写（SYS_FLASH_EraseSector / SaveParams /
     Write），又把看门狗超时设得偏短（SYS_WDG_Init(<8000ms)），或超时实参判不出时，
     给"注意"级提示——擦除期间 CPU 取指被硬件挂起、SysTick 与串口都停，喂不了狗。
     FWLIB 里 sys_flash.c / sys_wdg.c 的"实现"不算"工程在用"，只看工程自己的调用。

报告写到脚本同目录的 RTOS检查报告.md，并在控制台打印摘要。只读，不改任何文件。

用法：
    python 本地工具\工具箱\检查\RTOS检查\rtos_check.py
    python 本地工具\工具箱\检查\RTOS检查\rtos_check.py --quiet
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
}

RTOS_PREFIX = ('xQueue', 'xSemaphore', 'vTask', 'xTask', 'xEventGroup',
               'xTimer', 'ulTaskNotify', 'vTimerSetTimerID')

# 中断上下文里不能进的内核临界区：ISR 要用 taskENTER_CRITICAL_FROM_ISR /
# portSET_INTERRUPT_MASK_FROM_ISR。taskENTER_CRITICAL 不但关中断，还会碰内核状态。
ISR_UNSAFE_CRITICAL = {
    'taskENTER_CRITICAL', 'taskEXIT_CRITICAL',
    'portENTER_CRITICAL', 'portEXIT_CRITICAL',
}

# 看门狗 vs Flash 擦写：擦除/写页期间 CPU 取指被硬件挂起（Flash 被占用），
# SysTick 与串口都停，这段时间喂不了狗；IWDG 一旦启动又无法停。
# 所以"看门狗超时 < 8s + 工程里还做长擦写"要当心。阈值取 8s：
# STM32F4 擦一个 64KB 扇区约 1s 量级，留够余量（擦 + 写 + 校验）。
WDG_SAFE_MS = 8000

# B3：中断里干"又慢又不可重入"的活 —— printf 家族与堆分配。
# printf 会一路走到串口阻塞（中断里等 TXE 等于丢掉实时性），堆分配在 FreeRTOS 里
# 要挂起调度器才安全，ISR 里直接调会破坏堆结构。
ISR_REENTRANT_RISK = {
    'printf', 'sprintf', 'snprintf', 'vsnprintf', 'vprintf', 'fprintf',
    'puts', 'putchar', 'fputc',
    'malloc', 'calloc', 'realloc', 'free', 'pvPortMalloc', 'vPortFree',
}

# B3：中断里的忙等延时（时长由参数决定，运行时关不掉）。
# 这里按名字判：delay_* 系列与 HAL_Delay；新版项目若改名，改这一处即可。
ISR_BUSY_DELAY_PREFIX = ('delay_', 'Delay_', 'SYS_DELAY_', 'SYS_Delay')
ISR_BUSY_DELAY = {'HAL_Delay', 'delay_ms_dwt'}

# B3：中断里用浮点 —— Cortex-M4 的 ISR 默认不保存 FPU 寄存器（惰性压栈），
# 在 ISR 里做浮点运算要额外确认 FPU 上下文；改用定点最省心。
RE_FLOAT_USE = re.compile(r'\b(?:float|double)\b|\d+\.\d+[fF]?')

# 工程里会做长耗时 Flash 擦写的接口（定义在 FWLIB/sys_flash.c，这里只认"调用"）
FLASH_LONG_OPS = ('SYS_FLASH_EraseSector', 'SYS_FLASH_ErasePage',
                  'SYS_FLASH_SaveParams', 'SYS_FLASH_Write')

# 规则表（数据模型）外置在 工具箱/速查数据/rules/，检查器与其它工具共读一份：
# 加一个新的 RTOS 接口只要改表，不用动脚本。表缺失/损坏时回退到上面那三份内置默认值，
# 行为与老版本完全一致（所以"表没了"不会让检查静默失效，只会退化成内置规则）。
RULES_PATH = os.path.abspath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..', '速查数据', 'rules',
    'freertos_isr_rules.json'))
RULES_SOURCE = '内置默认'


def load_rules():
    """读外置规则表并覆盖内置默认值。返回是否成功（失败时保持内置默认，不抛异常）。"""
    global BLOCKING_API, RTOS_PREFIX, ISR_UNSAFE_CRITICAL, RULES_SOURCE
    try:
        with open(RULES_PATH, 'r', encoding='utf-8') as f:
            d = json.load(f)
    except Exception:
        return False
    if not isinstance(d, dict) or not d.get('blocking_api'):
        return False
    BLOCKING_API = set(d['blocking_api'])
    ISR_UNSAFE_CRITICAL = set(d.get('isr_unsafe_critical') or ISR_UNSAFE_CRITICAL)
    RTOS_PREFIX = tuple(d.get('rtos_prefix') or RTOS_PREFIX)
    try:
        RULES_SOURCE = os.path.relpath(RULES_PATH, ROOT).replace('\\', '/')
    except Exception:
        RULES_SOURCE = RULES_PATH
    return True


load_rules()


PRINCIPLE = {
    'prio': 'ISR 里调用 FromISR 接口时，其抢占优先级数值必须 >= '
            'configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY。否则该 ISR 优先级高于内核'
            '屏蔽上限，会越过 BASEPRI、破坏内核临界区。依据见 FreeRTOSConfig.h 顶部注释。',
    'group': 'FreeRTOS 要求 NVIC 用足够多的抢占位（推荐 PriorityGroup_4，4 位抢占）。'
             '分组不对会把抢占值按位截断，例如 PRE=5 被截成 1，使 ISR 越过内核上限。',
    'isr_api': '中断上下文只能用 xxxFromISR 系列与 portYIELD_FROM_ISR。调用会阻塞或'
               '进内核临界区的任务态接口，会破坏内核状态。',
    'critical': 'ISR 里不能进内核临界区。taskENTER_CRITICAL / portENTER_CRITICAL 会关中断'
                '并操作内核状态，中断上下文要用 taskENTER_CRITICAL_FROM_ISR /'
                ' portSET_INTERRUPT_MASK_FROM_ISR，拿到返回值后按序恢复。',
    'volatile': '中断里写、任务里读的变量必须加 volatile。否则编译器会把该变量缓存在寄存器，'
                '任务侧读到的是陈旧值（表现为"标志位怎么都不变"）。'
                '写法：`static volatile uint8_t g_flag;`。只读不写、或只在 ISR 里用，'
                '不需要 volatile。',
    'stack': 'xTaskCreate 的栈深度单位是字（1 字 = 4 字节）。过小会栈溢出'
             '（configCHECK_FOR_STACK_OVERFLOW 能抓），过大浪费堆。',
    'heap': '所有任务栈、队列、信号量都从 configTOTAL_HEAP_SIZE 里分配。'
            '申请总量超过堆大小会在创建时失败（configUSE_MALLOC_FAILED_HOOK 会抓到）。',
    'assert': '未定义 configASSERT 时，优先级违规不会被内核当场发现，只会在运行时'
              '破坏临界区。建议保留 configASSERT。',
    'wdg_flash': '擦除/写 Flash 期间，CPU 取指被硬件挂起，SysTick 与串口都会停 —— '
                 '这段时间喂不了狗；而 IWDG 一旦启动就无法停止。所以看门狗超时较短'
                 '（< 8s）又做长擦写时，务必在擦除前后各 SYS_WDG_Feed() 一次，'
                 '或把超时放到 8s 以上（IWDG 最大约 32.8s）。'
                 '探针判"没把握喂狗"只是提示，人工确认擦除路径上有喂狗即可忽略。',
    'isr_cost': 'ISR 里别做长耗时或不可重入的活：printf 会一路阻塞到串口把整串发完，'
                '堆分配（malloc / pvPortMalloc）在 FreeRTOS 下要先挂起调度器才安全；'
                '忙等延时（delay_*）的时长由参数决定、运行时关不掉。'
                '正确做法是在 ISR 里只置标志 / 投消息，把打印与分配放到任务里做。',
    'isr_float': 'Cortex-M4 的 ISR 默认不保存 FPU 寄存器（靠惰性压栈），ISR 里做浮点运算'
                 '要额外确认：要么改成定点（本项目线上传定点整数就是为这个），'
                 '要么明确 portTASK_USES_FLOATING_POINT 与 FPU 上下文的取舍。',
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


def collect_macros(text):
    """收集 `#define NAME(...) 体` 的宏体（先拼续行）。用于把 ISR 里调用的包装宏展开一层：
    固件常见 `#define SEND(q, x) xQueueSend((q), (x), 0)` 这种写法，只看调用名会漏掉背后的接口。"""
    joined = re.sub(r'\\\s*\n', ' ', text)
    out = {}
    for m in re.finditer(r'#\s*define\s+([A-Za-z_]\w*)\s*\(([^)]*)\)[ \t]*(.*)$', joined, re.M):
        out.setdefault(m.group(1), m.group(3).strip())
    return out


def scan_calls(masked):
    """返回体里出现的调用名（标识符紧跟左括号），控制语句与已知关键字滤掉。"""
    return [m.group(1) for m in re.finditer(r'([A-Za-z_]\w*)\s*\(', masked)
            if m.group(1) not in KEYWORDS]


def risky_api(api):
    """任务态接口判级：会阻塞/进内核临界区的判"严重"，其余 RTOS 前缀接口判"注意"。
    xxxFromISR 系列与 portYIELD_FROM_ISR 是中断安全的，返回 None。"""
    if api.endswith('FromISR') or api == 'portYIELD_FROM_ISR':
        return None
    if api in ISR_UNSAFE_CRITICAL or api in BLOCKING_API:
        return '严重'
    if api.startswith(RTOS_PREFIX):
        return '注意'
    return None


def isr_costly_call(api):
    """B3：中断上下文里"长耗时 / 不可重入"的调用。返回 (等级, 原因) 或 (None, '')。"""
    if api in ISR_REENTRANT_RISK:
        return '严重', ('它在 ISR 里既慢又不可重入 —— printf 会一路阻塞到串口发完，'
                        '堆分配在 FreeRTOS 下要先挂起调度器才安全')
    if api in ISR_BUSY_DELAY or api.startswith(ISR_BUSY_DELAY_PREFIX):
        return '注意', '它是忙等延时，时长由参数决定、运行时关不掉，会把中断响应拖长'
    return None, ''


RE_IRQ_SET = re.compile(r'SYS_NVIC_SetPriority\s*\(\s*([A-Za-z_]\w*)\s*,\s*'
                        r'([A-Za-z_]\w*|\d+)\s*,\s*([A-Za-z_]\w*|\d+)\s*\)')
RE_IRQ_CH = re.compile(r'NVIC_IRQChannel\s*=\s*([A-Za-z_]\w*)')
RE_IRQ_PRE = re.compile(r'NVIC_IRQChannelPreemptionPriority\s*=\s*([A-Za-z_]\w*|\d+)')


def read_irq_prios(proj):
    """扫 FWLIB 的 NVIC 配置，返回 {IRQn: (抢占值, 出处)}。

    覆盖 SYS_NVIC_SetPriority(...) 与 NVIC_InitStructure 两种写法；IRQn 或优先级
    由形参传入的写法静态解析不了，跳过不猜。目的一：知道某个 ISR 实际拿到多少抢占值，
    才能判断"它调 FromISR 会不会越过内核屏蔽"，光看宏声明是判不出来的。
    """
    inc = os.path.join(proj, 'FWLIB', 'inc')
    src = os.path.join(proj, 'FWLIB', 'src')
    macros = {}
    if os.path.isdir(inc):
        for fn in sorted(os.listdir(inc)):
            if not fn.endswith('.h'):
                continue
            txt, _ = read_text(os.path.join(inc, fn))
            for m in re.finditer(r'#\s*define\s+(SYS_\w*_(?:PRE|SUB)_PRIO)\s+(\d+)', txt):
                macros[m.group(1)] = int(m.group(2))

    def tok(v):
        return int(v) if v.isdigit() else macros.get(v)

    out = {}
    if not os.path.isdir(src):
        return out
    for fn in sorted(os.listdir(src)):
        if not fn.endswith('.c'):
            continue
        text, _ = read_text(os.path.join(src, fn))
        lines = text.splitlines()
        rp = os.path.join('FWLIB', 'src', fn)
        for i, line in enumerate(lines, 1):
            m = RE_IRQ_SET.search(line)
            if m:
                if not m.group(1).endswith('IRQn') or m.group(1) in out:
                    continue
                pre = tok(m.group(2))
                if pre is not None:
                    out[m.group(1)] = (pre, '%s:%d' % (rp, i))
                continue
            m = RE_IRQ_CH.search(line)
            if m and m.group(1).endswith('IRQn') and m.group(1) not in out:
                pre = None
                for j in range(i, min(i + 15, len(lines))):
                    mp = RE_IRQ_PRE.search(lines[j])
                    if mp:
                        pre = tok(mp.group(1))
                        break
                if pre is not None:
                    out[m.group(1)] = (pre, '%s:%d' % (rp, i))
    return out


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

    # 2) 各模块中断抢占优先级的宏声明：低于内核上限只算"提示"，
    #    真正算不算违规要看对应的 ISR 是否真的调了 FromISR 接口（见下面 1b 的按实际判定）。
    if mx_lib is not None and os.path.isdir(inc):
        for fn in sorted(os.listdir(inc)):
            if not fn.endswith('.h'):
                continue
            txt, _ = read_text(os.path.join(inc, fn))
            for m in re.finditer(r'#\s*define\s+(SYS_\w*_IRQ_PRE_PRIO)\s+(\d+)', txt):
                val = int(m.group(2))
                if val < mx_lib:
                    findings.append(('注意', '%s/%s' % (os.path.basename(inc), fn),
                                     '%s = %d，小于内核上限 %d：该中断一旦调用 FromISR 接口'
                                     '就会越过内核屏蔽'
                                     % (m.group(1), val, mx_lib), 'prio'))

    # 3) ISR 误用 RTOS 接口（直调 / 经宏展开一层 / 经函数最多三层转发） + 4/5) 任务栈与堆预算
    srcs = list(iter_sources(proj))
    bodies = {}         # 函数名 -> [(文件, 行, 函数体)]
    macros = {}         # 宏名 -> 宏体
    board_texts = []    # 本板所有 .c 的原文，供"文件作用域声明"判定用
    tasks = []          # (文件, 行, 名字, 栈, 优先级)
    for path in srcs:
        text, _ = read_text(path)
        masked = strip_comments(text)
        rel = safe_rel(path)
        board_texts.append(text)
        for name, line, s, e in find_functions(masked):
            bodies.setdefault(name, []).append((rel, line, masked[s + 1:e]))
        for mn, mb in collect_macros(text).items():
            macros.setdefault(mn, mb)
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

    # 从每个 ISR 出发按调用关系往下追。ISR 里间接调用会阻塞/进临界区的接口同样是 bug，
    # 所以不只看直调：包装宏展开一层，普通函数再往下最多三层，并写明转发路径。
    # 增强：chain 每个节点带 (函数名, 文件, 行)，报告时显示完整跨文件路径。
    MAX_HOPS = 5
    reported = set()
    fromisr_isrs = set()    # 这条调用链上用了 xxxFromISR 的 ISR：受优先级上限约束
    for isr_name, defs in bodies.items():
        if not is_isr_name(isr_name):
            continue
        isr_rel, isr_line, isr_body = defs[0]
        loc = '%s:%d' % (isr_rel, isr_line)
        # B3：ISR 自己的函数体里用了浮点（关键字或浮点字面量）
        if RE_FLOAT_USE.search(isr_body):
            findings.append(('注意', loc,
                             'ISR（%s）函数体里用了浮点：Cortex-M4 的 ISR 默认不保存 FPU 寄存器'
                             '（惰性压栈），要么改成定点运算，要么确认 FPU 上下文与 '
                             'portTASK_USES_FLOATING_POINT 的取舍' % isr_name, 'isr_float'))
        queue = [(isr_body, [(isr_name, isr_rel, isr_line)], 0)]
        for _ in range(500):        # 防失控上限，正常调用链远到不了
            if not queue:
                break
            body, chain, depth = queue.pop(0)
            last_name = chain[-1][0]
            for api in scan_calls(body):
                if api.endswith('FromISR') or api == 'portYIELD_FROM_ISR':
                    fromisr_isrs.add(isr_name)
                    continue
                lv = risky_api(api)
                if lv:
                    key = (loc, api, tuple(c[0] for c in chain))
                    if key in reported:
                        continue
                    reported.add(key)
                    suffix = '' if lv == '严重' else '（非 FromISR）'
                    if len(chain) == 1:
                        msg = 'ISR 上下文（%s）里调用了 %s%s' % (isr_name, api, suffix)
                    else:
                        # 显示带文件位置的转发路径
                        hops = ' → '.join('%s(%s:%d)' % (c[0], c[1], c[2]) for c in chain[1:])
                        msg = 'ISR 上下文（%s）经 %s 转发调用了 %s%s' % (
                            isr_name, hops, api, suffix)
                    findings.append((lv, loc, msg, 'isr_api'))
                    continue
                clv, cwhy = isr_costly_call(api)
                if clv:
                    key = (loc, 'costly:' + api, tuple(c[0] for c in chain))
                    if key in reported:
                        continue
                    reported.add(key)
                    if len(chain) == 1:
                        msg = 'ISR 上下文（%s）里调用了 %s：%s' % (isr_name, api, cwhy)
                    else:
                        hops = ' → '.join('%s(%s:%d)' % (c[0], c[1], c[2]) for c in chain[1:])
                        msg = 'ISR 上下文（%s）经 %s 转发调用了 %s：%s' % (
                            isr_name, hops, api, cwhy)
                    findings.append((clv, loc, msg, 'isr_cost'))
                    continue
                mb = macros.get(api)
                if mb is not None:
                    # 包装宏只展开一层：宏体里再直接找危险接口
                    for api2 in scan_calls(mb):
                        lv2 = risky_api(api2)
                        if not lv2 or api2 == api:
                            continue
                        key = (loc, api2, tuple(c[0] for c in chain) + ('宏 ' + api,))
                        if key in reported:
                            continue
                        reported.add(key)
                        suffix2 = '' if lv2 == '严重' else '（非 FromISR）'
                        if len(chain) == 1:
                            # 单跳（ISR 里直接调包装宏）：单独措辞，否则会拼出
                            # 「经 宏 SEND 调用了」多一个空格，与断言对不上
                            msg2 = 'ISR 上下文（%s）里经宏 %s 调用了 %s%s' % (
                                isr_name, api, api2, suffix2)
                        else:
                            hops = ' → '.join('%s(%s:%d)' % (c[0], c[1], c[2])
                                              for c in chain[1:])
                            msg2 = 'ISR 上下文（%s）经 %s → 宏 %s 调用了 %s%s' % (
                                isr_name, hops, api, api2, suffix2)
                        findings.append((lv2, loc, msg2, 'isr_api'))
                    continue
                if depth >= MAX_HOPS or api in (c[0] for c in chain):
                    continue
                for crel, cline, cbody in bodies.get(api, []):
                    queue.append((cbody, chain + [(api, crel, cline)], depth + 1))

    # 3b) 中断里写、任务里读的变量缺 volatile
    #     只看"文件作用域声明"的变量（声明行行首顶格）：局部变量不可能被 ISR 与任务共享，
    #     把它们算进来只会制造误报（i / tmp / cnt 这类名字到处都有）。
    file_scope = {}         # 变量名 -> 声明行原文
    for t in board_texts:
        for m in re.finditer(
                r'^(?:static\s+|extern\s+|const\s+|volatile\s+|unsigned\s+|signed\s+|'
                r'u?int\d*_t\s+|u?int\d+\s+|char\s+|short\s+|long\s+|float\s+|double\s+|'
                r'size_t\s+|bool\s+|TickType_t\s+|BaseType_t\s+|UBaseType_t\s+)+'
                r'\**\s*([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)?(?:=|;)', t, re.M):
            file_scope.setdefault(m.group(1), m.group(0).strip())

    isr_written = {}        # 变量名 -> (文件:行, ISR 名)：中断上下文里被赋值的变量
    for isr_name, defs in bodies.items():
        if not is_isr_name(isr_name):
            continue
        rel, line, body = defs[0]
        for m in re.finditer(r'\b([A-Za-z_]\w*)\s*(?:\+\+|--|\+=|-=|\|=|&=|=(?!=))', body):
            nm = m.group(1)
            if nm in KEYWORDS:
                continue
            if m.start() > 0 and body[m.start() - 1] in '.>':   # 成员访问不算共享变量
                continue
            isr_written.setdefault(nm, ('%s:%d' % (rel, line), isr_name))

    touched_outside = set()
    for fname, defs in bodies.items():
        if is_isr_name(fname):
            continue
        for _, _, b in defs:
            touched_outside.update(re.findall(r'\b[A-Za-z_]\w*\b', b))

    for nm, (loc, isr) in isr_written.items():
        if nm not in file_scope or nm not in touched_outside or 'volatile' in file_scope[nm]:
            continue
        findings.append(('注意', loc,
                         '变量 %s 在 ISR（%s）里被写入、任务态代码里也读写，'
                         '但声明「%s」没有 volatile，任务侧可能读到陈旧值'
                         % (nm, isr, file_scope[nm]), 'volatile'))

    # 1b) FromISR 中断优先级约束，按"实际调没调"判定
    #     真正的规则是：调了 xxxFromISR 的 ISR，其抢占值必须 >= 内核上限；没调 FromISR 的
    #     ISR 不受这条约束（官方也是这么说的）。所以这里把 ISR 名对回 IRQn，再取 FWLIB/src
    #     里真正配下去的那个值 —— 光看 FWLIB/inc 的宏声明判不出来，容易冤枉没用 FromISR 的中断。
    if mx_lib is not None:
        irq_prios = read_irq_prios(proj)
        for isr_name in sorted(fromisr_isrs):
            if not isr_name.endswith('_IRQHandler'):
                continue        # _isr/_irq/_dispatch 这类转发名对不上 IRQn，静态定位不了
            irqn = isr_name[:-len('_IRQHandler')] + '_IRQn'
            if irqn not in irq_prios:
                continue
            pre, where = irq_prios[irqn]
            if pre < mx_lib:
                findings.append(('严重', where,
                                 '%s 里调用了 FromISR 接口，但它的抢占优先级 %d 小于内核上限 %d'
                                 '（数值越小优先级越高，会越过 BASEPRI）'
                                 % (isr_name, pre, mx_lib), 'prio'))

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

    # 7) configASSERT
    if not vals.get('_has_assert'):
        findings.append(('注意', 'FreeRTOS/port/FreeRTOSConfig.h',
                         '未定义 configASSERT，优先级违规不会被内核当场发现', 'assert'))

    # 8) 看门狗 vs Flash 擦写：擦除/写页期间 CPU 取指被硬件挂起，SysTick 与串口都停，
    #    这段时间喂不了狗；IWDG 一启动又停不了。判据：工程里既做长擦写，
    #    又把看门狗超时设得偏短（< WDG_SAFE_MS），或超时实参不是常量、判不出。
    #    只看"工程自己在调"，FWLIB 里的实现本身不算（sys_flash.c / sys_wdg.c 跳过）。
    obj_macros = {}
    flash_ops = []
    wdg_inits = []
    for path in srcs:
        text, _ = read_text(path)
        masked = strip_comments(text)
        if os.path.basename(path) in ('sys_flash.c', 'sys_wdg.c'):
            continue
        rel = safe_rel(path)
        for m in re.finditer(r'^\s*#\s*define\s+([A-Za-z_]\w*)\s+([^\r\n]+)', text, re.M):
            obj_macros.setdefault(m.group(1), m.group(2).strip())
        for m in re.finditer(r'\b(%s)\s*\(' % '|'.join(FLASH_LONG_OPS), masked):
            flash_ops.append((rel, masked.count('\n', 0, m.start()) + 1, m.group(1)))
        for m in re.finditer(r'\bSYS_WDG_Init\s*\(', masked):
            res = call_args(masked, m.end() - 1)
            tmo = None
            if res:
                sub = {}
                for k, v in obj_macros.items():
                    iv = eval_int(v)
                    if iv is not None:
                        sub[k] = iv
                tmo = eval_int(res[0], sub)
            wdg_inits.append((rel, masked.count('\n', 0, m.start()) + 1, tmo))

    if flash_ops and wdg_inits:
        wrong = [t for _, _, t in wdg_inits if t is not None and t < WDG_SAFE_MS]
        unknown = any(t is None for _, _, t in wdg_inits)
        if wrong or unknown:
            rel0, ln0, op0 = flash_ops[0]
            why = ('看门狗超时只设了 %d ms（<%d ms）' % (min(wrong), WDG_SAFE_MS)) if wrong \
                else '看门狗超时实参不是常量、判不出（当心默认值很短）'
            findings.append(('注意', '%s:%d' % (rel0, ln0),
                             '工程里既做长耗时 Flash 操作（%s，此处为首次出现），%s：'
                             '擦除/写页期间 CPU 取指被硬件挂起，SysTick 与串口都停，'
                             '这段时间若没喂狗会直接复位。请确认擦除前后有 SYS_WDG_Feed()'
                             '（或把看门狗超时放到 %d ms 以上）。'
                             % (op0, why, WDG_SAFE_MS), 'wdg_flash'))

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
    out.append('4) 中断上下文 API 分类规则表：%s。' % RULES_SOURCE)
    out.append('')
    return '\n'.join(out)


def to_json(results):
    """结构化输出，供工具箱的可视化体检面板读取。"""
    boards = []
    for title, vals, findings, tasks in results:
        crit = sum(1 for f in findings if f[0] == '严重')
        warn = sum(1 for f in findings if f[0] == '注意')
        cfg = {}
        if vals:
            for k, v in vals.items():
                if isinstance(v, (int, float, bool, str)) or v is None:
                    cfg[k] = v
        boards.append({
            'title': title,
            'severe': crit,
            'warn': warn,
            'config': cfg,
            'tasks': [{'file': t[0], 'line': t[1], 'name': t[2],
                       'stack': t[3], 'prio': t[4]} for t in tasks],
            'findings': [{'level': f[0], 'loc': f[1], 'msg': f[2], 'key': f[3]}
                         for f in findings],
        })
    ok = all(b['severe'] == 0 for b in boards)
    return {'tool': 'rtos_check', 'schema': 1, 'ok': ok, 'boards': boards}


def selftest():
    """用一组故意写错的合成工程验证检查确实命中（含"经宏 / 经转发函数"两条最容易漏的路径），
    并用反例守住不误报：任务态函数里调同名接口、ISR 里调 xxxFromISR、配置齐全时不报配置项。
    避免"永远报通过"。"""
    import shutil
    import tempfile

    checks = []

    def ck(name, ok, extra=''):
        checks.append((name, bool(ok), extra))

    d = tempfile.mkdtemp(prefix='rtos_selftest_')
    base = os.path.join(d, 'Project')
    try:
        os.makedirs(os.path.join(base, 'FreeRTOS', 'port'))
        os.makedirs(os.path.join(base, 'FWLIB', 'inc'))
        os.makedirs(os.path.join(base, 'FWLIB', 'src'))

        def wr(path, text):
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)

        wr(os.path.join(base, 'FreeRTOS', 'port', 'FreeRTOSConfig.h'),
           '#define configPRIO_BITS 4\n'
           '#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5\n'
           '#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15\n'
           '#define configTOTAL_HEAP_SIZE ((size_t)(30 * 1024))\n'
           '#define configMINIMAL_STACK_SIZE ((unsigned short)128)\n'
           '#define configMAX_PRIORITIES 8\n'
           '#define configASSERT(x) if ((x) == 0) { for (;;); }\n')
        wr(os.path.join(base, 'FWLIB', 'inc', 'sys_nvic.h'),
           '#define SYS_NVIC_PRIORITY_GROUP NVIC_PriorityGroup_2\n')
        wr(os.path.join(base, 'FWLIB', 'inc', 'sys_usart.h'),
           '#define SYS_USART_IRQ_PRE_PRIO 2\n')
        wr(os.path.join(base, 'FWLIB', 'src', 'nvic.c'),
           'void Bsp_NvicInit(void)\n{\n'
           '    SYS_NVIC_SetPriority(USART1_IRQn, SYS_USART_IRQ_PRE_PRIO, 0);\n'
           '    SYS_NVIC_SetPriority(TIM5_IRQn, 6, 0);\n'
           '    SYS_NVIC_SetPriority(TIM9_IRQn, 2, 0);\n}\n')
        wr(os.path.join(base, 'main.c'),
           '#define SEND(q, x) xQueueSend((q), (x), 0)\n'
           '\n'
           'static uint8_t g_tick_flag;\n'
           'static volatile uint8_t g_ok_flag;\n'
           '\n'
           'static void App_ExtiNotify(void) { xSemaphoreGive(s_bin); }\n'
           'void USART1_IRQHandler(void) { xQueueSendFromISR(s_q, &x, &w); xQueueSend(s_q, &x, 0); }\n'
           'void EXTI0_IRQHandler(void)  { App_ExtiNotify(); }\n'
           'void TIM3_IRQHandler(void)   { SEND(s_q, &x); }\n'
           'void TIM2_IRQHandler(void)   { xQueueCreate(4, 4); }\n'
           'void TIM4_IRQHandler(void)\n{\n'
           '    taskENTER_CRITICAL();\n'
           '    g_tick_flag = 1;\n'
           '    taskEXIT_CRITICAL();\n}\n'
           'void TIM5_IRQHandler(void)   { xSemaphoreGiveFromISR(s_bin, &w); }\n'
           'void TIM6_IRQHandler(void)   { uint8_t tmp; tmp = 1; g_ok_flag = 1; }\n'
           'void TIM9_IRQHandler(void)   { xQueueSend(s_q, &x, 0); }\n'
           'void SPI1_IRQHandler(void)   { xQueueSendFromISR(s_q, &x, &w); }\n'
           'void App_Task(void)          { xQueueSend(s_q, &x, 0); }\n'
           'void App_Loop(void)          { if (g_tick_flag) { } if (g_ok_flag) { } tmp = 2; }\n'
           'void app_init(void)\n{\n'
           '    xTaskCreate(taskA, "A", 32, NULL, 2, NULL);\n'
           '    xTaskCreate(taskB, "B", 256, NULL, 9, NULL);\n}\n')

        _, findings, _ = check_board('synthetic', base)
        keys = {f[3] for f in findings}
        msgs = [(f[0], f[2]) for f in findings]

        def hit(needle, level=None):
            return any(needle in m and (level is None or lv == level) for lv, m in msgs)

        ck('直调阻塞接口被抓到（严重）',
           hit('USART1_IRQHandler）里调用了 xQueueSend', '严重'))
        ck('经普通函数转发被抓到（严重，且写明转发路径）',
           hit('经 App_ExtiNotify(', '严重'))
        ck('经包装宏展开被抓到（严重）',
           hit('经宏 SEND 调用了 xQueueSend', '严重'))
        ck('非阻塞的 RTOS 接口判为"注意"',
           hit('TIM2_IRQHandler）里调用了 xQueueCreate', '注意'))
        ck('反例：ISR 里调 xxxFromISR 不误报',
           not any('SPI1_IRQHandler' in m or 'xQueueSendFromISR' in m for _, m in msgs))
        ck('反例：任务态函数里调同名接口不误报', not any('App_Task' in m for _, m in msgs))
        ck('ISR 里进内核临界区被抓到（严重）',
           hit('TIM4_IRQHandler）里调用了 taskENTER_CRITICAL', '严重')
           and hit('TIM4_IRQHandler）里调用了 taskEXIT_CRITICAL', '严重'))
        ck('中断写、任务读的变量缺 volatile 被抓到（注意）',
           hit('变量 g_tick_flag', '注意') and 'volatile' in keys)
        ck('反例：已加 volatile 的共享变量不报', not any('g_ok_flag' in m for _, m in msgs))
        ck('反例：ISR 里的局部变量不报', not any('tmp' in m for _, m in msgs))
        ck('调 FromISR 且抢占低于上限 -> 严重（按实际调用判定，而不是看宏声明）',
           hit('USART1_IRQHandler 里调用了 FromISR 接口', '严重')
           and hit('抢占优先级 2 小于内核上限 5', '严重'))
        ck('反例：调 FromISR 但抢占已达上限，不报',
           not any('TIM5' in m for _, m in msgs))
        ck('反例：抢占低于上限但没调 FromISR，不报（不冤枉它）',
           not any('TIM9_IRQn' in m for _, m in msgs))
        ck('宏声明低于上限降为提示级（不再直接判严重）',
           hit('SYS_USART_IRQ_PRE_PRIO', '注意') and not hit('SYS_USART_IRQ_PRE_PRIO', '严重'))
        ck('原有类别仍在（NVIC 分组 / 抢占越界 / 栈过小）',
           {'group', 'prio', 'stack'}.issubset(keys), '、'.join(sorted(keys)) or '无')
        ck('任务优先级越界仍命中', hit('超出 configMAX_PRIORITIES', '严重'))
        ck('反例：配置齐全时不报 configASSERT / 堆预算',
           'assert' not in keys and 'heap' not in keys, '、'.join(sorted(keys)) or '无')
        ck('外置规则表已装载（数据模型取自 速查数据/rules，不是内置默认）',
           RULES_SOURCE != '内置默认' and 'xQueueSend' in BLOCKING_API, RULES_SOURCE)
        ck('外置规则表生效：阻断类 / 临界区类 / 前缀类都对得上',
           'xSemaphoreGive' in BLOCKING_API and 'taskENTER_CRITICAL' in ISR_UNSAFE_CRITICAL
           and 'xQueue' in RTOS_PREFIX, '前缀 %s' % ('、'.join(RTOS_PREFIX)))

        # ---- 看门狗 vs Flash 擦写（错例必须命中，反例必须不报）----
        def mkproj(name, extra_c):
            pp = os.path.join(d, name)
            os.makedirs(os.path.join(pp, 'FreeRTOS', 'port'))
            os.makedirs(os.path.join(pp, 'FWLIB', 'src'))
            wr(os.path.join(pp, 'FreeRTOS', 'port', 'FreeRTOSConfig.h'),
               '#define configPRIO_BITS 4\n'
               '#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5\n'
               '#define configTOTAL_HEAP_SIZE ((size_t)(30 * 1024))\n'
               '#define configMINIMAL_STACK_SIZE ((unsigned short)128)\n'
               '#define configMAX_PRIORITIES 8\n'
               '#define configASSERT(x) if ((x) == 0) { for (;;); }\n')
            wr(os.path.join(pp, 'app.c'), extra_c)
            return pp

        def fires(proj):
            _, ff, _ = check_board('x', proj)
            return any(k == 'wdg_flash' for _, _, _, k in ff)

        ck('看门狗超时短(4s) + 长擦写 -> 命中（注意）',
           fires(mkproj('wdg_short',
                        '#define APP_WDG_TIMEOUT_MS 4000\n'
                        'void App_Store(void)\n{\n'
                        '    SYS_WDG_Init(APP_WDG_TIMEOUT_MS);\n'
                        '    SYS_FLASH_SaveParams(&cfg, sizeof(cfg));\n}\n')))
        # 走 sys_wdg.c / sys_flash.c 的"库实现不算"这条：把实现文件放进工程，也不该因它命中
        ck('反例：看门狗超时够长(12s) 不报',
           not fires(mkproj('wdg_long',
                            '#define APP_WDG_TIMEOUT_MS 12000\n'
                            'void App_Store(void)\n{\n'
                            '    SYS_WDG_Init(APP_WDG_TIMEOUT_MS);\n'
                            '    SYS_FLASH_EraseSector(0x08070000);\n}\n')))
        ck('反例：没有看门狗，不报',
           not fires(mkproj('no_wdg',
                            'void App_Store(void)\n{\n'
                            '    SYS_FLASH_EraseSector(0x08070000);\n}\n')))
        ck('反例：没有长擦写，不报',
           not fires(mkproj('no_flash',
                            '#define APP_WDG_TIMEOUT_MS 4000\n'
                            'void App_Init(void)\n{\n'
                            '    SYS_WDG_Init(APP_WDG_TIMEOUT_MS);\n}\n')))
        ck('看门狗超时判不出（实参是变量）时也提示"没把握"',
           fires(mkproj('wdg_var',
                        'void App_Store(uint32_t ms)\n{\n'
                        '    SYS_WDG_Init(ms);\n'
                        '    SYS_FLASH_SaveParams(&cfg, sizeof(cfg));\n}\n')))
        # sys_wdg.c / sys_flash.c 是库实现，里面的 SYS_WDG_Init / SYS_FLASH_* 定义
        # 不该被当成"工程在用"。放进去但工程自己不调 -> 不报。
        lib_proj = mkproj('lib_only', 'void App_None(void)\n{\n}\n')
        wr(os.path.join(lib_proj, 'FWLIB', 'src', 'sys_flash.c'),
           'void SYS_FLASH_EraseSector(uint32_t a)\n{\n    (void)a;\n}\n'
           'void SYS_FLASH_SaveParams(const void *d, uint16_t n)\n{\n    (void)d; (void)n;\n}\n')
        wr(os.path.join(lib_proj, 'FWLIB', 'src', 'sys_wdg.c'),
           'void SYS_WDG_Init(uint32_t ms)\n{\n    (void)ms;\n}\n')
        ck('反例：sys_wdg.c / sys_flash.c 里的实现本身不算"在用"', not fires(lib_proj))

        # ---- B3：中断里的长耗时 / 不可重入调用 + 浮点 ----
        def b3_of(proj):
            """只挑 B3 两类（isr_cost / isr_float），避免被无关规则（如 NVIC 分组提示）干扰。"""
            _, ff, _ = check_board('x', proj)
            return [(lv, m, k) for lv, _, m, k in ff if k in ('isr_cost', 'isr_float')]

        ck('B3 错例：ISR 里调 printf 判严重',
           any(lv == '严重' and 'printf' in m
               for lv, m, _ in b3_of(mkproj('b3_printf',
                   'void USART1_IRQHandler(void)\n{\n    printf("x=%d\\n", g);\n}\n'))))
        ck('B3 错例：ISR 里调 pvPortMalloc 判严重',
           any(lv == '严重' and 'pvPortMalloc' in m
               for lv, m, _ in b3_of(mkproj('b3_malloc',
                   'void TIM2_IRQHandler(void)\n{\n    p = pvPortMalloc(8);\n}\n'))))
        ck('B3 错例：ISR 里调忙等延时判注意',
           any(lv == '注意' and 'delay_ms' in m
               for lv, m, _ in b3_of(mkproj('b3_delay',
                   'void EXTI0_IRQHandler(void)\n{\n    delay_ms(5);\n}\n'))))
        ck('B3 错例：ISR 里用浮点判注意',
           any(lv == '注意' and '浮点' in m
               for lv, m, _ in b3_of(mkproj('b3_float',
                   'void TIM3_IRQHandler(void)\n{\n    float v = 1.5f;\n    g = (int)v;\n}\n'))))
        ck('B3 反例：任务态函数里调 printf / delay / 浮点都不报',
           not b3_of(mkproj('b3_ok',
               'void App_Print(void)\n{\n    printf("ok\\n");\n'
               '    delay_ms(1);\n    float v = 2.5f;\n    (void)v;\n}\n')))
        ck('B3 反例：ISR 里只置标志 + FromISR 投递 → 不报',
           not b3_of(mkproj('b3_pos',
               'void TIM4_IRQHandler(void)\n{\n    g_flag = 1;\n'
               '    xQueueSendFromISR(q, &d, &w);\n}\n')))

        j = to_json([('板X', None, [('严重', 'loc', 'msg', 'assert')], [('f.c', 1, 't', 128, 3)])])
        ck('JSON 结构正确（工具名/板数/ok）',
           j['tool'] == 'rtos_check' and len(j['boards']) == 1 and j['ok'] is False)
        ck('JSON 任务与 findings 映射正确',
           j['boards'][0]['tasks'][0]['name'] == 't' and j['boards'][0]['tasks'][0]['prio'] == 3
           and j['boards'][0]['findings'][0]['key'] == 'assert')
        # B6 JSON 契约：面板读 severe/warn/tasks，字段名/类型一改就红
        ck('JSON 契约：面板读取的字段名/类型都在',
           set(j) == {'tool', 'schema', 'ok', 'boards'}
           and set(j['boards'][0]) == {'title', 'severe', 'warn', 'config',
                                       'tasks', 'findings'}
           and isinstance(j['boards'][0]['severe'], int)
           and isinstance(j['boards'][0]['warn'], int)
           and set(j['boards'][0]['tasks'][0]) == {'file', 'line', 'name', 'stack', 'prio'}
           and set(j['boards'][0]['findings'][0]) == {'level', 'loc', 'msg', 'key'},
           '、'.join(sorted(j['boards'][0])))
    finally:
        shutil.rmtree(d, ignore_errors=True)

    bad = [c for c in checks if not c[1]]
    for name, ok, extra in checks:
        print('  [%s] %s%s' % ('PASS' if ok else 'FAIL', name,
                               ('  （实际：%s）' % extra) if (extra and not ok) else ''))
    print('RTOS 检查自检：%d 通过 / %d 失败' % (len(checks) - len(bad), len(bad)))
    for lv, loc, msg, key in findings:
        print('    [%s][%s] %s' % (lv, key, msg))
    return 1 if bad else 0


def main(argv):
    if '--selftest' in argv:
        return selftest()
    quiet = False
    json_mode = False
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'RTOS检查报告.md')
    bad = []
    i = 1
    while i < len(argv):
        if argv[i] == '--quiet':
            quiet = True
            i += 1
        elif argv[i] == '--json':
            json_mode = True
            i += 1
        elif argv[i] == '--out':
            if i + 1 >= len(argv):
                bad.append('--out 缺少值')
                i += 1
                continue
            out_path = os.path.abspath(argv[i + 1])
            i += 2
        else:
            bad.append(argv[i])
            i += 1

    # 参数写错要报错退出，不能静默忽略 —— 否则拼错开关会跑成一次全量真实扫描，
    # 白白覆盖报告，还让人以为"检查过了"。
    if bad:
        print('未知参数：%s' % ' '.join(bad))
        print('可用参数：--out <文件> | --quiet | --json | --selftest')
        return 2

    results = []
    for title, projrel in BOARDS:
        proj = os.path.join(ROOT, projrel)
        if not os.path.isdir(proj):
            results.append((title, None, [('严重', projrel, '工程目录不存在', 'assert')], []))
            continue
        vals, findings, tasks = check_board(title, proj)
        results.append((title, vals, findings, tasks))

    if json_mode:
        print(json.dumps(to_json(results), ensure_ascii=False))
        return 0

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
