# -*- coding: utf-8 -*-
r"""两板 main.c 的统一维护脚本。

两份 main.c 同源，但有分叉（板2 多出 ESP8266/MQTT 自检与下行命令任务 vTaskCmdRx），
所以本脚本只做"在板1 main.c 上打结构补丁"这一件事，默认不碰板2。

做三件补丁（都幂等：已是目标状态就一个字节都不动）：
    1. 板1 test_rtc() 的声明块加 #if (SYS_TEST_BOARD == 1) 包壳。
       板1 用 GetDate+GetTime 需要 y/mo/d/wd，板2 用单个 SysRtc_t 不需要，
       不包起来板2 会报 7 条 "variable was declared but never referenced"。
    2. 板1 组帧段（put_u16_be + frame_build）用 #if (SYS_TEST_BOARD == 1) 包起来，
       否则板2 报 "frame_build was declared but never referenced"。
    3. 板号改成"必须由 Keil 工程 Define 提供" + 两条 #error 兜底。
    4. 在全部自检之后插入 GPIO_ClaimDump()（打印运行期引脚登记表）。

引脚占用表（两份 main.c 开头的注释块）由人工维护，本脚本只检查它在不在，不写入。

板2 main.c 的生成已停用：加 --write-board2 才会由板1 覆盖写出，正常不要用它，
两份文件现在各改各的。

GBK 编码：main.c 必须存成 GBK。AC5 实测存成 UTF-8 会报 30 个
`#8: missing closing quote` + `#870-D: invalid multibyte character sequence`，
存成 GBK 是 0 Error。本脚本统一按 GBK 读写，改这两个 main.c 请用本脚本或
能指定编码的编辑器。

用法：python 本地工具\仓库巡检\sync_main.py
"""

import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
P1 = os.path.join(ROOT, 'EnvMonitor', 'Project_1_检测端', 'Project', 'main.c')
P2 = os.path.join(ROOT, 'EnvMonitor', 'Project_2_数据通信端', 'Project', 'main.c')

MARK_DECL = '#if (SYS_TEST_BOARD == 1)   /* 板1 用 GetDate'
MARK_FRAME = '#if (SYS_TEST_BOARD == 1)   /* 组帧只有板1'

DECL_OLD = '''    uint8_t  h = 0, m = 0, s = 0;
    uint16_t y = 0;
    uint8_t  mo = 0, d = 0, wd = 0;
    uint8_t  r;

    banner("02 RTC '''
DECL_NEW = '''#if (SYS_TEST_BOARD == 1)   /* 板1 用 GetDate+GetTime，需要这几个变量；
                            * 板2 用单个 SysRtc_t，不需要它们 */
    uint8_t  h = 0, m = 0, s = 0;
    uint16_t y = 0;
    uint8_t  mo = 0, d = 0, wd = 0;
#endif
    uint8_t  r;

    banner("02 RTC '''

FRAME_HEAD_OLD = 'static void put_u16_be(uint8_t *p, uint16_t v)\n{'
FRAME_HEAD_NEW = ('#if (SYS_TEST_BOARD == 1)   /* 组帧只有板1（发送端）用到；'
                  '板2 只解析，不组帧 */\n'
                  'static void put_u16_be(uint8_t *p, uint16_t v)\n{')
FRAME_TAIL_OLD = '    f[18] = 0x55U;\n    f[19] = 0xAAU;\n}\n'
FRAME_TAIL_NEW = ('    f[18] = 0x55U;\n    f[19] = 0xAAU;\n}\n'
                  '#endif  /* 组帧段（仅板1） */\n')

FWD_OLD = '''            SYS_RTC_GetTime(&hh, &mm, &ss);
            ts = ((uint32_t)hh * 10000UL) + ((uint32_t)mm * 100UL) + ss;
'''
FWD_NEW = '''            /* 时间戳：这里只取"时分秒"当简易序号，够联调用。
             * 板2 的 sys_rtc 是 SysRtc_t 结构体，与板1 的 GetDate/GetTime
             * 不同，所以这里也按板号分开写。 */
            {
                SysRtc_t now;
                SYS_RTC_GetTime(&now);
                hh = now.hour; mm = now.minute; ss = now.second;
            }
            ts = ((uint32_t)hh * 10000UL) + ((uint32_t)mm * 100UL) + ss;
'''

