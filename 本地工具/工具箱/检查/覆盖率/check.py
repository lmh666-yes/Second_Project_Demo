# -*- coding: utf-8 -*-
r"""「覆盖率看板」插件壳（B1）：把 `coverage_board.py` 挂进工具箱侧栏（不用改一行 C++）。

默认真跑每个脚本的 `--selftest`，统计断言数与通过 / 失败，并把看板写到
`产物/覆盖率看板.md` —— 防"自检条数看着不少，关键判定其实没测"。
带变异测试跑走命令行 `--with-mutation`。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from _plugin_host import main        # noqa: E402

main(upstream='coverage_board.py',
     args=['--write', '产物/覆盖率看板.md'],
     name='覆盖率看板',
     desc='真跑每个脚本的 --selftest，一屏看断言数与通过 / 失败'
          '（防"自检条数虚高、关键判定没测"）。带变异测试跑走 --with-mutation。',
     report='产物/覆盖率看板.md')
