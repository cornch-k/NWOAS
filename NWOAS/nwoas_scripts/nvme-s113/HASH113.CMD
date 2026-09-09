@echo off
setlocal EnableExtensions EnableDelayedExpansion
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1
if errorlevel 1 exit /b 2
if /i not "%PROCESSOR_ARCHITECTURE%"=="ARM64" exit /b 2
if /i not "%SystemDrive%"=="X:" exit /b 2
set "MEDIA=%~d0"
if not exist "%MEDIA%\NWOAS-S113.TAG" exit /b 2
set "TAG="
set /p TAG=<"%MEDIA%\NWOAS-S113.TAG"
if not "%TAG%"=="NWOAS-S113-USB-HASH-20260908" exit /b 2
if exist "%MEDIA%\NWOAS-S113.RUNNING" exit /b 2
ren "%MEDIA%\NWOAS-S113.TAG" NWOAS-S113.RUNNING
if errorlevel 1 exit /b 2
set "OUT=%MEDIA%\NWOAS-SETUP-LOGS\s113-%RANDOM%-%RANDOM%"
set "REPORT=X:\Windows\Temp\S113-%RANDOM%-%RANDOM%.TXT"
echo S113 USB diagnostic running. Output appears after both complete file reads.
>"%REPORT%" echo S113 BEGIN %DATE% %TIME%
>>"%REPORT%" echo SELFTEST
"%MEDIA%\SCRIPTS\NWHASH.EXE" "%MEDIA%\SCRIPTS\S106-ABC.TXT" ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad >>"%REPORT%" 2>&1
if errorlevel 1 goto finish
>>"%REPORT%" echo USB FIRST
"%MEDIA%\SCRIPTS\NWHASH.EXE" "%MEDIA%\sources\install.swm" 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c >>"%REPORT%" 2>&1
set "RC1=!ERRORLEVEL!"
>>"%REPORT%" echo USB SECOND
"%MEDIA%\SCRIPTS\NWHASH.EXE" "%MEDIA%\sources\install.swm" 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c >>"%REPORT%" 2>&1
set "RC2=!ERRORLEVEL!"
>>"%REPORT%" echo RESULTS first=!RC1! second=!RC2! %TIME%
if "!RC1!!RC2!"=="00" (>>"%REPORT%" echo PASS USB HASHES) else (>>"%REPORT%" echo FAILED OR MISMATCH)
:finish
>>"%REPORT%" echo S113 END %DATE% %TIME%
type "%REPORT%"
md "%OUT%"
copy /b /y "%REPORT%" "%OUT%\REPORT.TXT"
if errorlevel 1 (
 echo REPORT COPY FAILED. RAM report: %REPORT%. Take a photo before shutdown.
 pause
 exit /b 2
)
ren "%MEDIA%\NWOAS-S113.RUNNING" NWOAS-S113.FINISHED
echo Diagnostic complete. Automatic shutdown in 15 seconds.
ping -n 16 127.0.0.1 >nul
wpeutil shutdown
exit /b 0
