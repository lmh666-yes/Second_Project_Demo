# -*- coding: utf-8 -*-
r"""「代码索引」插件壳（B1）：把 `code_index.py` 挂进工具箱侧栏（不用改一行 C++）。

真正的检查在 `code_index.py`；这里只是壳。默认跑法 = **板1 的 API 使用索引**。
反查 / 签名校验 / 数据流图走命令行（`--where` / `--signatures` / `--flow --svg`）。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from _plugin_host import main        # noqa: E402

main(upstream='code_index.py',
     args=['--index', '--board', '板1', '--write', '产物/API索引_板1.md', '--usage'],
     name='代码索引',
     desc='扫你写的 .c，列出每个库函数用在哪几行、手册有没有它的条目 '
          '（分清"有自己条目 / 只在别的卡正文里被提到 / 完全没提"）。'
          '默认跑板1 的使用索引；反查 / 签名校验 / 数据流图走命令行。'
          '同一页还跑 --usage —— 扫板1 + 板2 生成「条目名 → 使用位置」'
          '（速查数据/data/code_usage.json），手册页据此显示「你在 板1/main.c:123 用了它」。',
     report='产物/API索引_板1.md')
