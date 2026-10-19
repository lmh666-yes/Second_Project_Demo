# -*- coding: utf-8 -*-
"""build_data.py —— 把「标准库速查」的最新手机版单文件同步进 Android 工程。

流程：
  1) 运行 ../标准库速查/build_mobile.py（用最新数据重新打包手机版 html）
  2) 复制 标准库速查-手机版.html → android/app/src/main/assets/index.html

用法：python build_data.py（或双击 构建APK.bat，它会自动调用本脚本）
"""
import os, shutil, subprocess, sys
try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(os.path.dirname(HERE), "标准库速查")
BUILDER = os.path.join(TOOL, "build_mobile.py")
SRC = os.path.join(TOOL, "标准库速查-手机版.html")
DST = os.path.join(HERE, "android", "app", "src", "main", "assets", "index.html")

# 1) 重新打包（保证数据是最新的）
if os.path.exists(BUILDER):
    print("[1/2] 重新打包手机版 ...")
    r = subprocess.run([sys.executable, BUILDER], capture_output=True, text=True, encoding="utf-8")
    if r.stdout:
        print(r.stdout.strip())
    if r.returncode != 0:
        sys.exit("[错误] build_mobile.py 执行失败：" + (r.stderr or "").strip())
else:
    print("[提示] 未找到 build_mobile.py（跳过重新打包）")

if not os.path.exists(SRC):
    sys.exit("[错误] 未找到: " + SRC)

# 2) 同步进 Android 资源目录。
#    H3：内容与上次一样、且两个 APK 都在时，就别重写、也别让上层重打 APK ——
#    用退出码 2 告知调用方「无变化，跳过 gradle」，省掉每次全量重编（配合 display 戳：
#    数据没变时戳文件也不变，所以这里能真的命中"未变"）。退出码 0 = 有更新，1 = 出错。
import hashlib


def _sha(p):
    with open(p, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


new_h = _sha(SRC)
apks = [os.path.join(HERE, "STM32-Lookup.apk"), os.path.join(HERE, "STM32速查.apk")]
if os.path.exists(DST) and _sha(DST) == new_h and all(os.path.exists(a) for a in apks):
    print("[2/2] 手机版内容未变（%s），不重写 assets、跳过重打 APK" % new_h[:12])
    print("大小: %.0f KB" % (os.path.getsize(SRC) / 1024.0))
    sys.exit(2)

os.makedirs(os.path.dirname(DST), exist_ok=True)
shutil.copyfile(SRC, DST)
print("[2/2] 已同步到应用资源: " + DST)
print("大小: %.0f KB" % (os.path.getsize(DST) / 1024.0))
