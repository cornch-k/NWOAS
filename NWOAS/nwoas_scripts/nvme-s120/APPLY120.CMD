@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem S120: apply verified local image to the fixed Mac mini trial NTFS volume.
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if errorlevel 1 exit /b 2
if /i not "%PROCESSOR_ARCHITECTURE%"=="ARM64" exit /b 2
if /i not "%SystemDrive%"=="X:" exit /b 2
set "MEDIA=%~d0"
if not exist "%MEDIA%\NWOAS-S120.TAG" exit /b 2
set "TAG="
set /p TAG=<"%MEDIA%\NWOAS-S120.TAG"
if not "%TAG%"=="NWOAS-S120-VERIFIED-APPLY-20260908" exit /b 2
if exist "%MEDIA%\NWOAS-S120.RUNNING" exit /b 2
ren "%MEDIA%\NWOAS-S120.TAG" NWOAS-S120.RUNNING
if errorlevel 1 exit /b 2
set "OUT=%MEDIA%\NWOAS-SETUP-LOGS\s120-%RANDOM%-%RANDOM%"
set "REPORT=X:\Windows\Temp\S120-%RANDOM%-%RANDOM%.TXT"
set "DLOG=X:\Windows\Temp\S120-DISM-%RANDOM%.LOG"
set "HASH1=8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c"
set "HASH2=62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963"
>"%REPORT%" echo S120 BEGIN %DATE% %TIME%
echo S120: verify SSD source, clean old failed install, apply Windows image.
set "TARGET_OK=0"
set "DATA="
set /a FOUND=0
rem Wait for WinPE automount; use actual source files, not a prior text marker.
for /l %%T in (1,1,12) do if not defined DATA (
 set /a FOUND=0
 for %%D in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if /i not "%%D:"=="%MEDIA%" if exist "%%D:\S117SRC\install.swm" if exist "%%D:\S117SRC\install2.swm" (
  set "DATA=%%D:"
  set /a FOUND+=1
 )
 >>"%REPORT%" echo DISCOVERY attempt=%%T candidates=!FOUND! target=!DATA!
 if not defined DATA ping -n 3 127.0.0.1 >nul
)
rem If automount did not expose the partition, assign only the independently identified GPT target.
if not defined DATA if not exist R:\ for %%N in (0 1 2 3) do if not exist R:\ call :mount_known %%N
if not defined DATA (
 set /a FOUND=0
 for %%D in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if /i not "%%D:"=="%MEDIA%" if exist "%%D:\S117SRC\install.swm" if exist "%%D:\S117SRC\install2.swm" (
  set "DATA=%%D:"
  set /a FOUND+=1
 )
 >>"%REPORT%" echo POST_MOUNT candidates=!FOUND! target=!DATA!
)
if not "!FOUND!"=="1" (
 >>"%REPORT%" echo FAIL expected exactly one volume containing both source files
 for %%D in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist %%D:\ (
  dir /a %%D:\ >>"%REPORT%" 2>&1
  if exist %%D:\S117SRC dir /a %%D:\S117SRC >>"%REPORT%" 2>&1
 )
 >X:\Windows\Temp\S120-DISKS.TXT echo list disk
 >>X:\Windows\Temp\S120-DISKS.TXT echo list volume
 diskpart /s X:\Windows\Temp\S120-DISKS.TXT >>"%REPORT%" 2>&1
 goto failed
)
>>"%REPORT%" echo GUARD_BEGIN !DATA!
"%MEDIA%\SCRIPTS\NWGUARD.EXE" !DATA! identify >>"%REPORT%" 2>&1
if errorlevel 1 goto failed
set "TARGET_OK=1"
if exist "!DATA!\S118-APPLY-STARTED.TXT" (
 >>"%REPORT%" echo FAIL previous S118 changed target; inspect before retry
 goto failed
)
if exist "!DATA!\S119-APPLY-STARTED.TXT" (
 >>"%REPORT%" echo FAIL previous S119 changed target
 goto failed
)
if exist "!DATA!\S120-APPLY-STARTED.TXT" (
 >>"%REPORT%" echo FAIL previous S120 attempt exists; inspect before retry
 goto failed
)
>>"%REPORT%" echo VERIFY PREFIX AND REPAIR install2.swm EOF ONLY IF CONTENT MATCHES
"%MEDIA%\SCRIPTS\NWTRIM.EXE" "!DATA!\S117SRC\install2.swm" !HASH2! direct >>"%REPORT%" 2>&1
set "TRIM_RC=!ERRORLEVEL!"
>>"%REPORT%" echo TRIM_RC !TRIM_RC!
if "!TRIM_RC!"=="1" (
 >>"%REPORT%" echo Prefix mismatch; recopy only install2.swm once with xcopy /J
 xcopy "%MEDIA%\sources\install2.swm" "!DATA!\S117SRC\" /J /Y >>"%REPORT%" 2>&1
 set "RC=!ERRORLEVEL!"
 >>"%REPORT%" echo RECOPY_RC !RC!
 if not "!RC!"=="0" goto failed
 "%MEDIA%\SCRIPTS\NWTRIM.EXE" "!DATA!\S117SRC\install2.swm" !HASH2! direct >>"%REPORT%" 2>&1
 set "TRIM_RC=!ERRORLEVEL!"
 >>"%REPORT%" echo TRIM_RETRY_RC !TRIM_RC!
)
if not "!TRIM_RC!"=="0" goto failed
"%MEDIA%\SCRIPTS\NWVFLUSH.EXE" !DATA! flush >>"%REPORT%" 2>&1
if errorlevel 1 goto failed
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
>"!DATA!\S120-APPLY-STARTED.TXT" echo S120 guarded cleanup and image apply started.
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
md "!DATA!\S120SCRATCH"
if errorlevel 1 goto failed
>>"%REPORT%" echo DISM APPLY BEGIN %DATE% %TIME%
dism /Apply-Image /ImageFile:"!DATA!\S117SRC\install.swm" /SWMFile:"!DATA!\S117SRC\install*.swm" /Index:2 /ApplyDir:!DATA!\ /CheckIntegrity /Verify /ScratchDir:"!DATA!\S120SCRATCH" /LogPath:"%DLOG%" >>"%REPORT%" 2>&1
set "RC=!ERRORLEVEL!"
>>"%REPORT%" echo DISM_APPLY_RC !RC! %DATE% %TIME%
if not "!RC!"=="0" goto failed
if not exist "!DATA!\Windows\System32\ntoskrnl.exe" goto failed
if not exist "!DATA!\Windows\System32\winload.efi" goto failed
if not exist "!DATA!\Windows\System32\config\SYSTEM" goto failed
"%MEDIA%\SCRIPTS\NWVFLUSH.EXE" !DATA! flush >>"%REPORT%" 2>&1
if errorlevel 1 goto failed
>>"%REPORT%" echo PASS S120 DISM APPLY - installed Windows boot remains untested
>"!DATA!\S120-APPLY-PASS.TXT" echo S120 DISM apply index2 completed with CheckIntegrity and Verify.
goto finish
:failed
>>"%REPORT%" echo FAIL S120 - inspect guard, cleanup, or DISM output above
:finish
if defined DATA dir /a "!DATA!\" >>"%REPORT%" 2>&1
if exist "%DLOG%" (
 >>"%REPORT%" echo ==== DISM LOG ====
 type "%DLOG%" >>"%REPORT%"
)
>>"%REPORT%" echo S120 END %DATE% %TIME%
type "%REPORT%"
rem Retain a second report on SSD only after the verified target was modified.
if "!TARGET_OK!"=="1" (
 copy /b /y "%REPORT%" "!DATA!\S120-REPORT.TXT" >nul
 "%MEDIA%\SCRIPTS\NWVFLUSH.EXE" !DATA! flush >>"%REPORT%" 2>&1
 >>"%REPORT%" echo FINAL_VOLUME_FLUSH_RC !ERRORLEVEL!
)
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
ren "%MEDIA%\NWOAS-S120.RUNNING" NWOAS-S120.FINISHED
echo Image apply test finished; inspect PASS or FAIL in report. Shutdown in 15 seconds.
ping -n 16 127.0.0.1 >nul
wpeutil shutdown
exit /b 0

