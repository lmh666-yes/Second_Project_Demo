@echo off
chcp 65001 >nul
rem 三端手机版 APK 全量重建（clean，防增量构建空洞）
rem 位置：面试刷题\工具\build_all_apks.bat（按脚本位置相对定位，仓库整体搬走也能用）
setlocal EnableExtensions
set "JAVA_HOME=D:\dev-tools\jdk-17.0.20.1+1"
set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"
set "ROOT=%~dp0.."
set "DONE=D:\dev-tools\apk_build_status.txt"
echo BEGIN %date% %time% > "%DONE%"

rem ---- 1) 客观题 ----
echo [1/3] 客观题 ...
cd /d "%ROOT%\面试题手机版-客观题\android"
call gradlew.bat clean assembleDebug --console=plain > ..\apk_build_obj.log 2>&1
if errorlevel 1 ( echo OBJ-FAIL >> "%DONE%" & goto :eof )
copy /y "app\build\outputs\apk\debug\app-debug.apk" "..\面试刷题-客观题.apk" >nul
echo OBJ-OK >> "%DONE%"

rem ---- 2) 主观题 ----
echo [2/3] 主观题 ...
cd /d "%ROOT%\面试题手机版-主观题\android"
call gradlew.bat clean assembleDebug --console=plain > ..\apk_build_subj.log 2>&1
if errorlevel 1 ( echo SUBJ-FAIL >> "%DONE%" & goto :eof )
copy /y "app\build\outputs\apk\debug\app-debug.apk" "..\面试刷题-主观题.apk" >nul
echo SUBJ-OK >> "%DONE%"

rem ---- 3) 大题 ----
echo [3/3] 大题 ...
cd /d "%ROOT%\面试题手机版-大题\android"
call gradlew.bat clean assembleDebug --console=plain > ..\apk_build_bigq.log 2>&1
if errorlevel 1 ( echo BIGQ-FAIL >> "%DONE%" & goto :eof )
copy /y "app\build\outputs\apk\debug\app-debug.apk" "..\面试刷题-大题.apk" >nul
echo BIGQ-OK >> "%DONE%"

echo ALL-DONE >> "%DONE%"
