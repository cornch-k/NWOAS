@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem NWOAS S84: WinPE-only preparation and diagnostics. No installation/disk changes.
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if errorlevel 1 goto notpe
if /i not "%PROCESSOR_ARCHITECTURE%"=="ARM64" goto notpe
set "MEDIA=%~d0"
if not exist "%MEDIA%\NWOAS-S84.TAG" goto badmedia
set "TAG="
set /p TAG=<"%MEDIA%\NWOAS-S84.TAG"
if not "%TAG%"=="NWOAS-S84-WINARM2-20260907" goto badmedia
set "IMAGE="
if exist "%MEDIA%\sources\install.esd" set "IMAGE=%MEDIA%\sources\install.esd"
if exist "%MEDIA%\sources\install.wim" set "IMAGE=%MEDIA%\sources\install.wim"
if exist "%MEDIA%\sources\install.swm" set "IMAGE=%MEDIA%\sources\install.swm"
if not defined IMAGE goto badmedia
:newlog
set "OUT=%MEDIA%\NWOAS-SETUP-LOGS\run-%RANDOM%-%RANDOM%"
if exist "%OUT%" goto newlog
md "%OUT%" >nul 2>&1
if not exist "%OUT%\" goto badmedia
echo %OUT%>"%MEDIA%\NWOAS-SETUP-LOGS\LATEST.txt"
echo NWOAS-S84-BEGIN>"%OUT%\summary.txt"
ver >>"%OUT%\summary.txt"
echo Image=%IMAGE%>>"%OUT%\summary.txt"
dir "%MEDIA%\sources\install.*" >>"%OUT%\summary.txt" 2>&1
reg query HKLM\SYSTEM\Setup\LabConfig >"%OUT%\LabConfig-before.txt" 2>&1
call :capture before
set "FAILED=0"
for %%K in (BypassTPMCheck BypassSecureBootCheck BypassRAMCheck BypassCPUCheck BypassStorageCheck) do call :setkey %%K
reg query HKLM\SYSTEM\Setup\LabConfig >"%OUT%\LabConfig-after.txt" 2>&1
type "%OUT%\LabConfig-after.txt"
if "%FAILED%"=="1" goto failed
echo LABCONFIG_READBACK_PASS>>"%OUT%\summary.txt"
echo LABCONFIG_READBACK_PASS
md "%SystemDrive%\NWOAS-S84-WATCH.lock" >nul 2>&1
if not errorlevel 1 start "" /b cmd.exe /d /c call "%~dp0WATCH11.CMD" >nul 2>&1
echo Image: %IMAGE%
echo Logs: %OUT%
echo Return to Setup, go Back, then try Next again. No reboot is needed.
echo This confirms registry values only, not that the installation block is fixed.
echo If the block remains, run COLLECT11.CMD in this same SCRIPTS folder.
exit /b 0
:setkey
reg add HKLM\SYSTEM\Setup\LabConfig /v %1 /t REG_DWORD /d 1 /f >>"%OUT%\registry-write.txt" 2>&1
if errorlevel 1 set "FAILED=1"
set "READBACK=0"
for /f "tokens=1,2,3" %%A in ('reg query HKLM\SYSTEM\Setup\LabConfig /v %1 2^>nul') do if /i "%%A"=="%1" if /i "%%B"=="REG_DWORD" if /i "%%C"=="0x1" set "READBACK=1"
if not "%READBACK%"=="1" set "FAILED=1"
exit /b
:capture
md "%OUT%\%1" >nul 2>&1
for %%F in (setupact.log setuperr.log BlueBox.log CompatData*.xml *_APPRAISER*.xml) do (
  if exist "%SystemRoot%\Panther\%%F" copy /y "%SystemRoot%\Panther\%%F" "%OUT%\%1\" >nul 2>&1
)
if exist "%SystemDrive%\$WINDOWS.~BT\Sources\Panther\" xcopy /y /i "%SystemDrive%\$WINDOWS.~BT\Sources\Panther\*.log" "%OUT%\%1\BT" >nul 2>&1
for %%F in ("%OUT%\%1\*.log") do if exist "%%~fF" (
  for %%P in (TPM SecureBoot LabConfig Compat HardBlock requirement 0xC190 0x8007) do find /i "%%P" "%%~fF" >>"%OUT%\%1\matches.txt" 2>nul
)
exit /b
:failed
echo STOP: registry readback failed. See %OUT%
exit /b 3
:notpe
echo STOP: this script requires ARM64 Windows PE.
exit /b 2
:badmedia
echo STOP: expected marked Windows installation media or writable log folder missing.
exit /b 2
