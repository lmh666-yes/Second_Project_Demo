# -*- coding: utf-8 -*-
r"""check_sync.py —— 校验「网页版 ↔ 手机版 ↔ APK ↔ 桌面 dataset」是否严格同步。

为什么需要（历史病）：这条链的每一环漏跑都**不会报错，只会静默用旧数据**——
手机上装的是旧包、桌面读的是旧 dataset，而所有自检都是绿的。所以把"同步"本身也做成可查的。

用法：
    python check_sync.py              # 全查（含 APK）
    python check_sync.py --skip-apk   # 跳过 APK 两包（rebuild.bat /nodb 用：APK 本就没重打）
    python check_sync.py -v           # 多打几行细节

退出码 0 = 全同步；1 = 有不同步项（会逐条列出）。
本脚本只读，不改任何文件（"重导 dataset 比内容"那一步会先备份再还原）。
"""
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))          # 工具箱/速查数据/tools
DATA = os.path.dirname(HERE)                               # 工具箱/速查数据
TOOLBOX = os.path.dirname(DATA)                            # 工具箱
LOCALTOOLS = os.path.dirname(TOOLBOX)                      # 本地工具

STD = os.path.join(LOCALTOOLS, "标准库速查")               # 网页版（唯一数据源）
MOB = os.path.join(LOCALTOOLS, "标准库速查手机版")         # 手机链
WEB = os.path.join(STD, "标准库函数速查.html")
MOBHTML = os.path.join(STD, "标准库速查-手机版.html")
ASSETS = os.path.join(MOB, "android", "app", "src", "main", "assets", "index.html")
DSET = os.path.join(DATA, "data", "dataset.json")
EXPORT = os.path.join(HERE, "export_json.js")
STAMP = os.path.join(STD, "data_build_info.js")   # H2：三端页脚数据版本戳（export_json.js 生成）
APKS = [os.path.join(MOB, "STM32-Lookup.apk"), os.path.join(MOB, "STM32速查.apk")]

SKIP_APK = "--skip-apk" in sys.argv
VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv

# 判"某个页面有没有打手机版标记"必须看 <html> 标签本身：页面里的 CSS/JS 引用了
# 同一个字符串（html[data-platform="mobile"]），用 `in` 判断会误判（踩过）。
MARK_RE = re.compile(r'<html[^>]*data-platform="mobile"')
FLAG = "<script>window.__MOBILE__ = true;</script>"

fails = []
skips = []
passes = []


def chk(ok, what, detail=""):
    print(("PASS " if ok else "FAIL ") + what + (("  " + detail) if detail else ""))
    if ok:
        passes.append(what)
    else:
        fails.append(what)
    return ok


def skip(what, why=""):
    print("SKIP " + what + (("  " + why) if why else ""))
    skips.append(what)


def sha(path):
    with io.open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def exists(p):
    return os.path.exists(p)


