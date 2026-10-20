# -*- coding: utf-8 -*-
r"""「平台清单」插件壳（B4）：把 `platform_manifest.py` 挂进工具箱侧栏（不用改一行 C++）。

一页列出全部**插件页 / 闸门脚本**：分组、入口、自检断言数、有没有进闸门、变异测试覆盖到没到。
数字全部现场取（读 manifest、解析 `main.cpp`、真跑 `--selftest`）—— 所以"清单 = 现状"，
不靠翻文档。默认跑一遍并写 `产物/平台清单.md`；带变异测试跑走命令行 `--with-mutation`。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from _plugin_host import main        # noqa: E402

main(upstream='platform_manifest.py',
     args=['--write', '产物/平台清单.md'],
     name='平台清单',
     desc='一页看全平台家底：哪些插件页 / 闸门脚本、自检几条通过、谁进了闸门、'
          '谁有变异覆盖。数字现场取（读 manifest、解析 main.cpp、真跑 --selftest），不写死。'
          '带变异测试跑走 --with-mutation；只看结构不跑走 --no-run。',
     report='产物/平台清单.md')