# ====================================================================
# ④ 板号：必须由 Keil 工程 Define 提供（E 项）
# ====================================================================
MARK_BOARD = '板号：必须由 Keil 工程设置提供'
BOARD_OLD = '#ifndef SYS_TEST_BOARD\n  #define SYS_TEST_BOARD   1\n#endif\n'
BOARD_NEW = r'''/* ---------------- 板号：必须由 Keil 工程设置提供（2026-10-11 改） ----------------
 * 以前这里的写法是"文件里给个默认板号 1"，有两个毛病：
 *   ① 板2 main.c 是从板1 复制生成的，生成时要替换掉文件里那一行板号；
 *   ② 更糟的是：谁把这份 main.c 拷进别的工程，都会**静默**地编成板1，
 *      上板才发现串口 / LoRa 控制脚全不对（板号写在文件里，从工程设置上看不出来）。
 * 现在板号只认工程设置，文件里不再有默认值：
 *   板1 Project_1_检测端    Keil：Options for Target -> C/C++ -> Define 里加一项
 *                           SYS_TEST_BOARD=1
 *   板2 Project_2_数据通信端  同上，加 SYS_TEST_BOARD=2
 *   （其余 Define 项不动：USE_STDPERIPH_DRIVER, STM32F40_41xxx, HSE_VALUE=8000000）
 * 忘了加就直接编译失败（看下面 #error 的提示），不会再悄悄编成另一块板。
 * 注意：本注释里刻意**不写出**那行定义板号的宏，
 *       因为 本地工具\仓库巡检\sync_main.py 正是靠"文件里没有它"来判断
 *       "板号已经交给工程设置了"。
 */
#ifndef SYS_TEST_BOARD
  #error "没有定义 SYS_TEST_BOARD：请在 Keil 工程 Options for Target -> C/C++ -> Define 里加 SYS_TEST_BOARD=1 (板1) 或 =2 (板2)"
#elif ((SYS_TEST_BOARD != 1) && (SYS_TEST_BOARD != 2))
  #error "SYS_TEST_BOARD 只能是 1 (板1 检测端) 或 2 (板2 数据通信端)"
#endif
'''

# ====================================================================
# ====================================================================
# 引脚占用表：人工维护，脚本只检查在不在
# ====================================================================
# 表在两份 main.c 开头的注释块里（板1 main.c:52-83、板2 main.c:56-87）。
# 引脚全量声明清单由 pin_audit.py 扫 FWLIB\inc\*.h 生成；运行期实际配置由
# gpio_core 的 GPIO_Claim 登记，main() 里 GPIO_ClaimDump() 打印。
MARK_PIN = '引脚占用表（本工程 main.c 真正用到的脚）'
MARK_DUMP = 'GPIO_ClaimDump();'
DUMP_ANCHOR = ('#if (SYS_TEST_BOARD == 2)\n'
               '    test_mqtt();        /* MQTT + liveness (SKIPs if no WiFi) */\n'
               '#endif\n')
DUMP_NEW = DUMP_ANCHOR + r'''
    /* 打印 gpio_core 登记的引脚占用表。
     * 走到这里时 LED / RTC / Flash / DHT11 / LoRa / MQTT 各自把用到的脚配过一遍，
     * 且调度器还没启动，仍是单线程，所以表是完整的。
     * 标了 taken by N files 的行表示同一个脚被两个源文件配过，后配的生效，前一个失效。
     * 末行应为 "==== target : conflict 0 / lost 0 ===="；不为 0 就按行末的 文件:行号
     * 改引脚宏，并同步更新 main.c 开头的引脚占用表。 */
    GPIO_ClaimDump();
'''
DUMP_TAIL = '    GPIO_ClaimDump();\n'


def gbk_write(path, text):
    """先整段编码成 GBK，编不过就**一个字节都不写**（否则 io.open('w') 会先把
    文件清空，中途 UnicodeEncodeError 会把原来的 main.c 截成 0 字节）。"""
    data = text.encode('gbk')          # 编不过就在这里抛异常，文件还没动
    with io.open(path, 'wb') as f:
        f.write(data)
    return len(data)


def patch_block(s, anchor, tail, new, mark=None):
    r"""把 anchor 之后、tail（含）之前的内容换成 new 里"锚点之后"那一段。

    幂等 + **可重贴**：内容与脚本常量不一致时就地换掉（旧块被删，不会插两遍），
    一致时原样返回。返回值 (新文本, 结果)，结果是：
        'skip' = 已是最新（一个字节没动）    'done' = 插入 / 更新了
        None   = 锚点或尾标对不上（结构不认识，交给调用方报错让人工处理）
    tail 传 None 表示纯插入（锚点后没有要找的尾标）。
    mark 是"旧块特征串"：找不到 tail 却在文件里看见 mark，说明存在一个
      **认不出尾标的旧块**，此时返回 None —— 宁可报错也不静默插重复块。
    """
    if not new.startswith(anchor):
        raise ValueError('new 必须以 anchor 开头')

    i = s.find(anchor)
    if i < 0:
        return s, None
    start = i + len(anchor)

    if tail is None:
        end = start
    else:
        j = s.find(tail, start)
        if j >= 0:
            end = j + len(tail)
        elif (mark is not None) and (mark in s):
            return s, None
        else:
            end = start

    body = new[len(anchor):]
    if s[start:end] == body:
        return s, 'skip'
    return s[:start] + body + s[end:], 'done'


