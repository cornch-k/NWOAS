@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem NWOAS S100: unattended WinPE install of Windows 11 Pro (index 2) into the partitions
rem created in S98. Target disk AND partition numbers are located by partition GUID
rem (uniqueid partition), never by fixed numbers. Runs once: TAG -> RUNNING -> FINISHED.
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if errorlevel 1 exit /b 2
if /i not "%PROCESSOR_ARCHITECTURE%"=="ARM64" exit /b 2
set "MEDIA=%~d0"
if not exist "%MEDIA%\NWOAS-S100.TAG" exit /b 2
set "TAG="
set /p TAG=<"%MEDIA%\NWOAS-S100.TAG"
if not "%TAG%"=="NWOAS-S100-WINARM2-20260907" exit /b 2
if not exist "%MEDIA%\sources\install.swm" exit /b 2
ren "%MEDIA%\NWOAS-S100.TAG" NWOAS-S100.RUNNING
if exist "%MEDIA%\NWOAS-S100.TAG" exit /b 2
if not exist "%MEDIA%\NWOAS-SETUP-LOGS\" md "%MEDIA%\NWOAS-SETUP-LOGS"
:newlog
set "OUT=%MEDIA%\NWOAS-SETUP-LOGS\s100-%RANDOM%-%RANDOM%"
if exist "%OUT%" goto newlog
md "%OUT%"
set "LOG=%OUT%\s100.log"
>"%LOG%" echo NWOAS-S100-BEGIN %DATE% %TIME%
rem diskpart cannot print partition GUIDs; identify by byte offset from `detail partition`
rem (S95/S98 layout, 4096-byte LBAs): ESP starts LBA 53839104, Windows LBA 53920000.
set "WINGUID=220856320000"
set "ESPGUID=220524969984"
set "TARGET="
set "WINPART="
set "ESPDISK="
set "ESPPART="
for /l %%T in (1,1,18) do (
  if not defined TARGET (
    for %%N in (0 1 2 3) do if not defined TARGET call :probe %%N
    if not defined TARGET (>>"%LOG%" echo probe try %%T: target not found yet & ping -n 11 127.0.0.1 >nul)
  )
)
if not defined TARGET (
  >>"%LOG%" echo FAILED: no disk carries a partition at byte offset %WINGUID%
  for %%N in (0 1 2 3) do (>>"%LOG%" echo ---- dp-detail-%%N & type "%OUT%\dp-detail-%%N.txt" >>"%LOG%" 2>nul)
  goto failed
)
>>"%LOG%" echo Target disk !TARGET! Windows partition !WINPART! ESP disk !ESPDISK! partition !ESPPART!
if not "!ESPDISK!"=="!TARGET!" (
  >>"%LOG%" echo FAILED: ESP at offset %ESPGUID% not found on target disk
  goto failed
)
if not defined WINPART goto failed
if not defined ESPPART goto failed
(
  echo select disk !TARGET!
  echo select partition !WINPART!
  echo format fs=ntfs quick label=Windows
  echo assign letter=W
  echo select partition !ESPPART!
  echo assign letter=S
  echo list partition
  echo list volume
)>"%OUT%\dp-prep.txt"
diskpart /s "%OUT%\dp-prep.txt">"%OUT%\dp-prep.log" 2>&1
type "%OUT%\dp-prep.log">>"%LOG%"
if not exist W:\ (>>"%LOG%" echo FAILED: W: missing after diskpart & goto failed)
if not exist S:\ (>>"%LOG%" echo FAILED: S: missing after diskpart & goto failed)
>>"%LOG%" echo DISM begin %TIME%
dism /Apply-Image /ImageFile:"%MEDIA%\sources\install.swm" /SWMFile:"%MEDIA%\sources\install*.swm" /Index:2 /ApplyDir:W:\ /LogPath:"%OUT%\dism.log">"%OUT%\dism-out.txt" 2>&1
set "RC=%ERRORLEVEL%"
>>"%LOG%" echo DISM end %TIME% rc=%RC%
if not "%RC%"=="0" (>>"%LOG%" echo FAILED: dism rc=%RC% & goto failed)
if not exist W:\Windows\System32\ntoskrnl.exe (>>"%LOG%" echo FAILED: ntoskrnl missing & goto failed)
bcdboot W:\Windows /s S: /f UEFI>"%OUT%\bcdboot.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\bcdboot.txt">>"%LOG%"
>>"%LOG%" echo bcdboot rc=%RC%
if not "%RC%"=="0" (>>"%LOG%" echo FAILED: bcdboot rc=%RC% & goto failed)
dir /s /b S:\EFI>>"%LOG%" 2>&1
>>"%LOG%" echo NWOAS-S100-DONE %DATE% %TIME%
copy /y "%LOG%" "%MEDIA%\NWOAS-S100.DONE">nul
ren "%MEDIA%\NWOAS-S100.RUNNING" NWOAS-S100.FINISHED
exit /b 0
:failed
copy /y "%LOG%" "%MEDIA%\NWOAS-S100.FAILED">nul
ren "%MEDIA%\NWOAS-S100.RUNNING" NWOAS-S100.FINISHED
exit /b 1
:probe
>"%OUT%\dp-q%1.txt" echo select disk %1
for /l %%P in (1,1,8) do (
  >>"%OUT%\dp-q%1.txt" echo select partition %%P
  >>"%OUT%\dp-q%1.txt" echo detail partition
)
diskpart /s "%OUT%\dp-q%1.txt">"%OUT%\dp-detail-%1.txt" 2>&1
set "CURP="
for /f "usebackq delims=" %%L in ("%OUT%\dp-detail-%1.txt") do (
  set "line=%%L"
  rem the select-partition reply starts with the partition number; remember it
  for /f "tokens=1" %%A in ("%%L") do for /l %%P in (1,1,8) do if "%%A"=="%%P" set "CURP=%%P"
  if not "!line:%WINGUID%=!"=="!line!" (set "TARGET=%1" & set "WINPART=!CURP!")
  if not "!line:%ESPGUID%=!"=="!line!" (set "ESPDISK=%1" & set "ESPPART=!CURP!")
)
exit /b 0
