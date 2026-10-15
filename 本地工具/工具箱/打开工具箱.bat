@echo off
rem ============================================================
rem  工具箱的唯一入口：双击这个文件就打开「嵌入式工具箱」。
rem
rem  2026-10 起，工具箱里不再有独立子程序：
rem    协议工作台、项目起手助手本来就是 Toolbox.exe 里的页面，
rem    它们各自的 exe / .pro 只是重复入口，已归档删除。
rem  所以现在要打开的东西只有这一个（其余 .py 由程序内部调用）。
rem ============================================================

set "EXE=%~dp0程序\嵌入式工具箱\build\release\Toolbox.exe"

if not exist "%EXE%" (
  echo.
  echo [错误] 没找到主程序：
  echo   %EXE%
  echo.
  echo 请先编译一次（在工具箱目录下执行）：
  echo   python 构建.py
  echo.
  pause
  exit /b 1
)

start "" "%EXE%"
