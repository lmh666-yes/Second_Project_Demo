@echo off
chcp 65001 >nul
setlocal EnableExtensions
title 面试题刷题软件 · 主观题 · 更新题库

rem ============================================================
rem  更新题库工具
rem  1. 把题库文档（自带答案，*.md）放入 题库 文件夹（可放多个，用于切换）
rem  2. 双击本脚本 → 选择要使用的题库 → 自动解析并覆盖 题库.json
rem ============================================================

cd /d "%~dp0"

if not exist "%~dp0题库" (
    mkdir "%~dp0题库" >nul 2>nul
    echo [提示] 已创建 题库 文件夹，请把 Word 题库文档放进去后重新运行。
    echo.
    pause
    exit /b 0
)

rem ---- 定位并验证 Python ----
call :find_python
if not defined PY (
    echo.
    echo [错误] 未检测到可用的 Python 3 环境。
    echo        请先安装 Python 3 并勾选 "Add Python to PATH"。
    echo        下载地址：https://www.python.org/downloads/
    echo.
    pause
    exit /b 1
)

echo ================================================
echo   更新题库工具 · 主观题
echo   请把题库文档（自带答案，*.md）放入 题库 文件夹
echo ================================================
echo.
%PY% "%~dp0core\update_bank.py"
exit /b 0

rem ============================================================
rem  子程序：查找并验证可用的 Python
rem ============================================================
:find_python
set "PY="
where py >nul 2>nul && set "PY=py -3"
if defined PY (
    %PY% --version >nul 2>nul
    if errorlevel 1 set "PY="
)
if defined PY goto :eof
where python >nul 2>nul && set "PY=python"
if defined PY (
    python --version >nul 2>nul
    if errorlevel 1 set "PY="
)
if defined PY goto :eof
for %%D in (
    "%LocalAppData%\Programs\Python\Python3*\python.exe"
    "%LOCALAPPDATA%\Programs\Python\Python3*\python.exe"
    "C:\Python3*\python.exe"
    "C:\Program Files\Python3*\python.exe"
    "%ProgramFiles%\Python3*\python.exe"
    "D:\Python3*\python.exe"
    "%USERPROFILE%\Python3*\python.exe"
) do (
    if exist "%%~D" (
        set "PY="%%~D""
        goto :eof
    )
)
goto :eof
