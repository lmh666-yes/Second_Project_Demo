@echo off
rem ================================================================
rem  project_check.bat -- run project_check.ps1 (double-click me)
rem  The report will be written to 检查报告.md in this folder.
rem ================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0project_check.ps1"
echo.
pause
