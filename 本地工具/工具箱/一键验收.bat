@echo off
chcp 936 >nul
title 一键验收 - 嵌入式工具箱
setlocal EnableExtensions
set HERE=%~dp0
set PYEXE=python
where python >nul 2>nul || set PYEXE=py -3
echo 直接双击 = 快档；命令行加 --full = 全档；再加 --only/--skip 可只跑某几步。
echo.
%PYEXE% "%HERE%检查\一键验收\verify_all.py" %*
set RC=%ERRORLEVEL%
echo.
echo 退出码 %RC% （0=全绿；1=有红的步骤；报告在 检查\一键验收\产物\）
pause
