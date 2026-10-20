# -*- coding: utf-8 -*-
r"""「协议对照」插件壳（B1）：把 `host_compare.py` 挂进工具箱侧栏（不用改一行 C++）。

真正的对照在 `host_compare.py`：固件 `sys_frame.h` ↔ 上位机 `frameparser.h`，
三层（常量 / 期望向量 / 真解析），外加一个「三方对照台（D4）」——
真板串口流（`--capture` 抓包，默认同目录 `产物/真板抓包.hex`）↔ 仿真帧 ↔ 上位机 FrameParser
逐字段对照。第 3 层与 FrameParser 这一方因为上位机协议层还是 `TODO(阶段2)` 的桩，
现在是 honest SKIP —— 不假装跑过；抓包缺失时真板那一方也如实 SKIP。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from _plugin_host import main        # noqa: E402

main(upstream='host_compare.py',
     args=[],
     name='协议对照',
     desc='两板固件 ↔ 上位机解析器的协议对照：常量、期望向量逐字节、真解析三层；'
          '外加三方对照台（真板抓包 ↔ 仿真帧 ↔ 上位机 FrameParser）逐字段对照。'
          '真解析与 FrameParser 那两方要等上位机协议层写完（现在 honest SKIP）。')
