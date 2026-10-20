# -*- coding: utf-8 -*-
r"""「报告生成」插件壳（B1）：把 `report_gen.py` 挂进工具箱侧栏（不用改一行 C++）。

默认输出固定写 `产物/周报.html`（现场跑 7 个检查器的 --json + 阶段进度 + 体积
+ 代码规模 + 时序图 + 自检覆盖），点页上的「打开报告」即可看；CSS/SVG 全内联，无外链。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from _plugin_host import main        # noqa: E402

main(upstream='report_gen.py',
     args=['--out', '产物/周报.html'],
     name='报告生成',
     desc='一键出单文件 HTML 周报 / 答辩包：体检结论 + 阶段表 + 体积趋势折线 + 代码规模 '
          '+ 内联 SVG 时序图 + **内联界面截图** + 自检覆盖，双击能看、无外链依赖'
          '（固定写 产物/周报.html）。要"能直接发人的包"走命令行：--pack（目录 + zip，'
          '含截图原图与 明细/ 各检查报告）。',
     report='产物/周报.html')
