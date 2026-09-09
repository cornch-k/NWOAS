@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem S118: apply verified local image to the fixed Mac mini trial NTFS volume.
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if errorlevel 1 exit /b 2
if /i not "%PROCESSOR_ARCHITECTURE%"=="ARM64" exit /b 2
if /i not "%SystemDrive%"=="X:" exit /b 2
set "MEDIA=%~d0"
if not exist "%MEDIA%\NWOAS-S118.TAG" exit /b 2
set "TAG="
set /p TAG=<"%MEDIA%\NWOAS-S118.TAG"
if not "%TAG%"=="NWOAS-S118-VERIFIED-APPLY-20260908" exit /b 2
if exist "%MEDIA%\NWOAS-S118.RUNNING" exit /b 2
ren "%MEDIA%\NWOAS-S118.TAG" NWOAS-S118.RUNNING
if errorlevel 1 exit /b 2
set "OUT=%MEDIA%\NWOAS-SETUP-LOGS\s118-%RANDOM%-%RANDOM%"
set "REPORT=X:\Windows\Temp\S118-%RANDOM%-%RANDOM%.TXT"
set "DLOG=X:\Windows\Temp\S118-DISM-%RANDOM%.LOG"
set "HASH1=8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c"
set "HASH2=62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963"
>"%REPORT%" echo S118 BEGIN %DATE% %TIME%
echo S118: verify SSD source, clean old failed install, apply Windows image.
set "TARGET_OK=0"
set "DATA="
set /a FOUND=0
for %%D in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if /i not "%%D:"=="%MEDIA%" if exist "%%D:\S117SRC\VERIFIED-S117.TXT" (
 set "DATA=%%D:"
 set /a FOUND+=1
)
if not "!FOUND!"=="1" goto failed
"%MEDIA%\SCRIPTS\NWGUARD.EXE" !DATA! identify >>"%REPORT%" 2>&1
if errorlevel 1 goto failed
set "TARGET_OK=1"
if exist "!DATA!\S118-APPLY-STARTED.TXT" (
 >>"%REPORT%" echo FAIL previous S118 attempt exists; inspect before retry
 goto failed
)
>>"%REPORT%" echo REVERIFY persisted S117 copies after reboot
"%MEDIA%\SCRIPTS\NWREAD.EXE" "!DATA!\S117SRC\install.swm" !HASH1! direct >>"%REPORT%" 2>&1
if errorlevel 1 goto failed
"%MEDIA%\SCRIPTS\NWREAD.EXE" "!DATA!\S117SRC\install2.swm" !HASH2! direct >>"%REPORT%" 2>&1
if errorlevel 1 goto failed
rem Stop on top-level reparse points rather than traverse an unexpected mount.
for %%N in ("A" "Windows" "Program Files" "Program Files (Arm)" "Program Files (x86)" "ProgramData" "Users" "Recovery" "PerfLogs") do if exist "!DATA!\%%~N" (
 fsutil reparsepoint query "!DATA!\%%~N" >nul 2>&1
 if not errorlevel 1 (
  >>"%REPORT%" echo FAIL unexpected top-level reparse point %%~N
  goto failed
 )
)
>"!DATA!\S118-APPLY-STARTED.TXT" echo S118 guarded cleanup and image apply started.
if errorlevel 1 goto failed
>>"%REPORT%" echo CLEANUP existing failed Windows install directories only
for %%N in ("A" "Windows" "Program Files" "Program Files (Arm)" "Program Files (x86)" "ProgramData" "Users" "Recovery" "PerfLogs") do if exist "!DATA!\%%~N" (
 >>"%REPORT%" echo REMOVE !DATA!\%%~N
 attrib -r -s -h "!DATA!\%%~N\*" /s /d /l >>"%REPORT%" 2>&1
 rd /s /q "!DATA!\%%~N" >>"%REPORT%" 2>&1
 if exist "!DATA!\%%~N" (
  takeown /f "!DATA!\%%~N" /r /a /d Y >>"%REPORT%" 2>&1
  icacls "!DATA!\%%~N" /grant "*S-1-5-32-544:(OI)(CI)F" /t /c /l >>"%REPORT%" 2>&1
  rd /s /q "!DATA!\%%~N" >>"%REPORT%" 2>&1
 )
 if exist "!DATA!\%%~N" (
  >>"%REPORT%" echo FAIL cleanup incomplete for %%~N; DISM not started
  goto failed
 )
)
rem S106-S112 corruption evidence was archived on host before removing old inputs.
for %%N in (t1.swm t2.swm) do if exist "!DATA!\%%N" (
 del /f /q "!DATA!\%%N" >>"%REPORT%" 2>&1
 if exist "!DATA!\%%N" goto failed
)
"%MEDIA%\SCRIPTS\NWGUARD.EXE" !DATA! ready >>"%REPORT%" 2>&1
if errorlevel 1 goto failed
md "!DATA!\S118SCRATCH"
if errorlevel 1 goto failed
>>"%REPORT%" echo DISM APPLY BEGIN %DATE% %TIME%
dism /Apply-Image /ImageFile:"!DATA!\S117SRC\install.swm" /SWMFile:"!DATA!\S117SRC\install*.swm" /Index:2 /ApplyDir:!DATA!\ /CheckIntegrity /Verify /ScratchDir:"!DATA!\S118SCRATCH" /LogPath:"%DLOG%" >>"%REPORT%" 2>&1
set "RC=!ERRORLEVEL!"
>>"%REPORT%" echo DISM_APPLY_RC !RC! %DATE% %TIME%
if not "!RC!"=="0" goto failed
if not exist "!DATA!\Windows\System32\ntoskrnl.exe" goto failed
if not exist "!DATA!\Windows\System32\winload.efi" goto failed
if not exist "!DATA!\Windows\System32\config\SYSTEM" goto failed
>>"%REPORT%" echo PASS S118 DISM APPLY - installed Windows boot remains untested
>"!DATA!\S118-APPLY-PASS.TXT" echo S118 DISM apply index2 completed with CheckIntegrity and Verify.
goto finish
:failed
>>"%REPORT%" echo FAIL S118 - inspect guard, cleanup, or DISM output above
:finish
if defined DATA dir /a "!DATA!\" >>"%REPORT%" 2>&1
if exist "%DLOG%" (
 >>"%REPORT%" echo ==== DISM LOG ====
 type "%DLOG%" >>"%REPORT%"
)
>>"%REPORT%" echo S118 END %DATE% %TIME%
type "%REPORT%"
rem Retain a second report on SSD only after the verified target was modified.
if "!TARGET_OK!"=="1" if exist "!DATA!\S118-APPLY-STARTED.TXT" copy /b /y "%REPORT%" "!DATA!\S118-REPORT.TXT" >nul
if exist "%OUT%" (
 echo Report destination already exists. RAM report: %REPORT%
 pause
 exit /b 2
)
md "%OUT%"
copy /b /y "%REPORT%" "%OUT%\REPORT.TXT"
if errorlevel 1 (
 echo REPORT COPY FAILED. RAM report: %REPORT%
 pause
 exit /b 2
)
ren "%MEDIA%\NWOAS-S118.RUNNING" NWOAS-S118.FINISHED
echo Image apply test finished; inspect PASS or FAIL in report. Shutdown in 15 seconds.
ping -n 16 127.0.0.1 >nul
wpeutil shutdown
exit /b 0
