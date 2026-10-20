# -*- coding: utf-8 -*-
r"""「流程卡 demo」插件壳（D1）：把 `make_demo.py` 挂进工具箱侧栏（不用改一行 C++）。

真正的活在 `make_demo.py`；这里只是壳。默认跑法 = 39 张流程卡全生成 + armcc 编译校验，
报告写 `产物/流程卡demo报告.md`，democ 各一张放在 `产物/流程卡demo/`。
只生成某一张卡走命令行：`--card 22`；只看生成不编译走 `--no-compile`。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from _plugin_host import main        # noqa: E402

main(upstream='make_demo.py',
     args=['--write', '产物/流程卡demo报告.md'],
     name='流程卡 demo',
     desc='把速查里 39 张流程卡各生成一个"只含这张卡"的最小工程'
          '（头文件 + 时钟 + 调用），用 armcc 编译校验：能成的卡报通过，'
          '不能成的卡**点名缺哪个头、哪个函数/符号**（不改卡的内容）；'
          '纯讲解没有代码段的卡如实标"不生成"。',
     report='产物/流程卡demo报告.md')