:mount_known
>"X:\Windows\Temp\S120-QUERY.TXT" echo select disk %1
>>"X:\Windows\Temp\S120-QUERY.TXT" echo uniqueid disk
>>"X:\Windows\Temp\S120-QUERY.TXT" echo select partition 5
>>"X:\Windows\Temp\S120-QUERY.TXT" echo detail partition
diskpart /s "X:\Windows\Temp\S120-QUERY.TXT" >"X:\Windows\Temp\S120-QUERY.LOG" 2>&1
type "X:\Windows\Temp\S120-QUERY.LOG" >>"%REPORT%"
findstr /i /c:"898f172d-77bf-4007-b865-ac81a141eb3c" "X:\Windows\Temp\S120-QUERY.LOG" >nul
if errorlevel 1 exit /b 0
findstr /c:"220856320000" "X:\Windows\Temp\S120-QUERY.LOG" >nul
if errorlevel 1 exit /b 0
findstr /i /c:"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7" "X:\Windows\Temp\S120-QUERY.LOG" >nul
if errorlevel 1 exit /b 0
if exist R:\ exit /b 0
>"X:\Windows\Temp\S120-MOUNT.TXT" echo select disk %1
>>"X:\Windows\Temp\S120-MOUNT.TXT" echo select partition 5
>>"X:\Windows\Temp\S120-MOUNT.TXT" echo assign letter=R
diskpart /s "X:\Windows\Temp\S120-MOUNT.TXT" >>"%REPORT%" 2>&1
ping -n 3 127.0.0.1 >nul
exit /b 0
