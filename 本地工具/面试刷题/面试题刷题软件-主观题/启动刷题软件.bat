@echo off
chcp 65001 >nul
setlocal EnableExtensions
title 面试题刷题软件 · 主观题 · 启动器

rem ============================================================
rem  面试题刷题软件 · 主观题 · 启动封装
rem  跨电脑移植说明：
rem    1. 整个 面试题刷题软件-主观题 文件夹拷到目标电脑（或压缩包解压）
rem    2. 目标电脑需安装 Python 3（本脚本会自动定位/引导安装）
rem  本脚本会自动定位 Python，并【验证】其真的可运行——
rem  避免 Windows 自带的“假 python 存根”导致 9009 报错。
rem ============================================================

cd /d "%~dp0"

if not exist "%~dp0core\主观题软件.py" (
    echo [错误] 未找到 主程序 core\主观题软件.py。
    echo        请确认整个 面试题刷题软件-主观题 文件夹完整解压（不要只拷单个文件）。
    echo.
    pause
    exit /b 1
)

rem ---- 若缺少题库则尝试自动生成 ----
if not exist "%~dp0题库.json" (
    echo [提示] 未找到 题库.json，正在尝试从「题库」文件夹生成题库 ...
    echo.
    call :find_python
    if not defined PY (
        echo [错误] 未找到 Python，无法自动生成题库。
        call :show_python_help
        exit /b 1
    )
    %PY% "%~dp0core\build_bank.py"
    if not exist "%~dp0题库.json" (
        echo.
        echo [错误] 题库生成失败。
        echo        请确认 题库 文件夹中存在题库文档（*.md）。
        echo.
        pause
        exit /b 1
    )
)

rem ---- 定位并验证 Python ----
call :find_python
if not defined PY (
    echo.
    echo [错误] 未检测到可用的 Python 3 环境。
    call :show_python_help
    exit /b 1
)

echo ================================================
echo    面试题刷题软件 · 主观题
echo ================================================
echo [信息] 正在启动刷题软件，请稍候 ...
echo [提示] 关闭窗口即退出程序。
echo.
%PY% "%~dp0core\主观题软件.py"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo.
    echo [错误] 程序异常退出（错误码 %RC%）。
    if "%RC%"=="9009" (
        echo        原因：Python 命令未能真正运行（可能是未安装 Python，
        echo        或 PATH 中只有 Windows 商店的“假 Python 存根”）。
        call :show_python_help
    ) else (
        echo        详情请查看上方程序输出。
        echo.
        pause
    )
    exit /b 1
)
exit /b 0

rem ============================================================
rem  子程序：查找并【验证】可用的 Python
rem  返回：PY 变量（py -3 / python / 完整路径），未找到则为空
rem ============================================================
:find_python
set "PY="

rem 1) Windows py 启动器（C:\Windows\py.exe，系统目录自带）
where py >nul 2>nul && set "PY=py -3"
if defined PY (
    %PY% --version >nul 2>nul
    if errorlevel 1 set "PY="
)
if defined PY goto :eof

rem 2) PATH 中的 python（注意：WindowsApps 里的假存根会被 --version 验证筛掉）
where python >nul 2>nul && set "PY=python"
if defined PY (
    python --version >nul 2>nul
    if errorlevel 1 set "PY="
)
if defined PY goto :eof

rem 3) 常见安装目录（未加入 PATH 的情况），用通配符自动匹配版本号
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
        goto :found_py
    )
)
goto :eof

:found_py
%PY% --version >nul 2>nul
if errorlevel 1 set "PY="
goto :eof

rem ============================================================
rem  子程序：Python 安装指引
rem ============================================================
:show_python_help
echo.
echo  本软件需要 Python 3 才能运行。
echo.
echo  方法一：官网安装（推荐）
echo    1. 打开 https://www.python.org/downloads/
echo    2. 下载 Python 3 并运行安装程序
echo    3. 安装时【务必勾选】"Add Python to PATH"
echo    4. 安装完成后重新双击本启动器即可
echo.
echo  方法二：微软商店安装
echo    开始菜单搜索 "Python"，选择 Python 3 安装即可。
echo.
echo  验证是否安装成功：
echo    打开命令行（Win+R 输入 cmd），执行  python --version
echo    能显示 Python 3.x 即表示成功。
echo.
pause
goto :eof
