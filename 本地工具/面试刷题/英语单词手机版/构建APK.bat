@echo off
chcp 65001 >nul
setlocal EnableExtensions
title 英语单词刷词 · 构建 APK

rem ============================================================
rem  一键构建 APK：
rem   1) 用 词库.json 重新生成 www\index.html 等网页
rem   2) Gradle 构建 release + debug 两个 APK（用 enword-release.jks 签名）
rem   3) 把 APK 复制到本文件夹根目录（方便发到手机）
rem  依赖：JDK 17 + Android SDK（本机已配好）
rem ============================================================

cd /d "%~dp0"

rem ---- 定位 Python ----
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY (
  where python >nul 2>nul && set "PY=python"
)
if not defined PY (
  echo [错误] 未检测到 Python 3，请先安装（勾选 Add Python to PATH）。
  pause
  exit /b 1
)

echo ================================================
echo   1/3 生成网页（词库 -^> index.html）
echo ================================================
%PY% "%~dp0build_web.py" || (echo [错误] 生成网页失败 & pause & exit /b 1)

rem ---- 定位 JDK 17 ----
set "JAVA_HOME=D:\dev-tools\jdk-17.0.20.1+1"
if not exist "%JAVA_HOME%\bin\java.exe" (
  if not defined JAVA_HOME (
    echo [提示] 未找到 %JAVA_HOME%，将尝试使用系统已配置的 JAVA_HOME。
  )
)
set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"

echo.
echo ================================================
echo   2/3 Gradle 构建 APK（首次构建较慢，请耐心等待）
echo ================================================
cd /d "%~dp0android"
call gradlew.bat assembleRelease assembleDebug --console=plain
if errorlevel 1 (
  echo.
  echo [错误] 构建失败。可尝试：先执行 gradlew.bat --stop 再重试。
  pause
  exit /b 1
)
cd /d "%~dp0"

echo.
echo ================================================
echo   3/3 复制 APK 到本文件夹
echo ================================================
copy /y "%~dp0android\app\build\outputs\apk\release\app-release.apk" "%~dp0英语单词刷词.apk" >nul 2>nul
if exist "%~dp0英语单词刷词.apk" (
  echo   已生成：英语单词刷词.apk
) else (
  copy /y "%~dp0android\app\build\outputs\apk\debug\app-debug.apk" "%~dp0英语单词刷词.apk" >nul
  echo   已生成（debug 版）：英语单词刷词.apk
)
echo.
echo 完成！把「英语单词刷词.apk」发到手机即可安装（Android 7.0 及以上）。
pause
