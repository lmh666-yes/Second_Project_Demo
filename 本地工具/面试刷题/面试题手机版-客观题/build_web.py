# -*- coding: utf-8 -*-
"""从桌面版题库 生成 手机版 index.html（题库 + 题目配图全部内嵌，单文件离线可用）。

用法：python build_web.py
读取：../面试题刷题软件-客观题/题库.json + ../面试题刷题软件-客观题/imgs + www/template.html
输出：www/index.html + 根目录「面试刷题-客观题.html」+ android/app/src/main/assets/index.html
"""
import sys, io, os, json, base64
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

HERE = os.path.dirname(os.path.abspath(__file__))
DESK = os.path.join(os.path.dirname(HERE), "面试题刷题软件-客观题")
SRC = os.path.join(DESK, "题库.json")
IMG_DIR = os.path.join(DESK, "imgs")
TEMPLATE = os.path.join(HERE, "www", "template.html")
OUT = os.path.join(HERE, "www", "index.html")
FRIENDLY = os.path.join(HERE, "面试刷题-客观题.html")
APK_OUT = os.path.join(HERE, "android", "app", "src", "main", "assets", "index.html")


def embed_images(bank):
    """把 imgs 文件名列表替换为 base64 data URI（保持单文件离线可用）"""
    n = 0
    for it in bank:
        names = it.get("imgs") or []
        uris = []
        for name in names:
            p = os.path.join(IMG_DIR, name)
            if os.path.exists(p):
                ext = os.path.splitext(name)[1].lower()
                mime = "image/png" if ext == ".png" else "image/jpeg"
                with open(p, "rb") as f:
                    uris.append("data:%s;base64,%s" % (mime, base64.b64encode(f.read()).decode()))
            else:
                print("  [缺图]", name)
        it["imgs"] = uris
        n += len(uris)
    return n


def main():
    if not os.path.exists(SRC):
        sys.exit("[错误] 未找到桌面版题库: " + SRC)
    if not os.path.exists(TEMPLATE):
        sys.exit("[错误] 未找到模板: " + TEMPLATE)
    bank = json.load(open(SRC, encoding="utf-8"))
    img_n = embed_images(bank)
    html = open(TEMPLATE, encoding="utf-8").read()
    js = "const BANK=" + json.dumps(bank, ensure_ascii=False) + ";"
    if "/*__BANK__*/" not in html:
        sys.exit("[错误] 模板缺少题库占位符 /*__BANK__*/")
    html = html.replace("/*__BANK__*/", js)
    for path in (OUT, FRIENDLY, APK_OUT):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(html)
    cnt = {}
    for it in bank:
        cnt[it["kind"]] = cnt.get(it["kind"], 0) + 1
    print("已生成:")
    print("  ", OUT)
    print("  ", FRIENDLY, "（根目录友好名，发这个到手机更好找）")
    print("  ", APK_OUT)
    print("题库:", {k: v for k, v in cnt.items()}, "共", len(bank), "题；配图", img_n, "张")


if __name__ == "__main__":
    main()
