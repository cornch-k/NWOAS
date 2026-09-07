@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Bounded ten-minute capture after FIX11 returns; no Setup/process/disk changes.
set "MEDIA=%~d0"
set "TAG="
if not exist "%MEDIA%\NWOAS-S84.TAG" exit /b 2
set /p TAG=<"%MEDIA%\NWOAS-S84.TAG"
if not "%TAG%"=="NWOAS-S84-WINARM2-20260907" exit /b 2
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if errorlevel 1 exit /b 2
set "OUT=%MEDIA%\NWOAS-SETUP-LOGS\watch-%RANDOM%-%RANDOM%"
if exist "%OUT%" exit /b 2
md "%OUT%" >nul 2>&1
if not exist "%OUT%\" exit /b 2
for /l %%N in (1,1,40) do (
  if not exist "%MEDIA%\NWOAS-S84.TAG" exit /b 0
  call :capture %%N
  ping -n 16 127.0.0.1 >nul 2>&1
)
exit /b 0
:capture
echo Capture %1 of 40>"%OUT%\watch-status.txt"
reg query HKLM\SYSTEM\Setup\LabConfig >"%OUT%\LabConfig.txt" 2>&1
for %%F in (setupact.log setuperr.log BlueBox.log CompatData*.xml *_APPRAISER*.xml) do if exist "%SystemRoot%\Panther\%%F" copy /y "%SystemRoot%\Panther\%%F" "%OUT%\" >nul 2>&1
if exist "%SystemDrive%\$WINDOWS.~BT\Sources\Panther\" xcopy /y /i "%SystemDrive%\$WINDOWS.~BT\Sources\Panther\*.log" "%OUT%\BT" >nul 2>&1
exit /b
