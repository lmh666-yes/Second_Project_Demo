# -*- coding: utf-8 -*-
"""抽查三端 APK 内嵌 assets/index.html 是否为新版（热点/标准答案/题量）。
位置：面试刷题\\工具\\check_apk.py（按脚本位置相对定位）"""
import sys, io, zipfile, os
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # → 面试刷题/
ITEMS = [
    ("obj",  os.path.join("面试题手机版-客观题", "面试刷题-客观题.apk")),
    ("subj", os.path.join("面试题手机版-主观题", "面试刷题-主观题.apk")),
    ("bigq", os.path.join("面试题手机版-大题", "面试刷题-大题.apk")),
]
for tag, rel in ITEMS:
    p = os.path.join(BASE, rel)
    z = zipfile.ZipFile(p)
    info = z.getinfo("assets/index.html")
    data = z.read("assets/index.html").decode("utf-8", "ignore")
    print(tag, "| apk", os.path.getsize(p), "B | asset", info.file_size, "B",
          "| hot", data.count('"hot": true'),
          "| 标准答案", data.count("标准答案"),
          "| kinds", data.count('"kind":'),
          "| 热点tab", data.count('data-mode="热点"'))
