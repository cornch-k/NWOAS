@echo off
setlocal EnableExtensions DisableDelayedExpansion
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if errorlevel 1 exit /b 2
set "MEDIA=%~d0"
set "TAG="
if not exist "%MEDIA%\NWOAS-S84.TAG" exit /b 2
set /p TAG=<"%MEDIA%\NWOAS-S84.TAG"
if not "%TAG%"=="NWOAS-S84-WINARM2-20260907" exit /b 2
set "OUT=%MEDIA%\NWOAS-SETUP-LOGS\capture-%RANDOM%-%RANDOM%"
if exist "%OUT%" exit /b 2
md "%OUT%"
if not exist "%OUT%\" exit /b 2
echo %OUT%>"%MEDIA%\NWOAS-SETUP-LOGS\LATEST.txt"
reg query HKLM\SYSTEM\Setup\LabConfig >"%OUT%\LabConfig.txt" 2>&1
reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v CurrentBuildNumber >"%OUT%\build.txt" 2>&1
for %%F in (setupact.log setuperr.log BlueBox.log CompatData*.xml *_APPRAISER*.xml) do if exist "%SystemRoot%\Panther\%%F" copy /y "%SystemRoot%\Panther\%%F" "%OUT%\" >nul 2>&1
if exist "%SystemDrive%\$WINDOWS.~BT\Sources\Panther\" xcopy /y /i "%SystemDrive%\$WINDOWS.~BT\Sources\Panther\*.log" "%OUT%\BT" >nul 2>&1
for %%F in ("%OUT%\*.log") do if exist "%%~fF" for %%P in (TPM SecureBoot LabConfig Compat HardBlock requirement 0xC190 0x8007) do find /i "%%P" "%%~fF" >>"%OUT%\matches.txt" 2>nul
echo list disk>"%OUT%\list-only.txt"
echo list volume>>"%OUT%\list-only.txt"
diskpart /s "%OUT%\list-only.txt" >"%OUT%\disks.txt" 2>&1
echo CAPTURE_COMPLETE: %OUT%
if exist "%OUT%\matches.txt" type "%OUT%\matches.txt"
exit /b 0
