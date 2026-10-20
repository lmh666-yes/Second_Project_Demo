# -*- coding: utf-8 -*-
r"""「协议对照」插件壳（B1）：把 `host_compare.py` 挂进工具箱侧栏（不用改一行 C++）。

真正的对照在 `host_compare.py`：固件 `sys_frame.h` ↔ 上位机 `frameparser.h`，
三层（常量 / 期望向量 / 真解析）。第 3 层因为上位机协议层还是 `TODO(阶段2)` 的桩，
现在是 honest SKIP —— 不假装跑过。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from _plugin_host import main        # noqa: E402

main(upstream='host_compare.py',
     args=[],
     name='协议对照',
     desc='两板固件 ↔ 上位机解析器的协议对照：常量、期望向量逐字节、真解析三层。'
          '真解析那层要等上位机协议层写完（现在 honest SKIP）。')
