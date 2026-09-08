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
if exist %TARGET%\S124-APPLY-STARTED.TXT exit /b 25
for %%n in ("A" "Windows" "Program Files" "Program Files (Arm)" "Program Files (x86)" "ProgramData" "Users" "Recovery" "PerfLogs") do if exist "%TARGET%\%%~n" (
 fsutil reparsepoint query "%TARGET%\%%~n" >nul 2>&1
 if not errorlevel 1 exit /b 26
)
>"%TARGET%\S124-APPLY-STARTED.TXT" echo S124 guarded cleanup and apply started.
if not exist "%TARGET%\S124-APPLY-STARTED.TXT" exit /b 27
for %%n in ("A" "Windows" "Program Files" "Program Files (Arm)" "Program Files (x86)" "ProgramData" "Users" "Recovery" "PerfLogs") do (
 call :remove "%%~n"
 if errorlevel 1 exit /b 28
)
for %%n in (t1.swm t2.swm) do if exist "%TARGET%\%%n" (
 del /f /q "%TARGET%\%%n"
 if exist "%TARGET%\%%n" exit /b 29
)
%TOOLS%\NWGUARD.EXE %TARGET% ready
if errorlevel 1 exit /b 30
%TOOLS%\NWVFLUSH.EXE %TARGET% flush
if errorlevel 1 exit /b 31
>X:\S124-CLEANUP-PASS.TAG echo S124 cleanup complete this boot
echo S124 CLEANUP PASS - IMAGE APPLY NOT STARTED
exit /b 0
:remove
if not exist "%TARGET%\%~1" exit /b 0
echo S124 REMOVE "%TARGET%\%~1"
attrib -r -s -h "%TARGET%\%~1" /l >nul 2>&1
rd /s /q "%TARGET%\%~1" >X:\S124-REMOVE.LOG 2>&1
if not exist "%TARGET%\%~1" exit /b 0
takeown /f "%TARGET%\%~1" /r /a /d Y >X:\S124-ACL.LOG 2>&1
icacls "%TARGET%\%~1" /grant "*S-1-5-32-544:(OI)(CI)F" /t /c /l >>X:\S124-ACL.LOG 2>&1
attrib -r -s -h "%TARGET%\%~1\*" /s /d /l >nul 2>&1
rd /s /q "%TARGET%\%~1" >X:\S124-REMOVE.LOG 2>&1
if exist "%TARGET%\%~1" (
 type X:\S124-REMOVE.LOG
 exit /b 1
)
exit /b 0
