# -*- coding: utf-8 -*-
"""卡片渲染体检：真加载 data_flow.js / data_guide.js（走 node），逐个卡片检查代码框字段 e。

判定规则
  · 真换行     = e 里出现过真正的 0x0A，代码框能正常拆行（正常应有几十行）
  · 字面 \\n    = e 里残留「反斜杠 + n」两个字符（代码框会整段挤成一行）
  · 字面 \\r    = e 里残留「反斜杠 + r」
  · 裸 CRLF    = e 里出现真正的 0x0D（会让显示出现两个换行）

用法：
  python _card_audit.py            只报告有问题的卡
  python _card_audit.py -a         全部卡片都列出来
"""
import io, os, re, sys, json, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
B = chr(92)

JS = r"""
const fs = require('fs');
function grab(path, name) {
  const src = fs.readFileSync(path, 'utf8');
  return new Function('window', 'global', src + '\n;return ' + name + ';')({}, {});
}
console.log(JSON.stringify({
  flow:  grab(process.argv[2], 'STD_FLOW'),
  guide: grab(process.argv[3], 'STD_GUIDE')
}));
"""


def audit():
    out = []
    def w(s=""):
        out.append(s)

    js_path = os.path.join(HERE, "_audit_tmp.js")
    io.open(js_path, "w", encoding="utf-8").write(JS)
    try:
        p = subprocess.run(["node", js_path,
                            os.path.join(HERE, "data_flow.js"),
                            os.path.join(HERE, "data_guide.js")],
                           capture_output=True, text=True, encoding="utf-8")
    finally:
        try:
            os.remove(js_path)
        except OSError:
            pass
    if p.returncode != 0:
        w("[node] 加载失败：")
        w(p.stderr)
        io.open(os.path.join(HERE, "_card_audit.txt"), "w", encoding="utf-8").write("\n".join(out))
        return 1
    data = json.loads(p.stdout)

    show_all = "-a" in sys.argv
    bad = []
    rows = []
    for kind, items in (("流程", data["flow"]), ("引导", data["guide"])):
        for c in items:
            e = c.get("e") or ""
            rep = {
                "kind": kind,
                "n": c.get("n", ""),
                "steps": len(c.get("f") or []),
                "lines": e.count(chr(10)) + 1,
                "lit_n": e.count(B + "n"),
                "lit_r": e.count(B + "r"),
                "lit_crlf": e.count(B + "r" + B + "n"),
                "cr": e.count(chr(13)),
                "chars": len(e),
            }
            rows.append(rep)
            if rep["lines"] < 3 or rep["lit_n"] - rep["lit_crlf"] > 0 or rep["cr"] > 0:
                bad.append(rep)

    w("卡片渲染体检：流程 %d 张 / 引导 %d 张" % (len(data["flow"]), len(data["guide"])))
    w("%-4s %-50s %5s %7s %6s %6s %5s" % ("类", "标题", "步数", "代码行", "字面n", "字面r", "裸CR"))
    for r in (rows if show_all else bad):
        w("%-4s %-50s %5d %7d %6d %6d %5d"
          % (r["kind"], r["n"][:48], r["steps"], r["lines"], r["lit_n"], r["lit_r"], r["cr"]))
    if bad:
        w("")
        w("★ 有 %d 张卡需要看（代码框可能没换行）" % len(bad))
        rc = 1
    else:
        w("")
        w("★ 全部卡片代码框均为真换行")
        rc = 0
    io.open(os.path.join(HERE, "_card_audit.txt"), "w", encoding="utf-8").write("\n".join(out) + "\n")
    print("报告已写入 _card_audit.txt（流程 %d / 引导 %d，异常 %d）"
          % (len(data["flow"]), len(data["guide"]), len(bad)))
    return rc


if __name__ == "__main__":
    sys.exit(audit())
