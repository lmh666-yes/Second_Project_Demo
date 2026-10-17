@echo off
chcp 936 >nul
title Build APK - STM32 StdPeriph Lookup
setlocal EnableExtensions

rem ============================================================
rem  One-click build for "STM32 StdPeriph Lookup" APK
rem    1) Regenerate mobile HTML + copy into Android assets
rem    2) Gradle assembleDebug (no Android Studio needed)
rem    3) Copy APK to this folder: STM32-Lookup.apk
rem
rem  Environment (same as the interview-app project):
rem    JDK 17        D:\dev-tools\jdk-17.0.20.1+1
rem    Android SDK   %LOCALAPPDATA%\Android\Sdk  (API 35)
rem ============================================================

set "JAVA_HOME=D:\dev-tools\jdk-17.0.20.1+1"
set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"

rem 传 /auto = 被别的脚本调用时不要停下来等回车（方便接进一键重建链路）
set "NOPAUSE="
if /i "%~1"=="/auto" set "NOPAUSE=1"

cd /d "%~dp0"

echo [1/3] Regenerate mobile HTML and copy into Android assets ...
python "%~dp0build_data.py"
if errorlevel 1 (
  echo     FAILED. Check that Python 3 is installed and the lookup tool folder exists.
  if not defined NOPAUSE pause & exit /b 1
)

echo [2/3] Build Debug APK with Gradle (first run may need network) ...
cd /d "%~dp0android"
call gradlew.bat clean assembleDebug --console=plain
if errorlevel 1 (
  echo     BUILD FAILED - see errors above.
  if not defined NOPAUSE pause & exit /b 1
)

echo [3/3] Copy APK to project root ...
copy /y "app\build\outputs\apk\debug\app-debug.apk" "%~dp0STM32-Lookup.apk" >nul
copy /y "app\build\outputs\apk\debug\app-debug.apk" "%~dp0STM32速查.apk" >nul

echo.
echo DONE: %~dp0STM32-Lookup.apk
echo Send this APK to your phone and install it (allow unknown sources).
if not defined NOPAUSE pause
