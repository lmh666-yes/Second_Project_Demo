# -*- coding: utf-8 -*-
"""
build_mobile.py —— 打包「手机版」单文件 HTML（内联全部数据，离线可用）。

用法：python build_mobile.py
输出：标准库速查-手机版.html（把 标准库函数速查.html + 全部 data*.js 合并成一个文件）

手机使用：
  1. 把生成的 标准库速查-手机版.html 传到手机（微信文件传输助手 / 数据线 / 云盘）；
  2. 手机上用浏览器打开该文件即可（无需网络、无需其他文件）。
数据更新后：重新跑一遍本脚本，再传一次新文件。
"""
import io, os, re, sys
try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "标准库函数速查.html")
OUT = os.path.join(HERE, "标准库速查-手机版.html")

html = io.open(SRC, encoding="utf-8").read()

# 从主页面里自动读出所有 <script src="data_*.js">，
# 避免"手工维护文件名列表"——以前就是漏了 data_flow.js，
# 导致手机版/APK 静默丢掉整个「初始化流程卡」板块。
js_files = re.findall(r'<script src="(data_[^"]+\.js)"></script>', html)
if not js_files:
    sys.exit("[错误] 主页面里没找到任何 data_*.js 引用，请检查 标准库函数速查.html")

print("将内联 %d 个数据文件：" % len(js_files))
missing = []
for js in js_files:
    p = os.path.join(HERE, js)
    if not os.path.exists(p):
        missing.append(js)
        print("  ✘ 文件不存在: " + js)
        continue
    code = io.open(p, encoding="utf-8").read()
    code = code.replace("</script", "<\\/script")   # 防提前闭合
    tag = '<script src="%s"></script>' % js
    if tag not in html:
        print("  ✘ 主页面里找不到标签: " + tag)
        continue
    html = html.replace(tag, "<script>\n" + code + "\n</script>")
    print("  %s (%.0f KB)" % (js, os.path.getsize(p) / 1024.0))

if missing:
    sys.exit("[错误] 缺少数据文件，未生成手机版：" + ", ".join(missing))

# 自检：不应再残留任何 <script src="data_
left = re.findall(r'<script src="data_[^"]+"></script>', html)
if left:
    sys.exit("[错误] 仍有未内联的数据引用：" + ", ".join(left))

# 3) 打手机版标记。
#    网页版（桌面浏览器打开）不带这个属性，所以页面里那些
#    html[data-platform="mobile"] 的手机专用样式/脚本只影响手机页与 APK，不会改网页版的样子。
#    注意：判断"是否已打过标记"必须看**<html> 标签本身**——页面里的 CSS/JS 引用了同一个字符串，
#    直接 in 判断会误判成已标记（这坑踩过一次）。
MARK = 'data-platform="mobile"'
MARK_RE = re.compile(r'<html[^>]*data-platform="mobile"')
if not MARK_RE.search(html):
    before = html
    html = html.replace('<html lang="zh-CN" data-theme="dark">',
                        '<html lang="zh-CN" data-theme="dark" ' + MARK + '>', 1)
    if html == before:
        sys.exit('[错误] 没找到 <html> 标签，无法打手机版标记')
    print("已给 <html> 打上 " + MARK)
else:
    print("<html> 已带 " + MARK + "（跳过）")

# 开关常量：脚本里用 window.__MOBILE__ 判断；必须在引擎脚本之前注入。
# 判断也用完整注入串（页面里的 `if (window.__MOBILE__)` 守卫不含 "= true"，不会误判）。
FLAG = '<script>window.__MOBILE__ = true;</script>'
if FLAG not in html:
    html = html.replace('<head>', '<head>\n' + FLAG, 1)
    print("已注入 window.__MOBILE__ = true")
else:
    print("window.__MOBILE__ 已注入（跳过）")

io.open(OUT, "w", encoding="utf-8", newline="").write(html)
print("\n已生成: " + OUT)
print("文件大小: %.0f KB" % (os.path.getsize(OUT) / 1024.0))
