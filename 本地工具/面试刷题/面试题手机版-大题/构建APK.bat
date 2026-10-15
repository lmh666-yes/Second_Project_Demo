@echo off
chcp 65001 >nul
title 构建 APK · 面试刷题 · 大题
setlocal EnableExtensions

rem ============================================================
rem  一键构建「面试刷题 · 大题」APK
rem    1) 重新生成内嵌题库（读取 桌面版 题库.json）
rem    2) 用 Gradle 构建 debug APK（无需 Android Studio）
rem    3) 把 APK 复制到本文件夹：面试刷题-大题.apk
rem
rem  环境（本机已配好；换电脑请改下面两行）：
rem    · JDK 17        D:\dev-tools\jdk-17.0.20.1+1
rem    · Android SDK   %LOCALAPPDATA%\Android\Sdk（需含 API 35 平台）
rem ============================================================

set "JAVA_HOME=D:\dev-tools\jdk-17.0.20.1+1"
set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"

cd /d "%~dp0"

echo [1/3] 重新生成内嵌题库 ...
python "%~dp0build_web.py"
if errorlevel 1 (
  echo     失败：请确认已安装 Python 3、且 桌面版 题库.json 存在
  pause & exit /b 1
)

echo [2/3] 构建 Debug APK（首次需联网下载依赖，可能较慢）...
cd /d "%~dp0android"
call gradlew.bat clean assembleDebug --console=plain
if errorlevel 1 (
  echo     构建失败：请查看上方错误信息
  pause & exit /b 1
)

echo [3/3] 复制 APK 到本文件夹 ...
copy /y "app\build\outputs\apk\debug\app-debug.apk" "..\面试刷题-大题.apk" >nul

echo.
echo 构建成功：%~dp0面试刷题-大题.apk
echo 把这个 APK 发到手机（微信/QQ/网盘/数据线）直接安装即可。
echo 首次安装需允许"安装未知来源应用"。
pause
