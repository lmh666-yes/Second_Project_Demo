# -*- coding: utf-8 -*-
r"""「阶段判定」插件壳（B1）：把 `stage_status.py` 挂进工具箱侧栏（不用改一行 C++）。

默认是 dry-run（只打印判定结果）。**回写进度文档**走 `--apply` ——
手册页「项目引导」那个「判定阶段」按钮用的就是带 `--apply` 的这条路。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from _plugin_host import main        # noqa: E402

main(upstream='stage_status.py',
     args=[],
     name='阶段判定',
     desc='按代码现状判各阶段（关键 API 有没有真调用 / 文件在不在 / 桩还在不在），'
          '判不了的阶段标「需人工确认」。这里只打印；回写进度文档走 --apply'
          '（手册页的项目引导也有同一条入口）。')
