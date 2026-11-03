@echo off
set JLINK=E:\k5_v5\ARM\Segger\JLink.exe
set SCR=D:\GitHub_Project\b1_diag.jlink
set OUT=D:\GitHub_Project\b1_diag.txt
if not exist "%JLINK%" echo [ERR] JLink.exe not found & goto end
> "%SCR%" echo device STM32F407ZE
>> "%SCR%" echo si SWD
>> "%SCR%" echo speed 1000
>> "%SCR%" echo connect
>> "%SCR%" echo halt
>> "%SCR%" echo mem8 0x20000090 1
>> "%SCR%" echo mem32 0x20000094 1
>> "%SCR%" echo mem32 0x200000C0 1
>> "%SCR%" echo mem8 0x2000009C 1
>> "%SCR%" echo mem32 0x20000008 1
>> "%SCR%" echo mem32 0x40021014 1
>> "%SCR%" echo mem32 0x40021414 1
>> "%SCR%" echo regs
>> "%SCR%" echo g
>> "%SCR%" echo sleep 1000
>> "%SCR%" echo halt
>> "%SCR%" echo mem32 0x200000C0 1
>> "%SCR%" echo mem32 0x20000094 1
>> "%SCR%" echo g
>> "%SCR%" echo exit
"%JLINK%" -CommanderScript "%SCR%" > "%OUT%" 2>&1
type "%OUT%"
:end
pause
