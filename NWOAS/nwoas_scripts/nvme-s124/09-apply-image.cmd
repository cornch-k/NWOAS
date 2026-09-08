@echo off
set "TOOLS="
set "TARGET="
for %%d in (C D E F G H I J) do if exist %%d:\NWAGENT.EXE set "TOOLS=%%d:"
for %%d in (C D E F G H I J) do if exist %%d:\S117SRC set "TARGET=%%d:"
if not defined TOOLS exit /b 21
if not defined TARGET exit /b 22
%TOOLS%\NWGUARD.EXE %TARGET% identify
if errorlevel 1 exit /b 23
if not exist X:\S124-SOURCE-VERIFIED.TAG exit /b 24
if not exist X:\S124-CLEANUP-PASS.TAG exit /b 25
if not exist %TARGET%\S124-APPLY-STARTED.TXT exit /b 26
if exist %TARGET%\Windows exit /b 27
%TOOLS%\NWGUARD.EXE %TARGET% ready
if errorlevel 1 exit /b 28
md %TARGET%\S124SCRATCH
if not exist %TARGET%\S124SCRATCH exit /b 29
echo S124 DISM APPLY BEGIN
dism.exe /English /Apply-Image /ImageFile:%TARGET%\S117SRC\install.swm /SWMFile:%TARGET%\S117SRC\install*.swm /Index:2 /ApplyDir:%TARGET%\ /CheckIntegrity /Verify /ScratchDir:%TARGET%\S124SCRATCH /LogPath:X:\S124-DISM.LOG
set "DISMRC=%errorlevel%"
echo S124 DISM EXIT %DISMRC%
if not "%DISMRC%"=="0" exit /b %DISMRC%
if not exist %TARGET%\Windows\System32\ntoskrnl.exe exit /b 31
if not exist %TARGET%\Windows\System32\winload.efi exit /b 32
if not exist %TARGET%\Windows\System32\config\SYSTEM exit /b 33
%TOOLS%\NWVFLUSH.EXE %TARGET% flush
if errorlevel 1 exit /b 34
>%TARGET%\S124-APPLY-PASS.TXT echo S124 image apply succeeded; installed OS boot is untested.
echo S124 IMAGE APPLY PASS - INSTALLED OS BOOT UNTESTED
exit /b 0
