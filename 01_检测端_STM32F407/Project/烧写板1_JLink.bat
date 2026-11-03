@echo off
chcp 936 >nul
setlocal
rem ======================================================================
rem  板1 一键烧写（绕过 Keil 的 Flash Download）
rem  原理：把 hex 复制到英文路径，再用 J-Link Commander 直接烧；
rem        Commander 打不开含中文的路径，所以必须先拷贝。
rem  用法：先在 Keil 里 Build，然后双击本文件。
rem ======================================================================
set "HEX=%~dp0Objects\标准模板.hex"
set "TMPHX=%TEMP%\b1_flash.hex"
set "SCRIPT=%TEMP%\b1_flash.jlink"
set "JLINK=E:\k5_v5\ARM\Segger\JLink.exe"

if not exist "%JLINK%" (
    echo [错误] 找不到 J-Link 命令行：%JLINK%
    pause
    exit /b 1
)
if not exist "%HEX%" (
    echo [错误] 找不到 %HEX%
    echo         请先在 Keil 里 Build 一次。
    pause
    exit /b 1
)

echo 正在拷贝 hex ...
copy /y "%HEX%" "%TMPHX%" >nul

> "%SCRIPT%" echo device STM32F407ZE
>>"%SCRIPT%" echo si SWD
>>"%SCRIPT%" echo speed 1000
>>"%SCRIPT%" echo connect
>>"%SCRIPT%" echo loadfile %TMPHX:\=/%
>>"%SCRIPT%" echo r
>>"%SCRIPT%" echo g
>>"%SCRIPT%" echo exit

echo 正在烧写（约 2 秒）...
"%JLINK%" -CommanderScript "%SCRIPT%"
echo.
echo ==========================================================
echo  看到 Program ^& Verify speed        = 写成功
echo  看到 Skipped. Contents already match = 已是最新，正常
echo  若出现 Verification failed：插上 DC 电源，再跑一次本文件
echo ==========================================================
pause
