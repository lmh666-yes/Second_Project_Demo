@echo off
rem ================================================================
rem  build_keil.bat -- one-click rebuild for Keil project
rem  Usage: double-click, or run "build_keil.bat" in any terminal.
rem  Result: build_verify.log in the same folder (0 Error/0 Warning = OK)
rem ================================================================
for %%f in ("%~dp0*.uvprojx") do (
  start /wait "UV4" "E:\k5_v5\UV4\UV4.exe" -r "%%~ff" -j0 -o "%~dp0build_verify.log"
)
exit /b 0
