# -*- coding: utf-8 -*-
"""全库查重扫描：给一条题干，返回三个题库里最相似的题目与相似度。

相似度 = 字符 4-gram 余弦相似度（比较前先去掉全部空白与标点，只留汉字/字母/数字）。
经验阈值：客观题 >= 0.84、主观题 >= 0.62 需人工确认是否同题。

用法：
    python _dedup_scan.py "题干文本"
    python _dedup_scan.py --file queries.txt      # 每行一条题干
    python _dedup_scan.py --top 8 "题干文本"      # 每库返回前 N 条（默认 5）
"""
import io
import math
import os
import re
import sys

BASE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BANKS = {
    "01 客观": os.path.join(BASE, "面试题刷题软件-客观题", "题库", "01_客观题_选择题.md"),
    "02 主观": os.path.join(BASE, "面试题刷题软件-主观题", "题库", "02_主观题.md"),
    "03 大题": os.path.join(BASE, "面试题刷题软件-主观题", "题库", "03_编程画图大题.md"),
}
ITEM_RE = re.compile(r"^\*\*(\d{1,4})\.\*\*\s*(.*)$")


def norm(text):
    return re.sub(r"[^0-9A-Za-z\u4e00-\u9fff]+", "", text)


def grams(text, n=4):
    t = norm(text)
    if len(t) < n:
        return {t} if t else set()
    return set(t[i:i + n] for i in range(len(t) - n + 1))


def sim(ga, gb):
    if not ga or not gb:
        return 0.0
    return len(ga & gb) / math.sqrt(len(ga) * len(gb))


def load(bank_path):
    items = []
    cur = None
    for ln in io.open(bank_path, encoding="utf-8"):
        m = ITEM_RE.match(ln.rstrip("\n"))
        if m:
            cur = {"num": int(m.group(1)), "stem": m.group(2).strip()}
            items.append(cur)
        elif cur is not None and not cur["stem"]:
            cur["stem"] = ln.strip()
    return items


def scan(query, top=5):
    gq = grams(query)
    out = []
    for name, path in BANKS.items():
        if not os.path.exists(path):
            continue
        for it in load(path):
            out.append((sim(gq, grams(it["stem"])), name, it["num"], it["stem"]))
    out.sort(key=lambda r: -r[0])
    return out[:top]


def main(argv):
    top = 5
    if "--top" in argv:
        i = argv.index("--top")
        top = int(argv[i + 1])
        del argv[i:i + 2]
    if argv and argv[0] == "--file":
        queries = [ln.strip() for ln in io.open(argv[1], encoding="utf-8") if ln.strip()]
    elif argv:
        queries = [" ".join(argv)]
    else:
        print(__doc__)
        return 0
    for q in queries:
        print("查询：" + q[:70])
        for s, name, num, stem in scan(q, top):
            print("   %.3f  [%s #%d] %s" % (s, name, num, stem[:66]))
        print("")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
