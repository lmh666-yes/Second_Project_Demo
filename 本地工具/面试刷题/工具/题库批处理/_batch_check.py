# -*- coding: utf-8 -*-
import importlib.util
spec = importlib.util.spec_from_file_location("batch", r"D:\github\面试刷题\工具\题库批处理\batch_A_data.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
expected = [27, 28, 30, 31, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 52, 53, 54, 55, 56, 57, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72]
assert sorted(mod.NEW.keys()) == expected, "keys mismatch"
bad = []
for k, (a, b) in mod.NEW.items():
    for name, s in (("ana", a), ("ans", b)):
        if "\n" in s or "\r" in s:
            bad.append((k, name, "newline"))
        if "**" in s:
            bad.append((k, name, "double-star"))
        if s.strip() != s:
            bad.append((k, name, "whitespace"))
print("entries:", len(mod.NEW))
print("bad:", bad)