def main():
    if not os.path.isfile(P1):
        print('找不到板1 main.c：%s' % P1)
        return 1

    s1 = io.open(P1, encoding='gbk', newline='').read()
    changed = False

    if MARK_DECL in s1:
        print('板1：声明块包壳已存在')
    elif DECL_OLD in s1:
        s1 = s1.replace(DECL_OLD, DECL_NEW, 1)
        changed = True
        print('板1：声明块包壳已打上')
    else:
        print('板1：找不到 test_rtc 的声明块 —— 可能你已经改过，请人工确认')
        return 2

    if MARK_FRAME in s1:
        print('板1：组帧包壳已存在')
    elif FRAME_HEAD_OLD in s1 and FRAME_TAIL_OLD in s1:
        s1 = s1.replace(FRAME_HEAD_OLD, FRAME_HEAD_NEW, 1)
        s1 = s1.replace(FRAME_TAIL_OLD, FRAME_TAIL_NEW, 1)
        changed = True
        print('板1：组帧包壳已打上')
    else:
        print('板1：找不到组帧段 —— 可能你已经改过，请人工确认')
        return 2

    # ---- ③ 板号改成"必须由工程 Define 提供"（E 项） ----
    if MARK_BOARD in s1:
        print('板1：板号 #error 兜底已存在')
    elif BOARD_OLD in s1:
        s1 = s1.replace(BOARD_OLD, BOARD_NEW, 1)
        changed = True
        print('板1：板号已改成"必须由 Keil 工程 Define 提供"')
    else:
        print('板1：找不到"文件里给板号默认值"的那三行 —— 请人工确认')
        return 2

    # ---- ④ 引脚占用表：只检查在不在（表由人工维护，脚本不写入） ----
    if MARK_PIN in s1:
        print('板1：引脚占用表在')
    else:
        print('板1：找不到引脚占用表，按 pin_audit.py 的结果补回后再跑')
        return 2

    # ---- ⑤ 自检末尾打印运行期引脚登记表（A3 项；同样可重贴） ----
    s1, r = patch_block(s1, DUMP_ANCHOR, DUMP_TAIL, DUMP_NEW, MARK_DUMP)
    if r is None:
        print('板1：GPIO_ClaimDump() 插入点结构不认识 —— 请人工确认')
        return 2
    if r == 'done':
        changed = True
        print('板1：已在自检末尾插入 GPIO_ClaimDump()')
    else:
        print('板1：GPIO_ClaimDump() 调用已是最新')

    if changed:
        gbk_write(P1, s1)

    # ---- 板2 = 板1 + 时间戳写法 + 补板2 专属头文件（板号**不再**在这里替换） ----
    # 板号现在由各工程的 Define 提供（板1 工程 SYS_TEST_BOARD=1、板2 工程 =2），
    # 所以这里故意不做任何板号替换；万一那行宏又冒出来，直接报错拦下。
    s2 = s1
    if '#define SYS_TEST_BOARD' in s2:
        print('板1 main.c 里还有"定义板号的那行宏"：与"板号交给工程设置"的设计冲突，请先删掉')
        return 3

    # 板2 才有 MQTT/ESP8266 自检，把这两个头文件插在 lora_e22.h 之后。
    # （板1 编的是 #if (SYS_TEST_BOARD == 2) 之外的代码，不需要它们。）
    inc_anchor = '#include "lora_e22.h"\n'
    inc_new = ('#include "lora_e22.h"\n'
               '#include "esp8266.h"     /* 板2 才有：MQTT 自检用 */\n'
               '#include "mqtt.h"\n')
    if '#include "mqtt.h"' in s2:
        print('板2：MQTT/ESP8266 头文件已在')
    elif inc_anchor in s2:
        s2 = s2.replace(inc_anchor, inc_new, 1)
        print('板2：已插入 esp8266.h / mqtt.h')
    else:
        print('警告：找不到 lora_e22.h 的包含点，请人工补 esp8266.h / mqtt.h')

    if FWD_OLD in s2:
        s2 = s2.replace(FWD_OLD, FWD_NEW, 1)
        print('板2：时间戳读取已改为 SysRtc_t')
    elif 'SysRtc_t now;' in s2:
        print('板2：时间戳读取已是 SysRtc_t')
    else:
        print('板2：找不到 vTaskForward 里读时间的地方')
        return 4

    if '--write-board2' not in sys.argv:
        print('板2 main.c 未写出：两份 main.c 已分叉（板2 有自己的 vTaskCmdRx 与 MQTT 自检），')
        print('  由板1 覆盖会丢掉这些内容。确实要覆盖时加 --write-board2 再跑。')
        return 0

    gbk_write(P2, s2)
    print('板2 main.c 已写出（%d 字符）' % len(s2))
    print('板1 main.c 字符数 = %d' % len(s1))
    print('提示：改完记得用 Keil 各编一次，两边都应为 0 Error 0 Warning。')
    return 0


if __name__ == '__main__':
    sys.exit(main())
