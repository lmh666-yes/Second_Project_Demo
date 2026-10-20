# -*- coding: utf-8 -*-
r"""「工程配置」插件壳（B1）：把 `config_audit.py` 挂进工具箱侧栏（不用改一行 C++）。

默认跑 `--check`：逐项体检板级配置（字段齐全 / 目录与 .uvprojx 存在 / 板名不重复 /
进度文档份数 / 每块固件板都能在 stage_checks 找到判据）。
`--inject`（临时塞一块假工程跑七个检查器，验"换工程只改一份 JSON"）走命令行。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from _plugin_host import main        # noqa: E402

main(upstream='config_audit.py',
     args=['--check'],
     name='工程配置',
     desc='体检板级配置（字段 / 目录 / .uvprojx / 板名 / 进度文档 / 阶段判据齐不齐）。'
          '加假工程试跑走命令行 --inject。')