def main():
    for p, what in ((WEB, "网页版主页"), (MOBHTML, "手机版单文件"), (ASSETS, "Android 资源页")):
        if not exists(p):
            chk(False, "找得到" + what, p)
            print("\n缺关键文件，终止。")
            return 1

    print("=== 1. 网页版引用的数据文件 ===")
    web = io.open(WEB, encoding="utf-8").read()
    js_files = re.findall(r'<script src="(data_[^"]+\.js)"></script>', web)
    chk(len(js_files) > 0, "主页里抓得到 data_*.js 引用（%d 个）" % len(js_files))
    missing = [j for j in js_files if not exists(os.path.join(STD, j))]
    chk(not missing, "引用的数据文件都在", "缺: " + ", ".join(missing))
    if VERBOSE:
        for j in js_files:
            print("      %s  %.0f KB" % (j, os.path.getsize(os.path.join(STD, j)) / 1024.0))

    print()
    print("=== 2. 手机版单文件内联的是同一份数据 ===")
    mob = io.open(MOBHTML, encoding="utf-8").read()
    left = [j for j in js_files if ('<script src="%s"></script>' % j) in mob]
    chk(not left, "手机版里没有残留未内联的 data 引用", "残留: " + ", ".join(left))
    bad = []
    for j in js_files:
        code = io.open(os.path.join(STD, j), encoding="utf-8").read()
        if code.replace("</script", "<\\/script") not in mob:
            bad.append(j)
    chk(not bad, "每个数据文件内容都原样进了手机版", "不符: " + ", ".join(bad))
    chk(bool(MARK_RE.search(mob)), "手机版 <html> 带 mobile 标记")
    chk(FLAG in mob, "手机版注入了 window.__MOBILE__ 开关")
    chk(not MARK_RE.search(web), "网页版没被误打 mobile 标记")
    chk(FLAG not in web, "网页版没被误注入手机开关")

    print()
    print("=== 2b. 页脚数据版本戳（H2：三端显示「数据版本 + 导出时间」） ===")
    chk(exists(STAMP), "数据版本戳 data_build_info.js 在", STAMP)
    if exists(STAMP):
        st = io.open(STAMP, encoding="utf-8").read()
        m = re.search(r'window\.__BUILD_INFO__\s*=\s*(\{[^}]*\})', st)
        ver = tim = None
        okfmt = bool(m)
        if okfmt:
            try:
                info = json.loads(m.group(1))
                ver, tim = info.get("version"), info.get("time")
                okfmt = bool(ver and ver != "未标注" and tim)
            except Exception:
                okfmt = False
        chk(okfmt, "戳内容可解析且非占位（version + time）",
            "version=%s time=%s" % (ver, tim))
        # 手机版内联的就是同一份戳文件内容，所以版本串必须能在手机版里找到。
        chk(bool(ver) and (ver in mob), "手机版里带着同一版本戳", "version=%s" % ver)

    print()
    print("=== 3. 手机版单文件 → Android 资源 → APK（逐字节） ===")
    h_mob, h_assets = sha(MOBHTML), sha(ASSETS)
    chk(h_mob == h_assets, "手机版单文件与 android/assets/index.html 逐字节相同",
        "%s / %s" % (h_mob[:12], h_assets[:12]))
    for apk in APKS:
        name = os.path.basename(apk)
        if not exists(apk):
            chk(False, "APK 存在：" + name)
            continue
        if SKIP_APK:
            skip("APK 内容核对：" + name, "（--skip-apk：重打包时没打 APK）")
            continue
        with zipfile.ZipFile(apk) as z:
            cand = [n for n in z.namelist() if n.endswith("assets/index.html")]
            if not chk(len(cand) == 1, "%s 里有 assets/index.html" % name, ", ".join(cand)):
                continue
            data = z.read(cand[0])
            h = hashlib.sha256(data).hexdigest()
            chk(h == h_mob, "%s 内的页面就是当前手机版" % name, "%s / %s" % (h[:12], h_mob[:12]))
            t = data.decode("utf-8", "replace")
            chk(bool(MARK_RE.search(t)) and FLAG in t, "%s 里是手机版（标记 + 开关都在）" % name)

    print()
    print("=== 4. 桌面 dataset.json 与网页源是否同内容 ===")
    if not exists(EXPORT):
        chk(False, "找得到导出脚本", EXPORT)
    elif not exists(DSET):
        chk(False, "找得到 dataset.json", DSET)
    else:
        bak = DSET + ".syncbak"
        shutil.copyfile(DSET, bak)
        # H2：重跑 export 会顺手刷新数据版本戳的时间 —— 先备好，末尾还原，本脚本仍"只读"。
        sbak = STAMP + ".syncbak"
        had_stamp = exists(STAMP)
        if had_stamp:
            shutil.copyfile(STAMP, sbak)
        try:
            r = subprocess.run(["node", EXPORT], capture_output=True, text=True,
                               encoding="utf-8", cwd=HERE)
            ok_run = chk(r.returncode == 0, "重跑 export_json.js 成功",
                         (r.stderr or "").strip()[:200])
            if ok_run:
                ja = json.loads(io.open(bak, encoding="utf-8").read())
                jb = json.loads(io.open(DSET, encoding="utf-8").read())
                ma, mb = dict(ja["meta"]), dict(jb["meta"])
                ma.pop("generatedAt", None)
                mb.pop("generatedAt", None)
                same = (ja["all"] == jb["all"] and ja["search"] == jb["search"]
                        and ja["proj"] == jb["proj"] and ma == mb)
                # 不能比字节：meta.generatedAt 是导出时间戳，每次都不同（比字节会误判不同步）
                chk(same, "重导结果与现有 dataset.json 内容一致（%d 条）" % len(jb["all"]))
                if VERBOSE:
                    print("      仅 meta.generatedAt 不同（时间戳）：%s → %s"
                          % (ja["meta"].get("generatedAt"), jb["meta"].get("generatedAt")))
        finally:
            shutil.copyfile(bak, DSET)     # 还原：本脚本只读不写
            os.remove(bak)
            if had_stamp:
                shutil.copyfile(sbak, STAMP)
                os.remove(sbak)

    print()
    print("=== 小结 ===")
    print("同步检查：%d 项通过 / %d 项失败 / %d 项跳过"
          % (len(passes), len(fails), len(skips)))
    for f in fails:
        print("  FAIL " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
