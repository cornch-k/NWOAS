@echo off
setlocal
set "TARGET=C:\ProgramData\NWOAS"
if not exist "%TARGET%" mkdir "%TARGET%"
copy /y D:\NWOS.EXE "%TARGET%\NWOS.EXE"
if errorlevel 1 exit /b 21
schtasks.exe /Create /TN "NWOAS Agent" /SC ONSTART /RU SYSTEM /RL HIGHEST /TR "C:\ProgramData\NWOAS\NWOS.EXE" /F
if errorlevel 1 exit /b 22
schtasks.exe /Query /TN "NWOAS Agent" /FO LIST /V
exit /b 0
