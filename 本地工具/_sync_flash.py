# -*- coding: utf-8 -*-
"""把板2 刚改好的 sys_flash.c / .h 同步到板1。

为什么能整文件同步：这两个文件在两板之间只差"型号注释"（ZG/ZE、1MB/512KB），
实现完全一致（实测逐段相同）。同步后把型号字样换回板1 的说法。
"""
import io, os, sys
sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B1 = os.path.join(ROOT, "EnvMonitor", "Project_1_检测端", "Project", "FWLIB")
B2 = os.path.join(ROOT, "EnvMonitor", "Project_2_数据通信端", "Project", "FWLIB")

JOBS = [
    (r"src\sys_flash.c", [("STM32F407ZG，1MB", "STM32F407ZE，512KB")]),
    (r"inc\sys_flash.h", [("F407ZG", "F407ZE"), ("1MB 型号", "512KB 型号"),
                          # 板1 是 512KB（F407ZE），Flash 只到 0x0807FFFF：
                          # 参数区两个地址必须换成本板真实存在的扇区，
                          # 直接用板2 的 0x080E0000 / 0x080C0000 会写到不存在的地址。
                          ("0x080E0000UL", "0x08070000UL"),
                          ("0x080C0000UL", "0x08060000UL")]),
]

for rel, subs in JOBS:
    src = os.path.join(B2, rel)
    dst = os.path.join(B1, rel)
    t = io.open(src, encoding="utf-8", newline="").read()
    for a, b in subs:
        n = t.count(a)
        t = t.replace(a, b)
        print("  %-28s %-22s -> %-22s  %d 处" % (rel, a, b, n))
    io.open(dst, "w", encoding="utf-8", newline="").write(t)
    print("写入 %s (%d 字节)" % (dst, len(t)))

# 复核：两文件去掉型号字样后应逐字节相同
for rel, subs in JOBS:
    a = io.open(os.path.join(B1, rel), encoding="utf-8", newline="").read()
    b = io.open(os.path.join(B2, rel), encoding="utf-8", newline="").read()
    for x, y in subs:
        a = a.replace(y, "@@")
        b = b.replace(x, "@@")
    print("%-28s 等价: %s" % (rel, a == b))
