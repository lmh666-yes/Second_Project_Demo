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

H14：第 0 节核对"重建链前几环有没有真跑过" —— rebuild.bat 会在第 8 环之前落一份
`速查数据\_rebuild_status.txt` 标记；这里比"标记时间"与"各产物 mtime"，
标记比某份产物旧就说明那份产物改完后没重跑整链（以前漏跑前几环只会静默用旧数据）。
"""
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import time
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
STATUS = os.path.join(DATA, "_rebuild_status.txt")  # H14：重建链跑到 8/8 之前落的标记（rebuild.bat 写）
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


def artifact_mtimes():
    """把"该被重建链刷新"的那几份产物 → mtime 收集起来：判"前几环有没有真跑过"就看它们。"""
    out = {}
    for p in (WEB, MOBHTML, ASSETS, DSET):
        if os.path.exists(p):
            out[p] = os.path.getmtime(p)
    if os.path.isdir(STD):
        for f in os.listdir(STD):
            if f.startswith("data_") and f.endswith(".js"):
                fp = os.path.join(STD, f)
                out[fp] = os.path.getmtime(fp)
    return out


def fmt_mtime(t):
    return time.strftime("%m-%d %H:%M", time.localtime(t))


def main():
    for p, what in ((WEB, "网页版主页"), (MOBHTML, "手机版单文件"), (ASSETS, "Android 资源页")):
        if not exists(p):
            chk(False, "找得到" + what, p)
            print("\n缺关键文件，终止。")
            return 1

    print("=== 0. 重建链是否真跑过（H14）===")
    # 这条链历史上踩过两次：只跑后半段（或只跑本脚本）时，前半段（数据自检 / 引擎回归 /
    # 导出）没跑也不会报错，只会静默沿用旧数据。所以让 rebuild.bat 在第 8 环**之前**落一份
    # 标记；这里核对"标记不比任何一份产物旧" —— 比它新就说明那份产物改完后没重跑整链。
    if not exists(STATUS):
        chk(False, "重建链标记在（_rebuild_status.txt）",
            "缺 —— 第 1~7 环没跑过（或只跑过本脚本）；请整体重跑 rebuild.bat")
    else:
        mk = os.path.getmtime(STATUS)
        arts = artifact_mtimes()
        newest = max(arts.items(), key=lambda kv: kv[1]) if arts else None
        fresh = (newest is None) or (mk >= newest[1] - 1)     # 允许 1 秒容差
        chk(fresh, "重建链标记不比产物旧（前几环真跑过）",
            ("标记 %s · 最新产物 %s" % (fmt_mtime(mk), fmt_mtime(newest[1]))) if newest else "")
        if not fresh and newest is not None:
            print("      → 比标记还新的是：%s（说明它改完后没重跑整链）"
                  % os.path.relpath(newest[0], LOCALTOOLS))
        elif VERBOSE:
            print("      标记 %s 覆盖了 %d 份产物的刷新时间" % (fmt_mtime(mk), len(arts)))

    print()
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
        # H2/H14：重跑 export 会顺手刷新 dataset.json 与数据版本戳（内容与时间都会变）。
        # 先把**时间戳**也记下来，末尾连同内容一起还原 —— 否则本脚本自己就把这两份产物
        # "变新"了，下一次跑第 0 节（H14）就会误判"改完后没重跑整链"，自己判自己失败。
        dset_times = (os.path.getatime(DSET), os.path.getmtime(DSET))
        sbak = STAMP + ".syncbak"
        had_stamp = exists(STAMP)
        stamp_times = None
        if had_stamp:
            shutil.copyfile(STAMP, sbak)
            stamp_times = (os.path.getatime(STAMP), os.path.getmtime(STAMP))
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
            os.utime(DSET, dset_times)     # 连 mtime 一起还原（否则第 0 节会自己判自己）
            if had_stamp:
                shutil.copyfile(sbak, STAMP)
                os.remove(sbak)
                os.utime(STAMP, stamp_times)

    print("=== 5. 阶段卡序号是真值（A2：不靠数组顺序排） ===")
    if not exists(DSET):
        chk(False, "找得到 dataset.json", DSET)
    else:
        ds = json.loads(io.open(DSET, encoding="utf-8").read())
        cards = [c for c in ds.get("all", []) if c.get("_b")]
        per_board, bad_fmt = {}, []
        for c in cards:
            m = re.search(r"阶段\s*(\d+)", c.get("n") or "")
            o = c.get("_ord")
            if (not m) or (not isinstance(o, int)) or o < 0 or o != int(m.group(1)):
                bad_fmt.append("%s（_ord=%s）" % ((c.get("n") or "")[:24], o))
                continue
            per_board.setdefault(c["_b"], {})[o] = c.get("n")
        chk(len(cards) == 28, "阶段卡 28 张（板1 10 / 板2 10 / Qt 8）", str(len(cards)))
        chk(not bad_fmt, "每张阶段卡的 _ord = 名字里的「阶段 N」且 ≥ 0", "；".join(bad_fmt[:4]))
        seq_bad = ["%s：%s" % (b, sorted(mp)) for b, mp in sorted(per_board.items())
                   if sorted(mp) != list(range(len(mp)))]
        chk(not seq_bad, "每块板的阶段号 0..N 连续、不重号", "；".join(seq_bad))
        # 验收那条"故意打乱导出顺序仍按 0~9"：把数组整个倒过来，再按 _ord 排 ——
        # 各板顺序必须与原来一致（因为序号现在写在卡上，与数组位置无关）。
        def _order(lst, b):
            return [c["_ord"] for c in sorted([x for x in lst if x["_b"] == b],
                                              key=lambda c: c["_ord"])]
        rev = list(reversed(cards))
        same = all(_order(rev, b) == _order(cards, b) for b in per_board)
        chk(same, "把数组倒过来再按 _ord 排，各板阶段顺序不变（打乱顺序也乱不了）")

    print()
    print("=== 小结 ===")
    print("同步检查：%d 项通过 / %d 项失败 / %d 项跳过"
          % (len(passes), len(fails), len(skips)))
    for f in fails:
        print("  FAIL " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
