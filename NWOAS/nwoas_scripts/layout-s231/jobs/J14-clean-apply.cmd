@echo off
echo S231 J14 CLEAN PARTIAL IMAGE ON C: THEN DISM FROM SSD
if not exist C:\S231RC\install.swm exit /b 20
dir C:\ /a
for %%n in ("Windows" "Program Files" "Program Files (Arm)" "Program Files (x86)" "ProgramData" "Users" "Recovery" "PerfLogs" "S231SCRATCH") do call :remove "%%~n"
if errorlevel 1 exit /b 28
if exist C:\Windows exit /b 29
dir C:\ /a
md C:\S231SCRATCH
echo S231 DISM APPLY BEGIN
dism.exe /English /Apply-Image /ImageFile:C:\S231RC\install.swm /SWMFile:C:\S231RC\install*.swm /Index:2 /ApplyDir:C:\ /CheckIntegrity /Verify /ScratchDir:C:\S231SCRATCH /LogPath:X:\S231-DISM2.LOG
set "DISMRC=%errorlevel%"
echo S231 DISM EXIT %DISMRC%
if not "%DISMRC%"=="0" exit /b %DISMRC%
if not exist C:\Windows\System32\ntoskrnl.exe exit /b 61
if not exist C:\Windows\System32\winload.efi exit /b 62
if not exist C:\Windows\System32\config\SYSTEM exit /b 63
rd /s /q C:\S231SCRATCH
dir C:\ /a
echo S231 J14 IMAGE APPLY PASS - INSTALLED OS BOOT UNTESTED
exit /b 0
:remove
if not exist "C:\%~1" exit /b 0
echo REMOVE "C:\%~1"
attrib -r -s -h "C:\%~1" /l >/dev/null 2>&1
rd /s /q "C:\%~1" >X:\S231-REMOVE.LOG 2>&1
if not exist "C:\%~1" exit /b 0
takeown /f "C:\%~1" /r /a /d Y >X:\S231-ACL.LOG 2>&1
icacls "C:\%~1" /grant "*S-1-5-32-544:(OI)(CI)F" /t /c /l >>X:\S231-ACL.LOG 2>&1
attrib -r -s -h "C:\%~1\*" /s /d /l >/dev/null 2>&1
rd /s /q "C:\%~1" >X:\S231-REMOVE.LOG 2>&1
if exist "C:\%~1" (
 type X:\S231-REMOVE.LOG
 exit /b 1
)
exit /b 0
