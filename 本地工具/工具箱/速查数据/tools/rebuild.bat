@echo off
chcp 936 >nul
title Rebuild - STM32 Lookup (whole chain)
setlocal EnableExtensions

rem ============================================================
rem  One command for the WHOLE chain of the STM32 lookup tool.
rem
rem  Why this file exists:
rem    the chain has 8 links and skipping one of them fails SILENTLY.
rem    It has already bitten us twice (mobile html / APK shipped stale data;
rem    once a stale dataset.json was compared against a fresh baseline).
rem    So the last link *is* a check that the whole chain really lines up.
rem
rem  Links, in order:
rem    1/8  data checks      check_std_lib_data.py + check_quality.py
rem    2/8  web regression   _build_regress.js (runs the real page engine)
rem    3/8  dataset export   export_json.js -> data\dataset.json
rem    4/8  search baseline  export_json.js --baseline -> data\search_baseline.txt
rem    5/8  toolbox build    python 工具箱\构建.py (桌面速查已并入嵌入式工具箱)
rem    6/8  toolbox selftest Toolbox.exe --selftest (must print 0 failures)
rem    7/8  mobile + APK     ..\..\..\标准库速查手机版\构建APK.bat /auto
rem    8/8  sync check       check_sync.py（网页版 <-> 手机版 <-> APK <-> dataset 逐字节对账）
rem
rem  Usage:  rebuild.bat            (full chain)
rem          rebuild.bat /nodb      (skip building the APK; 8/8 仍跑，但跳过 APK 两包)
rem ============================================================

set "HERE=%~dp0"
rem 本脚本在 工具箱\速查数据\tools\ 下（2026-10-15 从 本地工具\STM32速查\tools\ 搬进来）
set "TOOLBOX=%HERE%..\.."
set "STD=%HERE%..\..\..\标准库速查"
set "MOBILE=%HERE%..\..\..\标准库速查手机版"
set "TBOXEXE=%TOOLBOX%\程序\嵌入式工具箱\build\release\Toolbox.exe"
rem 不在这里写死 Qt / MinGW 路径：编工具箱那一步交给 工具箱\构建.py（它自己探测工具链）。
rem node / python 走系统 PATH。

echo ============================================================
echo  [1/8] data checks (python)
echo ============================================================
python "%STD%\check_std_lib_data.py"
if errorlevel 1 (
  echo     FAILED: 有需要人工确认的标识符，先看上面的 [幽灵] 列表。
  exit /b 1
)
python "%STD%\check_quality.py"
if errorlevel 1 (
  echo     FAILED: 内容质量自检（Q1~Q9）有问题，看上面的明细。
  exit /b 1
)

echo.
echo ============================================================
echo  [2/8] web engine regression
echo ============================================================
node "%STD%\_build_regress.js"
if errorlevel 1 (
  echo     FAILED: 网页引擎回归没过（板块 / 渲染 / 排序）。
  exit /b 1
)

echo.
echo ============================================================
echo  [3/8] export dataset.json
echo ============================================================
node "%HERE%export_json.js"
if errorlevel 1 exit /b 1

echo.
echo ============================================================
echo  [4/8] regenerate data\search_baseline.txt
echo ============================================================
node "%HERE%export_json.js" --baseline
if errorlevel 1 exit /b 1

echo.
echo ============================================================
echo  [5/8] toolbox build (嵌入式工具箱; 桌面速查已并入其中)
echo ============================================================
rem 构建脚本内部自己会重跑 qmake（新增头文件后不重跑会静默漏编，踩过）
python "%TOOLBOX%\构建.py"
if errorlevel 1 (
  echo     FAILED: 工具箱构建失败。
  exit /b 1
)

echo.
echo ============================================================
echo  [6/8] toolbox selftest (含 STM32 速查的数据/搜索基线断言)
echo ============================================================
rem 关键：不用管道 + findstr（中文在 GBK 控制台下匹配不上，会假报成功）
rem       直接跑，靠 exe 自己的退出码判断。
"%TBOXEXE%" --selftest
if errorlevel 1 (
  echo     FAILED: 工具箱自检有 FAIL（细节看上面几行）。
  exit /b 1
)

set "SYNCARG="
if /i "%~1"=="/nodb" set "SYNCARG=--skip-apk"

if /i NOT "%~1"=="/nodb" (
  echo.
  echo ============================================================
  echo  [7/8] mobile html + APK
  echo ============================================================
  call "%MOBILE%\构建APK.bat" /auto
  if errorlevel 1 (
    echo     FAILED: APK 构建失败。
    exit /b 1
  )
) else (
  echo.
  echo ============================================================
  echo  [7/8] mobile html + APK  --  skipped by /nodb
  echo ============================================================
)

echo.
echo ============================================================
echo  [8/8] sync check  web / mobile / APK / dataset
echo ============================================================
rem 上面这行别写成 ^<-> 那种带转义箭头的写法：cmd 会把 ^< 转义后剩下的 > 当成重定向，
rem 直接报 "> was unexpected at this time" 整段中止（踩过）。echo 行保持 ASCII 最稳。
rem 这一环治的是"漏跑一环不报错、只会静默用旧数据"的老毛病：
rem 逐字节对账，任何一环的产物与源头不一致就直接 FAIL。
rem /nodb 时 APK 本就没重打，用 --skip-apk 跳过那两包（其余照查）。
python "%HERE%check_sync.py" %SYNCARG%
if errorlevel 1 (
  echo     FAILED: 有不同步项（细节看上面几行 FAIL）。
  exit /b 1
)

:done
echo.
echo ============================================================
echo  DONE. Reminder: re-install the APK on your phone before verifying it.
echo ============================================================
endlocal
