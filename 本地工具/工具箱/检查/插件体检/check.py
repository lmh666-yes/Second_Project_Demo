# -*- coding: utf-8 -*-
r"""「插件体检」插件壳：把 `plugin_audit.py` 挂进工具箱侧栏（不用改一行 C++）。

真正的体检在 `plugin_audit.py`；这里只是壳。默认跑一遍并把报告写到
`产物/插件体检报告.md` —— 一页一张表，每页一行：manifest 的 `report` 跟上游脚本
真会写出的路径对不对得上、`--json` 有没有登记契约。对不对得上都能点名到"哪一页"。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from _plugin_host import main        # noqa: E402

main(upstream='plugin_audit.py',
     args=['--write', '产物/插件体检报告.md'],
     name='插件体检',
     desc='对账每个插件页：manifest 里写的 report 跟上游脚本真会写出的路径一致吗'
          '（不一致 = 「打开报告」按钮永远点不亮）、有 --json 的脚本登记进 JSON 契约表没。'
          '每条问题都点名到"哪一页 / 哪个文件 / 哪个键"。',
     report='产物/插件体检报告.md')
