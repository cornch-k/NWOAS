@echo off
setlocal EnableExtensions
set "TOOLS="
for %%d in (C D E F G H I J) do if exist %%d:\NWESP.EXE set "TOOLS=%%d:"
if not defined TOOLS exit /b 20
%TOOLS%\NWGUARD.EXE C: identify
if errorlevel 1 exit /b 21
if not exist C:\S124-APPLY-PASS.TXT exit /b 22
if exist S:\nul exit /b 23
set "MATCHES=0"
set "MINIDISK="
for %%d in (0 1 2 3 4 5 6 7) do call :checkdisk %%d
if not "%MATCHES%"=="1" exit /b 24
>X:\S125-ESP.TXT echo select disk %MINIDISK%
>>X:\S125-ESP.TXT echo select partition 3
>>X:\S125-ESP.TXT echo detail partition
diskpart /s X:\S125-ESP.TXT
>>X:\S125-ESP.TXT echo assign letter=S
diskpart /s X:\S125-ESP.TXT
%TOOLS%\NWESP.EXE S: ready
if errorlevel 1 exit /b 25
dir S:\ /a
bcdedit /enum all
echo S125 ESP IDENTIFIED - NO BOOT FILES WRITTEN
exit /b 0
:checkdisk
>X:\S125-DISK.TXT echo select disk %1
>>X:\S125-DISK.TXT echo uniqueid disk
diskpart /s X:\S125-DISK.TXT >X:\S125-DISK.OUT 2>&1
find /i "898F172D-77BF-4007-B865-AC81A141EB3C" X:\S125-DISK.OUT >nul
if errorlevel 1 exit /b 0
set /a MATCHES+=1 >nul
set "MINIDISK=%1"
exit /b 0
