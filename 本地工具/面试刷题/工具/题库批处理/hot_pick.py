# -*- coding: utf-8 -*-
"""热点题目打分器：合并重复出现(多卷) + 热门考点关键词权重 → 选前 50% 题数。
用法：python hot_pick.py [subj|big] [N] [--mark]
"""
import json, re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

# 热门考点权重（嵌入式/C语言/C++ 面试高频）
WEIGHTS = {
    "volatile": 1.6, "static": 1.5, "const": 1.2, "指针": 1.4, "野指针": 1.5,
    "sizeof": 1.3, "大小端": 1.4, "对齐": 1.4, "malloc": 1.4, "free": 1.3,
    "内存泄漏": 1.3, "堆": 1.1, "栈": 1.2, "字符串": 1.2, "位操作": 1.4, "宏": 1.2,
    "预处理": 1.1, "函数指针": 1.4, "回调": 1.2, "结构体": 1.2, "union": 1.1,
    "枚举": 0.9, "递归": 1.1, "链表": 1.5, "队列": 1.2, "二叉树": 1.2, "排序": 1.6,
    "查找": 1.5, "二分": 1.6, "哈希": 1.2, "时间复杂度": 1.2,
    "中断": 1.6, "ISR": 1.5, "异常": 1.2, "DMA": 1.4, "看门狗": 1.3, "Bootloader": 1.2,
    "RTOS": 1.4, "任务": 1.2, "调度": 1.3, "优先级反转": 1.3, "信号量": 1.3,
    "互斥": 1.2, "临界区": 1.3, "死锁": 1.4, "状态机": 1.3, "低功耗": 1.3,
    "休眠": 1.1, "时钟": 1.1, "定时器": 1.2, "GPIO": 1.1, "PWM": 1.2, "ADC": 1.2,
    "DAC": 1.0, "串口": 1.3, "UART": 1.3, "I2C": 1.3, "SPI": 1.3, "CAN": 1.4,
    "485": 1.2, "Modbus": 1.2, "TCP": 1.2, "Socket": 1.1, "CRC": 1.2,
    "缓存": 1.2, "cache": 1.1, "MMU": 1.2, "进程": 1.4, "线程": 1.5, "多线程": 1.5,
    "同步": 1.2, "原子": 1.2, "编译": 1.1, "链接": 1.1, "段": 1.0, ".bss": 1.1,
    "启动": 1.1, "复位": 1.0, "电路": 0.9, "三极管": 0.9, "运放": 0.9, "RC": 0.8,
    "二极管": 0.8, "MOS": 0.9, "滤波": 1.0, "电平": 0.9, "总线": 1.1,
    "多态": 1.2, "虚函数": 1.3, "继承": 1.0, "拷贝": 1.2, "构造": 1.1, "析构": 1.1,
    "引用": 1.2, "模板": 1.0, "STL": 1.0, "vector": 1.0, "new": 1.3, "delete": 1.3,
    "智能指针": 1.2, "内存管理": 1.2, "文件系统": 0.9, "boot": 1.0, "IAP": 1.1,
    "OTA": 1.1, "蓝牙": 1.1, "BLE": 1.1, "WIFI": 0.9, "flash": 1.2, "EEPROM": 1.1,
    "裸机": 1.1, "前后台": 1.1, "轮询": 1.2, "竞态": 1.3, "volatile": 1.6,
}

def score(it):
    t = "\n".join([it.get("stem", ""), it.get("answer", ""),
                   it.get("analysis", ""), it.get("explain", "")])
    s = 0.0
    for kw, w in WEIGHTS.items():
        if kw in t:
            s += w
    ex = it.get("explain", "") + it.get("answer", "")
    if "合并说明" in ex or "重复题已合并" in ex:
        s += 2.0                    # 客观信号：多份试卷重复出现
    if it.get("answer", "").count("```"):
        s += 0.4                    # 代码题略加权
    return s

def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "subj"
    if which == "subj":
        bank = json.load(open(r"D:\github\面试刷题\面试题刷题软件-主观题\题库.json", encoding="utf-8"))
        N = 225
        force = []
    else:
        bank = json.load(open(r"D:\github\面试刷题\面试题刷题软件-大题\题库.json", encoding="utf-8"))
        N = 70
        force = ([409, 492] + list(range(506, 516)) +
                 [442, 449, 460, 407, 33, 264, 147, 418, 471, 485, 500, 451, 462, 470])
    scored = sorted(bank, key=lambda x: -score(x))
    picked, seen = [], set()
    for n in force:
        if any(x["num"] == n for x in bank) and n not in seen:
            picked.append(n); seen.add(n)
    for it in scored:
        if len(picked) >= N:
            break
        if it["num"] not in seen:
            picked.append(it["num"]); seen.add(it["num"])
    picked = sorted(picked)
    print(f"{which}: 总 {len(bank)} 题 → 热点 {len(picked)} 题（目标 {N}）")
    if len(sys.argv) > 2 and sys.argv[2] == "--mark":
        p = (r"D:\github\面试刷题\面试题刷题软件-主观题\题库\02_主观题.md" if which == "subj"
             else r"D:\github\面试刷题\面试题刷题软件-大题\题库\03_编程画图大题.md")
        sys.path.insert(0, r"D:\github\面试刷题\工具\题库批处理")
        from mark_hot import mark
        mark(p, picked)
    else:
        print("题号:", picked)

if __name__ == "__main__":
    main()
