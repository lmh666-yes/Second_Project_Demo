# -*- coding: utf-8 -*-
"""用法：python apply_batch.py <batch_data_file.py>
批次数据文件需定义 NEW = {题号: (题目解析, 标准答案), ...}"""
import importlib.util, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
sys.path.insert(0, r"D:\github\面试刷题\工具\题库批处理")
from migrate_lib import apply

spec = importlib.util.spec_from_file_location("batch_data", sys.argv[1])
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

MD = r"D:\github\面试刷题\面试题刷题软件-主观题\题库\02_主观题.md"
ok = apply(MD, mod.NEW)
sys.exit(0 if ok else 1)
